/* parleyd.c - the parley chat server.
 * No network yet (Chapters 6-14). The server talks to one client on
 * stdin/stdout: read protocol lines from stdin, write replies to stdout. */
#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    DEFAULT_PORT = 6667,
    PORT_MAX = 65535,
    DEFAULT_MAX_CLIENTS = 64,
    MAX_CLIENTS_MAX = 1000,
    MSG_LINE_MAX = 512,   /* longest protocol line, including "\r\n" */
    MSG_PARAMS_MAX = 8,   /* most middle parameters in one message */
    NICK_MAX = 16         /* longest nickname */
};

#define SERVER_NAME "parley"   /* the prefix of server messages (#define: Chapter 9) */

enum log_level { LOG_DEBUG, LOG_INFO, LOG_WARN, LOG_ERROR };

/* Settings from the command line, with their defaults. */
struct config {
    const char *addr;
    long port;
    long max_clients;
    bool verbose;
};

/* Set from config.verbose in main; log_msg prints debug lines only if true. */
static bool log_verbose = false;

static const char *level_name(enum log_level level)
{
    switch (level) {
    case LOG_DEBUG: return "debug";
    case LOG_INFO:  return "info";
    case LOG_WARN:  return "warning";
    case LOG_ERROR: return "error";
    }
    return "?";
}

/* log_msg(level, format, ...) works like printf, but writes one line to
 * stderr with a "parleyd: level: " prefix. Debug lines need -v. */
static void log_msg(enum log_level level, const char *fmt, ...)
{
    if (level == LOG_DEBUG && !log_verbose)
        return;

    fprintf(stderr, "parleyd: %s: ", level_name(level));
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);   /* the v-version takes a va_list */
    va_end(ap);
    fputc('\n', stderr);
}

/* Report a command-line mistake and exit with status 2. Never returns. */
static void usage_error(const char *fmt, ...)
{
    fprintf(stderr, "parleyd: ");
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\nTry 'parleyd --help' for more information.\n");
    exit(2);
}

static void print_help(void)
{
    printf("usage: parleyd [-p PORT] [-b ADDR] [-m MAXCLIENTS] [-v]\n"
           "  -p PORT        TCP port to listen on (default %d)\n"
           "  -b ADDR        address to listen on (default 127.0.0.1)\n"
           "  -m MAXCLIENTS  most clients at once, 1-%d (default %d)\n"
           "  -v             verbose (debug) logging\n"
           "  -h, --help     show this help and exit\n"
           "  --version      show the version and exit\n",
           DEFAULT_PORT, MAX_CLIENTS_MAX, DEFAULT_MAX_CLIENTS);
}

/* ---- string helpers ---------------------------------------------------- */

/* Copy src into dst, which has room for size bytes. Always NUL-terminates
 * (if size > 0) and never writes past dst[size - 1], like BSD strlcpy.
 * Returns strlen(src): if that is >= size, the copy was truncated. */
static size_t str_copy(char *dst, const char *src, size_t size)
{
    size_t len = strlen(src);
    if (size > 0) {
        size_t n = len < size - 1 ? len : size - 1;
        memcpy(dst, src, n);
        dst[n] = '\0';
    }
    return len;
}

/* Remove any trailing '\r' and '\n' characters. */
static void str_chomp(char *s)
{
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r'))
        s[--n] = '\0';
}

/* ASCII case-insensitive equality: str_ieq("Bob", "bOB") is true. */
static bool str_ieq(const char *a, const char *b)
{
    /* <ctype.h> functions need an unsigned char value (or EOF): a plain
     * char may be negative, which is undefined behaviour for tolower. */
    while (*a != '\0' && tolower((unsigned char)*a) == tolower((unsigned char)*b)) {
        a++;
        b++;
    }
    return tolower((unsigned char)*a) == tolower((unsigned char)*b);
}

/* Parse a whole decimal number in [min, max] into *out.
 * Returns false (and leaves *out alone) if s is not such a number. */
static bool str_to_long(const char *s, long min, long max, long *out)
{
    char *end;
    errno = 0;
    long n = strtol(s, &end, 10);
    if (errno != 0 || end == s || *end != '\0' || n < min || n > max)
        return false;
    *out = n;
    return true;
}

/* ---- nicknames ----------------------------------------------------------- */

/* A nickname is 1-16 characters: a letter, then letters, digits, '_' or '-'. */
static bool nick_valid(const char *nick)
{
    size_t len = strlen(nick);
    if (len == 0 || len > NICK_MAX || !isalpha((unsigned char)nick[0]))
        return false;
    for (size_t i = 1; i < len; i++) {
        unsigned char c = (unsigned char)nick[i];
        if (!isalnum(c) && c != '_' && c != '-')
            return false;
    }
    return true;
}

static void parse_args(int argc, char *argv[], struct config *cfg)
{
    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];

        if (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0) {
            print_help();
            exit(0);
        }
        if (strcmp(arg, "--version") == 0) {
            printf("parleyd 0.1\n");
            exit(0);
        }
        if (arg[0] != '-' || arg[1] == '\0' || arg[2] != '\0')
            usage_error("unexpected argument '%s'", arg);

        char opt = arg[1];
        const char *value = NULL;
        if (opt == 'p' || opt == 'b' || opt == 'm') {
            if (i + 1 >= argc)
                usage_error("option -%c needs a value", opt);
            value = argv[++i];
        }

        switch (opt) {
        case 'b':
            cfg->addr = value;
            break;
        case 'p':
            if (!str_to_long(value, 1, PORT_MAX, &cfg->port))
                usage_error("invalid port '%s' (expected 1-%d)", value, PORT_MAX);
            break;
        case 'm':
            if (!str_to_long(value, 1, MAX_CLIENTS_MAX, &cfg->max_clients))
                usage_error("invalid client limit '%s' (expected 1-%d)",
                            value, MAX_CLIENTS_MAX);
            break;
        case 'v':
            cfg->verbose = true;
            break;
        default:
            usage_error("unknown option '%s'", arg);
        }
    }
}

/* ---- protocol messages --------------------------------------------------- */

/* One protocol line, split into its parts. All pointers point into the
 * line that msg_parse() was given, so the line must outlive the msg. */
struct msg {
    const char *prefix;                   /* who sent it; NULL if absent */
    const char *command;                  /* "NICK", "001", ... */
    int nparams;                          /* how many middle parameters */
    const char *params[MSG_PARAMS_MAX];   /* the middle parameters */
    const char *trailing;                 /* text after " :"; NULL if absent */
};

enum msg_status {
    MSG_OK,
    MSG_EMPTY,             /* blank line: not an error, just ignore it */
    MSG_NO_COMMAND,        /* a prefix with nothing after it */
    MSG_TOO_MANY_PARAMS
};

/* End the word at p (overwrite the first space with '\0') and return a
 * pointer to the start of the next word, skipping extra spaces. */
static char *cut_word(char *p)
{
    while (*p != '\0' && *p != ' ')
        p++;
    while (*p == ' ')
        *p++ = '\0';
    return p;
}

/* Parse line (no "\r\n") into *m:
 *     [":" prefix " "] command {" " param} [" :" trailing]
 * The line is modified in place: spaces become '\0' terminators. */
static enum msg_status msg_parse(char *line, struct msg *m)
{
    *m = (struct msg){ .prefix = NULL };   /* compound literal: zero it all */

    char *p = line;
    while (*p == ' ')
        p++;
    if (*p == '\0')
        return MSG_EMPTY;

    if (*p == ':') {
        m->prefix = p + 1;
        p = cut_word(p);
        if (m->prefix[0] == '\0' || *p == '\0')
            return MSG_NO_COMMAND;
    }

    m->command = p;
    p = cut_word(p);

    while (*p != '\0') {
        if (*p == ':') {             /* the trailing parameter: rest of line */
            m->trailing = p + 1;
            break;
        }
        if (m->nparams == MSG_PARAMS_MAX)
            return MSG_TOO_MANY_PARAMS;
        m->params[m->nparams++] = p;
        p = cut_word(p);
    }
    return MSG_OK;
}

/* Parameter i, counting the trailing one as the last: for "NICK bob" and
 * "NICK :bob" msg_param(m, 0) is "bob". NULL if there is no such param. */
static const char *msg_param(const struct msg *m, int i)
{
    if (i < m->nparams)
        return m->params[i];
    if (i == m->nparams)
        return m->trailing;
    return NULL;
}

/* Append text to out (which holds *len bytes, room for size), stopping at
 * size - 1. Returns false if text did not fit completely. */
static bool append(char *out, size_t size, size_t *len, const char *text)
{
    size_t n = strlen(text);
    size_t room = size - 1 - *len;
    bool fits = n <= room;
    if (!fits)
        n = room;
    memcpy(out + *len, text, n);
    *len += n;
    out[*len] = '\0';
    return fits;
}

/* Format *m as a protocol line ending in "\r\n" into out[size], where
 * size >= MSG_LINE_MAX + 1. A trailing parameter that is too long is cut
 * short so the line fits in MSG_LINE_MAX bytes. Returns the line length,
 * or -1 if even the parts before the trailing text do not fit. */
static int msg_format(char *out, size_t size, const struct msg *m)
{
    if (size > MSG_LINE_MAX - 2 + 1)
        size = MSG_LINE_MAX - 2 + 1;     /* leave room for "\r\n" */
    size_t len = 0;
    bool ok = true;

    out[0] = '\0';
    if (m->prefix != NULL) {
        ok = ok && append(out, size, &len, ":");
        ok = ok && append(out, size, &len, m->prefix);
        ok = ok && append(out, size, &len, " ");
    }
    ok = ok && append(out, size, &len, m->command);
    for (int i = 0; i < m->nparams; i++) {
        ok = ok && append(out, size, &len, " ");
        ok = ok && append(out, size, &len, m->params[i]);
    }
    if (m->trailing != NULL) {
        ok = ok && append(out, size, &len, " :");
        if (ok)
            append(out, size, &len, m->trailing);   /* may be cut short */
    }
    if (!ok)
        return -1;
    memcpy(out + len, "\r\n", 3);     /* the 3 copies the '\0' too */
    return (int)len + 2;
}

/* ---- the session with our one client ------------------------------------- */

/* Everything the server knows about a connected client. */
struct conn {
    char nick[NICK_MAX + 1];   /* "" until the client registers */
    bool quitting;             /* sent QUIT: stop reading */
};

/* The name to put in replies: the nick, or "*" before registration. */
static const char *target(const struct conn *c)
{
    return c->nick[0] != '\0' ? c->nick : "*";
}

/* Format a message and send it to the client (for now: to stdout). */
static void send_msg(struct conn *c, const struct msg *m)
{
    char line[MSG_LINE_MAX + 1];
    (void)c;   /* one client on stdout for now; later each conn has a socket */
    if (msg_format(line, sizeof line, m) < 0) {
        log_msg(LOG_WARN, "message too long to send: %s", m->command);
        return;
    }
    fputs(line, stdout);
}

/* Send ":parley CODE <target> [param] :text", the server's reply format. */
static void send_numeric(struct conn *c, const char *code, const char *param,
                         const char *text)
{
    struct msg m = {
        .prefix = SERVER_NAME,
        .command = code,
        .nparams = 1,
        .params = { target(c), param },
        .trailing = text,
    };
    if (param != NULL)
        m.nparams = 2;
    send_msg(c, &m);
}

static void cmd_nick(struct conn *c, const struct msg *m)
{
    const char *nick = msg_param(m, 0);
    if (nick == NULL || nick[0] == '\0') {
        send_numeric(c, "431", NULL, "No nickname given");
        return;
    }
    if (!nick_valid(nick)) {
        send_numeric(c, "432", nick, "Erroneous nickname");
        return;
    }
    if (c->nick[0] == '\0') {                       /* registration */
        char welcome[64];
        snprintf(welcome, sizeof welcome, "Welcome to parley, %s", nick);
        str_copy(c->nick, nick, sizeof c->nick);
        send_numeric(c, "001", NULL, welcome);
        send_msg(c, &(struct msg){ .prefix = c->nick, .command = "JOIN" });
        send_numeric(c, "353", NULL, c->nick);      /* who is here: just you */
        send_numeric(c, "366", NULL, "End of NAMES");
        log_msg(LOG_INFO, "%s joined", c->nick);
    } else if (strcmp(c->nick, nick) != 0) {        /* rename */
        send_msg(c, &(struct msg){
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
        send_numeric(c, "412", NULL, "No text to send");
        return;
    }
    send_msg(c, &(struct msg){ .prefix = c->nick, .command = command, .trailing = text });
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
    send_numeric(c, "353", NULL, c->nick);
    send_numeric(c, "366", NULL, "End of NAMES");
}

static void cmd_ping(struct conn *c, const struct msg *m)
{
    const char *token = msg_param(m, 0);
    if (token == NULL) {
        send_numeric(c, "461", "PING", "Not enough parameters");
        return;
    }
    send_msg(c, &(struct msg){ .prefix = SERVER_NAME, .command = "PONG", .trailing = token });
}

static void cmd_pong(struct conn *c, const struct msg *m)
{
    (void)c;   /* a reply to our PING: nothing to do (yet) */
    (void)m;
}

static void cmd_quit(struct conn *c, const struct msg *m)
{
    const char *reason = msg_param(m, 0);
    log_msg(LOG_INFO, "%s left (%s)", target(c), reason != NULL ? reason : "Quit");
    send_msg(c, &(struct msg){ .command = "ERROR", .trailing = "Goodbye" });
    c->quitting = true;
}

/* The command table. Each entry ties a command name to its handler. */
typedef void command_fn(struct conn *c, const struct msg *m);

struct command {
    const char *name;
    command_fn *handler;
    bool needs_nick;        /* only allowed after registration */
};

static const struct command commands[] = {
    { .name = "NICK",  .handler = cmd_nick,  .needs_nick = false },
    { .name = "MSG",   .handler = cmd_msg,   .needs_nick = true  },
    { .name = "ME",    .handler = cmd_me,    .needs_nick = true  },
    { .name = "NAMES", .handler = cmd_names, .needs_nick = true  },
    { .name = "PING",  .handler = cmd_ping,  .needs_nick = false },
    { .name = "PONG",  .handler = cmd_pong,  .needs_nick = false },
    { .name = "QUIT",  .handler = cmd_quit,  .needs_nick = false },
};
enum { NCOMMANDS = sizeof commands / sizeof commands[0] };

static void handle_line(struct conn *c, char *line)
{
    struct msg m;
    enum msg_status status = msg_parse(line, &m);
    if (status == MSG_EMPTY)
        return;
    if (status != MSG_OK) {
        send_numeric(c, "400", NULL, "Malformed message");
        return;
    }

    for (int i = 0; i < NCOMMANDS; i++) {
        const struct command *cmd = &commands[i];
        if (!str_ieq(m.command, cmd->name))
            continue;
        if (cmd->needs_nick && c->nick[0] == '\0')
            send_numeric(c, "451", NULL, "You have not registered");
        else
            cmd->handler(c, &m);
        return;
    }
    send_numeric(c, "421", m.command, "Unknown command");
}

int main(int argc, char *argv[])
{
    struct config cfg = {
        .addr = "127.0.0.1",
        .port = DEFAULT_PORT,
        .max_clients = DEFAULT_MAX_CLIENTS,
        .verbose = false,
    };
    parse_args(argc, argv, &cfg);
    log_verbose = cfg.verbose;
    log_msg(LOG_DEBUG, "verbose logging is on");
    log_msg(LOG_INFO, "reading one client session from stdin");

    struct conn client = { .quitting = false };   /* nick[] starts as "" */
    char line[MSG_LINE_MAX + 1];
    while (!client.quitting && fgets(line, sizeof line, stdin) != NULL) {
        if (strchr(line, '\n') == NULL && !feof(stdin)) {
            int c;
            while ((c = getchar()) != EOF && c != '\n')
                ;
            send_numeric(&client, "417", NULL, "Input line too long");
            continue;
        }
        str_chomp(line);
        handle_line(&client, line);
    }
    log_msg(LOG_INFO, "session over");
    return 0;
}
