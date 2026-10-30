/* listen-only.c - listen on a port but never accept(), then exit.
 * usage: listen-only PORT SECONDS */
#define _POSIX_C_SOURCE 200809L
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

int main(int argc, char *argv[])
{
    if (argc != 3) {
        fprintf(stderr, "usage: listen-only PORT SECONDS\n");
        return 2;
    }
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("socket");
        return 1;
    }
    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_port = htons((uint16_t)atoi(argv[1]));
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);     /* 127.0.0.1 */
    if (bind(fd, (struct sockaddr *)&a, sizeof a) < 0) {
        perror("bind");
        return 1;
    }
    if (listen(fd, 5) < 0) {
        perror("listen");
        return 1;
    }
    printf("listening on 127.0.0.1:%s, not accepting\n", argv[1]);
    fflush(stdout);
    sleep((unsigned)atoi(argv[2]));
    close(fd);
    return 0;
}
