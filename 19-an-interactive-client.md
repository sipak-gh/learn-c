# Chapter 19 — An Interactive Client

The [Chapter 16](16-a-first-client.md) client waits for one thing at a time: while it waits for your next
line, it can't see what others say. This chapter fixes that with `poll()` ([Chapter 18](18-event-loop.md)):
one loop watches the keyboard and the server. You'll also learn to tell a terminal from a pipe, turn a
`time_t` into `[14:05]`, and choose exit codes. At the end, `parley` is a real line-mode chat client;
[Chapter 20](20-terminal-ui.md) gives it a full-screen UI.

## poll() works on stdin too

`poll()` takes any file descriptor, not just sockets. stdin is fd 0, and "readable" depends on what it is:

| stdin is… | `poll()` reports `POLLIN` when… | `read()` then returns… |
|---|---|---|
| a terminal | you press Enter (the terminal hands over whole lines; [Chapter 20](20-terminal-ui.md) changes that) | the line, with its `\n` |
| a pipe | the writer wrote something, or closed its end | the bytes, or 0 at end of file |
| a regular file | always | the next bytes, or 0 at end of file |

When a pipe's writer closes its end, Linux reports `POLLHUP` (often with `POLLIN`). That is not an error:
`read()` returns the last bytes, then 0.

The `timeout` is in milliseconds: -1 waits forever, 0 doesn't wait, 1000 waits at most a second.
`poll()` returns the number of ready fds, so 0 means "timed out".

`examples/ch19/ticker.c`

```c
/* ticker.c - wait for input with poll(), but never longer than 1 second. */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(void)
{
    for (;;) {
        struct pollfd pfd = { .fd = STDIN_FILENO, .events = POLLIN };
        int ready = poll(&pfd, 1, 1000);         /* timeout in milliseconds */
        if (ready < 0) {
            if (errno == EINTR)
                continue;
            perror("poll");
            return 1;
        }
        if (ready == 0) {                        /* nothing within 1 s */
            printf("tick\n");
            fflush(stdout);
            continue;
        }
        /* POLLIN: data to read. POLLHUP: the writer is gone. Either way,
         * read() tells us what happened, and won't block now. */
        char buf[256];
        ssize_t n = read(STDIN_FILENO, buf, sizeof buf);
        if (n < 0) {
            perror("read");
            return 1;
        }
        if (n == 0) {
            printf("end of input\n");
            return 0;
        }
        printf("read %zd bytes: %.*s", n, (int)n, buf);
        fflush(stdout);
    }
}
```

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o ticker ticker.c
$ { sleep 1.5; echo hello; sleep 1.2; printf 'one\ntwo\n'; } | ./ticker
tick
read 6 bytes: hello
tick
read 8 bytes: one
two
end of input
```

The musl build (`musl-gcc ... -static`) prints the same.

- `"one\ntwo\n"` came from **one** `read()`. `read()` returns what is there, not one line. Splitting into
  lines is your job ([Chapter 8](08-memory.md)'s framing).
- Run `./ticker` without the pipe and type. It ticks while you think; each line arrives when you press
  Enter. Ctrl-D on an empty line is end of input.

## Don't mix poll() with stdio input

`poll()` sees the fd, not stdio's buffer. That makes `poll()` + `fgets()` a trap:

`examples/ch19/pollbug.c`

```c
/* pollbug.c - why poll() and stdio input don't mix. */
#define _POSIX_C_SOURCE 200809L
#include <poll.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

static double seconds(void)              /* time since the first call */
{
    static struct timespec start;
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    if (start.tv_sec == 0 && start.tv_nsec == 0)
        start = now;
    return (double)(now.tv_sec - start.tv_sec) + (now.tv_nsec - start.tv_nsec) / 1e9;
}

int main(void)
{
    char line[256];
    seconds();
    for (;;) {
        struct pollfd pfd = { .fd = STDIN_FILENO, .events = POLLIN };
        if (poll(&pfd, 1, -1) < 0)
            return 1;
        /* poll() said "the fd is readable", so we read ONE line... */
        if (fgets(line, sizeof line, stdin) == NULL)
            break;
        printf("%.1f s: %s", seconds(), line);
        fflush(stdout);
        /* ...but fgets() read everything that was there into stdin's
         * buffer. The fd is empty now, so the next poll() sleeps, while
         * the other lines wait in a buffer poll() knows nothing about. */
    }
    printf("%.1f s: end of input\n", seconds());
    return 0;
}
```

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o pollbug pollbug.c
$ { printf 'one\ntwo\nthree\n'; sleep 2; } | ./pollbug
0.0 s: one
2.0 s: two
2.0 s: three
2.0 s: end of input
```

All three lines were there at once, but "two" and "three" show up 2 seconds late:

1. `poll()` says fd 0 is readable.
2. `fgets()` calls `read()` once, gets all 14 bytes into stdin's buffer, and returns only "one".
3. The fd is now empty. The next `poll()` sleeps while "two" and "three" wait in a buffer it can't see.
4. After 2 s the writer closes the pipe. `poll()` wakes and `fgets()` finds the buffered lines.

In a chat client, "2 s" means "until someone else says something". Both libcs behave the same.

The rule: **if you `poll()` an fd, read it with `read()` into your own buffer.** `parley` uses
`read_into()` ([Chapter 12](12-files-and-descriptors.md)) and `msg_take_line()`
([Chapter 8](08-memory.md)), the server's framing.

Output is fine through stdio, as long as you `fflush(stdout)` before sleeping in `poll()`. Otherwise
printed text sits in the buffer while the program waits.

## A tiny nc: stdin and a socket together

`pollcat` is the core of a chat client in about 80 lines. It copies stdin to a TCP server and the
server's replies to stdout, whichever comes first.

`examples/ch19/pollcat.c`

```c
/* pollcat.c - a tiny nc: copy stdin to a TCP server and the server's
 * replies to stdout, both at once, with poll(). */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <netdb.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static int connect_to(const char *host, const char *port)
{
    struct addrinfo hints = { .ai_family = AF_UNSPEC, .ai_socktype = SOCK_STREAM };
    struct addrinfo *res;
    int err = getaddrinfo(host, port, &hints, &res);
    if (err != 0) {
        fprintf(stderr, "pollcat: %s: %s\n", host, gai_strerror(err));
        return -1;
    }
    int fd = -1;
    for (struct addrinfo *ai = res; ai != NULL && fd < 0; ai = ai->ai_next) {
        fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd >= 0 && connect(fd, ai->ai_addr, ai->ai_addrlen) < 0) {
            close(fd);
            fd = -1;
        }
    }
    if (fd < 0)
        fprintf(stderr, "pollcat: cannot connect: %s\n", strerror(errno));
    freeaddrinfo(res);
    return fd;
}

/* Copy what one read() gets from 'from' to 'to'. Returns the byte count,
 * 0 at end of file, -1 on error. (A real program loops over short
 * writes: see write_all in Chapter 12.) */
static ssize_t pass(int from, int to)
{
    char buf[4096];
    ssize_t n = read(from, buf, sizeof buf);
    if (n > 0 && write(to, buf, (size_t)n) != n)
        return -1;
    return n;
}

int main(int argc, char *argv[])
{
    if (argc != 3) {
        fprintf(stderr, "usage: pollcat HOST PORT\n");
        return 2;
    }
    int sock = connect_to(argv[1], argv[2]);
    if (sock < 0)
        return 1;

    int in = STDIN_FILENO;               /* -1 once stdin is at its end */
    for (;;) {
        struct pollfd fds[2] = {
            { .fd = sock, .events = POLLIN },
            { .fd = in,   .events = POLLIN },  /* poll() skips fd -1 */
        };
        if (poll(fds, 2, -1) < 0) {
            if (errno == EINTR)
                continue;
            perror("poll");
            return 1;
        }
        if (fds[1].revents != 0 && pass(in, sock) <= 0) {
            in = -1;
            shutdown(sock, SHUT_WR);     /* "I'm done talking", keep listening */
        }
        if (fds[0].revents != 0) {
            ssize_t n = pass(sock, STDOUT_FILENO);
            if (n <= 0) {
                fprintf(stderr, "pollcat: %s\n",
                        n == 0 ? "the server closed the connection" : strerror(errno));
                break;
            }
        }
    }
    close(sock);
    return 0;
}
```

- **An fd of -1 is skipped.** `poll()` ignores negative fds and sets their `revents` to 0. When stdin
  ends, `pollcat` sets `in = -1` instead of rebuilding the array.
- **End of stdin is a half-close.** `shutdown(sock, SHUT_WR)` ([Chapter 16](16-a-first-client.md)) makes
  the server read end of file ("I'm done talking"). The socket stays open for reading, so the server's last
  replies still arrive. `close()` would throw them away.

Start the Chapter 18 server (`build/glibc/parleyd` in your project) in another terminal and talk the
protocol by hand:

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o pollcat pollcat.c
$ printf 'NICK alice\nMSG :hi\n' | ./pollcat localhost 6667; echo "exit $?"
:parley 001 alice :Welcome to parley, alice
:alice JOIN
:parley 353 alice :alice
:parley 366 alice :End of NAMES
:alice MSG :hi
pollcat: the server closed the connection
exit 0
```

`printf` finished at once, so `pollcat` half-closed at once. The server still answered both lines,
*then* saw end of file and closed. Its log:

```text
parleyd: info: alice joined
parleyd: info: alice left (Connection closed)
```

`nc` can play the server. In terminal 1, a "server" that says two things and leaves:

```console
$ { echo 'hello from the server'; sleep 1; echo 'bye'; } | nc -l 127.0.0.1 6667
hi from the client
```

In terminal 2:

```console
$ { echo 'hi from the client'; sleep 2; } | ./pollcat 127.0.0.1 6667; echo "exit $?"
hello from the server
bye
pollcat: the server closed the connection
exit 0
```

(`nc -l 127.0.0.1 6667` is the OpenBSD netcat most distributions ship; "traditional" netcat wants
`nc -l -p 6667`.)

When nothing listens, or the name is wrong:

```console
$ ./pollcat 127.0.0.1 6667; echo "exit $?"
pollcat: cannot connect: Connection refused
exit 1
$ ./pollcat no-such-host.invalid 6667
pollcat: no-such-host.invalid: Name or service not known
```

musl says `Name does not resolve`: `gai_strerror()` texts differ, like `strerror()` texts
([Chapter 10](10-errors-and-debugging.md)).

## Is it a terminal? isatty()

Is a human watching? `isatty(fd)` (POSIX, `<unistd.h>`) returns 1 if `fd` is a terminal, 0 otherwise.

`examples/ch19/istty.c`

```c
/* istty.c - which of stdin, stdout and stderr is a terminal? */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <unistd.h>

int main(void)
{
    const char *names[] = { "stdin", "stdout", "stderr" };
    for (int fd = 0; fd < 3; fd++)
        fprintf(stderr, "fd %d (%s): %s\n", fd, names[fd],
                isatty(fd) ? "a terminal" : "not a terminal");
    return 0;
}
```

It reports on stderr, so redirecting stdout doesn't hide it. In a terminal:

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o istty istty.c
$ ./istty
fd 0 (stdin): a terminal
fd 1 (stdout): a terminal
fd 2 (stderr): a terminal
$ ./istty | cat
fd 0 (stdin): a terminal
fd 1 (stdout): not a terminal
fd 2 (stderr): a terminal
$ ./istty < /dev/null
fd 0 (stdin): not a terminal
fd 1 (stdout): a terminal
fd 2 (stderr): a terminal
```

`ls` prints columns on a terminal and one name per line into a pipe; `grep --color=auto` colors only on a
terminal. Use it for:

- Decorations for humans (colors, timestamps), so scripts and tests get clean, stable output. `parley`
  stamps chat lines `[14:05]` only when stdout is a terminal.
- Terminal tricks: [Chapter 20](20-terminal-ui.md)'s full-screen UI needs stdin and stdout to be terminals.

## Telling the time

C99's `<time.h>` gets the calendar time and formats it; POSIX adds thread-safe variants.

| Function | Standard | Does |
|---|---|---|
| `time_t time(time_t *t)` | C99 | seconds since 1970-01-01 00:00:00 UTC (the "Unix epoch") |
| `struct tm *localtime_r(const time_t *t, struct tm *out)` | POSIX | splits `*t` into fields, in local time; NULL on error |
| `struct tm *gmtime_r(const time_t *t, struct tm *out)` | POSIX | the same, in UTC |
| `size_t strftime(char *s, size_t max, const char *fmt, const struct tm *tm)` | C99 | formats fields as text, like `printf` for dates |

C99's `localtime()` and `gmtime()` (no `_r`) return a pointer to **one static `struct tm`** in the
library: the next call, from anywhere, overwrites it, and two threads can corrupt it. The `_r`
("re-entrant") versions fill a `struct tm` you own. Use them.

`struct tm` traps:

| Field | Range | Trap |
|---|---|---|
| `tm_year` | years since 1900 | 2026 is 126 |
| `tm_mon` | 0–11 | October is 9 |
| `tm_mday` | 1–31 | this one starts at 1 |
| `tm_wday` | 0–6 | 0 is Sunday |
| `tm_hour`, `tm_min`, `tm_sec` | 0–23, 0–59, 0–60 | 60 is a leap second |
| `tm_isdst` | >0, 0, <0 | daylight saving time in effect, not in effect, unknown |

`strftime()` hides all of that:

| Conversion | Gives | Example |
|---|---|---|
| `%Y-%m-%d` | date | `2026-10-30` |
| `%H:%M:%S` | time, 24-hour | `16:00:00` |
| `%A`, `%B` | weekday and month names | `Friday`, `October` |
| `%Z`, `%z` | time zone name and offset | `EDT`, `-0400` |
| `%%` | a `%` | `%` |

`strftime()` returns the bytes written, without the `'\0'`. If the result doesn't fit in `max` bytes, it
returns 0 and the buffer contents are undefined. Check for 0.

`examples/ch19/stamp.c`

```c
/* stamp.c - from a time_t to text: localtime_r, gmtime_r, strftime. */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static void show(const char *label, const struct tm *tm)
{
    char buf[64];
    /* strftime returns 0 if the result (plus its '\0') does not fit. */
    if (strftime(buf, sizeof buf, "%Y-%m-%d %H:%M:%S %Z", tm) == 0)
        buf[0] = '\0';
    printf("%-6s %s  (%%H:%%M -> ", label, buf);
    strftime(buf, sizeof buf, "[%H:%M]", tm);
    printf("%s)\n", buf);
}

int main(int argc, char *argv[])
{
    /* A time_t counts seconds since 1970-01-01 00:00:00 UTC. With an
     * argument we use that number, otherwise the time right now. */
    time_t t = argc > 1 ? (time_t)strtoll(argv[1], NULL, 10) : time(NULL);
    struct tm tm;

    printf("time_t %lld\n", (long long)t);
    if (gmtime_r(&t, &tm) != NULL)       /* broken down, in UTC */
        show("UTC", &tm);
    if (localtime_r(&t, &tm) != NULL)    /* broken down, in local time (TZ) */
        show("local", &tm);
    printf("fields: tm_year=%d tm_mon=%d tm_mday=%d tm_wday=%d tm_isdst=%d\n",
           tm.tm_year, tm.tm_mon, tm.tm_mday, tm.tm_wday, tm.tm_isdst);
    return 0;
}
```

With a fixed `time_t`, any machine in the same time zone (here America/Toronto) prints the same:

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o stamp stamp.c
$ ./stamp 1793390400
time_t 1793390400
UTC    2026-10-30 20:00:00 GMT  (%H:%M -> [20:00])
local  2026-10-30 16:00:00 EDT  (%H:%M -> [16:00])
fields: tm_year=126 tm_mon=9 tm_mday=30 tm_wday=5 tm_isdst=1
$ TZ=Europe/Paris ./stamp 1793390400
time_t 1793390400
UTC    2026-10-30 20:00:00 GMT  (%H:%M -> [20:00])
local  2026-10-30 21:00:00 CET  (%H:%M -> [21:00])
fields: tm_year=126 tm_mon=9 tm_mday=30 tm_wday=5 tm_isdst=0
$ TZ=Asia/Tokyo ./stamp 1793390400 | grep local
local  2026-10-31 05:00:00 JST  (%H:%M -> [05:00])
```

The `TZ` environment variable picks "local":

- unset: the system zone, `/etc/localtime` (here a link to `/usr/share/zoneinfo/America/Toronto`).
- `TZ=Europe/Paris`: the file `/usr/share/zoneinfo/Europe/Paris`.
- `TZ=UTC`: no offset. Handy for tests, whose output then doesn't depend on the machine.

A `time_t` is the same everywhere; only its local *display* changes. Store and send `time_t` (or UTC),
and convert to local time at the last moment, for a human.

### glibc vs musl: time zone names

Build `stamp.c` with both libraries and compare:

```console
$ musl-gcc -std=c99 -Wall -Wextra -pedantic -static -o stamp-musl stamp.c
$ ./stamp 1793390400 | grep UTC
UTC    2026-10-30 20:00:00 GMT  (%H:%M -> [20:00])
$ ./stamp-musl 1793390400 | grep UTC
UTC    2026-10-30 20:00:00 UTC  (%H:%M -> [20:00])
$ TZ=Nowhere/Bogus ./stamp 1793390400 | grep local
local  2026-10-30 20:00:00 Nowhere  (%H:%M -> [20:00])
$ TZ=Nowhere/Bogus ./stamp-musl 1793390400 | grep local
local  2026-10-30 20:00:00 UTC  (%H:%M -> [20:00])
```

| | glibc 2.35 | musl 1.2.6 |
|---|---|---|
| `%Z` after `gmtime_r` | `GMT` | `UTC` |
| `TZ=Nowhere/Bogus` | UTC time, named `Nowhere` | UTC time, named `UTC` |
| valid `TZ` (unset, `Europe/Paris`, `UTC`, …) | identical | identical |

- Both read the same zone files. Even the static musl binary uses `/etc/localtime` and
  `/usr/share/zoneinfo`, so `parley` shows the same local time with either.
- Neither reports a bad `TZ`; both quietly fall back to UTC.
- **musl is simpler and more honest here**: it calls UTC "UTC" both times. glibc's `Nowhere` makes a typo
  look like a real zone.
- Portable code uses valid `TZ` names and doesn't parse `%Z`. `parley` prints only `%H:%M`, identical on
  both.

## Exit codes

The exit status is a program's one-number answer to "did it work?". The shell keeps it in `$?`; `&&`,
`||`, `if` and `make` act on it:

| Status | Meaning |
|---|---|
| 0 | success |
| 1 | failure |
| 2 | wrong usage (bad option or argument) |
| 128 + N | killed by signal N (the shell reports it; [Chapter 13](13-processes-and-signals.md)) |

For a chat client, "success" means "you left because you wanted to". `parley` v2:

| Event | What happens | Exit |
|---|---|---|
| end of stdin (Ctrl-D, or the end of a piped script) | sends `QUIT`, reads until the server closes | 0 |
| `/quit [reason]` | sends `QUIT :reason`, reads until the server closes | 0 |
| Ctrl-C (SIGINT) or SIGTERM | sends `QUIT`, as above | 0 |
| a second Ctrl-C while leaving | gives up: `parley: error: interrupted` | 1 |
| the server closes first | prints `-- Disconnected: reason` | 1 |
| cannot connect | `parley: error: cannot connect to ...` | 1 |
| usage error | message and "Try 'parley --help'" | 2 |

So `printf 'hello\n' | parley -n bot || echo "the server is gone"` does what it says.

The two ends of the connection differ:

- **End of stdin**: you're done talking. Send `QUIT`, half-close, and listen until the server closes, to
  see its last replies.
- **The server closes**: nothing is left to wait for. Stop at once, even if stdin has more.

## Slash commands, revisited

`parley` already has a dispatch table ([Chapter 5](05-pointers-and-arrays.md)): an array of
`struct command { name, args, help, run }`; `run_command()` finds the name and calls through the function
pointer. Since the table holds the help texts, `/help` loops over it and never forgets a command. A new
command is one function plus one row; the exercises add `/time` and `/ping` that way.

What changes in v2 is the output. v1 printed everything as it came; v2 has two functions:

- `show()`: something happened in the chat. Gets a `[hh:mm]` stamp on a terminal.
- `note()`: the client talking to you (help, usage hints). No stamp.

## Building parley: client v2

The client gets a `poll()` loop and readable output; the server log gets timestamps.

| File | Change |
|---|---|
| `src/log.h`, `src/log.c` | `log_timestamps()`: optional date and time in front of each log line |
| `src/parleyd.c` | stamps on by default; `-T` turns them off |
| `src/parley.c` | v2, rewritten after `parse_args()`: `poll()` loop, rendering, PING answers, exit codes |

### Timestamps in the server log

Without times, a log can't answer "when did bob leave?". Give the log module a switch. In `src/log.h`,
update the header comment and add the declaration after `log_init()`:

```c
/* log.h - log lines to stderr: "[time] prog: level: message". */
```

```c
/* Start each line with the local date and time (off by default). */
void log_timestamps(bool on);
```

In `src/log.c`, include `<time.h>` and add a flag and its setter:

```c
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static const char *log_prog = "parley";
static bool log_verbose = false;
static bool log_stamp = false;

void log_init(const char *prog, bool verbose)
{
    log_prog = prog;
    log_verbose = verbose;
}

void log_timestamps(bool on)
{
    log_stamp = on;
}
```

In `log_va()`, write the stamp first and the rest after it:

```c
static void log_va(enum log_level level, const char *fmt, va_list ap)
{
    char line[1024];
    size_t max = sizeof line - 1;      /* keep room for the '\n' */
    size_t len = 0;
    if (log_stamp) {
        /* localtime_r (POSIX) fills our struct tm; plain localtime()
         * returns a pointer to a static one, shared by all callers. */
        time_t now = time(NULL);
        struct tm tm;
        if (localtime_r(&now, &tm) != NULL)
            len = strftime(line, max, "%Y-%m-%d %H:%M:%S ", &tm);
    }
    int n = snprintf(line + len, max - len, "%s: %s: ", log_prog, level_name(level));
    if (n < 0)
        return;
    len += (size_t)n;
    if (len < max) {
        n = vsnprintf(line + len, max - len, fmt, ap);
        if (n < 0)
            return;
        len += (size_t)n;
    }
    if (len >= max)
        len = max - 1;                 /* it was cut short to fit */
    line[len] = '\n';
    line[len + 1] = '\0';
    fputs(line, stderr);
}
```

If `strftime()` fails, it returns 0, `len` stays 0, and the line has no stamp.

In `src/parleyd.c`, add a field to `struct config`:

```c
struct config {
    const char *addr;
    long port;
    long max_clients;
    long idle;
    bool verbose;
    bool timestamps;
};
```

Document the option in `print_help()`:

```c
static void print_help(void)
{
    printf("usage: parleyd [-p PORT] [-b ADDR] [-m MAXCLIENTS] [-t IDLE] [-v] [-T]\n"
           "  -p PORT        TCP port to listen on, 0: any free port (default %d)\n"
           "  -b ADDR        address to listen on (default 127.0.0.1)\n"
           "  -m MAXCLIENTS  most clients at once, 1-%d (default %d)\n"
           "  -t IDLE        PING clients idle for IDLE seconds, drop them after\n"
           "                 twice that; 0: never (default %d)\n"
           "  -v             verbose (debug) logging\n"
           "  -T             no timestamps in the log\n"
           "  -h, --help     show this help and exit\n"
           "  --version      show the version and exit\n",
           DEFAULT_PORT, MAX_CLIENTS_MAX, DEFAULT_MAX_CLIENTS, DEFAULT_IDLE);
}
```

In `parse_args()`, add a case before `default:` (`-T` takes no value, so the "needs a value" list stays):

```c
        case 'v':
            cfg->verbose = true;
            break;
        case 'T':
            cfg->timestamps = false;
            break;
        default:
            usage_error("unknown option '%s'", arg);
```

In `main()`, turn stamps on by default and pass the setting on:

```c
int main(int argc, char *argv[])
{
    struct config cfg = {
        .addr = "127.0.0.1",
        .port = DEFAULT_PORT,
        .max_clients = DEFAULT_MAX_CLIENTS,
        .idle = DEFAULT_IDLE,
        .verbose = false,
        .timestamps = true,
    };
    parse_args(argc, argv, &cfg);
    log_init("parleyd", cfg.verbose);
    log_timestamps(cfg.timestamps);
    log_msg(LOG_DEBUG, "verbose logging is on");
```

Why `-T`? Tests and the checkpoints below compare the log with fixed text; times would break that.

### parley.c v2: the plan

The loop watches three fds:

| `fds[]` | fd | Ready means |
|---|---|---|
| 0 | the server socket | lines from the server, or it closed |
| 1 | stdin, or -1 once stdin ended or we sent `QUIT` | you typed something |
| 2 | the self-pipe from `sig_wake_fd()` ([Chapter 18](18-event-loop.md)) | Ctrl-C or SIGTERM |

As in the server, the self-pipe makes sure a signal that arrives just before `poll()` still wakes it.

Each server line goes through `msg_parse()` and is shown the way a human wants to read it:

| The server sends | parley shows |
|---|---|
| `:alice MSG :hi` | `<alice> hi` |
| `:alice ME :waves` | `* alice waves` |
| `:bob JOIN` | `--> bob joined` |
| `:bob QUIT :Quit` | `<-- bob left (Quit)` |
| `:alice NICK bob` | `-- You are now known as bob` (if you are alice), else `-- alice is now known as bob` |
| `:parley 001 alice :Welcome to parley, alice` | `-- Welcome to parley, alice` |
| `:parley 353 alice :alice bob` | `-- Users: alice bob` |
| `:parley 433 * Alice :Nickname is already in use` | `!! Alice: Nickname is already in use` |
| `PING :parley` | nothing; parley answers `PONG :parley` |
| `PONG ...`, `366 ...` | nothing |
| `ERROR :Server shutting down` | nothing yet; it becomes `-- Disconnected: Server shutting down` |
| anything else | `?? ` and the raw line |

Answering `PING` matters: since Chapter 18 the server drops clients that stay silent too long.

### parley.c v2: the code

Start from Chapter 18's `src/parley.c`. Replace the header comment and includes (new: `<poll.h>`,
`<time.h>`):

```c
/* parley.c - the parley chat client.
 * Version 2: poll() waits for the keyboard (stdin) and the server at the
 * same time, so messages from others show up as soon as they arrive. */
#include <errno.h>
#include <poll.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>

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

Keep the `#define`s, `struct config`, `usage_error()`, `print_help()` and `parse_args()`. **Delete
everything after `parse_args()`** (the `quitting` flag, the connection variables, `send_msg()`,
`SYNC_TOKEN`, `show_replies()`, `sync_with_server()`, the commands and `main()`) and add the code below
in its place, in this order.

First the client's state and the two output functions. `vprint_line()` takes a `va_list`
([Chapter 4](04-functions.md)), so `show()` and `note()` share it:

```c
/* The connection to the server. */
static int server_fd = -1;
static struct strbuf server_in;     /* received, not yet handled */
static bool server_skipping;        /* skipping a too-long line */
static char server_error[128];      /* the reason in the server's ERROR */

/* What the user typed, not yet handled. We read stdin with read(), not
 * with stdio: stdio might read ahead into its own buffer, and poll()
 * knows nothing about that buffer. */
static struct strbuf kbd;
static bool kbd_skipping;

static bool quitting = false;       /* we sent QUIT */
static bool show_time = false;      /* timestamps in front of chat lines */
static char my_nick[32] = "";       /* our nick, once the server has said so */

/* Print one line for the user, with the time in front if stamp is true
 * and show_time is on. */
static void vprint_line(bool stamp, const char *fmt, va_list ap)
{
    if (stamp && show_time) {
        char hhmm[16];
        time_t now = time(NULL);
        struct tm tm;
        if (localtime_r(&now, &tm) != NULL && strftime(hhmm, sizeof hhmm, "%H:%M", &tm) > 0)
            printf("[%s] ", hhmm);
    }
    vprintf(fmt, ap);
    putchar('\n');
}

/* Something that happened in the chat: gets a timestamp. */
static void show(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vprint_line(true, fmt, ap);
    va_end(ap);
}

/* A message from the client itself (help, usage hints): no timestamp. */
static void note(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vprint_line(false, fmt, ap);
    va_end(ap);
}
```

Then `send_msg()` (unchanged) and the renderer, which follows the table above:

```c
/* Send one protocol message to the server. */
static void send_msg(const struct msg *m)
{
    char line[MSG_LINE_MAX + 1];
    int n = msg_format(line, sizeof line, m);
    if (n > 0 && write_all(server_fd, line, (size_t)n) < 0)
        die("sending to server: %s", strerror(errno));
}

/* The last parameter: the text of most messages. */
static const char *last_param(const struct msg *m)
{
    if (m->trailing != NULL)
        return m->trailing;
    return m->nparams > 0 ? m->params[m->nparams - 1] : "";
}

/* Show one line from the server the way a human wants to read it. */
static void handle_server_line(char *line)
{
    char raw[MSG_LINE_MAX + 1];
    str_copy(raw, line, sizeof raw);      /* msg_parse cuts line up */
    struct msg m;
    if (msg_parse(line, &m) != MSG_OK)
        return;
    const char *from = m.prefix != NULL ? m.prefix : "?";
    const char *cmd = m.command;
    const char *text = last_param(&m);
    const char *p0 = msg_param(&m, 0);

    if (strcmp(cmd, "MSG") == 0) {
        show("<%s> %s", from, text);
    } else if (strcmp(cmd, "ME") == 0) {
        show("* %s %s", from, text);
    } else if (strcmp(cmd, "JOIN") == 0) {
        show("--> %s joined", from);
    } else if (strcmp(cmd, "QUIT") == 0) {
        show("<-- %s left (%s)", from, text);
    } else if (strcmp(cmd, "NICK") == 0 && p0 != NULL) {
        if (strcmp(from, my_nick) == 0) {
            str_copy(my_nick, p0, sizeof my_nick);
            show("-- You are now known as %s", p0);
        } else {
            show("-- %s is now known as %s", from, p0);
        }
    } else if (strcmp(cmd, "PING") == 0) {
        send_msg(&(struct msg){ .command = "PONG", .trailing = text });
    } else if (strcmp(cmd, "PONG") == 0 || strcmp(cmd, "366") == 0) {
        /* nothing to show */
    } else if (strcmp(cmd, "ERROR") == 0) {
        str_copy(server_error, text, sizeof server_error);
    } else if (strcmp(cmd, "001") == 0 && p0 != NULL) {
        str_copy(my_nick, p0, sizeof my_nick);
        show("-- %s", text);
    } else if (strcmp(cmd, "353") == 0) {
        show("-- Users: %s", text);
    } else if (cmd[0] == '4') {
        /* An error reply: ":parley 433 * bob :Nickname is already in use" */
        if (m.nparams >= 2)
            show("!! %s: %s", m.params[1], text);
        else
            show("!! %s", text);
    } else {
        show("?? %s", raw);
    }
}
```

`handle_server_line()` copies the line first: `msg_parse()` cuts its input apart with `'\0'`s, and the
`??` case wants the original.

The commands are as before, but talk through `note()` instead of `printf()` and `fprintf(stderr, ...)`:

```c
static void cmd_nick(const char *arg)
{
    if (*arg == '\0') {
        note("-- usage: /nick NEWNICK");
        return;
    }
    send_msg(&(struct msg){ .command = "NICK", .nparams = 1, .params = { arg } });
}

static void cmd_me(const char *arg)
{
    if (*arg == '\0') {
        note("-- usage: /me ACTION");
        return;
    }
    send_msg(&(struct msg){ .command = "ME", .trailing = arg });
}

static void cmd_who(const char *arg)
{
    (void)arg;   /* unused: every handler must have the same type */
    send_msg(&(struct msg){ .command = "NAMES" });
}

static void cmd_quit(const char *arg)
{
    /* "QUIT" alone, or "QUIT :reason" */
    send_msg(&(struct msg){ .command = "QUIT", .trailing = *arg != '\0' ? arg : NULL });
    quitting = true;
}

static void cmd_help(const char *arg);

/* The dispatch table: one struct per slash command. The table also holds
 * the help text, so /help can never get out of date. */
typedef void command_fn(const char *arg);

struct command {
    const char *name;
    const char *args;      /* argument summary for /help */
    const char *help;
    command_fn *run;
};

static const struct command commands[] = {
    { "nick", "NEWNICK",  "change your nickname",    cmd_nick },
    { "me",   "ACTION",   "describe an action",      cmd_me   },
    { "who",  "",         "list who is in the room", cmd_who  },
    { "quit", "[REASON]", "leave",                   cmd_quit },
    { "help", "",         "show this help",          cmd_help },
};
enum { NCOMMANDS = sizeof commands / sizeof commands[0] };

static void cmd_help(const char *arg)
{
    (void)arg;
    note("-- commands:");
    for (int i = 0; i < NCOMMANDS; i++) {
        char usage[32];
        snprintf(usage, sizeof usage, "/%s %s", commands[i].name, commands[i].args);
        note("--   %-15s %s", usage, commands[i].help);
    }
    note("-- anything else is sent as a message");
}

/* "/nick bob" -> run cmd_nick("bob"). Modifies line in place. */
static void run_command(char *line)
{
    char *name = line + 1;   /* skip the '/' */
    char *arg = name;
    while (*arg != '\0' && *arg != ' ')
        arg++;
    if (*arg == ' ')
        *arg++ = '\0';      /* end the name; arg now points after the space */

    for (int i = 0; i < NCOMMANDS; i++) {
        if (strcmp(name, commands[i].name) == 0) {
            commands[i].run(arg);     /* call through the function pointer */
            return;
        }
    }
    note("-- unknown command /%s (try /help)", name);
}

```

Now input. `handle_input_line()` takes one typed line; `leave()` says goodbye. `read_keyboard()` and
`read_server()` each do one `read()` and handle every complete line it brought:

```c
static void handle_input_line(char *line)
{
    if (strlen(line) > INPUT_MAX) {
        note("-- line too long (max %d bytes), not sent", INPUT_MAX);
        return;
    }
    if (line[0] == '\0')
        return;                    /* ignore empty lines */
    if (line[0] == '/')
        run_command(line);
    else
        send_msg(&(struct msg){ .command = "MSG", .trailing = line });
}

/* Leave: send QUIT (unless /quit did), then tell the server that we will
 * send nothing more. We keep reading until it closes the connection. */
static void leave(void)
{
    if (!quitting)
        send_msg(&(struct msg){ .command = "QUIT" });
    quitting = true;
    shutdown(server_fd, SHUT_WR);
}

/* Read what is waiting on stdin. Returns false at end of input. */
static bool read_keyboard(void)
{
    char line[MSG_LINE_MAX + 1];
    ssize_t n = read_into(STDIN_FILENO, &kbd, 4096);
    if (n < 0) {
        if (errno == EINTR || errno == EAGAIN)
            return true;
        die("reading input: %s", strerror(errno));
    }
    if (n == 0 && kbd.len > 0)
        sb_append(&kbd, "\n", 1);     /* a last line without a newline */

    enum frame_status st;
    while (!quitting && (st = msg_take_line(&kbd, &kbd_skipping, line)) != FRAME_NONE) {
        if (st == FRAME_TOO_LONG)
            note("-- line too long (max %d bytes), not sent", INPUT_MAX);
        else
            handle_input_line(line);
    }
    return n > 0;
}

/* Read what the server sent. Returns false when it closed the connection. */
static bool read_server(void)
{
    char line[MSG_LINE_MAX + 1];
    ssize_t n = read_into(server_fd, &server_in, 4096);
    if (n < 0) {
        if (errno == EINTR || errno == EAGAIN)
            return true;
        str_copy(server_error, strerror(errno), sizeof server_error);
        return false;
    }
    enum frame_status st;
    while ((st = msg_take_line(&server_in, &server_skipping, line)) != FRAME_NONE)
        if (st == FRAME_LINE)
            handle_server_line(line);
    return n > 0;
}
```

- `read_keyboard()` stops handling lines once `quitting` is set: nothing after `/quit` is sent.
- At end of input, a last line without `'\n'` still counts: it gets one appended.
- `read_server()` returns false at end of file *or* on an error such as "Connection reset by peer"; the
  error text becomes the "Disconnected" reason.

Finally `main()`:

```c
int main(int argc, char *argv[])
{
    struct config cfg = { .host = "localhost", .port = DEFAULT_PORT, .nick = "guest" };
    parse_args(argc, argv, &cfg);
    log_init("parley", false);
    if (sig_init() < 0)
        die("setting up signals: %s", strerror(errno));
    int wake_fd = sig_wake_fd();
    if (wake_fd < 0)
        die("setting up signals: %s", strerror(errno));
    /* Timestamps help a human reading a terminal, and get in the way when
     * the output goes to a file or another program. */
    show_time = isatty(STDOUT_FILENO);

    char err[256];
    server_fd = net_connect(cfg.host, (int)cfg.port, err, sizeof err);
    if (server_fd < 0)
        die("cannot connect to %s", err);
    send_msg(&(struct msg){ .command = "NICK", .nparams = 1, .params = { cfg.nick } });

    bool kbd_open = true;
    for (;;) {
        /* stdout is a pipe or file in scripts: then stdio buffers it, so
         * flush before we sleep in poll(). */
        fflush(stdout);
        struct pollfd fds[3] = {
            { .fd = server_fd, .events = POLLIN },
            { .fd = kbd_open && !quitting ? STDIN_FILENO : -1, .events = POLLIN },
            { .fd = wake_fd, .events = POLLIN },
        };
        if (poll(fds, 3, -1) < 0) {
            if (errno == EINTR)
                continue;
            die("poll: %s", strerror(errno));
        }
        if (fds[2].revents & POLLIN) {      /* Ctrl-C or SIGTERM */
            sig_drain(wake_fd);
            if (quitting)
                die("interrupted");       /* the second time: give up */
            leave();
        }
        if (fds[1].revents & (POLLIN | POLLHUP)) {
            if (!read_keyboard()) {
                kbd_open = false;
                leave();                  /* end of input */
            }
        }
        if (fds[0].revents & (POLLIN | POLLHUP | POLLERR)) {
            if (!read_server())
                break;
        }
    }

    if (!quitting) {                      /* the server hung up on us */
        show("-- Disconnected: %s",
             server_error[0] != '\0' ? server_error : "Connection closed");
    }
    close(server_fd);
    close(wake_fd);
    sb_free(&server_in);
    sb_free(&kbd);
    if (fflush(stdout) == EOF)
        die("writing: %s", strerror(errno));
    return quitting ? 0 : 1;
}
```

- **Loop order:** signal, keyboard, server. A server close ends the loop at once.
- **The second Ctrl-C.** The first sends `QUIT` and waits for the server to close. If the server hangs, a
  second Ctrl-C gives up with `die("interrupted")`.
- **`fflush(stdout)` before `poll()`.** Into a pipe, stdout is fully buffered; without the flush, lines
  would sit in the buffer while `poll()` sleeps.
- **The final `fflush()` is checked.** If stdout is a full disk, the user should hear about it
  ([Chapter 12](12-files-and-descriptors.md)).

### Checkpoint

Build, test, then try a scripted session. The server runs with `-T` so its log can be compared line by
line; `sleep 0.2` gives it time to listen.

```console
$ make -s test-all
unit: 29 passed, 0 failed
unit: 29 passed, 0 failed
$ make -s SANITIZE=1 test
unit: 29 passed, 0 failed
$ make -s LIBC=musl
$ build/glibc/parleyd -p 6619 -T 2>server.log & server=$!; sleep 0.2
$ printf 'hello\n/me waves\n/who\n/nick bob\n/nick\n/dance\n' | build/glibc/parley -p 6619 -n alice; echo "exit $?"
-- usage: /nick NEWNICK
-- unknown command /dance (try /help)
-- Welcome to parley, alice
--> alice joined
-- Users: alice
<alice> hello
* alice waves
-- Users: alice
-- You are now known as bob
exit 0
```

The two notes come *before* the welcome, correctly: all six lines were in the pipe when `poll()` first
returned, so `parley` sent four to the server and printed the two notes at once; the answers came a
moment later. Humans type slower than the network, so in a real chat the order looks natural.

`/quit` with a reason; the line after it is never sent:

```console
$ printf '/quit see you\nnot sent\n' | build/musl/parley -p 6619 -n carol; echo "exit $?"
-- Welcome to parley, carol
--> carol joined
-- Users: carol
exit 0
```

Now two users. alice reads from a fifo (a named pipe, made with `mkfifo`), so she stays connected until
we close it; `exec 3>a` opens it for writing as fd 3 and keeps it open:

```console
$ mkfifo a; build/glibc/parley -p 6619 -n alice <a >alice.txt & alice=$!; exec 3>a; sleep 0.2
$ printf 'hi alice\n' | build/musl/parley -p 6619 -n Alice; echo "exit $?"
!! Alice: Nickname is already in use
!! You have not registered
exit 0
$ printf 'hi alice\n' | build/musl/parley -p 6619 -n bob >/dev/null; sleep 0.2; cat alice.txt
-- Welcome to parley, alice
--> alice joined
-- Users: alice
--> bob joined
<bob> hi alice
<-- bob left (Quit)
```

Nicknames are case-insensitive, so "Alice" is taken. Ctrl-C (`SIGINT`) makes the client say goodbye
properly:

```console
$ kill -INT $alice; wait $alice; echo "exit $?"; exec 3>&-
exit 0
```

When the server goes away, the client notices at once, though its stdin (the fifo) is still open:

```console
$ build/glibc/parley -p 6619 -n dave <a >dave.txt & dave=$!; exec 3>a; sleep 0.2
$ kill -TERM $server; wait $server; wait $dave; echo "exit $?"; exec 3>&-; cat dave.txt; cat server.log
exit 1
-- Welcome to parley, dave
--> dave joined
-- Users: dave
-- Disconnected: Server shutting down
parleyd: info: listening on 127.0.0.1:6619
parleyd: info: alice joined
parleyd: info: alice is now bob
parleyd: info: bob left (Quit)
parleyd: info: carol joined
parleyd: info: carol left (see you)
parleyd: info: alice joined
parleyd: info: bob joined
parleyd: info: bob left (Quit)
parleyd: info: alice left (Quit)
parleyd: info: dave joined
parleyd: info: got signal 15 (Terminated), shutting down
```

An idle client answers the server's PINGs itself. With `-t 1` the server PINGs after 1 s of silence and
drops a client after 2 s; erin is silent for 2.5 s and survives:

```console
$ build/musl/parleyd -p 6619 -T -t 1 2>server.log & server=$!; sleep 0.2
$ build/musl/parley -p 6619 -n erin <a >erin.txt & erin=$!; exec 3>a; sleep 2.5
$ printf 'still here\n' >&3; exec 3>&-; wait $erin; echo "exit $?"; cat erin.txt
exit 0
-- Welcome to parley, erin
--> erin joined
-- Users: erin
<erin> still here
$ kill -TERM $server; wait $server; cat server.log
parleyd: info: listening on 127.0.0.1:6619
parleyd: info: erin joined
parleyd: info: erin left (Quit)
parleyd: info: got signal 15 (Terminated), shutting down
```

Timestamps appear in the server log unless `-T`, and in the client only on a terminal. `script` runs a
command on a pseudo-terminal (`tr -d '\r'` removes its carriage returns). Your times will differ:

```console
$ TZ=UTC build/glibc/parleyd -p 6619 2>server.log & server=$!; sleep 0.2
$ script -qc "printf 'hi\\n' | build/musl/parley -p 6619 -n frank" /dev/null | tr -d '\r'
[15:43] -- Welcome to parley, frank
[15:43] --> frank joined
[15:43] -- Users: frank
[15:43] <frank> hi
$ kill -TERM $server; wait $server; cat server.log
2026-10-30 19:43:18 parleyd: info: listening on 127.0.0.1:6619
2026-10-30 19:43:18 parleyd: info: frank joined
2026-10-30 19:43:18 parleyd: info: frank left (Quit)
2026-10-30 19:43:18 parleyd: info: got signal 15 (Terminated), shutting down
$ TZ=Asia/Tokyo build/musl/parleyd -p 6619 2>&1 & server=$!; sleep 0.2; kill -TERM $server; wait $server
2026-10-31 04:43:18 parleyd: info: listening on 127.0.0.1:6619
2026-10-31 04:43:18 parleyd: info: got signal 15 (Terminated), shutting down
```

The server ran with `TZ=UTC` and the client in the machine's zone (UTC−4): 19:43 and 15:43 are the same
moment. The static musl server honours `TZ=Asia/Tokyo` too.

The failure exits:

```console
$ build/glibc/parley -p 6619; echo "exit $?"
parley: error: cannot connect to 127.0.0.1:6619: Connection refused
exit 1
$ build/glibc/parley -p 99999; echo "exit $?"
parley: invalid port '99999' (expected 1-65535)
Try 'parley --help' for more information.
exit 2
$ rm -f a *.txt server.log && make clean >/dev/null && ls
Makefile
src
tests
```

Try it for real: `build/glibc/parleyd` in one terminal, `build/glibc/parley -n alice` and
`build/glibc/parley -n bob` in two others. Messages appear at once. One flaw remains: bob's message lands
in the middle of the line you're typing, because the terminal echoes your keys. Chapter 20 takes control.

The complete project at this point is in `project/ch19/`.

## Summary

- `poll()` watches any fds: sockets, pipes, terminals. One loop waits for the keyboard, the server and
  signals (via the self-pipe).
- Never `poll()` an fd you read through stdio, whose read-ahead buffer `poll()` can't see. Use `read()`
  and your own buffer; `fflush()` output before you block.
- A negative fd in a `pollfd` is skipped.
- End of stdin: send `QUIT`, `shutdown(SHUT_WR)`, read until the server closes. Server closed: stop now.
- `isatty()` tells a terminal from a pipe or file. Decorate output for humans only.
- `time()` → `localtime_r()`/`gmtime_r()` → `strftime()`. Avoid `localtime()`/`gmtime()` (one shared
  static result). `tm_year` is +1900, `tm_mon` 0–11. `TZ` picks the zone.
- glibc and musl agree for valid zones. For UTC and bad `TZ`, musl says "UTC"; glibc says "GMT" or the
  bogus name.
- Exit codes: 0 success, 1 failure, 2 usage, so scripts can rely on them.

## Exercises

1. **`/time`.** Add a `/time` command that prints the local date and time, like
   `-- Friday 30 October 2026, 16:08:01 EDT`. It's a local note; the server isn't involved.

   <details><summary>Solution</summary>

   Add the function before `cmd_help`'s forward declaration, and a row to the table:

   ```c
   static void cmd_time(const char *arg)
   {
       (void)arg;
       char buf[64];
       time_t now = time(NULL);
       struct tm tm;
       if (localtime_r(&now, &tm) == NULL
               || strftime(buf, sizeof buf, "%A %d %B %Y, %H:%M:%S %Z", &tm) == 0) {
           note("-- cannot tell the time");
           return;
       }
       note("-- %s", buf);
   }
   ```

   ```c
       { "time", "",         "show the local time",     cmd_time },
   ```

   `/help` lists it with no other change: that's the point of keeping help texts in the table. Your date
   will differ:

   ```console
   $ printf '/time\n' | build/glibc/parley -p 6619 -n alice | grep 2026
   -- Friday 30 October 2026, 16:08:01 EDT
   ```

   </details>

2. **`/ping`.** Add a `/ping` command that measures the round trip to the server. Send `PING` with the
   current `clock_ms()` ([Chapter 18](18-event-loop.md)) as the token; the server echoes it in its `PONG`.
   Print `-- pong after N ms`.

   <details><summary>Solution</summary>

   `clock.o` is server-only so far. Add it to the client in the `Makefile`:

   ```make
   CLIENT_OBJS := $(addprefix $(BUILD)/, parley.o clock.o io.o net.o sig.o msg.o strbuf.o str.o log.o)
   ```

   In `parley.c`, `#include "clock.h"` and add the command and its table row:

   ```c
   static void cmd_ping(const char *arg)
   {
       (void)arg;
       char token[32];
       snprintf(token, sizeof token, "%lld", clock_ms());
       send_msg(&(struct msg){ .command = "PING", .trailing = token });
   }
   ```

   ```c
       { "ping", "",         "measure the round trip",  cmd_ping },
   ```

   In `handle_server_line()`, split the `PONG`/`366` branch in two:

   ```c
       } else if (strcmp(cmd, "PONG") == 0) {
           char *end;
           long long sent = strtoll(text, &end, 10);
           if (end != text && *end == '\0')      /* our /ping token */
               note("-- pong after %lld ms", clock_ms() - sent);
       } else if (strcmp(cmd, "366") == 0) {
           /* nothing to show */
   ```

   On localhost the answer is fast (you may see 0 or 1):

   ```console
   $ printf '/ping\n' | build/glibc/parley -p 6619 -n alice | grep pong
   -- pong after 0 ms
   ```

   `clock_ms()` uses `CLOCK_MONOTONIC`, not `time()`: if someone adjusts the system clock between `PING`
   and `PONG`, the time of day jumps but the monotonic clock doesn't.

   </details>

3. **Mentions.** When someone else's message contains your nick, put `** ` in front of it:
   `** <bob> hi alice`.

   <details><summary>Solution</summary>

   In `handle_server_line()`:

   ```c
       if (strcmp(cmd, "MSG") == 0) {
           bool mention = my_nick[0] != '\0' && strcmp(from, my_nick) != 0
                          && strstr(text, my_nick) != NULL;
           show("%s<%s> %s", mention ? "** " : "", from, text);
   ```

   With alice in the room (the fifo setup from the checkpoint):

   ```console
   $ printf 'hi alice\nhi bob\n' | build/glibc/parley -p 6619 -n bob >/dev/null; sleep 0.2; cat alice.txt
   -- Welcome to parley, alice
   --> alice joined
   -- Users: alice
   --> bob joined
   ** <bob> hi alice
   <bob> hi bob
   <-- bob left (Quit)
   ```

   `strstr()` is case-sensitive and also matches "malice". Nicknames are case-insensitive, so a better
   version compares case-insensitively and checks that the characters around the match aren't letters.

   </details>

4. **A timeout for pollcat.** Give `pollcat` an optional third argument, SECONDS. If neither stdin nor the
   server says anything for that long, print `pollcat: nothing for N s, giving up` and exit with status 1.

   <details><summary>Solution</summary>

   Include `<stdlib.h>` for `atoi()`. In `main()`:

   ```c
       if (argc != 3 && argc != 4) {
           fprintf(stderr, "usage: pollcat HOST PORT [SECONDS]\n");
           return 2;
       }
       int timeout = argc == 4 ? atoi(argv[3]) * 1000 : -1;   /* -1: forever */
   ```

   And in the loop:

   ```c
           int ready = poll(fds, 2, timeout);
           if (ready < 0) {
               if (errno == EINTR)
                   continue;
               perror("poll");
               return 1;
           }
           if (ready == 0) {
               fprintf(stderr, "pollcat: nothing for %s s, giving up\n", argv[3]);
               close(sock);
               return 1;
           }
   ```

   Test it with a server that says one thing, then nothing (terminal 1), and a stdin that stays open but
   silent (terminal 2):

   ```console
   $ { echo 'hello'; sleep 5; } | nc -l 127.0.0.1 6667
   ```

   ```console
   $ sleep 4 | ./pollcat 127.0.0.1 6667 2; echo "exit $?"
   hello
   pollcat: nothing for 2 s, giving up
   exit 1
   ```

   The timeout restarts after every event: it means "2 s of silence", not "2 s in total". `atoi()`
   accepts garbage silently; `strtol()` with checks ([Chapter 2](02-types-and-operators.md)) is the
   careful choice.

   </details>

5. **Order of output.** In the first checkpoint run, the local notes appear before the server's welcome.
   How could the output follow the order of the input lines? What would that cost?

   <details><summary>Solution</summary>

   Wait for the server after each line, like v1: send a `PING` with a token after each line, and stop
   reading stdin (set its fd to -1) until the matching `PONG` arrives. Each local note then prints after
   the replies to the previous lines.

   The cost: a round trip per line before the next one is read. Invisible on localhost, slow over a bad
   network. Interactive users don't need it, because humans type slower than the network answers. Only
   scripts care, and scripts can wait themselves (`sleep`).

   </details>

Sources for this chapter: [REFERENCES.md](REFERENCES.md#chapter-19--an-interactive-client)
