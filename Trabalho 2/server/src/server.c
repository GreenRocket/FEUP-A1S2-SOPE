#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "sope.h"
#include "requestQueue.h"

sem_t empty, full;
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
int fd_main_log, fd_server_fifo, fd_user_fifo;

bank_office_t *officesArray[MAX_BANK_OFFICES] = {NULL};
bank_account_t *accountsArray[MAX_BANK_ACCOUNTS] = {NULL};

int shutdownSignal = 0;
int shutdownReady = 0;

int workingThreads;
pthread_mutex_t cleanMutex = PTHREAD_MUTEX_INITIALIZER;

/**
 * @brief Function to generate the unique salt of each account based on pid, time and random hexadecimal characters
 * 
 * @param salt char array to be filled with salt
 */
void generateUniqueSalt(char salt[SALT_LEN])
{
	time_t seconds;
	seconds = time(NULL);

	char pid[65] = {0};
	sprintf(pid, "%x", getpid());

	char sec[65] = {0};
	sprintf(sec, "%lx", seconds);

	char randHex[65] = {0};
	char str[] = "0123456789ABCDEF";

	srand(seconds + getpid());
	size_t length = SALT_LEN - strlen(pid) - strlen(sec);
	for (size_t i = 0; i < length; i++)
		randHex[i] = str[rand() % 16];

	snprintf(salt, SALT_LEN + 1, "%s%s%s", pid, randHex, sec);
}

/**
 * @brief Function to calculate hash for a given password+hash string
 * 
 * @param password 
 * @param salt 
 * @param hash output hash string
 * @return ret_code_t RC_OK if success, RC_OTHER otherwise
 */
ret_code_t cmd_sha256sum(const char *password, const char *salt, char hash[HASH_LEN])
{
	char command[MAX_PASSWORD_LEN + SALT_LEN + 31];
	sprintf(command, "echo -n \"%s%s\" | sha256sum", password, salt);

	// Run command
	FILE *fp;
	fp = popen(command, "r");
	if (fp == NULL)
	{
		printf("Failed to run command\n");
		return RC_OTHER;
	}

	// Read command output
	fgets(hash, HASH_LEN + 1, fp);

	// Close pipe
	pclose(fp);

	return RC_OK;
}

void createAccount(const uint32_t account_id, const uint32_t balance, const char *password, const int id)
{
	if (account_id > MAX_BANK_ACCOUNTS)
		return;

	bank_account_t *account = malloc(sizeof(bank_account_t));

	pthread_mutex_init(&(account->lock), NULL);

	pthread_mutex_lock(&(account->lock));
	logSyncMech(fd_main_log, id, SYNC_OP_MUTEX_LOCK, SYNC_ROLE_ACCOUNT, account_id);

	char salt[SALT_LEN + 1];
	generateUniqueSalt(salt);

	char hash[HASH_LEN + 1];
	cmd_sha256sum(password, salt, hash);

	account->account_id = account_id;
	account->balance = balance;
	strcpy(account->hash, hash);
	strcpy(account->salt, salt);
	accountsArray[account_id] = account;

	logAccountCreation(fd_main_log, MAIN_THREAD_ID, account);

	pthread_mutex_unlock(&(account->lock));
	logSyncMech(fd_main_log, id, SYNC_OP_MUTEX_UNLOCK, SYNC_ROLE_ACCOUNT, account_id);
}

/**
 * @brief Start semaphores
 * 
 * @param semSize Empty semaphore value
 * @return ret_code_t RC_OK if semaphores are created with success, RC_OTHER otherwise
 */
ret_code_t createSyncMechSem(size_t semSize)
{
	logSyncMechSem(fd_main_log, MAIN_THREAD_ID, SYNC_OP_SEM_INIT, SYNC_ROLE_PRODUCER, 0, semSize);
	if (sem_init(&empty, 0, semSize) == -1)
	{
		perror("createSyncMechSem - sem_init");
		return RC_OTHER;
	}

	logSyncMechSem(fd_main_log, MAIN_THREAD_ID, SYNC_OP_SEM_INIT, SYNC_ROLE_PRODUCER, 0, 0);
	if (sem_init(&full, 0, 0) == -1)
	{
		perror("createSyncMechSem - sem_init");
		return RC_OTHER;
	}

	workingThreads = semSize;

	return RC_OK;
}

/**
 * @brief Prepare a reply
 * 
 * @param reply Reply struct to be filled
 * @param request Corresponding request
 * @param ret_code Reply return code
 * @param val Extra value argument (except for create)
 */
void prepareReply(tlv_reply_t *reply, const tlv_request_t *request, const int ret_code, const unsigned val)
{
	reply->value.header.account_id = request->value.header.account_id;
	reply->value.header.ret_code = ret_code;
	reply->value.balance.balance = val;
	reply->type = request->type;

	size_t length = sizeof(rep_header_t);
	if (ret_code == RC_OK)
	{
		switch (request->type)
		{
		case OP_BALANCE:
			length += sizeof(rep_balance_t);
			break;
		case OP_TRANSFER:
			length += sizeof(rep_transfer_t);
			break;
		case OP_SHUTDOWN:
			length += sizeof(rep_shutdown_t);
			break;
		default:
			break;
		}
	}
	reply->length = length;
}

ret_code_t loginUser(const tlv_request_t *request)
{
	if (accountsArray[request->value.header.account_id] == NULL)
		return RC_LOGIN_FAIL;

	ret_code_t ret;
	char hash[HASH_LEN + 1];
	ret = cmd_sha256sum(request->value.header.password, accountsArray[request->value.header.account_id]->salt, hash);
	if (ret != RC_OK)
		return ret;

	if (strcmp(accountsArray[request->value.header.account_id]->hash, hash) != 0)
		return RC_LOGIN_FAIL;

	return RC_OK;
}

ret_code_t validateCreateAccount(const tlv_request_t *request, const int id)
{
	if (accountsArray[request->value.create.account_id] != NULL)
		return RC_ID_IN_USE;

	createAccount(request->value.create.account_id, request->value.create.balance, request->value.create.password, id);

	return RC_OK;
}

ret_code_t validateBalance(const tlv_request_t *request, int *val)
{
	*val = accountsArray[request->value.header.account_id]->balance;
	return RC_OK;
}

ret_code_t validateTransfer(const tlv_request_t *request, int *val, int fd, int id)
{
	if (accountsArray[request->value.transfer.account_id] == NULL)
		return RC_ID_NOT_FOUND;

	pthread_mutex_lock(&(accountsArray[request->value.transfer.account_id]->lock));
	msleep(request->value.header.op_delay_ms);
	logSyncDelay(fd, id, request->value.header.account_id, request->value.header.op_delay_ms);


	if (request->value.transfer.account_id == request->value.header.account_id)
		return RC_SAME_ID;

	if (accountsArray[request->value.header.account_id]->balance < request->value.transfer.amount)
		return RC_NO_FUNDS;

	if (accountsArray[request->value.transfer.account_id]->balance + request->value.transfer.amount > MAX_BALANCE)
		return RC_TOO_HIGH;

	if (request->value.transfer.account_id == ADMIN_ACCOUNT_ID)
		return RC_OTHER;

	accountsArray[request->value.header.account_id]->balance -= request->value.transfer.amount;
	accountsArray[request->value.transfer.account_id]->balance += request->value.transfer.amount;

	*val = accountsArray[request->value.header.account_id]->balance;

	pthread_mutex_unlock(&(accountsArray[request->value.transfer.account_id]->lock));
	return RC_OK;
}

ret_code_t validateShutdown(int *val, uint32_t delay, int fd, int id)
{
	fprintf(stderr, "SHUTDOWN SIGNAL!\n");
	msleep(delay);
	logDelay(fd, id, delay);
	fchmod(fd_server_fifo, 0444);
	shutdownSignal = 1;

	sem_getvalue(&full, val);

	return RC_OK;
}

ret_code_t validateRequest(const tlv_request_t *request, int id, int *val, int fd)
{
	ret_code_t ret = loginUser(request);

	if (ret != RC_OK)
		return ret;

	pthread_mutex_lock(&(accountsArray[request->value.header.account_id]->lock));

	if (request->type != OP_SHUTDOWN)
	{
		msleep(request->value.header.op_delay_ms);
		logSyncDelay(fd, id, request->value.header.account_id, request->value.header.op_delay_ms);
	}

	switch (request->type)
	{
	case OP_CREATE_ACCOUNT:
		ret = (request->value.header.account_id != ADMIN_ACCOUNT_ID) ? RC_OP_NALLOW : validateCreateAccount(request, id);
		break;

	case OP_BALANCE:
		ret = (request->value.header.account_id == ADMIN_ACCOUNT_ID) ? RC_OP_NALLOW : validateBalance(request, val);
		break;

	case OP_TRANSFER:
		ret = (request->value.header.account_id == ADMIN_ACCOUNT_ID) ? RC_OP_NALLOW : validateTransfer(request, val, fd, id);
		break;

	case OP_SHUTDOWN:
		ret = (request->value.header.account_id != ADMIN_ACCOUNT_ID) ? RC_OP_NALLOW : validateShutdown(val, request->value.header.op_delay_ms, fd, id);
		break;

	default:
		ret = RC_OTHER;
		break;
	}

	pthread_mutex_unlock(&(accountsArray[request->value.header.account_id]->lock));

	return ret;
}

void *officeWorker(void *arg)
{
	int fd_log_file = open(SERVER_LOGFILE, O_WRONLY | O_APPEND);
	char reply_FIFO[USER_FIFO_PATH_LEN];
	int id = *(int *)arg;

	int sval = 0;

	logBankOfficeOpen(fd_log_file, id, pthread_self());

	while (1)
	{
		sem_getvalue(&full, &sval);
		logSyncMechSem(fd_log_file, id, SYNC_OP_SEM_WAIT, SYNC_ROLE_CONSUMER, 0, sval);
		sem_wait(&full);

		if (shutdownReady == 1)
			break;

		logSyncMech(fd_log_file, id, SYNC_OP_MUTEX_LOCK, SYNC_ROLE_CONSUMER, 0);
		pthread_mutex_lock(&mutex);
		tlv_request_t request;
		RequestQueue.pop(&request);

		msleep(request.value.header.op_delay_ms);
		logSyncDelay(fd_log_file, id, request.value.header.account_id, request.value.header.op_delay_ms);

		pthread_mutex_unlock(&mutex);
		logSyncMech(fd_log_file, id, SYNC_OP_MUTEX_UNLOCK, SYNC_ROLE_CONSUMER, request.value.header.pid);

		sem_post(&empty);
		sem_getvalue(&empty, &sval);
		logSyncMechSem(fd_log_file, id, SYNC_OP_SEM_POST, SYNC_ROLE_CONSUMER, request.value.header.pid, sval);

		logRequest(fd_log_file, id, &request);
		logRequest(STDOUT_FILENO, id, &request);

		int ret_value = 0;
		ret_code_t ret = validateRequest(&request, id, &ret_value, fd_log_file);

		tlv_reply_t reply;

		sprintf(reply_FIFO, "%s%0*d", USER_FIFO_PATH_PREFIX, WIDTH_ID, request.value.header.pid);
		int fd_user_fifo = open(reply_FIFO, O_WRONLY | O_NONBLOCK);
		if (fd_user_fifo == -1)
		{
			fprintf(stderr, "PID %d : Request timeout!\n", request.value.header.pid);
			ret = RC_USR_DOWN;
			ret_value = 0;
		}

		prepareReply(&reply, &request, ret, ret_value);

		if (ret != RC_USR_DOWN && write(fd_user_fifo, &reply, sizeof(tlv_reply_t)) == -1)
		{
			perror("write:");
		}

		logReply(fd_log_file, id, &reply);
		logReply(STDOUT_FILENO, id, &reply);

		if (close(fd_user_fifo) == -1 && errno != EBADF)
			perror("officeWorker - Failed to close user fifo");

		if (shutdownReady == 1)
			break;
	}

	pthread_mutex_lock(&cleanMutex);
	workingThreads--;
	pthread_mutex_unlock(&cleanMutex);

	logBankOfficeClose(fd_log_file, id, pthread_self());
	pthread_exit(0);
}

/**
 * @brief Start bank offices (threads)
 * 
 * @param officesCount Number of bank offices (threads) to start
 */
ret_code_t startOffices(size_t officesCount)
{
	for (size_t i = 0; i < officesCount; i++)
	{
		officesArray[i] = malloc(sizeof(bank_office_t));
		officesArray[i]->office_id = i + 1;

		if (pthread_create(&(officesArray[i]->office_thread), NULL, officeWorker, &(officesArray[i]->office_id)) != 0)
		{
			perror("startOffices - pthread_create");
			return RC_OTHER;
		}
	}
	return RC_OK;
}

ret_code_t prepareRequestFIFO()
{
	if (mkfifo(SERVER_FIFO_PATH, 0666) == -1)
	{
		perror("prepareRequestFIFO - Failed to create request FIFO");
		return RC_OTHER;
	}

	if ((fd_server_fifo = open(SERVER_FIFO_PATH, O_RDONLY | O_NONBLOCK)) == -1)
	{
		perror("prepareRequestFIFO: Failed to open request FIFO");
		return RC_OTHER;
	}

	return RC_OK;
}

void listenForRequests()
{
	int val = 0;
	ssize_t readbytes = 0;
	tlv_request_t request;

	while (1)
	{
		readbytes = read(fd_server_fifo, &(request.type), sizeof(request.type));
		if (readbytes == 0)
		{
			if (shutdownSignal == 1 && RequestQueue.isEmpty())
			{
				shutdownReady = 1;
				break;
			}
		}
		else if (readbytes < 0)
		{
			if (errno != EAGAIN)
				perror("listenForRequests - Failed read");
		}
		else
		{
			if(request.type >= __OP_MAX_NUMBER || request.type < 0)
			{
				fprintf(stderr, "%d\n", request.type);
				continue;
			}
			
			readbytes = read(fd_server_fifo, &(request.length), sizeof(request.length));
			if(request.length > sizeof(request.value) || request.length < sizeof(request.value.header))
			{
				fprintf(stderr, "%d\n", request.length);
				continue;
			}

			readbytes = read(fd_server_fifo, &(request.value), request.length);
			if(readbytes != request.length)
			{
				fprintf(stderr, "%ld\n", readbytes);
				fprintf(stderr, "%d\n", request.length);
				continue;
			}

			tlv_request_t *new_request = malloc(sizeof(tlv_request_t));
			memcpy(new_request, &request, sizeof(tlv_request_t));

			logRequest(fd_main_log, MAIN_THREAD_ID, &request);

			// PRODUCE REQUEST
			sem_getvalue(&empty, &val);
			logSyncMechSem(fd_main_log, MAIN_THREAD_ID, SYNC_OP_SEM_WAIT, SYNC_ROLE_PRODUCER, request.value.header.pid, val);
			// Wait if threads are all busy doing work
			sem_wait(&empty);
			// Try to get lock for mutex
			pthread_mutex_lock(&mutex);
			logSyncMech(fd_main_log, MAIN_THREAD_ID, SYNC_OP_MUTEX_LOCK, SYNC_ROLE_PRODUCER, request.value.header.pid);
			msleep(new_request->value.header.op_delay_ms);
			logSyncDelay(fd_main_log, MAIN_THREAD_ID, new_request->value.header.account_id, new_request->value.header.op_delay_ms);
			// Enqueue request
			RequestQueue.push(new_request);
			// Release lock
			pthread_mutex_unlock(&mutex);
			logSyncMech(fd_main_log, MAIN_THREAD_ID, SYNC_OP_MUTEX_UNLOCK, SYNC_ROLE_PRODUCER, request.value.header.pid);
			// Signal idle threads that there is work to be done
			sem_post(&full);
			sem_getvalue(&full, &val);
			logSyncMechSem(fd_main_log, MAIN_THREAD_ID, SYNC_OP_SEM_POST, SYNC_ROLE_PRODUCER, request.value.header.pid, val);
		}
		



		// else if (readbytes == sizeof(tlv_request_t))
		// {
		// 	tlv_request_t *new_request = malloc(sizeof(tlv_request_t));
		// 	memcpy(new_request, &request, sizeof(tlv_request_t));

		// 	logRequest(fd_main_log, MAIN_THREAD_ID, &request);

		// 	// PRODUCE REQUEST
		// 	sem_getvalue(&empty, &val);
		// 	logSyncMechSem(fd_main_log, MAIN_THREAD_ID, SYNC_OP_SEM_WAIT, SYNC_ROLE_PRODUCER, request.value.header.pid, val);
		// 	// Wait if threads are all busy doing work
		// 	sem_wait(&empty);
		// 	// Try to get lock for mutex
		// 	pthread_mutex_lock(&mutex);
		// 	logSyncMech(fd_main_log, MAIN_THREAD_ID, SYNC_OP_MUTEX_LOCK, SYNC_ROLE_PRODUCER, request.value.header.pid);
		// 	msleep(new_request->value.header.op_delay_ms);
		// 	logSyncDelay(fd_main_log, MAIN_THREAD_ID, new_request->value.header.account_id, new_request->value.header.op_delay_ms);
		// 	// Enqueue request
		// 	RequestQueue.push(new_request);
		// 	// Release lock
		// 	pthread_mutex_unlock(&mutex);
		// 	logSyncMech(fd_main_log, MAIN_THREAD_ID, SYNC_OP_MUTEX_UNLOCK, SYNC_ROLE_PRODUCER, request.value.header.pid);
		// 	// Signal idle threads that there is work to be done
		// 	sem_post(&full);
		// 	sem_getvalue(&full, &val);
		// 	logSyncMechSem(fd_main_log, MAIN_THREAD_ID, SYNC_OP_SEM_POST, SYNC_ROLE_PRODUCER, request.value.header.pid, val);
		// }

		// else
		// {
		// 	fprintf(stderr, "Something read: %ld\n", readbytes);
		// }
	}
}

int main(int argc, char *argv[])
{
	if (argc != SERVER_EXPECTED_ARGC)
		return return_error(RC_BAD_REQ_ARGS, "USAGE: server [bank_offices] [admin_pw]\n");

	unsigned bank_offices;
	if (verifyUnsignedArg(&bank_offices, argv[1], 1, MAX_BANK_OFFICES) == -1)
		return return_error(RC_BAD_REQ_ARGS, "USAGE: Number of bank offices must be between [1, %d]\n", MAX_BANK_OFFICES);

	const char *password = NULL;
	if (verifyStringArg(&password, argv[2], MIN_PASSWORD_LEN, MAX_PASSWORD_LEN) == -1)
		return return_error(RC_BAD_REQ_ARGS, "USAGE: Password's length must be [%d, %d] caracters\n", MIN_PASSWORD_LEN, MAX_PASSWORD_LEN);

	if (verifyStringContainsWhitespaces(password))
		return return_error(RC_BAD_REQ_ARGS, "USAGE: Password must NOT contain whitespace caracters\n");

	openLog(&fd_main_log, SERVER_LOGFILE, O_WRONLY | O_APPEND | O_CREAT, 0775, "Server Started");

	// Create semaphores
	if (createSyncMechSem(bank_offices) != RC_OK)
		return RC_OTHER;

	// Create bank offices
	if (startOffices(bank_offices) != RC_OK)
		return RC_OTHER;

	// Create admin account
	createAccount(ADMIN_ACCOUNT_ID, 0, password, MAIN_THREAD_ID);

	// Create and open (read) the request FIFO
	if (prepareRequestFIFO() != RC_OK)
		return RC_OTHER;

	fprintf(stderr, "Listening for requests...\n");

	// Loop to read requests from server FIFO
	// Return upon receiving shutdown operation and RequestQueue is empty
	listenForRequests();

	// Threads will exit their loop when shutdown is occuring and their work is finished
	// Some threads are locked with sem_wait() so post to awake them
	while (workingThreads > 0)
	{
		sem_post(&full);
	}

	// Cleanup
	sem_destroy(&empty);
	sem_destroy(&full);

	for (size_t i = 0; i < MAX_BANK_ACCOUNTS; i++)
		if(accountsArray[i] != NULL)
			free(accountsArray[i]);

	for (size_t i = 0; i < MAX_BANK_OFFICES; i++)
		if(officesArray[i] != NULL)
			free(officesArray[i]);

	fprintf(stderr, "Server Closed!\n");
	closeLog(fd_main_log, "Server Closed!");

	close(fd_server_fifo);
	unlink(SERVER_FIFO_PATH);

	return 0;
}