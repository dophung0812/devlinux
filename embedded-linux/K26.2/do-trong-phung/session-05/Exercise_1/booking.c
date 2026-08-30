
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>

/*
 * Bus Ticket Booking System
 *
 * 5 agents (threads) handle booking requests concurrently.
 * There are initially 10 available seats.
 */

typedef struct {
    int agent_id;
    char customer[50];
    int seats_wanted;
} BookingRequest;

/* Global shared data */
int seats_available = 10;
int seats_sold = 0;
int failed_bookings = 0;

/* Mutex protects the shared seat data */
pthread_mutex_t seat_lock;


/*
 * WHY MUST CHECK AND DEDUCT BE IN THE SAME CRITICAL SECTION?
 *
 * The operation:
 *
 *     if (seats_available >= seats_wanted)
 *         seats_available -= seats_wanted;
 *
 * must be performed atomically.
 *
 * If the check and deduct operations were protected by two separate
 * lock/unlock operations, another thread could change seats_available
 * between the check and the deduction.
 *
 * Example:
 *
 * Thread A checks: seats_available = 2, wants 2 -> enough seats.
 * Thread A unlocks.
 *
 * Thread B checks: seats_available = 2, wants 2 -> enough seats.
 * Thread B deducts 2 seats.
 * Thread B unlocks.
 *
 * Thread A then deducts 2 seats even though there are no seats left.
 * This can cause overbooking.
 *
 * Therefore, the check and deduction must be inside ONE mutex
 * lock/unlock block so that no other thread can modify
 * seats_available between these two operations.
 */


/*
 * Thread function: handles one booking request.
 */
void *book_ticket(void *arg)
{
    BookingRequest *request = (BookingRequest *)arg;

    /*
     * Sleep for 1 second to make the concurrent execution
     * of all five threads more visible.
     */
    sleep(1);

    printf("[Agent %d | TID %lu] Booking %d seat%s for %s...\n",
           request->agent_id,
           (unsigned long)pthread_self(),
           request->seats_wanted,
           request->seats_wanted == 1 ? "" : "s",
           request->customer);

    /*
     * The CHECK and DEDUCT operations are in the SAME
     * critical section.
     */
    pthread_mutex_lock(&seat_lock);

    if (seats_available >= request->seats_wanted) {

        /* Enough seats -> deduct immediately */
        seats_available -= request->seats_wanted;
        seats_sold += request->seats_wanted;

        printf("[Agent %d] CONFIRMED: %d seat%s for %s. Remaining: %d\n",
               request->agent_id,
               request->seats_wanted,
               request->seats_wanted == 1 ? "" : "s",
               request->customer,
               seats_available);

    } else {

        /* Not enough seats -> booking fails */
        failed_bookings++;

        printf("[Agent %d] SOLD OUT: needs %d seats, only %d left "
               "-- booking failed.\n",
               request->agent_id,
               request->seats_wanted,
               seats_available);
    }

    pthread_mutex_unlock(&seat_lock);

    return NULL;
}


int main(void)
{
    printf("==============================================\n");
    printf("   TICKET BOOKING SYSTEM (5 agents, 10 seats)\n");
    printf("==============================================\n\n");

    /*
     * Hardcoded booking requests.
     */
    BookingRequest requests[5] = {
        {1, "Nguyen Van An",  2},
        {2, "Tran Thi Bich",  1},
        {3, "Le Van Cuong",   3},
        {4, "Pham Thi Dung",  1},
        {5, "Hoang Van Em",   2}
    };

    pthread_t threads[5];

    /*
     * Initialize mutex before creating threads.
     */
    if (pthread_mutex_init(&seat_lock, NULL) != 0) {
        perror("pthread_mutex_init");
        return 1;
    }

    /*
     * Create 5 threads.
     */
    for (int i = 0; i < 5; i++) {
        if (pthread_create(&threads[i], NULL, book_ticket, &requests[i]) != 0) {
            perror("pthread_create");
            pthread_mutex_destroy(&seat_lock);
            return 1;
        }
    }

    /*
     * Wait for all threads to finish.
     */
    for (int i = 0; i < 5; i++) {
        if (pthread_join(threads[i], NULL) != 0) {
            perror("pthread_join");
            pthread_mutex_destroy(&seat_lock);
            return 1;
        }
    }

    /*
     * Print final summary.
     */
    printf("\n================ SUMMARY ================\n");
    printf("  Total seats     : %d\n", 10);
    printf("  Seats sold      : %d\n", seats_sold);
    printf("  Seats remaining : %d\n", seats_available);
    printf("  Failed bookings : %d\n", failed_bookings);
    printf("=========================================\n");

    /*
     * Destroy mutex after all threads have finished.
     */
    pthread_mutex_destroy(&seat_lock);

    return 0;
}
