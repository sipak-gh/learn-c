/* somaxconn.c - the C library's idea of the biggest listen() backlog.
 * usage: somaxconn [PORT]   (with a port: listen(fd, SOMAXCONN) for 1 s) */
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
    printf("SOMAXCONN = %d\n", SOMAXCONN);
    if (argc < 2)
        return 0;

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_port = htons((uint16_t)atoi(argv[1]));
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (fd < 0 || bind(fd, (struct sockaddr *)&a, sizeof a) < 0
            || listen(fd, SOMAXCONN) < 0) {
        perror("somaxconn");
        return 1;
    }
    sleep(1);                           /* time to look with ss */
    return 0;
}
