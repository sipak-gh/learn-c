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
