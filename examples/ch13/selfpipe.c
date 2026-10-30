/* selfpipe.c - wait for input OR a signal, without a race.
 * Echoes stdin lines until end of file or SIGINT/SIGTERM. */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int wake_fds[2];                 /* the "self-pipe" */

static void on_signal(int signo)
{
    int saved = errno;                  /* write() may change errno */
    unsigned char b = (unsigned char)signo;
    if (write(wake_fds[1], &b, 1) < 0) {
        /* pipe full: a wake-up is already waiting, nothing to do */
    }
    errno = saved;
}

int main(void)
{
    if (pipe(wake_fds) < 0)
        return 1;
    /* Non-blocking, so the handler can never get stuck on a full pipe. */
    fcntl(wake_fds[1], F_SETFL, fcntl(wake_fds[1], F_GETFL) | O_NONBLOCK);

    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_signal;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    /* poll() (Chapter 18) sleeps until one of several fds is readable. */
    struct pollfd pfd[2] = {
        { .fd = STDIN_FILENO, .events = POLLIN },
        { .fd = wake_fds[0], .events = POLLIN },
    };
    for (;;) {
        if (poll(pfd, 2, -1) < 0) {
            if (errno == EINTR)
                continue;               /* the pipe will tell us why */
            perror("poll");
            return 1;
        }
        if (pfd[1].revents & POLLIN) {
            unsigned char b;
            if (read(wake_fds[0], &b, 1) == 1)
                printf("signal %d (%s): bye\n", b, strsignal(b));
            return 0;
        }
        if (pfd[0].revents & (POLLIN | POLLHUP)) {
            char buf[256];
            ssize_t n = read(STDIN_FILENO, buf, sizeof buf);
            if (n <= 0) {
                printf("end of input\n");
                return 0;
            }
            printf("read %d bytes: %.*s", (int)n, (int)n, buf);
            fflush(stdout);
        }
    }
}
