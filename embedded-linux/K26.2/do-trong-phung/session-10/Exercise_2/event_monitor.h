
#ifndef EVENT_MONITOR_H
#define EVENT_MONITOR_H

#define FIFO_PATH "/tmp/event_log"
#define SOCKET_PATH "/tmp/event_control.sock"
#define FILE_PATH "/tmp/system_status"
#define POLL_TIMEOUT_MS 2000
#define BUFFER_SIZE 256

typedef enum
{
    FD_FIFO,
    FD_SOCKET_LISTENER,
    FD_FILE,
    NUM_POLL_FDS
} pollfd_index_t;

#endif

