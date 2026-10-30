/* trickle.c - send standard input to a server one byte at a time.
 * usage: trickle PORT [DELAY_MS]
 * Each byte goes out in its own write() and its own TCP segment, DELAY_MS
 * (default 1) apart: a test for the server's framing. Then it reads and
 * prints the replies until the server closes the connection. */
#define _POSIX_C_SOURCE 200809L
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

int main(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr, "usage: trickle PORT [DELAY_MS]\n");
        return 2;
    }
    long ms = argc > 2 ? atol(argv[2]) : 1;
    struct timespec delay = { ms / 1000, ms % 1000 * 1000000L };

    struct sockaddr_in addr = { .sin_family = AF_INET };
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons((unsigned short)atoi(argv[1]));
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0 || connect(fd, (struct sockaddr *)&addr, sizeof addr) < 0) {
        perror("connect");
        return 1;
    }
    /* Without this, TCP would collect our tiny writes into bigger segments
     * (Nagle's algorithm). */
    int on = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &on, sizeof on);

    int ch;
    while ((ch = getchar()) != EOF) {
        unsigned char b = (unsigned char)ch;
        if (write(fd, &b, 1) != 1) {
            perror("write");
            return 1;
        }
        nanosleep(&delay, NULL);
    }
    shutdown(fd, SHUT_WR);              /* "that's all": the server reads EOF */

    char buf[4096];
    ssize_t n;
    while ((n = read(fd, buf, sizeof buf)) > 0)
        fwrite(buf, 1, (size_t)n, stdout);
    close(fd);
    return 0;
}
