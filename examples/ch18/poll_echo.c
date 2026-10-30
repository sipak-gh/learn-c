/* poll_echo.c - an echo server with one thread and poll().
 * usage: poll_echo [PORT]
 * Every socket is non-blocking; the only place it waits is poll(). Each
 * client has an output buffer: what we read is appended there, and sent
 * when the socket is writable. */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define MAX_CLIENTS 1000
#define OUT_MAX (64 * 1024)     /* per client: stop reading when it is full */

struct client {
    int fd;
    bool eof;                   /* the client sent EOF: close once out is sent */
    size_t len;                 /* bytes waiting in out */
    char *out;                  /* OUT_MAX bytes */
};

static struct client clients[MAX_CLIENTS];
static size_t nclients;

static int set_nonblock(int fd)
{
    int flags = fcntl(fd, F_GETFL);
    if (flags < 0)
        return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static int listen_on(int port)
{
    struct sockaddr_in addr = { .sin_family = AF_INET };
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons((unsigned short)port);
    int on = 1;
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0 || setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof on) < 0
            || bind(fd, (struct sockaddr *)&addr, sizeof addr) < 0
            || listen(fd, 128) < 0 || set_nonblock(fd) < 0)
        return -1;
    return fd;
}

/* The listening socket is readable: accept everyone who is waiting. */
static void accept_all(int lfd)
{
    for (;;) {
        int fd = accept(lfd, NULL, NULL);
        if (fd < 0) {
            if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
                perror("accept");
            return;                     /* EAGAIN: nobody else is waiting */
        }
        char *out = malloc(OUT_MAX);
        if (nclients == MAX_CLIENTS || out == NULL || set_nonblock(fd) < 0) {
            free(out);
            close(fd);
            continue;
        }
        clients[nclients++] = (struct client){ .fd = fd, .out = out };
        printf("client %d connected (%zu in total)\n", fd, nclients);
    }
}

/* Close c as soon as possible: forget what it was still to be sent. */
static void drop(struct client *c)
{
    c->eof = true;
    c->len = 0;
}

/* c's socket is readable: read once, straight into its output buffer. */
static void on_readable(struct client *c)
{
    ssize_t n = read(c->fd, c->out + c->len, OUT_MAX - c->len);
    if (n > 0)
        c->len += (size_t)n;
    else if (n == 0)
        c->eof = true;                  /* close after sending the rest */
    else if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
        drop(c);
}

/* c's socket is writable: send what the kernel takes, keep the rest. */
static void on_writable(struct client *c)
{
    ssize_t n = write(c->fd, c->out, c->len);
    if (n > 0) {
        memmove(c->out, c->out + n, c->len - (size_t)n);
        c->len -= (size_t)n;
    } else if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
        drop(c);
    }
}

int main(int argc, char *argv[])
{
    int lfd = listen_on(argc > 1 ? atoi(argv[1]) : 6667);
    if (lfd < 0) {
        perror("listen");
        return 1;
    }
    signal(SIGPIPE, SIG_IGN);
    setvbuf(stdout, NULL, _IOLBF, 0);
    static struct pollfd fds[1 + MAX_CLIENTS];

    for (;;) {
        /* 1. What to wait for. */
        fds[0] = (struct pollfd){ .fd = lfd, .events = POLLIN };
        for (size_t i = 0; i < nclients; i++) {
            struct client *c = &clients[i];
            short events = 0;
            if (!c->eof && c->len < OUT_MAX)
                events |= POLLIN;       /* room for more: read */
            if (c->len > 0)
                events |= POLLOUT;      /* something to send: write */
            fds[1 + i] = (struct pollfd){ .fd = c->fd, .events = events };
        }
        size_t polled = nclients;

        /* 2. Wait until at least one of them is ready. */
        if (poll(fds, (nfds_t)(1 + polled), -1) < 0) {
            if (errno == EINTR)
                continue;
            perror("poll");
            return 1;
        }

        /* 3. Handle what is ready. */
        for (size_t i = 0; i < polled; i++) {
            short rev = fds[1 + i].revents;
            if (rev & (POLLERR | POLLHUP)) {
                drop(&clients[i]);      /* the connection is gone */
                continue;
            }
            if (rev & POLLIN)
                on_readable(&clients[i]);
            if (rev & POLLOUT)
                on_writable(&clients[i]);
        }
        if (fds[0].revents & POLLIN)
            accept_all(lfd);

        /* 4. Close the clients that are done, keeping the array packed. */
        for (size_t i = 0; i < nclients; ) {
            struct client *c = &clients[i];
            if (c->eof && c->len == 0) {
                close(c->fd);
                free(c->out);
                printf("client %d left (%zu in total)\n", c->fd, nclients - 1);
                *c = clients[--nclients];   /* move the last one here */
            } else {
                i++;
            }
        }
    }
}
