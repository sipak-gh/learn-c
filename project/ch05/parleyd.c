/* parleyd.c - the parley chat server (Chapter 4: functions and logging). */
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
    MAX_CLIENTS_MAX = 1000
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

/* Convert text to a number in [min, max]. Returns -1 if the text is not a
 * whole number in range (fine as an error value because min >= 0 here). */
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
            opt_port = parse_number(value, 1, PORT_MAX);
            if (opt_port < 0)
                usage_error("invalid port '%s' (expected 1-%d)", value, PORT_MAX);
            break;
        case 'm':
            opt_max_clients = parse_number(value, 1, MAX_CLIENTS_MAX);
            if (opt_max_clients < 0)
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

int main(int argc, char *argv[])
{
    parse_args(argc, argv);
    log_msg(LOG_DEBUG, "verbose logging is on");
    log_msg(LOG_INFO, "would listen on %s:%ld (max %ld clients)",
            opt_addr, opt_port, opt_max_clients);
    return 0;
}
