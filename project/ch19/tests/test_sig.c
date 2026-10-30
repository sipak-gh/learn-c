/* test_sig.c - tests for sig.c. */
#include <errno.h>
#include <unistd.h>

#include "sig.h"
#include "test.h"
#include "tests.h"

void test_sig_quit_flag(void)
{
    CHECK_INT(sig_init(), 0);
    sig_quit = 0;
    raise(SIGTERM);                    /* runs the handler, does not exit */
    CHECK_INT(sig_quit, SIGTERM);
    sig_quit = 0;
}

void test_sig_pipe_ignored(void)
{
    CHECK_INT(sig_init(), 0);
    int fds[2];
    CHECK_INT(pipe(fds), 0);
    close(fds[0]);                     /* nobody will ever read */
    errno = 0;
    CHECK_INT(write(fds[1], "x", 1), -1);   /* without sig_init: killed */
    CHECK_INT(errno, EPIPE);
    close(fds[1]);
}

void test_sig_wake_fd(void)
{
    CHECK_INT(sig_init(), 0);
    int fd = sig_wake_fd();
    CHECK(fd >= 0);
    char c;
    CHECK_INT(read(fd, &c, 1), -1);    /* empty, and non-blocking */
    CHECK_INT(errno, EAGAIN);
    raise(SIGINT);
    CHECK_INT(read(fd, &c, 1), 1);     /* the handler wrote a byte */
    raise(SIGINT);
    raise(SIGINT);
    sig_drain(fd);
    CHECK_INT(read(fd, &c, 1), -1);    /* drained */
    close(fd);
    sig_quit = 0;
}
