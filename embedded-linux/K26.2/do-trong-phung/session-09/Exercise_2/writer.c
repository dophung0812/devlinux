
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include "device_cfg.h"

#define CONFIG_FILE "/tmp/device.cfg"

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

int update_config(device_cfg_t *cfg)
{
    char input[64];

    printf("\nCurrent: baud_rate=%d sampling_rate=%d log_level=%d\n",
           cfg->baud_rate,
           cfg->sampling_rate_hz,
           cfg->log_level);

    printf("Select field to update [baud/rate/log/quit]: ");
    fflush(stdout);

    if (fgets(input, sizeof(input), stdin) == NULL)
    {
        return 0;
    }

    input[strcspn(input, "\r\n")] = '\0';

    if (strcmp(input, "quit") == 0)
    {
        return 0;
    }

    if (strcmp(input, "baud") == 0)
    {
        int baud_rate;

        printf("Select baud rate [9600/115200/460800]: ");
        fflush(stdout);

        if (fgets(input, sizeof(input), stdin) == NULL)
        {
            return 1;
        }

        if (sscanf(input, "%d", &baud_rate) != 1)
        {
            printf("Invalid baud rate.\n");
            return 1;
        }

        if (baud_rate != 9600 &&
            baud_rate != 115200 &&
            baud_rate != 460800)
        {
            printf("Invalid baud rate.\n");
            return 1;
        }

        cfg->baud_rate = baud_rate;

        if (msync(cfg, sizeof(device_cfg_t), MS_SYNC) < 0)
        {
            perror("msync");
            return 1;
        }

        printf("[Updated] baud_rate = %d\n", cfg->baud_rate);
    }
    else if (strcmp(input, "rate") == 0)
    {
        int rate;

        printf("Enter sampling rate [1-1000]: ");
        fflush(stdout);

        if (fgets(input, sizeof(input), stdin) == NULL)
        {
            return 1;
        }

        if (sscanf(input, "%d", &rate) != 1)
        {
            printf("Invalid sampling rate.\n");
            return 1;
        }

        if (rate < 1 || rate > 1000)
        {
            printf("Invalid sampling rate.\n");
            return 1;
        }

        cfg->sampling_rate_hz = rate;

        if (msync(cfg, sizeof(device_cfg_t), MS_SYNC) < 0)
        {
            perror("msync");
            return 1;
        }

        printf("[Updated] sampling_rate_hz = %d\n",
               cfg->sampling_rate_hz);
    }
    else if (strcmp(input, "log") == 0)
    {
        int level;

        printf("Select log level [0=OFF 1=ERROR 2=INFO 3=DEBUG]: ");
        fflush(stdout);

        if (fgets(input, sizeof(input), stdin) == NULL)
        {
            return 1;
        }

        if (sscanf(input, "%d", &level) != 1)
        {
            printf("Invalid log level.\n");
            return 1;
        }

        if (level < 0 || level > 3)
        {
            printf("Invalid log level.\n");
            return 1;
        }

        cfg->log_level = level;

        if (msync(cfg, sizeof(device_cfg_t), MS_SYNC) < 0)
        {
            perror("msync");
            return 1;
        }

        printf("[Updated] log_level = %s\n",
               log_level_name(cfg->log_level));
    }
    else
    {
        printf("Unknown command.\n");
    }

    return 1;
}

int main(void)
{
    int fd = open(CONFIG_FILE, O_RDWR | O_CREAT, 0666);

    if (fd < 0)
    {
        perror("open");
        exit(1);
    }

    if (ftruncate(fd, sizeof(device_cfg_t)) < 0)
    {
        perror("ftruncate");
        close(fd);
        exit(1);
    }

    device_cfg_t *cfg = mmap(NULL,
                             sizeof(device_cfg_t),
                             PROT_READ | PROT_WRITE,
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

    if (cfg->baud_rate != 9600 &&
        cfg->baud_rate != 115200 &&
        cfg->baud_rate != 460800)
    {
        cfg->baud_rate = 9600;
    }

    if (cfg->sampling_rate_hz < 1 ||
        cfg->sampling_rate_hz > 1000)
    {
        cfg->sampling_rate_hz = 100;
    }

    if (cfg->log_level < 0 ||
        cfg->log_level > 3)
    {
        cfg->log_level = 2;
    }

    if (msync(cfg, sizeof(device_cfg_t), MS_SYNC) < 0)
    {
        perror("msync");
        munmap(cfg, sizeof(device_cfg_t));
        exit(1);
    }

    printf("[Config Writer] Loaded %s\n",
           CONFIG_FILE);

    printf("[Config Writer] Commands: baud / rate / log / quit\n");

    while (update_config(cfg))
    {
    }

    if (munmap(cfg, sizeof(device_cfg_t)) < 0)
    {
        perror("munmap");
        return 1;
    }

    return 0;
}
