# Chapter 18 — The Event Loop

[Chapter 17](17-threads.md) gave every client a thread, and ended with a weak spot: one client that stops
reading blocks a `write()` inside a lock, and the whole server stops. This chapter takes the other design
from Chapter 17's list: **one thread, many sockets**. The server never waits for one client. It asks the
kernel "which of these sockets are ready?" with `poll()`, does the ready work, and asks again. That loop is
an **event loop**.

On the way: non-blocking sockets, partial writes and output queues, **backpressure**, timers built from
`poll()`'s timeout, the self-pipe again, and the alternatives `select()` and Linux's `epoll`.

At the end, `parleyd` becomes **server v3**, its final design: one thread, no locks, a 64 KiB output queue
per client, idle timeouts and a clean shutdown. The slow reader that stopped server v2 doesn't bother it.

Build every example twice, once for each C library:

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o poll_echo poll_echo.c
$ musl-gcc -std=c99 -Wall -Wextra -pedantic -static -o poll_echo-musl poll_echo.c
```

## Waiting for many things at once

A blocking `read()` waits for **one** descriptor. A server has many: the listening socket and every client.
While it waits on one, it can't notice the others. Chapter 17 gave each descriptor its own thread.

The other way is to wait for all of them **at once**. You give the kernel a list of descriptors and what
you want to do with each, read or write. The kernel puts you to sleep until at least one is **ready**, and
tells you which. You read from the ready ones, which won't block, and ask again:

```text
for (;;) {
    wait until some descriptors are ready       poll()
    for each ready descriptor:
        do what it is ready for                 accept(), read(), write()
}
```

`poll()` reports **readiness**: "a `read()` on this socket would not block now". You still make the call
and copy the data yourself. Some systems offer **completion** instead: you start a read with your buffer,
and the kernel tells you later that the data is in it. Windows' I/O completion ports and Linux's `io_uring`
work like that; powerful, but not portable. `poll()` is POSIX, works on every Unix, and is plenty for a
chat server.

Readiness is a hint about **now**. A "writable" socket has **some** room in its send buffer, maybe not
enough for everything. So the event loop also needs sockets that never block, even when you ask for too
much.

## Non-blocking sockets

You met `O_NONBLOCK` in [Chapter 12](12-files-and-descriptors.md), with a pipe: `read` and `write` on a
non-blocking descriptor never wait. With nothing to read, or no room to write, they fail at once with
`EAGAIN`. A write that finds **some** room is **short**: it writes what fits and returns the count. The
project has had `set_nonblock()` in `io.c` since then, unused until now.

Sockets work the same way, with bigger buffers. This program connects a TCP socket to itself and never
reads the other end:

`examples/ch18/nonblock.c`:

```c
/* nonblock.c - what "non-blocking" means for read() and write().
 * It makes a TCP connection to itself and never reads the other end. */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static int set_nonblock(int fd)
{
    int flags = fcntl(fd, F_GETFL);         /* the file status flags ... */
    if (flags < 0)
        return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);   /* ... plus one */
}

/* Connect a socket to a listening socket on a free port (port 0: the
 * kernel picks one); *server gets the accepted end. Returns the client end. */
static int connect_to_self(int *server)
{
    struct sockaddr_in a = { .sin_family = AF_INET };
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    socklen_t len = sizeof a;
    int lfd = socket(AF_INET, SOCK_STREAM, 0);
    int cfd = socket(AF_INET, SOCK_STREAM, 0);
    if (lfd < 0 || cfd < 0 || bind(lfd, (struct sockaddr *)&a, sizeof a) < 0
            || listen(lfd, 1) < 0
            || getsockname(lfd, (struct sockaddr *)&a, &len) < 0
            || connect(cfd, (struct sockaddr *)&a, sizeof a) < 0
            || (*server = accept(lfd, NULL, NULL)) < 0)
        return -1;
    close(lfd);
    return cfd;
}

int main(void)
{
    printf("EAGAIN = %d, EWOULDBLOCK = %d\n", EAGAIN, EWOULDBLOCK);

    int server;
    int fd = connect_to_self(&server);
    if (fd < 0 || set_nonblock(fd) < 0) {
        perror("setup");
        return 1;
    }

    /* Nothing has been sent to us: a blocking read() would wait for ever. */
    char c;
    ssize_t n = read(fd, &c, 1);
    printf("read:  %zd (%s)\n", n, n < 0 ? strerror(errno) : "data");

    /* Nobody reads the other end: the kernel's buffers fill up. */
    size_t size = 64 * 1024 * 1024;
    char *big = calloc(1, size);
    if (big == NULL)
        return 1;
    for (int i = 1; i <= 3; i++) {
        n = write(fd, big, size);
        if (n < 0)
            printf("write %d: -1 (%s)\n", i, strerror(errno));
        else
            printf("write %d: %zd of %zu bytes\n", i, n, size);
    }
    free(big);
    close(fd);
    close(server);
    return 0;
}
```

```console
$ ./nonblock
EAGAIN = 11, EWOULDBLOCK = 11
read:  -1 (Resource temporarily unavailable)
write 1: 2625195 of 67108864 bytes
write 2: -1 (Resource temporarily unavailable)
write 3: -1 (Resource temporarily unavailable)
$ ./nonblock-musl
EAGAIN = 11, EWOULDBLOCK = 11
read:  -1 (Resource temporarily unavailable)
write 1: 2625195 of 67108864 bytes
write 2: -1 (Resource temporarily unavailable)
write 3: -1 (Resource temporarily unavailable)
```

- The `read()` found nothing and returned `EAGAIN` at once. A blocking one would have waited for ever.
- The first `write()` of 64 MiB took about 2.5 MiB: what fits in the sender's send buffer plus the
  receiver's receive buffer. A blocking `write()` would have waited for someone to read the rest. The
  number depends on the kernel's settings (`/proc/sys/net/ipv4/tcp_wmem` and `tcp_rmem`), so it varies.
- Then the buffers were full, and every further `write()` failed with `EAGAIN`.

Both libraries print the same: the numbers come from the kernel, and their messages for `EAGAIN` match.
`EAGAIN` and `EWOULDBLOCK` are both 11 on Linux; POSIX allows them to differ, so portable code checks both.

`connect_to_self()` binds to port 0, which asks the kernel for any free port, and `getsockname()` says which
one it got. So the example can't clash with anything else on your machine.

With non-blocking sockets, nothing in the loop can wait by accident. The program sleeps only in `poll()`,
when there's nothing to do.

## poll()

```c
#include <poll.h>

struct pollfd {
    int   fd;         /* the descriptor to watch; negative: skip this entry */
    short events;     /* what you're interested in */
    short revents;    /* what happened: filled in by poll() */
};

int poll(struct pollfd fds[], nfds_t nfds, int timeout);
```

You fill in an array of `struct pollfd`, one per descriptor, and `poll()` fills in each `revents`. The
bits:

| bit | in `events` | in `revents` |
|---|---|---|
| `POLLIN` | wake me when there's something to read | `read()` won't block: data, end of file, or, on a listening socket, a connection to `accept()` |
| `POLLOUT` | wake me when I can write | `write()` won't block: there's room in the send buffer (maybe not for everything) |
| `POLLHUP` | (always reported) | hang-up: the connection is closed in both directions |
| `POLLERR` | (always reported) | an error is pending on the socket; the next `read()` or `write()` returns it |
| `POLLNVAL` | (always reported) | `fd` isn't an open descriptor: a bug in your program |

`timeout` is in milliseconds: `-1` waits for ever, `0` doesn't wait, anything else waits at most that long.
`poll()` returns how many entries have a non-zero `revents`, `0` if the time ran out, or `-1` with `errno`
set (then the `revents` fields mean nothing).

`poll()` is **level-triggered**: it reports the state **now**. While unread data sits in a socket, every
`poll()` says `POLLIN` again; read only part, and you hear about the rest next turn. That's forgiving: you
never lose an event by not handling it completely.

`struct pollfd` is 8 bytes on both C libraries, and the constants are the same: they're the kernel's.

## An echo server with poll

[Chapter 17](17-threads.md) built three echo servers: clients one at a time, a process per client, and a
thread per client. Here is the fourth design: one thread and `poll()`.

`examples/ch18/poll_echo.c`:

```c
/* poll_echo.c - an echo server with one thread and poll().
 * usage: poll_echo [PORT]
 * Every socket is non-blocking; the only place it waits is poll(). Each
 * client has an output buffer: what we read is appended there, and sent
 * when the socket is writable. */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define MAX_CLIENTS 1000
#define OUT_MAX (64 * 1024)     /* per client: stop reading when it is full */

struct client {
    int fd;
    bool eof;                   /* the client sent EOF: close once out is sent */
    size_t len;                 /* bytes waiting in out */
    char *out;                  /* OUT_MAX bytes */
};

static struct client clients[MAX_CLIENTS];
static size_t nclients;

static int set_nonblock(int fd)
{
    int flags = fcntl(fd, F_GETFL);
    if (flags < 0)
        return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static int listen_on(int port)
{
    struct sockaddr_in addr = { .sin_family = AF_INET };
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons((unsigned short)port);
    int on = 1;
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0 || setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof on) < 0
            || bind(fd, (struct sockaddr *)&addr, sizeof addr) < 0
            || listen(fd, 128) < 0 || set_nonblock(fd) < 0)
        return -1;
    return fd;
}

/* The listening socket is readable: accept everyone who is waiting. */
static void accept_all(int lfd)
{
    for (;;) {
        int fd = accept(lfd, NULL, NULL);
        if (fd < 0) {
            if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
                perror("accept");
            return;                     /* EAGAIN: nobody else is waiting */
        }
        char *out = malloc(OUT_MAX);
        if (nclients == MAX_CLIENTS || out == NULL || set_nonblock(fd) < 0) {
            free(out);
            close(fd);
            continue;
        }
        clients[nclients++] = (struct client){ .fd = fd, .out = out };
        printf("client %d connected (%zu in total)\n", fd, nclients);
    }
}

/* Close c as soon as possible: forget what it was still to be sent. */
static void drop(struct client *c)
{
    c->eof = true;
    c->len = 0;
}

/* c's socket is readable: read once, straight into its output buffer. */
static void on_readable(struct client *c)
{
    ssize_t n = read(c->fd, c->out + c->len, OUT_MAX - c->len);
    if (n > 0)
        c->len += (size_t)n;
    else if (n == 0)
        c->eof = true;                  /* close after sending the rest */
    else if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
        drop(c);
}

/* c's socket is writable: send what the kernel takes, keep the rest. */
static void on_writable(struct client *c)
{
    ssize_t n = write(c->fd, c->out, c->len);
    if (n > 0) {
        memmove(c->out, c->out + n, c->len - (size_t)n);
        c->len -= (size_t)n;
    } else if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
        drop(c);
    }
}

int main(int argc, char *argv[])
{
    int lfd = listen_on(argc > 1 ? atoi(argv[1]) : 6667);
    if (lfd < 0) {
        perror("listen");
        return 1;
    }
    signal(SIGPIPE, SIG_IGN);
    setvbuf(stdout, NULL, _IOLBF, 0);
    static struct pollfd fds[1 + MAX_CLIENTS];

    for (;;) {
        /* 1. What to wait for. */
        fds[0] = (struct pollfd){ .fd = lfd, .events = POLLIN };
        for (size_t i = 0; i < nclients; i++) {
            struct client *c = &clients[i];
            short events = 0;
            if (!c->eof && c->len < OUT_MAX)
                events |= POLLIN;       /* room for more: read */
            if (c->len > 0)
                events |= POLLOUT;      /* something to send: write */
            fds[1 + i] = (struct pollfd){ .fd = c->fd, .events = events };
        }
        size_t polled = nclients;

        /* 2. Wait until at least one of them is ready. */
        if (poll(fds, (nfds_t)(1 + polled), -1) < 0) {
            if (errno == EINTR)
                continue;
            perror("poll");
            return 1;
        }

        /* 3. Handle what is ready. */
        for (size_t i = 0; i < polled; i++) {
            short rev = fds[1 + i].revents;
            if (rev & (POLLERR | POLLHUP)) {
                drop(&clients[i]);      /* the connection is gone */
                continue;
            }
            if (rev & POLLIN)
                on_readable(&clients[i]);
            if (rev & POLLOUT)
                on_writable(&clients[i]);
        }
        if (fds[0].revents & POLLIN)
            accept_all(lfd);

        /* 4. Close the clients that are done, keeping the array packed. */
        for (size_t i = 0; i < nclients; ) {
            struct client *c = &clients[i];
            if (c->eof && c->len == 0) {
                close(c->fd);
                free(c->out);
                printf("client %d left (%zu in total)\n", c->fd, nclients - 1);
                *c = clients[--nclients];   /* move the last one here */
            } else {
                i++;
            }
        }
    }
}
```

How it works:

- Each client has an **output buffer**, `out`. Whatever `read()` returns goes straight into it, to be
  echoed. `on_writable()` sends from its front, and `memmove()` closes the gap. A short write is fine: the
  rest stays in `out` for the next turn.
- Step 1 rebuilds the `pollfd` array every turn. A client wants `POLLOUT` while it has something to send,
  and `POLLIN` while its buffer has room. The listening socket is entry 0.
- Step 3 handles the ready entries. `POLLERR` or `POLLHUP` means the connection is gone, with no one left
  to echo to. A `read()` of 0 means end of file; that client is closed only once its buffer is empty, so a
  client that sends its last line and then shuts down its side, like `nc -N`, still gets everything back.
- The listening socket is non-blocking too, and `accept_all()` accepts **until `EAGAIN`**: `POLLIN` on a
  listening socket means "at least one" connection is waiting.
- Every accepted socket needs its own `set_nonblock()`: on Linux, `accept()` doesn't copy `O_NONBLOCK`
  from the listening socket. (BSD systems do; POSIX leaves it open.)
- Step 4 closes finished clients, moving the last client into the gap to keep the array packed (order
  doesn't matter here). The loop handles only the first `polled` clients, the ones `poll()` saw; clients
  that `accept_all()` just added get their turn next time.

Try it with `nc` and with `clients.sh` from Chapter 17, which runs many clients at once:

```console
$ ./poll_echo > echo.log &
$ printf 'hello\nworld\n' | nc -N 127.0.0.1 6667
hello
world
$ ls /proc/$!/task | wc -l
1
$ bash ../ch17/clients.sh 6667 50
50 of 50 clients OK in 0.5 s
$ bash ../ch17/clients.sh 6667 500
500 of 500 clients OK in 0.6 s
$ head -n 3 echo.log
client 4 connected (1 in total)
client 4 left (0 in total)
client 4 connected (1 in total)
$ kill %1
```

The musl build prints the same. One task, no threads, and the 50 clients take 0.5 seconds, as with a thread
each. Each client sleeps half a second between two lines, so that's as fast as it gets. Five hundred take
hardly longer:

| server | serves clients with | 50 clients |
|---|---|---|
| `echo_iter.c` ([Chapter 17](17-threads.md)) | one at a time | 25.1 s |
| `echo_fork.c` | a process each | 0.5 s |
| `echo_threads.c` | a thread each | 0.5 s |
| `poll_echo.c` | one thread, `poll()` | 0.5 s |

Here's the loop as system calls. `strace -o` writes its trace to a file. `pid_on` finds the process
listening on a port, so `kill` stops the server, not `strace`:

```console
$ pid_on() { ss -tlnpH "sport = :$1" | grep -o "pid=[0-9]*" | cut -d= -f2; }
$ strace -o trace.txt ./poll_echo >/dev/null & sleep 0.5
$ printf 'hi\n' | nc -N 127.0.0.1 6667 >/dev/null; kill $(pid_on 6667); wait
$ sed -n '/^fcntl/,$p' trace.txt | grep -E '^(poll|accept|read|write|close|fcntl)'
fcntl(3, F_GETFL)                       = 0x2 (flags O_RDWR)
fcntl(3, F_SETFL, O_RDWR|O_NONBLOCK)    = 0
poll([{fd=3, events=POLLIN}], 1, -1)    = 1 ([{fd=3, revents=POLLIN}])
accept(3, NULL, NULL)                   = 4
fcntl(4, F_GETFL)                       = 0x2 (flags O_RDWR)
fcntl(4, F_SETFL, O_RDWR|O_NONBLOCK)    = 0
write(1, "client 4 connected (1 in total)\n", 32) = 32
accept(3, NULL, NULL)                   = -1 EAGAIN (Resource temporarily unavailable)
poll([{fd=3, events=POLLIN}, {fd=4, events=POLLIN}], 2, -1) = 1 ([{fd=4, revents=POLLIN}])
read(4, "hi\n", 65536)                  = 3
poll([{fd=3, events=POLLIN}, {fd=4, events=POLLIN|POLLOUT}], 2, -1) = 1 ([{fd=4, revents=POLLIN|POLLOUT}])
read(4, "", 65533)                      = 0
write(4, "hi\n", 3)                     = 3
close(4)                                = 0
write(1, "client 4 left (0 in total)\n", 27) = 27
poll([{fd=3, events=POLLIN}], 1, -1)    = ? ERESTART_RESTARTBLOCK (Interrupted by signal)
```

`sed` starts the listing at the first `fcntl()`, skipping the dynamic loader's start-up work. Every step
shows: `F_GETFL` on accepted socket 4 shows plain `O_RDWR`, no `O_NONBLOCK`; the second `accept()` ends
the accept loop with `EAGAIN`; once `out` holds `"hi\n"`, the next `poll()` asks for `POLLOUT` too. `kill`
interrupted the last `poll()` (`ERESTART_RESTARTBLOCK` is strace's name for that); with no handler,
SIGTERM then ended the process.

## Partial writes and backpressure

What if a client sends but never reads? Its echo can't be delivered. `poll_echo` keeps at most `OUT_MAX`
(64 KiB) of output per client. When that's full, it stops asking for `POLLIN` on that client, and reads no
more of its data until it has sent some.

Bash can open a TCP connection as a file descriptor: `exec 3<>/dev/tcp/HOST/PORT`. Then `yes` writes lines
into it as fast as it can, and nothing reads the echo:

```console
$ ./poll_echo > echo.log &
$ exec 3<>/dev/tcp/127.0.0.1/6667
$ timeout 2 yes 'I never read' >&3; echo "exit $?"
exit 124
$ ss -tnH 'sport = :6667 or dport = :6667'
ESTAB 2882541 1708416 127.0.0.1:6667  127.0.0.1:39022
ESTAB 126592  4190528 127.0.0.1:39022 127.0.0.1:6667
$ bash ../ch17/clients.sh 6667 50
50 of 50 clients OK in 0.5 s
$ exec 3>&-; kill %1
```

`yes` got stuck after a moment, and `timeout` ended it after 2 seconds (exit 124). `ss` shows where the
data is. The columns are the state, `Recv-Q` (received, not yet read by the program) and `Send-Q` (sent,
not yet acknowledged by the other side):

- Server side (`127.0.0.1:6667`): 2.9 MB from `yes` wait in the server's receive buffer, because the
  server stopped reading. 1.7 MB of echo wait in its send buffer.
- Client side: the receive buffer is full of unread echo (126 KB), and the send buffer is full of `yes`
  output that the server's full receive window won't let through (4.2 MB).
- In `poll_echo` itself: 64 KiB, no more.

Every buffer between `yes` and `poll_echo` filled up, and then `yes` blocked in `write()`. That's
**backpressure**: a receiver that stops reading makes the sender wait, and TCP's flow control carries it
all the way back to the source. Meanwhile the 50 other clients got their echo as usual: the loop never
waited for the stuck one. The numbers vary from run to run; the musl build shows the same picture.

Backpressure suits an echo server because the output comes from the same client: stop reading it, and it
stops causing output. A chat server can't do that. A slow client's pile of output comes from **other**
clients' messages. Stopping reading from everybody would let one slow client stop the room: Chapter 17's
weak spot again. The fair choice is to keep a limited amount for the slow client, and **disconnect** it
when it falls further behind: **slow-reader eviction**. `parleyd` does that, at 64 KiB.

The limit also bounds memory: at most 64 KiB of queued output per client, 64 MiB for a room of 1000.
Without it, one stuck client could make the server use unlimited memory.

## Framing, one byte at a time

TCP delivers a byte stream ([Chapter 14](14-networking-basics.md)): a line may arrive in pieces, and
several lines in one piece. Since [Chapter 8](08-memory.md), `parleyd` reads into a buffer and takes out
only **complete** lines (`msg_take_line()`), so how the bytes are cut up doesn't matter. In an event loop
that's even more important: a half line stays in the client's input buffer, and the loop serves others
until the rest arrives.

To test it, send a session one byte at a time. Each byte goes out in its own `write()`, and `TCP_NODELAY`
turns off Nagle's algorithm, which would otherwise collect tiny writes into bigger packets:

`examples/ch18/trickle.c`:

```c
/* trickle.c - send standard input to a server one byte at a time.
 * usage: trickle PORT [DELAY_MS]
 * Each byte goes out in its own write() and its own TCP segment, DELAY_MS
 * (default 1) apart: a test for the server's framing. Then it reads and
 * prints the replies until the server closes the connection. */
#define _POSIX_C_SOURCE 200809L
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

int main(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr, "usage: trickle PORT [DELAY_MS]\n");
        return 2;
    }
    long ms = argc > 2 ? atol(argv[2]) : 1;
    struct timespec delay = { ms / 1000, ms % 1000 * 1000000L };

    struct sockaddr_in addr = { .sin_family = AF_INET };
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons((unsigned short)atoi(argv[1]));
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0 || connect(fd, (struct sockaddr *)&addr, sizeof addr) < 0) {
        perror("connect");
        return 1;
    }
    /* Without this, TCP would collect our tiny writes into bigger segments
     * (Nagle's algorithm). */
    int on = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &on, sizeof on);

    int ch;
    while ((ch = getchar()) != EOF) {
        unsigned char b = (unsigned char)ch;
        if (write(fd, &b, 1) != 1) {
            perror("write");
            return 1;
        }
        nanosleep(&delay, NULL);
    }
    shutdown(fd, SHUT_WR);              /* "that's all": the server reads EOF */

    char buf[4096];
    ssize_t n;
    while ((n = read(fd, buf, sizeof buf)) > 0)
        fwrite(buf, 1, (size_t)n, stdout);
    close(fd);
    return 0;
}
```

In `project/ch18`, after `make`, run the finished server under `strace -e trace=read` (only `read()`
calls) and trickle a session into it:

```console
$ strace -e trace=read -o trace.txt build/glibc/parleyd -p 6667 2>/dev/null &
$ printf 'NICK alice\r\nPING :x\r\nQUIT\r\n' | ../../examples/ch18/trickle 6667; echo "exit $?"
:parley 001 alice :Welcome to parley, alice
:alice JOIN
:parley 353 alice :alice
:parley 366 alice :End of NAMES
:parley PONG :x
ERROR :Goodbye
exit 0
$ kill $(pid_on 6667); wait
$ grep -c '^read(6' trace.txt
27
$ grep '^read(6' trace.txt | head -n 4
read(6, "N", 4096)                      = 1
read(6, "I", 4096)                      = 1
read(6, "C", 4096)                      = 1
read(6, "K", 4096)                      = 1
```

27 bytes, 27 one-byte reads, and exactly the right replies. The musl build does the same. The checkpoints
below show the opposite: two commands in **one** `read()`.

## Timers

A server must also act when **nothing** happens: drop a client that connected and never registered, or
ask a silent one whether it's still there. The event loop has no other thread to watch the clock, and needs
none. `poll()`'s timeout is the timer:

1. For every client, work out its next deadline.
2. Pass `poll()` the time until the earliest one: `timeout = earliest - now`. With no deadlines, `-1`.
3. After `poll()` returns, for any reason, handle every deadline that has passed.

`poll()` then wakes when data arrives or the next timer is due, whichever comes first.

"Now" must come from the right clock. `time()` and `CLOCK_REALTIME` give the time of day, which can
**jump** when the administrator sets the clock or NTP corrects it. A deadline 60 seconds away could
suddenly be an hour away, or past. `CLOCK_MONOTONIC` never jumps: it counts steadily up from some fixed
point, often the boot. Its values mean nothing as dates, but the difference between two is an honest
duration:

```c
struct timespec ts;
clock_gettime(CLOCK_MONOTONIC, &ts);
long long ms = (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
```

`clock_gettime()` is POSIX, in `<time.h>`. (glibc before 2.17 needed `-lrt` for it; now it's in libc on
both libraries.) Both read this clock through the kernel's vDSO, a little piece of kernel code mapped into
every process, so `strace` shows no system call for it.

Checking every client every turn costs time in proportion to the number of clients: nothing for a
thousand. Servers with a hundred thousand keep their deadlines in a heap or a "timer wheel", to find the
earliest at once.

## Signals and the event loop

The server should shut down cleanly on Ctrl-C or SIGTERM. The handler sets a flag, as in
[Chapter 13](13-processes-and-signals.md), and the loop checks it. But the loop spends its time in
`poll()`. Does the signal wake it? This program finds out:

`examples/ch18/pollsig.c`:

```c
/* pollsig.c - poll() and signals: EINTR, a lost wake-up, and the self-pipe. */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static volatile sig_atomic_t got_signal;
static int wake_write = -1;             /* the self-pipe's write end, or -1 */

static void on_signal(int signo)
{
    int saved = errno;
    got_signal = signo;
    if (wake_write >= 0) {
        ssize_t r = write(wake_write, "!", 1);
        (void)r;
    }
    errno = saved;
}

static double now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + ts.tv_nsec / 1e9;
}

static void report(const char *what, int n, double start)
{
    printf("%-32s poll() = %2d (%s) after %.1f s\n", what, n,
           n < 0 ? strerror(errno) : n == 0 ? "timeout" : "ready", now() - start);
}

int main(void)
{
    struct sigaction sa = { .sa_handler = on_signal, .sa_flags = SA_RESTART };
    sigemptyset(&sa.sa_mask);
    sigaction(SIGALRM, &sa, NULL);
    sigaction(SIGUSR1, &sa, NULL);

    /* 1. A signal during poll(): EINTR, although we asked for SA_RESTART. */
    double start = now();
    alarm(1);
    int n = poll(NULL, 0, 5000);
    report("1. SIGALRM during poll:", n, start);

    /* 2. A signal just BEFORE poll(): it sets the flag, but poll() does
     *    not know, and sleeps until its timeout. raise() plays the part
     *    of a signal that arrives at the worst moment. */
    got_signal = 0;
    start = now();
    if (got_signal == 0) {
        raise(SIGUSR1);                 /* ... right here */
        n = poll(NULL, 0, 2000);
    }
    report("2. SIGUSR1 just before poll:", n, start);

    /* 3. The same with a self-pipe: the handler's byte waits in the pipe,
     *    so poll() returns at once. */
    int p[2];
    if (pipe(p) < 0)
        return 1;
    fcntl(p[1], F_SETFL, O_NONBLOCK);
    wake_write = p[1];
    got_signal = 0;
    start = now();
    struct pollfd fds[1] = { { .fd = p[0], .events = POLLIN } };
    if (got_signal == 0) {
        raise(SIGUSR1);
        n = poll(fds, 1, 2000);
    }
    report("3. the same, with a self-pipe:", n, start);
    return 0;
}
```

```console
$ ./pollsig
1. SIGALRM during poll:          poll() = -1 (Interrupted system call) after 1.0 s
2. SIGUSR1 just before poll:     poll() =  0 (timeout) after 2.0 s
3. the same, with a self-pipe:   poll() =  1 (ready) after 0.0 s
$ ./pollsig-musl
1. SIGALRM during poll:          poll() = -1 (Interrupted system call) after 1.0 s
2. SIGUSR1 just before poll:     poll() =  0 (timeout) after 2.0 s
3. the same, with a self-pipe:   poll() =  1 (ready) after 0.0 s
```

1. A signal **during** `poll()` interrupts it with `EINTR`, even though the handler has `SA_RESTART`.
   Linux never restarts `poll()`, whatever the flags. So an event loop must always handle `EINTR` from
   `poll()`: check the flags and go round again.
2. A signal **just before** `poll()` is the problem. The loop checked the flag (still 0), the signal came
   and set it, and `poll()` went to sleep, not knowing. Here `poll()` had a 2-second timeout; with `-1` it
   would have slept until some client did something. The window is tiny, so the bug shows up once in a
   blue moon: the worst kind. `raise()` puts the signal right into the window to show it every time.
3. The **self-pipe trick** from Chapter 13 closes the window. The handler also writes a byte into a pipe,
   and the loop polls the pipe's read end with everything else. A signal before `poll()` leaves its byte in
   the pipe, so `poll()` returns at once.

Other ways: `pselect()` (POSIX) and `ppoll()` (Linux; POSIX only since 2024) swap in a signal mask
atomically as they start to wait, so a signal blocked until then can only arrive **during** the wait.
Linux's `signalfd()` turns signals into a descriptor to read. The self-pipe works everywhere, with any kind
of wait, using just `pipe()` and `write()`. `parleyd` uses it.

## select and epoll

`poll()` has an older sibling and a younger, Linux-only cousin.

**`select()`** (4.2BSD, 1983) takes three **bit sets** of type `fd_set`: descriptors to watch for reading,
writing and errors. You fill them with the macros `FD_ZERO`, `FD_SET` and `FD_ISSET`, and `select()`
overwrites them with the ready ones, so you rebuild them before every call. The catch: an `fd_set` has a
fixed size, `FD_SETSIZE` bits, 1024 on Linux with both libraries. A server with more than about a thousand
clients gets descriptor numbers that **don't fit**, and `FD_SET` on them writes outside the set.
[Below](#glibc-vs-musl-select-past-fd_setsize) you'll see what each library does then. `poll()` takes an
array of any length, so it has no such limit. Use `poll()`.

**`epoll`** (Linux 2.6, 2002) fixes `poll()`'s cost. `poll()` passes the whole array into the kernel every
call, and the kernel checks every entry, even if only one of 10,000 clients is active. `epoll` keeps the
list **in the kernel**:

- `epoll_create1(0)` makes an epoll instance, itself a descriptor.
- `epoll_ctl()` adds a descriptor to its interest list (`EPOLL_CTL_ADD`), changes what to watch for
  (`EPOLL_CTL_MOD`), or removes it (`EPOLL_CTL_DEL`). Each comes with a value you choose, often a pointer
  to your own data, that comes back with its events.
- `epoll_wait()` returns **only the ready ones**, as an array of `struct epoll_event`.

Here is `poll_echo.c` ported to it:

`examples/ch18/epoll_echo.c`:

```c
/* epoll_echo.c - poll_echo.c ported to Linux's epoll (level-triggered).
 * usage: epoll_echo [PORT]
 * Linux only: epoll is not POSIX. The kernel keeps the list of sockets we
 * are interested in, so each wait no longer passes (and scans) all of them. */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#define OUT_MAX (64 * 1024)

struct client {
    int fd;
    bool eof;
    uint32_t events;            /* what epoll watches for now */
    size_t len;
    char out[OUT_MAX];
};

static int ep;                  /* the epoll instance */
static size_t nclients;

static int set_nonblock(int fd)
{
    int flags = fcntl(fd, F_GETFL);
    if (flags < 0)
        return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static int listen_on(int port)
{
    struct sockaddr_in addr = { .sin_family = AF_INET };
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons((unsigned short)port);
    int on = 1;
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0 || setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof on) < 0
            || bind(fd, (struct sockaddr *)&addr, sizeof addr) < 0
            || listen(fd, 128) < 0 || set_nonblock(fd) < 0)
        return -1;
    return fd;
}

/* Tell epoll what c waits for now, if that changed. */
static void update(struct client *c)
{
    uint32_t want = 0;
    if (!c->eof && c->len < OUT_MAX)
        want |= EPOLLIN;
    if (c->len > 0)
        want |= EPOLLOUT;
    if (want != c->events) {
        struct epoll_event ev = { .events = want, .data.ptr = c };
        epoll_ctl(ep, EPOLL_CTL_MOD, c->fd, &ev);
        c->events = want;
    }
}

static void accept_all(int lfd)
{
    for (;;) {
        int fd = accept(lfd, NULL, NULL);
        if (fd < 0)
            return;
        struct client *c = malloc(sizeof *c);
        struct epoll_event ev = { .events = EPOLLIN, .data.ptr = c };
        if (c == NULL || set_nonblock(fd) < 0
                || epoll_ctl(ep, EPOLL_CTL_ADD, fd, &ev) < 0) {
            free(c);
            close(fd);
            continue;
        }
        c->fd = fd;
        c->eof = false;
        c->events = EPOLLIN;
        c->len = 0;
        printf("client %d connected (%zu in total)\n", fd, ++nclients);
    }
}

static void drop(struct client *c)
{
    c->eof = true;
    c->len = 0;
}

static void handle(struct client *c, uint32_t rev)
{
    if (rev & (EPOLLERR | EPOLLHUP)) {
        drop(c);
    } else {
        if (rev & EPOLLIN) {
            ssize_t n = read(c->fd, c->out + c->len, OUT_MAX - c->len);
            if (n > 0)
                c->len += (size_t)n;
            else if (n == 0)
                c->eof = true;
            else if (errno != EAGAIN && errno != EINTR)
                drop(c);
        }
        if (rev & EPOLLOUT) {
            ssize_t n = write(c->fd, c->out, c->len);
            if (n > 0) {
                memmove(c->out, c->out + n, c->len - (size_t)n);
                c->len -= (size_t)n;
            } else if (n < 0 && errno != EAGAIN && errno != EINTR) {
                drop(c);
            }
        }
    }
    if (c->eof && c->len == 0) {
        epoll_ctl(ep, EPOLL_CTL_DEL, c->fd, NULL);
        close(c->fd);
        printf("client %d left (%zu in total)\n", c->fd, --nclients);
        free(c);
    } else {
        update(c);
    }
}

int main(int argc, char *argv[])
{
    int lfd = listen_on(argc > 1 ? atoi(argv[1]) : 6667);
    ep = epoll_create1(0);
    if (lfd < 0 || ep < 0) {
        perror("setup");
        return 1;
    }
    signal(SIGPIPE, SIG_IGN);
    setvbuf(stdout, NULL, _IOLBF, 0);
    /* data.ptr NULL marks the listening socket. */
    struct epoll_event ev = { .events = EPOLLIN, .data.ptr = NULL };
    epoll_ctl(ep, EPOLL_CTL_ADD, lfd, &ev);

    for (;;) {
        struct epoll_event ready[64];
        int n = epoll_wait(ep, ready, 64, -1);   /* only the ready ones */
        if (n < 0 && errno != EINTR) {
            perror("epoll_wait");
            return 1;
        }
        for (int i = 0; i < n; i++) {
            if (ready[i].data.ptr == NULL)
                accept_all(lfd);
            else
                handle(ready[i].data.ptr, ready[i].events);
        }
    }
}
```

- `data.ptr` leads straight from an event to its `struct client`: no searching, no array to rebuild.
- `update()` calls `epoll_ctl(EPOLL_CTL_MOD)` only when what the client waits for **changes**. `poll()`
  is told everything every turn; epoll only the changes.
- `epoll_echo` echoes and survives `clients.sh` exactly like `poll_echo`, on both libraries.

That's **level-triggered** epoll, the default: like `poll()`, it reports a socket as long as it's ready.
With `EPOLLET`, epoll is **edge-triggered**: it reports a socket once when it **becomes** ready, and not
again until something new happens. That saves some wake-ups, but if you don't read and write until
`EAGAIN`, leftover data can sit there with no new event to announce it, and the connection hangs. Exercise
1 shows that.

| | `select()` | `poll()` | `epoll` |
|---|---|---|---|
| standard | POSIX | POSIX | Linux only |
| descriptors | below `FD_SETSIZE` (1024) | any number | any number |
| per call | rebuild 3 bit sets | pass the whole array | only the ready ones come back |
| cost per wait | grows with the highest fd number | grows with the number of descriptors | grows with the number of **ready** descriptors |

The BSDs and macOS have `kqueue`, their equivalent of epoll. Libraries such as libevent and libuv hide the
differences behind one interface.

For `parleyd`, `poll()` is right: it's POSIX, the room holds at most 1000 clients, and scanning 1000
`pollfd`s takes microseconds. Choose epoll for tens of thousands of connections with few active at a time.

## Why no locks

In Chapter 17, any thread could run at any moment, so all shared data needed a lock. An event loop has
**one** thread. A handler runs from start to end with nothing else happening in the program, so no code can
see the room half-changed. No data races, no mutexes, no deadlocks. The code is simpler to write, test and
debug, because it does the same thing every run.

The price:

- **Never block in a handler.** While one handler runs, every other client waits. A `sleep()`, a slow DNS
  lookup, a long computation: the whole server stands still. This is **head-of-line blocking**; Exercise 2
  measures it. Anything that could wait must be non-blocking, or happen elsewhere.
- **One CPU.** One thread uses one core. A chat server is limited by the network, not the CPU, so that's
  fine. Servers that need more cores run one event loop per core, or hand slow work to worker threads that
  report back through a pipe, like the self-pipe.

## glibc vs musl

Both libraries pass the event loop's calls to the kernel almost unchanged. The differences are in the
corners.

### glibc vs musl: select() past FD_SETSIZE

`examples/ch18/fdset.c` puts descriptor 1500 into an `fd_set`, which has room for 1024:

```c
/* fdset.c - select()'s fd_set has room for FD_SETSIZE descriptors, no more. */
#define _POSIX_C_SOURCE 200809L
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/select.h>
#include <unistd.h>

int main(void)
{
    printf("FD_SETSIZE = %d, sizeof(fd_set) = %zu, sizeof(struct pollfd) = %zu\n",
           FD_SETSIZE, sizeof(fd_set), sizeof(struct pollfd));
    fflush(stdout);                     /* out before a possible crash */

    /* A busy server easily has descriptor numbers above 1024. Raise our
     * limit if we may, and make one: a copy of standard input. */
    struct rlimit rl;
    getrlimit(RLIMIT_NOFILE, &rl);
    if (rl.rlim_cur < 2000 && rl.rlim_max >= 2000) {
        rl.rlim_cur = 2000;
        setrlimit(RLIMIT_NOFILE, &rl);
    }
    int fd = dup2(0, 1500);
    if (fd < 0) {
        perror("dup2");
        return 1;
    }

    struct {
        fd_set set;
        unsigned char after[256];       /* whatever happens to come next */
    } s;
    memset(s.after, 0, sizeof s.after);
    FD_ZERO(&s.set);
    FD_SET(fd, &s.set);                 /* bit 1500 of a 1024-bit set! */

    for (size_t i = 0; i < sizeof s.after; i++)
        if (s.after[i] != 0)
            printf("FD_SET(%d) changed the byte %zu bytes after the fd_set\n",
                   fd, i);
    return 0;
}
```

`getrlimit()` and `setrlimit()` read and change the process's limits; `RLIMIT_NOFILE` is the most open
descriptors it may have (`ulimit -n` in the shell). `dup2(0, 1500)` makes descriptor 1500 a copy of
standard input. Built as usual, without optimization:

```console
$ ./fdset
FD_SETSIZE = 1024, sizeof(fd_set) = 128, sizeof(struct pollfd) = 8
FD_SET(1500) changed the byte 59 bytes after the fd_set
$ ./fdset-musl
FD_SETSIZE = 1024, sizeof(fd_set) = 128, sizeof(struct pollfd) = 8
FD_SET(1500) changed the byte 59 bytes after the fd_set
```

Bit 1500 is in byte 187; the set is 128 bytes, so `FD_SET` wrote 59 bytes past its end, into whatever lies
there: in a real program, some other variable, silently changed. Now with `-O2`:

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -O2 -o fdset fdset.c
$ musl-gcc -std=c99 -Wall -Wextra -pedantic -static -O2 -o fdset-musl fdset.c
$ ./fdset
FD_SETSIZE = 1024, sizeof(fd_set) = 128, sizeof(struct pollfd) = 8
*** buffer overflow detected ***: terminated
Aborted (core dumped)
$ ./fdset-musl
FD_SETSIZE = 1024, sizeof(fd_set) = 128, sizeof(struct pollfd) = 8
```

- **glibc** with `-O2`: Ubuntu's gcc turns on `_FORTIFY_SOURCE` when optimizing (see
  [Chapter 6](06-strings.md)), and glibc's fortified `FD_SET` checks the descriptor number. The program
  stops at once, before any damage.
- **musl** has no fortify checks. With `-O2` the program prints nothing: writing outside the set is
  undefined behaviour, so the compiler assumed it can't touch `after`, just zeroed, and dropped the check.
  The write still happened somewhere.

glibc is better here: a loud crash at the bug beats silent corruption. But it only helps in optimized,
fortified builds. The real cure works on both: don't use `select()` in a server.

### glibc vs musl: POLLRDHUP and the feature macros

Linux has one more `poll()` bit, `POLLRDHUP`: the other side has shut down its writing half (sent FIN).
It's not POSIX. With our flags, `-std=c99 -D_POSIX_C_SOURCE=200809L`, glibc hides it and musl shows it:

```c
#define _POSIX_C_SOURCE 200809L
#include <poll.h>
#include <stdio.h>

int main(void)
{
    printf("POLLRDHUP = %#x\n", POLLRDHUP);
    return 0;
}
```

```console
$ gcc -std=c99 -Wall -Wextra -pedantic rdhup.c -o rdhup
rdhup.c: In function ‘main’:
rdhup.c:7:33: error: ‘POLLRDHUP’ undeclared (first use in this function); did you mean ‘POLLHUP’?
    7 |     printf("POLLRDHUP = %#x\n", POLLRDHUP);
      |                                 ^~~~~~~~~
      |                                 POLLHUP
rdhup.c:7:33: note: each undeclared identifier is reported only once for each function it appears in
$ musl-gcc -std=c99 -Wall -Wextra -pedantic -static rdhup.c -o rdhup-musl && ./rdhup-musl
POLLRDHUP = 0x2000
$ gcc -std=c99 -Wall -Wextra -pedantic -D_GNU_SOURCE rdhup.c -o rdhup && ./rdhup
POLLRDHUP = 0x2000
```

glibc wants `_GNU_SOURCE` for it, and for `ppoll()` (musl wants `_DEFAULT_SOURCE` or `_GNU_SOURCE` for
that one). glibc's strictness is more useful: it tells you at compile time that your code is no longer
portable POSIX. We don't need `POLLRDHUP` anyway: `read()` returning 0 says the same on every system.

### glibc vs musl: epoll under strace

The programs behave the same on both, but `strace` shows small differences in how they talk to the kernel.
The same `printf 'hi\n' | nc -N` session as above, against `epoll_echo` and `epoll_echo-musl`:

```console
$ sed -n '/^fcntl/,$p' trace.txt | grep -E '^(epoll|fcntl|write)'      # glibc
fcntl(3, F_GETFL)                       = 0x2 (flags O_RDWR)
fcntl(3, F_SETFL, O_RDWR|O_NONBLOCK)    = 0
epoll_create1(0)                        = 4
epoll_ctl(4, EPOLL_CTL_ADD, 3, {events=EPOLLIN, data={u32=0, u64=0}}) = 0
epoll_wait(4, [{events=EPOLLIN, data={u32=0, u64=0}}], 64, -1) = 1
fcntl(5, F_GETFL)                       = 0x2 (flags O_RDWR)
fcntl(5, F_SETFL, O_RDWR|O_NONBLOCK)    = 0
epoll_ctl(4, EPOLL_CTL_ADD, 5, {events=EPOLLIN, data={u32=3252019872, u64=106084649263776}}) = 0
write(1, "client 5 connected (1 in total)\n", 32) = 32
...
$ sed -n '/^fcntl/,$p' trace.txt | grep -E '^(epoll|fcntl|write)'      # musl
fcntl(3, F_GETFL)                       = 0x2 (flags O_RDWR)
fcntl(3, F_SETFL, O_RDWR|O_NONBLOCK|O_LARGEFILE) = 0
epoll_create1(0)                        = 4
epoll_ctl(4, EPOLL_CTL_ADD, 3, {events=EPOLLIN, data={u32=0, u64=0}}) = 0
epoll_pwait(4, [{events=EPOLLIN, data={u32=0, u64=0}}], 64, -1, NULL, 8) = 1
fcntl(5, F_GETFL)                       = 0x2 (flags O_RDWR)
fcntl(5, F_SETFL, O_RDWR|O_NONBLOCK|O_LARGEFILE) = 0
epoll_ctl(4, EPOLL_CTL_ADD, 5, {events=EPOLLIN, data={u32=847159328, u64=128304405192736}}) = 0
writev(1, [{iov_base="client 5 connected (1", iov_len=21}, {iov_base=" in total)\n", iov_len=11}], 2) = 32
...
```

- `epoll_wait()`: glibc makes the `epoll_wait` system call; musl implements it as `epoll_pwait()` with no
  signal mask (`NULL`). Same result.
- `F_SETFL`: musl's `fcntl()` always adds `O_LARGEFILE`, which on 64-bit Linux changes nothing.
- `printf()`: glibc writes with `write()`; musl's stdio always uses `writev()`, here with the buffered part
  and the rest of the line as two pieces.
- `data.ptr` values are addresses, and differ from run to run.

Nothing to choose between them: each call does the same work. glibc's trace reads closer to your source,
which helps while you learn to read traces.

## Building parleyd: server v3

Server v3 replaces the threads with one event loop in a new file, `server.c`. The design rules:

> Every socket is non-blocking, and the only place the server waits is `poll()`. Handlers never write to a
> socket: `conn_send()` appends to the client's **output queue**, and the loop writes the queues when the
> sockets are ready. A client whose queue would grow past 64 KiB is disconnected.

`room.c` doesn't change. Chapter 17 kept it free of threads; its handlers call `conn_send()`, and whether
that writes or queues is `conn.c`'s business. `parleyd.c` shrinks to options and `main()`.

Two parts:

1. The event loop: output queues, `server.c`, the self-pipe and a clean shutdown. A complete server.
2. Timers: idle clients get a `PING`, and are dropped if they don't answer; `-t IDLE` sets the time.

### Part 1: the event loop

#### The Makefile

The server has no threads any more. Delete the `-pthread` lines Chapter 17 added below the `CFLAGS` lines
(the comment, `CFLAGS += -pthread` and `LDFLAGS += -pthread`). Then add the new files `server.o` and
`clock.o` to the server's objects:

```make
SERVER_OBJS := $(addprefix $(BUILD)/, parleyd.o server.o room.o conn.o clock.o io.o net.o sig.o msg.o nick.o strbuf.o str.o log.o)
```

#### src/clock.h and src/clock.c

A new module with one function: milliseconds on the monotonic clock (see [Timers](#timers)). Part 1 uses
it for the shutdown grace time, Part 2 for the idle timers.

```c
/* clock.h - a clock for timeouts. */
#ifndef PARLEY_CLOCK_H
#define PARLEY_CLOCK_H

/* Milliseconds since some fixed point in the past. Unlike the time of day,
 * this clock never jumps (CLOCK_MONOTONIC), so it is right for measuring
 * how long something took or how long to wait. */
long long clock_ms(void);

#endif
```

```c
/* clock.c - a clock for timeouts. */
#include "clock.h"

#include <time.h>

long long clock_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);   /* cannot fail with these arguments */
    return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}
```

#### src/conn.h

A connection gets an output queue, `out`, a `strbuf` like its input buffer `in`. The states keep their
names but change meaning. `CONN_CLOSING` used to mean "we sent ERROR"; now it means "close it once the
queue is sent", which is how `ERROR :Goodbye` reaches a client that quits. `CONN_DEAD` throws the queue
away. `reason` remembers why a client is closed, for the `QUIT` the others get. Replace the whole file:

```c
/* conn.h - one client connection, as the server sees it. */
#ifndef PARLEY_CONN_H
#define PARLEY_CONN_H

#include <stdbool.h>

#include "msg.h"
#include "net.h"
#include "nick.h"
#include "strbuf.h"

#define SERVER_NAME "parley"   /* the prefix of messages from the server */

/* The most output we keep for a client that does not read it. A client
 * that falls further behind is disconnected ("slow reader eviction"):
 * otherwise one stuck client could make the server use unlimited memory. */
#define OUTQ_MAX (64 * 1024)

enum conn_state {
    CONN_OPEN,       /* normal operation */
    CONN_CLOSING,    /* stop reading; close once the output queue is sent */
    CONN_DEAD        /* close it now, throwing away unsent output */
};

struct conn {
    int fd;                    /* the client's socket (non-blocking) */
    char addr[NET_ADDR_MAX];   /* the client's address, for log messages */
    char nick[NICK_MAX + 1];   /* "" until the client registers */
    enum conn_state state;
    char reason[64];           /* why we close it: for the QUIT the others see */
    struct strbuf in;          /* bytes received but not yet handled */
    struct strbuf out;         /* bytes waiting to be sent: the output queue */
    bool skipping;             /* throwing away a too-long line */
};

/* The name to use in replies: the nick, or "*" before registration. */
const char *conn_target(const struct conn *c);

/* Queue one message for the client; it is sent when the socket is ready
 * (see conn_flush). If the queue would grow beyond OUTQ_MAX, the client is
 * dropped instead. */
void conn_send(struct conn *c, const struct msg *m);

/* Send ":parley CODE <target> [param] :text", the server's reply format. */
void conn_numeric(struct conn *c, const char *code, const char *param,
                  const char *text);

/* Write as much of the output queue as the socket takes now, without
 * blocking. On a write error the client is dropped. */
void conn_flush(struct conn *c);

/* Close c once its output queue is sent (state CONN_CLOSING). */
void conn_close(struct conn *c, const char *reason);

/* Close c right away (state CONN_DEAD). */
void conn_drop(struct conn *c, const char *reason);

#endif
```

#### src/conn.c

`conn_send()` no longer writes. It formats the message and appends it to the queue, unless the queue would
grow past `OUTQ_MAX`; then the client is dropped: slow-reader eviction in five lines. `conn_flush()` writes
until the queue is empty or the socket says `EAGAIN`; `sb_consume()` (from [Chapter 8](08-memory.md))
removes what was written from the front, so a short write keeps the rest.

`conn_close()` and `conn_drop()` only change the state. Nothing is closed or freed until the end of the
loop's turn, so no code uses a `struct conn` that was just freed. `set_closed()` keeps the **first**
reason: if a client quits and its queue then overflows, the others still see its own goodbye. Replace the
whole file:

```c
/* conn.c - one client connection, as the server sees it. */
#include "conn.h"

#include <errno.h>
#include <string.h>
#include <unistd.h>

#include "log.h"
#include "str.h"

const char *conn_target(const struct conn *c)
{
    return c->nick[0] != '\0' ? c->nick : "*";
}

void conn_send(struct conn *c, const struct msg *m)
{
    char line[MSG_LINE_MAX + 1];
    if (c->state == CONN_DEAD)
        return;
    int n = msg_format(line, sizeof line, m);
    if (n < 0) {
        log_msg(LOG_WARN, "message too long to send: %s", m->command);
        return;
    }
    if (c->out.len + (size_t)n > OUTQ_MAX) {
        log_msg(LOG_WARN, "%s: output queue full, disconnecting", conn_target(c));
        conn_drop(c, "Output queue full");
        return;
    }
    if (sb_append(&c->out, line, (size_t)n) < 0) {
        log_msg(LOG_ERROR, "%s: out of memory", conn_target(c));
        conn_drop(c, "Out of memory");
    }
}

void conn_numeric(struct conn *c, const char *code, const char *param,
                  const char *text)
{
    struct msg m = {
        .prefix = SERVER_NAME,
        .command = code,
        .nparams = param != NULL ? 2 : 1,
        .params = { conn_target(c), param },
        .trailing = text,
    };
    conn_send(c, &m);
}

void conn_flush(struct conn *c)
{
    while (c->state != CONN_DEAD && c->out.len > 0) {
        ssize_t n = write(c->fd, c->out.data, c->out.len);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                return;                /* socket buffer full: wait for POLLOUT */
            log_msg(LOG_DEBUG, "writing to %s: %s", c->addr, strerror(errno));
            conn_drop(c, strerror(errno));
            return;
        }
        sb_consume(&c->out, (size_t)n);  /* a short write keeps the rest */
    }
}

static void set_closed(struct conn *c, enum conn_state state, const char *reason)
{
    if (c->state == CONN_OPEN)         /* keep the first reason */
        str_copy(c->reason, reason, sizeof c->reason);
    if (state > c->state)              /* OPEN -> CLOSING -> DEAD, never back */
        c->state = state;
}

void conn_close(struct conn *c, const char *reason)
{
    set_closed(c, CONN_CLOSING, reason);
}

void conn_drop(struct conn *c, const char *reason)
{
    set_closed(c, CONN_DEAD, reason);
}
```

`write_all()` is gone: it blocked until everything was written, and that stopped server v2.

#### src/sig.h and src/sig.c

The self-pipe from [Chapter 13](13-processes-and-signals.md) joins the signal module. `sig_wake_fd()`
creates the pipe; the handler writes a byte into it. Both ends are non-blocking: the handler must never
block, and a full pipe already holds a wake-up. The handler also saves and restores `errno`: it may
interrupt the main loop between a failing call and the line that checks `errno`, and its `write()` could
change it. Replace both files:

```c
/* sig.h - signal handling shared by server and client. */
#ifndef PARLEY_SIG_H
#define PARLEY_SIG_H

#include <signal.h>

/* The number of the SIGINT or SIGTERM that asked us to quit, or 0. The
 * handler sets it; the main loop checks it when a call fails with EINTR. */
extern volatile sig_atomic_t sig_quit;

/* Ignore SIGPIPE (a write to a closed connection then fails with EPIPE
 * instead of killing the process) and make SIGINT and SIGTERM set sig_quit.
 * Returns 0, or -1 with errno set. */
int sig_init(void);

/* The self-pipe trick: create a pipe, and make the SIGINT/SIGTERM handler
 * also write a byte into it. Returns the read end (non-blocking), for the
 * caller to poll(): it becomes readable when a quit signal arrives, so
 * poll() wakes up even if the signal came just before poll() was called.
 * Returns -1 with errno set on failure. Call after sig_init(). */
int sig_wake_fd(void);

/* Read and discard everything in the self-pipe. */
void sig_drain(int fd);

#endif
```

```c
/* sig.c - signal handling shared by server and client. */
#include "sig.h"

#include <errno.h>
#include <stddef.h>
#include <unistd.h>

#include "io.h"

volatile sig_atomic_t sig_quit = 0;

/* The write end of the self-pipe, or -1. */
static int wake_write_fd = -1;

/* A signal handler may run between any two instructions of the program,
 * so it must do almost nothing: here, set a flag and write one byte.
 * Only async-signal-safe functions may be called in a handler (write is,
 * printf and malloc are not), and it must not change errno behind the
 * back of the code it interrupted. */
static void on_quit(int signo)
{
    int saved = errno;
    sig_quit = signo;
    if (wake_write_fd >= 0) {
        /* If the pipe is full, a wake-up is already pending: fine. */
        ssize_t r = write(wake_write_fd, "!", 1);
        (void)r;
    }
    errno = saved;
}

int sig_init(void)
{
    struct sigaction sa;

    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sa.sa_handler = SIG_IGN;
    if (sigaction(SIGPIPE, &sa, NULL) < 0)
        return -1;

    /* No SA_RESTART in sa_flags: a blocking read() interrupted by the
     * signal fails with EINTR, so the main loop wakes up and sees sig_quit. */
    sa.sa_handler = on_quit;
    if (sigaction(SIGINT, &sa, NULL) < 0 || sigaction(SIGTERM, &sa, NULL) < 0)
        return -1;
    return 0;
}

int sig_wake_fd(void)
{
    int fds[2];
    if (pipe(fds) < 0)
        return -1;
    /* Both ends non-blocking: the handler must never block in write(),
     * and sig_drain() reads until the pipe is empty. */
    if (set_nonblock(fds[0]) < 0 || set_nonblock(fds[1]) < 0) {
        int saved = errno;
        close(fds[0]);
        close(fds[1]);
        errno = saved;
        return -1;
    }
    wake_write_fd = fds[1];
    return fds[0];
}

void sig_drain(int fd)
{
    char buf[64];
    while (read(fd, buf, sizeof buf) > 0)
        continue;
}
```

#### tests

Add a test to `tests/test_sig.c`. `raise()` runs the handler at once, so the test can check that the pipe
is empty, then has a byte, then is empty again after `sig_drain()`:

```c
void test_sig_wake_fd(void)
{
    CHECK_INT(sig_init(), 0);
    int fd = sig_wake_fd();
    CHECK(fd >= 0);
    char c;
    CHECK_INT(read(fd, &c, 1), -1);    /* empty, and non-blocking */
    CHECK_INT(errno, EAGAIN);
    raise(SIGINT);
    CHECK_INT(read(fd, &c, 1), 1);     /* the handler wrote a byte */
    raise(SIGINT);
    raise(SIGINT);
    sig_drain(fd);
    CHECK_INT(read(fd, &c, 1), -1);    /* drained */
    close(fd);
    sig_quit = 0;
}
```

and list it in `tests/tests.h`, after `X(sig_pipe_ignored)`:

```c
    X(sig_wake_fd)                  \
```

#### src/server.h

The event loop gets its own module. `parleyd.c` hands it the listening socket and the settings:

```c
/* server.h - the server's event loop: one thread, many sockets. */
#ifndef PARLEY_SERVER_H
#define PARLEY_SERVER_H

#include <stddef.h>

struct server_config {
    size_t max_clients;     /* more get "ERROR :Server full" */
};

/* Serve clients on listen_fd until SIGINT or SIGTERM (sig_init() must have
 * been called). Returns 0, or -1 after logging a fatal error. */
int server_run(int listen_fd, const struct server_config *cfg);

#endif
```

#### src/server.c

The heart of server v3, a new file. Its first comment sums up the design. The room and the settings are
`static` globals: there's one of each, used by one thread.

```c
/* server.c - the server's event loop: one thread, many sockets.
 *
 * Every socket is non-blocking, and the only place the server ever waits
 * is poll(). Each turn of the loop:
 *   1. build the pollfd array: the self-pipe, the listening socket, and
 *      each client (POLLIN while it is open, POLLOUT while it has output)
 *   2. poll()
 *   3. accept new clients, read from ready clients and handle their lines
 *   4. write queued output, then close the connections that are finished
 * Handlers never write to sockets: conn_send() only appends to the
 * client's output queue. With one thread, no locks are needed. */
#include "server.h"

#include <errno.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "clock.h"
#include "conn.h"
#include "io.h"
#include "log.h"
#include "room.h"
#include "sig.h"
#include "str.h"

/* How long to try to deliver "ERROR :Server shutting down". */
#define SHUTDOWN_GRACE_MS 2000

static struct room room;
static const struct server_config *config;
```

`accept_clients()` accepts until `EAGAIN`, like `poll_echo`. A client over the limit gets
`ERROR :Server full` by a single `write()` straight to its socket, result ignored: 20 bytes always fit
into a brand-new socket. New clients go at the end of the room:

```c
/* Accept every pending connection. */
static void accept_clients(int listen_fd)
{
    for (;;) {
        struct sockaddr_storage ss;
        socklen_t len = sizeof ss;
        int fd = accept(listen_fd, (struct sockaddr *)&ss, &len);
        if (fd < 0) {
            if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
                log_msg(LOG_WARN, "accept: %s", strerror(errno));
            return;
        }
        char addr[NET_ADDR_MAX];
        net_addr_str((struct sockaddr *)&ss, len, addr, sizeof addr);

        if (room.count >= config->max_clients) {
            static const char full[] = "ERROR :Server full\r\n";
            log_msg(LOG_WARN, "%s: server full", addr);
            ssize_t r = write(fd, full, sizeof full - 1);  /* best effort */
            (void)r;
            close(fd);
            continue;
        }
        struct conn *c = malloc(sizeof *c);
        if (c == NULL || set_nonblock(fd) < 0 || room_add(&room, c) < 0) {
            log_msg(LOG_ERROR, "%s: cannot add client: %s", addr, strerror(errno));
            free(c);
            close(fd);
            continue;
        }
        *c = (struct conn){ .fd = fd, .state = CONN_OPEN };
        str_copy(c->addr, addr, sizeof c->addr);
        log_msg(LOG_DEBUG, "connection from %s", addr);
    }
}
```

`handle_input()` makes **one** `read()` of at most 4096 bytes per turn, then handles every complete line.
It doesn't read until `EAGAIN`: `poll()` is level-triggered, so anything left is reported next turn. That
keeps the loop fair: a client sending 100,000 lines gets 4 KiB of attention per turn, like everyone else,
and can't starve the others. The loop stops when the client is no longer open, for example after `QUIT`,
so lines after a `QUIT` are ignored:

```c
/* The client's socket is readable: read once, handle complete lines. */
static void handle_input(struct conn *c)
{
    char line[MSG_LINE_MAX + 1];
    ssize_t n = read_into(c->fd, &c->in, 4096);
    if (n < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
            conn_drop(c, strerror(errno));
        return;
    }
    if (n == 0) {
        conn_drop(c, "Connection closed");
        return;
    }

    enum frame_status st;
    while (c->state == CONN_OPEN
           && (st = msg_take_line(&c->in, &c->skipping, line)) != FRAME_NONE) {
        if (st == FRAME_TOO_LONG)
            conn_numeric(c, "417", NULL, "Input line too long");
        else
            room_handle_line(&room, c, line);
    }
}
```

`poll_timeout()` is the timer from [Timers](#timers), with one timer so far, the end of the shutdown grace
time. Part 2 adds the idle timers.

```c
/* The poll() timeout in milliseconds: until the end of the shutdown grace
 * time, or -1 (wait forever). */
static int poll_timeout(long long now, long long shutdown_at)
{
    if (shutdown_at < 0)
        return -1;
    return shutdown_at <= now ? 0 : (int)(shutdown_at - now);
}
```

`reap()` is the only place connections end. A connection is finished when it's dead, or closing with
nothing left to send. `room_part()` tells the others it left, with its reason (for a client that sent
`QUIT`, `cmd_quit()` already did, and `room_part()` does nothing the second time). `room_remove()` keeps
the others' order, so the loop goes backwards: removing entry `i` moves only entries after it, which were
already looked at.

```c
/* Close and free the connections that are finished. */
static void reap(void)
{
    /* Backwards, so removing conns[i] does not skip the next one. */
    for (size_t i = room.count; i-- > 0; ) {
        struct conn *c = room.conns[i];
        if (c->state == CONN_OPEN || (c->state == CONN_CLOSING && c->out.len > 0))
            continue;
        room_part(&room, c, c->reason[0] != '\0' ? c->reason : "Connection closed");
        room_remove(&room, c);
        log_msg(LOG_DEBUG, "closing connection from %s", c->addr);
        close(c->fd);
        sb_free(&c->in);
        sb_free(&c->out);
        free(c);
    }
}
```

`start_shutdown()` queues `ERROR :Server shutting down` for everyone and marks them closing. Clearing the
nick first stops `reap()` from sending everyone a `QUIT` for everyone else:

```c
/* Tell every client goodbye; they close as soon as that is sent. */
static void start_shutdown(void)
{
    log_msg(LOG_INFO, "got signal %d (%s), shutting down",
            (int)sig_quit, strsignal(sig_quit));
    for (size_t i = 0; i < room.count; i++) {
        struct conn *c = room.conns[i];
        conn_send(c, &(struct msg){ .command = "ERROR", .trailing = "Server shutting down" });
        c->nick[0] = '\0';             /* everyone leaves: no QUIT messages */
        conn_close(c, "Server shutting down");
    }
}
```

And the loop itself. Its four steps match the comment at the top of the file:

```c
int server_run(int listen_fd, const struct server_config *cfg)
{
    config = cfg;
    int wake_fd = sig_wake_fd();
    if (wake_fd < 0 || set_nonblock(listen_fd) < 0) {
        log_msg(LOG_ERROR, "setting up: %s", strerror(errno));
        return -1;
    }

    struct pollfd *fds = NULL;
    size_t fds_cap = 0;
    long long shutdown_at = -1;        /* -1: not shutting down */
    int status = 0;

    for (;;) {
        /* 1. What to wait for. fds[0]: self-pipe, fds[1]: listening
         *    socket (fd -1 makes poll() skip an entry), then clients. */
        size_t nfds = 2 + room.count;
        if (nfds > fds_cap) {
            struct pollfd *p = realloc(fds, nfds * 2 * sizeof *p);
            if (p == NULL) {
                log_msg(LOG_ERROR, "out of memory");
                status = -1;
                break;
            }
            fds = p;
            fds_cap = nfds * 2;
        }
        fds[0] = (struct pollfd){ .fd = wake_fd, .events = POLLIN };
        fds[1] = (struct pollfd){ .fd = shutdown_at < 0 ? listen_fd : -1, .events = POLLIN };
        for (size_t i = 0; i < room.count; i++) {
            struct conn *c = room.conns[i];
            short events = 0;
            if (c->state == CONN_OPEN)
                events |= POLLIN;
            if (c->out.len > 0)
                events |= POLLOUT;
            fds[2 + i] = (struct pollfd){ .fd = c->fd, .events = events };
        }

        /* 2. Wait. */
        long long now = clock_ms();
        int n = poll(fds, (nfds_t)nfds, poll_timeout(now, shutdown_at));
        if (n < 0 && errno != EINTR) {
            log_msg(LOG_ERROR, "poll: %s", strerror(errno));
            status = -1;
            break;
        }
        now = clock_ms();

        /* 3. Events. Only the first room.count clients from step 1 have a
         *    pollfd: accept_clients() appends new ones after them. */
        if (n > 0 && (fds[0].revents & POLLIN))
            sig_drain(wake_fd);
        if (sig_quit != 0 && shutdown_at < 0) {
            start_shutdown();
            shutdown_at = now + SHUTDOWN_GRACE_MS;
        }
        size_t polled = nfds - 2;
        for (size_t i = 0; n > 0 && i < polled; i++) {
            struct conn *c = room.conns[i];
            short rev = fds[2 + i].revents;
            /* POLLHUP/POLLERR: the peer is gone, read() will tell how. */
            if (c->state == CONN_OPEN && (rev & (POLLIN | POLLHUP | POLLERR)))
                handle_input(c);
            else if (rev & (POLLHUP | POLLERR))
                conn_drop(c, "Connection closed");
        }
        if (n > 0 && (fds[1].revents & POLLIN))
            accept_clients(listen_fd);

        /* 4. Send what we can, then close finished connections. Writing
         *    right away (not waiting for POLLOUT) is fine: usually the
         *    socket has room, and if not, conn_flush() stops at EAGAIN. */
        for (size_t i = 0; i < room.count; i++)
            conn_flush(room.conns[i]);
        reap();

        if (shutdown_at >= 0 && (room.count == 0 || now >= shutdown_at))
            break;
    }

    /* Anything left (after the shutdown grace time, or an error). */
    for (size_t i = 0; i < room.count; i++)
        conn_drop(room.conns[i], "Server shutting down");
    reap();
    room_free(&room);
    free(fds);
    close(wake_fd);
    return status;
}
```

Some details:

- **`fds[2 + i]` belongs to `room.conns[i]`.** That works only if no client moves between step 1 and the
  end of step 3. Nothing removes a client before `reap()`, and `accept_clients()` adds new ones at the end,
  after the `polled` clients. That's also why step 3 handles the listening socket **after** the clients.
- **POLLHUP or POLLERR on an open client** calls `handle_input()` too. Its `read()` returns 0 or the
  error, so the client is dropped with the right reason, and anything it sent before hanging up is still
  handled.
- **Writing in step 4 without waiting for `POLLOUT`.** The output was usually just queued in step 3, and a
  socket usually has room; waiting for `POLLOUT` would cost an extra turn per reply. `POLLOUT` matters only
  for clients that are behind; for them, `conn_flush()` stops at `EAGAIN`.
- **Shutdown.** When `sig_quit` is set, the self-pipe wakes `poll()`. `start_shutdown()` runs once, and
  `shutdown_at` gives the clients two seconds to receive their goodbye. Meanwhile the listening socket's
  entry is `-1`, so no one new is accepted. The loop ends when everyone is gone or the time is up; then the
  remaining clients are dropped.
- The `pollfd` array grows when needed, to twice the size needed, like the vector of
  [Chapter 8](08-memory.md), and is reused every turn.

#### src/parleyd.c

`parleyd.c` loses the client threads, the room, the locks and the condition variable. Left are the
options and `main()`, which listens and calls `server_run()`. Replace the whole file:

```c
/* parleyd.c - the parley chat server: options and the main loop.
 * Version 3: one thread, many sockets (see server.c). */
#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "log.h"
#include "net.h"
#include "server.h"
#include "sig.h"
#include "str.h"
#include "version.h"

#define DEFAULT_PORT 6667
#define PORT_MAX 65535
#define DEFAULT_MAX_CLIENTS 64
#define MAX_CLIENTS_MAX 1000

struct config {
    const char *addr;
    long port;
    long max_clients;
    bool verbose;
};

static void usage_error(const char *fmt, ...)
{
    fprintf(stderr, "parleyd: ");
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\nTry 'parleyd --help' for more information.\n");
    exit(2);
}

static void print_help(void)
{
    printf("usage: parleyd [-p PORT] [-b ADDR] [-m MAXCLIENTS] [-v]\n"
           "  -p PORT        TCP port to listen on, 0: any free port (default %d)\n"
           "  -b ADDR        address to listen on (default 127.0.0.1)\n"
           "  -m MAXCLIENTS  most clients at once, 1-%d (default %d)\n"
           "  -v             verbose (debug) logging\n"
           "  -h, --help     show this help and exit\n"
           "  --version      show the version and exit\n",
           DEFAULT_PORT, MAX_CLIENTS_MAX, DEFAULT_MAX_CLIENTS);
}

static void parse_args(int argc, char *argv[], struct config *cfg)
{
    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];

        if (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0) {
            print_help();
            exit(0);
        }
        if (strcmp(arg, "--version") == 0) {
            printf("parleyd " PARLEY_VERSION "\n");   /* "a" "b" is "ab" */
            exit(0);
        }
        if (arg[0] != '-' || arg[1] == '\0' || arg[2] != '\0')
            usage_error("unexpected argument '%s'", arg);

        char opt = arg[1];
        const char *value = NULL;
        if (opt == 'p' || opt == 'b' || opt == 'm') {
            if (i + 1 >= argc)
                usage_error("option -%c needs a value", opt);
            value = argv[++i];
        }

        switch (opt) {
        case 'b':
            cfg->addr = value;
            break;
        case 'p':
            if (!str_to_long(value, 0, PORT_MAX, &cfg->port))
                usage_error("invalid port '%s' (expected 0-%d)", value, PORT_MAX);
            break;
        case 'm':
            if (!str_to_long(value, 1, MAX_CLIENTS_MAX, &cfg->max_clients))
                usage_error("invalid client limit '%s' (expected 1-%d)",
                            value, MAX_CLIENTS_MAX);
            break;
        case 'v':
            cfg->verbose = true;
            break;
        default:
            usage_error("unknown option '%s'", arg);
        }
    }
}

int main(int argc, char *argv[])
{
    struct config cfg = {
        .addr = "127.0.0.1",
        .port = DEFAULT_PORT,
        .max_clients = DEFAULT_MAX_CLIENTS,
        .verbose = false,
    };
    parse_args(argc, argv, &cfg);
    log_init("parleyd", cfg.verbose);
    log_msg(LOG_DEBUG, "verbose logging is on");
    if (sig_init() < 0)
        die("setting up signals: %s", strerror(errno));

    char err[256], where[NET_ADDR_MAX];
    int listen_fd = net_listen(cfg.addr, (int)cfg.port, err, sizeof err);
    if (listen_fd < 0)
        die("cannot listen on %s", err);
    if (net_local_addr(listen_fd, where, sizeof where) < 0)
        die("getsockname: %s", strerror(errno));
    log_msg(LOG_INFO, "listening on %s", where);

    struct server_config scfg = {
        .max_clients = (size_t)cfg.max_clients,
    };
    int status = server_run(listen_fd, &scfg);
    close(listen_fd);
    return status == 0 ? 0 : 1;
}
```

`room.c` stays as it is. `cmd_quit()` sets `CONN_CLOSING` itself, which now means "send `ERROR :Goodbye`,
then close".

#### Checkpoint: part 1

The chat from Chapter 17, with a twist: alice sends half a line, bob talks, then alice sends the rest.
Then the slow reader that stopped server v2, and the client limit with the musl build. `say` and the named
pipes work as in Chapter 17:

```console
$ make -s test-all
unit: 29 passed, 0 failed
unit: 29 passed, 0 failed
$ make -s LIBC=musl
# the same chat as in Chapter 17, now served by one thread
$ build/glibc/parleyd -p 6618 2>server.log & server=$!; sleep 0.2
$ mkfifo a b; nc -C 127.0.0.1 6618 <a >alice.txt & alice=$!; exec 3>a
$ nc -C 127.0.0.1 6618 <b >bob.txt 3>&- & bob=$!; exec 4>b
$ say() { printf "%b" "$2" >&"$1"; sleep 0.2; }
$ say 3 'NICK alice\n'; say 4 'NICK Alice\n'; say 4 'NICK bob\n'
$ ls /proc/$server/task | wc -l
1
# alice types half a line; the server keeps serving bob meanwhile
$ say 3 'MSG :hi b'; say 4 'ME waves\n'; say 3 'ob\n'
$ say 4 'NICK robert\n'; say 3 'QUIT :lunch\n'; exec 3>&-; wait $alice; cat alice.txt
:parley 001 alice :Welcome to parley, alice
:alice JOIN
:parley 353 alice :alice
:parley 366 alice :End of NAMES
:bob JOIN
:bob ME :waves
:alice MSG :hi bob
:bob NICK robert
ERROR :Goodbye
$ kill -TERM $server; wait $server; echo "exit $?"; exec 4>&-; wait $bob; cat bob.txt; cat server.log
exit 0
:parley 433 * Alice :Nickname is already in use
:parley 001 bob :Welcome to parley, bob
:bob JOIN
:parley 353 bob :alice bob
:parley 366 bob :End of NAMES
:bob ME :waves
:alice MSG :hi bob
:bob NICK robert
:alice QUIT :lunch
ERROR :Server shutting down
parleyd: info: listening on 127.0.0.1:6618
parleyd: info: alice joined
parleyd: info: bob joined
parleyd: info: bob is now robert
parleyd: info: alice left (lunch)
parleyd: info: got signal 15 (Terminated), shutting down
# the slow reader from Chapter 17: now it is the only one who suffers
$ build/glibc/parleyd -p 6618 2>server.log & server=$!; sleep 0.2
$ exec 5<>/dev/tcp/127.0.0.1/6618; printf 'NICK sloth\r\n' >&5
$ { echo 'NICK spammer'; yes 'MSG :spam spam spam spam spam spam spam spam spam spam' | head -n 100000; echo QUIT; } | timeout 5 nc -C -N 127.0.0.1 6618 | tail -n 3; echo "exit ${PIPESTATUS[1]}"
:spammer MSG :spam spam spam spam spam spam spam spam spam spam
:spammer MSG :spam spam spam spam spam spam spam spam spam spam
ERROR :Goodbye
exit 0
$ printf 'NICK carol\r\nPING :alive?\r\nQUIT\r\n' | timeout 2 nc -C -N 127.0.0.1 6618; echo "exit $?"
:parley 001 carol :Welcome to parley, carol
:carol JOIN
:parley 353 carol :carol
:parley 366 carol :End of NAMES
:parley PONG :alive?
ERROR :Goodbye
exit 0
$ exec 5>&-; kill -TERM $server; wait $server; echo "exit $?"; cat server.log
exit 0
parleyd: info: listening on 127.0.0.1:6618
parleyd: info: sloth joined
parleyd: info: spammer joined
parleyd: warning: sloth: output queue full, disconnecting
parleyd: info: sloth left (Output queue full)
parleyd: info: spammer left (Quit)
parleyd: info: carol joined
parleyd: info: carol left (Quit)
parleyd: info: got signal 15 (Terminated), shutting down
# -m 1, and shutdown with clients connected
$ build/musl/parleyd -p 6618 -m 1 2>server.log & server=$!; sleep 0.2
$ nc -C 127.0.0.1 6618 <a >alice.txt & alice=$!; exec 3>a; say 3 "NICK alice\n"
$ nc 127.0.0.1 6618 </dev/null; echo "exit $?"
ERROR :Server full
exit 0
$ kill -INT $server; wait $server; echo "exit $?"; exec 3>&-; wait $alice; cat alice.txt; cat server.log
exit 0
:parley 001 alice :Welcome to parley, alice
:alice JOIN
:parley 353 alice :alice
:parley 366 alice :End of NAMES
ERROR :Server shutting down
parleyd: info: listening on 127.0.0.1:6618
parleyd: info: alice joined
parleyd: warning: 127.0.0.1:52042: server full
parleyd: info: got signal 2 (Interrupt), shutting down
$ build/glibc/parleyd -t 5
parleyd: unknown option '-t'
Try 'parleyd --help' for more information.
$ rm -f a b alice.txt bob.txt server.log && make clean >/dev/null
```

- The server is **one** task. Alice's half line `MSG :hi b` stayed in her input buffer while the loop
  handled bob's `ME`; the `MSG` went out when `ob` arrived. That's why `:bob ME :waves` comes before
  `:alice MSG :hi bob` in both transcripts.
- **The slow reader**: `sloth` never reads. Its queue grew to 64 KiB, and the server dropped it with a
  warning in the log; the others got its `QUIT` with the reason "Output queue full". The spammer sent all
  100,000 messages and got its `ERROR :Goodbye`, and carol was served at once. (`${PIPESTATUS[1]}` is
  bash's exit status of the pipeline's second command, `timeout nc`; plain `$?` would be `tail`'s.)
- After `kill`, every client gets `ERROR :Server shutting down`, and the server exits 0.
- `-t` doesn't exist yet. The client port in the "server full" warning varies.

### Part 2: idle timeouts

A client whose network disappears (a laptop closed, a cable pulled) sends no FIN. Its socket stays open on
the server, and nobody reads from it. Real IRC servers handle that with `PING`: a client quiet for a while
gets `PING :parley`, which every client must answer with `PONG`. One that doesn't answer in time is
dropped with `ERROR :Ping timeout`. A client that connects and never registers is dropped too, with
`ERROR :Registration timeout`; otherwise anyone could fill the room with silent connections.

`-t IDLE` sets the time in seconds: 120 by default, 0 turns the timers off. A registered client silent for
`IDLE` seconds gets a `PING`; still silent `IDLE` seconds later, it's dropped. Any input counts, not only
`PONG`.

#### src/conn.h

Three new fields at the end of `struct conn`:

```c
struct conn {
    int fd;                    /* the client's socket (non-blocking) */
    char addr[NET_ADDR_MAX];   /* the client's address, for log messages */
    char nick[NICK_MAX + 1];   /* "" until the client registers */
    enum conn_state state;
    char reason[64];           /* why we close it: for the QUIT the others see */
    struct strbuf in;          /* bytes received but not yet handled */
    struct strbuf out;         /* bytes waiting to be sent: the output queue */
    bool skipping;             /* throwing away a too-long line */
    long long connected;       /* clock_ms() when it connected */
    long long last_active;     /* clock_ms() when it last sent something */
    bool ping_sent;            /* we sent a PING since then */
};
```

#### src/server.h

A new setting in `struct server_config`:

```c
struct server_config {
    size_t max_clients;     /* more get "ERROR :Server full" */
    int idle_secs;          /* PING after this long without input; 0: never */
};
```

#### src/server.c

The top comment gets a step 4, the timers:

```c
/* server.c - the server's event loop: one thread, many sockets.
 *
 * Every socket is non-blocking, and the only place the server ever waits
 * is poll(). Each turn of the loop:
 *   1. build the pollfd array: the self-pipe, the listening socket, and
 *      each client (POLLIN while it is open, POLLOUT while it has output)
 *   2. poll(), with a timeout that ends when the next timer is due
 *   3. accept new clients, read from ready clients and handle their lines
 *   4. check the idle timers
 *   5. write queued output, then close the connections that are finished
 * Handlers never write to sockets: conn_send() only appends to the
 * client's output queue. With one thread, no locks are needed. */
```

`accept_clients()` and `handle_input()` now take `now`, the time after `poll()` returned. A new connection
records when it connected; any input resets the idle timer. Replace both functions:

```c
/* Accept every pending connection. */
static void accept_clients(int listen_fd, long long now)
{
    for (;;) {
        struct sockaddr_storage ss;
        socklen_t len = sizeof ss;
        int fd = accept(listen_fd, (struct sockaddr *)&ss, &len);
        if (fd < 0) {
            if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
                log_msg(LOG_WARN, "accept: %s", strerror(errno));
            return;
        }
        char addr[NET_ADDR_MAX];
        net_addr_str((struct sockaddr *)&ss, len, addr, sizeof addr);

        if (room.count >= config->max_clients) {
            static const char full[] = "ERROR :Server full\r\n";
            log_msg(LOG_WARN, "%s: server full", addr);
            ssize_t r = write(fd, full, sizeof full - 1);  /* best effort */
            (void)r;
            close(fd);
            continue;
        }
        struct conn *c = malloc(sizeof *c);
        if (c == NULL || set_nonblock(fd) < 0 || room_add(&room, c) < 0) {
            log_msg(LOG_ERROR, "%s: cannot add client: %s", addr, strerror(errno));
            free(c);
            close(fd);
            continue;
        }
        *c = (struct conn){
            .fd = fd, .state = CONN_OPEN, .connected = now, .last_active = now,
        };
        str_copy(c->addr, addr, sizeof c->addr);
        log_msg(LOG_DEBUG, "connection from %s", addr);
    }
}
```

```c
/* The client's socket is readable: read once, handle complete lines. */
static void handle_input(struct conn *c, long long now)
{
    char line[MSG_LINE_MAX + 1];
    ssize_t n = read_into(c->fd, &c->in, 4096);
    if (n < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
            conn_drop(c, strerror(errno));
        return;
    }
    if (n == 0) {
        conn_drop(c, "Connection closed");
        return;
    }
    c->last_active = now;
    c->ping_sent = false;

    enum frame_status st;
    while (c->state == CONN_OPEN
           && (st = msg_take_line(&c->in, &c->skipping, line)) != FRAME_NONE) {
        if (st == FRAME_TOO_LONG)
            conn_numeric(c, "417", NULL, "Input line too long");
        else
            room_handle_line(&room, c, line);
    }
}
```

Replace `poll_timeout()` with these three functions. `deadline()` computes when a client's timer is due from
its state, so setting or resetting a timer needs no bookkeeping: changing `last_active` is enough.
`check_timer()` acts when the time has come. `poll_timeout()` finds the earliest deadline among the open
clients and the shutdown time, and waits until then:

```c
/* When c's timer is next due: its registration deadline, or when it is
 * due a PING, or when its PING is unanswered for too long. */
static long long deadline(const struct conn *c)
{
    long long idle_ms = config->idle_secs * 1000LL;
    if (c->nick[0] == '\0')
        return c->connected + idle_ms;
    return c->last_active + (c->ping_sent ? 2 * idle_ms : idle_ms);
}

static void check_timer(struct conn *c, long long now)
{
    if (c->state != CONN_OPEN || now < deadline(c))
        return;
    if (c->nick[0] == '\0') {
        conn_send(c, &(struct msg){ .command = "ERROR", .trailing = "Registration timeout" });
        conn_close(c, "Registration timeout");
    } else if (!c->ping_sent) {
        conn_send(c, &(struct msg){ .command = "PING", .trailing = SERVER_NAME });
        c->ping_sent = true;
    } else {
        conn_send(c, &(struct msg){ .command = "ERROR", .trailing = "Ping timeout" });
        conn_close(c, "Ping timeout");
    }
}

/* The poll() timeout in milliseconds: until the earliest timer, or -1
 * (wait forever) if there is none. */
static int poll_timeout(long long now, long long shutdown_at)
{
    long long first = shutdown_at;
    if (config->idle_secs > 0) {
        for (size_t i = 0; i < room.count; i++) {
            long long d = deadline(room.conns[i]);
            if (room.conns[i]->state == CONN_OPEN && (first < 0 || d < first))
                first = d;
        }
    }
    if (first < 0)
        return -1;
    return first <= now ? 0 : (int)(first - now);
}
```

The timeout must fit an `int` of milliseconds. The longest wait, `2 * IDLE_MAX` seconds, is 172,800,000 ms,
far below `INT_MAX` (2,147,483,647).

In `server_run()`, pass `now` to the two functions, and check the timers in a new step 4, after input is
handled, so a client whose data arrived this turn is never timed out. Replace the function:

```c
int server_run(int listen_fd, const struct server_config *cfg)
{
    config = cfg;
    int wake_fd = sig_wake_fd();
    if (wake_fd < 0 || set_nonblock(listen_fd) < 0) {
        log_msg(LOG_ERROR, "setting up: %s", strerror(errno));
        return -1;
    }

    struct pollfd *fds = NULL;
    size_t fds_cap = 0;
    long long shutdown_at = -1;        /* -1: not shutting down */
    int status = 0;

    for (;;) {
        /* 1. What to wait for. fds[0]: self-pipe, fds[1]: listening
         *    socket (fd -1 makes poll() skip an entry), then clients. */
        size_t nfds = 2 + room.count;
        if (nfds > fds_cap) {
            struct pollfd *p = realloc(fds, nfds * 2 * sizeof *p);
            if (p == NULL) {
                log_msg(LOG_ERROR, "out of memory");
                status = -1;
                break;
            }
            fds = p;
            fds_cap = nfds * 2;
        }
        fds[0] = (struct pollfd){ .fd = wake_fd, .events = POLLIN };
        fds[1] = (struct pollfd){ .fd = shutdown_at < 0 ? listen_fd : -1, .events = POLLIN };
        for (size_t i = 0; i < room.count; i++) {
            struct conn *c = room.conns[i];
            short events = 0;
            if (c->state == CONN_OPEN)
                events |= POLLIN;
            if (c->out.len > 0)
                events |= POLLOUT;
            fds[2 + i] = (struct pollfd){ .fd = c->fd, .events = events };
        }

        /* 2. Wait. */
        long long now = clock_ms();
        int n = poll(fds, (nfds_t)nfds, poll_timeout(now, shutdown_at));
        if (n < 0 && errno != EINTR) {
            log_msg(LOG_ERROR, "poll: %s", strerror(errno));
            status = -1;
            break;
        }
        now = clock_ms();

        /* 3. Events. Only the first room.count clients from step 1 have a
         *    pollfd: accept_clients() appends new ones after them. */
        if (n > 0 && (fds[0].revents & POLLIN))
            sig_drain(wake_fd);
        if (sig_quit != 0 && shutdown_at < 0) {
            start_shutdown();
            shutdown_at = now + SHUTDOWN_GRACE_MS;
        }
        size_t polled = nfds - 2;
        for (size_t i = 0; n > 0 && i < polled; i++) {
            struct conn *c = room.conns[i];
            short rev = fds[2 + i].revents;
            /* POLLHUP/POLLERR: the peer is gone, read() will tell how. */
            if (c->state == CONN_OPEN && (rev & (POLLIN | POLLHUP | POLLERR)))
                handle_input(c, now);
            else if (rev & (POLLHUP | POLLERR))
                conn_drop(c, "Connection closed");
        }
        if (n > 0 && (fds[1].revents & POLLIN))
            accept_clients(listen_fd, now);

        /* 4. Timers. */
        if (config->idle_secs > 0)
            for (size_t i = 0; i < room.count; i++)
                check_timer(room.conns[i], now);

        /* 5. Send what we can, then close finished connections. Writing
         *    right away (not waiting for POLLOUT) is fine: usually the
         *    socket has room, and if not, conn_flush() stops at EAGAIN. */
        for (size_t i = 0; i < room.count; i++)
            conn_flush(room.conns[i]);
        reap();

        if (shutdown_at >= 0 && (room.count == 0 || now >= shutdown_at))
            break;
    }

    /* Anything left (after the shutdown grace time, or an error). */
    for (size_t i = 0; i < room.count; i++)
        conn_drop(room.conns[i], "Server shutting down");
    reap();
    room_free(&room);
    free(fds);
    close(wake_fd);
    return status;
}
```

#### src/parleyd.c

The `-t` option. Two new constants:

```c
#define DEFAULT_PORT 6667
#define PORT_MAX 65535
#define DEFAULT_MAX_CLIENTS 64
#define MAX_CLIENTS_MAX 1000
#define DEFAULT_IDLE 120
#define IDLE_MAX 86400
```

a field in `struct config`:

```c
struct config {
    const char *addr;
    long port;
    long max_clients;
    long idle;
    bool verbose;
};
```

the help text:

```c
static void print_help(void)
{
    printf("usage: parleyd [-p PORT] [-b ADDR] [-m MAXCLIENTS] [-t IDLE] [-v]\n"
           "  -p PORT        TCP port to listen on, 0: any free port (default %d)\n"
           "  -b ADDR        address to listen on (default 127.0.0.1)\n"
           "  -m MAXCLIENTS  most clients at once, 1-%d (default %d)\n"
           "  -t IDLE        PING clients idle for IDLE seconds, drop them after\n"
           "                 twice that; 0: never (default %d)\n"
           "  -v             verbose (debug) logging\n"
           "  -h, --help     show this help and exit\n"
           "  --version      show the version and exit\n",
           DEFAULT_PORT, MAX_CLIENTS_MAX, DEFAULT_MAX_CLIENTS, DEFAULT_IDLE);
}
```

`parse_args()` accepts `-t` with a value, and checks its range:

```c
static void parse_args(int argc, char *argv[], struct config *cfg)
{
    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];

        if (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0) {
            print_help();
            exit(0);
        }
        if (strcmp(arg, "--version") == 0) {
            printf("parleyd " PARLEY_VERSION "\n");   /* "a" "b" is "ab" */
            exit(0);
        }
        if (arg[0] != '-' || arg[1] == '\0' || arg[2] != '\0')
            usage_error("unexpected argument '%s'", arg);

        char opt = arg[1];
        const char *value = NULL;
        if (opt == 'p' || opt == 'b' || opt == 'm' || opt == 't') {
            if (i + 1 >= argc)
                usage_error("option -%c needs a value", opt);
            value = argv[++i];
        }

        switch (opt) {
        case 'b':
            cfg->addr = value;
            break;
        case 'p':
            if (!str_to_long(value, 0, PORT_MAX, &cfg->port))
                usage_error("invalid port '%s' (expected 0-%d)", value, PORT_MAX);
            break;
        case 'm':
            if (!str_to_long(value, 1, MAX_CLIENTS_MAX, &cfg->max_clients))
                usage_error("invalid client limit '%s' (expected 1-%d)",
                            value, MAX_CLIENTS_MAX);
            break;
        case 't':
            if (!str_to_long(value, 0, IDLE_MAX, &cfg->idle))
                usage_error("invalid idle time '%s' (expected 0-%d)", value, IDLE_MAX);
            break;
        case 'v':
            cfg->verbose = true;
            break;
        default:
            usage_error("unknown option '%s'", arg);
        }
    }
}
```

and `main()` passes it on, with a default of 120 seconds:

```c
int main(int argc, char *argv[])
{
    struct config cfg = {
        .addr = "127.0.0.1",
        .port = DEFAULT_PORT,
        .max_clients = DEFAULT_MAX_CLIENTS,
        .idle = DEFAULT_IDLE,
        .verbose = false,
    };
    parse_args(argc, argv, &cfg);
    log_init("parleyd", cfg.verbose);
    log_msg(LOG_DEBUG, "verbose logging is on");
    if (sig_init() < 0)
        die("setting up signals: %s", strerror(errno));

    char err[256], where[NET_ADDR_MAX];
    int listen_fd = net_listen(cfg.addr, (int)cfg.port, err, sizeof err);
    if (listen_fd < 0)
        die("cannot listen on %s", err);
    if (net_local_addr(listen_fd, where, sizeof where) < 0)
        die("getsockname: %s", strerror(errno));
    log_msg(LOG_INFO, "listening on %s", where);

    struct server_config scfg = {
        .max_clients = (size_t)cfg.max_clients,
        .idle_secs = (int)cfg.idle,
    };
    int status = server_run(listen_fd, &scfg);
    close(listen_fd);
    return status == 0 ? 0 : 1;
}
```

### Checkpoint

The full test: the chat and the slow reader again, now also under the sanitizers, the idle timeouts with
the musl build, and the system calls of both builds:

```console
$ make -s test-all
unit: 29 passed, 0 failed
unit: 29 passed, 0 failed
$ make -s SANITIZE=1 test
unit: 29 passed, 0 failed
$ make -s LIBC=musl
# the same chat as in Chapter 17, now served by one thread
$ build/glibc/parleyd -p 6618 2>server.log & server=$!; sleep 0.2
$ mkfifo a b; nc -C 127.0.0.1 6618 <a >alice.txt & alice=$!; exec 3>a
$ nc -C 127.0.0.1 6618 <b >bob.txt 3>&- & bob=$!; exec 4>b
$ say() { printf "%b" "$2" >&"$1"; sleep 0.2; }
$ say 3 'NICK alice\n'; say 4 'NICK Alice\n'; say 4 'NICK bob\n'
$ ls /proc/$server/task | wc -l
1
# alice types half a line; the server keeps serving bob meanwhile
$ say 3 'MSG :hi b'; say 4 'ME waves\n'; say 3 'ob\n'
$ say 4 'NICK robert\n'; say 3 'QUIT :lunch\n'; exec 3>&-; wait $alice; cat alice.txt
:parley 001 alice :Welcome to parley, alice
:alice JOIN
:parley 353 alice :alice
:parley 366 alice :End of NAMES
:bob JOIN
:bob ME :waves
:alice MSG :hi bob
:bob NICK robert
ERROR :Goodbye
$ kill -TERM $server; wait $server; echo "exit $?"; exec 4>&-; wait $bob; cat bob.txt; cat server.log
exit 0
:parley 433 * Alice :Nickname is already in use
:parley 001 bob :Welcome to parley, bob
:bob JOIN
:parley 353 bob :alice bob
:parley 366 bob :End of NAMES
:bob ME :waves
:alice MSG :hi bob
:bob NICK robert
:alice QUIT :lunch
ERROR :Server shutting down
parleyd: info: listening on 127.0.0.1:6618
parleyd: info: alice joined
parleyd: info: bob joined
parleyd: info: bob is now robert
parleyd: info: alice left (lunch)
parleyd: info: got signal 15 (Terminated), shutting down
# the slow reader from Chapter 17: now it is the only one who suffers
$ build/glibc/parleyd -p 6618 2>server.log & server=$!; sleep 0.2
$ exec 5<>/dev/tcp/127.0.0.1/6618; printf 'NICK sloth\r\n' >&5
$ { echo 'NICK spammer'; yes 'MSG :spam spam spam spam spam spam spam spam spam spam' | head -n 100000; echo QUIT; } | timeout 5 nc -C -N 127.0.0.1 6618 | tail -n 3; echo "exit ${PIPESTATUS[1]}"
:spammer MSG :spam spam spam spam spam spam spam spam spam spam
:spammer MSG :spam spam spam spam spam spam spam spam spam spam
ERROR :Goodbye
exit 0
$ printf 'NICK carol\r\nPING :alive?\r\nQUIT\r\n' | timeout 2 nc -C -N 127.0.0.1 6618; echo "exit $?"
:parley 001 carol :Welcome to parley, carol
:carol JOIN
:parley 353 carol :carol
:parley 366 carol :End of NAMES
:parley PONG :alive?
ERROR :Goodbye
exit 0
$ exec 5>&-; kill -TERM $server; wait $server; echo "exit $?"; cat server.log
exit 0
parleyd: info: listening on 127.0.0.1:6618
parleyd: info: sloth joined
parleyd: info: spammer joined
parleyd: warning: sloth: output queue full, disconnecting
parleyd: info: sloth left (Output queue full)
parleyd: info: spammer left (Quit)
parleyd: info: carol joined
parleyd: info: carol left (Quit)
parleyd: info: got signal 15 (Terminated), shutting down
# idle timeouts (-t 1): a PING after 1 s of silence, closed after 2 s
$ build/musl/parleyd -p 6618 -t 1 2>server.log & server=$!; sleep 0.2
$ { printf 'NICK idler\r\n'; sleep 3; } | nc -C 127.0.0.1 6618
:parley 001 idler :Welcome to parley, idler
:idler JOIN
:parley 353 idler :idler
:parley 366 idler :End of NAMES
PING :parley
ERROR :Ping timeout
$ sleep 2 | nc 127.0.0.1 6618
ERROR :Registration timeout
$ kill -INT $server; wait $server; echo "exit $?"; cat server.log
exit 0
parleyd: info: listening on 127.0.0.1:6618
parleyd: info: idler joined
parleyd: info: idler left (Ping timeout)
parleyd: info: got signal 2 (Interrupt), shutting down
# -m 1, and shutdown with clients connected
$ build/musl/parleyd -p 6618 -m 1 2>server.log & server=$!; sleep 0.2
$ nc -C 127.0.0.1 6618 <a >alice.txt & alice=$!; exec 3>a; say 3 "NICK alice\n"
$ nc 127.0.0.1 6618 </dev/null; echo "exit $?"
ERROR :Server full
exit 0
$ kill -INT $server; wait $server; echo "exit $?"; exec 3>&-; wait $alice; cat alice.txt; cat server.log
exit 0
:parley 001 alice :Welcome to parley, alice
:alice JOIN
:parley 353 alice :alice
:parley 366 alice :End of NAMES
ERROR :Server shutting down
parleyd: info: listening on 127.0.0.1:6618
parleyd: info: alice joined
parleyd: warning: 127.0.0.1:52562: server full
parleyd: info: got signal 2 (Interrupt), shutting down
# the event loop at the system call level: the same on both C libraries
$ pid_on() { ss -tlnpH "sport = :$1" | grep -o "pid=[0-9]*" | cut -d= -f2; }
$ strace -o trace.txt build/glibc/parleyd -p 6618 -t 0 2>/dev/null & sleep 0.5
$ printf 'NICK x\r\nQUIT\r\n' | nc -N 127.0.0.1 6618 >/dev/null; kill $(pid_on 6618); wait
$ grep -E '^(poll|accept|read|write|writev|close)\(' trace.txt | grep -v ELF
close(3)                                = 0
close(3)                                = 0
write(2, "parleyd: info: listening on 127."..., 43) = 43
poll([{fd=4, events=POLLIN}, {fd=3, events=POLLIN}], 2, -1) = 1 ([{fd=3, revents=POLLIN}])
accept(3, {sa_family=AF_INET, sin_port=htons(52570), sin_addr=inet_addr("127.0.0.1")}, [128 => 16]) = 6
accept(3, 0x7ffc9231af00, [128])        = -1 EAGAIN (Resource temporarily unavailable)
poll([{fd=4, events=POLLIN}, {fd=3, events=POLLIN}, {fd=6, events=POLLIN}], 3, -1) = 1 ([{fd=6, revents=POLLIN}])
read(6, "NICK x\r\nQUIT\r\n", 4096)     = 14
write(2, "parleyd: info: x joined\n", 24) = 24
write(2, "parleyd: info: x left (Quit)\n", 29) = 29
write(6, ":parley 001 x :Welcome to parley"..., 109) = 109
close(6)                                = 0
poll([{fd=4, events=POLLIN}, {fd=3, events=POLLIN}], 2, -1) = ? ERESTART_RESTARTBLOCK (Interrupted by signal)
write(5, "!", 1)                        = 1
write(2, "parleyd: info: got signal 15 (Te"..., 57) = 57
close(4)                                = 0
close(3)                                = 0
$ strace -o trace.txt build/musl/parleyd -p 6618 -t 0 2>/dev/null & sleep 0.5
$ printf 'NICK x\r\nQUIT\r\n' | nc -N 127.0.0.1 6618 >/dev/null; kill $(pid_on 6618); wait
$ grep -E '^(poll|accept|read|write|writev|close)\(' trace.txt | grep -v ELF
writev(2, [{iov_base="parleyd: info: listening on 127."..., iov_len=43}], 1) = 43
poll([{fd=4, events=POLLIN}, {fd=3, events=POLLIN}], 2, -1) = 1 ([{fd=3, revents=POLLIN}])
accept(3, {sa_family=AF_INET, sin_port=htons(52576), sin_addr=inet_addr("127.0.0.1")}, [128 => 16]) = 6
accept(3, 0x7fffc41e3280, [128])        = -1 EAGAIN (Resource temporarily unavailable)
poll([{fd=4, events=POLLIN}, {fd=3, events=POLLIN}, {fd=6, events=POLLIN}], 3, -1) = 1 ([{fd=6, revents=POLLIN}])
read(6, "NICK x\r\nQUIT\r\n", 4096)     = 14
writev(2, [{iov_base="parleyd: info: x joined\n", iov_len=24}], 1) = 24
writev(2, [{iov_base="parleyd: info: x left (Quit)\n", iov_len=29}], 1) = 29
write(6, ":parley 001 x :Welcome to parley"..., 109) = 109
close(6)                                = 0
poll([{fd=4, events=POLLIN}, {fd=3, events=POLLIN}], 2, -1) = ? ERESTART_RESTARTBLOCK (Interrupted by signal)
write(5, "!", 1)                        = 1
writev(2, [{iov_base="parleyd: info: got signal 15 (Te"..., iov_len=57}], 1) = 57
close(4)                                = 0
close(3)                                = 0
$ rm -f a b alice.txt bob.txt server.log trace.txt && make clean >/dev/null && ls
Makefile
src
tests
```

What it shows:

- The 29 unit tests pass with both libraries and with ASan and UBSan.
- The chat and the slow reader behave as in Part 1.
- **Idle timeouts** with `-t 1`: `idler` registers and goes quiet. After 1 second it gets `PING :parley`,
  after 2 `ERROR :Ping timeout`. A client that never registers gets `ERROR :Registration timeout` after 1
  second.
- **The system calls** of one short session (`-t 0`, so `poll()` waits with `-1`): `poll()` on the
  self-pipe (descriptor 4) and the listening socket (3); two `accept()`s, the second ending in `EAGAIN`;
  **one** `read()` with both of the client's lines; all the replies in **one** 109-byte `write()` from the
  output queue; `close()`. Then `kill` interrupts `poll()`, and the handler's `write(5, "!", 1)` puts a
  byte in the self-pipe. Both libraries make the same calls in the same order, except that glibc's dynamic
  loader closes descriptor 3 twice while loading libraries at start-up, and musl's `stderr` uses
  `writev()`.

Server v3 is `parleyd`'s final design. The next chapters improve the client, and
[Chapter 21](21-robustness.md) tests the server under load and with hostile input.

The complete project at this point is in `project/ch18/`.

## Summary

- An event loop serves many clients with one thread: wait for readiness with `poll()`, do the ready work,
  repeat. For our chat it's as fast as a thread per client, with no locks.
- Non-blocking sockets (`O_NONBLOCK`) never wait: `EAGAIN` means "not now", and a write may be short. On
  Linux, `accept()` doesn't make the new socket non-blocking; set it yourself.
- `poll()` takes an array of `struct pollfd`: `POLLIN` and `POLLOUT` in `events`, results in `revents`.
  It's level-triggered: unfinished work is reported again. A negative `fd` makes it skip an entry.
- Keep an output queue per client, and write from it when the socket is writable. Cap the queue: an echo
  server can stop reading (backpressure); a chat server must disconnect a slow reader.
- Framing must not care how bytes arrive: keep partial lines in the input buffer.
- Timers: give `poll()` the time until the earliest deadline; measure with `CLOCK_MONOTONIC`, never the
  time of day.
- Linux never restarts `poll()` after a signal: handle `EINTR`. A signal just before `poll()` is missed
  unless the handler also writes to a self-pipe that `poll()` watches.
- `select()` can't handle descriptors above `FD_SETSIZE` (1024); glibc's fortify catches that in optimized
  builds, musl doesn't. `epoll` scales to many idle connections but is Linux-only. `poll()` is portable
  and enough for `parleyd`.
- Never block in an event loop handler: one slow handler stalls every client.
- glibc and musl make the same system calls here, apart from `epoll_pwait()` for `epoll_wait()` and
  `writev()` for stdio. glibc hides `POLLRDHUP` and `ppoll()` without `_GNU_SOURCE`.
- Server v3: one thread, `poll()`, output queues with slow-reader eviction, idle timeouts, a self-pipe for
  a clean shutdown.

## Exercises

1. **Edge-triggered.** Copy `epoll_echo.c` to `et_echo.c` and make it edge-triggered: register each client
   once, with `EPOLLIN | EPOLLOUT | EPOLLET`, and delete `update()`, since edge-triggered events seem to
   need no changes to what is watched. Test it with 1 MB of data and with one line:

   ```console
   $ head -c 1000000 /dev/zero | tr '\0' 'x' | timeout 3 nc -N 127.0.0.1 6667 | wc -c
   $ printf 'hello\n' | timeout 3 nc -N 127.0.0.1 6667; echo "exit $?"
   ```

   What goes wrong, and why? Fix it.

<details><summary>Solution</summary>

The changes in `et_echo.c`, in `accept_all()`:

```c
        struct epoll_event ev = { .events = EPOLLIN | EPOLLOUT | EPOLLET, .data.ptr = c };
```

and `handle()`, which now looks at `rev` instead of `update()`:

```c
static void handle(struct client *c, uint32_t rev)
{
    if (rev & (EPOLLERR | EPOLLHUP)) {
        drop(c);
    } else {
        if (rev & EPOLLIN) {
            ssize_t n = read(c->fd, c->out + c->len, OUT_MAX - c->len);
            if (n > 0)
                c->len += (size_t)n;
            else if (n == 0)
                c->eof = true;
            else if (errno != EAGAIN && errno != EINTR)
                drop(c);
        }
        if (rev & EPOLLOUT) {
            ssize_t n = write(c->fd, c->out, c->len);
            if (n > 0) {
                memmove(c->out, c->out + n, c->len - (size_t)n);
                c->len -= (size_t)n;
            } else if (n < 0 && errno != EAGAIN && errno != EINTR) {
                drop(c);
            }
        }
    }
    if (c->eof && c->len == 0) {
        epoll_ctl(ep, EPOLL_CTL_DEL, c->fd, NULL);
        close(c->fd);
        printf("client %d left (%zu in total)\n", c->fd, --nclients);
        free(c);
    }
}
```

```console
$ head -c 1000000 /dev/zero | tr '\0' 'x' | timeout 3 nc -N 127.0.0.1 6667 | wc -c; echo "exit ${PIPESTATUS[2]}"
1000000
exit 124
$ printf 'hello\n' | timeout 3 nc -N 127.0.0.1 6667; echo "exit $?"
hello
exit 124
```

The data comes back, but the connections never end: `timeout` kills `nc` after 3 seconds (exit 124), on
musl too. `nc -N` sends `hello\n` and then its FIN, close together. epoll reports **one** edge,
"readable". `handle()` reads once and gets `hello\n`. The end of file is still waiting, but the socket was
already readable, so no new edge comes, and the server never makes the `read()` that would return 0.
Level-triggered epoll or `poll()` would report it again next turn.

The fix: with edge-triggered events, keep reading and writing until neither makes progress: `read()` until
`EAGAIN` (or the buffer is full), `write()` until `EAGAIN` (or the buffer is empty). Replace `handle()`:

```c
/* Edge-triggered: epoll reports a change of state only once, so read and
 * write until neither can make progress (EAGAIN, or the buffer is full or
 * empty). Stopping earlier could leave data that no new event announces. */
static void handle(struct client *c, uint32_t rev)
{
    if (rev & (EPOLLERR | EPOLLHUP))
        drop(c);
    bool progress = true;
    while (progress && !(c->eof && c->len == 0)) {
        progress = false;
        if (!c->eof && c->len < OUT_MAX) {
            ssize_t n = read(c->fd, c->out + c->len, OUT_MAX - c->len);
            if (n > 0) {
                c->len += (size_t)n;
                progress = true;
            } else if (n == 0) {
                c->eof = true;
            } else if (errno == EINTR) {
                progress = true;
            } else if (errno != EAGAIN) {
                drop(c);
            }
        }
        if (c->len > 0) {
            ssize_t n = write(c->fd, c->out, c->len);
            if (n > 0) {
                memmove(c->out, c->out + n, c->len - (size_t)n);
                c->len -= (size_t)n;
                progress = true;
            } else if (n < 0 && errno == EINTR) {
                progress = true;
            } else if (n < 0 && errno != EAGAIN) {
                drop(c);
            }
        }
    }
    if (c->eof && c->len == 0) {
        epoll_ctl(ep, EPOLL_CTL_DEL, c->fd, NULL);
        close(c->fd);
        printf("client %d left (%zu in total)\n", c->fd, --nclients);
        free(c);
    }
}
```

```console
$ head -c 1000000 /dev/zero | tr '\0' 'x' | timeout 3 nc -N 127.0.0.1 6667 | wc -c; echo "exit ${PIPESTATUS[2]}"
1000000
exit 0
$ printf 'hello\n' | timeout 3 nc -N 127.0.0.1 6667; echo "exit $?"
hello
exit 0
```

The same on musl. One more trap: keep `update()` and the bug hides, because every `EPOLL_CTL_MOD` re-arms
the descriptor, and epoll reports it again if it's ready. Edge-triggered code that works only thanks to
such a side effect breaks when someone "optimizes" the call away. Edge-triggered mode saves wake-ups but is
easy to get wrong; level-triggered is the safe default.

</details>

2. **Head-of-line blocking.** Measure what a blocking handler costs. In the project's `room.c`, make
   `cmd_ping()` sleep 3 seconds when the token is `slow`. Then let alice send `PING :slow`, and half a
   second later time how long bob's `NICK bob` and `QUIT` take.

<details><summary>Solution</summary>

In `room.c`, add `#include <unistd.h>`, and in `cmd_ping()`, after `token` is checked:

```c
    if (strcmp(token, "slow") == 0)
        sleep(3);                       /* pretend to do 3 s of work */
```

```console
$ build/glibc/parleyd -p 6667 2>server.log & server=$!; sleep 0.3
$ { printf 'NICK alice\r\nPING :slow\r\n'; sleep 4; } | nc -C 127.0.0.1 6667 >/dev/null & alice=$!
$ sleep 0.5; time (printf 'NICK bob\r\nQUIT\r\n' | nc -N 127.0.0.1 6667 | tail -n 1)
ERROR :Goodbye

real	0m2.500s
$ kill $server; wait $alice
```

The musl build: `real 0m2.497s`. Bob's two lines need microseconds, but he waited 2.5 seconds, the rest of
alice's 3-second `sleep()`. While one handler runs, the loop does nothing else. In server v2 bob had his
own thread and wouldn't have noticed. That's the price of an event loop: anything slow (a disk read, a DNS
lookup, a big computation) must be made non-blocking, or moved to a worker thread that reports back
through a pipe. Remove the `sleep()` again.

</details>

3. **Length-prefixed framing.** Text protocols end messages with a newline; binary protocols often put the
   length first. Write `frame_take()` for frames made of a 4-byte big-endian length followed by that many
   bytes. It returns "complete frame" (and removes it from the buffer), "need more bytes", or "too big"
   (over 1 MiB). Test it by feeding a stream of frames one byte at a time, ending with a length of about
   2 GiB. When must "too big" be detected?

<details><summary>Solution</summary>

`lenframe.c`:

```c
/* lenframe.c - length-prefixed framing: a 4-byte big-endian length, then
 * that many bytes of payload. Tested by feeding a stream 1 byte at a time. */
#define _POSIX_C_SOURCE 200809L
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define FRAME_MAX (1024 * 1024)     /* refuse bigger frames */

struct inbuf {
    unsigned char data[8192];
    size_t len;
};

enum frame_status { FRAME_NONE, FRAME_OK, FRAME_TOO_BIG };

/* If in starts with a complete frame, copy its payload to payload (cap
 * bytes of room), set *size, remove the frame from in and return FRAME_OK.
 * FRAME_NONE: wait for more bytes. FRAME_TOO_BIG: the length is over
 * FRAME_MAX or cap; the caller should close the connection, since there is
 * no way to find the start of the next frame without reading this one. */
static enum frame_status frame_take(struct inbuf *in, unsigned char *payload,
                                    size_t cap, size_t *size)
{
    if (in->len < 4)
        return FRAME_NONE;
    uint32_t n = (uint32_t)in->data[0] << 24 | (uint32_t)in->data[1] << 16
               | (uint32_t)in->data[2] << 8 | in->data[3];
    if (n > FRAME_MAX || n > cap || n > sizeof in->data - 4)
        return FRAME_TOO_BIG;           /* before waiting for n bytes */
    if (in->len - 4 < n)
        return FRAME_NONE;
    memcpy(payload, in->data + 4, n);
    *size = n;
    memmove(in->data, in->data + 4 + n, in->len - 4 - n);
    in->len -= 4 + n;
    return FRAME_OK;
}

/* Append a frame holding text to out; returns the new length. */
static size_t put_frame(unsigned char *out, size_t len, const char *text)
{
    uint32_t n = (uint32_t)strlen(text);
    out[len++] = (unsigned char)(n >> 24);
    out[len++] = (unsigned char)(n >> 16);
    out[len++] = (unsigned char)(n >> 8);
    out[len++] = (unsigned char)n;
    memcpy(out + len, text, n);
    return len + n;
}

int main(void)
{
    unsigned char stream[256];
    size_t len = 0;
    len = put_frame(stream, len, "hello");
    len = put_frame(stream, len, "");
    len = put_frame(stream, len, "no newline needed: any bytes go");
    len = put_frame(stream, len, "bye");
    stream[len++] = 0x7f;               /* a length of about 2 GiB */
    stream[len++] = 0xff;
    stream[len++] = 0xff;
    stream[len++] = 0xff;

    struct inbuf in = { .len = 0 };
    unsigned char payload[1000];
    size_t size;
    for (size_t i = 0; i < len; i++) {
        in.data[in.len++] = stream[i];  /* one byte arrives ... */
        enum frame_status st;           /* ... take every complete frame */
        while ((st = frame_take(&in, payload, sizeof payload, &size)) == FRAME_OK)
            printf("after byte %2zu: frame of %zu bytes: \"%.*s\"\n",
                   i + 1, size, (int)size, (const char *)payload);
        if (st == FRAME_TOO_BIG) {
            printf("after byte %2zu: frame too big: close the connection\n", i + 1);
            break;
        }
    }
    return 0;
}
```

```console
$ ./lenframe
after byte  9: frame of 5 bytes: "hello"
after byte 13: frame of 0 bytes: ""
after byte 48: frame of 31 bytes: "no newline needed: any bytes go"
after byte 55: frame of 3 bytes: "bye"
after byte 59: frame too big: close the connection
```

The musl build prints the same, and it's clean under ASan and UBSan. Each frame comes out exactly when its
last byte arrives, and the empty frame works too. The length is checked **as soon as its 4 bytes are
there**, before waiting for the payload; a server that waited for 2 GiB would let any client make it
buffer that much. The bytes are combined with shifts, not copied into a `uint32_t`, so the code is right
on any byte order ([Chapter 14](14-networking-basics.md)). Unlike a long line, a frame that's too big
can't be skipped: without reading it, the server can't tell where the next frame starts, so it must close
the connection.

</details>

4. **A callback event loop.** Libraries like libevent hide `poll()` behind callbacks: you register a
   descriptor with a function, and the loop calls it when the descriptor is ready. Write `el.h` and `el.c`
   with `el_add(fd, events, callback, ctx)`, `el_mod(fd, events)`, `el_del(fd)` and `el_run()`, and rewrite
   `poll_echo.c` on top of it as `el_echo.c`. `el_del()` must be safe to call from inside a callback.

<details><summary>Solution</summary>

`el.h`:

```c
/* el.h - a tiny event loop: call a function when a descriptor is ready. */
#ifndef EL_H
#define EL_H

/* revents: what poll() reported (POLLIN, POLLOUT, POLLHUP, POLLERR). */
typedef void el_callback(int fd, short revents, void *ctx);

/* Watch fd for events (POLLIN and/or POLLOUT); when it is ready, call
 * cb(fd, revents, ctx). Returns 0, or -1 if out of memory. */
int el_add(int fd, short events, el_callback *cb, void *ctx);

/* Change what fd is watched for. */
void el_mod(int fd, short events);

/* Stop watching fd (it is not closed). Safe inside a callback. */
void el_del(int fd);

/* Wait and dispatch until nothing is watched. Returns 0, or -1 if poll()
 * fails. */
int el_run(void);

#endif
```

`el.c`:

```c
/* el.c - a tiny event loop on poll(). */
#define _POSIX_C_SOURCE 200809L
#include "el.h"

#include <errno.h>
#include <poll.h>
#include <stdlib.h>

struct entry {
    int fd;                     /* -1: deleted, removed after dispatching */
    el_callback *cb;
    void *ctx;
};

static struct entry *entries;   /* entries[i] goes with fds[i] */
static struct pollfd *fds;
static size_t count, cap;

int el_add(int fd, short events, el_callback *cb, void *ctx)
{
    if (count == cap) {
        size_t n = cap ? 2 * cap : 16;
        struct entry *e = realloc(entries, n * sizeof *e);
        if (e == NULL)
            return -1;
        entries = e;
        struct pollfd *p = realloc(fds, n * sizeof *p);
        if (p == NULL)
            return -1;
        fds = p;
        cap = n;
    }
    entries[count] = (struct entry){ fd, cb, ctx };
    fds[count] = (struct pollfd){ .fd = fd, .events = events };
    count++;
    return 0;
}

static size_t find(int fd)
{
    for (size_t i = 0; i < count; i++)
        if (entries[i].fd == fd)
            return i;
    return count;
}

void el_mod(int fd, short events)
{
    size_t i = find(fd);
    if (i < count)
        fds[i].events = events;
}

void el_del(int fd)
{
    size_t i = find(fd);
    if (i < count)
        entries[i].fd = fds[i].fd = -1;   /* poll() skips negative fds */
}

int el_run(void)
{
    while (count > 0) {
        if (poll(fds, (nfds_t)count, -1) < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        /* Callbacks may add entries (after n: not ready yet, revents
         * unset) or delete them (fd -1: skipped). */
        size_t n = count;
        for (size_t i = 0; i < n; i++)
            if (entries[i].fd >= 0 && fds[i].revents != 0)
                entries[i].cb(entries[i].fd, fds[i].revents, entries[i].ctx);
        size_t kept = 0;
        for (size_t i = 0; i < count; i++) {
            if (entries[i].fd >= 0) {
                entries[kept] = entries[i];
                fds[kept++] = fds[i];
            }
        }
        count = kept;
    }
    free(entries);
    free(fds);
    entries = NULL;
    fds = NULL;
    cap = 0;
    return 0;
}
```

`el_echo.c`:

```c
/* el_echo.c - poll_echo.c on top of el.c: each descriptor has a callback.
 * usage: el_echo [PORT] */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "el.h"

#define OUT_MAX (64 * 1024)

struct client {
    bool eof;
    size_t len;
    char out[OUT_MAX];
};

static int set_nonblock(int fd)
{
    int flags = fcntl(fd, F_GETFL);
    return flags < 0 ? -1 : fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static void on_client(int fd, short rev, void *ctx)
{
    struct client *c = ctx;
    if (rev & (POLLERR | POLLHUP)) {
        c->eof = true;
        c->len = 0;
    }
    if (rev & POLLIN) {
        ssize_t n = read(fd, c->out + c->len, OUT_MAX - c->len);
        if (n > 0)
            c->len += (size_t)n;
        else if (n == 0 || (errno != EAGAIN && errno != EINTR))
            c->eof = true;
    }
    if ((rev & POLLOUT) && c->len > 0) {
        ssize_t n = write(fd, c->out, c->len);
        if (n > 0) {
            memmove(c->out, c->out + n, c->len - (size_t)n);
            c->len -= (size_t)n;
        } else if (n < 0 && errno != EAGAIN && errno != EINTR) {
            c->eof = true;
            c->len = 0;
        }
    }
    if (c->eof && c->len == 0) {
        el_del(fd);
        close(fd);
        free(c);
        printf("client %d left\n", fd);
        return;
    }
    el_mod(fd, (short)((!c->eof && c->len < OUT_MAX ? POLLIN : 0)
                       | (c->len > 0 ? POLLOUT : 0)));
}

static void on_listen(int lfd, short rev, void *ctx)
{
    (void)rev;
    (void)ctx;
    int fd;
    while ((fd = accept(lfd, NULL, NULL)) >= 0) {
        struct client *c = calloc(1, sizeof *c);
        if (c == NULL || set_nonblock(fd) < 0 || el_add(fd, POLLIN, on_client, c) < 0) {
            free(c);
            close(fd);
            continue;
        }
        printf("client %d connected\n", fd);
    }
}

int main(int argc, char *argv[])
{
    struct sockaddr_in addr = { .sin_family = AF_INET };
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons((unsigned short)(argc > 1 ? atoi(argv[1]) : 6667));
    int on = 1;
    int lfd = socket(AF_INET, SOCK_STREAM, 0);
    if (lfd < 0 || setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof on) < 0
            || bind(lfd, (struct sockaddr *)&addr, sizeof addr) < 0
            || listen(lfd, 128) < 0 || set_nonblock(lfd) < 0
            || el_add(lfd, POLLIN, on_listen, NULL) < 0) {
        perror("listen");
        return 1;
    }
    signal(SIGPIPE, SIG_IGN);
    setvbuf(stdout, NULL, _IOLBF, 0);
    if (el_run() < 0) {
        perror("poll");
        return 1;
    }
    return 0;
}
```

`Makefile` (build with `make`, or `make CC=musl-gcc LDFLAGS=-static` for musl):

```make
CC ?= cc
CFLAGS = -std=c99 -Wall -Wextra -pedantic

el_echo: el_echo.o el.o
	$(CC) $(LDFLAGS) -o $@ el_echo.o el.o

el_echo.o el.o: el.h

clean:
	rm -f el_echo *.o
```

```console
$ ./el_echo >/dev/null &
$ printf 'hello\nworld\n' | nc -N 127.0.0.1 6667
hello
world
$ ls /proc/$!/task | wc -l
1
$ bash ../ch17/clients.sh 6667 50
50 of 50 clients OK in 0.5 s
$ head -c 1000000 /dev/zero | tr '\0' x | nc -N 127.0.0.1 6667 | wc -c
1000000
$ kill %1
```

The same with the musl build. `el_del()` inside a callback only marks the entry with `fd = -1`, which
`poll()` and the dispatch loop skip; the array is compacted after the turn's callbacks have run. Removing
the entry at once would make the dispatch loop skip or repeat entries, the problem `parleyd` avoids by
closing connections only in `reap()`. With callbacks, each kind of descriptor gets its own function, with
no `if (i == 0)` for the listening socket: the style of libevent, libuv and most event-driven frameworks.

</details>

5. **Without your notes.** Answer from memory, then check:
   1. A client sends two lines with one `send()`. How many `recv()`s does the server need?
   2. What does `EAGAIN` from `write()` on a non-blocking socket mean? What happens if you ignore the return
      value of a successful `write()`?
   3. When is `epoll` better than `poll()`, and why does `parleyd` use `poll()`?
   4. Why does a server set `SO_REUSEADDR`?
   5. What is head-of-line blocking in an event loop?
   6. A client sends `NICK ` and then 10 GB without a newline. What should the server do?

<details><summary>Solution</summary>

1. Any number. TCP is a byte stream: the two lines may arrive in one `recv()`, or a byte at a time, as
   `trickle` showed. The server collects bytes in a buffer and takes out complete lines.
2. The send buffer is full; try again when `poll()` reports `POLLOUT`. A successful `write()` may be
   **short**: it returns how many bytes it took. Ignore that, and the rest is silently lost and the client
   sees broken messages. Keep the rest in the output queue.
3. With many connections, few active: `epoll` keeps the interest list in the kernel and returns only ready
   descriptors, so idle connections cost nothing. `parleyd` has at most 1000 clients, and `poll()` is
   POSIX, so it works on every Unix.
4. After a server stops, its old connections stay in `TIME_WAIT` for a while (up to a minute or two);
   without `SO_REUSEADDR`, a restarted server's `bind()` fails with `EADDRINUSE`
   ([Chapter 15](15-a-first-server.md)).
5. One slow handler, or one blocking call, makes every other client wait, because the loop handles one
   thing at a time.
6. Not buffer it all: limit the input buffer. `parleyd` caps a line at 512 bytes, answers `417`, and
   throws away the rest of the line ([Chapter 15](15-a-first-server.md)). A protocol that can't skip, like
   Exercise 3's frames, must close the connection.

</details>

Sources for this chapter: [REFERENCES.md](REFERENCES.md#chapter-18--the-event-loop)
