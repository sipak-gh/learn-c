/* nohints.c - getaddrinfo() without hints, and why a client must pass them.
 * Lists every result for "localhost" PORT and tries to connect to each. */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static const char *type_name(int type)
{
    switch (type) {
    case SOCK_STREAM: return "SOCK_STREAM";
    case SOCK_DGRAM:  return "SOCK_DGRAM";
    case SOCK_RAW:    return "SOCK_RAW";
    default:          return "other";
    }
}

int main(int argc, char *argv[])
{
    if (argc != 2) {
        fprintf(stderr, "usage: nohints PORT\n");
        return 2;
    }
    struct addrinfo *res;
    int rc = getaddrinfo("localhost", argv[1], NULL, &res);   /* no hints! */
    if (rc != 0) {
        fprintf(stderr, "nohints: %s\n", gai_strerror(rc));
        return 1;
    }
    for (struct addrinfo *ai = res; ai != NULL; ai = ai->ai_next) {
        printf("%-11s protocol %2d: ", type_name(ai->ai_socktype), ai->ai_protocol);
        int fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd < 0) {
            printf("socket: %s\n", strerror(errno));
            continue;
        }
        if (connect(fd, ai->ai_addr, ai->ai_addrlen) < 0)
            printf("connect: %s\n", strerror(errno));
        else
            printf("connect: OK\n");
        close(fd);
    }
    freeaddrinfo(res);
    return 0;
}
