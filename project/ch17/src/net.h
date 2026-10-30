/* net.h - TCP sockets: listen, connect, and addresses as text. */
#ifndef PARLEY_NET_H
#define PARLEY_NET_H

#include <stddef.h>
#include <sys/socket.h>

/* Room for "[ipv6-address]:port" and a '\0'. */
#define NET_ADDR_MAX 64

/* Create a TCP socket listening on addr:port. addr may be a name
 * ("localhost") or a numeric IPv4/IPv6 address; port 0 lets the kernel
 * choose a free port. Returns the socket, or -1 with an error message in
 * err[errsize]. */
int net_listen(const char *addr, int port, char *err, size_t errsize);

/* Connect to host:port over TCP, trying each address the name resolves
 * to. Returns the socket, or -1 with an error message in err[errsize]. */
int net_connect(const char *host, int port, char *err, size_t errsize);

/* Format a socket address as "127.0.0.1:6667" or "[::1]:6667". */
void net_addr_str(const struct sockaddr *sa, socklen_t len, char *out, size_t size);

/* The local address of socket fd, formatted like net_addr_str(). Returns
 * 0, or -1 with errno set. */
int net_local_addr(int fd, char *out, size_t size);

/* The local port of socket fd (useful after listening on port 0), or -1. */
int net_local_port(int fd);

#endif
