#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/un.h>

#define SOCKET_PATH "/tmp/monitor.sock"
#define BUFFER_SIZE 256

void handle_sigint(int sig)
{
    (void)sig;
    unlink(SOCKET_PATH);
    exit(0);
}

int get_load_average(char *response, size_t size)
{
    FILE *file = fopen("/proc/loadavg", "r");

    if (file == NULL)
    {
        return -1;
    }

    double load1;

    if (fscanf(file, "%lf", &load1) != 1)
    {
        fclose(file);
        return -1;
    }

    fclose(file);

    snprintf(response, size, "load_avg=%.2f\n", load1);

    return 0;
}

int get_memory_info(char *response, size_t size)
{
    FILE *file = fopen("/proc/meminfo", "r");

    if (file == NULL)
    {
        return -1;
    }

    char line[128];
    long mem_total = 0;
    long mem_free = 0;

    while (fgets(line, sizeof(line), file) != NULL)
    {
        if (sscanf(line, "MemTotal: %ld kB", &mem_total) == 1)
        {
            continue;
        }

        if (sscanf(line, "MemFree: %ld kB", &mem_free) == 1)
        {
            continue;
        }
    }

    fclose(file);

    if (mem_total == 0)
    {
        return -1;
    }

    snprintf(response, size,
             "mem_total=%ld kB mem_free=%ld kB\n",
             mem_total, mem_free);

    return 0;
}

int main(void)
{
    signal(SIGINT, handle_sigint);

    unlink(SOCKET_PATH);

    int server_fd = socket(AF_UNIX, SOCK_STREAM, 0);

    if (server_fd < 0)
    {
        perror("socket");
        exit(1);
    }

    struct sockaddr_un addr;

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SOCKET_PATH, sizeof(addr.sun_path) - 1);

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        perror("bind");
        close(server_fd);
        exit(1);
    }

    if (listen(server_fd, 5) < 0)
    {
        perror("listen");
        close(server_fd);
        unlink(SOCKET_PATH);
        exit(1);
    }

    printf("[Daemon] Listening on %s...\n", SOCKET_PATH);

    while (1)
    {
        int client_fd = accept(server_fd, NULL, NULL);

        if (client_fd < 0)
        {
            perror("accept");
            continue;
        }

        printf("[Daemon] Client connected.\n");

        while (1)
        {
            char buffer[BUFFER_SIZE];
            char response[BUFFER_SIZE];

            ssize_t received = recv(client_fd, buffer, sizeof(buffer) - 1, 0);

            if (received < 0)
            {
                perror("recv");
                break;
            }

            if (received == 0)
            {
                printf("[Daemon] Client disconnected. Waiting for next client...\n");
                break;
            }

            buffer[received] = '\0';

            buffer[strcspn(buffer, "\r\n")] = '\0';

            printf("[Daemon] CMD: %s\n", buffer);

            if (strcmp(buffer, "cpu") == 0)
            {
                if (get_load_average(response, sizeof(response)) < 0)
                {
                    snprintf(response, sizeof(response),
                             "ERROR: failed to read /proc/loadavg\n");
                }
            }
            else if (strcmp(buffer, "mem") == 0)
            {
                if (get_memory_info(response, sizeof(response)) < 0)
                {
                    snprintf(response, sizeof(response),
                             "ERROR: failed to read /proc/meminfo\n");
                }
            }
            else
            {
                snprintf(response, sizeof(response),
                         "ERROR: unknown command\n");
            }

            size_t response_len = strlen(response);
            ssize_t sent = send(client_fd, response, response_len, 0);

            if (sent < 0)
            {
                perror("send");
                break;
            }

            if ((size_t)sent != response_len)
            {
                fprintf(stderr, "send: incomplete transmission\n");
                break;
            }
        }

        close(client_fd);
    }

    close(server_fd);
    unlink(SOCKET_PATH);

    return 0;
}

