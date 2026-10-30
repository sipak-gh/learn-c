/* parley.c - the parley chat client.
 * Version 1: blocking I/O. After each line you type, the client sends it,
 * then shows what the server sent until the server has answered. While it
 * waits for your input, it cannot notice anything the server sends. */
#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include <sys/socket.h>
#include <unistd.h>

#include "io.h"
#include "log.h"
#include "msg.h"
#include "net.h"
#include "sig.h"
#include "str.h"
#include "version.h"

#define DEFAULT_PORT 6667
#define PORT_MAX 65535
/* Longest line we send as a message: the server adds ":nick " to it, and
 * that must still fit in MSG_LINE_MAX. */
#define INPUT_MAX 400

/* Settings from the command line. */
struct config {
    const char *host;
    long port;
    const char *nick;
};

static void usage_error(const char *fmt, ...)
{
    fprintf(stderr, "parley: ");
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\nTry 'parley --help' for more information.\n");
    exit(2);
}

static void print_help(void)
{
    printf("usage: parley [-h HOST] [-p PORT] [-n NICK]\n"
           "  -h HOST    server to connect to (default localhost)\n"
           "  -p PORT    server port (default %d)\n"
           "  -n NICK    your nickname (default guest)\n"
           "  --help     show this help and exit\n"
           "  --version  show the version and exit\n",
           DEFAULT_PORT);
}

static void parse_args(int argc, char *argv[], struct config *cfg)
{
    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];

        if (strcmp(arg, "--help") == 0) {
            print_help();
            exit(0);
        }
        if (strcmp(arg, "--version") == 0) {
            printf("parley " PARLEY_VERSION "\n");
            exit(0);
        }
        if (arg[0] != '-' || arg[1] == '\0' || arg[2] != '\0')
            usage_error("unexpected argument '%s'", arg);

        char opt = arg[1];
        if (opt != 'h' && opt != 'p' && opt != 'n')
            usage_error("unknown option '%s'", arg);
        if (i + 1 >= argc)
            usage_error("option -%c needs a value", opt);
        const char *value = argv[++i];

        switch (opt) {
        case 'h':
            cfg->host = value;
            break;
        case 'n':
            cfg->nick = value;
            break;
        case 'p':
            if (!str_to_long(value, 1, PORT_MAX, &cfg->port))
                usage_error("invalid port '%s' (expected 1-%d)", value, PORT_MAX);
            break;
        }
    }
}

/* Set by /quit: stop reading input. */
static bool quitting = false;

/* The connection to the server. */
static int server_fd = -1;
static struct strbuf server_in;     /* received, not yet handled */
static bool server_skipping;        /* skipping a too-long line */

/* Send one protocol message to the server. */
static void send_msg(const struct msg *m)
{
    char line[MSG_LINE_MAX + 1];
    int n = msg_format(line, sizeof line, m);
    if (n > 0 && write_all(server_fd, line, (size_t)n) < 0)
        die("sending to server: %s", strerror(errno));
}

/* Our PING token: the server's PONG with it means "it has seen everything
 * we sent before the PING". */
#define SYNC_TOKEN "sync"

/* Print the lines the server sends until it answers our PING (returns
 * true), or until it closes the connection (returns false). */
static bool show_replies(bool want_pong)
{
    char line[MSG_LINE_MAX + 1];
    for (;;) {
        enum frame_status st;
        while ((st = msg_take_line(&server_in, &server_skipping, line)) != FRAME_NONE) {
            if (st == FRAME_TOO_LONG)
                continue;              /* the server never sends those */
            char copy[MSG_LINE_MAX + 1];
            struct msg m;
            str_copy(copy, line, sizeof copy);   /* msg_parse cuts it up */
            if (msg_parse(copy, &m) == MSG_OK && str_ieq(m.command, "PONG")
                    && msg_param(&m, 0) != NULL
                    && strcmp(msg_param(&m, 0), SYNC_TOKEN) == 0) {
                if (want_pong) {
                    fflush(stdout);    /* show it now: we may wait for input next */
                    return true;
                }
                continue;
            }
            printf("%s\n", line);
        }
        fflush(stdout);
        ssize_t n = read_into(server_fd, &server_in, 4096);
        if (n < 0 && errno == EINTR)
            continue;
        if (n < 0)
            die("reading from server: %s", strerror(errno));
        if (n == 0)
            return false;              /* end of file: the server hung up */
    }
}

/* Send a PING and show everything up to its PONG. */
static void sync_with_server(void)
{
    send_msg(&(struct msg){ .command = "PING", .trailing = SYNC_TOKEN });
    if (!show_replies(true) && !quitting)
        die("the server closed the connection");
}

static void cmd_nick(const char *arg)
{
    if (*arg == '\0') {
        fprintf(stderr, "parley: usage: /nick NEWNICK\n");
        return;
    }
    send_msg(&(struct msg){ .command = "NICK", .nparams = 1, .params = { arg } });
}

static void cmd_me(const char *arg)
{
    if (*arg == '\0') {
        fprintf(stderr, "parley: usage: /me ACTION\n");
        return;
    }
    send_msg(&(struct msg){ .command = "ME", .trailing = arg });
}

static void cmd_who(const char *arg)
{
    (void)arg;   /* unused: every handler must have the same type */
    send_msg(&(struct msg){ .command = "NAMES" });
}

static void cmd_quit(const char *arg)
{
    /* "QUIT" alone, or "QUIT :reason" */
    send_msg(&(struct msg){ .command = "QUIT", .trailing = *arg != '\0' ? arg : NULL });
    quitting = true;
}

static void cmd_help(const char *arg);

/* The dispatch table: one struct per slash command. The table also holds
 * the help text, so /help can never get out of date. */
typedef void command_fn(const char *arg);

struct command {
    const char *name;
    const char *args;      /* argument summary for /help */
    const char *help;
    command_fn *run;
};

static const struct command commands[] = {
    { "nick", "NEWNICK",  "change your nickname",    cmd_nick },
    { "me",   "ACTION",   "describe an action",      cmd_me   },
    { "who",  "",         "list who is in the room", cmd_who  },
    { "quit", "[REASON]", "leave",                   cmd_quit },
    { "help", "",         "show this help",          cmd_help },
};
enum { NCOMMANDS = sizeof commands / sizeof commands[0] };

static void cmd_help(const char *arg)
{
    (void)arg;
    printf("commands:\n");
    for (int i = 0; i < NCOMMANDS; i++) {
        char usage[32];
        snprintf(usage, sizeof usage, "/%s %s", commands[i].name, commands[i].args);
        printf("  %-15s %s\n", usage, commands[i].help);
    }
    printf("anything else is sent as a message\n");
}

/* "/nick bob" -> run cmd_nick("bob"). Modifies line in place. */
static void run_command(char *line)
{
    char *name = line + 1;   /* skip the '/' */
    char *arg = name;
    while (*arg != '\0' && *arg != ' ')
        arg++;
    if (*arg == ' ')
        *arg++ = '\0';      /* end the name; arg now points after the space */

    for (int i = 0; i < NCOMMANDS; i++) {
        if (strcmp(name, commands[i].name) == 0) {
            commands[i].run(arg);     /* call through the function pointer */
            return;
        }
    }
    fprintf(stderr, "parley: unknown command /%s (try /help)\n", name);
}

int main(int argc, char *argv[])
{
    struct config cfg = { .host = "localhost", .port = DEFAULT_PORT, .nick = "guest" };
    parse_args(argc, argv, &cfg);
    log_init("parley", false);
    if (sig_init() < 0)
        die("setting up signals: %s", strerror(errno));

    char err[256];
    server_fd = net_connect(cfg.host, (int)cfg.port, err, sizeof err);
    if (server_fd < 0)
        die("cannot connect to %s", err);
    send_msg(&(struct msg){ .command = "NICK", .nparams = 1, .params = { cfg.nick } });
    sync_with_server();

    /* getline (POSIX) reads a whole line of any length, growing the
     * buffer with realloc as needed. We own the buffer and free it. */
    char *line = NULL;
    size_t cap = 0;
    ssize_t len;
    while (!quitting && (len = getline(&line, &cap, stdin)) != -1) {
        if (strlen(line) != (size_t)len) {
            log_msg(LOG_WARN, "line contains a NUL byte, not sent");
            continue;
        }
        str_chomp(line);
        if (strlen(line) > INPUT_MAX) {
            log_msg(LOG_WARN, "line too long (max %d bytes), not sent", INPUT_MAX);
            continue;
        }
        if (line[0] == '\0')
            continue;              /* ignore empty lines */
        if (line[0] == '/')
            run_command(line);
        else
            send_msg(&(struct msg){ .command = "MSG", .trailing = line });
        if (!quitting)
            sync_with_server();
    }
    /* getline fails with EINTR when Ctrl-C (SIGINT) arrives: quit too. */
    if (ferror(stdin) && sig_quit == 0)
        die("reading input: %s", strerror(errno));
    free(line);

    if (!quitting)
        send_msg(&(struct msg){ .command = "QUIT" });   /* end of input: goodbye */
    /* shutdown(SHUT_WR): "we will send nothing more" (the server sees end of
     * file), but we can still read its last words until it closes. */
    shutdown(server_fd, SHUT_WR);
    show_replies(false);
    close(server_fd);
    sb_free(&server_in);
    if (fflush(stdout) == EOF)
        die("writing: %s", strerror(errno));
    return 0;
}
