#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>

volatile sig_atomic_t reading_count = 0;

void handle_sigint(int sig)
{
    (void)sig;
    printf("[WARN] Received SIGINT, ignoring...\n");
}

void handle_sigterm(int sig)
{
    (void)sig;
    printf("[INFO] Received SIGTERM, shutting down gracefully...\n");
    exit(0);
}

void handle_sigusr1(int sig)
{
    (void)sig;
    printf("[REPORT] Total readings so far: %d\n", reading_count);
}

int main(void)
{
    signal(SIGINT, handle_sigint);
    signal(SIGTERM, handle_sigterm);
    signal(SIGUSR1, handle_sigusr1);

    while (1)
    {
        reading_count++;

        printf("[INFO] Sensor reading #%d: temperature=%d C\n",
               reading_count, 25 + reading_count % 10);

        sleep(1);
    }

    return 0;
}

