/* naive.c - a blocking chat client that waits on one thing at a time.
 * It reads a line from stdin, sends it to 127.0.0.1:PORT, then waits for
 * the server's answer. Every message is stamped with the time it is seen. */
#define _POSIX_C_SOURCE 200809L
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

static struct timespec start;

/* Seconds since the program started. */
static double elapsed(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (double)(now.tv_sec - start.tv_sec) + (now.tv_nsec - start.tv_nsec) / 1e9;
}

int main(int argc, char *argv[])
{
    if (argc != 2) {
        fprintf(stderr, "usage: naive PORT\n");
        return 2;
    }
    clock_gettime(CLOCK_MONOTONIC, &start);
    struct sockaddr_in addr = { .sin_family = AF_INET };
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons((unsigned short)atoi(argv[1]));
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0 || connect(fd, (struct sockaddr *)&addr, sizeof addr) < 0) {
        perror("connect");
        return 1;
    }

    char line[256], buf[512];
    for (;;) {
        printf("[%.1fs] waiting for stdin\n", elapsed());
        fflush(stdout);
        if (fgets(line, sizeof line, stdin) == NULL)
            break;                                  /* blocks here ... */
        printf("[%.1fs] sending %s", elapsed(), line);
        if (write(fd, line, strlen(line)) < 0) {
            perror("write");
            return 1;
        }
        printf("[%.1fs] waiting for the server\n", elapsed());
        fflush(stdout);
        ssize_t n = read(fd, buf, sizeof buf);      /* ... or here */
        if (n <= 0) {
            printf("[%.1fs] the server hung up\n", elapsed());
            break;
        }
        printf("[%.1fs] received %zd bytes:\n%.*s", elapsed(), n, (int)n, buf);
    }
    close(fd);
    return 0;
}
