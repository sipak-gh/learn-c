/* race.c - four threads add 1 to a shared counter a million times each.
 *   ./race        without a lock: updates get lost
 *   ./race lock   with a mutex: always 4000000 */
#define _POSIX_C_SOURCE 200809L
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

enum { THREADS = 4, TIMES = 1000000 };

static long counter;
static bool use_lock;
static pthread_mutex_t counter_lock = PTHREAD_MUTEX_INITIALIZER;

static void *worker(void *arg)
{
    (void)arg;
    for (int i = 0; i < TIMES; i++) {
        if (use_lock)
            pthread_mutex_lock(&counter_lock);
        counter++;                      /* load, add 1, store */
        if (use_lock)
            pthread_mutex_unlock(&counter_lock);
    }
    return NULL;
}

int main(int argc, char *argv[])
{
    use_lock = argc > 1 && strcmp(argv[1], "lock") == 0;
    pthread_t tids[THREADS];
    for (int i = 0; i < THREADS; i++)
        pthread_create(&tids[i], NULL, worker, NULL);
    for (int i = 0; i < THREADS; i++)
        pthread_join(tids[i], NULL);
    printf("counter = %ld (expected %d)\n", counter, THREADS * TIMES);
    return 0;
}
