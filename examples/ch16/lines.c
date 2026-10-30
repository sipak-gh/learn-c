/* lines.c - read lines from a TCP server: one read() is not one line.
 * Connects to 127.0.0.1:PORT and shows what each read() returned and
 * which complete lines the client could cut out of its buffer. */
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
    if (argc != 2) {
        fprintf(stderr, "usage: lines PORT\n");
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

    char buf[512];              /* received bytes not yet cut into lines */
    size_t len = 0;
    for (;;) {
        ssize_t n = read(fd, buf + len, sizeof buf - 1 - len);
        if (n < 0 && errno == EINTR)
            continue;
        if (n < 0) {
            perror("read");
            return 1;
        }
        if (n == 0) {
            printf("end of file (%zu bytes left over)\n", len);
            break;
        }
        printf("read() returned %zd bytes\n", n);
        len += (size_t)n;

        /* Cut out every complete line. */
        char *start = buf, *nl;
        while ((nl = memchr(start, '\n', len - (size_t)(start - buf))) != NULL) {
            *nl = '\0';
            if (nl > start && nl[-1] == '\r')
                nl[-1] = '\0';          /* the protocol ends lines with CRLF */
            printf("  line: \"%s\"\n", start);
            start = nl + 1;
        }
        len -= (size_t)(start - buf);
        memmove(buf, start, len);       /* keep the partial line */
        if (len == sizeof buf - 1) {
            fprintf(stderr, "lines: line too long\n");
            return 1;
        }
    }
    close(fd);
    return 0;
}
