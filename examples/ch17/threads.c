/* threads.c - start three threads, give each a job, wait for them all. */
#define _POSIX_C_SOURCE 200809L
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

struct job {
    int id;
    long from, to;      /* input: add up from..to */
    long sum;           /* output */
};

static void *worker(void *arg)
{
    struct job *job = arg;
    for (long i = job->from; i <= job->to; i++)
        job->sum += i;
    printf("thread %d: %ld..%ld adds up to %ld\n", job->id, job->from, job->to, job->sum);
    sleep(1);           /* stay alive a moment, so you can look at /proc */
    return NULL;
}

int main(void)
{
    enum { N = 3 };
    struct job jobs[N];
    pthread_t tids[N];

    for (int i = 0; i < N; i++) {
        jobs[i] = (struct job){ .id = i, .from = i * 1000L + 1, .to = (i + 1) * 1000L };
        int err = pthread_create(&tids[i], NULL, worker, &jobs[i]);
        if (err != 0) {
            fprintf(stderr, "pthread_create: %s\n", strerror(err));
            return 1;
        }
    }
    long total = 0;
    for (int i = 0; i < N; i++) {
        pthread_join(tids[i], NULL);    /* wait for thread i to end */
        total += jobs[i].sum;
    }
    printf("main: total %ld\n", total);
    return 0;
}
