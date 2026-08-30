
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>

/*
 * ============================================================
 *              OFFICE PRINT QUEUE
 * ============================================================
 *
 * This program implements a Producer-Consumer system.
 *
 * - 3 producer threads submit documents.
 * - 1 printer thread consumes and prints documents.
 * - The shared queue can contain at most 5 documents.
 *
 * Synchronization:
 * - q_lock    : protects the shared queue.
 * - not_full  : producers wait when the queue is full.
 * - not_empty : printer waits when the queue is empty.
 */


/*
 * Document stored in the print queue.
 */
typedef struct {
    int doc_id;
    char filename[60];
    int pages;
} Document;


/*
 * Shared circular queue.
 *
 * queue[5] can store at most 5 documents.
 *
 * head  -> position where the printer removes a document.
 * tail  -> position where a producer inserts a document.
 * count -> number of documents currently in the queue.
 */
#define QUEUE_SIZE 5
#define NUM_PRODUCERS 3
#define DOCS_PER_PRODUCER 3
#define TOTAL_DOCS 9

Document queue[QUEUE_SIZE];

int head = 0;
int tail = 0;
int count = 0;

/*
 * main sets all_sent = 1 after all producers finish.
 *
 * This tells the printer that no more documents will arrive.
 */
int all_sent = 0;


/*
 * Mutex and condition variables.
 */
pthread_mutex_t q_lock;
pthread_cond_t not_full;
pthread_cond_t not_empty;


/*
 * Statistics.
 */
int documents_submitted = 0;
int documents_printed = 0;
int total_pages_printed = 0;


/*
 * ============================================================
 * WHY pthread_cond_wait() MUST BE INSIDE A while LOOP
 * ============================================================
 *
 * pthread_cond_wait() must normally be used like this:
 *
 *     while (condition_is_not_true)
 *         pthread_cond_wait(&condition, &mutex);
 *
 * NOT:
 *
 *     if (condition_is_not_true)
 *         pthread_cond_wait(&condition, &mutex);
 *
 *
 * The reason is that waking up from pthread_cond_wait() does NOT
 * guarantee that the condition is actually true.
 *
 * A thread can wake up because:
 *
 * 1. Another thread called pthread_cond_signal().
 * 2. Another thread called pthread_cond_broadcast().
 * 3. A spurious wakeup occurred.
 *
 *
 * A "spurious wakeup" means that a thread waiting on a condition
 * variable can wake up even though no thread explicitly signaled
 * the condition variable.
 *
 *
 * Example:
 *
 * The queue is full:
 *
 *     count == 5
 *
 * Producer A waits because there is no free slot.
 *
 * Later, Producer A wakes up. If we used:
 *
 *     if (count == 5)
 *         pthread_cond_wait(...);
 *
 * the thread would continue after waking up without checking
 * the condition again.
 *
 * But another producer may have already filled the queue again,
 * or the wakeup may have been spurious.
 *
 * Therefore, we use:
 *
 *     while (count == 5)
 *         pthread_cond_wait(&not_full, &q_lock);
 *
 * After waking up, the condition is checked again.
 *
 * The same rule applies to the printer:
 *
 *     while (count == 0 && !all_sent)
 *         pthread_cond_wait(&not_empty, &q_lock);
 *
 * This guarantees that the thread only proceeds when the
 * required condition is actually satisfied.
 */


/*
 * ============================================================
 * PRODUCER THREAD
 * ============================================================
 *
 * Each producer submits exactly 3 documents.
 */
void *producer(void *arg)
{
    int producer_id = *(int *)arg;

    /*
     * Documents assigned to each producer.
     */
    Document documents[NUM_PRODUCERS][DOCS_PER_PRODUCER] = {

        {
            {1, "report_Q1.pdf", 12},
            {4, "slides.pdf", 20},
            {7, "summary.pdf", 4}
        },

        {
            {2, "contract.pdf", 5},
            {5, "memo.pdf", 2},
            {8, "budget.pdf", 7}
        },

        {
            {3, "invoice.pdf", 3},
            {6, "proposal.pdf", 8},
            {9, "presentation.pdf", 5}
        }
    };

    for (int i = 0; i < DOCS_PER_PRODUCER; i++) {

        Document doc = documents[producer_id - 1][i];

        /*
         * Lock the queue before accessing shared data.
         */
        pthread_mutex_lock(&q_lock);

        /*
         * If the queue is full, wait until the printer
         * removes at least one document.
         *
         * IMPORTANT:
         * pthread_cond_wait() automatically releases q_lock
         * while sleeping and reacquires it before returning.
         */
        while (count == QUEUE_SIZE) {

            printf("[Producer %d] Queue full — waiting...\n",
                   producer_id);

            pthread_cond_wait(&not_full, &q_lock);
        }


        /*
         * Insert document into the circular queue.
         */
        queue[tail] = doc;

        tail = (tail + 1) % QUEUE_SIZE;
        count++;

        documents_submitted++;


        printf("[Producer %d] Submitting: %-15s (%2d pages) — queue: %d/%d\n",
               producer_id,
               doc.filename,
               doc.pages,
               count,
               QUEUE_SIZE);


        /*
         * Tell the printer that at least one document is available.
         */
        pthread_cond_signal(&not_empty);


        /*
         * Unlock queue.
         */
        pthread_mutex_unlock(&q_lock);

        /*
         * Small delay makes concurrent execution easier to observe.
         */
        usleep(100000);
    }

    return NULL;
}


/*
 * ============================================================
 * PRINTER THREAD
 * ============================================================
 *
 * Continuously removes and prints documents.
 */
void *printer(void *arg)
{
    (void)arg;

    while (1) {

        Document doc;

        /*
         * Lock queue before checking shared data.
         */
        pthread_mutex_lock(&q_lock);


        /*
         * Wait while:
         *
         * - queue is empty
         * - AND producers have not all finished
         *
         * If all_sent == 1 and count == 0, there will never
         * be another document, so the printer exits.
         */
        while (count == 0 && !all_sent) {
            pthread_cond_wait(&not_empty, &q_lock);
        }


        /*
         * No documents remain AND all producers have finished.
         * Therefore, printing is complete.
         */
        if (count == 0 && all_sent) {

            pthread_mutex_unlock(&q_lock);
            break;
        }


        /*
         * Remove document from the queue.
         */
        doc = queue[head];

        head = (head + 1) % QUEUE_SIZE;
        count--;

        documents_printed++;
        total_pages_printed += doc.pages;


        printf("[Printer]    Printing: %-15s (%2d pages) — queue: %d/%d\n",
               doc.filename,
               doc.pages,
               count,
               QUEUE_SIZE);


        /*
         * A slot has become available.
         * Wake up one waiting producer.
         */
        pthread_cond_signal(&not_full);


        /*
         * Unlock before simulating printing.
         *
         * The printer should NOT keep the mutex locked while
         * sleeping, otherwise producers cannot access the queue.
         */
        pthread_mutex_unlock(&q_lock);


        /*
         * Simulate printing time.
         */
        sleep(1);
    }


    printf("[Printer]    All documents printed. Exiting.\n");

    return NULL;
}


/*
 * ============================================================
 * MAIN
 * ============================================================
 */
int main(void)
{
    pthread_t producer_threads[NUM_PRODUCERS];
    pthread_t printer_thread;

    int producer_ids[NUM_PRODUCERS] = {1, 2, 3};


    printf("==============================================\n");
    printf("   OFFICE PRINT QUEUE (3 producers, 1 printer)\n");
    printf("   Queue capacity: 5 documents\n");
    printf("==============================================\n\n");


    /*
     * Initialize mutex.
     */
    if (pthread_mutex_init(&q_lock, NULL) != 0) {
        perror("pthread_mutex_init");
        return 1;
    }


    /*
     * Initialize condition variables.
     */
    if (pthread_cond_init(&not_full, NULL) != 0) {
        perror("pthread_cond_init");
        pthread_mutex_destroy(&q_lock);
        return 1;
    }

    if (pthread_cond_init(&not_empty, NULL) != 0) {
        perror("pthread_cond_init");
        pthread_cond_destroy(&not_full);
        pthread_mutex_destroy(&q_lock);
        return 1;
    }


    /*
     * Create printer thread.
     */
    if (pthread_create(&printer_thread,
                       NULL,
                       printer,
                       NULL) != 0) {

        perror("pthread_create");

        pthread_cond_destroy(&not_empty);
        pthread_cond_destroy(&not_full);
        pthread_mutex_destroy(&q_lock);

        return 1;
    }


    /*
     * Create 3 producer threads.
     */
    for (int i = 0; i < NUM_PRODUCERS; i++) {

        if (pthread_create(&producer_threads[i],
                           NULL,
                           producer,
                           &producer_ids[i]) != 0) {

            perror("pthread_create");

            pthread_cond_destroy(&not_empty);
            pthread_cond_destroy(&not_full);
            pthread_mutex_destroy(&q_lock);

            return 1;
        }
    }


    /*
     * Wait for all producers to finish.
     *
     * Each producer submits exactly 3 documents.
     * Therefore, after joining all producers:
     *
     *     3 producers × 3 documents = 9 documents
     */
    for (int i = 0; i < NUM_PRODUCERS; i++) {
        pthread_join(producer_threads[i], NULL);
    }


    /*
     * All producers have finished.
     *
     * Set all_sent while holding the mutex because the printer
     * accesses this shared variable.
     */
    pthread_mutex_lock(&q_lock);

    all_sent = 1;

    /*
     * Wake the printer.
     *
     * The printer may currently be sleeping because the queue
     * was empty. broadcast guarantees that it wakes up and
     * checks the condition again.
     */
    pthread_cond_broadcast(&not_empty);

    pthread_mutex_unlock(&q_lock);


    /*
     * Wait for the printer to finish all remaining documents.
     */
    pthread_join(printer_thread, NULL);


    /*
     * Print final summary.
     */
    printf("\n================ SUMMARY ================\n");
    printf("  Documents submitted : %d\n", documents_submitted);
    printf("  Documents printed   : %d\n", documents_printed);
    printf("  Total pages printed : %d\n", total_pages_printed);
    printf("=========================================\n");


    /*
     * Destroy synchronization objects.
     */
    pthread_cond_destroy(&not_empty);
    pthread_cond_destroy(&not_full);
    pthread_mutex_destroy(&q_lock);


    return 0;
}

