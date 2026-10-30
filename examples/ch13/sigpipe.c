/* sigpipe.c - write to a pipe nobody reads.
 * usage: sigpipe           (default action: SIGPIPE kills us)
 *        sigpipe ignore    (SIGPIPE ignored: write fails with EPIPE) */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char *argv[])
{
    if (argc > 1 && strcmp(argv[1], "ignore") == 0) {
        struct sigaction sa;
        memset(&sa, 0, sizeof sa);
        sa.sa_handler = SIG_IGN;
        sigemptyset(&sa.sa_mask);
        sigaction(SIGPIPE, &sa, NULL);
    }

    int fds[2];
    if (pipe(fds) < 0)
        return 1;
    close(fds[0]);                      /* the only reader is gone */

    fprintf(stderr, "writing...\n");
    if (write(fds[1], "hello", 5) < 0)
        fprintf(stderr, "write: %s (errno %d)\n", strerror(errno), errno);
    fprintf(stderr, "still alive\n");
    return 0;
}
