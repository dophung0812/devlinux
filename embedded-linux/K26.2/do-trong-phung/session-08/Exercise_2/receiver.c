#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>

#define COLLECTOR_PORT 9000
#define BUFFER_SIZE 128

volatile sig_atomic_t running = 1;

void handle_sigint(int sig)
{
    (void)sig;
    running = 0;
}

int main(void)
{
    signal(SIGINT, handle_sigint);

    int receiver_fd = socket(AF_INET, SOCK_DGRAM, 0);

    if (receiver_fd < 0)
    {
        perror("socket");
        exit(1);
    }

    int reuse = 1;

    if (setsockopt(receiver_fd, SOL_SOCKET, SO_REUSEADDR,
                   &reuse, sizeof(reuse)) < 0)
    {
        perror("setsockopt");
        close(receiver_fd);
        exit(1);
    }

    struct sockaddr_in addr;

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(COLLECTOR_PORT);

    if (bind(receiver_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        perror("bind");
        close(receiver_fd);
        exit(1);
    }

    printf("[Collector] Listening on 0.0.0.0:%d...\n",
           COLLECTOR_PORT);

    while (running)
    {
        char buffer[BUFFER_SIZE];
        struct sockaddr_in sender_addr;
        socklen_t sender_len = sizeof(sender_addr);

        ssize_t received = recvfrom(receiver_fd,
                                    buffer,
                                    sizeof(buffer) - 1,
                                    0,
                                    (struct sockaddr *)&sender_addr,
                                    &sender_len);

        if (received < 0)
        {
            if (!running)
            {
                break;
            }

            perror("recvfrom");
            continue;
        }

        buffer[received] = '\0';

        time_t now = time(NULL);
        struct tm *time_info = localtime(&now);

        if (time_info == NULL)
        {
            fprintf(stderr, "localtime failed\n");
            continue;
        }

        char timestamp[16];

        if (strftime(timestamp, sizeof(timestamp),
                     "%H:%M:%S", time_info) == 0)
        {
            fprintf(stderr, "strftime failed\n");
            continue;
        }

        char sender_ip[INET_ADDRSTRLEN];

        if (inet_ntop(AF_INET,
                      &sender_addr.sin_addr,
                      sender_ip,
                      sizeof(sender_ip)) == NULL)
        {
            perror("inet_ntop");
            continue;
        }

        printf("[%s] %s:%d -> %s\n",
               timestamp,
               sender_ip,
               ntohs(sender_addr.sin_port),
               buffer);

        fflush(stdout);
    }

    close(receiver_fd);

    return 0;
}
