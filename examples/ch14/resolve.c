/* resolve.c - show what getaddrinfo() returns for a host and port.
 * usage: resolve [-4 | -6 | -a] HOST [PORT]
 *   -4, -6  only IPv4 / only IPv6 (default: both)
 *   -a      all socket types (default: TCP only) */
#define _POSIX_C_SOURCE 200809L
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>

static const char *type_name(int t)
{
    return t == SOCK_STREAM ? "TCP" : t == SOCK_DGRAM ? "UDP" : t == SOCK_RAW ? "raw" : "?";
}

int main(int argc, char *argv[])
{
    struct addrinfo hints;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    int i = 1;
    for (; i < argc && argv[i][0] == '-'; i++) {
        if (strcmp(argv[i], "-4") == 0)
            hints.ai_family = AF_INET;
        else if (strcmp(argv[i], "-6") == 0)
            hints.ai_family = AF_INET6;
        else if (strcmp(argv[i], "-a") == 0)
            hints.ai_socktype = 0;
    }
    if (i >= argc) {
        fprintf(stderr, "usage: resolve [-4 | -6 | -a] HOST [PORT]\n");
        return 2;
    }
    const char *host = argv[i];
    const char *port = i + 1 < argc ? argv[i + 1] : NULL;

    struct addrinfo *list;
    int rc = getaddrinfo(host, port, &hints, &list);
    if (rc != 0) {
        /* Not errno: getaddrinfo returns its own codes (EAI_...). */
        printf("getaddrinfo: error %d: %s\n", rc, gai_strerror(rc));
        return 1;
    }
    for (struct addrinfo *ai = list; ai != NULL; ai = ai->ai_next) {
        char addr[INET6_ADDRSTRLEN], serv[8];
        getnameinfo(ai->ai_addr, ai->ai_addrlen, addr, sizeof addr, serv, sizeof serv,
                    NI_NUMERICHOST | NI_NUMERICSERV);
        printf("%-5s %-3s %-16s port %-5s (addrlen %d)\n",
               ai->ai_family == AF_INET ? "IPv4" : "IPv6", type_name(ai->ai_socktype),
               addr, serv, (int)ai->ai_addrlen);
    }
    freeaddrinfo(list);
    return 0;
}
