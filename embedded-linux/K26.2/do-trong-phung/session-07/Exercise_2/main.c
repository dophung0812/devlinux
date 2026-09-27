#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>

void handle_sigusr1(int sig)
{
    (void)sig;
    printf("[GATEWAY] Worker reported READY signal received\n");
}

int main(void)
{
    signal(SIGUSR1, handle_sigusr1);

    pid_t pid = fork();

    if (pid < 0)
    {
        perror("fork");
        exit(1);
    }

    if (pid == 0)
    {
        sleep(2);

        kill(getppid(), SIGUSR1);

        printf("[WORKER] Sent READY signal to gateway\n");

        exit(7);
    }

    printf("[GATEWAY] Worker PID = %d\n", pid);

    sigset_t block_set;

    sigemptyset(&block_set);
    sigaddset(&block_set, SIGUSR1);

    sigprocmask(SIG_BLOCK, &block_set, NULL);

    sleep(5);

    sigprocmask(SIG_UNBLOCK, &block_set, NULL);

    int status;

    wait(&status);

    if (WIFEXITED(status))
    {
        printf("[GATEWAY] Worker exited with code %d\n",
               WEXITSTATUS(status));
    }

    return 0;
}
