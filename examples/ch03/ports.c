/* ports.c - if / else if / else: classify a TCP port number. */
#include <stdio.h>

int main(void)
{
    long port;

    if (scanf("%ld", &port) != 1) {
        fprintf(stderr, "ports: expected a number\n");
        return 1;
    }

    if (port < 1 || port > 65535) {
        printf("%ld: not a valid port\n", port);
        return 1;
    } else if (port < 1024) {
        printf("%ld: well-known port (binding it needs root)\n", port);
    } else if (port < 49152) {
        printf("%ld: registered port\n", port);
    } else {
        printf("%ld: dynamic port (used for outgoing connections)\n", port);
    }

    if (port == 6667)
        printf("that's the traditional IRC port\n");
    return 0;
}
