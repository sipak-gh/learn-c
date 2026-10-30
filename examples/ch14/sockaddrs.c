/* sockaddrs.c - the socket address structs, filled in by hand. */
#define _POSIX_C_SOURCE 200809L
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>

static void dump(const char *label, const void *p, size_t n)
{
    const unsigned char *b = p;
    printf("%s (%zu bytes):", label, n);
    for (size_t i = 0; i < n; i++)
        printf(" %02x", b[i]);
    printf("\n");
}

int main(void)
{
    printf("sizeof(struct sockaddr)         = %zu\n", sizeof(struct sockaddr));
    printf("sizeof(struct sockaddr_in)      = %zu\n", sizeof(struct sockaddr_in));
    printf("sizeof(struct sockaddr_in6)     = %zu\n", sizeof(struct sockaddr_in6));
    printf("sizeof(struct sockaddr_storage) = %zu\n", sizeof(struct sockaddr_storage));

    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_port = htons(6667);                     /* network byte order! */
    inet_pton(AF_INET, "127.0.0.1", &a.sin_addr); /* already network order */
    dump("127.0.0.1:6667", &a, sizeof a);

    struct sockaddr_in6 b;
    memset(&b, 0, sizeof b);
    b.sin6_family = AF_INET6;
    b.sin6_port = htons(6667);
    inet_pton(AF_INET6, "::1", &b.sin6_addr);
    dump("[::1]:6667", &b, sizeof b);
    return 0;
}
