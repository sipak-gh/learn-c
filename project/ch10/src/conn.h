/* conn.h - one client connection, as the server sees it. */
#ifndef PARLEY_CONN_H
#define PARLEY_CONN_H

#include <stdbool.h>

#include "msg.h"
#include "nick.h"
#include "strbuf.h"

#define SERVER_NAME "parley"   /* the prefix of messages from the server */

struct conn {
    char nick[NICK_MAX + 1];   /* "" until the client registers */
    bool quitting;             /* sent QUIT: stop reading */
    struct strbuf in;          /* bytes received but not yet handled */
    bool skipping;             /* throwing away a too-long line */
};

/* The name to use in replies: the nick, or "*" before registration. */
const char *conn_target(const struct conn *c);

/* Send one message to the client. */
void conn_send(struct conn *c, const struct msg *m);

/* Send ":parley CODE <target> [param] :text", the server's reply format. */
void conn_numeric(struct conn *c, const char *code, const char *param,
                  const char *text);

#endif
