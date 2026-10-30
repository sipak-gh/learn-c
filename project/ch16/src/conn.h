/* conn.h - one client connection, as the server sees it. */
#ifndef PARLEY_CONN_H
#define PARLEY_CONN_H

#include <stdbool.h>

#include "msg.h"
#include "net.h"
#include "nick.h"
#include "strbuf.h"

#define SERVER_NAME "parley"   /* the prefix of messages from the server */

enum conn_state {
    CONN_OPEN,       /* normal operation */
    CONN_CLOSING,    /* we sent ERROR: stop reading, close the link */
    CONN_DEAD        /* reading or writing failed: just close it */
};

struct conn {
    int fd;                    /* the client's socket */
    char addr[NET_ADDR_MAX];   /* the client's address, for log messages */
    char nick[NICK_MAX + 1];   /* "" until the client registers */
    enum conn_state state;
    struct strbuf in;          /* bytes received but not yet handled */
    bool skipping;             /* throwing away a too-long line */
};

/* The name to use in replies: the nick, or "*" before registration. */
const char *conn_target(const struct conn *c);

/* Send one message to the client. If writing fails, the error is logged
 * and the state becomes CONN_DEAD. */
void conn_send(struct conn *c, const struct msg *m);

/* Send ":parley CODE <target> [param] :text", the server's reply format. */
void conn_numeric(struct conn *c, const char *code, const char *param,
                  const char *text);

#endif
