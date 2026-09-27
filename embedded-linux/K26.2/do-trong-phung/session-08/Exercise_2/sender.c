#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>

#define COLLECTOR_IP "127.0.0.1"
#define COLLECTOR_PORT 9000
#define BUFFER_SIZE 128

int get_load_average(double *load1)
{
    FILE *file = fopen("/proc/loadavg", "r");

    if (file == NULL)
    {
        return -1;
    }

    if (fscanf(file, "%lf", load1) != 1)
    {
        fclose(file);
        return -1;
    }

    fclose(file);

    return 0;
}

int get_memory_info(long *mem_total, long *mem_free)
{
    FILE *file = fopen("/proc/meminfo", "r");

    if (file == NULL)
    {
        return -1;
    }

    char line[128];

    *mem_total = 0;
    *mem_free = 0;

    while (fgets(line, sizeof(line), file) != NULL)
    {
        if (sscanf(line, "MemTotal: %ld kB", mem_total) == 1)
        {
            continue;
        }

        if (sscanf(line, "MemFree: %ld kB", mem_free) == 1)
        {
            continue;
        }
    }

    fclose(file);

    if (*mem_total == 0)
    {
        return -1;
    }

    return 0;
}

int main(void)
{
    int sender_fd = socket(AF_INET, SOCK_DGRAM, 0);

    if (sender_fd < 0)
    {
        perror("socket");
        exit(1);
    }

    struct sockaddr_in collector_addr;

    memset(&collector_addr, 0, sizeof(collector_addr));
    collector_addr.sin_family = AF_INET;
    collector_addr.sin_port = htons(COLLECTOR_PORT);

    if (inet_pton(AF_INET, COLLECTOR_IP,
                  &collector_addr.sin_addr) <= 0)
    {
        fprintf(stderr, "Invalid collector address\n");
        close(sender_fd);
        exit(1);
    }

    printf("[Sensor] Target collector: %s:%d\n",
           COLLECTOR_IP, COLLECTOR_PORT);

    for (int i = 1; i <= 5; i++)
    {
        double load1;
        long mem_total;
        long mem_free;

        if (get_load_average(&load1) < 0)
        {
            fprintf(stderr, "Failed to read /proc/loadavg\n");
            close(sender_fd);
            exit(1);
        }

        if (get_memory_info(&mem_total, &mem_free) < 0)
        {
            fprintf(stderr, "Failed to read /proc/meminfo\n");
            close(sender_fd);
            exit(1);
        }

        double temperature = 40.0 + load1 * 10.0;
        double mem_used_pct =
            (double)(mem_total - mem_free) / mem_total * 100.0;

        char message[BUFFER_SIZE];

        snprintf(message,
                 sizeof(message),
                 "id=sensor-01 temp=%.1f mem_used=%.1f%%",
                 temperature,
                 mem_used_pct);

        size_t message_len = strlen(message);

        ssize_t sent = sendto(sender_fd,
                              message,
                              message_len,
                              0,
                              (struct sockaddr *)&collector_addr,
                              sizeof(collector_addr));

        if (sent < 0)
        {
            perror("sendto");
            close(sender_fd);
            exit(1);
        }

        if ((size_t)sent != message_len)
        {
            fprintf(stderr, "sendto: incomplete transmission\n");
            close(sender_fd);
            exit(1);
        }

        printf("[Sent %d/5] %s\n", i, message);

        if (i < 5)
        {
            sleep(2);
        }
    }

    printf("[Sensor] Done.\n");

    close(sender_fd);

    return 0;
}

