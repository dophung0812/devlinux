
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <pthread.h>
#include "device_state.h"

#define SHM_NAME "/device_shm"

volatile sig_atomic_t running = 1;

void handle_sigint(int sig)
{
    (void)sig;
    running = 0;
}

int main(void)
{
    signal(SIGINT, handle_sigint);

    int fd = shm_open(SHM_NAME, O_RDWR, 0666);

    if (fd < 0)
    {
        perror("shm_open");
        exit(1);
    }

    device_state_t *state = mmap(NULL,
                                 sizeof(device_state_t),
                                 PROT_READ | PROT_WRITE,
                                 MAP_SHARED,
                                 fd,
                                 0);

    if (state == MAP_FAILED)
    {
        perror("mmap");
        close(fd);
        exit(1);
    }

    close(fd);

    printf("[Device] Attached to %s\n", SHM_NAME);

    while (running)
    {
        int status;

        if (pthread_mutex_lock(&state->mutex) != 0)
        {
            fprintf(stderr, "pthread_mutex_lock failed\n");
            break;
        }

        status = state->status;

        if (pthread_mutex_unlock(&state->mutex) != 0)
        {
            fprintf(stderr, "pthread_mutex_unlock failed\n");
            break;
        }

        if (status == 1)
        {
            printf("[Device] Status: ON  - Running...\n");
        }
        else
        {
            printf("[Device] Status: OFF - Idle.\n");
        }

        fflush(stdout);

        sleep(1);
    }

    if (munmap(state, sizeof(device_state_t)) < 0)
    {
        perror("munmap");
        return 1;
    }

    return 0;
}
