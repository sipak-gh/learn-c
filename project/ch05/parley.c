/* parley.c - the parley chat client.
 * Chapter 5: no network yet. The client reads what you type on stdin and
 * prints the protocol lines it would send to the server on stdout. */
#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { DEFAULT_PORT = 6667, PORT_MAX = 65535, INPUT_MAX = 512 };

static const char *opt_host = "localhost";
static long opt_port = DEFAULT_PORT;
static const char *opt_nick = "guest";

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

static long parse_number(const char *text, long min, long max)
{
    char *end;
    errno = 0;
    long n = strtol(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || n < min || n > max)
        return -1;
    return n;
}

static void parse_args(int argc, char *argv[])
{
    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];

        if (strcmp(arg, "--help") == 0) {
            print_help();
            exit(0);
        }
        if (strcmp(arg, "--version") == 0) {
            printf("parley 0.1\n");
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
            opt_host = value;
            break;
        case 'n':
            opt_nick = value;
            break;
        case 'p':
            opt_port = parse_number(value, 1, PORT_MAX);
            if (opt_port < 0)
                usage_error("invalid port '%s' (expected 1-%d)", value, PORT_MAX);
            break;
        }
    }
}

/* Set by /quit: stop reading input. */
static bool quitting = false;

/* The protocol ends every line with CR LF ("\r\n"). */
static void cmd_nick(const char *arg)
{
    if (*arg == '\0') {
        fprintf(stderr, "parley: usage: /nick NEWNICK\n");
        return;
    }
    printf("NICK %s\r\n", arg);
}

static void cmd_me(const char *arg)
{
    if (*arg == '\0') {
        fprintf(stderr, "parley: usage: /me ACTION\n");
        return;
    }
    printf("ME :%s\r\n", arg);
}

static void cmd_who(const char *arg)
{
    (void)arg;   /* unused: every handler must have the same type */
    printf("NAMES\r\n");
}

static void cmd_quit(const char *arg)
{
    if (*arg == '\0')
        printf("QUIT\r\n");
    else
        printf("QUIT :%s\r\n", arg);
    quitting = true;
}

static void cmd_help(const char *arg)
{
    (void)arg;
    /* stdout carries protocol lines, so help goes to stderr. */
    fprintf(stderr, "commands:\n"
                    "  /nick NEWNICK   change your nickname\n"
                    "  /me ACTION      describe an action\n"
                    "  /who            list who is in the room\n"
                    "  /quit [REASON]  leave\n"
                    "  /help           show this help\n"
                    "anything else is sent as a message\n");
}

/* The dispatch table: two parallel arrays. command_names[i] is handled by
 * command_funcs[i]. command_funcs is "an array of const pointers to
 * functions taking (const char *) and returning void". Chapter 7 merges
 * the two arrays into one array of structs. */
static const char *const command_names[] = { "nick", "me", "who", "quit", "help" };
static void (*const command_funcs[])(const char *arg) = {
    cmd_nick, cmd_me, cmd_who, cmd_quit, cmd_help,
};
enum { NCOMMANDS = sizeof command_names / sizeof command_names[0] };

/* Cut the string at the first '\r' or '\n'. */
static void strip_newline(char *s)
{
    for (char *p = s; *p != '\0'; p++) {
        if (*p == '\r' || *p == '\n') {
            *p = '\0';
            return;
        }
    }
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
        if (strcmp(name, command_names[i]) == 0) {
            command_funcs[i](arg);    /* call through the function pointer */
            return;
        }
    }
    fprintf(stderr, "parley: unknown command /%s (try /help)\n", name);
}

int main(int argc, char *argv[])
{
    parse_args(argc, argv);
    printf("NICK %s\r\n", opt_nick);

    char line[INPUT_MAX];
    while (!quitting && fgets(line, sizeof line, stdin) != NULL) {
        strip_newline(line);
        if (line[0] == '\0')
            continue;              /* ignore empty lines */
        if (line[0] == '/')
            run_command(line);
        else
            printf("MSG :%s\r\n", line);
    }
    if (!quitting)
        printf("QUIT\r\n");      /* end of input means goodbye */
    return 0;
}
