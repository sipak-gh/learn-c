/* io.c - helpers for reading and writing file descriptors. */
#include "io.h"

#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

int write_all(int fd, const void *buf, size_t n)
{
    const char *p = buf;
    while (n > 0) {
        ssize_t w = write(fd, p, n);
        if (w < 0) {
            if (errno == EINTR)
                continue;               /* interrupted before writing: retry */
            return -1;
        }
        p += w;                         /* a short write: send the rest */
        n -= (size_t)w;
    }
    return 0;
}

ssize_t read_exact(int fd, void *buf, size_t n)
{
    char *p = buf;
    size_t got = 0;
    while (got < n) {
        ssize_t r = read(fd, p + got, n - got);
        if (r < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        if (r == 0)
            break;                      /* end of file */
        got += (size_t)r;
    }
    return (ssize_t)got;
}

ssize_t read_into(int fd, struct strbuf *sb, size_t max)
{
    if (sb_reserve(sb, max) < 0) {
        errno = ENOMEM;
        return -1;
    }
    ssize_t r = read(fd, sb->data + sb->len, max);
    if (r > 0) {
        sb->len += (size_t)r;
        sb->data[sb->len] = '\0';
    }
    return r;
}

int set_nonblock(int fd)
{
    int flags = fcntl(fd, F_GETFL);
    if (flags < 0)
        return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}
