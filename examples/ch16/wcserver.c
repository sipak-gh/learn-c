/* wcserver.c - a network wc: for each client, count the lines and bytes
 * it sends until end of file, then send back the counts and hang up.
 * Serves one client at a time on 127.0.0.1:PORT. */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <unistd.h>

int main(int argc, char *argv[])
{
    if (argc != 2) {
        fprintf(stderr, "usage: wcserver PORT\n");
        return 2;
    }
    signal(SIGPIPE, SIG_IGN);
    struct sockaddr_in addr = { .sin_family = AF_INET };
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons((unsigned short)atoi(argv[1]));
    int on = 1;
    int lfd = socket(AF_INET, SOCK_STREAM, 0);
    if (lfd < 0 || setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof on) < 0
            || bind(lfd, (struct sockaddr *)&addr, sizeof addr) < 0
            || listen(lfd, 8) < 0) {
        perror("wcserver");
        return 1;
    }
    for (;;) {
        int fd = accept(lfd, NULL, NULL);
        if (fd < 0)
            continue;
        long lines = 0, bytes = 0;
        char buf[4096];
        ssize_t n;
        while ((n = read(fd, buf, sizeof buf)) != 0) {
            if (n < 0 && errno == EINTR)
                continue;
            if (n < 0)
                break;
            for (ssize_t i = 0; i < n; i++)
                lines += buf[i] == '\n';
            bytes += n;
        }
        /* read() returned 0: the client has finished sending. Answer. */
        char reply[64];
        int len = snprintf(reply, sizeof reply, "%ld lines, %ld bytes\n", lines, bytes);
        if (write(fd, reply, (size_t)len) < 0)
            perror("wcserver: write");
        close(fd);
    }
}
