/* signal-semantics.c - what does plain signal() give you? */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static volatile sig_atomic_t usr1_count;
static int pipefd[2];

static void on_usr1(int sig)
{
    (void)sig;
    usr1_count++;
}

static void on_alrm(int sig)
{
    (void)sig;
    if (write(pipefd[1], "x", 1) < 0) { /* data arrives while read() is interrupted */ }
}

int main(void)
{
    char c;
    ssize_t n;

    /* 1. Is a blocking read() restarted after the handler returns? */
    if (pipe(pipefd) != 0)
        return 1;
    signal(SIGALRM, on_alrm);
    alarm(1);
    n = read(pipefd[0], &c, 1);
    if (n < 0)
        printf("read: -1, errno=%s (not restarted)\n", strerror(errno));
    else
        printf("read: %d byte(s) (restarted after the handler)\n", (int)n);
    fflush(stdout);

    /* 2. Does the handler stay installed after it runs once? */
    signal(SIGUSR1, on_usr1);
    raise(SIGUSR1);
    printf("after 1st SIGUSR1: count=%d\n", (int)usr1_count);
    fflush(stdout);
    raise(SIGUSR1);
    printf("after 2nd SIGUSR1: count=%d (handler stayed installed)\n", (int)usr1_count);

    return 0;
}
