#pragma once

#include <pthread.h>
#include "types.h"

struct requestQueue
{
    int (*isEmpty)(void);
    int (*isFull)(void);
    int (*size)(void);
    int (*push)(tlv_request_t* request);
    int (*pop)(tlv_request_t* request);
};

extern const struct requestQueue RequestQueue;