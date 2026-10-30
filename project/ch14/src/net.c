/* net.c - TCP sockets: listen, connect, and addresses as text. */
#include "net.h"

#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define LISTEN_BACKLOG 16   /* connections the kernel may queue for accept() */

/* Resolve host:port into a list of TCP addresses with getaddrinfo(). The
 * caller frees the list with freeaddrinfo(). Returns 0, or -1 with an
 * error message in err. */
static int resolve(const char *host, int port, int flags, struct addrinfo **list,
                   char *err, size_t errsize)
{
    char service[8];
    snprintf(service, sizeof service, "%d", port);

    struct addrinfo hints;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;            /* IPv4 or IPv6 */
    hints.ai_socktype = SOCK_STREAM;        /* TCP */
    hints.ai_flags = flags | AI_NUMERICSERV;

    int rc = getaddrinfo(host, service, &hints, list);
    if (rc != 0) {
        /* getaddrinfo has its own error codes: gai_strerror, not strerror. */
        snprintf(err, errsize, "%s: %s", host, gai_strerror(rc));
        return -1;
    }
    return 0;
}

int net_listen(const char *addr, int port, char *err, size_t errsize)
{
    struct addrinfo *list;
    if (resolve(addr, port, AI_PASSIVE, &list, err, errsize) < 0)
        return -1;

    int fd = -1;
    for (struct addrinfo *ai = list; ai != NULL; ai = ai->ai_next) {
        char where[NET_ADDR_MAX];
        net_addr_str(ai->ai_addr, ai->ai_addrlen, where, sizeof where);

        fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd < 0) {
            snprintf(err, errsize, "socket: %s", strerror(errno));
            continue;
        }
        if (bind(fd, ai->ai_addr, ai->ai_addrlen) == 0
                && listen(fd, LISTEN_BACKLOG) == 0)
            break;                          /* success */
        snprintf(err, errsize, "%s: %s", where, strerror(errno));
        close(fd);
        fd = -1;
    }
    freeaddrinfo(list);
    return fd;
}

int net_connect(const char *host, int port, char *err, size_t errsize)
{
    struct addrinfo *list;
    if (resolve(host, port, 0, &list, err, errsize) < 0)
        return -1;

    int fd = -1;
    for (struct addrinfo *ai = list; ai != NULL; ai = ai->ai_next) {
        char where[NET_ADDR_MAX];
        net_addr_str(ai->ai_addr, ai->ai_addrlen, where, sizeof where);

        fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd < 0) {
            snprintf(err, errsize, "socket: %s", strerror(errno));
            continue;
        }
        if (connect(fd, ai->ai_addr, ai->ai_addrlen) == 0)
            break;
        snprintf(err, errsize, "%s: %s", where, strerror(errno));
        close(fd);
        fd = -1;
    }
    freeaddrinfo(list);
    return fd;
}

void net_addr_str(const struct sockaddr *sa, socklen_t len, char *out, size_t size)
{
    /* INET6_ADDRSTRLEN (POSIX) fits any numeric address. NI_MAXHOST would
     * be bigger, but it is not POSIX: strict builds don't define it. */
    char host[INET6_ADDRSTRLEN], port[8];
    /* NI_NUMERIC*: give numbers, never look up names. */
    int rc = getnameinfo(sa, len, host, sizeof host, port, sizeof port,
                         NI_NUMERICHOST | NI_NUMERICSERV);
    if (rc != 0)
        snprintf(out, size, "?");
    else if (sa->sa_family == AF_INET6)
        snprintf(out, size, "[%s]:%s", host, port);
    else
        snprintf(out, size, "%s:%s", host, port);
}

int net_local_addr(int fd, char *out, size_t size)
{
    struct sockaddr_storage ss;             /* big enough for any address */
    socklen_t len = sizeof ss;
    if (getsockname(fd, (struct sockaddr *)&ss, &len) < 0)
        return -1;
    net_addr_str((struct sockaddr *)&ss, len, out, size);
    return 0;
}

int net_local_port(int fd)
{
    struct sockaddr_storage ss;
    socklen_t len = sizeof ss;
    if (getsockname(fd, (struct sockaddr *)&ss, &len) < 0)
        return -1;
    if (ss.ss_family == AF_INET)
        return ntohs(((struct sockaddr_in *)&ss)->sin_port);
    if (ss.ss_family == AF_INET6)
        return ntohs(((struct sockaddr_in6 *)&ss)->sin6_port);
    return -1;
}
