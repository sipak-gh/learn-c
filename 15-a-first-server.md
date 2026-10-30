# Chapter 15 — A First Server

[Chapter 14](14-networking-basics.md) gave `parleyd` a listening socket. Clients could connect, but
nobody answered: the connections just waited in the kernel's queue. This chapter adds the missing piece,
`accept()`. You'll write an echo server, watch the backlog at work, see why a restarted server can fail
with "Address already in use" and how `SO_REUSEADDR` fixes it, and learn the most important fact about
TCP: it delivers a **stream of bytes, not messages**.

At the end, `parleyd` becomes **server v1**: it serves one client at a time over TCP, using the protocol
from earlier chapters, and you'll talk to it by hand with `nc`. It can't serve two clients at once yet;
[Chapter 17](17-threads.md) removes that limit.

Build every example with both C libraries:

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o echo_server echo_server.c
$ musl-gcc -std=c99 -Wall -Wextra -pedantic -static -o echo_server-musl echo_server.c
```

The examples use ports 6834–6839 on `127.0.0.1`, so nothing is reachable from outside your machine.

## The four calls of a server

A client needs two calls: `socket()` and `connect()`. A server needs four:

| Call | What it does | Typical failure |
|---|---|---|
| `socket(family, SOCK_STREAM, 0)` | makes an unconnected TCP socket and returns a file descriptor | `EMFILE`: too many open files |
| `bind(fd, addr, len)` | gives the socket a local address and port | `EADDRINUSE`: the port is taken; `EACCES`: port below 1024 |
| `listen(fd, backlog)` | marks it as a **listening socket**: the kernel now answers handshakes on that port | rarely fails |
| `accept(fd, addr, &len)` | waits for a finished connection and returns a **new** file descriptor for it | `EINTR`: a signal arrived; `EMFILE` |

The key idea: a server has **two kinds of socket**.

```
                       listening socket (fd 3)
  client A ──connect──►  127.0.0.1:6834   ──accept()──► connected socket (fd 4) ◄──► client A
  client B ──connect──►  (never carries   ──accept()──► connected socket (fd 5) ◄──► client B
                          any data)
```

- The **listening socket** only collects new connections. You never `read()` or `write()` it.
- Each `accept()` returns a **connected socket** for one client. You `read()` what that client sends and
  `write()` replies to it, as with files in [Chapter 12](12-files-and-descriptors.md), and `close()` it
  when you're done.
- The kernel tells connected sockets apart by the four-part name from Chapter 14 (local address and port,
  peer address and port). All of them share the local port 6834.

`accept()` can also tell you who connected. Pass a `struct sockaddr_storage` (big enough for any address
family) and its size; `accept()` fills it in and sets the size to the length it used. Pass `NULL, NULL`
if you don't care.

## An echo server

An **echo server** sends back every byte it receives: the "hello, world" of network servers, and all you
need to see the four calls working.

`examples/ch15/echo_server.c`:

```c
/* echo_server.c - a blocking TCP echo server, one client at a time.
 * usage: echo_server [-n] PORT
 *   -n  don't set SO_REUSEADDR (to see why servers set it) */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

/* Write all n bytes, however many write() calls that takes. */
static int write_all(int fd, const char *p, size_t n)
{
    while (n > 0) {
        ssize_t w = write(fd, p, n);
        if (w < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        p += w;
        n -= (size_t)w;
    }
    return 0;
}

static int listen_on(const char *port, int reuse)
{
    struct addrinfo hints, *list;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE | AI_NUMERICSERV;
    int rc = getaddrinfo("127.0.0.1", port, &hints, &list);
    if (rc != 0) {
        fprintf(stderr, "echo_server: %s\n", gai_strerror(rc));
        return -1;
    }
    int fd = socket(list->ai_family, list->ai_socktype, list->ai_protocol);
    if (fd < 0) {
        perror("socket");
        freeaddrinfo(list);
        return -1;
    }
    int on = 1;
    if (reuse && setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof on) < 0)
        perror("setsockopt");
    if (bind(fd, list->ai_addr, list->ai_addrlen) < 0 || listen(fd, 8) < 0) {
        fprintf(stderr, "echo_server: port %s: %s\n", port, strerror(errno));
        close(fd);
        fd = -1;
    }
    freeaddrinfo(list);
    return fd;
}

int main(int argc, char *argv[])
{
    int reuse = !(argc == 3 && strcmp(argv[1], "-n") == 0);
    if (argc != 2 && reuse) {
        fprintf(stderr, "usage: echo_server [-n] PORT\n");
        return 2;
    }
    int lfd = listen_on(argv[argc - 1], reuse);
    if (lfd < 0)
        return 1;
    printf("echo_server: listening on 127.0.0.1:%s\n", argv[argc - 1]);
    fflush(stdout);

    for (;;) {
        struct sockaddr_storage ss;
        socklen_t len = sizeof ss;
        int cfd = accept(lfd, (struct sockaddr *)&ss, &len);  /* waits */
        if (cfd < 0) {
            perror("accept");
            continue;
        }
        char host[64], serv[8];
        getnameinfo((struct sockaddr *)&ss, len, host, sizeof host, serv, sizeof serv,
                    NI_NUMERICHOST | NI_NUMERICSERV);
        printf("client %s:%s connected\n", host, serv);
        fflush(stdout);

        char buf[512];
        ssize_t n;
        while ((n = read(cfd, buf, sizeof buf)) > 0)  /* until the client closes */
            if (write_all(cfd, buf, (size_t)n) < 0)
                break;
        printf("client %s:%s %s\n", host, serv, n == 0 ? "disconnected" : strerror(errno));
        fflush(stdout);
        close(cfd);
    }
}
```

Walk through it:

- **`getaddrinfo` with `AI_PASSIVE`** asks for an address to listen on, not one to connect to.
  `"127.0.0.1"` keeps the server on loopback. `AI_NUMERICSERV` says the port is a number, so there's no
  `/etc/services` lookup; with a numeric address too, no name lookup happens at all.
- **`setsockopt(..., SO_REUSEADDR, ...)`** must come **before** `bind()`. You'll see why
  [below](#restarting-a-server-time_wait-and-so_reuseaddr).
- **`listen(fd, 8)`**: up to 8 finished connections may wait for `accept()`.
- **`accept()` blocks** until a client has connected, then returns a new descriptor, `cfd`.
- **`getnameinfo()`** is the reverse of `getaddrinfo()`: it turns a socket address into text. The
  `NI_NUMERIC*` flags ask for numbers, so it never asks DNS.
- **The inner loop** reads whatever has arrived, up to 512 bytes, and writes it all back. `read()` returns
  **0** when the client has closed its side: end of file, as with files.
- `write()` on a socket may write **less than you asked**, as on a pipe, so `write_all()` loops. On
  loopback with small messages it rarely happens, but a server that ignores it loses data under load.
- **`close(cfd)`** ends this connection. The listening socket stays open, and the loop goes back to
  `accept()` for the next client.

Run it in the background with its output going to a file, and send it two lines with `nc`. `-N` makes `nc`
close its sending side at the end of its input, so the server sees end of file:

```console
$ ./echo_server 6834 > server.log &
$ printf 'hello\nworld\n' | nc -N 127.0.0.1 6834
hello
world
$ printf 'again\n' | nc -N localhost 6834
again
$ kill %1
$ cat server.log
echo_server: listening on 127.0.0.1:6834
client 127.0.0.1:35894 connected
client 127.0.0.1:35894 disconnected
client 127.0.0.1:35898 connected
client 127.0.0.1:35898 disconnected
```

Notice:

- Each client gets a new **ephemeral port** (35894, 35898) from its kernel. Yours will differ.
- `nc localhost` worked too: `nc` resolved `localhost` and tried its addresses until one answered. Our
  server only listens on IPv4, so `::1` would have been refused.
- Run `nc -N 127.0.0.1 6834` without redirection to type lines yourself. Ctrl-D ends your side (that
  needs `-N`); Ctrl-C kills `nc`. The server sees end of file either way, because the kernel closes the
  sockets of a process that dies.

The musl build prints the same.

### What the system calls look like

`strace` shows each step, in order. One client sends `hi`, then a `kill` arrives while the server waits in
`accept()`:

```console
$ strace -e trace=socket,setsockopt,bind,listen,accept,read,write,close ./echo_server 6834 >/dev/null
socket(AF_INET, SOCK_STREAM, IPPROTO_TCP) = 3
setsockopt(3, SOL_SOCKET, SO_REUSEADDR, [1], 4) = 0
bind(3, {sa_family=AF_INET, sin_port=htons(6834), sin_addr=inet_addr("127.0.0.1")}, 16) = 0
listen(3, 8)                            = 0
write(1, "echo_server: listening on 127.0."..., 41) = 41
accept(3, {sa_family=AF_INET, sin_port=htons(39696), sin_addr=inet_addr("127.0.0.1")}, [128 => 16]) = 4
write(1, "client 127.0.0.1:39696 connected"..., 33) = 33
read(4, "hi\n", 512)                    = 3
write(4, "hi\n", 3)                     = 3
read(4, "", 512)                        = 0
write(1, "client 127.0.0.1:39696 disconnec"..., 36) = 36
close(4)                                = 0
accept(3, 0x7ffffbc1f7e0, [128])        = ? ERESTARTSYS (To be restarted if SA_RESTART is set)
--- SIGTERM {si_signo=SIGTERM, si_code=SI_USER, si_pid=302564, si_uid=1000} ---
+++ killed by SIGTERM +++
```

(The `read` and `close` lines of the dynamic loader loading `libc.so` come before `socket`; they are
left out.)

- `[128 => 16]` is the address length: you passed 128, the size of `sockaddr_storage`, and `accept()`
  used 16, the size of a `sockaddr_in`.
- fd 3 is the listening socket, fd 4 the client's. The reads and writes on fd 4 are the conversation.
- `read(4, "", 512) = 0` is the client's FIN: end of file.

The musl build makes the same socket calls; it only writes stdout with `writev(1, ...)` instead of
`write(1, ...)`, as in [Chapter 12](12-files-and-descriptors.md).

## One client at a time: the backlog

The echo server is **blocking** and **iterative**: while it's in the inner `read()` loop with one client,
it isn't calling `accept()`. What happens to a second client? Here client one stays connected for 2
seconds, while clients two and three connect and send their lines at once:

```console
$ ./echo_server 6834 > server.log &
$ { printf 'one\n'; sleep 2; } | nc -N 127.0.0.1 6834 > c1.out &
$ printf 'two\n' | nc -N 127.0.0.1 6834 > c2.out &
$ printf 'three\n' | nc -N 127.0.0.1 6834 > c3.out &
$ ss -tln "( sport = :6834 )"
State  Recv-Q Send-Q Local Address:Port Peer Address:PortProcess
LISTEN 2      8          127.0.0.1:6834      0.0.0.0:*          
$ ss -tn "( sport = :6834 )"
State      Recv-Q Send-Q Local Address:Port Peer Address:Port Process
CLOSE-WAIT 7      0          127.0.0.1:6834    127.0.0.1:51136       
ESTAB      0      0          127.0.0.1:6834    127.0.0.1:51108       
CLOSE-WAIT 5      0          127.0.0.1:6834    127.0.0.1:51124       
$ wc -c < c2.out
0
```

The `ss` output tells the whole story:

- On a `LISTEN` line, **Recv-Q is the number of connections waiting for `accept()`** (2), and Send-Q is
  the backlog limit (8).
- Clients two and three are fully connected: the **kernel** did the handshake, so their `connect()`
  returned long ago. They have even sent their data and closed their side, hence `CLOSE-WAIT` ("the peer
  has closed, waiting for this program to close too").
- Their bytes wait in the kernel's receive buffers: Recv-Q 5 is `two\n` (4 bytes), Recv-Q 7 is `three\n`
  (6 bytes). The extra 1 is the FIN, which takes up one TCP sequence number.
- `c2.out` is empty: nobody has read client two's line, so nobody has echoed it.

When client one hangs up, the server accepts the others in order and echoes their waiting lines:

```console
$ wait %2 %3 %4; kill %1; cat server.log; cat c1.out c2.out c3.out
echo_server: listening on 127.0.0.1:6834
client 127.0.0.1:51108 connected
client 127.0.0.1:51108 disconnected
client 127.0.0.1:51124 connected
client 127.0.0.1:51124 disconnected
client 127.0.0.1:51136 connected
client 127.0.0.1:51136 disconnected
one
two
three
```

So the **backlog** is the kernel's queue of finished but not yet accepted connections. A slow server
doesn't refuse clients; it makes them wait. When the queue is full, Linux **ignores** new connection
requests (SYN packets), and the client's kernel resends after 1 second, then 2, then 4, and so on. A full
backlog makes clients slow, not failed. Exercise 3 measures it.

What should you pass to `listen()`? The value is only a hint: the kernel caps it at the system limit in
`/proc/sys/net/core/somaxconn`. A small number like 16 is plenty for parley. Servers that want "as many as
the system allows" pass the constant `SOMAXCONN` from `<sys/socket.h>`, and that's where the C libraries
differ.

### glibc vs musl: SOMAXCONN

`examples/ch15/somaxconn.c`:

```c
/* somaxconn.c - the C library's idea of the biggest listen() backlog.
 * usage: somaxconn [PORT]   (with a port: listen(fd, SOMAXCONN) for 1 s) */
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
    printf("SOMAXCONN = %d\n", SOMAXCONN);
    if (argc < 2)
        return 0;

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_port = htons((uint16_t)atoi(argv[1]));
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (fd < 0 || bind(fd, (struct sockaddr *)&a, sizeof a) < 0
            || listen(fd, SOMAXCONN) < 0) {
        perror("somaxconn");
        return 1;
    }
    sleep(1);                           /* time to look with ss */
    return 0;
}
```

```console
$ cat /proc/sys/net/core/somaxconn
4096
$ ./somaxconn 6835 & sleep 0.3; ss -tln "( sport = :6835 )"
SOMAXCONN = 4096
State  Recv-Q Send-Q Local Address:Port Peer Address:PortProcess
LISTEN 0      4096       127.0.0.1:6835      0.0.0.0:*          
$ ./somaxconn-musl 6835 & sleep 0.3; ss -tln "( sport = :6835 )"
SOMAXCONN = 128
State  Recv-Q Send-Q Local Address:Port Peer Address:PortProcess
LISTEN 0      128        127.0.0.1:6835      0.0.0.0:*          
```

- **glibc** defines `SOMAXCONN` as 4096 since version 2.31, matching the kernel default since Linux 5.4.
  Older glibc said 128.
- **musl** defines it as 128, the old kernel default: the same source line gives a backlog 32 times
  smaller.
- Either way, the kernel never allows more than `net.core.somaxconn`; a larger number is quietly reduced.

**Which is better?** Neither: the constant is a compile-time guess about a runtime setting, so don't rely
on it. Choose the backlog your server needs, as parley does with `LISTEN_BACKLOG 16`. If you really want
the system maximum, pass a large number and let the kernel cap it; then both libraries behave the same.

## Restarting a server: TIME_WAIT and SO_REUSEADDR

Every server author meets this one. Stop a server while a client is connected, start it again at once,
and it fails:

```console
$ ./echo_server -n 6836 &
echo_server: listening on 127.0.0.1:6836
$ { printf 'hi\n'; sleep 1; } | nc 127.0.0.1 6836 &
client 127.0.0.1:47032 connected
hi
$ kill %1                               # stop the server while the client is connected
$ ss -tan "( sport = :6836 )"
State     Recv-Q Send-Q Local Address:Port Peer Address:Port Process
TIME-WAIT 0      0          127.0.0.1:6836    127.0.0.1:47032       
$ ./echo_server -n 6836
echo_server: port 6836: Address already in use
```

Nothing is listening on port 6836, yet `bind()` fails with `EADDRINUSE`. Here's why:

1. When the server died, the kernel closed its sockets. The server sent the first FIN, so its side of the
   connection, `127.0.0.1:6836 ↔ 127.0.0.1:47032`, went into **TIME_WAIT**
   ([Chapter 14](14-networking-basics.md)).
2. TIME_WAIT lasts 60 seconds on Linux, so late packets of the old connection can't land in a new one
   with the same four-part name.
3. By default, `bind()` refuses a port while **any** socket still uses it as its local port, even one in
   TIME_WAIT.

The fix is the socket option `SO_REUSEADDR`, set between `socket()` and `bind()`:

```c
int on = 1;
setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof on);
```

It tells `bind()`: "connections in TIME_WAIT on this port don't count". A **listening** socket on the port
still counts, so you can't start two servers on one port by accident. A Linux detail that's easy to miss:
the option must be set on **both** the old socket and the new one. The same test, with and without `-n`
for the first server:

```console
$ ./echo_server -n 6836 &                # first server WITHOUT SO_REUSEADDR
... client connects, server is killed, TIME_WAIT as above ...
$ ./echo_server -n 6836
echo_server: port 6836: Address already in use
$ ./echo_server 6836
echo_server: port 6836: Address already in use

$ ./echo_server 6838 &                   # first server WITH SO_REUSEADDR
... client connects, server is killed, TIME_WAIT as above ...
$ ./echo_server -n 6838
echo_server: port 6838: Address already in use
$ ./echo_server 6838
echo_server: listening on 127.0.0.1:6838
```

So **every server should always set `SO_REUSEADDR` on its listening socket**. It costs nothing and is
safe: TIME_WAIT still protects the old connection.

Two more things about TIME_WAIT:

- **Whoever closes first gets it.** If the client closes first (as `nc -N` does when its input ends), the
  TIME_WAIT is on the client's side and the server can restart freely; that's why the first echo demo
  left none on the server's port. In parley the server closes first after a `QUIT`, so it collects
  TIME_WAITs.
- It's harmless: a little kernel memory, no file descriptor, and it goes away by itself.

### glibc vs musl: "Address already in use"

The error (`EADDRINUSE`) and the kernel's behaviour are the same. Only the `strerror()` text differs:

```console
$ ./echo_server -n 6836
echo_server: port 6836: Address already in use
$ ./echo_server-musl -n 6837
echo_server: port 6837: Address in use
```

**Which is better?** It doesn't matter much, but it's why programs and tests must never compare error
**texts**. In code, compare `errno` with `EADDRINUSE`; in tests, check the part of the message your own
program wrote ("cannot listen on").

## TCP is a byte stream

The most important fact in network programming, and the source of the most bugs:

> TCP delivers the bytes you sent, in order, with nothing lost or duplicated. It does **not** keep the
> boundaries between your `write()` calls. One `read()` may return part of a write, several writes, or a
> bit of both.

The next program accepts one connection and shows what each `read()` returned, with `\r` and `\n` made
visible.

`examples/ch15/chunks.c`:

```c
/* chunks.c - accept one connection and show what each read() returns.
 * usage: chunks [-q] PORT     (-q: only count the reads) */
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
    int quiet = argc == 3 && strcmp(argv[1], "-q") == 0;
    if (argc != 2 && !quiet) {
        fprintf(stderr, "usage: chunks [-q] PORT\n");
        return 2;
    }
    int lfd = socket(AF_INET, SOCK_STREAM, 0);
    int on = 1;
    setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof on);
    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_port = htons((uint16_t)atoi(argv[argc - 1]));
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (lfd < 0 || bind(lfd, (struct sockaddr *)&a, sizeof a) < 0 || listen(lfd, 1) < 0) {
        perror("chunks");
        return 1;
    }
    int fd = accept(lfd, NULL, NULL);   /* NULL: we don't want the address */
    close(lfd);                         /* one connection is all we take */
    if (fd < 0) {
        perror("accept");
        return 1;
    }

    static char buf[1 << 20];           /* 1 MiB: room for big reads */
    ssize_t n;
    long reads = 0, total = 0, smallest = -1, largest = 0;
    while ((n = read(fd, buf, sizeof buf)) > 0) {
        reads++;
        total += n;
        if (smallest < 0 || n < smallest)
            smallest = n;
        if (n > largest)
            largest = n;
        if (quiet)
            continue;
        printf("read %2d bytes: [", (int)n);
        for (ssize_t i = 0; i < n; i++) {   /* make \r and \n visible */
            if (buf[i] == '\r')
                printf("\\r");
            else if (buf[i] == '\n')
                printf("\\n");
            else
                putchar(buf[i]);
        }
        printf("]\n");
    }
    printf("%ld reads, %ld bytes", reads, total);
    if (reads > 0)
        printf(" (smallest %ld, largest %ld)", smallest, largest);
    printf("\n");
    close(fd);
    return 0;
}
```

Send three protocol lines in odd pieces, with short pauses between them:

```console
$ ./chunks 6835 &
$ { printf 'NICK '; sleep 0.1; printf 'bob\r\nMSG :hi\r\n'; sleep 0.1; printf 'MSG :how are'; sleep 0.1; printf ' you?\r\n'; } | nc -N 127.0.0.1 6835
read  5 bytes: [NICK ]
read 14 bytes: [bob\r\nMSG :hi\r\n]
read 12 bytes: [MSG :how are]
read  7 bytes: [ you?\r\n]
4 reads, 38 bytes (smallest 5, largest 14)
```

The same bytes without the pauses:

```console
$ ./chunks 6835 &
$ { printf 'NICK '; printf 'bob\r\nMSG :hi\r\n'; printf 'MSG :how are'; printf ' you?\r\n'; } | nc -N 127.0.0.1 6835
read 38 bytes: [NICK bob\r\nMSG :hi\r\nMSG :how are you?\r\n]
1 reads, 38 bytes (smallest 38, largest 38)
```

Same data, different pieces: neither a line nor a `write()` survives the trip as a unit. Large transfers
show it even more. Three runs sending 10 MB of zero bytes, with `chunks` reading into a 1 MiB buffer:

```console
$ ./chunks -q 6835 & head -c 10000000 /dev/zero | nc -N 127.0.0.1 6835
243 reads, 10000000 bytes (smallest 53, largest 1048576)
$ ./chunks -q 6835 & head -c 10000000 /dev/zero | nc -N 127.0.0.1 6835
274 reads, 10000000 bytes (smallest 53, largest 699840)
$ ./chunks -q 6835 & head -c 10000000 /dev/zero | nc -N 127.0.0.1 6835
300 reads, 10000000 bytes (smallest 53, largest 1048576)
```

Every run splits the data differently (your counts will differ too). It depends on timing: how fast `nc`
writes, when the scheduler runs `chunks`, how much the kernel has buffered. The musl build behaves the
same, because the splitting happens in the kernel. The only guarantees are the order and the total.

### Framing: finding the messages in the stream

So a program that reads a socket must find the message boundaries itself: **framing**. The common ways:

| Framing | How the receiver finds the end | Used by |
|---|---|---|
| **Delimiter** | a special byte sequence ends each message, e.g. `\r\n` | IRC, SMTP, HTTP headers, **parley** |
| **Length prefix** | each message starts with its length | HTTP bodies (`Content-Length`), TLS, most binary protocols |
| **Fixed size** | every message has the same size | some hardware and game protocols |
| **One per connection** | end of file ends the message | old HTTP/1.0, `nc` file transfers |

Line framing always works the same way:

1. Append whatever `read()` returned to a buffer.
2. While the buffer contains a `\n`, take out the line up to it and handle it.
3. Keep the leftover bytes (an unfinished line); the next `read()` adds the rest.
4. Limit the line length, so a client that never sends `\n` can't make the buffer grow forever.

parley has done this since [Chapter 8](08-memory.md): `msg_take_line()` (called `take_line` until
[Chapter 9](09-preprocessor-and-building.md)) takes complete lines out of a `struct strbuf`, and
`read_into()` from [Chapter 12](12-files-and-descriptors.md) appends one `read()` to it. The bytes came
from stdin then, but a unit test in [Chapter 11](11-testing.md) fed the framer one byte at a time. Now
that pays off: the same code works on a socket unchanged.

The echo server needed no framing because it treats the data as plain bytes. A chat server must
understand `NICK bob` as a command, so it has to wait for the whole line.

## The parley protocol, in brief

You've used the protocol on stdin and stdout since Chapter 6; now it runs over TCP. A reminder
([Appendix C](appendix-c-parley-protocol.md) has the full specification):

- Each message is one line of at most 512 bytes, ending in `\r\n`. The server also accepts a bare `\n`.
- Shape: `[:<prefix> ]<COMMAND>[ <params>...][ :<trailing>]`. Commands are not case-sensitive.

| Client sends | Server replies |
|---|---|
| `NICK alice` | `:parley 001 alice :Welcome to parley, alice`, then `:alice JOIN` and the names list |
| `MSG :hello` | `:alice MSG :hello` (to everyone in the room) |
| `NAMES` | `:parley 353 alice :alice bob` and `:parley 366 alice :End of NAMES` |
| `PING :12345` | `:parley PONG :12345` |
| `QUIT :bye` | `ERROR :Goodbye`, then the server closes the connection |
| anything wrong | a numeric error like `:parley 421 ...` (unknown command) or `417` (line too long) |

The **prefix** says who a message is from, `parley` (the server) or a nick. That's how a client tells
`:alice MSG :hi` (alice spoke) from `:parley 001 ...` (the server spoke).

## Testing a server with nc

`nc` (netcat) is your universal test client. This book uses the OpenBSD `nc`, Debian's and Ubuntu's
default. The options you need for a line protocol:

| Option | Meaning | Why you want it |
|---|---|---|
| `-C` | send `\r\n` instead of `\n` at each line end | protocols like IRC and parley expect `\r\n` |
| `-N` | close the sending side (FIN) at end of input | the server sees end of file and you get the rest of its replies |
| `-z` | just connect, send nothing | "is anything listening?" |
| `-v` | say what's happening | "Connection to ... succeeded!" |

Ways to drive it:

- **By hand:** `nc -C -N 127.0.0.1 6667`, then type lines; Ctrl-D closes your side. Without `-N`, the
  OpenBSD `nc` sends no FIN at end of input, so the server never sees end of file.
- **From a here-document:** `nc -C -N 127.0.0.1 6667 <<'END'` and a few lines, for scripts and tests.
- **Slowly, in pieces:** `{ printf 'NI'; sleep 0.2; printf 'CK bob\r\n'; } | nc -N ...` tests your
  framing.
- **Held open from a script:** connect `nc` to a named pipe (`mkfifo in; nc ... <in &`) and write to the
  pipe whenever you like. The connection stays open until you close the pipe. The checkpoint uses this
  to keep alice connected while bob tries to join.

Other `nc` programs (GNU netcat, nmap's `ncat`, BusyBox `nc`) spell `-C` and `-N` differently. If yours
doesn't understand them, install the OpenBSD one (`netcat-openbsd` on Debian and Ubuntu).

## Building parley: server v1

Until now `parleyd` read one client session from stdin ([Chapter 6](06-strings.md)) and opened a
listening socket it never used ([Chapter 14](14-networking-basics.md)). Version 1 connects the two:

- an `accept()` loop, `serve()`, that takes one client at a time;
- `serve_client(fd, addr)`, the old stdin session, now reading the client's socket;
- `SO_REUSEADDR` in `net_listen()`;
- SIGINT and SIGTERM, set up in [Chapter 13](13-processes-and-signals.md), stop the server cleanly.

The change is small (+56 / −38 lines): the protocol code (`room.c`, `msg.c`) and the I/O helpers (`io.c`)
don't care where the bytes come from. A file descriptor is a file descriptor.

### conn.h: the client's address

A connection now remembers its address, for log messages. `NET_ADDR_MAX` (64) comes from `net.h`, so
`conn.h` includes it:

```c
#include "msg.h"
#include "net.h"
#include "nick.h"
#include "strbuf.h"
```

```c
struct conn {
    int fd;                    /* the client's socket */
    char addr[NET_ADDR_MAX];   /* the client's address, for log messages */
    char nick[NICK_MAX + 1];   /* "" until the client registers */
    enum conn_state state;
    struct strbuf in;          /* bytes received but not yet handled */
    bool skipping;             /* throwing away a too-long line */
};
```

`conn_send()` already writes replies to `c->fd` with `write_all()`; that was 1 (stdout), now it's the
socket. `room.c` doesn't change at all.

### net.c: SO_REUSEADDR

In `net_listen()`'s loop over the `getaddrinfo` results, set the option before `bind()`:

```c
        /* Allow bind() even while old connections on this port are in
         * TIME_WAIT, so a restarted server can listen again at once. */
        int on = 1;
        if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof on) == 0
                && bind(fd, ai->ai_addr, ai->ai_addrlen) == 0
                && listen(fd, LISTEN_BACKLOG) == 0)
            break;                          /* success */
        snprintf(err, errsize, "%s: %s", where, strerror(errno));
```

The `&&` chain stops at the first call that fails, so `errno` belongs to that call and the message names
the right problem.

### parleyd.c: serve_client and serve

The header comment now says what this version is:

```c
/* parleyd.c - the parley chat server: options and the main loop.
 * Version 1: a blocking server that talks to one client at a time. */
```

`serve_stdin()` becomes `serve_client()`. It takes the socket and the address text, and closes the
socket when the client is done:

```c
/* Serve one client on socket fd until it quits or disconnects, or a
 * signal asks us to stop. Closes fd. */
static void serve_client(int fd, const char *addr)
{
    struct conn client = { .fd = fd, .state = CONN_OPEN };
    str_copy(client.addr, addr, sizeof client.addr);
    char line[MSG_LINE_MAX + 1];

    while (client.state == CONN_OPEN) {
        ssize_t n = read_into(fd, &client.in, 4096);
        if (n < 0 && errno == EINTR) {
            if (sig_quit == 0)
                continue;              /* some other signal: just retry */
            conn_send(&client, &(struct msg){
                .command = "ERROR", .trailing = "Server shutting down" });
            break;
        }
        if (n < 0) {
            log_msg(LOG_WARN, "%s: reading: %s", addr, strerror(errno));
            break;
        }
        if (n == 0) {                  /* end of file: the client hung up */
            if (client.nick[0] != '\0')
                log_msg(LOG_INFO, "%s left (Connection closed)", client.nick);
            break;
        }

        enum frame_status st;
        while (client.state == CONN_OPEN
               && (st = msg_take_line(&client.in, &client.skipping, line)) != FRAME_NONE) {
            if (st == FRAME_TOO_LONG)
                conn_numeric(&client, "417", NULL, "Input line too long");
            else
                room_handle_line(&client, line);
        }
    }
    log_msg(LOG_DEBUG, "closing connection from %s", addr);
    sb_free(&client.in);
    close(fd);
}
```

The loop has four outcomes for each `read_into()`:

| `read_into` returned | Meaning | What we do |
|---|---|---|
| `-1`, `errno == EINTR` | a signal interrupted the read | SIGINT/SIGTERM: say `ERROR :Server shutting down` and stop. Any other signal: read again |
| `-1`, other `errno` | a real error, e.g. `ECONNRESET` (the client's kernel aborted the connection) | log a warning, drop the client |
| `0` | end of file: the client closed its side | log it and stop |
| `> 0` | some bytes, maybe several lines, maybe half of one | take out every complete line and handle it |

After `QUIT`, `room_handle_line()` sends `ERROR :Goodbye` and sets `client.state` to `CONN_CLOSING`, so
both loops end. A `QUIT` in the middle of a buffer stops the inner loop at once; the lines after it are
never handled.

Then `serve()`, the accept loop:

```c
/* Accept clients one after the other until SIGINT or SIGTERM. */
static void serve(int listen_fd)
{
    while (sig_quit == 0) {
        struct sockaddr_storage ss;       /* room for any kind of address */
        socklen_t len = sizeof ss;
        int fd = accept(listen_fd, (struct sockaddr *)&ss, &len);
        if (fd < 0) {
            if (errno != EINTR)
                log_msg(LOG_WARN, "accept: %s", strerror(errno));
            continue;                  /* the loop condition checks sig_quit */
        }
        char addr[NET_ADDR_MAX];
        net_addr_str((struct sockaddr *)&ss, len, addr, sizeof addr);
        log_msg(LOG_DEBUG, "connection from %s", addr);
        serve_client(fd, addr);
    }
    log_msg(LOG_INFO, "got signal %d (%s), shutting down",
            (int)sig_quit, strsignal(sig_quit));
}
```

- `net_addr_str()` from Chapter 14 turns the address into `127.0.0.1:51108` (or `[::1]:51108` for IPv6).
- An `accept()` error doesn't stop the server: `EMFILE` (out of file descriptors) or `ECONNABORTED` (the
  client gave up while in the backlog) affect one client, not the server.

**How a signal stops it.** `sig_init()` from [Chapter 13](13-processes-and-signals.md) installs the
SIGINT and SIGTERM handler **without** `SA_RESTART`. On Ctrl-C, the blocked `accept()` or `read()` fails
with `EINTR` instead of quietly restarting; the handler has set `sig_quit`, so the loop ends. With
`SA_RESTART`, the server would only notice the signal when the next client arrived. The `ERESTARTSYS` in
the echo server's `strace` above is the kernel's internal name for "interrupted; restart only if
`SA_RESTART`".

`sig_init()` also ignores SIGPIPE, so if a client disappears and the server writes to its socket, `write()`
returns `-1` with `EPIPE` instead of the signal killing the whole server.

### parleyd.c: main

The end of `main()` gets simpler:

```c
    log_msg(LOG_INFO, "listening on %s (one client at a time)", where);

    serve(listen_fd);
    close(listen_fd);
    return 0;
}
```

A signal is the normal way to stop a server, so `parleyd` exits with 0 after one.

### Checkpoint

The tests still pass; they test the modules, which didn't change:

```console
$ make -s test-all
unit: 28 passed, 0 failed
unit: 28 passed, 0 failed
$ make -s SANITIZE=1 test
unit: 28 passed, 0 failed
$ make -s LIBC=musl
```

Start the server on port 6615 with its log going to a file, and run a whole session from a
here-document (`names` in lower case works because commands are not case-sensitive):

```console
$ build/glibc/parleyd -p 6615 2>server.log & server=$!
$ sleep 0.2
$ nc -C -N 127.0.0.1 6615 <<'END'
NICK alice
MSG :hello, server
PING :12345
names
QUIT :bye
END
:parley 001 alice :Welcome to parley, alice
:alice JOIN
:parley 353 alice :alice
:parley 366 alice :End of NAMES
:alice MSG :hello, server
:parley PONG :12345
:parley 353 alice :alice
:parley 366 alice :End of NAMES
ERROR :Goodbye
```

TCP is a byte stream, so send the lines in odd pieces, with pauses. Even the `\r` and `\n` of one line
end arrive in different reads:

```console
$ { printf 'NI'; sleep 0.2; printf 'CK bob\r\nMSG :one '; sleep 0.2; printf 'line\r'; sleep 0.2; printf '\nQUIT\r\n'; } | nc -N 127.0.0.1 6615
:parley 001 bob :Welcome to parley, bob
:bob JOIN
:parley 353 bob :bob
:parley 366 bob :End of NAMES
:bob MSG :one line
ERROR :Goodbye
```

One client at a time: alice connects through a named pipe and stays connected. bob connects too; the
kernel puts him in the backlog, but nobody reads his lines, so he gets nothing:

```console
$ mkfifo in; nc -C 127.0.0.1 6615 <in >alice.txt & alice=$!; exec 3>in
$ printf 'NICK alice\n' >&3; sleep 0.3
$ printf 'NICK bob\nQUIT\n' | nc -C -N 127.0.0.1 6615 >bob.txt & bob=$!; sleep 0.5
$ wc -c <bob.txt
0
```

When alice quits, the server accepts bob and handles his lines, which waited in the kernel all along:

```console
$ printf 'QUIT\n' >&3; exec 3>&-; wait $alice $bob; cat alice.txt; echo ---; cat bob.txt
:parley 001 alice :Welcome to parley, alice
:alice JOIN
:parley 353 alice :alice
:parley 366 alice :End of NAMES
ERROR :Goodbye
---
:parley 001 bob :Welcome to parley, bob
:bob JOIN
:parley 353 bob :bob
:parley 366 bob :End of NAMES
ERROR :Goodbye
```

Stop the server with SIGTERM. It exits with 0; the log shows all four sessions:

```console
$ kill -TERM $server; wait $server; echo "exit $?"; cat server.log
exit 0
parleyd: info: listening on 127.0.0.1:6615 (one client at a time)
parleyd: info: alice joined
parleyd: info: alice left (bye)
parleyd: info: bob joined
parleyd: info: bob left (Quit)
parleyd: info: alice joined
parleyd: info: alice left (Quit)
parleyd: info: bob joined
parleyd: info: bob left (Quit)
parleyd: info: got signal 15 (Terminated), shutting down
```

The server closed those connections first (after each `QUIT`), so it holds some in TIME_WAIT:

```console
$ ss -tanH state time-wait "( sport = :6615 )" | wc -l
2
```

You may see 1 or 2, depending on timing: when a client's FIN arrives before the server closes, the client
gets the TIME_WAIT instead. Restarting works anyway, thanks to `SO_REUSEADDR`. Here's the musl build,
stopped with SIGINT (what Ctrl-C sends):

```console
$ build/musl/parleyd -p 6615 2>server.log & server=$!; sleep 0.2
$ kill -INT $server; wait $server; echo "exit $?"; cat server.log
exit 0
parleyd: info: listening on 127.0.0.1:6615 (one client at a time)
parleyd: info: got signal 2 (Interrupt), shutting down
```

**Experiment:** in a copy of the project, take `SO_REUSEADDR` out again and restart while the TIME_WAIT
entries are still there:

```console
$ sed -i 's/setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof on) == 0/on/' src/net.c && grep -n -A1 'int on = 1' src/net.c
55:        int on = 1;
56-        if (on
$ make -s && build/glibc/parleyd -p 6615; echo "exit $?"
parleyd: error: cannot listen on 127.0.0.1:6615: Address already in use
exit 1
```

The musl build fails the same way, with its own text:

```console
$ make -s LIBC=musl && build/musl/parleyd -p 6615; echo "exit $?"
parleyd: error: cannot listen on 127.0.0.1:6615: Address in use
exit 1
```

Clean up:

```console
$ rm -f in alice.txt bob.txt server.log && make clean >/dev/null && ls
Makefile
src
tests
```

The complete project at this point is in `project/ch15/`.

## Summary

- A server calls `socket()`, `setsockopt(SO_REUSEADDR)`, `bind()`, `listen()`, then `accept()` in a loop.
  The **listening socket** only collects connections; each `accept()` returns a new **connected socket**
  for one client.
- The kernel completes handshakes itself and queues the connections in the **backlog** until you
  `accept()` them. Clients in the backlog are connected and can already send data. When the backlog is
  full, new clients are delayed, not refused.
- `ss -tln` shows the backlog (Recv-Q = waiting connections, Send-Q = the limit); `ss -tan` shows every
  connection's state.
- The side that closes first holds the connection in **TIME_WAIT** for 60 s. Without `SO_REUSEADDR`, a
  restarted server then fails with `EADDRINUSE`. On Linux the option must be set on the old and the new
  socket, so always set it.
- TCP is a **byte stream**: reads don't match writes, so programs must do their own **framing**. parley
  uses lines, with `read_into()` + `msg_take_line()`.
- Write with a `write_all()` loop. Read until `read()` returns 0 (end of file) or -1.
- A signal handler installed without `SA_RESTART` makes a blocked `accept()` or `read()` return `EINTR`,
  so a blocking server can shut down cleanly.
- glibc vs musl: `SOMAXCONN` is 4096 vs 128 (choose your own backlog instead); `strerror(EADDRINUSE)` is
  "Address already in use" vs "Address in use". Everything the kernel does (backlog, TIME_WAIT, how data
  is split) is the same on both.
- parley server v1 serves one client at a time; the others wait in the backlog. [Chapter
  16](16-a-first-client.md) writes a client, and [Chapter 17](17-threads.md) serves many clients at once.

## Exercises

1. **A numbering server.** Change the echo server to answer each **line** with its number:
   `1: first line`, `2: second`, and so on, counting from 1 for each client. It must work however the
   lines are split. Test it with `{ printf 'first li'; sleep 0.2; printf 'ne\nsecond\nthi'; sleep 0.2;
   printf 'rd\n'; } | nc -N 127.0.0.1 6836`.

   <details><summary>Solution</summary>

   Keep a buffer of received bytes, answer every complete line, and move the unfinished rest to the
   front: the framing recipe from this chapter.

   ```c
   /* numbered.c - like echo_server, but replies "N: line" for each line.
    * usage: numbered PORT */
   #define _POSIX_C_SOURCE 200809L
   #include <arpa/inet.h>
   #include <netinet/in.h>
   #include <stdio.h>
   #include <stdlib.h>
   #include <string.h>
   #include <sys/socket.h>
   #include <unistd.h>

   static int listen_on(int port)
   {
       int fd = socket(AF_INET, SOCK_STREAM, 0);
       if (fd < 0)
           return -1;
       int on = 1;
       setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof on);
       struct sockaddr_in a;
       memset(&a, 0, sizeof a);
       a.sin_family = AF_INET;
       a.sin_port = htons((uint16_t)port);
       a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
       if (bind(fd, (struct sockaddr *)&a, sizeof a) < 0 || listen(fd, 8) < 0) {
           close(fd);
           return -1;
       }
       return fd;
   }

   static int write_all(int fd, const char *p, size_t n)
   {
       while (n > 0) {
           ssize_t w = write(fd, p, n);
           if (w < 0)
               return -1;
           p += w;
           n -= (size_t)w;
       }
       return 0;
   }

   static void serve(int fd)
   {
       char in[1024];          /* bytes received but not yet answered */
       size_t used = 0;
       long lineno = 0;
       for (;;) {
           ssize_t n = read(fd, in + used, sizeof in - used);
           if (n <= 0)
               return;
           used += (size_t)n;

           char *start = in, *nl;
           while ((nl = memchr(start, '\n', used - (size_t)(start - in))) != NULL) {
               char out[1100];
               int len = snprintf(out, sizeof out, "%ld: %.*s\n", ++lineno,
                                  (int)(nl - start), start);
               if (write_all(fd, out, (size_t)len) < 0)
                   return;
               start = nl + 1;
           }
           used -= (size_t)(start - in);       /* keep the unfinished line */
           memmove(in, start, used);
           if (used == sizeof in)              /* a line longer than the buffer */
               return;
       }
   }

   int main(int argc, char *argv[])
   {
       if (argc != 2) {
           fprintf(stderr, "usage: numbered PORT\n");
           return 2;
       }
       int lfd = listen_on(atoi(argv[1]));
       if (lfd < 0) {
           perror("numbered");
           return 1;
       }
       for (;;) {
           int fd = accept(lfd, NULL, NULL);
           if (fd < 0)
               continue;
           serve(fd);
           close(fd);
       }
   }
   ```

   ```console
   $ ./numbered 6836 &
   $ { printf 'first li'; sleep 0.2; printf 'ne\nsecond\nthi'; sleep 0.2; printf 'rd\n'; } | nc -N 127.0.0.1 6836
   1: first line
   2: second
   3: third
   $ printf 'new client\n' | nc -N 127.0.0.1 6836
   1: new client
   ```

   Same output with musl. Notes:

   - `memmove`, not `memcpy`: the leftover and its new place may overlap.
   - `%.*s` prints exactly `nl - start` bytes, so the line needs no `'\0'`.
   - A line of 1024 bytes or more fills the buffer with no `\n`. This solution drops such a client;
     parley's `msg_take_line()` skips the rest of the line and reports error 417 instead.

   </details>

2. **A deadlock.** Write `blast PORT N`: connect to the echo server, `write()` N bytes,
   `shutdown(fd, SHUT_WR)`, and only then read back the echo and print how much came back. Try N = 100000,
   1000000, 3000000 and 10000000. What happens, and why? Why doesn't `nc` have this problem with 10 MB?

   <details><summary>Solution</summary>

   ```c
   /* blast.c - send N bytes to an echo server, and only then read the echo.
    * usage: blast PORT N */
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
           fprintf(stderr, "usage: blast PORT N\n");
           return 2;
       }
       long total = atol(argv[2]);
       int fd = socket(AF_INET, SOCK_STREAM, 0);
       struct sockaddr_in a;
       memset(&a, 0, sizeof a);
       a.sin_family = AF_INET;
       a.sin_port = htons((uint16_t)atoi(argv[1]));
       a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
       if (fd < 0 || connect(fd, (struct sockaddr *)&a, sizeof a) < 0) {
           perror("blast");
           return 1;
       }
       static char buf[65536];
       long sent = 0, got = 0;
       while (sent < total) {
           size_t want = total - sent < (long)sizeof buf ? (size_t)(total - sent) : sizeof buf;
           ssize_t n = write(fd, buf, want);
           if (n < 0) {
               perror("write");
               return 1;
           }
           sent += n;
           fprintf(stderr, "\rsent %ld", sent);
       }
       fprintf(stderr, "\n");
       shutdown(fd, SHUT_WR);              /* "I'm done sending" */
       ssize_t n;
       while ((n = read(fd, buf, sizeof buf)) > 0)
           got += n;
       printf("sent %ld, got back %ld\n", sent, got);
       return 0;
   }
   ```

   ```console
   $ ./echo_server 6837 >/dev/null &
   $ ./blast 6837 1000000
   sent 1000000
   sent 1000000, got back 1000000
   $ ./blast 6837 3000000
   sent 3000000
   sent 3000000, got back 3000000
   $ ./blast 6837 10000000
   sent 8978432^C
   ```

   With 10 MB it hangs (where it stops varies). Both programs are stuck in `write()`:

   - The echo server writes the echo back. Nobody reads it, so blast's **receive** buffer fills up and the
     server's `write()` blocks.
   - A blocked server stops reading, so blast's data piles up in the server's receive buffer and blast's
     **send** buffer. When those are full, blast's `write()` blocks too.
   - Each waits for the other forever: a **deadlock**. Smaller amounts fit in the kernel buffers (a few MB
     on loopback, tuned automatically), so they work.

   `nc` reads and writes **at the same time**: it waits with `poll()` for whichever of stdin and the
   socket is ready. Any program that sends a lot while the other side also sends must do the same, or use
   threads. [Chapter 16](16-a-first-client.md) runs into this limit, and Chapters
   [18](18-event-loop.md) and [19](19-an-interactive-client.md) solve it with `poll()`. musl and glibc
   hang the same way: the buffers are in the kernel.

   </details>

3. **Fifty at once.** Write a shell script that starts 50 `nc` clients at once against `echo_server`.
   Each sends one line and keeps the connection open for 0.1 s. Predict how long it takes, then time it.
   Use `nstat` to explain the result.

   <details><summary>Solution</summary>

   ```sh
   #!/bin/sh
   # fifty.sh PORT - start 50 clients at once; each one talks for 0.1 s
   i=1
   while [ $i -le 50 ]; do
       { printf 'client %d\n' $i; sleep 0.1; } | nc -N 127.0.0.1 "$1" >/dev/null &
       i=$((i + 1))
   done
   wait
   ```

   ```console
   $ ./echo_server 6838 >/dev/null &
   $ nstat -n; time sh fifty.sh 6838; nstat | grep -i 'listen\|synretrans'

   real	0m3.077s
   user	0m0.117s
   sys	0m0.028s
   TcpExtListenOverflows           93                 0.0
   TcpExtListenDrops               93                 0.0
   TcpExtTCPSynRetrans             60                 0.0
   ```

   You might predict 50 × 0.1 s = 5 s, one client after the other. It's faster than that, but slower than
   0.1 s, and it jumps in whole seconds (2 s, 3 s) from run to run:

   - Clients waiting in the backlog keep running: their 0.1 s passes while they wait. When the server
     finally accepts one, its line and its FIN are already there, so it's served at once.
   - But the backlog of 8 can't hold 50 clients. When it's full, the kernel drops new SYNs
     (`ListenOverflows`, `ListenDrops`), and those clients' kernels resend after 1 s, then 2 s
     (`TCPSynRetrans`). Hence the whole seconds.
   - `nstat` counts for the whole machine, so other programs may add to the numbers.

   With `listen(fd, 64)` in the echo server, `nstat` shows no drops and the script takes about 0.1 s
   (`real 0m0.112s` here). A server that serves clients at the same time (Chapter 17) would also be
   faster. The musl build gives the same kind of result, because the kernel handles the backlog.

   </details>

4. **Questions.**
   1. Why does `accept()` return a new descriptor instead of turning the listening socket into the
      connection?
   2. A client's `connect()` succeeded, but `parleyd` never logged the connection. How can that be?
   3. In `serve_client()`, why is `EINTR` checked before the other errors, and why isn't `read_into()` itself
      retrying it?
   4. You see `CLOSE-WAIT` connections piling up in `ss -tan` for your server. What bug does that point to?
   5. Why is it safe to set `SO_REUSEADDR` always, but not to skip TIME_WAIT altogether?

   <details><summary>Solution</summary>

   1. A server must go on accepting new clients while it talks to old ones. Every connection needs its own
      descriptor, and the listening socket must stay open for the next `accept()`.
   2. The kernel completed the handshake and put the connection in the backlog. `parleyd` v1 is busy with
      another client and hasn't called `accept()` yet.
   3. `EINTR` is not an error; it means a signal arrived, and the caller decides: stop if `sig_quit` is
      set, otherwise read again. If `read_into()` retried by itself, a Ctrl-C would go unnoticed until the
      client sent something.
   4. `CLOSE-WAIT` means the client has closed but your program hasn't closed its socket. You're leaking
      descriptors: some path forgets to `close()`, perhaps after an error or end of file.
   5. `SO_REUSEADDR` only lets `bind()` ignore TIME_WAIT entries on the port. Those entries stay in place
      and still catch the old connections' late packets, and two servers still can't listen on the same
      port. Skipping TIME_WAIT (for example by closing with a reset) would let old packets confuse a new
      connection with the same four-part name.

   </details>

Sources for this chapter: [REFERENCES.md](REFERENCES.md#chapter-15--a-first-server)
