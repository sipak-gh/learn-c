/* room.c - the chat room: who is here, and what the server does with each
 * line a client sends. */
#include "room.h"

#include <stdio.h>
#include <stdlib.h>
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
    static void fn(struct room *r, struct conn *c, const struct msg *m);
COMMANDS(DECLARE)
#undef DECLARE

struct command {
    const char *name;
    void (*handler)(struct room *r, struct conn *c, const struct msg *m);
    bool needs_nick;        /* only allowed after registration */
};

#define ENTRY(name, fn, needs_nick) { name, fn, needs_nick },
static const struct command commands[] = { COMMANDS(ENTRY) };
#undef ENTRY

#define NCOMMANDS (sizeof commands / sizeof commands[0])

int room_add(struct room *r, struct conn *c)
{
    if (r->count == r->cap) {
        size_t cap = r->cap == 0 ? 8 : r->cap * 2;
        struct conn **conns = realloc(r->conns, cap * sizeof *conns);
        if (conns == NULL)
            return -1;
        r->conns = conns;
        r->cap = cap;
    }
    r->conns[r->count++] = c;
    return 0;
}

void room_remove(struct room *r, struct conn *c)
{
    for (size_t i = 0; i < r->count; i++) {
        if (r->conns[i] == c) {
            /* Close the gap; the order of the clients is kept. */
            memmove(&r->conns[i], &r->conns[i + 1],
                    (r->count - i - 1) * sizeof r->conns[0]);
            r->count--;
            return;
        }
    }
}

void room_free(struct room *r)
{
    free(r->conns);
    *r = (struct room){ 0 };
}

static bool registered(const struct conn *c)
{
    return c->nick[0] != '\0';
}

/* Send m to every registered client except skip (NULL: to everyone). */
static void broadcast(struct room *r, const struct conn *skip, const struct msg *m)
{
    for (size_t i = 0; i < r->count; i++) {
        struct conn *other = r->conns[i];
        if (other != skip && registered(other))
            conn_send(other, m);
    }
}

/* The registered client using nick (in any letter case), or NULL. */
static struct conn *find_nick(struct room *r, const char *nick)
{
    for (size_t i = 0; i < r->count; i++)
        if (registered(r->conns[i]) && nick_equal(r->conns[i]->nick, nick))
            return r->conns[i];
    return NULL;
}

void room_part(struct room *r, struct conn *c, const char *reason)
{
    if (!registered(c))
        return;
    log_msg(LOG_INFO, "%s left (%s)", c->nick, reason);
    broadcast(r, c, &(struct msg){ .prefix = c->nick, .command = "QUIT", .trailing = reason });
    c->nick[0] = '\0';
}

/* Send the list of nicks as 353 replies, then 366. One message can hold
 * at most MSG_LINE_MAX bytes, so a long list is split over several 353s. */
#define NAMES_MAX 400
static void send_names(struct room *r, struct conn *c)
{
    char names[NAMES_MAX + 1] = "";
    size_t len = 0;
    for (size_t i = 0; i < r->count; i++) {
        const char *nick = r->conns[i]->nick;
        size_t n = strlen(nick);
        if (n == 0)
            continue;                    /* not registered yet */
        if (len > 0 && len + 1 + n > NAMES_MAX) {
            conn_numeric(c, "353", NULL, names);   /* full: send this part */
            len = 0;
        }
        if (len > 0)
            names[len++] = ' ';
        memcpy(names + len, nick, n + 1);          /* + 1: copy the '\0' */
        len += n;
    }
    if (len > 0)
        conn_numeric(c, "353", NULL, names);
    conn_numeric(c, "366", NULL, "End of NAMES");
}

static void cmd_nick(struct room *r, struct conn *c, const struct msg *m)
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
    struct conn *owner = find_nick(r, nick);
    if (owner != NULL && owner != c) {
        conn_numeric(c, "433", nick, "Nickname is already in use");
        return;
    }
    if (!registered(c)) {                           /* registration */
        char welcome[64];
        snprintf(welcome, sizeof welcome, "Welcome to parley, %s", nick);
        str_copy(c->nick, nick, sizeof c->nick);
        conn_numeric(c, "001", NULL, welcome);
        broadcast(r, NULL, &(struct msg){ .prefix = c->nick, .command = "JOIN" });
        send_names(r, c);
        log_msg(LOG_INFO, "%s joined", c->nick);
    } else if (strcmp(c->nick, nick) != 0) {        /* rename */
        broadcast(r, NULL, &(struct msg){
            .prefix = c->nick, .command = "NICK", .nparams = 1, .params = { nick } });
        log_msg(LOG_INFO, "%s is now %s", c->nick, nick);
        str_copy(c->nick, nick, sizeof c->nick);
    }
}

/* MSG and ME: send the text to everyone, the sender included, so every
 * client sees the same conversation in the same order. */
static void say(struct room *r, struct conn *c, const struct msg *m, const char *command)
{
    const char *text = msg_param(m, 0);
    if (text == NULL || text[0] == '\0') {
        conn_numeric(c, "412", NULL, "No text to send");
        return;
    }
    broadcast(r, NULL, &(struct msg){ .prefix = c->nick, .command = command, .trailing = text });
}

static void cmd_msg(struct room *r, struct conn *c, const struct msg *m)
{
    say(r, c, m, "MSG");
}

static void cmd_me(struct room *r, struct conn *c, const struct msg *m)
{
    say(r, c, m, "ME");
}

static void cmd_names(struct room *r, struct conn *c, const struct msg *m)
{
    (void)m;
    send_names(r, c);
}

static void cmd_ping(struct room *r, struct conn *c, const struct msg *m)
{
    (void)r;
    const char *token = msg_param(m, 0);
    if (token == NULL) {
        conn_numeric(c, "461", "PING", "Not enough parameters");
        return;
    }
    conn_send(c, &(struct msg){ .prefix = SERVER_NAME, .command = "PONG", .trailing = token });
}

static void cmd_pong(struct room *r, struct conn *c, const struct msg *m)
{
    (void)r;   /* a reply to our PING: nothing to do (yet) */
    (void)c;
    (void)m;
}

static void cmd_quit(struct room *r, struct conn *c, const struct msg *m)
{
    const char *reason = msg_param(m, 0);
    room_part(r, c, reason != NULL ? reason : "Quit");
    conn_send(c, &(struct msg){ .command = "ERROR", .trailing = "Goodbye" });
    c->state = CONN_CLOSING;
}

void room_handle_line(struct room *r, struct conn *c, char *line)
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
        if (cmd->needs_nick && !registered(c))
            conn_numeric(c, "451", NULL, "You have not registered");
        else
            cmd->handler(r, c, &m);
        return;
    }
    conn_numeric(c, "421", m.command, "Unknown command");
}
