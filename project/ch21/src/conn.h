/* conn.h - one client connection, as the server sees it. */
#ifndef PARLEY_CONN_H
#define PARLEY_CONN_H

#include <stdbool.h>

#include "msg.h"
#include "net.h"
#include "nick.h"
#include "strbuf.h"

#define SERVER_NAME "parley"   /* the prefix of messages from the server */

/* The most output we keep for a client that does not read it. A client
 * that falls further behind is disconnected ("slow reader eviction"):
 * otherwise one stuck client could make the server use unlimited memory. */
#define OUTQ_MAX (64 * 1024)

enum conn_state {
    CONN_OPEN,       /* normal operation */
    CONN_CLOSING,    /* stop reading; close once the output queue is sent */
    CONN_LINGER,     /* all sent: wait (briefly) for the client to close */
    CONN_DEAD        /* close it now, throwing away unsent output */
};

struct conn {
    int fd;                    /* the client's socket (non-blocking) */
    char addr[NET_ADDR_MAX];   /* the client's address, for log messages */
    char nick[NICK_MAX + 1];   /* "" until the client registers */
    enum conn_state state;
    char reason[64];           /* why we close it: for the QUIT the others see */
    struct strbuf in;          /* bytes received but not yet handled */
    struct strbuf out;         /* bytes waiting to be sent: the output queue */
    bool skipping;             /* throwing away a too-long line */
    long long connected;       /* clock_ms() when it connected */
    long long last_active;     /* clock_ms() when it last sent something */
    bool ping_sent;            /* we sent a PING since then */
    long long flood_clock;     /* flood control: see flood_check() in server.c */
    long long closing_since;   /* clock_ms() when it stopped being CONN_OPEN */
};

/* How long a connection may take to close: to receive its last messages
 * (CONN_CLOSING) and to close its end (CONN_LINGER). Then it is dropped. */
#define CLOSE_TIMEOUT_MS 2000

/* The name to use in replies: the nick, or "*" before registration. */
const char *conn_target(const struct conn *c);

/* Queue one message for the client; it is sent when the socket is ready
 * (see conn_flush). If the queue would grow beyond OUTQ_MAX, the client is
 * dropped instead. */
void conn_send(struct conn *c, const struct msg *m);

/* Send ":parley CODE <target> [param] :text", the server's reply format. */
void conn_numeric(struct conn *c, const char *code, const char *param,
                  const char *text);

/* Write as much of the output queue as the socket takes now, without
 * blocking. On a write error the client is dropped. */
void conn_flush(struct conn *c);

/* Close c once its output queue is sent (state CONN_CLOSING, then
 * CONN_LINGER: see linger() in server.c). */
void conn_close(struct conn *c, const char *reason);

/* Close c right away (state CONN_DEAD). */
void conn_drop(struct conn *c, const char *reason);

#endif
