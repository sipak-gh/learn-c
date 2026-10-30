/* whosig.c - which thread gets a signal sent to the process?
 * Two threads, main and worker, wait in read() on an empty pipe. After
 * 0.2 s a helper thread sends SIGINT to the whole process.
 *   ./whosig          nobody blocks SIGINT
 *   ./whosig worker   the worker blocks SIGINT (what parleyd does)
 *   ./whosig main     the main thread blocks SIGINT */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static int pipe_fds[2];

static void on_int(int signo)
{
    (void)signo;                        /* only here to interrupt read() */
}

/* Wait in read() and report how it ended. */
static void wait_in_read(const char *who)
{
    char c;
    ssize_t n = read(pipe_fds[0], &c, 1);
    if (n < 0 && errno == EINTR) {
        printf("%-6s: read() was interrupted: I got the signal\n", who);
        close(pipe_fds[1]);             /* wake the other one: end of file */
    } else {
        printf("%-6s: read() returned %zd\n", who, n);
    }
}

static void *worker(void *arg)
{
    (void)arg;
    wait_in_read("worker");
    return NULL;
}

static void *sender(void *arg)
{
    (void)arg;
    nanosleep(&(struct timespec){ .tv_nsec = 200000000 }, NULL);
    kill(getpid(), SIGINT);             /* to the process, not to a thread */
    return NULL;
}

int main(int argc, char *argv[])
{
    const char *who_blocks = argc > 1 ? argv[1] : "nobody";
    setvbuf(stdout, NULL, _IONBF, 0);
    if (pipe(pipe_fds) < 0) {
        perror("pipe");
        return 1;
    }

    struct sigaction sa = { .sa_handler = on_int };  /* no SA_RESTART */
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);

    sigset_t sigint, old;
    sigemptyset(&sigint);
    sigaddset(&sigint, SIGINT);

    /* A new thread starts with the signal mask of the thread that creates it. */
    pthread_t w, s;
    if (strcmp(who_blocks, "worker") == 0) {
        pthread_sigmask(SIG_BLOCK, &sigint, &old);
        pthread_create(&w, NULL, worker, NULL);
        pthread_sigmask(SIG_SETMASK, &old, NULL);
    } else {
        pthread_create(&w, NULL, worker, NULL);
    }
    pthread_sigmask(SIG_BLOCK, &sigint, &old);
    pthread_create(&s, NULL, sender, NULL);     /* the sender always blocks it */
    pthread_sigmask(SIG_SETMASK, &old, NULL);
    if (strcmp(who_blocks, "main") == 0)
        pthread_sigmask(SIG_BLOCK, &sigint, NULL);

    printf("SIGINT blocked by: %s\n", who_blocks);
    wait_in_read("main");
    pthread_join(w, NULL);
    pthread_join(s, NULL);
    return 0;
}
