
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>

/*
 * Global flag used to tell the main loop to stop.
 *
 * volatile sig_atomic_t is appropriate for data modified
 * inside a signal handler.
 */
volatile sig_atomic_t running = 1;


/*
 * SIGTERM signal handler.
 *
 * systemd normally sends SIGTERM when stopping a service.
 */
void handle_sigterm(int signum)
{
    (void)signum;

    /*
     * Set the flag so the main loop can exit cleanly.
     */
    running = 0;
}


int main(void)
{
    struct sigaction sa;

    /*
     * Disable stdout buffering so every log line is sent
     * to stdout immediately and can appear in the systemd journal.
     */
    setbuf(stdout, NULL);


    /*
     * Configure SIGTERM handler.
     */
    sa.sa_handler = handle_sigterm;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    if (sigaction(SIGTERM, &sa, NULL) == -1) {
        perror("sigaction");
        return EXIT_FAILURE;
    }


    printf("Monitor service started. PID: %d\n", getpid());


    /*
     * Main service loop.
     */
    while (running) {

        printf("Monitor service is running... PID: %d\n", getpid());

        /*
         * Print one log line every second.
         */
        sleep(1);
    }


    /*
     * SIGTERM was received.
     * Exit cleanly.
     */
    printf("Service shutting down...\n");


    return EXIT_SUCCESS;
}
