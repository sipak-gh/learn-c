/* parleyd.c - the parley chat server (Chapter 3: command-line options). */
#include <errno.h>
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

int main(int argc, char *argv[])
{
    const char *addr = "127.0.0.1";
    long port = DEFAULT_PORT;
    long max_clients = DEFAULT_MAX_CLIENTS;
    bool verbose = false;

    /* argv[0] is the program name; options start at argv[1].
     * strcmp(a, b) == 0 means "a and b are the same string" (Chapter 6). */
    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];

        if (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0) {
            printf("usage: parleyd [-p PORT] [-b ADDR] [-m MAXCLIENTS] [-v]\n"
                   "  -p PORT        TCP port to listen on (default %d)\n"
                   "  -b ADDR        address to listen on (default 127.0.0.1)\n"
                   "  -m MAXCLIENTS  most clients at once, 1-%d (default %d)\n"
                   "  -v             verbose (debug) logging\n"
                   "  -h, --help     show this help and exit\n"
                   "  --version      show the version and exit\n",
                   DEFAULT_PORT, MAX_CLIENTS_MAX, DEFAULT_MAX_CLIENTS);
            return 0;
        }
        if (strcmp(arg, "--version") == 0) {
            printf("parleyd 0.1\n");
            return 0;
        }

        /* Every other option is exactly two characters: '-' and a letter. */
        if (arg[0] != '-' || arg[1] == '\0' || arg[2] != '\0') {
            fprintf(stderr, "parleyd: unexpected argument '%s'\n", arg);
            goto usage_error;
        }

        char opt = arg[1];
        const char *value = NULL;
        if (opt == 'p' || opt == 'b' || opt == 'm') {
            if (i + 1 >= argc) {
                fprintf(stderr, "parleyd: option -%c needs a value\n", opt);
                goto usage_error;
            }
            value = argv[++i];  /* consume the next word as the value */
        }

        switch (opt) {
        case 'b':
            addr = value;
            break;
        case 'p':   /* -p and -m share the number parsing below */
        case 'm': {
            long min = 1;
            long max = opt == 'p' ? PORT_MAX : MAX_CLIENTS_MAX;
            char *end;
            errno = 0;
            long n = strtol(value, &end, 10);
            if (errno != 0 || end == value || *end != '\0' || n < min || n > max) {
                fprintf(stderr, "parleyd: invalid value '%s' for -%c (expected %ld-%ld)\n",
                        value, opt, min, max);
                goto usage_error;
            }
            if (opt == 'p')
                port = n;
            else
                max_clients = n;
            break;
        }
        case 'v':
            verbose = true;
            break;
        default:
            fprintf(stderr, "parleyd: unknown option '%s'\n", arg);
            goto usage_error;
        }
    }

    printf("parleyd: would listen on %s:%ld (max %ld clients%s)\n",
           addr, port, max_clients, verbose ? ", verbose" : "");
    return 0;

usage_error:
    /* One exit path for every command-line mistake: exit status 2 is the
     * Unix convention for "you used me wrong". */
    fprintf(stderr, "Try 'parleyd --help' for more information.\n");
    return 2;
}
