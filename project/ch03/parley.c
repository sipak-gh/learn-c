/* parley.c - the parley chat client (Chapter 3: command-line options). */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { DEFAULT_PORT = 6667, PORT_MAX = 65535 };

int main(int argc, char *argv[])
{
    const char *host = "localhost";
    long port = DEFAULT_PORT;
    const char *nick = "guest";

    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];

        /* -h means "host" here, so help is only --help. */
        if (strcmp(arg, "--help") == 0) {
            printf("usage: parley [-h HOST] [-p PORT] [-n NICK]\n"
                   "  -h HOST    server to connect to (default localhost)\n"
                   "  -p PORT    server port (default %d)\n"
                   "  -n NICK    your nickname (default guest)\n"
                   "  --help     show this help and exit\n"
                   "  --version  show the version and exit\n",
                   DEFAULT_PORT);
            return 0;
        }
        if (strcmp(arg, "--version") == 0) {
            printf("parley 0.1\n");
            return 0;
        }
        if (arg[0] != '-' || arg[1] == '\0' || arg[2] != '\0') {
            fprintf(stderr, "parley: unexpected argument '%s'\n", arg);
            goto usage_error;
        }

        char opt = arg[1];
        if (opt != 'h' && opt != 'p' && opt != 'n') {
            fprintf(stderr, "parley: unknown option '%s'\n", arg);
            goto usage_error;
        }
        if (i + 1 >= argc) {
            fprintf(stderr, "parley: option -%c needs a value\n", opt);
            goto usage_error;
        }
        const char *value = argv[++i];

        switch (opt) {
        case 'h':
            host = value;
            break;
        case 'n':
            nick = value;
            break;
        case 'p': {
            char *end;
            errno = 0;
            long n = strtol(value, &end, 10);
            if (errno != 0 || end == value || *end != '\0' || n < 1 || n > PORT_MAX) {
                fprintf(stderr, "parley: invalid port '%s' (expected 1-%d)\n",
                        value, PORT_MAX);
                goto usage_error;
            }
            port = n;
            break;
        }
        }
    }

    printf("parley: would connect to %s:%ld as %s\n", host, port, nick);
    return 0;

usage_error:
    fprintf(stderr, "Try 'parley --help' for more information.\n");
    return 2;
}
