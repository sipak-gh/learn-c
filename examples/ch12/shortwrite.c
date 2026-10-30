/* shortwrite.c - a write() that writes less than asked for. The pipe has
 * no reader yet, so it fills up; in non-blocking mode write() then returns
 * what fitted, and the next write() fails with EAGAIN. */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static char data[100000];

int main(void)
{
    int fds[2];                        /* fds[0]: read end, fds[1]: write end */
    if (pipe(fds) < 0) {
        perror("pipe");
        return 1;
    }
    int flags = fcntl(fds[1], F_GETFL);
    fcntl(fds[1], F_SETFL, flags | O_NONBLOCK);

    ssize_t w = write(fds[1], data, sizeof data);
    printf("asked for %zu, write returned %zd\n", sizeof data, w);
    w = write(fds[1], data, sizeof data);
    printf("again: write returned %zd (%s)\n", w,
           w < 0 && (errno == EAGAIN || errno == EWOULDBLOCK) ? "EAGAIN" : strerror(errno));

    char buf[10000];
    ssize_t r = read(fds[0], buf, sizeof buf);
    printf("read %zd bytes; now write returns %zd\n", r, write(fds[1], data, sizeof data));
    close(fds[0]);
    close(fds[1]);
    return 0;
}
