
#ifndef IOT_SERVER_H
#define IOT_SERVER_H

#include <time.h>

#define SERVER_PORT 9999
#define MAX_CLIENTS 10
#define BROADCAST_INTERVAL 5
#define BUFFER_SIZE 256

typedef struct
{
    int fd;
    int mode;
    time_t last_activity;
} client_t;

#endif
