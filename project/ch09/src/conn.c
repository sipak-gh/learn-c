/* conn.c - one client connection, as the server sees it. */
#include "conn.h"

#include <stdio.h>

#include "log.h"

const char *conn_target(const struct conn *c)
{
    return c->nick[0] != '\0' ? c->nick : "*";
}

void conn_send(struct conn *c, const struct msg *m)
{
    char line[MSG_LINE_MAX + 1];
    (void)c;   /* one client on stdout for now; later each conn has a socket */
    if (msg_format(line, sizeof line, m) < 0) {
        log_msg(LOG_WARN, "message too long to send: %s", m->command);
        return;
    }
    fputs(line, stdout);
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
