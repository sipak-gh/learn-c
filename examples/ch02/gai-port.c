/* gai-port.c - what getaddrinfo() does with out-of-range port numbers.
 * A preview of Chapter 14: run it now, read the code later. */
#define _POSIX_C_SOURCE 200809L
#include <arpa/inet.h>
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>

int main(void)
{
    const char *ports[] = { "6667", "65535", "65536", "99999" };

    for (size_t i = 0; i < sizeof ports / sizeof ports[0]; i++) {
        struct addrinfo hints, *res;
        memset(&hints, 0, sizeof hints);
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        int rc = getaddrinfo("127.0.0.1", ports[i], &hints, &res);
        if (rc != 0) {
            printf("\"%s\" -> error: %s\n", ports[i], gai_strerror(rc));
            continue;
        }
        struct sockaddr_in *sin = (struct sockaddr_in *)(void *)res->ai_addr;
        printf("\"%s\" -> port %u\n", ports[i], (unsigned)ntohs(sin->sin_port));
        freeaddrinfo(res);
    }
    return 0;
}
