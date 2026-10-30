/* latch.c - wait until a number of detached threads have finished, with a
 * condition variable. The server's shutdown in this chapter does the same. */
#define _POSIX_C_SOURCE 200809L
#include <pthread.h>
#include <stdio.h>
#include <time.h>

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t all_done = PTHREAD_COND_INITIALIZER;
static int running;                     /* protected by lock */

static void *worker(void *arg)
{
    int id = *(int *)arg;
    nanosleep(&(struct timespec){ .tv_nsec = id * 100000000L }, NULL);  /* "work" */

    pthread_mutex_lock(&lock);
    running--;
    printf("worker %d done, %d still running\n", id, running);
    if (running == 0)
        pthread_cond_signal(&all_done); /* wake the waiting thread */
    pthread_mutex_unlock(&lock);
    return NULL;
}

int main(void)
{
    enum { N = 4 };
    static int ids[N];
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

    pthread_mutex_lock(&lock);
    for (int i = 0; i < N; i++) {
        ids[i] = N - i;                 /* the last one started finishes first */
        pthread_t tid;
        if (pthread_create(&tid, &attr, worker, &ids[i]) == 0)
            running++;
    }
    /* Sleep until running is 0. pthread_cond_wait unlocks the mutex while
     * it sleeps and locks it again before it returns. */
    while (running > 0)
        pthread_cond_wait(&all_done, &lock);
    pthread_mutex_unlock(&lock);
    pthread_attr_destroy(&attr);
    printf("main: all workers are done\n");
    return 0;
}
