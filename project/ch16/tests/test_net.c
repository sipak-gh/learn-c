/* test_net.c - tests for net.c, over the loopback interface. */
#include <string.h>
#include <unistd.h>

#include "net.h"
#include "test.h"
#include "tests.h"

void test_net_loopback(void)
{
    char err[128] = "", addr[NET_ADDR_MAX];

    /* Port 0: the kernel picks a free port, so tests never clash. */
    int lfd = net_listen("127.0.0.1", 0, err, sizeof err);
    CHECK(lfd >= 0);
    CHECK_STR(err, "");
    int port = net_local_port(lfd);
    CHECK(port > 0);

    int cfd = net_connect("127.0.0.1", port, err, sizeof err);
    CHECK(cfd >= 0);
    int sfd = accept(lfd, NULL, NULL);   /* the server's end */
    CHECK(sfd >= 0);

    /* Bytes written at one end come out at the other. */
    char buf[8] = "";
    CHECK_INT(write(cfd, "PING\n", 5), 5);
    CHECK_INT(read(sfd, buf, sizeof buf - 1), 5);
    CHECK_STR(buf, "PING\n");

    CHECK_INT(net_local_addr(lfd, addr, sizeof addr), 0);
    CHECK(strncmp(addr, "127.0.0.1:", 10) == 0);
    close(cfd);
    close(sfd);
    close(lfd);
}

void test_net_ipv6(void)
{
    char err[128], addr[NET_ADDR_MAX];
    int lfd = net_listen("::1", 0, err, sizeof err);
    if (lfd < 0)
        return;                        /* no IPv6 on this machine: skip */
    CHECK_INT(net_local_addr(lfd, addr, sizeof addr), 0);
    CHECK(strncmp(addr, "[::1]:", 6) == 0);
    close(lfd);
}

void test_net_errors(void)
{
    char err[128];
    int lfd = net_listen("127.0.0.1", 0, err, sizeof err);
    CHECK(lfd >= 0);
    int port = net_local_port(lfd);

    /* Listening twice on one port fails (EADDRINUSE). */
    CHECK_INT(net_listen("127.0.0.1", port, err, sizeof err), -1);
    CHECK(strstr(err, "127.0.0.1:") == err);

    /* Nobody listens any more after close: connecting is refused. */
    close(lfd);
    CHECK_INT(net_connect("127.0.0.1", port, err, sizeof err), -1);

    /* A bad address fails in getaddrinfo, before any socket exists. */
    CHECK_INT(net_listen("300.1.2.3", 0, err, sizeof err), -1);
    CHECK(strncmp(err, "300.1.2.3: ", 11) == 0);
}
