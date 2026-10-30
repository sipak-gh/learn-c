/* wcclient.c - send stdin to wcserver on 127.0.0.1:PORT, print its answer.
 *   ./wcclient PORT shutdown   send, shutdown(SHUT_WR), then read (correct)
 *   ./wcclient PORT nothing    send, then read: both sides wait forever
 *   ./wcclient PORT close      send, close(), then read: too late */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

int main(int argc, char *argv[])
{
    if (argc != 3) {
        fprintf(stderr, "usage: wcclient PORT shutdown|nothing|close\n");
        return 2;
    }
    struct sockaddr_in addr = { .sin_family = AF_INET };
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons((unsigned short)atoi(argv[1]));
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0 || connect(fd, (struct sockaddr *)&addr, sizeof addr) < 0) {
        perror("connect");
        return 1;
    }

    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, stdin)) > 0) {
        if (write(fd, buf, n) < 0) {
            perror("write");
            return 1;
        }
    }

    if (strcmp(argv[2], "shutdown") == 0)
        shutdown(fd, SHUT_WR);          /* "I have finished sending" */
    else if (strcmp(argv[2], "close") == 0)
        close(fd);                      /* gives up the descriptor */

    ssize_t got;
    while ((got = read(fd, buf, sizeof buf)) > 0)
        fwrite(buf, 1, (size_t)got, stdout);
    if (got < 0) {
        fprintf(stderr, "wcclient: read: %s\n", strerror(errno));
        return 1;
    }
    close(fd);
    return 0;
}
