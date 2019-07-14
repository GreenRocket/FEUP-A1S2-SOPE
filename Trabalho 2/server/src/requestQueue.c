#include <string.h>
#include <stdlib.h>

#include "requestQueue.h"
#include "constants.h"

tlv_request_t* requestArray[MAX_BANK_OFFICES];
int front = 0;
int rear = -1;
int itemCount = 0;

int isEmpty(void) { return (itemCount == 0) ? 1 : 0; }

int isFull(void) { return (itemCount == MAX_BANK_OFFICES) ? 1 : 0; }

int size(void) { return itemCount; }

int push(tlv_request_t* request)
{
	if(isFull())
		return -1;

	if(rear == MAX_BANK_OFFICES - 1)
		rear = -1;

	requestArray[++rear] = request;

	itemCount++;

	return itemCount;
}

int pop(tlv_request_t* request)
{
	if(isEmpty())
		return -1;

	memcpy(request, requestArray[front], sizeof(tlv_request_t));
	free(requestArray[front]);
	front++;

	if(front == MAX_BANK_OFFICES)
		front = 0;

	itemCount--;

	return itemCount;
}

const struct requestQueue RequestQueue = {
    .isEmpty = isEmpty,
    .isFull = isFull,
    .size = size,
    .push = push,
    .pop = pop
};