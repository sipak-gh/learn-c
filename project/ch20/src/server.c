/* server.c - the server's event loop: one thread, many sockets.
 *
 * Every socket is non-blocking, and the only place the server ever waits
 * is poll(). Each turn of the loop:
 *   1. build the pollfd array: the self-pipe, the listening socket, and
 *      each client (POLLIN while it is open, POLLOUT while it has output)
 *   2. poll(), with a timeout that ends when the next timer is due
 *   3. accept new clients, read from ready clients and handle their lines
 *   4. check the idle timers
 *   5. write queued output, then close the connections that are finished
 * Handlers never write to sockets: conn_send() only appends to the
 * client's output queue. With one thread, no locks are needed. */
#include "server.h"

#include <errno.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "clock.h"
#include "conn.h"
#include "io.h"
#include "log.h"
#include "room.h"
#include "sig.h"
#include "str.h"

/* How long to try to deliver "ERROR :Server shutting down". */
#define SHUTDOWN_GRACE_MS 2000

static struct room room;
static const struct server_config *config;

/* Accept every pending connection. */
static void accept_clients(int listen_fd, long long now)
{
    for (;;) {
        struct sockaddr_storage ss;
        socklen_t len = sizeof ss;
        int fd = accept(listen_fd, (struct sockaddr *)&ss, &len);
        if (fd < 0) {
            if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
                log_msg(LOG_WARN, "accept: %s", strerror(errno));
            return;
        }
        char addr[NET_ADDR_MAX];
        net_addr_str((struct sockaddr *)&ss, len, addr, sizeof addr);

        if (room.count >= config->max_clients) {
            static const char full[] = "ERROR :Server full\r\n";
            log_msg(LOG_WARN, "%s: server full", addr);
            ssize_t r = write(fd, full, sizeof full - 1);  /* best effort */
            (void)r;
            close(fd);
            continue;
        }
        struct conn *c = malloc(sizeof *c);
        if (c == NULL || set_nonblock(fd) < 0 || room_add(&room, c) < 0) {
            log_msg(LOG_ERROR, "%s: cannot add client: %s", addr, strerror(errno));
            free(c);
            close(fd);
            continue;
        }
        *c = (struct conn){
            .fd = fd, .state = CONN_OPEN, .connected = now, .last_active = now,
        };
        str_copy(c->addr, addr, sizeof c->addr);
        log_msg(LOG_DEBUG, "connection from %s", addr);
    }
}

/* The client's socket is readable: read once, handle complete lines. */
static void handle_input(struct conn *c, long long now)
{
    char line[MSG_LINE_MAX + 1];
    ssize_t n = read_into(c->fd, &c->in, 4096);
    if (n < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
            conn_drop(c, strerror(errno));
        return;
    }
    if (n == 0) {
        conn_drop(c, "Connection closed");
        return;
    }
    c->last_active = now;
    c->ping_sent = false;

    enum frame_status st;
    while (c->state == CONN_OPEN
           && (st = msg_take_line(&c->in, &c->skipping, line)) != FRAME_NONE) {
        if (st == FRAME_TOO_LONG)
            conn_numeric(c, "417", NULL, "Input line too long");
        else
            room_handle_line(&room, c, line);
    }
}

/* When c's timer is next due: its registration deadline, or when it is
 * due a PING, or when its PING is unanswered for too long. */
static long long deadline(const struct conn *c)
{
    long long idle_ms = config->idle_secs * 1000LL;
    if (c->nick[0] == '\0')
        return c->connected + idle_ms;
    return c->last_active + (c->ping_sent ? 2 * idle_ms : idle_ms);
}

static void check_timer(struct conn *c, long long now)
{
    if (c->state != CONN_OPEN || now < deadline(c))
        return;
    if (c->nick[0] == '\0') {
        conn_send(c, &(struct msg){ .command = "ERROR", .trailing = "Registration timeout" });
        conn_close(c, "Registration timeout");
    } else if (!c->ping_sent) {
        conn_send(c, &(struct msg){ .command = "PING", .trailing = SERVER_NAME });
        c->ping_sent = true;
    } else {
        conn_send(c, &(struct msg){ .command = "ERROR", .trailing = "Ping timeout" });
        conn_close(c, "Ping timeout");
    }
}

/* The poll() timeout in milliseconds: until the earliest timer, or -1
 * (wait forever) if there is none. */
static int poll_timeout(long long now, long long shutdown_at)
{
    long long first = shutdown_at;
    if (config->idle_secs > 0) {
        for (size_t i = 0; i < room.count; i++) {
            long long d = deadline(room.conns[i]);
            if (room.conns[i]->state == CONN_OPEN && (first < 0 || d < first))
                first = d;
        }
    }
    if (first < 0)
        return -1;
    return first <= now ? 0 : (int)(first - now);
}

/* Close and free the connections that are finished. */
static void reap(void)
{
    /* Backwards, so removing conns[i] does not skip the next one. */
    for (size_t i = room.count; i-- > 0; ) {
        struct conn *c = room.conns[i];
        if (c->state == CONN_OPEN || (c->state == CONN_CLOSING && c->out.len > 0))
            continue;
        room_part(&room, c, c->reason[0] != '\0' ? c->reason : "Connection closed");
        room_remove(&room, c);
        log_msg(LOG_DEBUG, "closing connection from %s", c->addr);
        close(c->fd);
        sb_free(&c->in);
        sb_free(&c->out);
        free(c);
    }
}

/* Tell every client goodbye; they close as soon as that is sent. */
static void start_shutdown(void)
{
    log_msg(LOG_INFO, "got signal %d (%s), shutting down",
            (int)sig_quit, strsignal(sig_quit));
    for (size_t i = 0; i < room.count; i++) {
        struct conn *c = room.conns[i];
        conn_send(c, &(struct msg){ .command = "ERROR", .trailing = "Server shutting down" });
        c->nick[0] = '\0';             /* everyone leaves: no QUIT messages */
        conn_close(c, "Server shutting down");
    }
}

int server_run(int listen_fd, const struct server_config *cfg)
{
    config = cfg;
    int wake_fd = sig_wake_fd();
    if (wake_fd < 0 || set_nonblock(listen_fd) < 0) {
        log_msg(LOG_ERROR, "setting up: %s", strerror(errno));
        return -1;
    }

    struct pollfd *fds = NULL;
    size_t fds_cap = 0;
    long long shutdown_at = -1;        /* -1: not shutting down */
    int status = 0;

    for (;;) {
        /* 1. What to wait for. fds[0]: self-pipe, fds[1]: listening
         *    socket (fd -1 makes poll() skip an entry), then clients. */
        size_t nfds = 2 + room.count;
        if (nfds > fds_cap) {
            struct pollfd *p = realloc(fds, nfds * 2 * sizeof *p);
            if (p == NULL) {
                log_msg(LOG_ERROR, "out of memory");
                status = -1;
                break;
            }
            fds = p;
            fds_cap = nfds * 2;
        }
        fds[0] = (struct pollfd){ .fd = wake_fd, .events = POLLIN };
        fds[1] = (struct pollfd){ .fd = shutdown_at < 0 ? listen_fd : -1, .events = POLLIN };
        for (size_t i = 0; i < room.count; i++) {
            struct conn *c = room.conns[i];
            short events = 0;
            if (c->state == CONN_OPEN)
                events |= POLLIN;
            if (c->out.len > 0)
                events |= POLLOUT;
            fds[2 + i] = (struct pollfd){ .fd = c->fd, .events = events };
        }

        /* 2. Wait. */
        long long now = clock_ms();
        int n = poll(fds, (nfds_t)nfds, poll_timeout(now, shutdown_at));
        if (n < 0 && errno != EINTR) {
            log_msg(LOG_ERROR, "poll: %s", strerror(errno));
            status = -1;
            break;
        }
        now = clock_ms();

        /* 3. Events. Only the first room.count clients from step 1 have a
         *    pollfd: accept_clients() appends new ones after them. */
        if (n > 0 && (fds[0].revents & POLLIN))
            sig_drain(wake_fd);
        if (sig_quit != 0 && shutdown_at < 0) {
            start_shutdown();
            shutdown_at = now + SHUTDOWN_GRACE_MS;
        }
        size_t polled = nfds - 2;
        for (size_t i = 0; n > 0 && i < polled; i++) {
            struct conn *c = room.conns[i];
            short rev = fds[2 + i].revents;
            /* POLLHUP/POLLERR: the peer is gone, read() will tell how. */
            if (c->state == CONN_OPEN && (rev & (POLLIN | POLLHUP | POLLERR)))
                handle_input(c, now);
            else if (rev & (POLLHUP | POLLERR))
                conn_drop(c, "Connection closed");
        }
        if (n > 0 && (fds[1].revents & POLLIN))
            accept_clients(listen_fd, now);

        /* 4. Timers. */
        if (config->idle_secs > 0)
            for (size_t i = 0; i < room.count; i++)
                check_timer(room.conns[i], now);

        /* 5. Send what we can, then close finished connections. Writing
         *    right away (not waiting for POLLOUT) is fine: usually the
         *    socket has room, and if not, conn_flush() stops at EAGAIN. */
        for (size_t i = 0; i < room.count; i++)
            conn_flush(room.conns[i]);
        reap();

        if (shutdown_at >= 0 && (room.count == 0 || now >= shutdown_at))
            break;
    }

    /* Anything left (after the shutdown grace time, or an error). */
    for (size_t i = 0; i < room.count; i++)
        conn_drop(room.conns[i], "Server shutting down");
    reap();
    room_free(&room);
    free(fds);
    close(wake_fd);
    return status;
}
