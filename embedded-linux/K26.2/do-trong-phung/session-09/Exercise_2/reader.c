
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include "device_cfg.h"

#define CONFIG_FILE "/tmp/device.cfg"

volatile sig_atomic_t running = 1;

void handle_sigint(int sig)
{
    (void)sig;
    running = 0;
}

const char *log_level_name(int level)
{
    switch (level)
    {
        case 0:
            return "OFF";
        case 1:
            return "ERROR";
        case 2:
            return "INFO";
        case 3:
            return "DEBUG";
        default:
            return "UNKNOWN";
    }
}

int main(void)
{
    signal(SIGINT, handle_sigint);

    int fd = open(CONFIG_FILE, O_RDONLY);

    if (fd < 0)
    {
        perror("open");
        exit(1);
    }

    device_cfg_t *cfg = mmap(NULL,
                             sizeof(device_cfg_t),
                             PROT_READ,
                             MAP_SHARED,
                             fd,
                             0);

    if (cfg == MAP_FAILED)
    {
        perror("mmap");
        close(fd);
        exit(1);
    }

    close(fd);

    printf("[Config Reader] Polling %s every 2s...\n",
           CONFIG_FILE);

    while (running)
    {
        printf("baud_rate=%d sampling_rate=%d Hz log_level=%s\n",
               cfg->baud_rate,
               cfg->sampling_rate_hz,
               log_level_name(cfg->log_level));

        sleep(2);
    }

    if (munmap(cfg, sizeof(device_cfg_t)) < 0)
    {
        perror("munmap");
        return 1;
    }

    return 0;
}
