/* pollsig.c - poll() and signals: EINTR, a lost wake-up, and the self-pipe. */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static volatile sig_atomic_t got_signal;
static int wake_write = -1;             /* the self-pipe's write end, or -1 */

static void on_signal(int signo)
{
    int saved = errno;
    got_signal = signo;
    if (wake_write >= 0) {
        ssize_t r = write(wake_write, "!", 1);
        (void)r;
    }
    errno = saved;
}

static double now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + ts.tv_nsec / 1e9;
}

static void report(const char *what, int n, double start)
{
    printf("%-32s poll() = %2d (%s) after %.1f s\n", what, n,
           n < 0 ? strerror(errno) : n == 0 ? "timeout" : "ready", now() - start);
}

int main(void)
{
    struct sigaction sa = { .sa_handler = on_signal, .sa_flags = SA_RESTART };
    sigemptyset(&sa.sa_mask);
    sigaction(SIGALRM, &sa, NULL);
    sigaction(SIGUSR1, &sa, NULL);

    /* 1. A signal during poll(): EINTR, although we asked for SA_RESTART. */
    double start = now();
    alarm(1);
    int n = poll(NULL, 0, 5000);
    report("1. SIGALRM during poll:", n, start);

    /* 2. A signal just BEFORE poll(): it sets the flag, but poll() does
     *    not know, and sleeps until its timeout. raise() plays the part
     *    of a signal that arrives at the worst moment. */
    got_signal = 0;
    start = now();
    if (got_signal == 0) {
        raise(SIGUSR1);                 /* ... right here */
        n = poll(NULL, 0, 2000);
    }
    report("2. SIGUSR1 just before poll:", n, start);

    /* 3. The same with a self-pipe: the handler's byte waits in the pipe,
     *    so poll() returns at once. */
    int p[2];
    if (pipe(p) < 0)
        return 1;
    fcntl(p[1], F_SETFL, O_NONBLOCK);
    wake_write = p[1];
    got_signal = 0;
    start = now();
    struct pollfd fds[1] = { { .fd = p[0], .events = POLLIN } };
    if (got_signal == 0) {
        raise(SIGUSR1);
        n = poll(fds, 1, 2000);
    }
    report("3. the same, with a self-pipe:", n, start);
    return 0;
}
