# Appendix D — POSIX Sockets Reference

This appendix lists every POSIX function the course uses for sockets, `poll`, signals, file descriptors, threads and the terminal. For each one it gives the prototype, the header, the feature-test macro that makes it visible, what it returns, the `errno` values the course handles, and the chapter that teaches it. Where glibc and musl differ, a note says how.

The prototypes are the POSIX ones. Each was checked against the glibc 2.35 headers in `/usr/include` and the musl 1.2.6 headers. A test program assigned every function to a pointer of exactly the type shown, and it compiled with `-std=c99 -Wall -Wextra -pedantic -Werror` against both libcs. The headers spell some things differently: glibc writes `__restrict` and names its parameters, musl writes `restrict` and leaves them out. Those differences don't change the types. The few real differences are listed in [Prototypes that differ](#prototypes-that-differ).

For the C language itself, see [Appendix A](appendix-a-c99-cheatsheet.md). For the protocol these calls carry, see [Appendix C](appendix-c-parley-protocol.md).

## Feature-test macros

`-std=c99` asks for ISO C99 and nothing else. POSIX functions are then hidden, or partly hidden, until you define a feature-test macro **before the first `#include`** ([Chapter 4](04-functions.md), [Chapter 9](09-preprocessor-and-building.md)):

| Macro | Gives you | Used by the course |
|---|---|---|
| `_POSIX_C_SOURCE 200809L` | POSIX.1-2008: everything in this appendix except the XSI parts | everywhere: `CPPFLAGS += -D_POSIX_C_SOURCE=200809L` in the Makefile |
| `_XOPEN_SOURCE 700` | POSIX.1-2008 plus the X/Open System Interfaces (XSI) | `parleyd.c` only, for `getrlimit`/`setrlimit` |
| `_DEFAULT_SOURCE`, `_BSD_SOURCE` | BSD and System V extras | no |
| `_GNU_SOURCE` | everything, including GNU extensions | no |

What the macro changes, measured with `-std=c99 -pedantic` and each header on its own:

| Symbols | Plain `-std=c99`, glibc | Plain `-std=c99`, musl | With `_POSIX_C_SOURCE 200809L` |
|---|---|---|---|
| `socket`, `bind`, `listen`, `accept`, `connect`, `shutdown`, `setsockopt`, `send`, `htons`, `inet_ntop`, `struct sockaddr_in6`, `SOMAXCONN` | visible | visible | visible |
| `read`, `write`, `close`, `pipe`, `dup2`, `fork`, `execv`, `_exit`, `fcntl`, `open`, `waitpid`, `ssize_t`, `STDIN_FILENO` | visible | visible | visible |
| `signal`, `raise`, `SIGTERM`, `pthread_*` (in `<pthread.h>`), `tcgetattr`, `tcsetattr`, `ioctl`, `TIOCGWINSZ`, `getrlimit` | visible | visible | visible |
| `getaddrinfo`, `freeaddrinfo`, `gai_strerror`, `getnameinfo`, `NI_NUMERICHOST`, `getopt`, `struct timespec`, `O_CLOEXEC` | **hidden** | visible | visible |
| `sigaction`, `SA_RESTART`, `sigaddset`, `sigprocmask`, `pthread_sigmask`, `kill`, `clock_gettime`, `CLOCK_MONOTONIC`, `nanosleep`, `localtime_r`, `strsignal`, `strerror_r`, `getline`, `fileno` | **hidden** | **hidden** | visible |
| `accept4`, `pipe2` (Linux extensions) | hidden (need `_GNU_SOURCE`) | visible | glibc: hidden; musl: visible |

So code that compiles with musl may still fail on glibc (`getaddrinfo` undeclared), and code that uses `accept4` builds on musl but not on glibc without `_GNU_SOURCE`. The course defines `_POSIX_C_SOURCE` for every file and uses only POSIX calls, so the same source builds on both.

One more glibc quirk: with only `_POSIX_C_SOURCE` defined, glibc's `signal()` gets System V semantics. See [`signal`](#processes-and-signals) below.

## Return values and errno

Three conventions cover almost everything here:

| Convention | Functions | How to test |
|---|---|---|
| `-1` and `errno` | the system calls: `read`, `socket`, `accept`, `poll`, `sigaction`, `tcgetattr`, … | `if (n < 0)`, then look at `errno` at once |
| An error code as the return value; `errno` is not set | `pthread_*` (an `errno`-style number), `getaddrinfo` and `getnameinfo` (`EAI_*`, negative on Linux) | `int rc = pthread_create(...); if (rc != 0) ... strerror(rc)`; `gai_strerror(rc)` for `EAI_*` |
| A special value | `getline` (`-1` at end of file or on error), `strftime` (`0` if it did not fit), `inet_pton` (`0` = not an address, `-1` = bad family) | see the entry |

`errno` is only meaningful right after a call has failed. Successful calls may change it.

The `errno` values the course handles, and what each libc's `strerror()` says ([Chapter 10](10-errors-and-debugging.md)). The numbers come from the Linux kernel, so they are the same on both. Only some texts differ:

| `errno` | Value | Meaning in the course | glibc text | musl text |
|---|---|---|---|---|
| `EINTR` | 4 | a signal handler ran during the call; try again, or check the flag the handler set | Interrupted system call | same |
| `EAGAIN` / `EWOULDBLOCK` | 11 | non-blocking fd: nothing to read, or no room to write; wait for `poll` | Resource temporarily unavailable | same |
| `EMFILE` | 24 | this process has no free fd: `accept` cannot create the client's fd | Too many open files | **No file descriptors available** |
| `ENFILE` | 23 | the whole system has no free fd | Too many open files in system | same |
| `ENOMEM` | 12 | out of memory (set by the course's own buffer code too) | Cannot allocate memory | **Out of memory** |
| `ERANGE` | 34 | `strtol`: the number does not fit in a `long` | Numerical result out of range | **Result not representable** |
| `EPIPE` | 32 | writing to a socket whose peer has gone (with `SIGPIPE` ignored) | Broken pipe | same |
| `ECONNRESET` | 104 | the peer reset the connection | Connection reset by peer | same |
| `ECONNREFUSED` | 111 | `connect`: nothing listens on that port | Connection refused | same |
| `EADDRINUSE` | 98 | `bind`: the port is taken | Address already in use | **Address in use** |
| `ENOTTY` | 25 | `tcgetattr` on something that is not a terminal | Inappropriate ioctl for device | **Not a tty** |

Never compare `strerror()` texts in code or tests; compare `errno` with the constant.

## File descriptors

```c
#include <unistd.h>
ssize_t read(int fd, void *buf, size_t count);
ssize_t write(int fd, const void *buf, size_t count);
int     close(int fd);
int     pipe(int fds[2]);
int     dup2(int oldfd, int newfd);
int     isatty(int fd);

#include <fcntl.h>
int     fcntl(int fd, int cmd, ...);
int     open(const char *path, int flags, ...);   /* ... = mode_t mode, with O_CREAT */
```

| Function | Returns | errno handled by the course | Taught in |
|---|---|---|---|
| `read` | bytes read; **0 at end of file** (the peer closed); `-1` | `EINTR`: retry. `EAGAIN`/`EWOULDBLOCK`: no data yet, back to `poll`. Others (`ECONNRESET`): drop the connection with `strerror(errno)` as the reason. | [Ch. 12](12-files-and-descriptors.md) |
| `write` | bytes written, possibly **fewer than asked** (keep the rest); `-1` | `EINTR`: retry. `EAGAIN`/`EWOULDBLOCK`: socket buffer full, wait for `POLLOUT`. `EPIPE`: the peer has gone. | [Ch. 12](12-files-and-descriptors.md) |
| `close` | `0`; `-1` | not checked on sockets. On Linux the fd is closed even when `close` fails, so never retry it. | [Ch. 12](12-files-and-descriptors.md) |
| `pipe` | `0`; `-1` | `EMFILE`. `fds[0]` is the read end, `fds[1]` the write end. Used for the self-pipe. | [Ch. 12](12-files-and-descriptors.md), [Ch. 13](13-processes-and-signals.md) |
| `fcntl` | depends on `cmd`; `-1` | `F_GETFL` returns the flags; `F_SETFL` with `flags \| O_NONBLOCK` makes an fd non-blocking (`set_nonblock` in `io.c`). | [Ch. 12](12-files-and-descriptors.md), [Ch. 18](18-event-loop.md) |
| `open` | a new fd (the lowest free number); `-1` | `open("/dev/null", O_RDONLY)` holds the spare fd for the `EMFILE` trick. | [Ch. 12](12-files-and-descriptors.md), [Ch. 21](21-robustness.md) |
| `dup2` | `newfd`; `-1` | The integration tests use it to send the server's stderr to a log file. | [Ch. 12](12-files-and-descriptors.md), [Ch. 13](13-processes-and-signals.md), [Ch. 21](21-robustness.md) |
| `isatty` | `1` for a terminal; `0` with `errno = ENOTTY` | decides between the full-screen and the line-mode client | [Ch. 12](12-files-and-descriptors.md), [Ch. 20](20-terminal-ui.md) |

glibc vs musl: identical. `O_NONBLOCK` is 2048 on both.

## Processes and signals

```c
#include <unistd.h>
pid_t fork(void);
int   execv(const char *path, char *const argv[]);
int   execvp(const char *file, char *const argv[]);
void  _exit(int status);
unsigned alarm(unsigned seconds);

#include <sys/wait.h>
pid_t waitpid(pid_t pid, int *status, int options);
/* WIFEXITED(status), WEXITSTATUS(status), WIFSIGNALED(status), WTERMSIG(status) */

#include <signal.h>
int  kill(pid_t pid, int sig);
int  raise(int sig);
int  sigaction(int sig, const struct sigaction *restrict act, struct sigaction *restrict oldact);
int  sigemptyset(sigset_t *set);
int  sigaddset(sigset_t *set, int sig);
int  sigprocmask(int how, const sigset_t *restrict set, sigset_t *restrict oldset);
int  sigsuspend(const sigset_t *mask);
int  pthread_sigmask(int how, const sigset_t *restrict set, sigset_t *restrict oldset);
void (*signal(int sig, void (*handler)(int)))(int);

struct sigaction {          /* the members you set; there are more */
    void   (*sa_handler)(int);   /* a function, SIG_DFL or SIG_IGN */
    sigset_t sa_mask;            /* signals blocked while the handler runs */
    int      sa_flags;           /* 0, SA_RESTART, ... */
    /* ... */
};

#include <string.h>
char *strsignal(int sig);
```

| Function | Returns | Notes | Taught in |
|---|---|---|---|
| `fork` | `0` in the child, the child's pid in the parent; `-1` | The integration tests start `parleyd` with `fork` + `execv`. | [Ch. 13](13-processes-and-signals.md), [Ch. 21](21-robustness.md) |
| `execv`, `execvp` | only on failure: `-1` | The child then calls `_exit(127)`, not `exit`, so it does not flush the parent's copied stdio buffers. | [Ch. 13](13-processes-and-signals.md) |
| `waitpid` | the pid; `0` with `WNOHANG` and nothing finished; `-1` | `EINTR`: retry. | [Ch. 13](13-processes-and-signals.md), [Ch. 21](21-robustness.md) |
| `kill`, `raise` | `0`; `-1` | `kill(pid, SIGTERM)` stops the test server; `raise` sends a signal to yourself (the tests use it). | [Ch. 13](13-processes-and-signals.md) |
| `sigaction` | `0`; `-1` | The course always uses it, with `sa_flags = 0` (no `SA_RESTART`), so a blocked call returns `EINTR` and the loop sees the flag. `SIGPIPE` is set to `SIG_IGN`. | [Ch. 13](13-processes-and-signals.md) |
| `sigemptyset`, `sigaddset` | `0`; `-1` | build `sa_mask` and the masks below | [Ch. 13](13-processes-and-signals.md) |
| `sigprocmask` | `0`; `-1` | blocks signals in a single-threaded process; `SIG_BLOCK`, `SIG_UNBLOCK`, `SIG_SETMASK` | [Ch. 13](13-processes-and-signals.md) |
| `pthread_sigmask` | **an error number**, `0` on success | the same, for one thread. Declared in `<signal.h>` on both libcs, so include that, not just `<pthread.h>`. | [Ch. 17](17-threads.md) |
| `sigsuspend`, `alarm` | `sigsuspend` always returns `-1`/`EINTR` | only in the Chapter 13 examples | [Ch. 13](13-processes-and-signals.md) |
| `signal` | the old handler; `SIG_ERR` | Avoid it; see the glibc note. The tests use it only for `SIG_IGN`/`SIG_DFL`, where the semantics don't matter. | [Ch. 13](13-processes-and-signals.md) |
| `strsignal` | a string | for the log line `got signal 15 (Terminated)` | [Ch. 13](13-processes-and-signals.md) |

A signal handler may call only async-signal-safe functions (`write`, `_exit`, `read`, …; not `printf` or `malloc`) and may only set `volatile sig_atomic_t` variables. The course's handlers set a flag and write one byte to the self-pipe, saving and restoring `errno` around the `write`.

glibc vs musl:

- **`signal()` semantics differ.** With strict feature macros (the course's `_POSIX_C_SOURCE`), glibc's `signal()` resets the handler after one delivery and does not restart interrupted calls (System V style). musl always restarts and keeps the handler (BSD style). `sigaction` behaves the same on both. This is why the course uses `sigaction`.
- **`strsignal` texts** are the same for the signals the course meets (`Terminated`, `Interrupt`, `Broken pipe`, `Window changed`). They differ for `SIGFPE` ("Floating point exception" vs "Arithmetic exception"), `SIGCHLD` and unknown numbers.
- `poll` returns `EINTR` when a handler runs, **even with `SA_RESTART`**, on both. Linux never restarts `poll`.

## Addresses and name resolution

```c
#include <netdb.h>
int  getaddrinfo(const char *restrict host, const char *restrict service,
                 const struct addrinfo *restrict hints, struct addrinfo **restrict result);
void freeaddrinfo(struct addrinfo *list);
const char *gai_strerror(int code);
int  getnameinfo(const struct sockaddr *restrict sa, socklen_t salen,
                 char *restrict host, socklen_t hostlen,
                 char *restrict serv, socklen_t servlen, int flags);

struct addrinfo {
    int              ai_flags;      /* hints: AI_PASSIVE, AI_NUMERICSERV, ... */
    int              ai_family;     /* AF_INET, AF_INET6, AF_UNSPEC (hints) */
    int              ai_socktype;   /* SOCK_STREAM */
    int              ai_protocol;
    socklen_t        ai_addrlen;
    struct sockaddr *ai_addr;
    char            *ai_canonname;
    struct addrinfo *ai_next;       /* the next result, or NULL */
};

#include <arpa/inet.h>
int         inet_pton(int af, const char *restrict src, void *restrict dst);
const char *inet_ntop(int af, const void *restrict src, char *restrict dst, socklen_t size);
uint16_t    htons(uint16_t x);    /* also ntohs, htonl, ntohl (in <arpa/inet.h> and <netinet/in.h>) */
```

The address structures, from `<sys/socket.h>` and `<netinet/in.h>`. Their sizes are the same on both libcs:

| Type | Size | Use |
|---|---|---|
| `struct sockaddr` | 16 | the generic type in every prototype; you cast to it |
| `struct sockaddr_in` | 16 | IPv4: `sin_family`, `sin_port` (network byte order), `sin_addr` |
| `struct sockaddr_in6` | 28 | IPv6: `sin6_family`, `sin6_port`, `sin6_addr`, … |
| `struct sockaddr_storage` | 128 | big enough for any address; use it for `accept` and `getsockname` |
| `socklen_t` | 4 | the length type, an unsigned integer of at least 32 bits |
| `INET6_ADDRSTRLEN` | 46 | buffer size for any numeric address as text (`INET_ADDRSTRLEN` is 16) |

| Function | Returns | How the course uses it | Taught in |
|---|---|---|---|
| `getaddrinfo` | `0` and a list in `*result`; an `EAI_*` code | `hints`: `AF_UNSPEC`, `SOCK_STREAM`, `AI_NUMERICSERV`, plus `AI_PASSIVE` for the server. The port is passed as text (`"6667"`). Try each result in order until one works. | [Ch. 14](14-networking-basics.md) |
| `freeaddrinfo` | — | frees the whole list; call it exactly once | [Ch. 14](14-networking-basics.md) |
| `gai_strerror` | text for an `EAI_*` code | **not** `strerror`: `getaddrinfo` does not set `errno`, except with `EAI_SYSTEM` | [Ch. 14](14-networking-basics.md) |
| `getnameinfo` | `0`; an `EAI_*` code | `NI_NUMERICHOST \| NI_NUMERICSERV`: numbers only, never a DNS lookup. Turns `127.0.0.1:41234` into text for the log. | [Ch. 14](14-networking-basics.md) |
| `inet_pton` | `1`; `0` if `src` is not a valid address; `-1` (`EAFNOSUPPORT`) | only in the Chapter 14 examples | [Ch. 14](14-networking-basics.md) |
| `inet_ntop` | `dst`; `NULL` (`ENOSPC` if too small) | only in the Chapter 14 examples | [Ch. 14](14-networking-basics.md) |
| `htons`, `ntohs` | the converted value | `ntohs(sin_port)` to read a port from an address | [Ch. 14](14-networking-basics.md) |

glibc vs musl:

- **Error texts differ.** `EAI_NONAME` is "Name or service not known" on glibc and "Name does not resolve" on musl. `EAI_SERVICE` is "Servname not supported for ai_socktype" vs "Unrecognized service". The codes are the same.
- **Out-of-range ports.** glibc truncates a numeric service to 16 bits (`"99999"` becomes port 34463, `"65536"` becomes 0). musl rejects them with `EAI_SERVICE`. The course checks the port range itself with `strtol` before calling `getaddrinfo`.
- **`localhost` with `AF_INET6`.** musl answers `localhost` itself. glibc reads `/etc/hosts` and may ask DNS. With `AF_UNSPEC`, as the course uses, both return the same results.
- **`hints == NULL`.** glibc then also returns a `SOCK_RAW` entry. Always pass hints.
- **Static linking.** A static glibc binary that calls `getaddrinfo` makes the linker warn: "Using 'getaddrinfo' in statically linked applications requires at runtime the shared libraries from the glibc version used for linking". It still works for `/etc/hosts` and DNS. musl has no such limit ([Chapter 22](22-shipping-and-libc-choices.md)).
- `NI_MAXHOST` is not POSIX; strict builds don't define it on glibc. Use `INET6_ADDRSTRLEN` for numeric hosts.
- `inet_pton` behaves the same on both. `AF_INET` accepts only the full dotted quad: `127.1` is rejected.

## Sockets

```c
#include <sys/socket.h>
int     socket(int domain, int type, int protocol);
int     setsockopt(int fd, int level, int name, const void *value, socklen_t len);
int     bind(int fd, const struct sockaddr *addr, socklen_t len);
int     listen(int fd, int backlog);
int     accept(int fd, struct sockaddr *restrict addr, socklen_t *restrict len);
int     connect(int fd, const struct sockaddr *addr, socklen_t len);
int     shutdown(int fd, int how);
int     getsockname(int fd, struct sockaddr *restrict addr, socklen_t *restrict len);
int     getpeername(int fd, struct sockaddr *restrict addr, socklen_t *restrict len);
ssize_t send(int fd, const void *buf, size_t len, int flags);
ssize_t recv(int fd, void *buf, size_t len, int flags);

#include <netinet/tcp.h>
/* TCP_NODELAY, for setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, ...) */
```

| Function | Returns | errno handled / how the course uses it | Taught in |
|---|---|---|---|
| `socket` | a new fd; `-1` | `socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol)` from a `getaddrinfo` result. On failure (`EAFNOSUPPORT` when IPv6 is off) try the next result. | [Ch. 14](14-networking-basics.md) |
| `setsockopt` | `0`; `-1` | `SOL_SOCKET`, `SO_REUSEADDR`, `int 1` before `bind`, so a restarted server can listen again while old connections are in `TIME_WAIT`. `IPPROTO_TCP`, `TCP_NODELAY` on each client so short lines go out at once. | [Ch. 15](15-a-first-server.md), [Ch. 18](18-event-loop.md), [Ch. 22](22-shipping-and-libc-choices.md) |
| `bind` | `0`; `-1` | `EADDRINUSE`: another server has the port (a test checks it). `EACCES`: ports below 1024 need privileges. Port 0 lets the kernel choose. | [Ch. 14](14-networking-basics.md), [Ch. 15](15-a-first-server.md) |
| `listen` | `0`; `-1` | backlog 16: the kernel queues up to that many connections that `accept` has not taken yet | [Ch. 14](14-networking-basics.md) |
| `accept` | a new fd for the client; `-1` | `EAGAIN`/`EWOULDBLOCK`: no more pending (the listening fd is non-blocking). `EINTR`: try later. `EMFILE`/`ENFILE`: close the spare fd, accept, send `ERROR :Server full`, close, reopen the spare. Others are logged. | [Ch. 14](14-networking-basics.md), [Ch. 18](18-event-loop.md), [Ch. 21](21-robustness.md) |
| `connect` | `0`; `-1` | `ECONNREFUSED`: no server; the client prints `cannot connect to 127.0.0.1:6667: Connection refused`. Try the next address. | [Ch. 14](14-networking-basics.md), [Ch. 16](16-a-first-client.md) |
| `shutdown` | `0`; `-1` | `SHUT_WR`: send end of file but keep reading. The client uses it after `QUIT`, the server before closing (linger). | [Ch. 16](16-a-first-client.md), [Ch. 21](21-robustness.md) |
| `getsockname` | `0`; `-1` | finds the real port after binding port 0 (tests), and the address for `listening on ...` | [Ch. 14](14-networking-basics.md) |
| `getpeername` | `0`; `-1` | only in examples; the server keeps the address from `accept` instead | [Ch. 16](16-a-first-client.md) |
| `send`, `recv` | like `write`, `read` | `read`/`write` do the same with `flags = 0`; the course uses those. `send` appears in the Chapter 16 examples. | [Ch. 16](16-a-first-client.md) |

Constants used: `AF_INET`, `AF_INET6`, `AF_UNSPEC`, `SOCK_STREAM`, `SOL_SOCKET`, `SO_REUSEADDR` (2), `IPPROTO_TCP`, `TCP_NODELAY` (1), `SHUT_RD`, `SHUT_WR`, `SHUT_RDWR`.

`SIGPIPE`: writing to a socket after the peer has closed it raises `SIGPIPE`, which kills the process by default. The course ignores `SIGPIPE` with `sigaction` and handles `EPIPE` from `write`. The first write after the peer closed usually succeeds; the peer answers with a reset, and later writes fail.

glibc vs musl:

- `SOMAXCONN` is 4096 on glibc and 128 on musl. The kernel caps the backlog at `/proc/sys/net/core/somaxconn` anyway. The course uses its own constant, 16.
- `EADDRINUSE` prints as "Address already in use" vs "Address in use".
- Everything else is the same: the prototypes, the constants and the behaviour. The kernel implements sockets; the libc only wraps the system calls.

## poll

```c
#include <poll.h>
int poll(struct pollfd fds[], nfds_t nfds, int timeout_ms);

struct pollfd {
    int   fd;        /* the fd to watch; a negative fd is skipped */
    short events;    /* what you want: POLLIN, POLLOUT */
    short revents;   /* what happened, filled in by poll */
};
```

| Returns | Meaning |
|---|---|
| `> 0` | the number of entries with a non-zero `revents` |
| `0` | the timeout expired; nothing happened |
| `-1` | error. `EINTR` (a signal arrived) is normal: the course checks its signal flags and loops. |

| Flag | In `events`? | In `revents` means |
|---|---|---|
| `POLLIN` (1) | yes | data to read, a connection to `accept`, or end of file (then `read` returns 0) |
| `POLLOUT` (4) | yes | room to write. Ask for it only while there is output queued, or `poll` returns at once, again and again. |
| `POLLERR` (8) | always reported | an error on the fd; a `read` or `write` will say which |
| `POLLHUP` (16) | always reported | the peer hung up. There may still be data to read first. |
| `POLLNVAL` (32) | always reported | the fd is not open: a bug |

`timeout_ms`: `-1` waits for ever, `0` returns at once, otherwise milliseconds. The server computes it from the next timer (idle, closing, shutdown). `poll` is taught in [Chapter 18](18-event-loop.md); the self-pipe example in [Chapter 13](13-processes-and-signals.md) uses it first.

glibc vs musl: identical (`sizeof(struct pollfd)` is 8; `nfds_t` is `unsigned long`).

## Time

```c
#include <time.h>
int        clock_gettime(clockid_t clock, struct timespec *now);
int        nanosleep(const struct timespec *request, struct timespec *remaining);
struct tm *localtime_r(const time_t *restrict t, struct tm *restrict result);
size_t     strftime(char *restrict s, size_t max, const char *restrict format,
                    const struct tm *restrict tm);

struct timespec { time_t tv_sec; long tv_nsec; };
```

| Function | Returns | How the course uses it | Taught in |
|---|---|---|---|
| `clock_gettime` | `0`; `-1` | `CLOCK_MONOTONIC` for every timer (`clock_ms` in `clock.c`). It never jumps when someone sets the system clock; `CLOCK_REALTIME` can. | [Ch. 18](18-event-loop.md) |
| `nanosleep` | `0`; `-1` with `EINTR` and the time left in `*remaining` | the tests sleep in a loop: `while (nanosleep(&ts, &ts) < 0 && errno == EINTR)` | [Ch. 16](16-a-first-client.md), [Ch. 21](21-robustness.md) |
| `localtime_r` | `result`; `NULL` | the thread-safe `localtime`: it fills your `struct tm` instead of a shared one | [Ch. 19](19-an-interactive-client.md) |
| `strftime` | bytes written, without the `'\0'`; **`0` if it did not fit** | `"%H:%M"` for the client's timestamps, `"%Y-%m-%d %H:%M:%S"` for the log | [Ch. 19](19-an-interactive-client.md) |

glibc vs musl: the same for valid `TZ` values and `/etc/localtime`; a static musl binary reads the same zone files. Only invalid `TZ` values differ (glibc uses the bogus name as the zone abbreviation, musl falls back to `UTC`). `time_t` is 64 bits on both.

## Threads

```c
#include <pthread.h>
int pthread_create(pthread_t *restrict thread, const pthread_attr_t *restrict attr,
                   void *(*start)(void *), void *restrict arg);
int pthread_join(pthread_t thread, void **result);
int pthread_detach(pthread_t thread);

int pthread_attr_init(pthread_attr_t *attr);
int pthread_attr_destroy(pthread_attr_t *attr);
int pthread_attr_setstacksize(pthread_attr_t *attr, size_t size);
int pthread_attr_getstacksize(const pthread_attr_t *restrict attr, size_t *restrict size);
int pthread_attr_setdetachstate(pthread_attr_t *attr, int state);  /* PTHREAD_CREATE_DETACHED */

int pthread_mutex_lock(pthread_mutex_t *m);
int pthread_mutex_unlock(pthread_mutex_t *m);
int pthread_cond_wait(pthread_cond_t *restrict c, pthread_mutex_t *restrict m);
int pthread_cond_signal(pthread_cond_t *c);
int pthread_cond_broadcast(pthread_cond_t *c);
/* static initializers: PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER */
```

All of these return **`0` or an error number**. They do not set `errno`; print the error with `strerror(rc)`. Taught in [Chapter 17](17-threads.md).

| Function | Errors worth knowing | Notes |
|---|---|---|
| `pthread_create` | `EAGAIN`: no resources for another thread | the thread runs `start(arg)`; `arg` must stay valid until the thread has used it |
| `pthread_join` | `EDEADLK`, `EINVAL` | waits for the thread and frees it. A thread must be joined or detached, not both. |
| `pthread_detach`, `PTHREAD_CREATE_DETACHED` | `EINVAL` | the thread frees itself when it ends |
| `pthread_attr_setstacksize` | `EINVAL` if below `PTHREAD_STACK_MIN` | sets the stack of new threads |
| `pthread_mutex_lock`/`unlock` | `EDEADLK` (error-checking mutexes only) | locking a normal mutex twice from one thread hangs |
| `pthread_cond_wait` | — | unlocks the mutex, waits, locks it again. It may wake **without** a signal (a spurious wakeup), so always wait in a `while (!condition)` loop. |
| `pthread_sigmask` | `EINVAL` | see [Processes and signals](#processes-and-signals). Block signals before creating threads, so only the main thread receives them. |

glibc vs musl:

- **The default stack size differs**: 8 MiB on glibc (taken from `ulimit -s`), 128 KiB on musl. A thread with a 256 KiB local array works on glibc and crashes on musl. Keep big buffers off thread stacks, or set the size with `pthread_attr_setstacksize`.
- No `-lpthread` is needed on either: since glibc 2.34, and always on musl, the thread functions are in libc. `-pthread` is still harmless, and the Chapter 17 Makefile uses it for older systems.

## The terminal

```c
#include <termios.h>
int tcgetattr(int fd, struct termios *t);
int tcsetattr(int fd, int when, const struct termios *t);  /* when: TCSANOW, TCSADRAIN, TCSAFLUSH */

struct termios {          /* the members you change */
    tcflag_t c_iflag;     /* input modes */
    tcflag_t c_oflag;     /* output modes */
    tcflag_t c_cflag;     /* control modes */
    tcflag_t c_lflag;     /* local modes */
    cc_t     c_cc[NCCS];  /* special characters, VMIN, VTIME */
    /* ... */
};

#include <sys/ioctl.h>    /* not POSIX, but on every Unix-like system */
int ioctl(int fd, unsigned long request, ...);   /* glibc; musl: int request */
/* ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) fills struct winsize { ws_row, ws_col, ... } */
```

| Function | Returns | errno handled / use | Taught in |
|---|---|---|---|
| `tcgetattr` | `0`; `-1` | `ENOTTY`: not a terminal. Save the settings so you can restore them. | [Ch. 20](20-terminal-ui.md) |
| `tcsetattr` | `0` if **any** of the changes worked; `-1` | `TCSAFLUSH`: apply after pending output is written, and throw away unread input. Restore the saved settings at exit (`atexit`) and before any error message. | [Ch. 20](20-terminal-ui.md) |
| `ioctl(TIOCGWINSZ)` | `0`; `-1` | the window size; the client falls back to 24 × 80 if it fails or reports 0. Called again after `SIGWINCH`. | [Ch. 20](20-terminal-ui.md) |

The flags the client's raw mode clears (`term.c`):

| Field | Flag | Cleared because |
|---|---|---|
| `c_lflag` | `ICANON` | read key by key, not line by line |
| `c_lflag` | `ECHO` | the client draws what you type |
| `c_lflag` | `ISIG` | Ctrl-C arrives as byte 3, not as `SIGINT` |
| `c_lflag` | `IEXTEN` | Ctrl-V arrives as a byte |
| `c_iflag` | `IXON` | Ctrl-S and Ctrl-Q arrive as bytes, instead of pausing output |
| `c_iflag` | `ICRNL` | Enter arrives as `\r`, not translated to `\n` |
| `c_iflag` | `BRKINT`, `INPCK`, `ISTRIP` | old serial-line settings; clear them for 8-bit input |

Then `c_cc[VMIN] = 1` and `c_cc[VTIME] = 0`: `read` returns as soon as one byte is there. `OPOST` stays on, so `\n` still prints as a new line.

glibc vs musl:

- `ioctl`'s second parameter is `unsigned long` on glibc and `int` on musl. Calls look the same; only a function pointer to `ioctl` would notice.
- `struct termios` is 60 bytes on both. Its speed fields are named `c_ispeed`/`c_ospeed` on glibc and `__c_ispeed`/`__c_ospeed` on musl; use `cfgetospeed` and `cfsetospeed` instead.
- POSIX.1-2024 adds `tcgetwinsize()` in `<termios.h>` as a portable `TIOCGWINSZ`. musl 1.2.6 has it; glibc 2.35 does not. The course uses `ioctl`.
- `tcgetattr` on a non-terminal fails with `ENOTTY` on both. The text differs: "Inappropriate ioctl for device" vs "Not a tty".

## Resource limits

```c
#define _XOPEN_SOURCE 700      /* getrlimit and setrlimit are XSI */
#include <sys/resource.h>
int getrlimit(int resource, struct rlimit *limit);
int setrlimit(int resource, const struct rlimit *limit);

struct rlimit {
    rlim_t rlim_cur;   /* the soft limit: what applies now */
    rlim_t rlim_max;   /* the hard limit: the most rlim_cur may be raised to */
};
```

| Function | Returns | How the course uses it | Taught in |
|---|---|---|---|
| `getrlimit(RLIMIT_NOFILE, &rl)` | `0`; `-1` | the most fds the process may have (`ulimit -n`). `parleyd` lowers `-m` to fit, leaving room for its own fds. `RLIM_INFINITY` means no limit. | [Ch. 21](21-robustness.md) |
| `setrlimit` | `0`; `-1` (`EPERM` to raise the hard limit) | the Chapter 21 example `fdlimit.c` lowers the limit to provoke `EMFILE` | [Ch. 21](21-robustness.md) |

glibc vs musl: the first parameter is the enum `__rlimit_resource_t` on glibc and `int` on musl. Calls with `RLIMIT_NOFILE` look the same.

## Other helpers

```c
#include <unistd.h>
int getopt(int argc, char *const argv[], const char *optstring);
extern char *optarg;
extern int optind, opterr, optopt;

#include <string.h>
char *strerror(int err);
int   strerror_r(int err, char *buf, size_t size);   /* the POSIX (XSI) version */

#include <stdio.h>
ssize_t getline(char **restrict line, size_t *restrict size, FILE *restrict stream);

#include <stdlib.h>
long strtol(const char *restrict s, char **restrict end, int base);
int  atexit(void (*fn)(void));
```

| Function | Returns | Notes | Taught in |
|---|---|---|---|
| `getopt` | the next option letter; `'?'` for an unknown option; `':'` for a missing argument if the string starts with `:`; `-1` when done | `":p:b:m:t:f:vTh"` for `parleyd`: the leading `:` stops `getopt` printing its own messages. Operands start at `argv[optind]`. | [Ch. 3](03-control-flow.md), [Ch. 22](22-shipping-and-libc-choices.md) |
| `strerror` | the text for an `errno` value | not thread-safe in theory; fine in the single-threaded server | [Ch. 10](10-errors-and-debugging.md) |
| `strerror_r` | `0`; an error number (`ERANGE`: buffer too small) | thread-safe | [Ch. 10](10-errors-and-debugging.md) |
| `getline` | the line length; `-1` at end of file or on error (`ENOMEM`) | the client read standard input with it up to Chapter 18; `free(line)` at the end | [Ch. 12](12-files-and-descriptors.md) |
| `strtol` | the number | set `errno = 0` first; then `ERANGE` means too big, `end == s` means no digits, `*end != '\0'` means junk after the number | [Ch. 6](06-strings.md) |
| `atexit` | `0`; non-zero | the client registers the function that restores the terminal | [Ch. 13](13-processes-and-signals.md), [Ch. 20](20-terminal-ui.md) |

glibc vs musl:

- **`getopt`.** Neither permutes with the course's macros: options must come before operands. With `_GNU_SOURCE`, glibc moves options found after operands to the front, and musl never does. The error messages differ too: "invalid option -- 'x'" vs "unrecognized option: x".
- **`strerror_r`.** With `_GNU_SOURCE`, glibc declares a different `strerror_r` that returns `char *` and may leave `buf` untouched. musl has only the POSIX one. With `_POSIX_C_SOURCE` both have the POSIX version.
- **`strerror` texts** differ for many values; see [Return values and errno](#return-values-and-errno).

## Prototypes that differ

Everything above has the same type on both libcs, except:

| Declaration | glibc 2.35 | musl 1.2.6 | Matters when |
|---|---|---|---|
| `ioctl` | `int ioctl(int, unsigned long, ...)` | `int ioctl(int, int, ...)` | you take its address or declare it yourself |
| `getrlimit`, `setrlimit` | first parameter `__rlimit_resource_t` (an enum) | `int` | the same |
| `signal` | written with the type `__sighandler_t` | written out as `void (*)(int)` | never: the types are the same |
| `SOMAXCONN` | 4096 | 128 | you pass it to `listen` |

Spelling only, the same type: glibc uses `__restrict` and parameter names, musl `restrict` and none.

Same on both (checked with `sizeof` and the constants): `struct sockaddr` 16, `sockaddr_in` 16, `sockaddr_in6` 28, `sockaddr_storage` 128, `addrinfo` 48, `pollfd` 8, `termios` 60, `sigaction` 152, `sigset_t` 128, `time_t` 8; `POLLIN`/`POLLOUT`/`POLLERR`/`POLLHUP`/`POLLNVAL` = 1/4/8/16/32; `SO_REUSEADDR` 2, `TCP_NODELAY` 1, `O_NONBLOCK` 2048, `MSG_NOSIGNAL` 16384, `EAGAIN` = `EWOULDBLOCK` = 11. The layouts must match, because the kernel reads them.

## Not used in the course, but you will meet them

| API | What it is | Why the course does without it |
|---|---|---|
| `select` (`<sys/select.h>`) | the older `poll`: three `fd_set` bitmaps and a `struct timeval` | it cannot watch fd numbers of `FD_SETSIZE` (1024) or more, and the sets must be rebuilt before every call |
| `epoll_create1`, `epoll_ctl`, `epoll_wait` (`<sys/epoll.h>`) | Linux: the kernel remembers the fd set, so each wait costs per ready fd, not per watched fd | not POSIX. `poll` is fast enough for a few hundred clients. On the BSDs and macOS the equivalent is `kqueue`. Both libcs provide `epoll`. |
| `accept4(fd, addr, len, SOCK_NONBLOCK \| SOCK_CLOEXEC)` | `accept` and `fcntl` in one call | new in POSIX.1-2024, not in the 2008 edition the course targets; needs `_GNU_SOURCE` on glibc (musl always declares it) |
| `pipe2(fds, O_NONBLOCK \| O_CLOEXEC)` | `pipe` and `fcntl` in one call | new in POSIX.1-2024; needs `_GNU_SOURCE` on glibc 2.35 |
| `SOCK_NONBLOCK`, `SOCK_CLOEXEC` in `socket()`'s type | set the flags at creation | new in POSIX.1-2024 (both libcs define them even under plain `-std=c99`) |
| `send(fd, buf, len, MSG_NOSIGNAL)` | report `EPIPE` without raising `SIGPIPE` | POSIX.1-2008 has it (a [Chapter 16](16-a-first-client.md#exercises) exercise), but only for `send`. Ignoring `SIGPIPE` once covers `write` too. |
| `ppoll`, `pselect` | `poll`/`select` with an atomic signal mask | `ppoll` is new in POSIX.1-2024. The self-pipe does the same job with POSIX 2008. |
| `signalfd` (`<sys/signalfd.h>`) | Linux: read signals from an fd | not POSIX |
| `sendmsg`, `recvmsg`, `writev`, `readv` | scatter/gather I/O, fd passing | one buffer per connection is enough |
| `getifaddrs`, `if_nametoindex` | list network interfaces | parley binds one address |

## Summary

- Define `_POSIX_C_SOURCE 200809L` (or `_XOPEN_SOURCE 700` for XSI calls such as `getrlimit`) before the first `#include`. Without it, glibc hides `getaddrinfo` and both libcs hide `sigaction`.
- System calls return `-1` and set `errno`. `pthread_*` return an error number; `getaddrinfo` and `getnameinfo` return `EAI_*` codes for `gai_strerror`.
- Handle `EINTR` (retry or check flags), `EAGAIN`/`EWOULDBLOCK` (wait for `poll`), and `EMFILE`/`ENFILE` (the spare fd). A `read` of 0 means the peer closed.
- The prototypes, constants and structure layouts are the same on glibc and musl, except `ioctl`, `getrlimit`/`setrlimit` and `SOMAXCONN`.
- The libcs differ in behaviour around these calls: `signal()` semantics, `getopt` permutation, `strerror_r` under `_GNU_SOURCE`, out-of-range ports in `getaddrinfo`, thread stack size, and many error texts.
- Compare `errno` values, never `strerror` texts.

Sources for this appendix: [REFERENCES.md](REFERENCES.md#appendix-d--posix-sockets-reference)
