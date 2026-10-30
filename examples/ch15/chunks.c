/* chunks.c - accept one connection and show what each read() returns.
 * usage: chunks [-q] PORT     (-q: only count the reads) */
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
    int quiet = argc == 3 && strcmp(argv[1], "-q") == 0;
    if (argc != 2 && !quiet) {
        fprintf(stderr, "usage: chunks [-q] PORT\n");
        return 2;
    }
    int lfd = socket(AF_INET, SOCK_STREAM, 0);
    int on = 1;
    setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof on);
    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_port = htons((uint16_t)atoi(argv[argc - 1]));
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (lfd < 0 || bind(lfd, (struct sockaddr *)&a, sizeof a) < 0 || listen(lfd, 1) < 0) {
        perror("chunks");
        return 1;
    }
    int fd = accept(lfd, NULL, NULL);   /* NULL: we don't want the address */
    close(lfd);                         /* one connection is all we take */
    if (fd < 0) {
        perror("accept");
        return 1;
    }

    static char buf[1 << 20];           /* 1 MiB: room for big reads */
    ssize_t n;
    long reads = 0, total = 0, smallest = -1, largest = 0;
    while ((n = read(fd, buf, sizeof buf)) > 0) {
        reads++;
        total += n;
        if (smallest < 0 || n < smallest)
            smallest = n;
        if (n > largest)
            largest = n;
        if (quiet)
            continue;
        printf("read %2d bytes: [", (int)n);
        for (ssize_t i = 0; i < n; i++) {   /* make \r and \n visible */
            if (buf[i] == '\r')
                printf("\\r");
            else if (buf[i] == '\n')
                printf("\\n");
            else
                putchar(buf[i]);
        }
        printf("]\n");
    }
    printf("%ld reads, %ld bytes", reads, total);
    if (reads > 0)
        printf(" (smallest %ld, largest %ld)", smallest, largest);
    printf("\n");
    close(fd);
    return 0;
}
