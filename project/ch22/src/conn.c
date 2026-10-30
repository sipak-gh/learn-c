/* conn.c - one client connection, as the server sees it. */
#include "conn.h"

#include <errno.h>
#include <string.h>
#include <unistd.h>

#include "clock.h"
#include "log.h"
#include "str.h"

const char *conn_target(const struct conn *c)
{
    return c->nick[0] != '\0' ? c->nick : "*";
}

void conn_send(struct conn *c, const struct msg *m)
{
    char line[MSG_LINE_MAX + 1];
    if (c->state == CONN_DEAD)
        return;
    int n = msg_format(line, sizeof line, m);
    if (n < 0) {
        log_msg(LOG_WARN, "message too long to send: %s", m->command);
        return;
    }
    if (c->out.len + (size_t)n > OUTQ_MAX) {
        log_msg(LOG_WARN, "%s: output queue full, disconnecting", conn_target(c));
        conn_drop(c, "Output queue full");
        return;
    }
    if (sb_append(&c->out, line, (size_t)n) < 0) {
        log_msg(LOG_ERROR, "%s: out of memory", conn_target(c));
        conn_drop(c, "Out of memory");
    }
}

void conn_numeric(struct conn *c, const char *code, const char *param,
                  const char *text)
{
    struct msg m = {
        .prefix = SERVER_NAME,
        .command = code,
        .nparams = param != NULL ? 2 : 1,
        .params = { conn_target(c), param },
        .trailing = text,
    };
    conn_send(c, &m);
}

void conn_flush(struct conn *c)
{
    while (c->state != CONN_DEAD && c->out.len > 0) {
        ssize_t n = write(c->fd, c->out.data, c->out.len);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                return;                /* socket buffer full: wait for POLLOUT */
            log_msg(LOG_DEBUG, "writing to %s: %s", c->addr, strerror(errno));
            conn_drop(c, strerror(errno));
            return;
        }
        sb_consume(&c->out, (size_t)n);  /* a short write keeps the rest */
    }
}

static void set_closed(struct conn *c, enum conn_state state, const char *reason)
{
    if (c->state == CONN_OPEN) {       /* keep the first reason */
        str_copy(c->reason, reason, sizeof c->reason);
        c->closing_since = clock_ms();
    }
    /* OPEN -> CLOSING -> LINGER -> DEAD, never back */
    if (state > c->state)
        c->state = state;
}

void conn_close(struct conn *c, const char *reason)
{
    set_closed(c, CONN_CLOSING, reason);
}

void conn_drop(struct conn *c, const char *reason)
{
    set_closed(c, CONN_DEAD, reason);
}
