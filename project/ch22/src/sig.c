/* sig.c - signal handling shared by server and client. */
#include "sig.h"

#include <errno.h>
#include <stddef.h>
#include <unistd.h>

#include "io.h"

volatile sig_atomic_t sig_quit = 0;
volatile sig_atomic_t sig_winch = 0;

/* The write end of the self-pipe, or -1. */
static int wake_write_fd = -1;

/* A signal handler may run between any two instructions of the program,
 * so it must do almost nothing: here, set a flag and write one byte.
 * Only async-signal-safe functions may be called in a handler (write is,
 * printf and malloc are not), and it must not change errno behind the
 * back of the code it interrupted. */
static void wake(void)
{
    int saved = errno;
    if (wake_write_fd >= 0) {
        /* If the pipe is full, a wake-up is already pending: fine. */
        ssize_t r = write(wake_write_fd, "!", 1);
        (void)r;
    }
    errno = saved;
}

static void on_quit(int signo)
{
    sig_quit = signo;
    wake();
}

static void on_winch(int signo)
{
    (void)signo;
    sig_winch = 1;
    wake();
}

int sig_init(void)
{
    struct sigaction sa;

    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sa.sa_handler = SIG_IGN;
    if (sigaction(SIGPIPE, &sa, NULL) < 0)
        return -1;

    /* No SA_RESTART in sa_flags: a blocking read() interrupted by the
     * signal fails with EINTR, so the main loop wakes up and sees sig_quit. */
    sa.sa_handler = on_quit;
    if (sigaction(SIGINT, &sa, NULL) < 0 || sigaction(SIGTERM, &sa, NULL) < 0)
        return -1;
    return 0;
}

int sig_wake_fd(void)
{
    int fds[2];
    if (pipe(fds) < 0)
        return -1;
    /* Both ends non-blocking: the handler must never block in write(),
     * and sig_drain() reads until the pipe is empty. */
    if (set_nonblock(fds[0]) < 0 || set_nonblock(fds[1]) < 0) {
        int saved = errno;
        close(fds[0]);
        close(fds[1]);
        errno = saved;
        return -1;
    }
    wake_write_fd = fds[1];
    return fds[0];
}

int sig_watch_winch(void)
{
    struct sigaction sa;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sa.sa_handler = on_winch;
    return sigaction(SIGWINCH, &sa, NULL);
}

void sig_drain(int fd)
{
    char buf[64];
    while (read(fd, buf, sizeof buf) > 0)
        continue;
}
