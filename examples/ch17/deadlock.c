/* deadlock.c - two threads take two locks in opposite orders.
 *   ./deadlock           thread 1 takes a then b, thread 2 takes b then a
 *   ./deadlock ordered   both take a then b: no deadlock */
#define _POSIX_C_SOURCE 200809L
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static pthread_mutex_t a = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t b = PTHREAD_MUTEX_INITIALIZER;

struct order {
    const char *name;
    pthread_mutex_t *first, *second;
};

static void *worker(void *arg)
{
    struct order *o = arg;
    pthread_mutex_lock(o->first);
    printf("%s: got my first lock\n", o->name);
    nanosleep(&(struct timespec){ .tv_nsec = 100000000 }, NULL);  /* 0.1 s */
    pthread_mutex_lock(o->second);
    printf("%s: got both locks\n", o->name);
    pthread_mutex_unlock(o->second);
    pthread_mutex_unlock(o->first);
    return NULL;
}

int main(int argc, char *argv[])
{
    setvbuf(stdout, NULL, _IONBF, 0);   /* show each line at once */
    int ordered = argc > 1 && strcmp(argv[1], "ordered") == 0;
    struct order o1 = { "thread 1", &a, &b };
    struct order o2 = { "thread 2", ordered ? &a : &b, ordered ? &b : &a };
    pthread_t t1, t2;
    pthread_create(&t1, NULL, worker, &o1);
    pthread_create(&t2, NULL, worker, &o2);
    pthread_join(t1, NULL);
    pthread_join(t2, NULL);
    printf("done\n");
    return 0;
}
