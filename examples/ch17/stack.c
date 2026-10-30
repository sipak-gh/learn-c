/* stack.c - how big is a thread's stack, and what happens when a thread
 * puts a 256 KiB array on it?
 *   ./stack          use the default stack size
 *   ./stack KIB      ask for a KIB kibibyte stack */
#define _POSIX_C_SOURCE 200809L
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BIG (256 * 1024)

static void *worker(void *arg)
{
    char local[BIG];                    /* lives on this thread's stack */
    (void)arg;
    memset(local, 'x', sizeof local);
    printf("thread: used a %d KiB local array (%c)\n", BIG / 1024, local[BIG - 1]);
    return NULL;
}

int main(int argc, char *argv[])
{
    pthread_attr_t attr;
    size_t size;
    pthread_attr_init(&attr);
    pthread_attr_getstacksize(&attr, &size);
    printf("default thread stack: %zu KiB\n", size / 1024);
    if (argc > 1) {
        int err = pthread_attr_setstacksize(&attr, strtoul(argv[1], NULL, 10) * 1024);
        if (err != 0) {
            fprintf(stderr, "pthread_attr_setstacksize: %s\n", strerror(err));
            return 1;
        }
        pthread_attr_getstacksize(&attr, &size);
        printf("asked for:            %zu KiB\n", size / 1024);
    }
    fflush(stdout);                     /* the thread may crash the process */

    pthread_t tid;
    int err = pthread_create(&tid, &attr, worker, NULL);
    if (err != 0) {
        fprintf(stderr, "pthread_create: %s\n", strerror(err));
        return 1;
    }
    pthread_join(tid, NULL);
    pthread_attr_destroy(&attr);
    return 0;
}
