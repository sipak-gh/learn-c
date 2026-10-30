/* pton.c - text <-> binary IP addresses with inet_pton/inet_ntop.
 * usage: pton ADDRESS... */
#define _POSIX_C_SOURCE 200809L
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>

int main(int argc, char *argv[])
{
    for (int i = 1; i < argc; i++) {
        unsigned char bin[sizeof(struct in6_addr)];   /* 16: fits both */
        char text[INET6_ADDRSTRLEN];                  /* 46: fits both */
        int family = AF_INET;
        int rc = inet_pton(AF_INET, argv[i], bin);
        if (rc != 1) {
            family = AF_INET6;
            rc = inet_pton(AF_INET6, argv[i], bin);
        }
        if (rc != 1) {
            printf("%-24s not an IPv4 or IPv6 address\n", argv[i]);
            continue;
        }
        int n = family == AF_INET ? 4 : 16;
        printf("%-24s IPv%d, %2d bytes:", argv[i], family == AF_INET ? 4 : 6, n);
        for (int j = 0; j < n; j++)
            printf(" %d", bin[j]);
        inet_ntop(family, bin, text, sizeof text);  /* and back to text */
        printf("  -> %s\n", text);
    }
    return 0;
}
