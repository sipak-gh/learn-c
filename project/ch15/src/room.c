/* room.c - the chat room: what the server does with each line. */
#include "room.h"

#include <stdio.h>
#include <string.h>

#include "log.h"
#include "str.h"

/* The command table as an X-macro: one line per command, expanded twice
 * below (once for prototypes, once for table entries), so the two can
 * never disagree.  X(NAME, handler, needs_nick) */
#define COMMANDS(X)                  \
    X("NICK",  cmd_nick,  false)     \
    X("MSG",   cmd_msg,   true)      \
    X("ME",    cmd_me,    true)      \
    X("NAMES", cmd_names, true)      \
    X("PING",  cmd_ping,  false)     \
    X("PONG",  cmd_pong,  false)     \
    X("QUIT",  cmd_quit,  false)

#define DECLARE(name, fn, needs_nick) \
    static void fn(struct conn *c, const struct msg *m);
COMMANDS(DECLARE)
#undef DECLARE

struct command {
    const char *name;
    void (*handler)(struct conn *c, const struct msg *m);
    bool needs_nick;        /* only allowed after registration */
};

#define ENTRY(name, fn, needs_nick) { name, fn, needs_nick },
static const struct command commands[] = { COMMANDS(ENTRY) };
#undef ENTRY

#define NCOMMANDS (sizeof commands / sizeof commands[0])

static void cmd_nick(struct conn *c, const struct msg *m)
{
    const char *nick = msg_param(m, 0);
    if (nick == NULL || nick[0] == '\0') {
        conn_numeric(c, "431", NULL, "No nickname given");
        return;
    }
    if (!nick_valid(nick)) {
        conn_numeric(c, "432", nick, "Erroneous nickname");
        return;
    }
    if (c->nick[0] == '\0') {                       /* registration */
        char welcome[64];
        snprintf(welcome, sizeof welcome, "Welcome to parley, %s", nick);
        str_copy(c->nick, nick, sizeof c->nick);
        conn_numeric(c, "001", NULL, welcome);
        conn_send(c, &(struct msg){ .prefix = c->nick, .command = "JOIN" });
        conn_numeric(c, "353", NULL, c->nick);      /* who is here: just you */
        conn_numeric(c, "366", NULL, "End of NAMES");
        log_msg(LOG_INFO, "%s joined", c->nick);
    } else if (strcmp(c->nick, nick) != 0) {        /* rename */
        conn_send(c, &(struct msg){
            .prefix = c->nick, .command = "NICK", .nparams = 1, .params = { nick } });
        log_msg(LOG_INFO, "%s is now %s", c->nick, nick);
        str_copy(c->nick, nick, sizeof c->nick);
    }
}

/* MSG and ME: send the text back with the sender as prefix. */
static void say(struct conn *c, const struct msg *m, const char *command)
{
    const char *text = msg_param(m, 0);
    if (text == NULL || text[0] == '\0') {
        conn_numeric(c, "412", NULL, "No text to send");
        return;
    }
    conn_send(c, &(struct msg){ .prefix = c->nick, .command = command, .trailing = text });
}

static void cmd_msg(struct conn *c, const struct msg *m)
{
    say(c, m, "MSG");
}

static void cmd_me(struct conn *c, const struct msg *m)
{
    say(c, m, "ME");
}

static void cmd_names(struct conn *c, const struct msg *m)
{
    (void)m;
    conn_numeric(c, "353", NULL, c->nick);
    conn_numeric(c, "366", NULL, "End of NAMES");
}

static void cmd_ping(struct conn *c, const struct msg *m)
{
    const char *token = msg_param(m, 0);
    if (token == NULL) {
        conn_numeric(c, "461", "PING", "Not enough parameters");
        return;
    }
    conn_send(c, &(struct msg){ .prefix = SERVER_NAME, .command = "PONG", .trailing = token });
}

static void cmd_pong(struct conn *c, const struct msg *m)
{
    (void)c;   /* a reply to our PING: nothing to do (yet) */
    (void)m;
}

static void cmd_quit(struct conn *c, const struct msg *m)
{
    const char *reason = msg_param(m, 0);
    log_msg(LOG_INFO, "%s left (%s)", conn_target(c), reason != NULL ? reason : "Quit");
    conn_send(c, &(struct msg){ .command = "ERROR", .trailing = "Goodbye" });
    c->state = CONN_CLOSING;
}

void room_handle_line(struct conn *c, char *line)
{
    struct msg m;
    enum msg_status status = msg_parse(line, &m);
    if (status == MSG_EMPTY)
        return;
    if (status != MSG_OK) {
        conn_numeric(c, "400", NULL, "Malformed message");
        return;
    }

    for (size_t i = 0; i < NCOMMANDS; i++) {
        const struct command *cmd = &commands[i];
        if (!str_ieq(m.command, cmd->name))
            continue;
        if (cmd->needs_nick && c->nick[0] == '\0')
            conn_numeric(c, "451", NULL, "You have not registered");
        else
            cmd->handler(c, &m);
        return;
    }
    conn_numeric(c, "421", m.command, "Unknown command");
}
