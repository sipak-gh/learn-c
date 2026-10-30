/* connect.c - connect to HOST PORT and show both ends of the connection.
 * Try each address the name resolves to, in order, like net_connect(). */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

/* "127.0.0.1:6667" or "[::1]:6667" */
static void addr_str(const struct sockaddr *sa, socklen_t len, char *out, size_t size)
{
    char host[INET6_ADDRSTRLEN], port[8];
    if (getnameinfo(sa, len, host, sizeof host, port, sizeof port,
                    NI_NUMERICHOST | NI_NUMERICSERV) != 0) {
        snprintf(out, size, "?");
        return;
    }
    snprintf(out, size, sa->sa_family == AF_INET6 ? "[%s]:%s" : "%s:%s", host, port);
}

int main(int argc, char *argv[])
{
    if (argc != 3) {
        fprintf(stderr, "usage: connect HOST PORT\n");
        return 2;
    }
    struct addrinfo hints = { .ai_family = AF_UNSPEC, .ai_socktype = SOCK_STREAM };
    struct addrinfo *res;
    int rc = getaddrinfo(argv[1], argv[2], &hints, &res);
    if (rc != 0) {
        fprintf(stderr, "connect: %s: %s\n", argv[1], gai_strerror(rc));
        return 1;
    }

    int fd = -1;
    for (struct addrinfo *ai = res; ai != NULL && fd < 0; ai = ai->ai_next) {
        char addr[80];
        addr_str(ai->ai_addr, ai->ai_addrlen, addr, sizeof addr);
        printf("trying %s ... ", addr);
        fflush(stdout);                 /* show it before connect() blocks */
        fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd < 0) {
            printf("socket: %s\n", strerror(errno));
            continue;
        }
        if (connect(fd, ai->ai_addr, ai->ai_addrlen) < 0) {
            printf("%s\n", strerror(errno));
            close(fd);
            fd = -1;
            continue;
        }
        printf("connected\n");
    }
    freeaddrinfo(res);
    if (fd < 0)
        return 1;

    /* The kernel picked our local address and port during connect(). */
    struct sockaddr_storage ss;
    socklen_t len = sizeof ss;
    char addr[80];
    if (getsockname(fd, (struct sockaddr *)&ss, &len) == 0) {
        addr_str((struct sockaddr *)&ss, len, addr, sizeof addr);
        printf("local end:  %s\n", addr);
    }
    len = sizeof ss;
    if (getpeername(fd, (struct sockaddr *)&ss, &len) == 0) {
        addr_str((struct sockaddr *)&ss, len, addr, sizeof addr);
        printf("remote end: %s\n", addr);
    }
    close(fd);
    return 0;
}
