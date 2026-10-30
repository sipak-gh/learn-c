/* parley.c - the parley chat client (Chapter 4: functions). */
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { DEFAULT_PORT = 6667, PORT_MAX = 65535 };

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

int main(int argc, char *argv[])
{
    parse_args(argc, argv);
    printf("parley: would connect to %s:%ld as %s\n", opt_host, opt_port, opt_nick);
    return 0;
}
