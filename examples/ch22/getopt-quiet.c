/* getopt-quiet.c - getopt()'s own error messages, or ours.
 *     getopt-quiet [-q] [-p PORT] [-v]
 * -q must come first: it chooses the option string. */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char *argv[])
{
    /* A leading ':' in the option string: getopt() prints nothing, returns
     * ':' for a missing value, '?' for an unknown option, and puts the
     * option letter in optopt. */
    const char *opts = argc > 1 && strcmp(argv[1], "-q") == 0 ? ":qp:v" : "qp:v";
    int opt;

    while ((opt = getopt(argc, argv, opts)) != -1) {
        switch (opt) {
        case 'q':
            break;
        case 'p':
            printf("port %s\n", optarg);
            break;
        case 'v':
            printf("verbose\n");
            break;
        case ':':
            fprintf(stderr, "%s: option -%c needs a value\n", argv[0], optopt);
            return 2;
        default:                       /* '?' */
            if (opts[0] == ':')
                fprintf(stderr, "%s: unknown option '-%c'\n", argv[0], optopt);
            return 2;
        }
    }
    printf("first operand: argv[%d] = %s\n", optind, optind < argc ? argv[optind] : "(none)");
    return 0;
}
