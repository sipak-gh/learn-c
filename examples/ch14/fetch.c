/* fetch.c - a tiny TCP client: connect, send a line, print the reply.
 * usage: fetch HOST PORT [LINE] */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

int main(int argc, char *argv[])
{
    if (argc < 3) {
        fprintf(stderr, "usage: fetch HOST PORT [LINE]\n");
        return 2;
    }

    /* 1. Name and port -> a list of socket addresses. */
    struct addrinfo hints, *list;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;            /* IPv4 or IPv6, whatever works */
    hints.ai_socktype = SOCK_STREAM;        /* TCP */
    int rc = getaddrinfo(argv[1], argv[2], &hints, &list);
    if (rc != 0) {
        fprintf(stderr, "fetch: %s: %s\n", argv[1], gai_strerror(rc));
        return 1;
    }

    /* 2. Try each address until one connects. */
    int fd = -1;
    for (struct addrinfo *ai = list; ai != NULL; ai = ai->ai_next) {
        fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd < 0)
            continue;
        if (connect(fd, ai->ai_addr, ai->ai_addrlen) == 0)
            break;                          /* the handshake is done */
        fprintf(stderr, "fetch: connect: %s\n", strerror(errno));
        close(fd);
        fd = -1;
    }
    freeaddrinfo(list);
    if (fd < 0)
        return 1;

    /* 3. Send a request, if any. A socket is a file descriptor: write() works. */
    if (argc > 3) {
        char line[512];
        int n = snprintf(line, sizeof line, "%s\r\n", argv[3]);
        if (n < 0 || (size_t)n >= sizeof line || write(fd, line, (size_t)n) != n) {
            fprintf(stderr, "fetch: cannot send\n");
            close(fd);
            return 1;
        }
    }

    /* 4. Print everything until the server closes (read returns 0). */
    char buf[4096];
    ssize_t n;
    long total = 0;
    while ((n = read(fd, buf, sizeof buf)) > 0) {
        fwrite(buf, 1, (size_t)n, stdout);
        total += n;
    }
    if (n < 0)
        fprintf(stderr, "fetch: read: %s\n", strerror(errno));
    fprintf(stderr, "fetch: %ld bytes, connection closed\n", total);
    close(fd);
    return n < 0 ? 1 : 0;
}
