
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <pthread.h>
#include "device_state.h"

#define SHM_NAME "/device_shm"

int main(void)
{
    int fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);

    if (fd < 0)
    {
        perror("shm_open");
        exit(1);
    }

    if (ftruncate(fd, sizeof(device_state_t)) < 0)
    {
        perror("ftruncate");
        close(fd);
        shm_unlink(SHM_NAME);
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
        shm_unlink(SHM_NAME);
        exit(1);
    }

    close(fd);

    pthread_mutexattr_t attr;

    if (pthread_mutexattr_init(&attr) != 0)
    {
        fprintf(stderr, "pthread_mutexattr_init failed\n");
        munmap(state, sizeof(device_state_t));
        shm_unlink(SHM_NAME);
        exit(1);
    }

    if (pthread_mutexattr_setpshared(&attr,
                                     PTHREAD_PROCESS_SHARED) != 0)
    {
        fprintf(stderr, "pthread_mutexattr_setpshared failed\n");
        pthread_mutexattr_destroy(&attr);
        munmap(state, sizeof(device_state_t));
        shm_unlink(SHM_NAME);
        exit(1);
    }

    if (pthread_mutex_init(&state->mutex, &attr) != 0)
    {
        fprintf(stderr, "pthread_mutex_init failed\n");
        pthread_mutexattr_destroy(&attr);
        munmap(state, sizeof(device_state_t));
        shm_unlink(SHM_NAME);
        exit(1);
    }

    pthread_mutexattr_destroy(&attr);

    if (pthread_mutex_lock(&state->mutex) != 0)
    {
        fprintf(stderr, "pthread_mutex_lock failed\n");
        pthread_mutex_destroy(&state->mutex);
        munmap(state, sizeof(device_state_t));
        shm_unlink(SHM_NAME);
        exit(1);
    }

    state->status = 0;

    pthread_mutex_unlock(&state->mutex);

    printf("[Controller] Shared memory ready. Commands: on / off / quit\n");

    while (1)
    {
        char command[32];

        printf("> ");
        fflush(stdout);

        if (fgets(command, sizeof(command), stdin) == NULL)
        {
            break;
        }

        command[strcspn(command, "\r\n")] = '\0';

        if (strcmp(command, "on") == 0)
        {
            if (pthread_mutex_lock(&state->mutex) != 0)
            {
                fprintf(stderr, "pthread_mutex_lock failed\n");
                break;
            }

            state->status = 1;

            if (pthread_mutex_unlock(&state->mutex) != 0)
            {
                fprintf(stderr, "pthread_mutex_unlock failed\n");
                break;
            }

            printf("[Controller] Command sent: ON\n");
        }
        else if (strcmp(command, "off") == 0)
        {
            if (pthread_mutex_lock(&state->mutex) != 0)
            {
                fprintf(stderr, "pthread_mutex_lock failed\n");
                break;
            }

            state->status = 0;

            if (pthread_mutex_unlock(&state->mutex) != 0)
            {
                fprintf(stderr, "pthread_mutex_unlock failed\n");
                break;
            }

            printf("[Controller] Command sent: OFF\n");
        }
        else if (strcmp(command, "quit") == 0)
        {
            break;
        }
        else
        {
            printf("[Controller] Unknown command.\n");
        }
    }

    printf("[Controller] Cleaning up. Goodbye.\n");

    if (pthread_mutex_destroy(&state->mutex) != 0)
    {
        fprintf(stderr, "pthread_mutex_destroy failed\n");
    }

    if (munmap(state, sizeof(device_state_t)) < 0)
    {
        perror("munmap");
    }

    if (shm_unlink(SHM_NAME) < 0)
    {
        perror("shm_unlink");
    }

    return 0;
}
