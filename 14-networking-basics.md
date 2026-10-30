# Chapter 14 — Networking Basics

So far `parleyd` reads one client session from stdin. Real chat needs clients at the other end of a
**network connection**. This chapter covers what a programmer needs to know about networks, all on your
own machine: IP addresses, ports, TCP and UDP, how a connection opens and closes, byte order, the socket
address structs, and `getaddrinfo`, which turns a name and port into something you can connect to. You'll
write a small TCP client and test it against `nc`.

Name lookup is where glibc and musl differ most visibly, so there are several `glibc vs musl` sections. In
the project, the server opens its first **listening socket**. It doesn't accept connections yet; that's
[Chapter 15](15-a-first-server.md), which uses the signals from [Chapter
13](13-processes-and-signals.md) again.

## The big picture

Network communication is split into layers. Each solves one problem and relies on the layer below:

| layer | job | in this course |
|---|---|---|
| application | what the bytes mean | the parley protocol: `NICK al`, `PING`, ... |
| transport | connect two **programs**, reliably or not | TCP (ports) |
| network (IP) | get a packet to the right **machine** | IPv4 `127.0.0.1`, IPv6 `::1` |
| link | move bytes over a wire, Wi-Fi, or nothing | the **loopback** interface |

Your code sees only the top two. You open a TCP connection to an address and port, then read and write
bytes. The kernel does the rest: splits the bytes into packets, numbers them, resends lost ones, and
reassembles them in order on the other side.

This course runs everything over **loopback**: a virtual network interface inside the kernel that connects
your machine to itself. Packets never leave the computer, so you need no network, no firewall changes and
no second machine. The code is the same as for a real network.

## IP addresses

An **IP address** names a network interface on a machine. There are two versions:

| | IPv4 | IPv6 |
|---|---|---|
| size | 32 bits (4 bytes) | 128 bits (16 bytes) |
| text form | 4 decimal numbers: `192.168.1.20` | 8 groups of 4 hex digits; `::` replaces one run of zero groups: `2001:db8::42` |
| loopback | `127.0.0.1` (all of `127.0.0.0/8`, in fact) | `::1` |
| "any address" (for servers) | `0.0.0.0` | `::` |
| examples for documentation | `192.0.2.0/24` | `2001:db8::/32` |

`/8` means "the first 8 bits are fixed": everything from `127.0.0.0` to `127.255.255.255` is loopback.
IPv4's roughly 4 billion addresses ran out years ago; IPv6 fixes that. Both are in use, so your code
should handle both. That's easy once `getaddrinfo` does the work (below).

The `ip` command shows the loopback interface, `lo`:

```console
$ ip addr show lo
1: lo: <LOOPBACK,UP,LOWER_UP> mtu 65536 qdisc noqueue state UNKNOWN group default qlen 1000
    link/loopback 00:00:00:00:00:00 brd 00:00:00:00:00:00
    inet 127.0.0.1/8 scope host lo
       valid_lft forever preferred_lft forever
    inet 10.255.255.254/32 brd 10.255.255.254 scope global lo
       valid_lft forever preferred_lft forever
    inet6 ::1/128 scope host 
       valid_lft forever preferred_lft forever
```

`inet` lines are IPv4, `inet6` lines are IPv6. The `10.255.255.254` line is specific to this machine: Windows
Subsystem for Linux adds it. On plain Linux you'll usually see just `127.0.0.1/8` and `::1/128`.

## Ports

An IP address gets a packet to the right machine; a **port** (a 16-bit number, 0–65535) gets it to the
right program there. A TCP connection is identified by four values: local address, local port, remote
address, remote port. So one server port can serve many clients at once: each connection differs in the
remote address or port.

| range | name | notes |
|---|---|---|
| 0 | — | in `bind()`: "pick any free port for me" |
| 1–1023 | well-known ports | `22` ssh, `80` http, `443` https; on Linux, binding them needs root (or `CAP_NET_BIND_SERVICE`) |
| 1024–49151 | registered ports | `6667` IRC (and parley's default), `8080` http-alt |
| 49152–65535 | dynamic ports | for temporary use |

When a **client** connects, the kernel gives its socket a free temporary port, an **ephemeral port**, from
this range:

```console
$ cat /proc/sys/net/ipv4/ip_local_port_range
32768	60999
```

Many port numbers have names, listed in `/etc/services`:

```console
$ grep -w -e ssh -e http -e ircd /etc/services
ssh		22/tcp				# SSH Remote Login Protocol
http		80/tcp		www		# WorldWideWeb HTTP
ircd		6667/tcp			# Internet Relay Chat
```

## TCP and UDP

IP delivers single packets with no promises: they can be lost, duplicated, or reordered. Two transport
protocols sit on top:

| | TCP | UDP |
|---|---|---|
| model | a **connection**: a two-way stream of bytes | single **datagrams** (messages) |
| setup | a handshake before any data | none: just send |
| delivery | every byte arrives, once, in order, or you get an error | a datagram may be lost, duplicated, reordered |
| message boundaries | **none**: 2 writes may arrive as 1 read, or 1 write as 3 reads | kept: one send is one receive |
| socket type | `SOCK_STREAM` | `SOCK_DGRAM` |
| used by | web, ssh, mail, IRC, parley | DNS, video calls, games |

Chat needs every message, in order, so parley uses TCP. For a programmer, "no message boundaries" is the
most important row; [Chapter 15](15-a-first-server.md) shows it happening.

## Loopback and localhost

`localhost` is a **name**; `127.0.0.1` and `::1` are **addresses**. A name must be looked up. On Linux the
lookup reads `/etc/hosts` first, then may ask a DNS server. On this machine:

```console
$ grep localhost /etc/hosts
127.0.0.1	localhost
::1     ip6-localhost ip6-loopback
```

So here `localhost` is IPv4 only, and the IPv6 loopback is `ip6-localhost`. This is common on Debian,
Ubuntu and WSL; other systems also list `::1 localhost`. It causes a glibc vs musl difference
[below](#glibc-vs-musl-localhost-and-ipv6).

## Clients, servers and sockets

A **server** waits for connections at a known address and port. A **client** connects to it. After that,
both sides are equal: each can read and write.

A **socket** is a program's end of a network connection. To your program it's a file descriptor, so
`read`, `write` and `close` ([Chapter 12](12-files-and-descriptors.md)) work on it. A few extra calls set
it up:

| step | server | client |
|---|---|---|
| 1 | `socket()`: make a TCP socket | `socket()`: make a TCP socket |
| 2 | `bind()`: claim an address and port | — (the kernel picks an ephemeral port) |
| 3 | `listen()`: become a listening socket | — |
| 4 | `accept()`: take the next connection; returns a **new** socket for it | `connect()`: open a connection |
| 5 | `read()`/`write()` on the new socket | `read()`/`write()` |
| 6 | `close()` | `close()` |

The server has two kinds of sockets: one **listening** socket, which only produces connections, and one
**connected** socket per client. This chapter uses server steps 1–3 and the whole client side; [Chapter
15](15-a-first-server.md) covers `accept` and the server loop.

## How a TCP connection opens and closes

Opening takes a **three-way handshake**. Closing takes a FIN from each side:

```text
  client                                   server (listening)
    |  ---- SYN  "I want to connect" --->     |
    |  <--- SYN+ACK  "OK, you too?" -----     |   connection now waits in the
    |  ---- ACK  "yes" ----------------->     |   server's accept queue
    |                                         |
    |  <======= data, both ways ========>     |
    |                                         |
    |  ---- FIN  "I'm done sending" ----->    |   first to close: goes to TIME_WAIT
    |  <--- ACK -------------------------     |
    |  <--- FIN  "me too" ---------------     |
    |  ---- ACK ------------------------->    |
  TIME_WAIT (60 s on Linux)                closed
```

Notice:

- `connect()` returns when the handshake is done. On the server side the **kernel** answers the
  handshake, before the program calls `accept()`. Finished connections wait in a queue, the **backlog**.
- Each direction closes separately. The other side's `read()` sees "I'm done sending" as **0, end of
  file**.
- The side that closes first stays in **TIME_WAIT** (60 seconds on Linux). It catches late packets from
  the old connection, so they can't be mistaken for a new connection on the same ports. [Chapter
  15](15-a-first-server.md) shows how TIME_WAIT can stop a server from restarting.

Seeing the packets needs `tcpdump` and root. You can watch the connection **states** without root, with
`ss` (socket statistics). The next program listens but never accepts, so you see the kernel do the
handshake alone.

`examples/ch14/listen-only.c`:

```c
/* listen-only.c - listen on a port but never accept(), then exit.
 * usage: listen-only PORT SECONDS */
#define _POSIX_C_SOURCE 200809L
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

int main(int argc, char *argv[])
{
    if (argc != 3) {
        fprintf(stderr, "usage: listen-only PORT SECONDS\n");
        return 2;
    }
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("socket");
        return 1;
    }
    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_port = htons((uint16_t)atoi(argv[1]));
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);     /* 127.0.0.1 */
    if (bind(fd, (struct sockaddr *)&a, sizeof a) < 0) {
        perror("bind");
        return 1;
    }
    if (listen(fd, 5) < 0) {
        perror("listen");
        return 1;
    }
    printf("listening on 127.0.0.1:%s, not accepting\n", argv[1]);
    fflush(stdout);
    sleep((unsigned)atoi(argv[2]));
    close(fd);
    return 0;
}
```

The next sections explain `htons`, `sockaddr_in` and the cast. Start it, then connect two clients with `nc`
(netcat), a tool that connects to or listens on a port and copies data between the socket and your
terminal. The first `nc` stays connected for 2 seconds; the second, `nc -z`, only checks that the port
accepts connections, then closes.

```console
$ ./listen-only 6832 3 &
listening on 127.0.0.1:6832, not accepting
$ ss -tln 'sport = :6832'
State  Recv-Q Send-Q Local Address:Port Peer Address:PortProcess
LISTEN 0      5          127.0.0.1:6832      0.0.0.0:*          
$ sleep 2 | nc 127.0.0.1 6832 &
$ nc -z -v 127.0.0.1 6832
Connection to 127.0.0.1 6832 port [tcp/*] succeeded!
$ ss -tan 'sport = :6832 or dport = :6832'
State      Recv-Q Send-Q Local Address:Port  Peer Address:Port Process
LISTEN     2      5          127.0.0.1:6832       0.0.0.0:*           
ESTAB      0      0          127.0.0.1:55698    127.0.0.1:6832        
FIN-WAIT-2 0      0          127.0.0.1:55708    127.0.0.1:6832        
CLOSE-WAIT 1      0          127.0.0.1:6832     127.0.0.1:55708       
ESTAB      0      0          127.0.0.1:6832     127.0.0.1:55698       
```

How to read it (`-t` TCP, `-l` listening only, `-a` all states, `-n` numbers instead of names):

- `LISTEN` line: for a listening socket, **Send-Q is the backlog** (5, from `listen(fd, 5)`) and
  **Recv-Q is the number of connections waiting** for `accept()` (2).
- Both connections "succeeded" without `accept()`: the kernel did the handshake.
- Each connection appears twice, once per end. Client ends have ephemeral ports (55698, 55708; yours will
  differ).
- The first `nc` is `ESTAB`lished on both ends.
- `nc -z` already sent its FIN: its end waits for the server's FIN (`FIN-WAIT-2`); the server end got it
  but hasn't closed (`CLOSE-WAIT`). It never will, because the program never accepted it. When
  `listen-only` exits, the kernel cleans up.

## Byte order

A port number is 2 bytes; an IPv4 address is 4. In which order do the bytes go? CPUs disagree:

- **little-endian** (x86, most ARM setups): lowest byte first in memory;
- **big-endian**: highest byte first.

Network protocols use one order on the wire: **big-endian**, called **network byte order**. Socket address
structs hold ports and addresses in network byte order, so you convert:

| function | converts | for |
|---|---|---|
| `htons(x)` | host to network, 16 bits | ports |
| `ntohs(x)` | network to host, 16 bits | ports |
| `htonl(x)` | host to network, 32 bits | IPv4 addresses as numbers |
| `ntohl(x)` | network to host, 32 bits | IPv4 addresses as numbers |

On a big-endian CPU they do nothing; on little-endian they swap the bytes. Always call them, so your code
works on both. They're declared in `<arpa/inet.h>`.

`examples/ch14/byteorder.c`:

```c
/* byteorder.c - how a 16-bit port number is stored in memory. */
#define _POSIX_C_SOURCE 200809L
#include <arpa/inet.h>
#include <stdint.h>
#include <stdio.h>

static void show(const char *label, uint16_t value)
{
    const unsigned char *b = (const unsigned char *)&value;
    printf("%-22s 0x%04x  bytes in memory: %02x %02x\n", label, value, b[0], b[1]);
}

int main(void)
{
    uint16_t port = 6667;               /* 0x1a0b */
    show("host order:", port);
    show("htons (network order):", htons(port));
    show("ntohs(htons(port)):", ntohs(htons(port)));

    uint32_t one = 1;
    printf("this CPU is %s-endian\n",
           *(const unsigned char *)&one == 1 ? "little" : "big");
    return 0;
}
```

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o byteorder byteorder.c
$ ./byteorder
host order:            0x1a0b  bytes in memory: 0b 1a
htons (network order): 0x0b1a  bytes in memory: 1a 0b
ntohs(htons(port)):    0x1a0b  bytes in memory: 0b 1a
this CPU is little-endian
```

Same with musl. Look at the memory bytes: after `htons`, the high byte `1a` comes first, as the network
wants. As a number the converted value is meaningless (`0x0b1a` is 2842): never print or compare it, only
store it in a socket address.

Gotcha: forgetting `htons` is a classic bug. `bind` succeeds, but on port 2842 instead of 6667, and your
clients get "Connection refused".

## Socket addresses

Socket functions work with many address families (IPv4, IPv6, local Unix sockets...), each with its own
struct. C99 has no inheritance, so the API uses a trick: every struct starts with a **family** field, and
functions take a pointer to a generic `struct sockaddr` plus the struct's length.

| struct | header | family field | for |
|---|---|---|---|
| `struct sockaddr` | `<sys/socket.h>` | `sa_family` | the generic type in function signatures; never declare one |
| `struct sockaddr_in` | `<netinet/in.h>` | `sin_family = AF_INET` | IPv4: `sin_port`, `sin_addr` |
| `struct sockaddr_in6` | `<netinet/in.h>` | `sin6_family = AF_INET6` | IPv6: `sin6_port`, `sin6_addr`, and more |
| `struct sockaddr_storage` | `<sys/socket.h>` | `ss_family` | big enough for **any** address: use it when you don't know the family yet |

`examples/ch14/sockaddrs.c` fills in an IPv4 and an IPv6 address by hand and dumps their bytes:

```c
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
```

```console
$ ./sockaddrs
sizeof(struct sockaddr)         = 16
sizeof(struct sockaddr_in)      = 16
sizeof(struct sockaddr_in6)     = 28
sizeof(struct sockaddr_storage) = 128
127.0.0.1:6667 (16 bytes): 02 00 1a 0b 7f 00 00 01 00 00 00 00 00 00 00 00
[::1]:6667 (28 bytes): 0a 00 1a 0b 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 01 00 00 00 00
```

Same sizes and bytes with musl: both follow the Linux kernel's layout. Decode the IPv4 one:

| bytes | field | value |
|---|---|---|
| `02 00` | `sin_family` | `AF_INET` = 2, in **host** order (the kernel reads it, not the network) |
| `1a 0b` | `sin_port` | 6667, network order |
| `7f 00 00 01` | `sin_addr` | 127.0.0.1, network order |
| `00` × 8 | padding | must be zero: that's why you `memset` first |

To pass one to a socket function, cast its address: `bind(fd, (struct sockaddr *)&a, sizeof a)`. The
length tells the kernel which struct it really is. The other way round, `accept()` and `getsockname()`
fill in a `sockaddr_storage`; check `ss_family`, then cast to `sockaddr_in` or `sockaddr_in6`.

`listen-only.c` used `htonl(INADDR_LOOPBACK)`: `INADDR_LOOPBACK` is `0x7f000001` (127.0.0.1) in host
order, so it needs `htonl`.

## Text addresses: inet_pton and inet_ntop

Two functions convert a **numeric** address between text and binary:

| function | direction | returns |
|---|---|---|
| `inet_pton(family, text, dst)` | "presentation to network": text to bytes | 1 if valid, 0 if not an address of that family |
| `inet_ntop(family, src, buf, size)` | bytes to text | `buf`, or `NULL` if `size` is too small |

`INET_ADDRSTRLEN` (16) and `INET6_ADDRSTRLEN` (46) are buffer sizes that fit any address of each family,
plus the `'\0'`.

`examples/ch14/pton.c`:

```c
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
```

```console
$ ./pton 127.0.0.1 192.168.1.20 ::1 ::ffff:127.0.0.1 2001:db8::42 localhost 256.1.1.1 1.2.3 127.000.000.001
127.0.0.1                IPv4,  4 bytes: 127 0 0 1  -> 127.0.0.1
192.168.1.20             IPv4,  4 bytes: 192 168 1 20  -> 192.168.1.20
::1                      IPv6, 16 bytes: 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 1  -> ::1
::ffff:127.0.0.1         IPv6, 16 bytes: 0 0 0 0 0 0 0 0 0 0 255 255 127 0 0 1  -> ::ffff:127.0.0.1
2001:db8::42             IPv6, 16 bytes: 32 1 13 184 0 0 0 0 0 0 0 0 0 0 0 66  -> 2001:db8::42
localhost                not an IPv4 or IPv6 address
256.1.1.1                not an IPv4 or IPv6 address
1.2.3                    not an IPv4 or IPv6 address
127.000.000.001          not an IPv4 or IPv6 address
```

musl prints the same: `inet_pton` is strict on both (POSIX allows only four decimal numbers 0–255 for
IPv4). `localhost` is a **name**, not an address: these functions never look anything up.
`::ffff:127.0.0.1` is an "IPv4-mapped" IPv6 address: an IPv4 address dressed as IPv6.

You'll rarely call these directly: `getaddrinfo` (next) accepts numeric addresses and names alike, and
`getnameinfo` converts back. But they show what an address really is: just bytes.

## Names to addresses: getaddrinfo

`getaddrinfo` is the modern, family-neutral way to get socket addresses. Give it a host and port (as
strings) plus some **hints**; it returns a linked list of ready-to-use addresses:

```c
int getaddrinfo(const char *host, const char *service,
                const struct addrinfo *hints, struct addrinfo **result);
void freeaddrinfo(struct addrinfo *list);
const char *gai_strerror(int code);
```

| `struct addrinfo` field | in `hints` you set | in each result you get |
|---|---|---|
| `ai_family` | `AF_UNSPEC` (any), `AF_INET` or `AF_INET6` | the family of this address |
| `ai_socktype` | `SOCK_STREAM` (TCP) or `SOCK_DGRAM` (UDP) | the socket type |
| `ai_protocol` | 0 | the protocol (6 for TCP) |
| `ai_flags` | options, see below | — |
| `ai_addr`, `ai_addrlen` | — | the address, ready for `bind`/`connect` |
| `ai_next` | — | the next result, or `NULL` |

Useful flags:

- `AI_PASSIVE`: the address is for `bind`; with a `NULL` host you get the "any" address;
- `AI_NUMERICHOST`: the host must be a numeric address (never look up names);
- `AI_NUMERICSERV`: the port must be a number (never read `/etc/services`).

The arguments of `socket` and `bind`/`connect` match the `addrinfo` fields, so the standard code is a
loop: for each result, `socket(ai_family, ai_socktype, ai_protocol)`, then `connect(fd, ai_addr,
ai_addrlen)`; stop at the first that works.

Two gotchas:

- On error, getaddrinfo **returns** a code (`EAI_NONAME`, `EAI_SERVICE`, ...) instead of setting `errno`.
  Turn it into text with `gai_strerror(rc)`, **not** `strerror`.
- The result list is allocated: free it with `freeaddrinfo()`. Copy what you need first.

`examples/ch14/resolve.c` prints whatever getaddrinfo returns:

```c
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
```

`getnameinfo` is the reverse: socket address to host and port text. With `NI_NUMERICHOST |
NI_NUMERICSERV` it gives numbers, never looks up names, and does the `ntohs` for you.

Where both libraries agree:

```console
$ ./resolve localhost 6667
IPv4  TCP 127.0.0.1        port 6667  (addrlen 16)
$ ./resolve ::1 6667
IPv6  TCP ::1              port 6667  (addrlen 28)
$ ./resolve ip6-localhost 6667
IPv6  TCP ::1              port 6667  (addrlen 28)
$ ./resolve localhost ircd
IPv4  TCP 127.0.0.1        port 6667  (addrlen 16)
$ ./resolve localhost
IPv4  TCP 127.0.0.1        port 0     (addrlen 16)
```

Numeric addresses are converted directly, `localhost` comes from `/etc/hosts`, the service name `ircd`
from `/etc/services`, and no port means port 0. Now the differences.

### glibc vs musl: getaddrinfo error texts

Error **codes** are the same on both libraries; the **texts** differ:

```console
$ ./resolve no-such-host.invalid 6667
getaddrinfo: error -2: Name or service not known
$ ./resolve-musl no-such-host.invalid 6667
getaddrinfo: error -2: Name does not resolve
$ ./resolve localhost -1
getaddrinfo: error -8: Servname not supported for ai_socktype
$ ./resolve-musl localhost -1
getaddrinfo: error -8: Unrecognized service
```

| code | glibc `gai_strerror` | musl `gai_strerror` |
|---|---|---|
| `EAI_NONAME` | Name or service not known | Name does not resolve |
| `EAI_SERVICE` | Servname not supported for ai_socktype | Unrecognized service |
| `EAI_AGAIN` | Temporary failure in name resolution | Try again |
| `EAI_FAIL` | Non-recoverable failure in name resolution | Non-recoverable error |
| `EAI_FAMILY` | ai_family not supported | Unrecognized address family or invalid length |
| `EAI_SOCKTYPE` | ai_socktype not supported | Unrecognized socket type |
| `EAI_MEMORY` | Memory allocation failure | Out of memory |

musl's texts are shorter and plainer ("Name does not resolve" says what happened; glibc's "Name or service
not known" leaves you guessing which). For correct code it doesn't matter: show `gai_strerror(rc)` to
humans, and compare `rc` with the `EAI_` constants in code, never the text.

A malformed address such as `300.1.2.3` isn't an address, so both libraries try it as a **name** and fail
with `EAI_NONAME` (on glibc, after asking DNS).

### glibc vs musl: port numbers out of range

A port is 16 bits. What if the string says `99999`?

```console
$ ./resolve localhost 99999
IPv4  TCP 127.0.0.1        port 34463 (addrlen 16)
$ ./resolve-musl localhost 99999
getaddrinfo: error -8: Unrecognized service
$ ./resolve localhost 65536
IPv4  TCP 127.0.0.1        port 0     (addrlen 16)
$ ./resolve-musl localhost 65536
getaddrinfo: error -8: Unrecognized service
```

glibc silently keeps the low 16 bits: 99999 − 65536 = 34463, and 65536 becomes 0, which for a server
means "any free port". musl rejects both. **musl is better here**: a wrong port should be an error, not a
different port. Portable code depends on neither: it range-checks the port itself with `strtol` (as
`parleyd`'s `-p` has done with `str_to_long` since [Chapter 9](09-preprocessor-and-building.md)), then
passes a known-good number to getaddrinfo.

### glibc vs musl: localhost and IPv6

Ask for **only IPv6** addresses of `localhost`, on a machine whose `/etc/hosts` lists `localhost` only for
IPv4:

```console
$ time ./resolve -6 localhost 6667
IPv6  TCP ::1              port 6667  (addrlen 28)

real	0m0.461s
user	0m0.002s
sys	0m0.000s
$ ./resolve-musl -6 localhost 6667
getaddrinfo: error -5: Name has no usable address
```

`strace` shows what happened:

- **glibc** found no IPv6 `localhost` in `/etc/hosts`, so it asked the **DNS server**. With this machine's
  search domains (`/etc/resolv.conf`), that took 20 DNS queries (almost half a second). The last, for
  `localhost.`, got `::1` back from the DNS server. The count and the answer depend on your network
  setup.
- **musl** opened `/etc/hosts`, found `localhost` with no IPv6 address, and stopped: "the name exists but
  has no usable address" (`EAI_NODATA`, a non-POSIX code). It sent no packets.

Count the packets yourself:

```console
$ strace -f -e trace=network ./resolve -6 localhost 6667 2>&1 | grep -c 'sendto\|sendmmsg'
20
$ strace -e trace=file,network ./resolve-musl -6 localhost 6667 2>&1 | grep -v execve
open("/etc/hosts", O_RDONLY|O_LARGEFILE|O_CLOEXEC) = 3
getaddrinfo: error -5: Name has no usable address
+++ exited with 1 +++
```

**musl is better here**: `localhost` should never cause network traffic (RFC 6761 says so), and the answer
shouldn't depend on which network you're on. glibc's answer is more convenient, but slow and depends on
your DNS server.

Portable advice:

- use `AF_UNSPEC` and try each result in order (the standard loop): `localhost` then gives `127.0.0.1` on
  both;
- for a local server, use the numeric `127.0.0.1` (parley's default) or `::1`: numbers never cause
  lookups.

### glibc vs musl: socket types with no hints

With `ai_socktype = 0` (or `hints == NULL`), getaddrinfo returns one result per socket type:

```console
$ ./resolve -a localhost 6667
IPv4  TCP 127.0.0.1        port 6667  (addrlen 16)
IPv4  UDP 127.0.0.1        port 6667  (addrlen 16)
IPv4  raw 127.0.0.1        port 6667  (addrlen 16)
$ ./resolve-musl -a localhost 6667
IPv4  TCP 127.0.0.1        port 6667  (addrlen 16)
IPv4  UDP 127.0.0.1        port 6667  (addrlen 16)
```

glibc adds a raw-socket entry a normal program can't even use (raw sockets need root). Both are allowed.
Always set `ai_socktype = SOCK_STREAM` in hints: you get just the TCP result, on both.

### glibc vs musl: static linking and getaddrinfo

Link `resolve.c` statically with glibc (the `/tmp/cc….o` name is a temporary object file and differs each
time):

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -static -o resolve-static resolve.c
/usr/bin/ld: /tmp/cc1UIQrT.o: in function `main':
resolve.c:(.text+0x260): warning: Using 'getaddrinfo' in statically linked applications requires at runtime the shared libraries from the glibc version used for linking
$ ls -l resolve resolve-musl resolve-static | awk '{print $5, $9}'
16416 resolve
81864 resolve-musl
1163232 resolve-static
```

glibc looks up names through **NSS** (the Name Service Switch, configured in `/etc/nsswitch.conf`). NSS
loads plug-in shared libraries at run time, even from a "static" binary. So a static glibc program that
calls `getaddrinfo` still needs the same glibc version installed where it runs. musl has no NSS: its
resolver reads `/etc/hosts` and talks DNS itself, all inside your 80 KB binary, with no warning.

**musl is simpler** for static binaries that use the network. The price: no NSS plug-ins (e.g. no LDAP or
mDNS host names). [Chapter 22](22-shipping-and-libc-choices.md) compares the two for shipping.

## A first TCP client

Now put it together. `fetch` connects to a host and port, optionally sends one line, and prints
everything it receives until the server closes the connection.

`examples/ch14/fetch.c`:

```c
/* fetch.c - a tiny TCP client: connect, send a line, print the reply.
 * usage: fetch HOST PORT [LINE] */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

int main(int argc, char *argv[])
{
    if (argc < 3) {
        fprintf(stderr, "usage: fetch HOST PORT [LINE]\n");
        return 2;
    }

    /* 1. Name and port -> a list of socket addresses. */
    struct addrinfo hints, *list;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;            /* IPv4 or IPv6, whatever works */
    hints.ai_socktype = SOCK_STREAM;        /* TCP */
    int rc = getaddrinfo(argv[1], argv[2], &hints, &list);
    if (rc != 0) {
        fprintf(stderr, "fetch: %s: %s\n", argv[1], gai_strerror(rc));
        return 1;
    }

    /* 2. Try each address until one connects. */
    int fd = -1;
    for (struct addrinfo *ai = list; ai != NULL; ai = ai->ai_next) {
        fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd < 0)
            continue;
        if (connect(fd, ai->ai_addr, ai->ai_addrlen) == 0)
            break;                          /* the handshake is done */
        fprintf(stderr, "fetch: connect: %s\n", strerror(errno));
        close(fd);
        fd = -1;
    }
    freeaddrinfo(list);
    if (fd < 0)
        return 1;

    /* 3. Send a request, if any. A socket is a file descriptor: write() works. */
    if (argc > 3) {
        char line[512];
        int n = snprintf(line, sizeof line, "%s\r\n", argv[3]);
        if (n < 0 || (size_t)n >= sizeof line || write(fd, line, (size_t)n) != n) {
            fprintf(stderr, "fetch: cannot send\n");
            close(fd);
            return 1;
        }
    }

    /* 4. Print everything until the server closes (read returns 0). */
    char buf[4096];
    ssize_t n;
    long total = 0;
    while ((n = read(fd, buf, sizeof buf)) > 0) {
        fwrite(buf, 1, (size_t)n, stdout);
        total += n;
    }
    if (n < 0)
        fprintf(stderr, "fetch: read: %s\n", strerror(errno));
    fprintf(stderr, "fetch: %ld bytes, connection closed\n", total);
    close(fd);
    return n < 0 ? 1 : 0;
}
```

Network protocols usually end lines with `\r\n` (carriage return, line feed), so `fetch` does too. For a
server, use `nc -l` ("listen"): it waits for one connection, sends its stdin, and prints what it receives.
`-N` closes the connection when its stdin ends. Here it runs in the background and saves what it received
in `got.txt`:

```console
$ printf 'Hello from nc!\n' | nc -N -l 127.0.0.1 6830 > got.txt &
$ ./fetch localhost 6830 'GET /'
Hello from nc!
fetch: 15 bytes, connection closed
$ od -c got.txt
0000000   G   E   T       /  \r  \n
0000007
$ ss -tan 'sport = :6830 or dport = :6830'
State     Recv-Q Send-Q Local Address:Port Peer Address:Port Process
TIME-WAIT 0      0          127.0.0.1:6830    127.0.0.1:45630       
```

The request arrived with its `\r\n`, the reply came back, and the connection ended. `nc` closed first, so
its end (port 6830) is now in **TIME_WAIT** for 60 seconds. (Your client port will differ.) The musl build
prints the same.

When nothing listens on the port, the server machine's kernel answers the SYN with a "reset" packet, and
`connect` fails at once:

```console
$ ./fetch 127.0.0.1 6831
fetch: connect: Connection refused
$ ./fetch nosuch.invalid 6831
fetch: nosuch.invalid: Name or service not known
$ ./fetch-musl nosuch.invalid 6831
fetch: nosuch.invalid: Name does not resolve
```

"Connection refused" (`ECONNREFUSED`) reads the same on both libraries.

### What the system calls look like

`strace -e trace=network` shows the socket calls; add `read,write,close` to see the data:

```console
$ printf 'Hello from nc!\n' | nc -N -l 127.0.0.1 6833 >/dev/null &
$ strace -e trace=network,read,write,close ./fetch-musl 127.0.0.1 6833 'GET /'
socket(AF_INET, SOCK_STREAM, IPPROTO_TCP) = 3
connect(3, {sa_family=AF_INET, sin_port=htons(6833), sin_addr=inet_addr("127.0.0.1")}, 16) = 0
write(3, "GET /\r\n", 7)                = 7
read(3, "Hello from nc!\n", 4096)       = 15
Hello from nc!
read(3, "", 4096)                       = 0
fetch: 15 bytes, connection closed
close(3)                                = 0
+++ exited with 0 +++
```

Each line matches a step in `fetch.c`. The socket is fd 3 (0, 1 and 2 are taken). `read` returning 0 is
the server's FIN. strace decodes the `sockaddr_in`, including `htons(6833)`: the port is stored in network
order. (`getaddrinfo` of a numeric address needs no lookup: no files, no network.)

### Tools for watching the network

| command | shows |
|---|---|
| `ss -tlnp` | listening TCP sockets, with numbers and the owning process |
| `ss -tan` | all TCP sockets and their states (`ESTAB`, `TIME-WAIT`, ...) |
| `ss -tan 'sport = :6667'` | only those with local port 6667 |
| `nc -l 127.0.0.1 PORT` | a one-connection server on your terminal |
| `nc 127.0.0.1 PORT` | a client on your terminal |
| `nc -z -v 127.0.0.1 PORT` | "is anything listening?" |
| `strace -e trace=network PROG` | the socket system calls a program makes |
| `ip addr` | network interfaces and their addresses |

## Building parley: a listening socket

A new module, `net.c`, hides everything socket-related behind five functions. The client ([Chapter
16](16-a-first-client.md)) will use it too. This chapter's server only **listens**: that proves the
address and port work; it still serves one session on stdin.

`src/net.h` (new):

```c
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
```

Errors come back as text in a caller-supplied buffer, `err[errsize]`: a failure can come from getaddrinfo
(`gai_strerror`) or a system call (`strerror`), and the caller just wants a message to print.

`src/net.c` (new):

```c
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
```

How it works:

- `resolve()` turns the `int` port into a string for getaddrinfo. `parleyd` has already range-checked it
  (0–65535), so glibc's silent truncation can't happen. `AI_NUMERICSERV` means "this is a number": no
  `/etc/services` lookup.
- `net_listen` and `net_connect` are the standard "try each result" loop. `AI_PASSIVE` marks the address
  as one to `bind`. The loop keeps the **last** error message; `err` only matters when the function
  returns -1.
- `bind(...) == 0 && listen(...) == 0`: `&&` stops at the first failure, so `errno` still belongs to the
  failed call when `snprintf` reads it.
- `net_addr_str` uses `getnameinfo` with numeric flags. IPv6 addresses get brackets, `[::1]:6667`,
  because the address contains colons; that's the standard notation (as in URLs).
- `net_local_addr` and `net_local_port` ask the kernel which address a socket really got, with
  `getsockname`. That's how you learn the port after binding port 0. `sockaddr_storage` holds either
  family; check `ss_family`, then cast.

In `src/parleyd.c`, include the header:

```c
#include "msg.h"
#include "net.h"
#include "room.h"
```

Allow port 0 in `-p` (the help text and the range check):

```c
           "  -p PORT        TCP port to listen on, 0: any free port (default %d)\n"
```

```c
        case 'p':
            if (!str_to_long(value, 0, PORT_MAX, &cfg->port))
                usage_error("invalid port '%s' (expected 0-%d)", value, PORT_MAX);
            break;
```

And the end of `main()` now opens the listening socket first:

```c
    if (sig_init() < 0)
        die("setting up signals: %s", strerror(errno));

    char err[256], where[NET_ADDR_MAX];
    int listen_fd = net_listen(cfg.addr, (int)cfg.port, err, sizeof err);
    if (listen_fd < 0)
        die("cannot listen on %s", err);
    if (net_local_addr(listen_fd, where, sizeof where) < 0)
        die("getsockname: %s", strerror(errno));
    log_msg(LOG_INFO, "listening on %s", where);

    /* Connections wait in the kernel's queue: we don't accept() them yet. */
    log_msg(LOG_INFO, "reading one client session from stdin");
    int status = serve_stdin() < 0 ? 1 : 0;
    close(listen_fd);
    log_msg(LOG_INFO, "session over");
    return status;
}
```

The log shows the address the kernel reports, not the one you asked for: with `-p 0`, that's how you see
the real port.

In the `Makefile`, add `net.o` to the server and `test_net.o` and `net.o` to the tests:

```make
SERVER_OBJS := $(addprefix $(BUILD)/, parleyd.o room.o conn.o io.o net.o sig.o msg.o nick.o strbuf.o str.o log.o)
CLIENT_OBJS := $(addprefix $(BUILD)/, parley.o msg.o strbuf.o str.o log.o)
TEST_OBJS   := $(addprefix $(BUILD)/, unit.o test_str.o test_nick.o test_strbuf.o \
                 test_msg.o test_io.o test_sig.o test_net.o \
                 io.o net.o sig.o msg.o nick.o strbuf.o str.o)
```

`tests/test_net.c` (new) tests over loopback. Port 0 makes the kernel choose a free port, so the tests
never clash with anything else on the machine:

```c
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
```

`test_net_loopback` uses a real `accept()` (next chapter) for the server's end. No loop is needed: the
client already connected, so the connection waits in the queue and `accept` returns at once. The tests
check only the **start** of error messages, because the rest is libc text that differs. Register them in
`tests/tests.h`:

```c
    X(sig_pipe_ignored)             \
    X(net_loopback)                 \
    X(net_ipv6)                     \
    X(net_errors)
```

### Checkpoint

```console
$ make -s test-all
unit: 28 passed, 0 failed
unit: 28 passed, 0 failed
$ make -s SANITIZE=1 test
unit: 28 passed, 0 failed
$ build/glibc/parleyd --help
usage: parleyd [-p PORT] [-b ADDR] [-m MAXCLIENTS] [-v]
  -p PORT        TCP port to listen on, 0: any free port (default 6667)
  -b ADDR        address to listen on (default 127.0.0.1)
  -m MAXCLIENTS  most clients at once, 1-1000 (default 64)
  -v             verbose (debug) logging
  -h, --help     show this help and exit
  --version      show the version and exit
```

Start a server on port 6614, with a named pipe as stdin so it stays up, and look at it from outside:

```console
$ mkfifo in
$ build/glibc/parleyd -p 6614 <in 2>err.txt &
$ exec 3>in; sleep 0.2
$ ss -tln "sport = :6614"
State  Recv-Q Send-Q Local Address:Port Peer Address:PortProcess
LISTEN 0      16         127.0.0.1:6614      0.0.0.0:*          
$ nc -z -v 127.0.0.1 6614
Connection to 127.0.0.1 6614 port [tcp/*] succeeded!
```

The backlog of 16 shows as Send-Q. `nc -z` connects although `parleyd` never calls `accept()`. A second
server on the same port fails, with each library's text for `EADDRINUSE`:

```console
$ build/glibc/parleyd -p 6614 </dev/null; echo "exit $?"
parleyd: error: cannot listen on 127.0.0.1:6614: Address already in use
exit 1
$ build/musl/parleyd -p 6614 </dev/null; echo "exit $?"
parleyd: error: cannot listen on 127.0.0.1:6614: Address in use
exit 1
$ exec 3>&-; wait; cat err.txt
parleyd: info: listening on 127.0.0.1:6614
parleyd: info: reading one client session from stdin
parleyd: info: session over
```

Port 0 and IPv6 (your port number will differ):

```console
$ build/musl/parleyd -p 0 </dev/null
parleyd: info: listening on 127.0.0.1:35875
parleyd: info: reading one client session from stdin
parleyd: info: session over
$ build/musl/parleyd -b ::1 -p 6614 </dev/null
parleyd: info: listening on [::1]:6614
parleyd: info: reading one client session from stdin
parleyd: info: session over
```

### glibc vs musl: socket error texts

Errors from getaddrinfo (`gai_strerror`) and from `bind` (`strerror`):

```console
$ build/glibc/parleyd -b no-such-host.invalid </dev/null
parleyd: error: cannot listen on no-such-host.invalid: Name or service not known
$ build/musl/parleyd -b no-such-host.invalid </dev/null
parleyd: error: cannot listen on no-such-host.invalid: Name does not resolve
$ build/glibc/parleyd -b 10.9.9.9 -p 6614 </dev/null
parleyd: error: cannot listen on 10.9.9.9:6614: Cannot assign requested address
$ build/musl/parleyd -b 10.9.9.9 -p 6614 </dev/null
parleyd: error: cannot listen on 10.9.9.9:6614: Address not available
```

`10.9.9.9` is a valid address, but no interface here has it, so `bind` fails with `EADDRNOTAVAIL`.

| errno | glibc `strerror` | musl `strerror` |
|---|---|---|
| `EADDRINUSE` | Address already in use | Address in use |
| `EADDRNOTAVAIL` | Cannot assign requested address | Address not available |
| `ECONNREFUSED` | Connection refused | Connection refused |

musl's `EADDRNOTAVAIL` text is clearer; glibc's "Cannot assign requested address" puzzles most people the
first time. As always: compare `errno`, show the text.

The system calls behind `net_listen()`:

```console
$ strace -e trace=socket,setsockopt,bind,listen,getsockname,close build/musl/parleyd -p 6614 </dev/null 2>&1 | grep -v "^parleyd"
socket(AF_INET, SOCK_STREAM, IPPROTO_TCP) = 3
bind(3, {sa_family=AF_INET, sin_port=htons(6614), sin_addr=inet_addr("127.0.0.1")}, 16) = 0
listen(3, 16)                           = 0
getsockname(3, {sa_family=AF_INET, sin_port=htons(6614), sin_addr=inet_addr("127.0.0.1")}, [128 => 16]) = 0
close(3)                                = 0
+++ exited with 0 +++
```

`[128 => 16]` is the length argument: you passed `sizeof(struct sockaddr_storage)` (128); the kernel wrote
back the size it stored (16, a `sockaddr_in`).

Finally, a static glibc build of the server gets the same warning as `resolve.c`:

```console
$ make -s clean && make -s LDFLAGS=-static build/glibc/parleyd 2>&1 | sed "s/^.*: warning/warning/" | head -2
/usr/bin/ld: build/glibc/net.o: in function `resolve':
warning: Using 'getaddrinfo' in statically linked applications requires at runtime the shared libraries from the glibc version used for linking
$ rm -f in err.txt && make clean >/dev/null
```

`make LIBC=musl` builds a static server with no warning.

The complete project at this point is in `project/ch14/`.

## Summary

- IP addresses name machines (IPv4: 4 bytes, IPv6: 16); ports (16 bits) name programs on them. A TCP
  connection is identified by both addresses and both ports.
- Loopback (`127.0.0.1`, `::1`) is a network inside your machine. `localhost` is a name that must be
  looked up; numeric addresses never are.
- TCP is a reliable, ordered **byte stream** with no message boundaries; UDP sends separate datagrams
  with no guarantees.
- A connection opens with a three-way handshake the kernel completes before `accept()`. Each side closes
  its direction with a FIN (`read` returns 0). The side that closes first stays in TIME_WAIT.
- Ports and addresses in socket structs are in network byte order: `htons`/`ntohs`, `htonl`/`ntohl`.
- Pass `struct sockaddr_in`/`sockaddr_in6` as `struct sockaddr *` with their length; receive unknown
  addresses in `struct sockaddr_storage`.
- `getaddrinfo` turns host and port strings into a list of socket addresses: set `ai_socktype`, use
  `AF_UNSPEC`, try each result, `freeaddrinfo` the list, report errors with `gai_strerror`.
- glibc vs musl: different `gai_strerror` and `strerror` texts; glibc truncates out-of-range ports, musl
  rejects them; glibc asks DNS for an IPv6 `localhost`, musl doesn't; glibc adds a raw result when no
  socket type is given; a static glibc binary still needs glibc's shared NSS libraries at run time for
  getaddrinfo, a static musl binary is self-contained.
- Tools: `ss -tlnp`, `ss -tan`, `nc`, `strace -e trace=network`.

## Exercises

1. **Predict, then check.** Without running anything, what does `./resolve` print on each library for
   `-4 ::1 6667`, `127.0.0.1 http`, and `localhost 65535`? Then run it.

   <details><summary>Solution</summary>

   - `-4 ::1 6667`: `::1` is IPv6 but you asked for IPv4 only, so it fails. glibc:
     `getaddrinfo: error -9: Address family for hostname not supported`; musl:
     `getaddrinfo: error -5: Name has no usable address`. Different **codes** this time, both non-POSIX
     extras: glibc's `EAI_ADDRFAMILY`, musl's `EAI_NODATA`. Portable code just reports
     `gai_strerror(rc)`.
   - `127.0.0.1 http`: `IPv4  TCP 127.0.0.1        port 80    (addrlen 16)` on both.
   - `localhost 65535`: the highest valid port, `port 65535` on both.

   </details>

2. **A port checker.** Write `portcheck HOST PORT...` that prints `open` or `closed (reason)` for each
   port, like `nc -z`. Exit 0 if at least one port is open. Try port 99999 on both libraries: what
   happens, and how would you make both behave the same?

   <details><summary>Solution</summary>

   ```c
   /* portcheck.c - is anything listening on these TCP ports?
    * usage: portcheck HOST PORT... */
   #define _POSIX_C_SOURCE 200809L
   #include <errno.h>
   #include <netdb.h>
   #include <stdio.h>
   #include <string.h>
   #include <sys/socket.h>
   #include <unistd.h>

   int main(int argc, char *argv[])
   {
       if (argc < 3) {
           fprintf(stderr, "usage: portcheck HOST PORT...\n");
           return 2;
       }
       int open_ports = 0;
       for (int i = 2; i < argc; i++) {
           struct addrinfo hints, *list;
           memset(&hints, 0, sizeof hints);
           hints.ai_family = AF_UNSPEC;
           hints.ai_socktype = SOCK_STREAM;
           hints.ai_flags = AI_NUMERICSERV;    /* ports as numbers only */
           int rc = getaddrinfo(argv[1], argv[i], &hints, &list);
           if (rc != 0) {
               printf("%s: %s\n", argv[i], gai_strerror(rc));
               continue;
           }
           int fd = socket(list->ai_family, list->ai_socktype, list->ai_protocol);
           if (fd >= 0 && connect(fd, list->ai_addr, list->ai_addrlen) == 0) {
               printf("%s: open\n", argv[i]);
               open_ports++;
           } else {
               printf("%s: closed (%s)\n", argv[i], strerror(errno));
           }
           if (fd >= 0)
               close(fd);
           freeaddrinfo(list);
       }
       return open_ports > 0 ? 0 : 1;
   }
   ```

   ```console
   $ nc -l 127.0.0.1 6836 &
   $ ./portcheck localhost 6836 6837 99999 abc
   6836: open
   6837: closed (Connection refused)
   99999: closed (Connection refused)
   abc: Name or service not known
   ```

   With musl, the last two lines are `99999: Unrecognized service` and `abc: Name does not resolve`. glibc
   quietly checked port 34463 instead. To make both agree, check each port with `strtol` first (whole
   string used, 1–65535) and skip invalid ones with your own message. (The solution tries only the first
   address; looping over all, as `fetch` does, is better.)

   </details>

3. **UDP.** Write `udp-send HOST PORT MESSAGE` that sends one UDP datagram with `sendto()`; test it with
   `nc -u -l 127.0.0.1 6834`. What happens if nothing listens on the port?

   <details><summary>Solution</summary>

   ```c
   /* udp-send.c - send one UDP datagram.
    * usage: udp-send HOST PORT MESSAGE */
   #define _POSIX_C_SOURCE 200809L
   #include <errno.h>
   #include <netdb.h>
   #include <stdio.h>
   #include <string.h>
   #include <sys/socket.h>
   #include <unistd.h>

   int main(int argc, char *argv[])
   {
       if (argc != 4) {
           fprintf(stderr, "usage: udp-send HOST PORT MESSAGE\n");
           return 2;
       }
       struct addrinfo hints, *list;
       memset(&hints, 0, sizeof hints);
       hints.ai_family = AF_UNSPEC;
       hints.ai_socktype = SOCK_DGRAM;         /* UDP */
       int rc = getaddrinfo(argv[1], argv[2], &hints, &list);
       if (rc != 0) {
           fprintf(stderr, "udp-send: %s: %s\n", argv[1], gai_strerror(rc));
           return 1;
       }
       int fd = socket(list->ai_family, list->ai_socktype, list->ai_protocol);
       if (fd < 0) {
           perror("socket");
           freeaddrinfo(list);
           return 1;
       }
       /* No connect(), no handshake: every datagram carries its address. */
       ssize_t n = sendto(fd, argv[3], strlen(argv[3]), 0, list->ai_addr, list->ai_addrlen);
       if (n < 0)
           fprintf(stderr, "udp-send: sendto: %s\n", strerror(errno));
       else
           printf("sent %d bytes\n", (int)n);
       freeaddrinfo(list);
       close(fd);
       return n < 0 ? 1 : 0;
   }
   ```

   ```console
   $ nc -u -l 127.0.0.1 6834 &
   $ ./udp-send 127.0.0.1 6834 'hello over UDP'
   sent 14 bytes
   hello over UDP
   $ ./udp-send 127.0.0.1 6835 'nobody listens'
   sent 14 bytes
   ```

   (`nc` prints `hello over UDP`.) With nobody listening, `sendto` still "succeeds": there's no
   connection, so the sender can't know the datagram was dropped. A TCP `connect` would fail with
   "Connection refused". Same on both libraries.

   </details>

4. **Answer without notes.**
   - Why does a socket API need `htons`, and what goes wrong without it?
   - Why use `struct sockaddr_storage` for the address `accept()` or `getsockname()` returns?
   - What is TIME_WAIT for, and which side of a connection gets it?
   - Your program connects to `localhost` and is slow to start on one machine only. What may be happening?

   <details><summary>Solution</summary>

   - The network uses big-endian byte order; x86 is little-endian. Without `htons(6667)` the bytes are
     swapped and the program binds or connects to port 2842.
   - Before the call you don't know whether an IPv4 (16 bytes) or IPv6 (28 bytes) address comes back.
     `sockaddr_storage` is big enough and correctly aligned for any family.
   - The side that closed first remembers the connection (60 s on Linux), so late packets can't be
     mistaken for part of a new connection on the same ports. The side that sends the first FIN gets it.
   - Name lookup: e.g. glibc asking DNS for an IPv6 `localhost` that `/etc/hosts` doesn't list, once per
     search domain. Use `127.0.0.1` or `::1`, or `AF_UNSPEC` so the IPv4 entry is enough.

   </details>

Sources for this chapter: [REFERENCES.md](REFERENCES.md#chapter-14--networking-basics)
