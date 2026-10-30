/* ticker.c - wait for input with poll(), but never longer than 1 second. */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(void)
{
    for (;;) {
        struct pollfd pfd = { .fd = STDIN_FILENO, .events = POLLIN };
        int ready = poll(&pfd, 1, 1000);         /* timeout in milliseconds */
        if (ready < 0) {
            if (errno == EINTR)
                continue;
            perror("poll");
            return 1;
        }
        if (ready == 0) {                        /* nothing within 1 s */
            printf("tick\n");
            fflush(stdout);
            continue;
        }
        /* POLLIN: data to read. POLLHUP: the writer is gone. Either way,
         * read() tells us what happened, and won't block now. */
        char buf[256];
        ssize_t n = read(STDIN_FILENO, buf, sizeof buf);
        if (n < 0) {
            perror("read");
            return 1;
        }
        if (n == 0) {
            printf("end of input\n");
            return 0;
        }
        printf("read %zd bytes: %.*s", n, (int)n, buf);
        fflush(stdout);
    }
}
