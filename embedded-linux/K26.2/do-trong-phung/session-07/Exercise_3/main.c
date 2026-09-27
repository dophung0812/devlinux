#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>

int main(void)
{
    sigset_t block_set;
    sigset_t old_set;

    sigemptyset(&block_set);
    sigaddset(&block_set, SIGINT);

    for (int i = 1; i <= 5; i++)
    {
        sigprocmask(SIG_BLOCK, &block_set, &old_set);

        printf("[SAFE] Writing transaction #%d ...\n", i);
        sleep(3);

        printf("[SAFE] Transaction #%d committed.\n", i);

        sigprocmask(SIG_SETMASK, &old_set, NULL);

        printf("[IDLE] Waiting for next transaction...\n");
        sleep(3);
    }

    return 0;
}
