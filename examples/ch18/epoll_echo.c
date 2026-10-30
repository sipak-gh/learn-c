/* epoll_echo.c - poll_echo.c ported to Linux's epoll (level-triggered).
 * usage: epoll_echo [PORT]
 * Linux only: epoll is not POSIX. The kernel keeps the list of sockets we
 * are interested in, so each wait no longer passes (and scans) all of them. */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#define OUT_MAX (64 * 1024)

struct client {
    int fd;
    bool eof;
    uint32_t events;            /* what epoll watches for now */
    size_t len;
    char out[OUT_MAX];
};

static int ep;                  /* the epoll instance */
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

/* Tell epoll what c waits for now, if that changed. */
static void update(struct client *c)
{
    uint32_t want = 0;
    if (!c->eof && c->len < OUT_MAX)
        want |= EPOLLIN;
    if (c->len > 0)
        want |= EPOLLOUT;
    if (want != c->events) {
        struct epoll_event ev = { .events = want, .data.ptr = c };
        epoll_ctl(ep, EPOLL_CTL_MOD, c->fd, &ev);
        c->events = want;
    }
}

static void accept_all(int lfd)
{
    for (;;) {
        int fd = accept(lfd, NULL, NULL);
        if (fd < 0)
            return;
        struct client *c = malloc(sizeof *c);
        struct epoll_event ev = { .events = EPOLLIN, .data.ptr = c };
        if (c == NULL || set_nonblock(fd) < 0
                || epoll_ctl(ep, EPOLL_CTL_ADD, fd, &ev) < 0) {
            free(c);
            close(fd);
            continue;
        }
        c->fd = fd;
        c->eof = false;
        c->events = EPOLLIN;
        c->len = 0;
        printf("client %d connected (%zu in total)\n", fd, ++nclients);
    }
}

static void drop(struct client *c)
{
    c->eof = true;
    c->len = 0;
}

static void handle(struct client *c, uint32_t rev)
{
    if (rev & (EPOLLERR | EPOLLHUP)) {
        drop(c);
    } else {
        if (rev & EPOLLIN) {
            ssize_t n = read(c->fd, c->out + c->len, OUT_MAX - c->len);
            if (n > 0)
                c->len += (size_t)n;
            else if (n == 0)
                c->eof = true;
            else if (errno != EAGAIN && errno != EINTR)
                drop(c);
        }
        if (rev & EPOLLOUT) {
            ssize_t n = write(c->fd, c->out, c->len);
            if (n > 0) {
                memmove(c->out, c->out + n, c->len - (size_t)n);
                c->len -= (size_t)n;
            } else if (n < 0 && errno != EAGAIN && errno != EINTR) {
                drop(c);
            }
        }
    }
    if (c->eof && c->len == 0) {
        epoll_ctl(ep, EPOLL_CTL_DEL, c->fd, NULL);
        close(c->fd);
        printf("client %d left (%zu in total)\n", c->fd, --nclients);
        free(c);
    } else {
        update(c);
    }
}

int main(int argc, char *argv[])
{
    int lfd = listen_on(argc > 1 ? atoi(argv[1]) : 6667);
    ep = epoll_create1(0);
    if (lfd < 0 || ep < 0) {
        perror("setup");
        return 1;
    }
    signal(SIGPIPE, SIG_IGN);
    setvbuf(stdout, NULL, _IOLBF, 0);
    /* data.ptr NULL marks the listening socket. */
    struct epoll_event ev = { .events = EPOLLIN, .data.ptr = NULL };
    epoll_ctl(ep, EPOLL_CTL_ADD, lfd, &ev);

    for (;;) {
        struct epoll_event ready[64];
        int n = epoll_wait(ep, ready, 64, -1);   /* only the ready ones */
        if (n < 0 && errno != EINTR) {
            perror("epoll_wait");
            return 1;
        }
        for (int i = 0; i < n; i++) {
            if (ready[i].data.ptr == NULL)
                accept_all(lfd);
            else
                handle(ready[i].data.ptr, ready[i].events);
        }
    }
}
