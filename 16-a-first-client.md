# Chapter 16 — A First Client

In [Chapter 15](15-a-first-server.md) you spoke to the server with `nc`. Now you write the other end: a
client. A client makes one connection with `connect()`, then reads and writes like any file descriptor.
But a chat client must listen to the keyboard and the server at once, and blocking I/O can wait for only
one. This chapter shows that limit, then builds **client v1**, which works around it.
[Chapter 17](17-threads.md) gives the server real concurrency; [Chapter 19](19-an-interactive-client.md)
removes the limit from the client.

Build every example twice, once for each C library:

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o connect connect.c
$ musl-gcc -std=c99 -Wall -Wextra -pedantic -static -o connect-musl connect.c
```

## Connecting: socket and connect

A client needs three calls:

| call | what it does |
|---|---|
| `getaddrinfo(host, port, &hints, &res)` | turns a name and a port into a list of addresses ([Chapter 14](14-networking-basics.md)) |
| `socket(family, type, protocol)` | makes an unconnected socket |
| `connect(fd, addr, addrlen)` | connects it to the address; blocks until the server answers or refuses |

A name can have several addresses (IPv4 and IPv6, or many servers), so a client tries each in turn until
one works. The project's `net_connect()` does this. Here is the same loop, made chatty:

`examples/ch16/connect.c`:

```c
/* connect.c - connect to HOST PORT and show both ends of the connection.
 * Try each address the name resolves to, in order, like net_connect(). */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

/* "127.0.0.1:6667" or "[::1]:6667" */
static void addr_str(const struct sockaddr *sa, socklen_t len, char *out, size_t size)
{
    char host[INET6_ADDRSTRLEN], port[8];
    if (getnameinfo(sa, len, host, sizeof host, port, sizeof port,
                    NI_NUMERICHOST | NI_NUMERICSERV) != 0) {
        snprintf(out, size, "?");
        return;
    }
    snprintf(out, size, sa->sa_family == AF_INET6 ? "[%s]:%s" : "%s:%s", host, port);
}

int main(int argc, char *argv[])
{
    if (argc != 3) {
        fprintf(stderr, "usage: connect HOST PORT\n");
        return 2;
    }
    struct addrinfo hints = { .ai_family = AF_UNSPEC, .ai_socktype = SOCK_STREAM };
    struct addrinfo *res;
    int rc = getaddrinfo(argv[1], argv[2], &hints, &res);
    if (rc != 0) {
        fprintf(stderr, "connect: %s: %s\n", argv[1], gai_strerror(rc));
        return 1;
    }

    int fd = -1;
    for (struct addrinfo *ai = res; ai != NULL && fd < 0; ai = ai->ai_next) {
        char addr[80];
        addr_str(ai->ai_addr, ai->ai_addrlen, addr, sizeof addr);
        printf("trying %s ... ", addr);
        fflush(stdout);                 /* show it before connect() blocks */
        fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd < 0) {
            printf("socket: %s\n", strerror(errno));
            continue;
        }
        if (connect(fd, ai->ai_addr, ai->ai_addrlen) < 0) {
            printf("%s\n", strerror(errno));
            close(fd);
            fd = -1;
            continue;
        }
        printf("connected\n");
    }
    freeaddrinfo(res);
    if (fd < 0)
        return 1;

    /* The kernel picked our local address and port during connect(). */
    struct sockaddr_storage ss;
    socklen_t len = sizeof ss;
    char addr[80];
    if (getsockname(fd, (struct sockaddr *)&ss, &len) == 0) {
        addr_str((struct sockaddr *)&ss, len, addr, sizeof addr);
        printf("local end:  %s\n", addr);
    }
    len = sizeof ss;
    if (getpeername(fd, (struct sockaddr *)&ss, &len) == 0) {
        addr_str((struct sockaddr *)&ss, len, addr, sizeof addr);
        printf("remote end: %s\n", addr);
    }
    close(fd);
    return 0;
}
```

`getsockname()` gives the local end of a socket, `getpeername()` the remote end. Try it with nothing
listening, then with `nc -l` listening:

```console
$ ./connect localhost 6667
trying 127.0.0.1:6667 ... Connection refused
$ ./connect ::1 6667
trying [::1]:6667 ... Connection refused
$ nc -l 127.0.0.1 6667 >/dev/null &
$ ./connect localhost 6667
trying 127.0.0.1:6667 ... connected
local end:  127.0.0.1:49886
remote end: 127.0.0.1:6667
```

Your local port will differ. What happened:

- **Connection refused** (`ECONNREFUSED`): the host answered, but nothing listens on that port. Its kernel
  sent back a reset (RST), at once.
- On success, the kernel gave the socket a local address and a free **ephemeral port**, on Linux from
  32768–60999 (`/proc/sys/net/ipv4/ip_local_port_range`). A client almost never calls `bind()`.
- `connect()` returns after TCP's **three-way handshake**: your SYN, the server's SYN-ACK, your ACK. The
  server needn't call `accept()` first: its kernel finishes the handshake and queues the connection in the
  listen backlog.

If nobody answers at all, `connect()` waits much longer. For an address that drops packets, Linux resends
the SYN 6 times (`/proc/sys/net/ipv4/tcp_syn_retries`), with growing gaps:

```console
$ time ./connect 10.255.255.1 6667
trying 10.255.255.1:6667 ... Connection timed out

real	2m13.824s
```

More than two minutes. Exercise 1 adds your own time limit.

The `fflush(stdout)` after "trying" matters: on a terminal, stdout is line-buffered, so without it you'd
see nothing during those two minutes.

### glibc vs musl: getaddrinfo without hints

`hints` may be `NULL`. Then you get every kind of socket the address supports, not only TCP:

`examples/ch16/nohints.c`:

```c
/* nohints.c - getaddrinfo() without hints, and why a client must pass them.
 * Lists every result for "localhost" PORT and tries to connect to each. */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static const char *type_name(int type)
{
    switch (type) {
    case SOCK_STREAM: return "SOCK_STREAM";
    case SOCK_DGRAM:  return "SOCK_DGRAM";
    case SOCK_RAW:    return "SOCK_RAW";
    default:          return "other";
    }
}

int main(int argc, char *argv[])
{
    if (argc != 2) {
        fprintf(stderr, "usage: nohints PORT\n");
        return 2;
    }
    struct addrinfo *res;
    int rc = getaddrinfo("localhost", argv[1], NULL, &res);   /* no hints! */
    if (rc != 0) {
        fprintf(stderr, "nohints: %s\n", gai_strerror(rc));
        return 1;
    }
    for (struct addrinfo *ai = res; ai != NULL; ai = ai->ai_next) {
        printf("%-11s protocol %2d: ", type_name(ai->ai_socktype), ai->ai_protocol);
        int fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd < 0) {
            printf("socket: %s\n", strerror(errno));
            continue;
        }
        if (connect(fd, ai->ai_addr, ai->ai_addrlen) < 0)
            printf("connect: %s\n", strerror(errno));
        else
            printf("connect: OK\n");
        close(fd);
    }
    freeaddrinfo(res);
    return 0;
}
```

Nothing listens on port 6667:

```console
$ ./nohints 6667
SOCK_STREAM protocol  6: connect: Connection refused
SOCK_DGRAM  protocol 17: connect: OK
SOCK_RAW    protocol  0: socket: Protocol not supported
$ ./nohints-musl 6667
SOCK_STREAM protocol  6: connect: Connection refused
SOCK_DGRAM  protocol 17: connect: OK
```

- glibc returns three entries: TCP (protocol 6), UDP (17) and a raw socket. musl returns only TCP and UDP.
- On **both**, the UDP `connect()` "succeeds" with nobody listening. UDP has no handshake; `connect()` only
  records the default destination. A "try each address" loop without hints would skip the refused TCP
  entry and happily use UDP.

The two libraries also word some resolver errors differently:

```console
$ ./connect nosuch.invalid 6667
connect: nosuch.invalid: Name or service not known
$ ./connect-musl nosuch.invalid 6667
connect: nosuch.invalid: Name does not resolve
```

**Better:** musl: its list holds only entries a client might use. **Portable:** always pass hints with
`ai_socktype = SOCK_STREAM` (and `ai_family = AF_UNSPEC`). Then both libraries return the same list, and
the loop can only pick TCP. Print `gai_strerror()` text, but never compare it.

## Reading lines from a stream

TCP carries a **byte stream**: the bytes stay in order, but the boundaries between your writes are lost.
One `read()` may return half a line, or three lines and a bit. [Chapter 15](15-a-first-server.md) solved
this on the server with a buffer and `msg_take_line()`. A client has the same problem.

`examples/ch16/lines.c` reads from a server, prints what each `read()` returned, and cuts out complete lines:

```c
/* lines.c - read lines from a TCP server: one read() is not one line.
 * Connects to 127.0.0.1:PORT and shows what each read() returned and
 * which complete lines the client could cut out of its buffer. */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

int main(int argc, char *argv[])
{
    if (argc != 2) {
        fprintf(stderr, "usage: lines PORT\n");
        return 2;
    }
    struct sockaddr_in addr = { .sin_family = AF_INET };
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons((unsigned short)atoi(argv[1]));
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0 || connect(fd, (struct sockaddr *)&addr, sizeof addr) < 0) {
        perror("connect");
        return 1;
    }

    char buf[512];              /* received bytes not yet cut into lines */
    size_t len = 0;
    for (;;) {
        ssize_t n = read(fd, buf + len, sizeof buf - 1 - len);
        if (n < 0 && errno == EINTR)
            continue;
        if (n < 0) {
            perror("read");
            return 1;
        }
        if (n == 0) {
            printf("end of file (%zu bytes left over)\n", len);
            break;
        }
        printf("read() returned %zd bytes\n", n);
        len += (size_t)n;

        /* Cut out every complete line. */
        char *start = buf, *nl;
        while ((nl = memchr(start, '\n', len - (size_t)(start - buf))) != NULL) {
            *nl = '\0';
            if (nl > start && nl[-1] == '\r')
                nl[-1] = '\0';          /* the protocol ends lines with CRLF */
            printf("  line: \"%s\"\n", start);
            start = nl + 1;
        }
        len -= (size_t)(start - buf);
        memmove(buf, start, len);       /* keep the partial line */
        if (len == sizeof buf - 1) {
            fprintf(stderr, "lines: line too long\n");
            return 1;
        }
    }
    close(fd);
    return 0;
}
```

The "server" is `nc`, the OpenBSD one this book uses (if yours rejects `-N` or `-C`, see
[Testing a server with nc](15-a-first-server.md#testing-a-server-with-nc)). It sends two and a half lines,
waits a second, then the rest of the third line and an unfinished fourth. `-N` shuts down its sending side
at the end of its input:

```console
$ { printf 'one\r\ntwo\r\nthr'; sleep 1; printf 'ee\r\nfour'; } | nc -N -l 127.0.0.1 6667 &
$ ./lines 6667
read() returned 13 bytes
  line: "one"
  line: "two"
read() returned 8 bytes
  line: "three"
end of file (4 bytes left over)
```

The same on musl. Notice:

- The first `read()` got two whole lines and `thr`, which stayed in the buffer.
- `read()` returned 0 at **end of file**: the server will send nothing more. `four` never got its line
  end, so it isn't a line. A real client would report or drop it.
- `read()` blocks while no data has arrived. That will matter soon.

## Writing to a closed connection: EPIPE and SIGPIPE

What happens if you write after the server has gone? Sockets behave like the pipes of
[Chapter 13](13-processes-and-signals.md), with a twist. This program connects to itself, lets the
"server" side close, then writes three times:

`examples/ch16/epipe.c`:

```c
/* epipe.c - what happens when you write to a connection the peer closed.
 *   ./epipe          default SIGPIPE action: the second write kills us
 *   ./epipe ignore   SIGPIPE ignored: the second write fails with EPIPE */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

/* Sleep for ms milliseconds (usleep is gone from POSIX: use nanosleep). */
static void sleep_ms(long ms)
{
    struct timespec ts = { ms / 1000, ms % 1000 * 1000000 };
    nanosleep(&ts, NULL);
}

int main(int argc, char *argv[])
{
    if (argc > 1 && strcmp(argv[1], "ignore") == 0)
        signal(SIGPIPE, SIG_IGN);

    /* A listener on 127.0.0.1, port 0: the kernel picks a free port. */
    struct sockaddr_in addr = { .sin_family = AF_INET };
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    socklen_t len = sizeof addr;
    int lfd = socket(AF_INET, SOCK_STREAM, 0);
    if (lfd < 0 || bind(lfd, (struct sockaddr *)&addr, sizeof addr) < 0
            || listen(lfd, 1) < 0
            || getsockname(lfd, (struct sockaddr *)&addr, &len) < 0) {
        perror("listen");
        return 1;
    }

    /* Connect to ourselves, accept, and let the "server" side hang up. */
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0 || connect(fd, (struct sockaddr *)&addr, sizeof addr) < 0) {
        perror("connect");
        return 1;
    }
    int peer = accept(lfd, NULL, NULL);
    close(peer);
    sleep_ms(100);                     /* let the FIN arrive */

    for (int i = 1; i <= 3; i++) {
        ssize_t n = write(fd, "hello\n", 6);
        if (n < 0)
            printf("write %d: %s\n", i, strerror(errno));
        else
            printf("write %d: %zd bytes\n", i, n);
        fflush(stdout);
        sleep_ms(100);                 /* let the peer's reply (RST) arrive */
    }
    close(fd);
    close(lfd);
    return 0;
}
```

```console
$ ./epipe; echo "exit $?"
write 1: 6 bytes
exit 141
$ ./epipe ignore; echo "exit $?"
write 1: 6 bytes
write 2: Broken pipe
write 3: Broken pipe
exit 0
```

musl prints exactly the same. Step by step:

1. The peer closed its socket, and its kernel sent us a FIN: "I'm done sending".
2. A FIN says nothing about **reading**, so the first write succeeds.
3. Those bytes reach a socket that no longer exists. The peer's kernel answers with a reset (RST).
4. Now our kernel knows. The second write raises SIGPIPE, which kills the process (exit status 141 =
   128 + 13, SIGPIPE's number). If SIGPIPE is ignored, the write fails with `EPIPE` instead.

`strace` shows the order:

```console
$ strace -o trace.txt -e trace=write ./epipe >/dev/null; grep -v 'write(1,' trace.txt
write(4, "hello\n", 6)                  = 6
write(4, "hello\n", 6)                  = -1 EPIPE (Broken pipe)
--- SIGPIPE {si_signo=SIGPIPE, si_code=SI_USER, si_pid=260822, si_uid=1000} ---
+++ killed by SIGPIPE +++
```

The `grep` hides the program's own `printf` output. Your PID will differ. musl's trace is the same.

So a successful write doesn't mean the other side got your data, only that your kernel took it. That's why
parley's `sig_init()` ignores SIGPIPE ([Chapter 13](13-processes-and-signals.md)): a client must report
"Broken pipe", not vanish.

`sleep_ms()` uses `nanosleep()`. The older `usleep()` was removed from POSIX in 2008; with
`_POSIX_C_SOURCE 200809L` both libraries hide it, and gcc warns "implicit declaration of function
'usleep'".

## Ending a conversation: shutdown() vs close()

In some protocols the client sends a request, says "that's all", then reads the answer. How does it say
"that's all" without hanging up?

- `close(fd)` gives up the **descriptor**. You can't read the answer after that.
- `shutdown(fd, how)` acts on the **connection**:

| `how` | effect |
|---|---|
| `SHUT_WR` | no more sending. The peer reads end of file. You can still read. This is a **half-close**. |
| `SHUT_RD` | no more receiving. Rarely useful. |
| `SHUT_RDWR` | both. Wakes up any thread blocked in `read()` on this socket ([Chapter 17](17-threads.md)). |

`close()` ends the connection only when the **last** descriptor for it closes; after `fork()` or `dup()`
there may be several. `shutdown()` acts at once.

Here's a network `wc`. For each client, it counts lines and bytes until end of file, then sends the
counts back:

`examples/ch16/wcserver.c`:

```c
/* wcserver.c - a network wc: for each client, count the lines and bytes
 * it sends until end of file, then send back the counts and hang up.
 * Serves one client at a time on 127.0.0.1:PORT. */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <unistd.h>

int main(int argc, char *argv[])
{
    if (argc != 2) {
        fprintf(stderr, "usage: wcserver PORT\n");
        return 2;
    }
    signal(SIGPIPE, SIG_IGN);
    struct sockaddr_in addr = { .sin_family = AF_INET };
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons((unsigned short)atoi(argv[1]));
    int on = 1;
    int lfd = socket(AF_INET, SOCK_STREAM, 0);
    if (lfd < 0 || setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof on) < 0
            || bind(lfd, (struct sockaddr *)&addr, sizeof addr) < 0
            || listen(lfd, 8) < 0) {
        perror("wcserver");
        return 1;
    }
    for (;;) {
        int fd = accept(lfd, NULL, NULL);
        if (fd < 0)
            continue;
        long lines = 0, bytes = 0;
        char buf[4096];
        ssize_t n;
        while ((n = read(fd, buf, sizeof buf)) != 0) {
            if (n < 0 && errno == EINTR)
                continue;
            if (n < 0)
                break;
            for (ssize_t i = 0; i < n; i++)
                lines += buf[i] == '\n';
            bytes += n;
        }
        /* read() returned 0: the client has finished sending. Answer. */
        char reply[64];
        int len = snprintf(reply, sizeof reply, "%ld lines, %ld bytes\n", lines, bytes);
        if (write(fd, reply, (size_t)len) < 0)
            perror("wcserver: write");
        close(fd);
    }
}
```

The client sends its stdin and then does one of three things before it reads the answer:

`examples/ch16/wcclient.c`:

```c
/* wcclient.c - send stdin to wcserver on 127.0.0.1:PORT, print its answer.
 *   ./wcclient PORT shutdown   send, shutdown(SHUT_WR), then read (correct)
 *   ./wcclient PORT nothing    send, then read: both sides wait forever
 *   ./wcclient PORT close      send, close(), then read: too late */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

int main(int argc, char *argv[])
{
    if (argc != 3) {
        fprintf(stderr, "usage: wcclient PORT shutdown|nothing|close\n");
        return 2;
    }
    struct sockaddr_in addr = { .sin_family = AF_INET };
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons((unsigned short)atoi(argv[1]));
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0 || connect(fd, (struct sockaddr *)&addr, sizeof addr) < 0) {
        perror("connect");
        return 1;
    }

    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, stdin)) > 0) {
        if (write(fd, buf, n) < 0) {
            perror("write");
            return 1;
        }
    }

    if (strcmp(argv[2], "shutdown") == 0)
        shutdown(fd, SHUT_WR);          /* "I have finished sending" */
    else if (strcmp(argv[2], "close") == 0)
        close(fd);                      /* gives up the descriptor */

    ssize_t got;
    while ((got = read(fd, buf, sizeof buf)) > 0)
        fwrite(buf, 1, (size_t)got, stdout);
    if (got < 0) {
        fprintf(stderr, "wcclient: read: %s\n", strerror(errno));
        return 1;
    }
    close(fd);
    return 0;
}
```

```console
$ ./wcserver 6667 &
$ printf 'one\ntwo\nthree\n' | ./wcclient 6667 shutdown; echo "exit $?"
3 lines, 14 bytes
exit 0
$ printf 'one\ntwo\nthree\n' | timeout 2 ./wcclient 6667 nothing; echo "exit $?"
exit 124
$ printf 'one\ntwo\nthree\n' | ./wcclient 6667 close; echo "exit $?"
wcclient: read: Bad file descriptor
exit 1
$ ./wcclient 6667 shutdown < /etc/services
361 lines, 12813 bytes
$ wc -lc /etc/services
  361 12813 /etc/services
```

The counts for `/etc/services` will differ on your machine. musl gives the same results.

- `shutdown`: correct.
- `nothing`: a **deadlock**. The server waits for end of file, the client for the answer. `timeout`
  killed the client after 2 seconds (exit 124).
- `close`: the descriptor is gone, so `read()` fails with `EBADF`.

`nc` has the same choice. Without `-N` it never shuts down its sending side:

```console
$ printf 'hello\n' | nc -N 127.0.0.1 6667
1 lines, 6 bytes
$ printf 'hello\n' | timeout 2 nc 127.0.0.1 6667; echo "exit $?"
exit 124
```

`strace` shows the client's side of a good conversation (glibc adds some start-up lines before these):

```console
$ printf 'one\ntwo\nthree\n' | strace -o trace.txt -e trace=connect,write,shutdown,read,close ./wcclient-musl 6667 shutdown >/dev/null
$ grep -v '(0,\|(1,' trace.txt
connect(3, {sa_family=AF_INET, sin_port=htons(6667), sin_addr=inet_addr("127.0.0.1")}, 16) = 0
write(3, "one\ntwo\nthree\n", 14)       = 14
shutdown(3, SHUT_WR)                    = 0
read(3, "3 lines, 14 bytes\n", 4096)    = 18
read(3, "", 4096)                       = 0
close(3)                                = 0
+++ exited with 0 +++
```

(The `grep` hides the reads from stdin and writes to stdout.) parley's client ends the same way: send
`QUIT`, `shutdown(SHUT_WR)`, then print the server's last words until it closes.

## The limit of blocking I/O

A chat client has two inputs, the keyboard and the server, and messages from others may arrive at any
time. With blocking calls, the program must pick one thing to wait for. This client waits for each in
turn:

`examples/ch16/naive.c`:

```c
/* naive.c - a blocking chat client that waits on one thing at a time.
 * It reads a line from stdin, sends it to 127.0.0.1:PORT, then waits for
 * the server's answer. Every message is stamped with the time it is seen. */
#define _POSIX_C_SOURCE 200809L
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

static struct timespec start;

/* Seconds since the program started. */
static double elapsed(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (double)(now.tv_sec - start.tv_sec) + (now.tv_nsec - start.tv_nsec) / 1e9;
}

int main(int argc, char *argv[])
{
    if (argc != 2) {
        fprintf(stderr, "usage: naive PORT\n");
        return 2;
    }
    clock_gettime(CLOCK_MONOTONIC, &start);
    struct sockaddr_in addr = { .sin_family = AF_INET };
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons((unsigned short)atoi(argv[1]));
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0 || connect(fd, (struct sockaddr *)&addr, sizeof addr) < 0) {
        perror("connect");
        return 1;
    }

    char line[256], buf[512];
    for (;;) {
        printf("[%.1fs] waiting for stdin\n", elapsed());
        fflush(stdout);
        if (fgets(line, sizeof line, stdin) == NULL)
            break;                                  /* blocks here ... */
        printf("[%.1fs] sending %s", elapsed(), line);
        if (write(fd, line, strlen(line)) < 0) {
            perror("write");
            return 1;
        }
        printf("[%.1fs] waiting for the server\n", elapsed());
        fflush(stdout);
        ssize_t n = read(fd, buf, sizeof buf);      /* ... or here */
        if (n <= 0) {
            printf("[%.1fs] the server hung up\n", elapsed());
            break;
        }
        printf("[%.1fs] received %zd bytes:\n%.*s", elapsed(), n, (int)n, buf);
    }
    close(fd);
    return 0;
}
```

The "server" sends a news line every second. You type `hi` after 3.5 seconds, then `bye`, then
`still there?`:

```console
$ { for i in 1 2 3; do echo "news $i"; sleep 1; done; sleep 3; } | nc -N -l 127.0.0.1 6667 >/dev/null &
$ { sleep 3.5; echo hi; sleep 0.5; echo bye; sleep 0.5; echo 'still there?'; } | ./naive 6667
[0.0s] waiting for stdin
[3.5s] sending hi
[3.5s] waiting for the server
[3.5s] received 21 bytes:
news 1
news 2
news 3
[3.5s] waiting for stdin
[4.0s] sending bye
[4.0s] waiting for the server
[5.7s] the server hung up
```

Times may vary a little. musl behaves the same. Two failures:

1. **Blocked on stdin, deaf to the server.** The news arrived at 0, 1 and 2 seconds and waited in the
   socket's receive buffer. The program saw it only at 3.5 s, when you typed.
2. **Blocked on the server, deaf to you.** After `bye`, it waited for a reply that never came.
   `still there?` (typed at 4.5 s) was never even read.

Three ways out:

| way | idea | where |
|---|---|---|
| a protocol trick | after each line, ask the server to reply, and read until it does | client v1, this chapter |
| threads | one thread blocks on stdin, another on the socket | [Chapter 17](17-threads.md) (for the server) |
| readiness | `poll()` waits for **any** of several descriptors | [Chapter 18](18-event-loop.md), and the client in [Chapter 19](19-an-interactive-client.md) |

## Building parley: client v1

Until now `parley` printed the protocol lines it would send. Now it sends them:

1. Connect with `net_connect()` ([Chapter 14](14-networking-basics.md)). Send `NICK`.
2. Read a line from you. Send it (as `MSG`, or a command).
3. Send `PING :sync`. Print every line the server sends until its `PONG :sync` arrives.
4. Repeat. At end of input, send `QUIT`, `shutdown(SHUT_WR)`, and print the rest until the server
   closes.

Step 3 is the protocol trick. The server handles lines in order, so when it answers our `PING`, it has
already sent every reply to the line before. And the `PONG` always comes, so the client never waits for
nothing. It still can't show messages while it waits for your input, but a one-client-at-a-time server has
no other people yet. The trick is a teaching device; [Chapter 19](19-an-interactive-client.md) replaces it
with `poll()`.

### The Makefile

The client now needs `io.o`, `net.o` and `sig.o`. Change the `CLIENT_OBJS` line:

```make
CLIENT_OBJS := $(addprefix $(BUILD)/, parley.o io.o net.o sig.o msg.o strbuf.o str.o log.o)
```

### src/parley.c

Replace the comment at the top of the file, and add five includes:

```c
/* parley.c - the parley chat client.
 * Version 1: blocking I/O. After each line you type, the client sends it,
 * then shows what the server sent until the server has answered. While it
 * waits for your input, it cannot notice anything the server sends. */
#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include <sys/socket.h>
#include <unistd.h>

#include "io.h"
#include "log.h"
#include "msg.h"
#include "net.h"
#include "sig.h"
#include "str.h"
#include "version.h"
```

Below `quitting`, add the connection state, and replace `send_msg()`. It now writes the line to the socket
with `write_all()` ([Chapter 12](12-files-and-descriptors.md)), which loops over short writes.

```c
/* Set by /quit: stop reading input. */
static bool quitting = false;

/* The connection to the server. */
static int server_fd = -1;
static struct strbuf server_in;     /* received, not yet handled */
static bool server_skipping;        /* skipping a too-long line */

/* Send one protocol message to the server. */
static void send_msg(const struct msg *m)
{
    char line[MSG_LINE_MAX + 1];
    int n = msg_format(line, sizeof line, m);
    if (n > 0 && write_all(server_fd, line, (size_t)n) < 0)
        die("sending to server: %s", strerror(errno));
}
```

Then add the code that reads replies. `show_replies()` is the receive half of `lines.c`, built from
[Chapter 15](15-a-first-server.md)'s tools: `read_into()` appends to a `strbuf`, `msg_take_line()` cuts
out lines. It parses each line to spot our `PONG`, and prints all the others.

```c
/* Our PING token: the server's PONG with it means "it has seen everything
 * we sent before the PING". */
#define SYNC_TOKEN "sync"

/* Print the lines the server sends until it answers our PING (returns
 * true), or until it closes the connection (returns false). */
static bool show_replies(bool want_pong)
{
    char line[MSG_LINE_MAX + 1];
    for (;;) {
        enum frame_status st;
        while ((st = msg_take_line(&server_in, &server_skipping, line)) != FRAME_NONE) {
            if (st == FRAME_TOO_LONG)
                continue;              /* the server never sends those */
            char copy[MSG_LINE_MAX + 1];
            struct msg m;
            str_copy(copy, line, sizeof copy);   /* msg_parse cuts it up */
            if (msg_parse(copy, &m) == MSG_OK && str_ieq(m.command, "PONG")
                    && msg_param(&m, 0) != NULL
                    && strcmp(msg_param(&m, 0), SYNC_TOKEN) == 0) {
                if (want_pong) {
                    fflush(stdout);    /* show it now: we may wait for input next */
                    return true;
                }
                continue;
            }
            printf("%s\n", line);
        }
        fflush(stdout);
        ssize_t n = read_into(server_fd, &server_in, 4096);
        if (n < 0 && errno == EINTR)
            continue;
        if (n < 0)
            die("reading from server: %s", strerror(errno));
        if (n == 0)
            return false;              /* end of file: the server hung up */
    }
}

/* Send a PING and show everything up to its PONG. */
static void sync_with_server(void)
{
    send_msg(&(struct msg){ .command = "PING", .trailing = SYNC_TOKEN });
    if (!show_replies(true) && !quitting)
        die("the server closed the connection");
}
```

- `msg_parse()` cuts its input into pieces, so it gets a copy; we still print the original.
- `show_replies(false)` is for the end: it skips any `PONG` and reads until end of file.
- `read_into()` doesn't retry on `EINTR`. Ctrl-C interrupts the `read()`, and we go round the loop again.
  The server closes the connection soon after `QUIT`, which ends the loop.
- `fflush(stdout)` before each `read()`: when stdout is a pipe it's fully buffered, and replies would sit
  in the buffer while the client waits.

`cmd_help()` printed to stderr, because stdout carried protocol lines. Now stdout shows what the server
says, and help, so `cmd_help()` uses `printf`:

```c
static void cmd_help(const char *arg)
{
    (void)arg;
    printf("commands:\n");
    for (int i = 0; i < NCOMMANDS; i++) {
        char usage[32];
        snprintf(usage, sizeof usage, "/%s %s", commands[i].name, commands[i].args);
        printf("  %-15s %s\n", usage, commands[i].help);
    }
    printf("anything else is sent as a message\n");
}
```

Last, `main()`. The new parts: `sig_init()`, connecting, a sync after `NICK` and after each line, and the
half-close at the end.

```c
int main(int argc, char *argv[])
{
    struct config cfg = { .host = "localhost", .port = DEFAULT_PORT, .nick = "guest" };
    parse_args(argc, argv, &cfg);
    log_init("parley", false);
    if (sig_init() < 0)
        die("setting up signals: %s", strerror(errno));

    char err[256];
    server_fd = net_connect(cfg.host, (int)cfg.port, err, sizeof err);
    if (server_fd < 0)
        die("cannot connect to %s", err);
    send_msg(&(struct msg){ .command = "NICK", .nparams = 1, .params = { cfg.nick } });
    sync_with_server();

    /* getline (POSIX) reads a whole line of any length, growing the
     * buffer with realloc as needed. We own the buffer and free it. */
    char *line = NULL;
    size_t cap = 0;
    ssize_t len;
    while (!quitting && (len = getline(&line, &cap, stdin)) != -1) {
        if (strlen(line) != (size_t)len) {
            log_msg(LOG_WARN, "line contains a NUL byte, not sent");
            continue;
        }
        str_chomp(line);
        if (strlen(line) > INPUT_MAX) {
            log_msg(LOG_WARN, "line too long (max %d bytes), not sent", INPUT_MAX);
            continue;
        }
        if (line[0] == '\0')
            continue;              /* ignore empty lines */
        if (line[0] == '/')
            run_command(line);
        else
            send_msg(&(struct msg){ .command = "MSG", .trailing = line });
        if (!quitting)
            sync_with_server();
    }
    /* getline fails with EINTR when Ctrl-C (SIGINT) arrives: quit too. */
    if (ferror(stdin) && sig_quit == 0)
        die("reading input: %s", strerror(errno));
    free(line);

    if (!quitting)
        send_msg(&(struct msg){ .command = "QUIT" });   /* end of input: goodbye */
    /* shutdown(SHUT_WR): "we will send nothing more" (the server sees end of
     * file), but we can still read its last words until it closes. */
    shutdown(server_fd, SHUT_WR);
    show_replies(false);
    close(server_fd);
    sb_free(&server_in);
    if (fflush(stdout) == EOF)
        die("writing: %s", strerror(errno));
    return 0;
}
```

Notice the order at the end: `QUIT`, then `shutdown(SHUT_WR)` ("nothing more from me"), then
`show_replies(false)` prints the server's `ERROR :Goodbye` until it closes. With `close()` instead of
`shutdown()`, the goodbye would be lost.

`sig_init()` makes SIGINT set `sig_quit` without `SA_RESTART`, so Ctrl-C makes `getline()` fail with
`EINTR` and `ferror(stdin)` is true. That's not an error: the client says goodbye as at end of input.

### Checkpoint

Build and test as usual, then start the server from [Chapter 15](15-a-first-server.md) in the background.
It still serves one client at a time, which is all client v1 needs:

```console
$ make -s test-all
unit: 28 passed, 0 failed
unit: 28 passed, 0 failed
$ make -s SANITIZE=1 test
unit: 28 passed, 0 failed
$ make -s LIBC=musl
$ build/glibc/parleyd -p 6616 2>server.log & server=$!; sleep 0.2
$ printf 'hello\n/me waves\n/who\n/nick bob\n/quit see you\n' | build/glibc/parley -p 6616 -n alice; echo "exit $?"
:parley 001 alice :Welcome to parley, alice
:alice JOIN
:parley 353 alice :alice
:parley 366 alice :End of NAMES
:alice MSG :hello
:alice ME :waves
:parley 353 alice :alice
:parley 366 alice :End of NAMES
:alice NICK bob
ERROR :Goodbye
exit 0
$ printf 'hi from musl\n' | build/musl/parley -h 127.0.0.1 -p 6616 -n carol; echo "exit $?"
:parley 001 carol :Welcome to parley, carol
:carol JOIN
:parley 353 carol :carol
:parley 366 carol :End of NAMES
:carol MSG :hi from musl
ERROR :Goodbye
exit 0
$ printf '/help\n/dance\n/nick\n' | build/glibc/parley -p 6616 -n dave 2>&1
:parley 001 dave :Welcome to parley, dave
:dave JOIN
:parley 353 dave :dave
:parley 366 dave :End of NAMES
commands:
  /nick NEWNICK   change your nickname
  /me ACTION      describe an action
  /who            list who is in the room
  /quit [REASON]  leave
  /help           show this help
anything else is sent as a message
parley: unknown command /dance (try /help)
parley: usage: /nick NEWNICK
ERROR :Goodbye
$ printf 'hello?\n/nick erin\nhello!\n' | build/glibc/parley -p 6616 -n 9lives
:parley 432 * 9lives :Erroneous nickname
:parley 451 * :You have not registered
:parley 001 erin :Welcome to parley, erin
:erin JOIN
:parley 353 erin :erin
:parley 366 erin :End of NAMES
:erin MSG :hello!
ERROR :Goodbye
```

The output is raw protocol lines for now. [Chapter 19](19-an-interactive-client.md) renders them nicely.
Note that `PONG :sync` never shows: the client eats it.

What the client does on the wire:

```console
$ printf 'hi\n' | strace -o trace.txt -e trace=connect,shutdown build/musl/parley -p 6616 -n frank >/dev/null; cat trace.txt
connect(3, {sa_family=AF_INET, sin_port=htons(6616), sin_addr=inet_addr("127.0.0.1")}, 16) = 0
shutdown(3, SHUT_WR)                    = 0
+++ exited with 0 +++
```

Now the limitation. gina connects and reads her input from a named pipe (`mkfifo`), so the test can type
for her later. Then the server stops:

```console
$ mkfifo in; build/glibc/parley -p 6616 -n gina <in >gina.txt 2>&1 & gina=$!; exec 3>in; sleep 0.3
$ kill -TERM $server; wait $server; sleep 0.3; cat gina.txt
:parley 001 gina :Welcome to parley, gina
:gina JOIN
:parley 353 gina :gina
:parley 366 gina :End of NAMES
$ printf 'anyone there?\n' >&3; wait $gina; echo "exit $?"; cat gina.txt
exit 1
:parley 001 gina :Welcome to parley, gina
:gina JOIN
:parley 353 gina :gina
:parley 366 gina :End of NAMES
parley: error: sending to server: Broken pipe
```

gina was blocked in `getline()` and didn't notice the server was gone; only her next line found out. As
in `epipe.c`, the first write (`MSG :anyone there?`) succeeded and the second (`PING :sync`) failed with
`EPIPE`. The server's log:

```console
$ exec 3>&-; cat server.log
parleyd: info: listening on 127.0.0.1:6616 (one client at a time)
parleyd: info: alice joined
parleyd: info: alice is now bob
parleyd: info: bob left (see you)
parleyd: info: carol joined
parleyd: info: carol left (Quit)
parleyd: info: dave joined
parleyd: info: dave left (Quit)
parleyd: info: erin joined
parleyd: info: erin left (Quit)
parleyd: info: frank joined
parleyd: info: frank left (Quit)
parleyd: info: gina joined
parleyd: info: got signal 15 (Terminated), shutting down
```

With no server, both libraries give the same error. The server listened on 127.0.0.1 only, so `::1` was
refused too:

```console
$ build/musl/parley -p 6616; echo "exit $?"
parley: error: cannot connect to 127.0.0.1:6616: Connection refused
exit 1
$ build/glibc/parley -h ::1 -p 6616; echo "exit $?"
parley: error: cannot connect to [::1]:6616: Connection refused
exit 1
$ rm -f in trace.txt gina.txt server.log && make clean >/dev/null && ls
Makefile
src
tests
```

The complete project at this point is in `project/ch16/`.

## Summary

- A client calls `getaddrinfo()`, then `socket()` and `connect()` for each address until one works. The
  kernel picks the local address and an ephemeral port.
- "Connection refused" comes back at once: the host is there, but nothing listens. A silent host makes
  `connect()` wait over two minutes on Linux.
- Always pass hints with `SOCK_STREAM`. Without them you also get UDP entries (and raw ones on glibc), and
  a UDP `connect()` "succeeds" with nobody there.
- TCP is a byte stream: one `read()` is not one line. Buffer the bytes and cut out complete lines. `read()`
  returning 0 means end of file.
- After the peer closes, the first write still succeeds. The next raises SIGPIPE, or fails with `EPIPE`
  if SIGPIPE is ignored.
- `shutdown(fd, SHUT_WR)` is a half-close: the peer reads end of file, and you can still read its answer.
  `close()` gives up the descriptor.
- A blocking client waits for one thing at a time: the keyboard **or** the server. Client v1 hides this
  with a PING/PONG sync. Threads and `poll()` are the real cures.

## Exercises

1. **A connect time limit.** Copy `connect.c` to `timeout.c` and give each `connect()` 3 seconds. Use
   `alarm(3)` and a SIGALRM handler installed with `sigaction()` **without** `SA_RESTART`
   ([Chapter 13](13-processes-and-signals.md)). Print "timed out" when `connect()` fails with `EINTR`.
   Test it with `10.255.255.1`.

<details><summary>Solution</summary>

Add `#include <signal.h>`, a handler that does nothing, and install it at the start of `main()`:

```c
static void on_alarm(int sig)
{
    (void)sig;          /* nothing to do: interrupting connect() is the point */
}

int main(int argc, char *argv[])
{
    struct sigaction sa = { .sa_handler = on_alarm };   /* no SA_RESTART */
    sigemptyset(&sa.sa_mask);
    sigaction(SIGALRM, &sa, NULL);
```

Then wrap the `connect()` call in the loop:

```c
        alarm(3);
        rc = connect(fd, ai->ai_addr, ai->ai_addrlen);
        int saved = errno;
        alarm(0);
        if (rc < 0) {
            printf("%s\n", saved == EINTR ? "timed out" : strerror(saved));
            close(fd);
            fd = -1;
            continue;
        }
```

`alarm(0)` cancels the alarm if `connect()` finished first. Save `errno` before it, out of habit.

```console
$ time ./timeout 10.255.255.1 6667
trying 10.255.255.1:6667 ... timed out

real	0m3.001s
$ ./timeout-musl 10.255.255.1 6667
trying 10.255.255.1:6667 ... timed out
```

With `SA_RESTART`, the kernel would restart `connect()` and the alarm would change nothing.
[Chapter 19](19-an-interactive-client.md) shows the event-loop way: a non-blocking `connect()` and `poll()`
with a timeout.

</details>

2. **SIGPIPE, one call at a time.** Ignoring SIGPIPE affects the whole process. POSIX also has a per-call
   flag: `send(fd, buf, len, MSG_NOSIGNAL)` (`send()` is `write()` for sockets, with flags). Change
   `epipe.c` to use it, without touching the signal. What does it print?

<details><summary>Solution</summary>

Change the write in the loop:

```c
        ssize_t n = send(fd, "hello\n", 6, MSG_NOSIGNAL);
```

Run it without `ignore`:

```console
$ ./nosignal; echo "exit $?"
write 1: 6 bytes
write 2: Broken pipe
write 3: Broken pipe
exit 0
```

The same on musl. The second `send()` fails with `EPIPE`, and no signal is raised: handy in a library,
which shouldn't change the signal settings of the program using it. parley ignores SIGPIPE once in
`sig_init()`, because it also writes with plain `write()`.

</details>

3. **tcpcat.** Write `tcpcat HOST PORT`: it connects with the `getaddrinfo()` loop, sends all of stdin,
   half-closes, and prints everything the server sends until it closes (like `nc -N`). Handle short
   writes and `EINTR`. Test it against `wcserver`.

<details><summary>Solution</summary>

```c
/* tcpcat.c - send stdin to HOST PORT, then print everything the server
 * sends back until it closes the connection (like nc -N). */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <netdb.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static int tcp_connect(const char *host, const char *port)
{
    struct addrinfo hints = { .ai_family = AF_UNSPEC, .ai_socktype = SOCK_STREAM };
    struct addrinfo *res;
    int rc = getaddrinfo(host, port, &hints, &res);
    if (rc != 0) {
        fprintf(stderr, "tcpcat: %s: %s\n", host, gai_strerror(rc));
        return -1;
    }
    int fd = -1;
    for (struct addrinfo *ai = res; ai != NULL && fd < 0; ai = ai->ai_next) {
        fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd >= 0 && connect(fd, ai->ai_addr, ai->ai_addrlen) < 0) {
            fprintf(stderr, "tcpcat: connect: %s\n", strerror(errno));
            close(fd);
            fd = -1;
        }
    }
    freeaddrinfo(res);
    return fd;
}

int main(int argc, char *argv[])
{
    if (argc != 3) {
        fprintf(stderr, "usage: tcpcat HOST PORT\n");
        return 2;
    }
    signal(SIGPIPE, SIG_IGN);
    int fd = tcp_connect(argv[1], argv[2]);
    if (fd < 0)
        return 1;

    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, stdin)) > 0) {
        for (size_t done = 0; done < n; ) {          /* write may be short */
            ssize_t w = write(fd, buf + done, n - done);
            if (w < 0 && errno == EINTR)
                continue;
            if (w < 0) {
                fprintf(stderr, "tcpcat: write: %s\n", strerror(errno));
                return 1;
            }
            done += (size_t)w;
        }
    }
    shutdown(fd, SHUT_WR);              /* the server sees end of file */

    ssize_t got;
    while ((got = read(fd, buf, sizeof buf)) != 0) {
        if (got < 0 && errno == EINTR)
            continue;
        if (got < 0) {
            fprintf(stderr, "tcpcat: read: %s\n", strerror(errno));
            return 1;
        }
        fwrite(buf, 1, (size_t)got, stdout);
    }
    close(fd);
    return 0;
}
```

```console
$ ./wcserver 6667 &
$ printf 'a\nb\n' | ./tcpcat localhost 6667
2 lines, 4 bytes
$ ./tcpcat-musl 127.0.0.1 6667 < /etc/services
361 lines, 12813 bytes
$ kill %1
$ ./tcpcat localhost 6667 </dev/null; echo "exit $?"
tcpcat: connect: Connection refused
exit 1
```

</details>

4. **Where am I connected?** Make parley log `parley: info: connected to 127.0.0.1:6667` on stderr after
   it connects. Use `getpeername()` and `net_addr_str()` from `net.h`.

<details><summary>Solution</summary>

In `main()`, right after the `net_connect()` check:

```c
    struct sockaddr_storage ss;
    socklen_t sslen = sizeof ss;
    if (getpeername(server_fd, (struct sockaddr *)&ss, &sslen) == 0) {
        char where[NET_ADDR_MAX];
        net_addr_str((struct sockaddr *)&ss, sslen, where, sizeof where);
        log_msg(LOG_INFO, "connected to %s", where);
    }
```

`struct sockaddr_storage` is big enough for any address ([Chapter 14](14-networking-basics.md)).

```console
$ printf 'hi\n' | build/glibc/parley -n ann
parley: info: connected to 127.0.0.1:6667
:parley 001 ann :Welcome to parley, ann
:ann JOIN
:parley 353 ann :ann
:parley 366 ann :End of NAMES
:ann MSG :hi
ERROR :Goodbye
```

This is the address that worked. With a name like `localhost`, it tells you whether you got IPv4 or IPv6.

</details>

Sources for this chapter: [REFERENCES.md](REFERENCES.md#chapter-16--a-first-client)
