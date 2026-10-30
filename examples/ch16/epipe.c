/* epipe.c - what happens when you write to a connection the peer closed.
 *   ./epipe          default SIGPIPE action: the second write kills us
 *   ./epipe ignore   SIGPIPE ignored: the second write fails with EPIPE */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

/* Sleep for ms milliseconds (usleep is gone from POSIX: use nanosleep). */
static void sleep_ms(long ms)
{
    struct timespec ts = { ms / 1000, ms % 1000 * 1000000 };
    nanosleep(&ts, NULL);
}

int main(int argc, char *argv[])
{
    if (argc > 1 && strcmp(argv[1], "ignore") == 0)
        signal(SIGPIPE, SIG_IGN);

    /* A listener on 127.0.0.1, port 0: the kernel picks a free port. */
    struct sockaddr_in addr = { .sin_family = AF_INET };
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    socklen_t len = sizeof addr;
    int lfd = socket(AF_INET, SOCK_STREAM, 0);
    if (lfd < 0 || bind(lfd, (struct sockaddr *)&addr, sizeof addr) < 0
            || listen(lfd, 1) < 0
            || getsockname(lfd, (struct sockaddr *)&addr, &len) < 0) {
        perror("listen");
        return 1;
    }

    /* Connect to ourselves, accept, and let the "server" side hang up. */
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0 || connect(fd, (struct sockaddr *)&addr, sizeof addr) < 0) {
        perror("connect");
        return 1;
    }
    int peer = accept(lfd, NULL, NULL);
    close(peer);
    sleep_ms(100);                     /* let the FIN arrive */

    for (int i = 1; i <= 3; i++) {
        ssize_t n = write(fd, "hello\n", 6);
        if (n < 0)
            printf("write %d: %s\n", i, strerror(errno));
        else
            printf("write %d: %zd bytes\n", i, n);
        fflush(stdout);
        sleep_ms(100);                 /* let the peer's reply (RST) arrive */
    }
    close(fd);
    close(lfd);
    return 0;
}
