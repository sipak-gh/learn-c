/* echo_server.c - a blocking TCP echo server, one client at a time.
 * usage: echo_server [-n] PORT
 *   -n  don't set SO_REUSEADDR (to see why servers set it) */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

/* Write all n bytes, however many write() calls that takes. */
static int write_all(int fd, const char *p, size_t n)
{
    while (n > 0) {
        ssize_t w = write(fd, p, n);
        if (w < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        p += w;
        n -= (size_t)w;
    }
    return 0;
}

static int listen_on(const char *port, int reuse)
{
    struct addrinfo hints, *list;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE | AI_NUMERICSERV;
    int rc = getaddrinfo("127.0.0.1", port, &hints, &list);
    if (rc != 0) {
        fprintf(stderr, "echo_server: %s\n", gai_strerror(rc));
        return -1;
    }
    int fd = socket(list->ai_family, list->ai_socktype, list->ai_protocol);
    if (fd < 0) {
        perror("socket");
        freeaddrinfo(list);
        return -1;
    }
    int on = 1;
    if (reuse && setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof on) < 0)
        perror("setsockopt");
    if (bind(fd, list->ai_addr, list->ai_addrlen) < 0 || listen(fd, 8) < 0) {
        fprintf(stderr, "echo_server: port %s: %s\n", port, strerror(errno));
        close(fd);
        fd = -1;
    }
    freeaddrinfo(list);
    return fd;
}

int main(int argc, char *argv[])
{
    int reuse = !(argc == 3 && strcmp(argv[1], "-n") == 0);
    if (argc != 2 && reuse) {
        fprintf(stderr, "usage: echo_server [-n] PORT\n");
        return 2;
    }
    int lfd = listen_on(argv[argc - 1], reuse);
    if (lfd < 0)
        return 1;
    printf("echo_server: listening on 127.0.0.1:%s\n", argv[argc - 1]);
    fflush(stdout);

    for (;;) {
        struct sockaddr_storage ss;
        socklen_t len = sizeof ss;
        int cfd = accept(lfd, (struct sockaddr *)&ss, &len);  /* waits */
        if (cfd < 0) {
            perror("accept");
            continue;
        }
        char host[64], serv[8];
        getnameinfo((struct sockaddr *)&ss, len, host, sizeof host, serv, sizeof serv,
                    NI_NUMERICHOST | NI_NUMERICSERV);
        printf("client %s:%s connected\n", host, serv);
        fflush(stdout);

        char buf[512];
        ssize_t n;
        while ((n = read(cfd, buf, sizeof buf)) > 0)  /* until the client closes */
            if (write_all(cfd, buf, (size_t)n) < 0)
                break;
        printf("client %s:%s %s\n", host, serv, n == 0 ? "disconnected" : strerror(errno));
        fflush(stdout);
        close(cfd);
    }
}
