#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>

#define LOG_ERR     "<3>"
#define LOG_WARNING "<4>"
#define LOG_INFO    "<6>"

int main(void)
{
    time_t start_time;
    int cycle = 0;

    /*
     * Disable buffering so log messages are immediately
     * sent to the systemd journal.
     */
    setbuf(stdout, NULL);
    setbuf(stderr, NULL);

    srand((unsigned int)time(NULL));

    fprintf(stderr, LOG_INFO "Logger service started. PID: %d\n", getpid());

    start_time = time(NULL);

    while (1) {
        cycle++;

        /*
         * Write three log messages with different severity levels.
         */
        fprintf(stderr,
                LOG_INFO "Service running normally, cycle %d\n",
                cycle);

        fprintf(stderr,
                LOG_WARNING "Memory usage high: %d%%\n",
                80 + rand() % 15);

        fprintf(stderr,
                LOG_ERR "Failed to connect to database, retry %d\n",
                cycle);

        /*
         * Wait 2 seconds before the next logging cycle.
         */
        sleep(2);

        /*
         * Crash automatically after 30 seconds.
         */
        if (time(NULL) - start_time >= 30) {
            fprintf(stderr,
                    LOG_ERR "30 seconds elapsed. Simulating crash...\n");

            abort();
        }
    }

    return 0;
}