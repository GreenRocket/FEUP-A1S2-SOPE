#include <errno.h>
#include <pthread.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "sope.h"

int fd_log_file = -1, fd_server_fifo = -1, fd_user_fifo = -1;
char user_fifo_path[USER_FIFO_PATH_LEN + 1];
unsigned timeCounter = 0;

/**
 * @brief 
 * 
 * @return void* 
 */
void *increaseTimer()
{
	while (timeCounter < FIFO_TIMEOUT_SECS)
	{
		timeCounter++;
		sleep(1);
		if (timeCounter <= FIFO_TIMEOUT_SECS)
			fprintf(stderr, "Waiting for reply... (%ds)\n", timeCounter);
	}
	pthread_exit(0);
}

void onExit()
{
	if (close(fd_server_fifo) == -1 && errno != EBADF)
		perror("onExit - Failed to close server fifo");

	if (close(fd_user_fifo) == -1 && errno != EBADF)
		perror("onExit - Failed to close user fifo");

	if (unlink(user_fifo_path) == -1 && errno != ENOENT)
		perror("onExit - Failed to remove user fifo");
}

ret_code_t verifyCreateAccountArgs(req_create_account_t *req_c, const char *request)
{
	char format[FORMAT_TEMP_BUFFER + 1];
	int formatLength = sprintf(format, "%%%ds %%%ds %%%ds %%s", WIDTH_ACCOUNT + 1, WIDTH_BALANCE + 1, MAX_PASSWORD_LEN + 1); // %%%ds -> %%ds -> %4s
	if (formatLength < 0 || formatLength > FORMAT_TEMP_BUFFER)
		return return_error(RC_OTHER, "verifyCreateAccountArgs: Format buffer needs to be increased - %d/%d\n", formatLength, FORMAT_TEMP_BUFFER);

	char account_id_buf[WIDTH_ACCOUNT + 1];
	char balance_buf[WIDTH_BALANCE + 1];
	char password[MAX_PASSWORD_LEN + 1];
	char trash_buf[WIDTH_BALANCE + 1];

	if (sscanf(request, format, account_id_buf, balance_buf, password, trash_buf) != 3)
		return return_error(RC_BAD_REQ_ARGS, "USAGE: Create new account requires 3 arguments: id balance password\n");

	unsigned account_id = 0;
	if (verifyUnsignedArg(&account_id, account_id_buf, 1, MAX_BANK_ACCOUNTS) == -1)
		return return_error(RC_BAD_REQ_ARGS, "USAGE: New account IDs must be between [1, %d]\n", MAX_BANK_ACCOUNTS);

	unsigned balance = 0;
	if (verifyUnsignedArg(&balance, balance_buf, MIN_BALANCE, MAX_BALANCE) == -1)
		return return_error(RC_BAD_REQ_ARGS, "USAGE: Account balance must be between [%ld, %ld] €\n", MIN_BALANCE, MAX_BALANCE);

	int pw_len = strlen(password);
	if (pw_len < MIN_PASSWORD_LEN || pw_len > MAX_PASSWORD_LEN)
		return return_error(RC_BAD_REQ_ARGS, "USAGE: New account password must be between [%d, %d]\n", MIN_PASSWORD_LEN, MAX_PASSWORD_LEN);

	req_c->account_id = account_id;
	req_c->balance = balance;
	strcpy(req_c->password, password);

	return RC_OK;
}

ret_code_t verifyTransferArgs(req_transfer_t *req_tr, const char *request)
{
	char format[FORMAT_TEMP_BUFFER + 1];
	int formatLength = sprintf(format, "%%%ds %%%ds %%s", WIDTH_ACCOUNT + 1, WIDTH_BALANCE + 1); // %%%ds -> %%ds -> %4s
	if (formatLength < 0 || formatLength > FORMAT_TEMP_BUFFER)
		return return_error(RC_OTHER, "verifyTransferArgs: Format buffer needs to be increased - %d/%d", formatLength, FORMAT_TEMP_BUFFER);

	char accountID_buf[WIDTH_ACCOUNT + 1];
	char amount_buf[WIDTH_BALANCE + 1];
	char trash_buf[WIDTH_BALANCE + 1];

	if (sscanf(request, format, accountID_buf, amount_buf, trash_buf) != 2)
		return return_error(RC_BAD_REQ_ARGS, "USAGE: Transfer requires 2 arguments: id amount\n");

	unsigned account_id = 0;
	if (verifyUnsignedArg(&account_id, accountID_buf, 1, MAX_BANK_ACCOUNTS) == -1)
		return return_error(RC_BAD_REQ_ARGS, "USAGE: Destination account IDs must be between [1, %d]\n", MAX_BANK_ACCOUNTS);

	unsigned balance = 0;
	if (verifyUnsignedArg(&balance, amount_buf, MIN_BALANCE, MAX_BALANCE) == -1)
		return return_error(RC_BAD_REQ_ARGS, "USAGE: Transfer amount must be between [%ld, %ld] €\n", MIN_BALANCE, MAX_BALANCE);

	req_tr->account_id = account_id;
	req_tr->amount = balance;

	return RC_OK;
}

ret_code_t prepareRequest(tlv_request_t *request, const unsigned account_id, const char *password, const unsigned op_delay_ms, const unsigned op_code, const char *opArgs)
{
	size_t length = sizeof(req_header_t);

	if (op_code == OP_CREATE_ACCOUNT)
	{
		// if(account_id != ADMIN_ACCOUNT_ID)
		// 	return RC_OP_NALLOW;

		int ret_code;
		if ((ret_code = verifyCreateAccountArgs(&(request->value.create), opArgs)) != RC_OK)
			return ret_code;

		length += (sizeof(req_create_account_t));
	}
	else if (op_code == OP_TRANSFER)
	{
		// if(account_id != ADMIN_ACCOUNT_ID)
		// 	return RC_OP_NALLOW;

		int ret_code;
		if ((ret_code = verifyTransferArgs(&(request->value.transfer), opArgs)) != RC_OK)
			return ret_code;

		length += sizeof(req_transfer_t);
	}
	else
	{
		// if(account_id == ADMIN_ACCOUNT_ID)
		// 	return RC_OP_NALLOW;

		if (strlen(opArgs) > 0)
			return return_error(RC_BAD_REQ_ARGS, "USAGE: Balance checking and Shutdown operations don't take arguments\n");
	}

	request->value.header.pid = getpid();
	request->value.header.account_id = account_id;
	request->value.header.op_delay_ms = op_delay_ms;
	strcpy(request->value.header.password, password);

	request->type = op_code;
	request->length = length;

	return RC_OK;
}

void makeOfflineReply(tlv_reply_t *reply, const int ret_code, const int account_id, const op_type_t op_code)
{
	reply->value.header.account_id = account_id;
	reply->value.header.ret_code = ret_code;

	reply->type = op_code;
	reply->length = sizeof(rep_header_t);

	switch (op_code)
	{
	case OP_BALANCE:
		reply->value.balance.balance = 0;
		break;
	case OP_TRANSFER:
		reply->value.transfer.balance = 0;
		break;
	case OP_SHUTDOWN:
		reply->value.shutdown.active_offices = 0;
		break;
	default:
		break;
	}
}

ret_code_t pingServer(const tlv_request_t *request)
{
	if ((fd_server_fifo = open(SERVER_FIFO_PATH, O_WRONLY | O_APPEND)) == -1)
	{
		//perror("prepareReplyFIFO - Failed to open request FIFO");

		tlv_reply_t reply;
		makeOfflineReply(&reply, RC_SRV_DOWN, request->value.header.account_id, request->type);

		// Log reply
		if (logReply(fd_log_file, getpid(), &reply) < 0)
			fprintf(stderr, "pingServer: logReply failed\n");

		// Log reply
		if (logReply(STDOUT_FILENO, getpid(), &reply) < 0)
			fprintf(stderr, "pingServer: logReply failed\n");

		return RC_SRV_DOWN;
	}
	return RC_OK;
}

ret_code_t prepareReplyFIFO()
{
	if (sprintf(user_fifo_path, "%s%0*d", USER_FIFO_PATH_PREFIX, WIDTH_ID, getpid()) < 0)
	{
		perror("prepareReplyFIFO - Failed to concatenate user_fifo_path");
		return RC_OTHER;
	}

	if (mkfifo(user_fifo_path, 0666) == -1)
	{
		perror("prepareReplyFIFO - Failed to create reply FIFO");
		return RC_OTHER;
	}

	if ((fd_user_fifo = open(user_fifo_path, O_RDONLY | O_NONBLOCK)) == -1)
	{
		perror("prepareReplyFIFO - Failed to open reply FIFO");
		return RC_OTHER;
	}
	return RC_OK;
}

ret_code_t sendRequest(const tlv_request_t *request)
{
	// op_type_t tre;
	// uint32_t len;
	// void* ptr = (void* )request;
	// memcpy(&tre, ptr, sizeof(op_type_t));
	// ptr += sizeof(uint32_t);
	// memcpy(&len, ptr, sizeof(uint32_t));
	// fprintf(stderr, "%d\n", tre);
	// fprintf(stderr, "%d\n", len);
	// write(STDOUT_FILENO, request, sizeof(op_type_t));
	// write(STDOUT_FILENO, "\n", 1);

	if (write(fd_server_fifo, request, sizeof(request->type) + sizeof(request->length) + request->length) == -1)
		return return_error(RC_OTHER, "sendRequest: Failed to write request in server fifo\n");

	if (close(fd_server_fifo) == -1 && errno != EBADF)
	{
		perror("sendRequest - Failed to close server fifo");
		return RC_OTHER;
	}

	return RC_OK;
}

ret_code_t listenForReply()
{
	pthread_t thread;
	pthread_create(&thread, NULL, increaseTimer, NULL);
	pthread_detach(thread);

	tlv_reply_t reply;
	ssize_t readbytes = 0;

	while (timeCounter < FIFO_TIMEOUT_SECS)
	{
		readbytes = read(fd_user_fifo, &(reply.type), sizeof(reply.type));
		if (readbytes < 0)
		{
			if (errno != EAGAIN)
				perror("listenForRequests - Failed read");
		}
		else if (readbytes == 0)
		{
			//fprintf(stderr, "EOF!\n");
		}
		else
		{
			if(reply.type >= __OP_MAX_NUMBER || reply.type < 0)
			{
				fprintf(stderr, "%d\n", reply.type);
				continue;
			}
			
			readbytes = read(fd_user_fifo, &(reply.length), sizeof(reply.length));
			if(reply.length > sizeof(reply.value) || reply.length < sizeof(reply.value.header))
			{
				fprintf(stderr, "%d\n", reply.length);
				continue;
			}

			readbytes = read(fd_user_fifo, &(reply.value), reply.length);
			if(readbytes != reply.length)
			{
				fprintf(stderr, "%ld\n", readbytes);
				fprintf(stderr, "%d\n", reply.length);
				continue;
			}

			timeCounter = FIFO_TIMEOUT_SECS;

			if(reply.length == sizeof(reply.value.header))
				reply.value.balance.balance = 0;

			// Log reply
			if (logReply(fd_log_file, getpid(), &reply) < 0)
				fprintf(stderr, "listenForReply: logReply failed\n");

			// Log reply
			if (logReply(STDOUT_FILENO, getpid(), &reply) < 0)
				fprintf(stderr, "listenForReply: logReply failed\n");

			return reply.value.header.ret_code;
		}
		


		readbytes = read(fd_user_fifo, &reply, sizeof(tlv_reply_t));
		if (readbytes < 0)
		{
			if (errno != EAGAIN)
				perror("listenForReply - Failed read");
		}

		else if (readbytes == sizeof(tlv_reply_t))
		{
			timeCounter = FIFO_TIMEOUT_SECS;

			// Log reply
			if (logReply(fd_log_file, getpid(), &reply) < 0)
				fprintf(stderr, "listenForReply: logReply failed\n");

			// Log reply
			if (logReply(STDOUT_FILENO, getpid(), &reply) < 0)
				fprintf(stderr, "listenForReply: logReply failed\n");

			return reply.value.header.ret_code;
		}

		// else if (readbytes == 0)
		// {
		// 	//fprintf(stderr, "EOF!\n");
		// }

		// else
		// {
		// 	fprintf(stderr, "Something read: %ld bytes\n", readbytes);
		// }
	}

	return RC_SRV_TIMEOUT;
}

int main(int argc, char *argv[])
{
	if (argc != USER_EXPECTED_ARGC)
		return return_error(RC_BAD_REQ_ARGS, "USAGE: user [account_id] [password] [op_delay] [op_code] [op_args]\n");

	unsigned account_id;
	if (verifyUnsignedArg(&account_id, argv[1], 0, MAX_BANK_ACCOUNTS) == -1)
		return return_error(RC_BAD_REQ_ARGS, "USAGE: Account ID must be between [0, %d]\n", MAX_BANK_ACCOUNTS);

	const char *password = NULL;
	if (verifyStringArg(&password, argv[2], MIN_PASSWORD_LEN, MAX_PASSWORD_LEN) == -1)
		return return_error(RC_BAD_REQ_ARGS, "USAGE: Password's length must be [%d, %d] caracters\n", MIN_PASSWORD_LEN, MAX_PASSWORD_LEN);

	if (verifyStringContainsWhitespaces(password))
		return return_error(RC_BAD_REQ_ARGS, "USAGE: Password must NOT contain whitespace caracters\n");

	unsigned op_delay_ms;
	if (verifyUnsignedArg(&op_delay_ms, argv[3], 0, MAX_OP_DELAY_MS) == -1)
		return return_error(RC_BAD_REQ_ARGS, "USAGE: Operation delay must be between [0, %d] ms\n", MAX_OP_DELAY_MS);

	op_type_t op_code;
	if (verifyUnsignedArg(&op_code, argv[4], 0, __OP_MAX_NUMBER - 1) == -1)
		return return_error(RC_BAD_REQ_ARGS, "USAGE: Operation code must be between [0, %d]\n", __OP_MAX_NUMBER - 1);

	ret_code_t code = RC_OK;
	tlv_request_t request;

	// Try to populate tlv_request_t struct (argv[5] validation)
	if ((code = prepareRequest(&request, account_id, password, op_delay_ms, op_code, argv[5])) != RC_OK)
		return code;

	if ((fd_log_file = open(USER_LOGFILE, O_WRONLY | O_APPEND | O_CREAT, 0775)) == -1)
	{
		perror("main: Failed to open user log");
	}

	// Log request
	if (logRequest(fd_log_file, getpid(), &request) < 0)
		fprintf(stderr, "main: logRequest failed\n");

	// Log request
	if (logRequest(STDOUT_FILENO, getpid(), &request) < 0)
		fprintf(stderr, "main: logRequest failed\n");

	// Setup cleaning when main exits
	atexit(onExit);

	// Check if server is running
	if ((code = pingServer(&request)) != RC_OK)
		return code;

	// Create and open (read) the reply FIFO
	if ((code = prepareReplyFIFO()) != RC_OK)
		return code;

	// Serialize request struct, write it in the server fifo and close connection
	if ((code = sendRequest(&request)) != RC_OK)
		return code;

	if ((code = listenForReply()) == RC_SRV_TIMEOUT)
	{
		timeCounter = FIFO_TIMEOUT_SECS;

		tlv_reply_t reply;
		makeOfflineReply(&reply, RC_SRV_TIMEOUT, account_id, op_code);

		// Log reply
		if (logReply(fd_log_file, getpid(), &reply) < 0)
			fprintf(stderr, "main: logReply failed\n");

		// Log reply
		if (logReply(STDOUT_FILENO, getpid(), &reply) < 0)
			fprintf(stderr, "main: logReply failed\n");

		return RC_SRV_TIMEOUT;
	}

	return code;
}