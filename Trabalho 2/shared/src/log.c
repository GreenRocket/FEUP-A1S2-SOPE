#include "sope.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <limits.h>
#include <pthread.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <errno.h>
#include <ctype.h> // isspace
#include <fcntl.h> // open

static const char *OP_TYPE_STR[] = {
	[OP_CREATE_ACCOUNT] = "CREATE",
	[OP_BALANCE] = "BALANCE",
	[OP_TRANSFER] = "TRANSFER",
	[OP_SHUTDOWN] = "SHUTDOWN"};

static const char *RC_STR[] = {
	[RC_OK] = "OK",
	[RC_SRV_DOWN] = "SRV_DOWN",
	[RC_SRV_TIMEOUT] = "SRV_TIMEOUT",
	[RC_USR_DOWN] = "USR_DOWN",
	[RC_LOGIN_FAIL] = "LOGIN_FAIL",
	[RC_OP_NALLOW] = "OP_NALLOW",
	[RC_ID_IN_USE] = "ID_IN_USE",
	[RC_ID_NOT_FOUND] = "ID_NOT_FOUND",
	[RC_SAME_ID] = "SAME_ID",
	[RC_NO_FUNDS] = "NO_FUNDS",
	[RC_TOO_HIGH] = "TOO_HIGH",
	[RC_BAD_REQ_ARGS] = "BAD_REQ_ARGS",
	[RC_OTHER] = "OTHER"};

static const char *SYNC_MECH_STR[] = {
	[SYNC_OP_MUTEX_LOCK] = "MUTEX_LOCK",
	[SYNC_OP_MUTEX_TRYLOCK] = "MUTEX_TRYLOCK",
	[SYNC_OP_MUTEX_UNLOCK] = "MUTEX_UNLOCK",
	[SYNC_OP_COND_SIGNAL] = "COND_SIGNAL",
	[SYNC_OP_COND_WAIT] = "COND_WAIT",
	[SYNC_OP_SEM_INIT] = "SEM_INIT",
	[SYNC_OP_SEM_POST] = "SEM_POST",
	[SYNC_OP_SEM_WAIT] = "SEM_WAIT"};

static const char *SYNC_ROLE_STR[] = {
	[SYNC_ROLE_ACCOUNT] = "ACCOUNT",
	[SYNC_ROLE_CONSUMER] = "CONSUMER",
	[SYNC_ROLE_PRODUCER] = "PRODUCER"};

static int atomicPrintf(int fd, const char *format, ...);

static char *logBaseRequestInfo(int id, char *str, const tlv_request_t *request);

static char *logBaseReplyInfo(int id, char *str, const tlv_reply_t *reply);

static int logBankOfficeInfo(int fd, int id, pthread_t tid, bool open);

int logRequest(int fd, int id, const tlv_request_t *request)
{
	if (!request)
		return -1;

	char buffer[PIPE_BUF];

	switch (request->type)
	{
	case OP_CREATE_ACCOUNT:
		return atomicPrintf(fd, "%s %0*d %*u€ \"%s\"\n",
							logBaseRequestInfo(id, buffer, request),
							WIDTH_ACCOUNT, request->value.create.account_id,
							WIDTH_BALANCE, request->value.create.balance,
							request->value.create.password);
	case OP_BALANCE:
		return atomicPrintf(fd, "%s\n", logBaseRequestInfo(id, buffer, request));
	case OP_TRANSFER:
		return atomicPrintf(fd, "%s %0*d %*u€\n",
							logBaseRequestInfo(id, buffer, request),
							WIDTH_ACCOUNT, request->value.transfer.account_id,
							WIDTH_BALANCE, request->value.transfer.amount);
	case OP_SHUTDOWN:
		return atomicPrintf(fd, "%s\n", logBaseRequestInfo(id, buffer, request));
	default:
		break;
	}

	return -2;
}

int logReply(int fd, int id, const tlv_reply_t *reply)
{
	if (!reply)
		return -1;

	char buffer[PIPE_BUF];

	switch (reply->type)
	{
	case OP_CREATE_ACCOUNT:
		return atomicPrintf(fd, "%s\n", logBaseReplyInfo(id, buffer, reply));
	case OP_BALANCE:
		return atomicPrintf(fd, "%s %*d€\n", logBaseReplyInfo(id, buffer, reply),
							WIDTH_BALANCE, reply->value.balance.balance);
	case OP_TRANSFER:
		return atomicPrintf(fd, "%s %*d€\n", logBaseReplyInfo(id, buffer, reply),
							WIDTH_BALANCE, reply->value.transfer.balance);
	case OP_SHUTDOWN:
		return atomicPrintf(fd, "%s %d\n", logBaseReplyInfo(id, buffer, reply),
							reply->value.shutdown.active_offices);
	default:
		break;
	}

	return -2;
}

int logBankOfficeOpen(int fd, int id, pthread_t tid)
{
	return logBankOfficeInfo(fd, id, tid, true);
}

int logBankOfficeClose(int fd, int id, pthread_t tid)
{
	return logBankOfficeInfo(fd, id, tid, false);
}

int logAccountCreation(int fd, int id, const bank_account_t *account)
{
	if (!account)
		return -1;

	return atomicPrintf(fd, "I - %0*d - %0*d %*s ...%*s\n",
						WIDTH_ID, id, WIDTH_ACCOUNT, account->account_id,
						SALT_LEN, account->salt, WIDTH_HASH, &account->hash[HASH_LEN - WIDTH_HASH]);
}

int logSyncMech(int fd, int id, sync_mech_op_t smo, sync_role_t role, int sid)
{
	return atomicPrintf(fd, "S - %0*d - %c%0*d %s\n",
						WIDTH_ID, id, SYNC_ROLE_STR[role][0], WIDTH_ID, sid, SYNC_MECH_STR[smo]);
}

int logSyncMechSem(int fd, int id, sync_mech_op_t smo, sync_role_t role, int sid, int val)
{
	return atomicPrintf(fd, "S - %0*d - %c%0*d %s [val=%d]\n",
						WIDTH_ID, id, SYNC_ROLE_STR[role][0], WIDTH_ID, sid, SYNC_MECH_STR[smo], val);
}

int logDelay(int fd, int id, uint32_t delay_ms)
{
	return atomicPrintf(fd, "A - %0*d - [%*u ms]\n", WIDTH_ID, id, WIDTH_DELAY, delay_ms);
}

int logSyncDelay(int fd, int id, int sid, uint32_t delay_ms)
{
	return atomicPrintf(fd, "A - %0*d - %c%0*d [%*u ms]\n",
						WIDTH_ID, id, SYNC_ROLE_STR[SYNC_ROLE_ACCOUNT][0],
						WIDTH_ID, sid, WIDTH_DELAY, delay_ms);
}

/*
 * Ancillary functions
 */

static int atomicPrintf(int fd, const char *format, ...) {
  char buffer[PIPE_BUF];
  va_list args;
  int ret;

  va_start(args, format);
  ret = vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);

  write(fd, buffer, strlen(buffer));
  
  return ret;
}

static char *logBaseRequestInfo(int id, char *str, const tlv_request_t *request)
{
	const int pass_padding = MAX_PASSWORD_LEN - (int)strlen(request->value.header.password);
	const bool received = (id != getpid());

	snprintf(str, PIPE_BUF, "%c - %0*d - [%*d bytes] %0*d (%0*d, \"%s\")%*s [%*u ms] %-*s",
			 (received ? 'R' : 'E'), WIDTH_ID, id, WIDTH_TLV_LEN, request->length,
			 WIDTH_ID, request->value.header.pid, WIDTH_ACCOUNT, request->value.header.account_id,
			 request->value.header.password, pass_padding, "",
			 WIDTH_DELAY, request->value.header.op_delay_ms, WIDTH_OP, OP_TYPE_STR[request->type]);

	return str;
}

static char *logBaseReplyInfo(int id, char *str, const tlv_reply_t *reply)
{
	const bool received = (id == getpid());

	snprintf(str, PIPE_BUF, "%c - %0*d - [%*d bytes] %0*d %*s %*s", (received ? 'R' : 'E'),
			 WIDTH_ID, id, WIDTH_TLV_LEN, reply->length,
			 WIDTH_ACCOUNT, reply->value.header.account_id,
			 WIDTH_OP, OP_TYPE_STR[reply->type], WIDTH_RC, RC_STR[reply->value.header.ret_code]);

	return str;
}

static int logBankOfficeInfo(int fd, int id, pthread_t tid, bool open)
{
	return atomicPrintf(fd, "I - %0*d - %-*s %lu\n",
						WIDTH_ID, id, WIDTH_STARTEND, (open ? "OPEN" : "CLOSE"), tid);
}

////////////////////////////////////////////////////////////////////////////////////////

/**
 * @brief Fill provided buffer with current day and time
 * 
 * @param dateString buffer that will be filled
 * @param size size of the buffer
 */
void getDateTimeNow(char dateString[], size_t size)
{
	time_t t = time(NULL);
	strftime(dateString, size, "%A %d-%m-%Y %T", localtime(&t));
}

const char *getRCSTR(const int ret_code)
{
	return RC_STR[ret_code];
}

ret_code_t return_error(ret_code_t code, const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	vfprintf(stderr, fmt, args);
	va_end(args);
	return code;
}

int verifyStringContainsWhitespaces(const char *target)
{
	int i = 0;
	char c = target[i];
	while (c != '\0')
	{
		if (isspace(c) != 0)
			return 1;
		c = target[++i];
	}
	return 0;
}

int verifyUnsignedArg(unsigned *result, const char *arg, size_t lowerBound, size_t upperBound)
{
	char *p;
	errno = 0;

	*result = strtoul(arg, &p, 10);
	if (errno != 0 || *p != '\0')
	{
		fprintf(stderr, "verifyUnsignedArg: problems with buffer\n");
		return -1;
	}
	else if (*result < lowerBound || *result > upperBound)
	{
		//fprintf(stderr, "verifyUnsignedArg: out of bounds\n");
		return -1;
	}

	return 0;
}

int verifyStringArg(const char **result, const char *arg, size_t lowerBound, size_t upperBound)
{
	unsigned arg_len = strlen(arg);
	if (arg_len < lowerBound || arg_len > upperBound)
		return -1;

	*result = arg;
	return 0;
}

void msleep(uint32_t delay)
{
	if (delay > 0)
	{
		/*
      	atraso é ms
      	segundo o "man" usleep aceita [0, 1000000] us
     	constanst.h diz MAX_OP_DELAY_MS = 99999 -> 99999*1000us > 1000000us 
    	*/
		if (delay >= 1000)
		{
			//printf("%ds\n", request.value.header.op_delay_ms / 1000);
			//printf("%dms\n", (request.value.header.op_delay_ms % 1000));
			sleep(delay / 1000);
		}
		usleep((delay % 1000) * 1000);
	}
}

void writePaddedMsg(int fd, const char* msg)
{
	if( (fcntl(fd, F_GETFD) == -1 && errno == EBADF) )
		return;

	int msgSpace = 80 - 4;
	int padlen = (msgSpace - strlen(msg)) / 2;

	char paddedMsg[80 + 1];
	sprintf(paddedMsg, "| %*s%s%*s |\n", padlen, "", msg, ((padlen % 2 == 0) ? padlen : padlen + 1), "");

	char blockMsg[81*3+1];

	sprintf(blockMsg, "%s%s%s", 
		"--------------------------------------------------------------------------------\n",
		paddedMsg, 
		"--------------------------------------------------------------------------------\n");

	if (write(fd, blockMsg, strlen(blockMsg)) == -1)
	{
		perror("write:");
	}
}

void openLog(int *fd, const char *file_name, int oflags, mode_t mode, const char* msg)
{
	if ((*fd = open(file_name, oflags, mode)) == -1)
	{
		perror("prepareReplyFIFO: Failed to open server log");
		return;
	}

	char dateString[30];
	getDateTimeNow(dateString, 30);
	char headerMsg[80 - 4];
	sprintf(headerMsg, "%s [%s]", msg, dateString);

	writePaddedMsg(*fd, headerMsg);
}

void closeLog(int fd, const char* msg)
{
	writePaddedMsg(fd, msg);

	if (close(fd) == -1)
	{
		perror("closeLog - Failed to close server log");
	}
}