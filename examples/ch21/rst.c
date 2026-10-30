/* rst.c - close() with unread input sends a reset; shutdown() does not.
 *     rst close | rst linger */
#define _POSIX_C_SOURCE 200809L
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define PORT 6910

static void pause_ms(long ms)
{
    struct timespec ts = { 0, ms * 1000000L };
    nanosleep(&ts, NULL);
}

static void say(int fd, const char *text)
{
    if (write(fd, text, strlen(text)) < 0)
        printf("client: write: %s\n", strerror(errno));
}

int main(int argc, char *argv[])
{
    int linger = argc > 1 && strcmp(argv[1], "linger") == 0;
    signal(SIGPIPE, SIG_IGN);

    struct sockaddr_in a = { .sin_family = AF_INET, .sin_port = htons(PORT) };
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    int lis = socket(AF_INET, SOCK_STREAM, 0), on = 1;
    setsockopt(lis, SOL_SOCKET, SO_REUSEADDR, &on, sizeof on);
    if (bind(lis, (struct sockaddr *)&a, sizeof a) < 0 || listen(lis, 1) < 0) {
        perror("bind/listen");
        return 1;
    }
    int client = socket(AF_INET, SOCK_STREAM, 0);
    if (connect(client, (struct sockaddr *)&a, sizeof a) < 0) {
        perror("connect");
        return 1;
    }
    int server = accept(lis, NULL, NULL);

    say(client, "MSG :spam\r\nMSG :spam\r\n");     /* the server never reads it */
    say(server, "ERROR :Excess flood\r\n");
    if (linger)
        shutdown(server, SHUT_WR);     /* send FIN; keep reading */
    else
        close(server);                 /* unread input: send RST */
    pause_ms(100);

    say(client, "MSG :more spam\r\n");             /* the client goes on */
    char buf[256];
    for (;;) {
        ssize_t n = read(client, buf, sizeof buf - 1);
        if (n < 0) {
            printf("client: read: %s\n", strerror(errno));
            break;
        }
        if (n == 0) {
            printf("client: end of connection\n");
            break;
        }
        buf[n] = '\0';
        printf("client: got %s", buf);
    }
    close(client);
    if (linger) {                      /* the server drains, then closes */
        ssize_t n;
        while ((n = read(server, buf, sizeof buf)) > 0)
            printf("server: threw away %zd bytes\n", n);
        close(server);
    }
    close(lis);
    return 0;
}
