/* parleyd.c - the parley chat server.
 * Chapter 6: no network yet. The server talks to one client on
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
    NICK_MAX = 16         /* longest nickname */
};

enum log_level { LOG_DEBUG, LOG_INFO, LOG_WARN, LOG_ERROR };

/* Settings from the command line. 'static' at file scope means "private to
 * this file" (internal linkage): no other .c file can see these names. */
static const char *opt_addr = "127.0.0.1";
static long opt_port = DEFAULT_PORT;
static long opt_max_clients = DEFAULT_MAX_CLIENTS;
static bool opt_verbose = false;

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
    if (level == LOG_DEBUG && !opt_verbose)
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

static void parse_args(int argc, char *argv[])
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
            opt_addr = value;
            break;
        case 'p':
            if (!str_to_long(value, 1, PORT_MAX, &opt_port))
                usage_error("invalid port '%s' (expected 1-%d)", value, PORT_MAX);
            break;
        case 'm':
            if (!str_to_long(value, 1, MAX_CLIENTS_MAX, &opt_max_clients))
                usage_error("invalid client limit '%s' (expected 1-%d)",
                            value, MAX_CLIENTS_MAX);
            break;
        case 'v':
            opt_verbose = true;
            break;
        default:
            usage_error("unknown option '%s'", arg);
        }
    }
}

/* ---- the session with our one client ------------------------------------- */

static char client_nick[NICK_MAX + 1];  /* "" until the client registers */
static bool client_quit = false;

/* The name to put in replies: the nick, or "*" before registration. */
static const char *target(void)
{
    return client_nick[0] != '\0' ? client_nick : "*";
}

/* Send one protocol line built printf-style. The line (without "\r\n")
 * must fit in MSG_LINE_MAX - 2 bytes; vsnprintf tells us if it didn't. */
static void reply(const char *fmt, ...)
{
    char line[MSG_LINE_MAX - 2 + 1];   /* + 1 for the NUL terminator */
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    if (n < 0)
        return;
    if ((size_t)n >= sizeof line)
        log_msg(LOG_WARN, "reply truncated from %d to %zu bytes", n, sizeof line - 1);
    printf("%s\r\n", line);
}

static void handle_nick(const char *arg)
{
    if (*arg == '\0') {
        reply(":parley 431 %s :No nickname given", target());
        return;
    }
    if (!nick_valid(arg)) {
        reply(":parley 432 %s %s :Erroneous nickname", target(), arg);
        return;
    }
    if (client_nick[0] == '\0') {          /* first NICK: registration */
        str_copy(client_nick, arg, sizeof client_nick);
        reply(":parley 001 %s :Welcome to parley, %s", client_nick, client_nick);
        reply(":%s JOIN", client_nick);
        log_msg(LOG_INFO, "%s joined", client_nick);
    } else if (strcmp(client_nick, arg) != 0) {   /* a rename */
        reply(":%s NICK %s", client_nick, arg);
        log_msg(LOG_INFO, "%s is now %s", client_nick, arg);
        str_copy(client_nick, arg, sizeof client_nick);
    }
}

static void handle_line(char *line)
{
    /* Split "COMMAND rest of line" at the first space. */
    char *arg = strchr(line, ' ');
    if (arg != NULL)
        *arg++ = '\0';
    else
        arg = line + strlen(line);   /* no argument: point at the "" */

    if (line[0] == '\0')
        return;                       /* empty line: ignore it */
    if (str_ieq(line, "NICK")) {
        handle_nick(arg);
    } else if (str_ieq(line, "QUIT")) {
        reply("ERROR :Goodbye");
        client_quit = true;
    } else if (client_nick[0] == '\0') {
        reply(":parley 451 * :You have not registered");
    } else if (str_ieq(line, "MSG")) {
        if (*arg == ':')
            arg++;                    /* skip the ':' before the text */
        reply(":%s MSG :%s", client_nick, arg);
    } else {
        reply(":parley 421 %s %s :Unknown command", target(), line);
    }
}

int main(int argc, char *argv[])
{
    parse_args(argc, argv);
    log_msg(LOG_DEBUG, "verbose logging is on");
    log_msg(LOG_INFO, "reading one client session from stdin");

    char line[MSG_LINE_MAX + 1];
    while (!client_quit && fgets(line, sizeof line, stdin) != NULL) {
        /* fgets stops at '\n' or when the array is full. No '\n' (and not
         * at end of input) means the line was too long: drop the rest. */
        if (strchr(line, '\n') == NULL && !feof(stdin)) {
            int c;
            while ((c = getchar()) != EOF && c != '\n')
                ;
            reply(":parley 417 %s :Input line too long", target());
            continue;
        }
        str_chomp(line);
        handle_line(line);
    }
    log_msg(LOG_INFO, "session over");
    return 0;
}
