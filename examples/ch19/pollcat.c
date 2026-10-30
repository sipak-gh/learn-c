/* pollcat.c - a tiny nc: copy stdin to a TCP server and the server's
 * replies to stdout, both at once, with poll(). */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <netdb.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static int connect_to(const char *host, const char *port)
{
    struct addrinfo hints = { .ai_family = AF_UNSPEC, .ai_socktype = SOCK_STREAM };
    struct addrinfo *res;
    int err = getaddrinfo(host, port, &hints, &res);
    if (err != 0) {
        fprintf(stderr, "pollcat: %s: %s\n", host, gai_strerror(err));
        return -1;
    }
    int fd = -1;
    for (struct addrinfo *ai = res; ai != NULL && fd < 0; ai = ai->ai_next) {
        fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd >= 0 && connect(fd, ai->ai_addr, ai->ai_addrlen) < 0) {
            close(fd);
            fd = -1;
        }
    }
    if (fd < 0)
        fprintf(stderr, "pollcat: cannot connect: %s\n", strerror(errno));
    freeaddrinfo(res);
    return fd;
}

/* Copy what one read() gets from 'from' to 'to'. Returns the byte count,
 * 0 at end of file, -1 on error. (A real program loops over short
 * writes: see write_all in Chapter 12.) */
static ssize_t pass(int from, int to)
{
    char buf[4096];
    ssize_t n = read(from, buf, sizeof buf);
    if (n > 0 && write(to, buf, (size_t)n) != n)
        return -1;
    return n;
}

int main(int argc, char *argv[])
{
    if (argc != 3) {
        fprintf(stderr, "usage: pollcat HOST PORT\n");
        return 2;
    }
    int sock = connect_to(argv[1], argv[2]);
    if (sock < 0)
        return 1;

    int in = STDIN_FILENO;               /* -1 once stdin is at its end */
    for (;;) {
        struct pollfd fds[2] = {
            { .fd = sock, .events = POLLIN },
            { .fd = in,   .events = POLLIN },  /* poll() skips fd -1 */
        };
        if (poll(fds, 2, -1) < 0) {
            if (errno == EINTR)
                continue;
            perror("poll");
            return 1;
        }
        if (fds[1].revents != 0 && pass(in, sock) <= 0) {
            in = -1;
            shutdown(sock, SHUT_WR);     /* "I'm done talking", keep listening */
        }
        if (fds[0].revents != 0) {
            ssize_t n = pass(sock, STDOUT_FILENO);
            if (n <= 0) {
                fprintf(stderr, "pollcat: %s\n",
                        n == 0 ? "the server closed the connection" : strerror(errno));
                break;
            }
        }
    }
    close(sock);
    return 0;
}
