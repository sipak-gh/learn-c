/* getopt-order.c - does getopt() find options after a non-option word?
 * A preview of Chapter 22, where the project switches to getopt. */
#define _POSIX_C_SOURCE 200809L          /* ask for POSIX functions (Chapter 9) */
#include <stdio.h>
#include <unistd.h>                      /* getopt, optarg, optind */

int main(int argc, char *argv[])
{
    const char *port = "6667";
    int verbose = 0;
    int opt;

    while ((opt = getopt(argc, argv, "p:v")) != -1) {
        switch (opt) {
        case 'p':
            port = optarg;
            break;
        case 'v':
            verbose = 1;
            break;
        default:
            fprintf(stderr, "usage: %s [-p PORT] [-v] [ARGS...]\n", argv[0]);
            return 2;
        }
    }
    printf("port=%s verbose=%d\n", port, verbose);
    for (int i = optind; i < argc; i++)
        printf("operand: %s\n", argv[i]);
    return 0;
}
