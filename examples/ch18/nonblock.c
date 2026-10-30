/* nonblock.c - what "non-blocking" means for read() and write().
 * It makes a TCP connection to itself and never reads the other end. */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static int set_nonblock(int fd)
{
    int flags = fcntl(fd, F_GETFL);         /* the file status flags ... */
    if (flags < 0)
        return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);   /* ... plus one */
}

/* Connect a socket to a listening socket on a free port (port 0: the
 * kernel picks one); *server gets the accepted end. Returns the client end. */
static int connect_to_self(int *server)
{
    struct sockaddr_in a = { .sin_family = AF_INET };
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    socklen_t len = sizeof a;
    int lfd = socket(AF_INET, SOCK_STREAM, 0);
    int cfd = socket(AF_INET, SOCK_STREAM, 0);
    if (lfd < 0 || cfd < 0 || bind(lfd, (struct sockaddr *)&a, sizeof a) < 0
            || listen(lfd, 1) < 0
            || getsockname(lfd, (struct sockaddr *)&a, &len) < 0
            || connect(cfd, (struct sockaddr *)&a, sizeof a) < 0
            || (*server = accept(lfd, NULL, NULL)) < 0)
        return -1;
    close(lfd);
    return cfd;
}

int main(void)
{
    printf("EAGAIN = %d, EWOULDBLOCK = %d\n", EAGAIN, EWOULDBLOCK);

    int server;
    int fd = connect_to_self(&server);
    if (fd < 0 || set_nonblock(fd) < 0) {
        perror("setup");
        return 1;
    }

    /* Nothing has been sent to us: a blocking read() would wait for ever. */
    char c;
    ssize_t n = read(fd, &c, 1);
    printf("read:  %zd (%s)\n", n, n < 0 ? strerror(errno) : "data");

    /* Nobody reads the other end: the kernel's buffers fill up. */
    size_t size = 64 * 1024 * 1024;
    char *big = calloc(1, size);
    if (big == NULL)
        return 1;
    for (int i = 1; i <= 3; i++) {
        n = write(fd, big, size);
        if (n < 0)
            printf("write %d: -1 (%s)\n", i, strerror(errno));
        else
            printf("write %d: %zd of %zu bytes\n", i, n, size);
    }
    free(big);
    close(fd);
    close(server);
    return 0;
}
