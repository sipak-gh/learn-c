/* parley.c - the parley chat client.
 * No network yet (Chapters 5-15). The client reads what you type on stdin and
 * prints the protocol lines it would send to the server on stdout. */
#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "log.h"
#include "msg.h"
#include "str.h"
#include "version.h"

#define DEFAULT_PORT 6667
#define PORT_MAX 65535

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

/* Send one protocol message (for now: print it on stdout). */
static void send_msg(const struct msg *m)
{
    char line[MSG_LINE_MAX + 1];
    if (msg_format(line, sizeof line, m) > 0)
        fputs(line, stdout);
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
    /* stdout carries protocol lines, so help goes to stderr. */
    fprintf(stderr, "commands:\n");
    for (int i = 0; i < NCOMMANDS; i++) {
        char usage[32];
        snprintf(usage, sizeof usage, "/%s %s", commands[i].name, commands[i].args);
        fprintf(stderr, "  %-15s %s\n", usage, commands[i].help);
    }
    fprintf(stderr, "anything else is sent as a message\n");
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
    send_msg(&(struct msg){ .command = "NICK", .nparams = 1, .params = { cfg.nick } });

    char line[MSG_LINE_MAX];
    while (!quitting && fgets(line, sizeof line, stdin) != NULL) {
        if (strchr(line, '\n') == NULL && !feof(stdin)) {
            int c;
            while ((c = getchar()) != EOF && c != '\n')
                ;   /* throw away the rest of the long line */
            fprintf(stderr, "parley: line too long (max %zu bytes), not sent\n",
                    sizeof line - 2);
            continue;
        }
        str_chomp(line);
        if (line[0] == '\0')
            continue;              /* ignore empty lines */
        if (line[0] == '/')
            run_command(line);
        else
            send_msg(&(struct msg){ .command = "MSG", .trailing = line });
    }
    if (!quitting)
        send_msg(&(struct msg){ .command = "QUIT" });   /* end of input: goodbye */
    if (ferror(stdin))
        die("reading input: %s", strerror(errno));
    if (fflush(stdout) == EOF)
        die("writing: %s", strerror(errno));
    return 0;
}
