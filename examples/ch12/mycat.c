/* mycat.c - copy files (or stdin) to stdout with read() and write(). */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

/* Write all n bytes: write() may write fewer than asked for. Returns 0,
 * or -1 with errno set. */
static int write_all(int fd, const char *p, size_t n)
{
    while (n > 0) {
        ssize_t w = write(fd, p, n);
        if (w < 0) {
            if (errno == EINTR)
                continue;              /* a signal arrived first: retry */
            return -1;
        }
        p += w;
        n -= (size_t)w;
    }
    return 0;
}

/* Copy fd in to stdout. name is for error messages. Returns 0 or -1. */
static int copy(int in, const char *name)
{
    char buf[4096];
    for (;;) {
        ssize_t r = read(in, buf, sizeof buf);
        if (r == 0)
            return 0;                  /* end of file */
        if (r < 0) {
            if (errno == EINTR)
                continue;
            fprintf(stderr, "mycat: %s: %s\n", name, strerror(errno));
            return -1;
        }
        if (write_all(STDOUT_FILENO, buf, (size_t)r) < 0) {
            fprintf(stderr, "mycat: write error: %s\n", strerror(errno));
            return -1;
        }
    }
}

int main(int argc, char *argv[])
{
    if (argc == 1)
        return copy(STDIN_FILENO, "stdin") == 0 ? 0 : 1;

    int status = 0;
    for (int i = 1; i < argc; i++) {
        int fd = open(argv[i], O_RDONLY);
        if (fd < 0) {
            fprintf(stderr, "mycat: %s: %s\n", argv[i], strerror(errno));
            status = 1;
            continue;
        }
        if (copy(fd, argv[i]) < 0)
            status = 1;
        close(fd);
    }
    return status;
}
