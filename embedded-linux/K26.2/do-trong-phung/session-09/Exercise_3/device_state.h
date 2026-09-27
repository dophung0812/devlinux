
#ifndef DEVICE_STATE_H
#define DEVICE_STATE_H

#include <pthread.h>

typedef struct
{
    pthread_mutex_t mutex;
    int status;
} device_state_t;

#endif

