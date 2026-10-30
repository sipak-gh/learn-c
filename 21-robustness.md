# Chapter 21 — Robustness

A server talks to strangers: some send garbage, some never read, some never stop writing, some vanish. This
chapter makes `parleyd` survive them all (flood control, running out of file descriptors, closing so the last
message arrives) and proves it with a fuzzer and an integration test that drives the real server over TCP.
[Chapter 20](20-terminal-ui.md) finished the client; [Chapter 22](22-shipping-and-libc-choices.md) ships
version 1.0.

## Every byte is hostile

**A client can send anything** — any length, bytes, speed or order — and the server must keep serving
everyone else. Four ideals follow:

- **Bound everything** (buffers, queues, counts, waits), or one client takes all your memory, descriptors
  or time.
- **Fail one client, not the server.** It gets an error or a disconnect; the server never crashes or blocks.
- **Check at the edge, then trust.** After the framer and parser, a line is short, NUL-free and has a command.
- **Test every limit.** An untested limit may not work.

How `parleyd` answers a hostile client by the end of this chapter:

| The client… | `parleyd` answers | Since |
|---|---|---|
| sends a line over 512 bytes | `417 Input line too long`; next line works | [Chapter 8](08-memory.md) |
| sends a bad nick (`9lives`, 17 letters) | `432 Erroneous nickname` | [Chapter 6](06-strings.md) |
| sends an unknown command or a malformed line | `421 Unknown command`, `400 Malformed message` | [Chapter 7](07-structs-unions-enums.md) |
| connects and never registers | `ERROR :Registration timeout` | [Chapter 18](18-event-loop.md) |
| stops answering (or vanishes) | `PING`, then `ERROR :Ping timeout` | [Chapter 18](18-event-loop.md) |
| never reads what it is sent | dropped when 64 KiB are queued | [Chapter 18](18-event-loop.md) |
| connects when the room is full | `ERROR :Server full` | [Chapter 18](18-event-loop.md) |
| sends a NUL byte in a line | `400 Line contains a NUL byte` | this chapter |
| sends faster than a human types | `ERROR :Excess flood`, disconnected | this chapter |
| connects when descriptors run out | `ERROR :Server full`, no busy loop | this chapter |
| keeps sending after its disconnect | still gets its `ERROR` line | this chapter |

## Defensive parsing

Parse untrusted input in this order:

1. **Length.** Find the line end with `memchr()` within the bytes you have; reject an over-long line before
   copying it.
2. **Bytes.** Reject bytes the rest of the program can't handle.
3. **Structure.** Only now split into prefix, command and parameters.

- **Reject, don't repair.** Throw a bad line away and say so. "Fixing" it means guessing, and an attacker
  chooses your guess.
- **One parser.** Server, client and tests share `msg.c`. Two parsers always disagree somewhere, and attackers
  live in the gap.

### NUL bytes

C strings end at the first `'\0'`; network data is a count of bytes that may include zeros. So one line can
look different to different parts of a program:

`examples/ch21/nul.c`
```c
/* nul.c - a NUL byte makes one line look like two different lines. */
#include <stdio.h>
#include <string.h>

int main(void)
{
    static const char line[] = "MSG :hi\0QUIT";   /* 12 bytes from the network */
    size_t len = sizeof line - 1;                 /* what read() said */

    printf("read() length: %zu\n", len);
    printf("strlen():      %zu\n", strlen(line));
    printf("printf(\"%%s\"):  %s\n", line);
    printf("bytes:         ");
    fwrite(line, 1, len, stdout);                 /* writes the NUL too */
    printf("\n");
    return 0;
}
```

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o nul nul.c && ./nul | cat -v
read() length: 12
strlen():      7
printf("%s"):  MSG :hi
bytes:         MSG :hi^@QUIT
```

(`cat -v` shows the NUL byte as `^@`. The musl build prints the same.)

Length-based code sees 12 bytes, `strlen()` sees 7; a check using one view and an action using the other is
useless. In 2009 some browsers accepted a TLS certificate for `www.paypal.com\0.thoughtcrime.org` as one for
`www.paypal.com`. The fix is to never let such a line in: from this chapter on, the framer rejects them.

## Flood control

A script can send thousands of lines a second, and each goes to the whole room. **Flood control** limits how
fast one client may send. IRC servers use a **penalty clock** per client (RFC 1459 section 8.10: 2 seconds
per message, at most 10 seconds ahead):

- If the clock is behind real time, set it to "now".
- Each line moves it ahead by a fixed cost, say 200 ms.
- Too far ahead of real time means the client is flooding.

A slow sender keeps the clock at "now"; a burst pushes it ahead and it drains back at real-time speed. So
pasting a few lines is fine, a steady stream is not.

`examples/ch21/penalty.c`
```c
/* penalty.c - rate limiting with a penalty clock. */
#include <stdbool.h>
#include <stdio.h>

#define COST_MS 200   /* each line costs 200 ms: 5 lines/s on average */
#define BURST 3       /* the clock may run at most 3 lines ahead */

static long long penalty;   /* the client's clock, in ms */

/* May a line that arrives at time now (ms) go through? */
static bool allow(long long now)
{
    if (penalty < now)
        penalty = now;               /* never lag behind real time */
    if (penalty + COST_MS - now > BURST * COST_MS)
        return false;                /* too far ahead: refuse */
    penalty += COST_MS;
    return true;
}

int main(void)
{
    /* When lines arrive (ms): a burst of 5, a pause, then 1 per 100 ms. */
    static const long long at[] = { 0, 0, 0, 0, 0, 700, 800, 900, 1000, 1100, 1200 };
    for (size_t i = 0; i < sizeof at / sizeof at[0]; i++) {
        bool ok = allow(at[i]);
        printf("t=%4lld  clock=%4lld  ahead=%3lld  %s\n",
               at[i], penalty, penalty - at[i], ok ? "ok" : "refused");
    }
    return 0;
}
```

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o penalty penalty.c && ./penalty
t=   0  clock= 200  ahead=200  ok
t=   0  clock= 400  ahead=400  ok
t=   0  clock= 600  ahead=600  ok
t=   0  clock= 600  ahead=600  refused
t=   0  clock= 600  ahead=600  refused
t= 700  clock= 900  ahead=200  ok
t= 800  clock=1100  ahead=300  ok
t= 900  clock=1300  ahead=400  ok
t=1000  clock=1500  ahead=500  ok
t=1100  clock=1700  ahead=600  ok
t=1200  clock=1700  ahead=500  refused
```

Three lines of the burst pass. After the pause the clock has caught up; one line per 100 ms is twice the
allowed rate, so the clock creeps ahead until lines are refused.

One number per client, three lines of arithmetic. A "lines in the last second" counter needs a list of
timestamps and allows a burst at the end of one second plus another at the start of the next.

When the clock is too far ahead you can **stop reading** (RFC 1459's way: buffers fill and TCP slows the
sender), **refuse the line** (as here), or **disconnect**. `parleyd` disconnects with `ERROR :Excess flood`: a
flooder is broken or hostile, and silently dropped lines would confuse a real user.

## Running out of file descriptors

Each client costs a descriptor, and the **resource limit** `RLIMIT_NOFILE` caps how many a process may have
open. It has two values:

- the **soft limit**, which the kernel enforces;
- the **hard limit**, the ceiling. A process may lower either, or raise the soft limit up to the hard one.
  Raising the hard limit needs root.

`ulimit -n` shows and sets the soft limit for the shell and its children. In C: `getrlimit()` and
`setrlimit()` from `<sys/resource.h>`, part of the **XSI** option (X/Open System Interfaces), not base POSIX.
Ask for it with `#define _XOPEN_SOURCE 700` (the 2008 edition, including all of POSIX.1-2008).

When the table is full, calls that make a descriptor (`open`, `socket`, `accept`, `pipe`, `dup`) fail with
**EMFILE**; **ENFILE** means the whole *system* is out. This program lowers its own limit and runs out:

`examples/ch21/fdlimit.c`
```c
/* fdlimit.c - run out of file descriptors on purpose. */
#define _XOPEN_SOURCE 700            /* getrlimit/setrlimit are XSI */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/resource.h>
#include <unistd.h>

int main(void)
{
    struct rlimit rl;
    if (getrlimit(RLIMIT_NOFILE, &rl) < 0) {
        perror("getrlimit");
        return 1;
    }
    printf("limit: soft %llu, hard %llu\n",
           (unsigned long long)rl.rlim_cur, (unsigned long long)rl.rlim_max);

    rl.rlim_cur = 8;                 /* lower the soft limit: fds 0..7 */
    if (setrlimit(RLIMIT_NOFILE, &rl) < 0) {
        perror("setrlimit");
        return 1;
    }
    int spare = open("/dev/null", O_RDONLY);    /* kept in reserve */
    int fd, last = -1;
    while ((fd = open("/dev/null", O_RDONLY)) >= 0)
        last = fd;
    printf("opened up to fd %d, then: %s\n", last, strerror(errno));

    close(spare);                    /* free the reserve ... */
    fd = open("/dev/null", O_RDONLY);
    printf("after closing the spare: fd %d\n", fd);
    return 0;
}
```

`rlim_t` is unsigned of unknown size, so print it cast to `unsigned long long`. Your limits will differ; many
desktops have a soft limit of 1024. The last two lines show a trick used below: free a descriptor kept **in
reserve** when you run out, and the next `open()` gets its number (the kernel hands out the lowest free one).

### glibc vs musl: out of descriptors

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o fdlimit fdlimit.c && ./fdlimit
limit: soft 1048576, hard 1048576
opened up to fd 7, then: Too many open files
after closing the spare: fd 3
$ musl-gcc -std=c99 -Wall -Wextra -pedantic -static -o fdlimit fdlimit.c && ./fdlimit
limit: soft 1048576, hard 1048576
opened up to fd 7, then: No file descriptors available
after closing the spare: fd 3
```

The kernel's behaviour is identical; only `strerror(EMFILE)` differs: glibc's traditional "Too many open
files" (which search engines know) versus musl's more exact "No file descriptors available". **Compare `errno`
values, never message texts.** (`strerror(ENFILE)`, "Too many open files in system", is the same in both.)

### Why EMFILE is dangerous for a poll() server

After EMFILE from `accept()`, the connection stays in the listen queue. `poll()` is level-triggered, so it
reports the socket readable again, `accept()` fails again, forever: 100% CPU, nobody served.

The fix is the **spare descriptor** trick (described in the libev documentation, among others):

1. At start-up, open `/dev/null` and keep the descriptor.
2. On EMFILE or ENFILE: close the spare, `accept()` the client into the free slot, send it
   `ERROR :Server full`, close it, and reopen the spare.

The queue empties and the client learns why. Better still, at start-up lower `-m` to what the limit allows,
so the normal "Server full" check catches extra clients. The server does both.

## Closing gracefully: FIN, RST and lingering

TCP ends a connection in one of two ways:

- **FIN**, the normal end: "I will send no more". The other side reads all the data, then `read()` returns 0.
- **RST**, a reset: "this connection is broken". The other side's next `write()` or `read()` fails with
  `ECONNRESET`, and data it has not yet read may be thrown away.

`close()` normally sends a FIN, but with *unread input* in the socket the kernel sends a RST to say that data
was never delivered (RFC 2525, section 2.17) — exactly the case of a server dropping a flooding client.

**`shutdown(fd, SHUT_WR)`** sends a FIN but keeps reading open (a **half-close**). The server discards input
until the client closes (`read()` returns 0), then calls `close()`: nothing unread, no RST. This is
**lingering**.

`examples/ch21/rst.c`
```c
/* rst.c - close() with unread input sends a reset; shutdown() does not.
 *     rst close | rst linger */
#define _POSIX_C_SOURCE 200809L
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define PORT 6910

static void pause_ms(long ms)
{
    struct timespec ts = { 0, ms * 1000000L };
    nanosleep(&ts, NULL);
}

static void say(int fd, const char *text)
{
    if (write(fd, text, strlen(text)) < 0)
        printf("client: write: %s\n", strerror(errno));
}

int main(int argc, char *argv[])
{
    int linger = argc > 1 && strcmp(argv[1], "linger") == 0;
    signal(SIGPIPE, SIG_IGN);

    struct sockaddr_in a = { .sin_family = AF_INET, .sin_port = htons(PORT) };
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    int lis = socket(AF_INET, SOCK_STREAM, 0), on = 1;
    setsockopt(lis, SOL_SOCKET, SO_REUSEADDR, &on, sizeof on);
    if (bind(lis, (struct sockaddr *)&a, sizeof a) < 0 || listen(lis, 1) < 0) {
        perror("bind/listen");
        return 1;
    }
    int client = socket(AF_INET, SOCK_STREAM, 0);
    if (connect(client, (struct sockaddr *)&a, sizeof a) < 0) {
        perror("connect");
        return 1;
    }
    int server = accept(lis, NULL, NULL);

    say(client, "MSG :spam\r\nMSG :spam\r\n");     /* the server never reads it */
    say(server, "ERROR :Excess flood\r\n");
    if (linger)
        shutdown(server, SHUT_WR);     /* send FIN; keep reading */
    else
        close(server);                 /* unread input: send RST */
    pause_ms(100);

    say(client, "MSG :more spam\r\n");             /* the client goes on */
    char buf[256];
    for (;;) {
        ssize_t n = read(client, buf, sizeof buf - 1);
        if (n < 0) {
            printf("client: read: %s\n", strerror(errno));
            break;
        }
        if (n == 0) {
            printf("client: end of connection\n");
            break;
        }
        buf[n] = '\0';
        printf("client: got %s", buf);
    }
    close(client);
    if (linger) {                      /* the server drains, then closes */
        ssize_t n;
        while ((n = read(server, buf, sizeof buf)) > 0)
            printf("server: threw away %zd bytes\n", n);
        close(server);
    }
    close(lis);
    return 0;
}
```

One process plays both sides on port 6910 (change `PORT` if it is busy). The "client" sends two lines the
"server" never reads; the server says goodbye and closes; the client keeps talking:

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o rst rst.c
$ ./rst close
client: write: Connection reset by peer
client: got ERROR :Excess flood
client: end of connection
$ ./rst linger
client: got ERROR :Excess flood
client: end of connection
server: threw away 38 bytes
```

With `close()`, the client's next `write()` fails with `ECONNRESET`; a real client treats that as fatal and
often exits before reading the `ERROR` line that explains why. With `shutdown()` it sees a normal end after
the last line. (musl prints the same: this is the kernel's TCP.)

Lingering needs a time limit, since a hostile client may never close. `parleyd` waits at most 2 seconds.

### Half-open connections

A connection is **half-open** when one side vanished without FIN or RST (power loss, network down). Silence
looks like "nothing to say": a read blocks forever, and a write fails only when TCP gives up retransmitting,
about 15 minutes on Linux.

The only cure is a **timer**, which `parleyd` has had since [Chapter 18](18-event-loop.md): a client silent
for `-t` seconds gets a `PING` and is dropped if it doesn't answer; a closing connection gets 2 seconds.
(`SO_KEEPALIVE` makes the kernel probe idle connections, but Linux waits 2 hours by default.) The timers also
stop **slowloris** attacks — many connections, each sending a byte now and then to use up descriptors — since
an unregistered connection is dropped after `-t` seconds whatever it sends.

## Logging hostile input

A log is for the person running the server: log every disconnect with its reason, and every refusal. But
**a client's bytes, written raw into a log, can lie.**

`examples/ch21/logsafe.c`
```c
/* logsafe.c - never log a client's bytes as they are. */
#include <stdio.h>

/* Copy src into dst[size], writing control bytes as \xHH. */
static void escape(char *dst, size_t size, const char *src)
{
    size_t len = 0;
    for (; *src != '\0'; src++) {
        unsigned char c = (unsigned char)*src;
        char tmp[5];
        int n = c < 0x20 || c == 0x7f ? snprintf(tmp, sizeof tmp, "\\x%02x", c)
                                      : snprintf(tmp, sizeof tmp, "%c", c);
        if (len + (size_t)n >= size)
            break;                     /* no room: cut it short */
        for (int i = 0; i < n; i++)
            dst[len++] = tmp[i];
    }
    dst[len] = '\0';
}

int main(void)
{
    /* A QUIT reason chosen by a hostile client: a fake log line, and an
     * escape sequence that clears the screen of whoever reads the log. */
    const char *reason = "bye\nparleyd: info: root joined\x1b[2J";
    char safe[128];

    fprintf(stderr, "parleyd: info: eve left (%s)\n", reason);   /* wrong */
    escape(safe, sizeof safe, reason);
    fprintf(stderr, "parleyd: info: eve left (%s)\n", safe);     /* right */
    return 0;
}
```

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o logsafe logsafe.c && ./logsafe 2>&1 | cat -v
parleyd: info: eve left (bye
parleyd: info: root joined^[[2J)
parleyd: info: eve left (bye\x0aparleyd: info: root joined\x1b[2J)
```

The first line forges a log entry, and `ESC [2J` would clear a terminal; the escaped version shows exactly
what was sent. In `parleyd` no newline reaches the log (the framer cuts at `'\n'`) and nicks are checked, but
a QUIT reason may hold an ESC byte; Exercise 2 fixes that.

- **Log per event, not per line** (joined, left, refused), or a flood fills the disk.
- **Log the reason with the event.** `spam left (Excess flood)` beats `spam left`.

## Fuzzing

Unit tests check the cases you thought of. A **fuzzer** finds the others: it feeds a function thousands of
random inputs and checks an **oracle**, something that must *always* hold. Two oracles fit almost any code:

- **Properties.** "A parsed nick is never longer than the limit." "Formatting a parsed message and parsing it
  again gives the same parts." "Framing gives the same lines however the input is split."
- **Sanitizers.** Under ASan ([Chapter 10](10-errors-and-debugging.md)) every silent out-of-bounds access
  becomes a crash with a report, for free.

Don't be *too* random: random bytes almost never form `NICK `, so the parser would only see its error paths.
Mix in **interesting** bytes: keywords, separators, boundary lengths. A tiny fuzzer for a tiny buggy parser:

`examples/ch21/fuzzme.c`
```c
/* fuzzme.c - a tiny fuzzer finds a bug in a tiny parser.
 *     fuzzme [ROUNDS [SEED]] */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NAME_LEN 9    /* nicks have at most 9 bytes */

/* "NICK name": copy name into name[NAME_LEN + 1]. Returns 0, or -1 if
 * the line is not like that. There is a bug in here. */
static int parse_nick(const char *line, char *name)
{
    if (strncmp(line, "NICK ", 5) != 0)
        return -1;
    size_t n = strcspn(line + 5, " ");
    if (n == 0 || n > NAME_LEN + 1)
        return -1;
    memcpy(name, line + 5, n);
    name[n] = '\0';
    return 0;
}

/* A random line: often "NICK ", then 0-15 bytes from a small alphabet. */
static void random_line(char *line)
{
    static const char alphabet[] = "ab ";
    size_t len = 0;
    if (rand() % 2 == 0) {
        strcpy(line, "NICK ");
        len = 5;
    }
    for (int n = rand() % 16; n > 0; n--)
        line[len++] = alphabet[rand() % 3];
    line[len] = '\0';
}

int main(int argc, char *argv[])
{
    long rounds = argc > 1 ? atol(argv[1]) : 100000;
    unsigned seed = argc > 2 ? (unsigned)atol(argv[2]) : 1;
    srand(seed);
    for (long i = 1; i <= rounds; i++) {
        char line[32];
        random_line(line);
        char *name = malloc(NAME_LEN + 1);     /* exactly the size needed */
        if (name == NULL)
            return 1;
        /* The property: a name that parses fits in NAME_LEN bytes. */
        if (parse_nick(line, name) == 0 && strlen(name) > NAME_LEN) {
            printf("round %ld (seed %u): \"%s\" gave a %zu-byte name\n",
                   i, seed, line, strlen(name));
            free(name);
            return 1;
        }
        free(name);
    }
    printf("%ld rounds (seed %u): no bug found\n", rounds, seed);
    return 0;
}
```

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o fuzzme fuzzme.c
$ ./fuzzme 100
100 rounds (seed 1): no bug found
$ ./fuzzme
round 752 (seed 1): "NICK baabbaabbb b b" gave a 10-byte name
$ ./fuzzme 100000 7
round 93 (seed 7): "NICK aabbabbaba " gave a 10-byte name
```

`n > NAME_LEN + 1` should be `n > NAME_LEN`: a 10-byte name passes, and `name[10] = '\0'` writes past the
10-byte block. Only the property noticed; ASan catches the write itself, at its line:

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -g -fsanitize=address,undefined -o fuzzme fuzzme.c
$ ./fuzzme
=================================================================
==235359==ERROR: AddressSanitizer: heap-buffer-overflow on address 0x502000005dfa at pc 0x... bp 0x... sp 0x...
WRITE of size 1 at 0x502000005dfa thread T0
    #0 0x... in parse_nick .../examples/ch21/fuzzme.c:19
    #1 0x... in main .../examples/ch21/fuzzme.c:49
...
0x502000005dfa is located 0 bytes to the right of 10-byte region [0x502000005df0,0x502000005dfa)
...
SUMMARY: AddressSanitizer: heap-buffer-overflow .../examples/ch21/fuzzme.c:19 in parse_nick
```

(Addresses and the process number vary; `...` marks lines cut from the long report.)

**Print the seed** (with `srand(seed)` it replays a failure) and **stop after a few failures** (one bug
usually fails thousands of rounds).

### glibc vs musl: random numbers and sanitizers

A seed replays only with the same C library: C99 says `rand()` returns 0 to `RAND_MAX`, not which numbers.
With musl:

```console
$ musl-gcc -std=c99 -Wall -Wextra -pedantic -static -o fuzzme fuzzme.c
$ ./fuzzme
round 529 (seed 1): "NICK bbbbbbbaaa " gave a 10-byte name
$ ./fuzzme 100000 7
round 2181 (seed 7): "NICK abaaabbbab " gave a 10-byte name
```

To replay everywhere, bring your own generator. **xorshift32** (George Marsaglia, 2003) is three lines:

`examples/ch21/rng.c`
```c
/* rng.c - rand() differs between C libraries; xorshift32 does not. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/* George Marsaglia's xorshift32: tiny, fast, the same everywhere.
 * The state must never be 0. */
static uint32_t xorshift32(uint32_t *state)
{
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return *state = x;
}

int main(void)
{
    srand(42);
    printf("rand():       ");
    for (int i = 0; i < 4; i++)
        printf(" %d", rand() % 1000);
    printf("\n");

    uint32_t state = 42;
    printf("xorshift32(): ");
    for (int i = 0; i < 4; i++)
        printf(" %lu", (unsigned long)(xorshift32(&state) % 1000));
    printf("\n");
    return 0;
}
```

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o rng rng.c && ./rng
rand():        166 740 881 241
xorshift32():  432 348 59 16
$ musl-gcc -std=c99 -Wall -Wextra -pedantic -static -o rng rng.c && ./rng
rand():        560 839 38 940
xorshift32():  432 348 59 16
```

Neither `rand()` is wrong. **Simpler:** your own generator, when results must match across platforms.
`parley`'s fuzzer only replays where it found the bug, so `rand()` will do.

Sanitizers differ more: the ASan and UBSan run-time libraries are built against glibc:

```console
$ musl-gcc -std=c99 -g -static -fsanitize=address -o fuzzme fuzzme.c
gcc: error: cannot specify -static with -fsanitize=address
```

musl gets only UBSan in **trap mode** (`-fsanitize=undefined -fsanitize-undefined-trap-on-error`): no
run-time library, no report, just `SIGILL` (illegal instruction) at the bad operation. That finds undefined
behaviour such as signed overflow, not heap overflows like the one above. **Better:** glibc, clearly, for
testing. `make SANITIZE=1` uses glibc with full ASan + UBSan; `make SANITIZE=1 LIBC=musl` falls back to trap
mode. Fuzz with glibc + ASan, ship with either libc.

## Integration tests

Unit tests ([Chapter 11](11-testing.md)) call functions. An **integration test** runs the real program: start
`parleyd`, connect TCP clients, send lines, compare every reply. With tools from
[Chapter 13](13-processes-and-signals.md) and [Chapter 17](17-threads.md):

| Step | How |
|---|---|
| start the server | `fork()`, then `execv()` in the child |
| keep its log | `dup2()` a log file onto the child's stderr before `execv()` |
| find a free port | `-p 0`: the kernel picks one, the server logs which |
| wait for it | read the log for `listening on 127.0.0.1:PORT`, with a time limit |
| stop it | `kill(pid, SIGTERM)`, then `waitpid()` and check the exit status |

Three rules make such tests reliable:

- **Every wait has a time limit.** A hanging test is worse than a failing one. Wait with `poll()` and a
  timeout, and report "nothing (timeout)".
- **Never use fixed ports.** Port 0 avoids collisions with other programs and parallel tests.
- **Ignore SIGPIPE.** If the server dies, the test's next write must fail with an error, not kill the test.

## Building parley: the hardened server

Seven steps, each with a checkpoint.

| Step | What | Files |
|---|---|---|
| 1 | flood control (`-f`) | `server.h`, `conn.h`, `server.c`, `parleyd.c` |
| 2 | reject NUL bytes (`400`) | `msg.h`, `msg.c`, `server.c`, `parley.c`, tests |
| 3 | fit `-m` to the open file limit | `parleyd.c` |
| 4 | survive EMFILE with a spare descriptor | `server.c` |
| 5 | close gracefully (`CONN_LINGER`) | `conn.h`, `conn.c`, `room.c`, `server.c` |
| 6 | the fuzzer | `tests/fuzz.c`, `Makefile` |
| 7 | the integration test | `tests/itest.c`, `Makefile` |

Checkpoints start the server with `&` and `sleep 0.2`; job lines such as `[1] 12345` are left out.

### Step 1: flood control

In `src/server.h`, add a field to `struct server_config` and the cost of a line:

`project/ch21/src/server.h` (part)
```c
struct server_config {
    size_t max_clients;     /* more get "ERROR :Server full" */
    int idle_secs;          /* PING after this long without input; 0: never */
    int flood_lines;        /* lines a client may send in a burst; 0: no limit */
};

/* Flood control: a client may send flood_lines lines at once, then one
 * line per FLOOD_COST_MS on average. */
#define FLOOD_COST_MS 200
```

Each connection gets a penalty clock. Add it to `struct conn` in `src/conn.h`, after `ping_sent`:

```c
    long long flood_clock;     /* flood control: see flood_check() in server.c */
```

In `src/server.c`, a new client's clock starts at "now": in `accept_clients()`, the compound literal gets one
more field:

```c
        *c = (struct conn){
            .fd = fd, .state = CONN_OPEN, .connected = now, .last_active = now,
            .flood_clock = now,
        };
```

The check is `penalty.c`'s clock plus a disconnect. Put it before `handle_input()`:

`project/ch21/src/server.c` (part)
```c
/* Flood control with a "penalty clock" (as in IRC servers): every line
 * moves the client's clock FLOOD_COST_MS ahead, and the clock never lags
 * behind the real time. A client sending slowly keeps its clock at "now";
 * one sending fast pushes it into the future. Once it is more than
 * flood_lines lines ahead, the client is disconnected. Returns true if c
 * may go on. */
static bool flood_check(struct conn *c, long long now)
{
    if (config->flood_lines == 0)
        return true;
    if (c->flood_clock < now)
        c->flood_clock = now;
    c->flood_clock += FLOOD_COST_MS;
    if (c->flood_clock - now <= (long long)config->flood_lines * FLOOD_COST_MS)
        return true;
    conn_send(c, &(struct msg){ .command = "ERROR", .trailing = "Excess flood" });
    conn_close(c, "Excess flood");
    return false;
}
```

Bad lines count too (thousands of too-long lines are a flood), so the check comes first in
`handle_input()`'s loop:

```c
    enum frame_status st;
    while (c->state == CONN_OPEN
           && (st = msg_take_line(&c->in, &c->skipping, line)) != FRAME_NONE) {
        if (!flood_check(c, now))
            break;
        if (st == FRAME_TOO_LONG)
            conn_numeric(c, "417", NULL, "Input line too long");
        else
            room_handle_line(&room, c, line);
    }
```

`conn_close()` sets the state to `CONN_CLOSING`, so the loop would stop anyway; the `break` makes it obvious.

Last, the option. In `src/parleyd.c`, add the default and the limit:

```c
#define DEFAULT_FLOOD 20
#define FLOOD_MAX 1000
```

Add `long flood;` to `struct config` (after `idle`) and `.flood = DEFAULT_FLOOD,` to its initializer in
`main()`. The help gets two lines, and the usage line now needs two:

```c
    printf("usage: parleyd [-p PORT] [-b ADDR] [-m MAXCLIENTS] [-t IDLE] [-f LINES]\n"
           "               [-v] [-T]\n"
           "  -p PORT        TCP port to listen on, 0: any free port (default %d)\n"
           "  -b ADDR        address to listen on (default 127.0.0.1)\n"
           "  -m MAXCLIENTS  most clients at once, 1-%d (default %d)\n"
           "  -t IDLE        PING clients idle for IDLE seconds, drop them after\n"
           "                 twice that; 0: never (default %d)\n"
           "  -f LINES       drop clients sending more than LINES lines at once\n"
           "                 (5 lines/s on average); 0: no limit (default %d)\n"
           "  -v             verbose (debug) logging\n"
           "  -T             no timestamps in the log\n"
           "  -h, --help     show this help and exit\n"
           "  --version      show the version and exit\n",
           DEFAULT_PORT, MAX_CLIENTS_MAX, DEFAULT_MAX_CLIENTS, DEFAULT_IDLE,
           DEFAULT_FLOOD);
```

In `parse_args()`, `-f` takes a value like the others:

```c
        if (opt == 'p' || opt == 'b' || opt == 'm' || opt == 't' || opt == 'f') {
```

and gets its own case:

```c
        case 'f':
            if (!str_to_long(value, 0, FLOOD_MAX, &cfg->flood))
                usage_error("invalid flood limit '%s' (expected 0-%d)", value, FLOOD_MAX);
            break;
```

Pass it on in `main()`:

```c
    struct server_config scfg = {
        .max_clients = (size_t)cfg.max_clients,
        .idle_secs = (int)cfg.idle,
        .flood_lines = (int)cfg.flood,
    };
```

#### Checkpoint

```console
$ make -s test-all
unit: 29 passed, 0 failed
unit: 29 passed, 0 failed
$ build/glibc/parleyd -p 6667 -T 2>server.log & server=$!; sleep 0.2
$ { echo 'NICK spam'; yes 'MSG :spam' | head -n 30; echo QUIT; } | nc -C 127.0.0.1 6667 | uniq -c
      1 :parley 001 spam :Welcome to parley, spam
      1 :spam JOIN
      1 :parley 353 spam :spam
      1 :parley 366 spam :End of NAMES
     19 :spam MSG :spam
      1 ERROR :Excess flood
```

`uniq -c` counts repeated lines. `NICK` used one of the 20 lines, so 19 messages got through; `QUIT` was
never reached. Run it a few times, and sometimes you see this instead (the count varies):

```console
$ { echo 'NICK spam'; yes 'MSG :spam' | head -n 30; } | nc -C 127.0.0.1 6667 | uniq -c
nc: write failed (0/2): Broken pipe
      1 :parley 001 spam :Welcome to parley, spam
      1 :spam JOIN
      1 :parley 353 spam :spam
      1 :parley 366 spam :End of NAMES
     12 :spam MSG :spam
```

No `ERROR`, though the server sent it: the server closed with unread input, the kernel sent RST, and `nc`
lost the rest. Step 5 fixes it. Now without a limit, and with a bad value:

```console
$ kill $server; wait $server
$ build/glibc/parleyd -p 6667 -T -f 0 2>server.log & server=$!; sleep 0.2
$ { echo 'NICK spam'; yes 'MSG :spam' | head -n 30; echo QUIT; } | nc -C 127.0.0.1 6667 | uniq -c
      1 :parley 001 spam :Welcome to parley, spam
      1 :spam JOIN
      1 :parley 353 spam :spam
      1 :parley 366 spam :End of NAMES
     30 :spam MSG :spam
      1 ERROR :Goodbye
$ kill $server; wait $server; cat server.log
parleyd: info: listening on 127.0.0.1:6667
parleyd: info: spam joined
parleyd: info: spam left (Quit)
parleyd: info: got signal 15 (Terminated), shutting down
$ build/glibc/parleyd -f 5000; echo "exit $?"
parleyd: invalid flood limit '5000' (expected 0-1000)
Try 'parleyd --help' for more information.
exit 2
```

### Step 2: reject NUL bytes

Every line passes through the framer, so the check goes there. `src/msg.h` gets a new status:

`project/ch21/src/msg.h` (part)
```c
enum frame_status {
    FRAME_NONE,       /* no complete line yet: read more input */
    FRAME_LINE,       /* a line (without "\r\n") was copied into line[] */
    FRAME_TOO_LONG,   /* a line longer than MSG_LINE_MAX was thrown away */
    FRAME_BAD         /* a line containing a NUL byte was thrown away */
};
```

In `src/msg.c`, `msg_take_line()` checks the complete line for a NUL with `memchr()` (which, unlike
`strchr()`, takes a length and doesn't stop at `'\0'`) before copying it:

`project/ch21/src/msg.c` (part)
```c
        if (n > MSG_LINE_MAX) {
            sb_consume(in, n);
            return FRAME_TOO_LONG;
        }
        /* A '\0' would end the line early for every string function: in
         * "MSG :hi\0QUIT" one part of the program might see "MSG :hi" and
         * another the whole line. Never accept such a line. */
        if (memchr(in->data, '\0', n) != NULL) {
            sb_consume(in, n);
            return FRAME_BAD;
        }
        memcpy(line, in->data, n - 1);
```

The server answers with numeric 400, already used for malformed messages. In `handle_input()`'s loop:

```c
        if (st == FRAME_TOO_LONG)
            conn_numeric(c, "417", NULL, "Input line too long");
        else if (st == FRAME_BAD)
            conn_numeric(c, "400", NULL, "Line contains a NUL byte");
        else
            room_handle_line(&room, c, line);
```

The client frames both keyboard and server input. In `src/parley.c`, `read_keyboard()`:

`project/ch21/src/parley.c` (part)
```c
    while (!quitting && (st = msg_take_line(&kbd, &kbd_skipping, line)) != FRAME_NONE) {
        if (st == FRAME_TOO_LONG)
            note("-- line too long (max %d bytes), not sent", INPUT_MAX);
        else if (st == FRAME_BAD)
            note("-- line contains a NUL byte, not sent");
        else
            handle_input_line(line);
    }
```

and `read_server()`, which used to ignore bad lines silently:

```c
    enum frame_status st;
    while ((st = msg_take_line(&server_in, &server_skipping, line)) != FRAME_NONE) {
        if (st == FRAME_LINE)
            handle_server_line(line);
        else
            show("!! the server sent a bad line (ignored)");
    }
    return n > 0;
```

Unit tests: `frame_all()` in `tests/test_msg.c` builds its input with `strlen()`, so it can't carry a NUL,
but teach it the new status anyway (its comment and one line change):

`project/ch21/tests/test_msg.c` (part)
```c
/* Feed text into a framer in pieces of size step, collecting what comes out
 * as "line|line|" ("!" for a too-long line, "?" for a bad one) in got[size]. */
```

```c
            strcat(got, st == FRAME_LINE ? line : st == FRAME_TOO_LONG ? "!" : "?");
```

and add a test that fills the buffer itself, with `sizeof` for the length:

```c
/* A string literal can't hold a NUL for frame_all() (it uses strlen), so
 * this test feeds the buffer itself. */
void test_frame_nul(void)
{
    static const char text[] = "NICK al\r\nMSG :a\0b\r\nQUIT\r\n";
    struct strbuf in = { 0 };
    bool skipping = false;
    char line[MSG_LINE_MAX + 1];

    sb_append(&in, text, sizeof text - 1);
    CHECK_INT(msg_take_line(&in, &skipping, line), FRAME_LINE);
    CHECK_STR(line, "NICK al");
    CHECK_INT(msg_take_line(&in, &skipping, line), FRAME_BAD);
    CHECK_INT(msg_take_line(&in, &skipping, line), FRAME_LINE);
    CHECK_STR(line, "QUIT");
    CHECK_INT(msg_take_line(&in, &skipping, line), FRAME_NONE);
    sb_free(&in);
}
```

Register it in `tests/tests.h`, after `X(frame_too_long)`:

```c
    X(frame_nul)                    \
```

#### Checkpoint

`printf` writes a NUL for `\0`. `parley` frames its keyboard (here a pipe) the same way:

```console
$ make -s test-all
unit: 30 passed, 0 failed
unit: 30 passed, 0 failed
$ build/glibc/parleyd -p 6667 -T 2>server.log & server=$!; sleep 0.2
$ printf 'NICK nul\r\nMSG :a\0b\r\nQUIT\r\n' | nc 127.0.0.1 6667
:parley 001 nul :Welcome to parley, nul
:nul JOIN
:parley 353 nul :nul
:parley 366 nul :End of NAMES
:parley 400 nul :Line contains a NUL byte
ERROR :Goodbye
$ printf 'hi\0there\nbye\n' | build/musl/parley -p 6667 -n nully; echo "exit $?"
-- line contains a NUL byte, not sent
-- Welcome to parley, nully
--> nully joined
-- Users: nully
<nully> bye
exit 0
$ kill $server; wait $server; cat server.log
parleyd: info: listening on 127.0.0.1:6667
parleyd: info: nul joined
parleyd: info: nul left (Quit)
parleyd: info: nully joined
parleyd: info: nully left (Quit)
parleyd: info: got signal 15 (Terminated), shutting down
```

(The client's note comes first: it reads the whole pipe before the server's welcome arrives.)

### Step 3: fit `-m` to the open file limit

`getrlimit()` is XSI, so `src/parleyd.c` asks for it before any `#include`:

`project/ch21/src/parleyd.c` (part)
```c
/* parleyd.c - the parley chat server: options and the main loop.
 * Version 3: one thread, many sockets (see server.c). */

/* getrlimit() is part of the X/Open System Interfaces (XSI) option of
 * POSIX, so ask for those too; 700 includes all of POSIX.1-2008. */
#define _XOPEN_SOURCE 700
#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <unistd.h>
```

(It implies the Makefile's `_POSIX_C_SOURCE 200809L`; both together are fine. glibc and musl would compile
this without it, but other systems are stricter, and it documents what the code needs.)

How many descriptors does the server need besides the clients? Count them, next to the other `#define`s:

```c
/* File descriptors the server needs besides one per client: stdin, stdout,
 * stderr, the listening socket, the self-pipe (2) and the spare (1). */
#define FDS_RESERVED 7
```

The spare comes in step 4; count it now. Then the function:

```c
/* Each client needs a file descriptor, and a process may only have
 * RLIMIT_NOFILE of them open (see "ulimit -n"). Lower *max_clients to
 * what fits, so clients get "Server full" rather than accept() failing. */
static void limit_clients(long *max_clients)
{
    struct rlimit rl;
    if (getrlimit(RLIMIT_NOFILE, &rl) < 0)
        die("getrlimit: %s", strerror(errno));
    log_msg(LOG_DEBUG, "open file limit: %llu", (unsigned long long)rl.rlim_cur);
    if (rl.rlim_cur == RLIM_INFINITY || rl.rlim_cur >= (rlim_t)(*max_clients + FDS_RESERVED))
        return;
    long fit = rl.rlim_cur > FDS_RESERVED ? (long)rl.rlim_cur - FDS_RESERVED : 0;
    if (fit == 0)
        die("the open file limit (%llu) leaves no room for clients",
            (unsigned long long)rl.rlim_cur);
    log_msg(LOG_WARN, "the open file limit (%llu) allows only %ld clients",
            (unsigned long long)rl.rlim_cur, fit);
    *max_clients = fit;
}
```

`rlim_t` is printed through a cast to `unsigned long long`, as in `fdlimit.c`. Call the function in `main()`
right after the "listening on" message:

```c
    log_msg(LOG_INFO, "listening on %s", where);
    limit_clients(&cfg.max_clients);
```

#### Checkpoint

Run `ulimit -n` in a subshell `( ... )` so your shell keeps its limit, and `exec` the server so `$server` is
its PID. For idle clients, bash connects a socket when a redirection names `/dev/tcp/HOST/PORT` (a bash
feature, not a file): `exec 5<>/dev/tcp/...` opens it as descriptor 5, `exec 5>&-` closes it.

```console
$ make -s; make -s LIBC=musl
$ (ulimit -n 12; exec build/musl/parleyd -p 6667 -T) & server=$!; sleep 0.2
parleyd: info: listening on 127.0.0.1:6667
parleyd: warning: the open file limit (12) allows only 5 clients
$ exec 5<>/dev/tcp/127.0.0.1/6667 6<>/dev/tcp/127.0.0.1/6667 7<>/dev/tcp/127.0.0.1/6667 8<>/dev/tcp/127.0.0.1/6667 9<>/dev/tcp/127.0.0.1/6667
$ nc -d 127.0.0.1 6667
parleyd: warning: 127.0.0.1:50828: server full
ERROR :Server full
$ exec 5>&- 6>&- 7>&- 8>&- 9>&-; kill $server; wait $server; echo "exit $?"
parleyd: info: got signal 15 (Terminated), shutting down
exit 0
```

The sixth client (`nc -d` sends nothing) gets the normal "Server full" refusal; your port after `127.0.0.1:`
will differ. If the limit leaves no room for clients, the server stops at once, and `-v` shows the limit:

```console
$ (ulimit -n 7; exec build/glibc/parleyd -p 6667 -T); echo "exit $?"
parleyd: info: listening on 127.0.0.1:6667
parleyd: error: the open file limit (7) leaves no room for clients
exit 1
$ (ulimit -n 8; exec build/glibc/parleyd -p 6667 -T -v) & server=$!; sleep 0.2
parleyd: debug: verbose logging is on
parleyd: info: listening on 127.0.0.1:6667
parleyd: debug: open file limit: 8
parleyd: warning: the open file limit (8) allows only 1 clients
$ kill $server; wait $server
parleyd: info: got signal 15 (Terminated), shutting down
```

Yes, "1 clients": correct plurals everywhere are more code than they are worth here.

### Step 4: survive EMFILE with a spare descriptor

Running out is still possible: the limit can change at run time (`prlimit`), and there is the system-wide
limit (ENFILE). In `src/server.c`, include `<fcntl.h>` for `open()` and add the spare:

`project/ch21/src/server.c` (part)
```c
/* An open file descriptor we keep in reserve, for when we run out. */
static int spare_fd = -1;

/* accept() failed with EMFILE or ENFILE: no file descriptor is left for
 * the new client. The connection then stays in the listen queue, and
 * poll() reports the listening socket readable again at once: the server
 * would spin at 100% CPU. So free the spare descriptor, accept the
 * client with it, say why we can't serve it, and close it again.
 * Linux finds a free descriptor before it looks at the queue, so with a
 * full table accept() fails with EMFILE even when nobody is waiting: the
 * second accept() then fails with EAGAIN, and there is nothing to say. */
static void refuse_client(int listen_fd)
{
    static const char full[] = "ERROR :Server full\r\n";
    const char *why = strerror(errno);
    if (spare_fd < 0) {
        log_msg(LOG_WARN, "accept: %s", why);
        return;
    }
    close(spare_fd);
    int fd = accept(listen_fd, NULL, NULL);
    if (fd >= 0) {
        log_msg(LOG_WARN, "accept: %s: refused a client", why);
        ssize_t r = write(fd, full, sizeof full - 1);   /* best effort */
        (void)r;
        close(fd);
    }
    spare_fd = open("/dev/null", O_RDONLY);
}
```

- `why` is saved first: `close()` and `accept()` may change `errno`.
- The refused client gets one `write()` ("best effort"). If its socket buffer is full the message is lost; we
  won't keep a descriptor for it.
- `(void)r` keeps gcc quiet: in a "fortified" build (Ubuntu's gcc enables `_FORTIFY_SOURCE` at `-O2`) glibc
  marks `write()` `warn_unused_result`, which `(void)write(...)` doesn't silence. musl has no such mark.
- If reopening `/dev/null` fails, `spare_fd` is -1 and later refusals only log.

In `accept_clients()`, the error branch calls it:

```c
        if (fd < 0) {
            if (errno == EMFILE || errno == ENFILE)
                refuse_client(listen_fd);
            else if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
                log_msg(LOG_WARN, "accept: %s", strerror(errno));
            return;
        }
```

`server_run()` opens the spare at the start and closes it at the end:

```c
    config = cfg;
    int wake_fd = sig_wake_fd();
    spare_fd = open("/dev/null", O_RDONLY);
    if (wake_fd < 0 || spare_fd < 0 || set_nonblock(listen_fd) < 0) {
        log_msg(LOG_ERROR, "setting up: %s", strerror(errno));
        return -1;
    }
```

```c
    close(wake_fd);
    close(spare_fd);
    return status;
```

#### Checkpoint

Rerun the step-3 test. With the spare, all 12 descriptors are in use, so `accept()` itself fails for the
sixth client:

```console
$ make -s; make -s LIBC=musl
$ (ulimit -n 12; exec build/glibc/parleyd -p 6667 -T) & server=$!; sleep 0.2
parleyd: info: listening on 127.0.0.1:6667
parleyd: warning: the open file limit (12) allows only 5 clients
$ exec 5<>/dev/tcp/127.0.0.1/6667 6<>/dev/tcp/127.0.0.1/6667 7<>/dev/tcp/127.0.0.1/6667 8<>/dev/tcp/127.0.0.1/6667 9<>/dev/tcp/127.0.0.1/6667
$ nc -d 127.0.0.1 6667
parleyd: warning: accept: Too many open files: refused a client
ERROR :Server full
```

The refused client still learns why. Count the descriptors in Linux's `/proc/PID/fd`:

```console
$ ls -v /proc/$server/fd | while read fd; do echo "$fd -> $(readlink /proc/$server/fd/$fd)"; done
0 -> /dev/null
1 -> pipe:[1442461]
2 -> pipe:[1442461]
3 -> socket:[1435191]
4 -> pipe:[1435192]
5 -> pipe:[1435192]
6 -> /dev/null
7 -> socket:[1435193]
8 -> socket:[1435194]
9 -> socket:[1435196]
10 -> socket:[1435198]
11 -> socket:[1435200]
$ exec 5>&- 6>&- 7>&- 8>&- 9>&-; kill $server; wait $server
parleyd: info: got signal 15 (Terminated), shutting down
```

`FDS_RESERVED` plus five: the standard three (targets depend on how you started it), listening socket 3,
self-pipe 4 and 5, spare 6, clients 7 to 11. Inode numbers will differ.

Don't exhaust the system-wide limit on a shared machine; let `strace` fake it:
`-e inject=accept:error=EMFILE:when=1` fails the first `accept()` with EMFILE without running it. The first
client is refused, the second served:

```console
$ strace -o glibc.trace -e trace=accept,open,openat,close,write -e inject=accept:error=EMFILE:when=1 build/glibc/parleyd -p 6667 -T & sleep 0.2
parleyd: info: listening on 127.0.0.1:6667
$ printf 'NICK x\n' | nc -C 127.0.0.1 6667; printf 'NICK x\nQUIT\n' | nc -C 127.0.0.1 6667
parleyd: warning: accept: Too many open files: refused a client
ERROR :Server full
parleyd: info: x joined
parleyd: info: x left (Quit)
:parley 001 x :Welcome to parley, x
:x JOIN
:parley 353 x :x
:parley 366 x :End of NAMES
ERROR :Goodbye
$ kill $(pgrep -n parleyd); wait; sed -n '/INJECTED/,/dev.null/p' glibc.trace
parleyd: info: got signal 15 (Terminated), shutting down
accept(3, 0x7fff95bed860, [128])        = -1 EMFILE (Too many open files) (INJECTED)
close(6)                                = 0
accept(3, NULL, NULL)                   = 6
write(2, "parleyd: warning: accept: Too ma"..., 64) = 64
write(6, "ERROR :Server full\r\n", 20)  = 20
close(6)                                = 0
openat(AT_FDCWD, "/dev/null", O_RDONLY) = 6
```

(`pgrep -n parleyd` prints the newest `parleyd`'s PID; signal the server, not `strace`, whose PID is `$!`.
The first address in the trace varies.) From the top: close the spare (6), accept the client into 6, log, tell
the client, close it, reopen the spare as 6.

### glibc vs musl: running out of descriptors in the server

The same test with the musl build:

```console
$ strace -o musl.trace -e trace=accept,open,openat,close,write -e inject=accept:error=EMFILE:when=1 build/musl/parleyd -p 6667 -T & sleep 0.2
parleyd: info: listening on 127.0.0.1:6667
$ printf 'NICK x\n' | nc -C 127.0.0.1 6667; printf 'NICK x\nQUIT\n' | nc -C 127.0.0.1 6667
parleyd: warning: accept: No file descriptors available: refused a client
ERROR :Server full
parleyd: info: x joined
parleyd: info: x left (Quit)
:parley 001 x :Welcome to parley, x
:x JOIN
:parley 353 x :x
:parley 366 x :End of NAMES
ERROR :Goodbye
$ kill $(pgrep -n parleyd); wait; sed -n '/INJECTED/,/dev.null/p' musl.trace
parleyd: info: got signal 15 (Terminated), shutting down
accept(3, 0x7ffe697c0370, [128])        = -1 EMFILE (Too many open files) (INJECTED)
close(6)                                = 0
accept(3, NULL, NULL)                   = 6
write(6, "ERROR :Server full\r\n", 20)  = 20
close(6)                                = 0
open("/dev/null", O_RDONLY|O_LARGEFILE) = 6
```

Same behaviour, three different details:

| | glibc | musl |
|---|---|---|
| `strerror(EMFILE)` | Too many open files | No file descriptors available |
| `open()` becomes | `openat(AT_FDCWD, ...)` | `open(...)` with `O_LARGEFILE` |
| the log line | `write(2, ...)` | not in the trace |

musl's `stdio` writes with `writev()` (buffer and new data in one call), and we traced only `write`; add
`writev` and it appears as `writev(2, [{iov_base="parleyd: warning: accept: No fil"..., iov_len=74}], 1) = 74`.
glibc implements `open()` with the newer `openat()`; musl uses the old `open()` where the kernel has it and
sets `O_LARGEFILE` itself (glibc's 64-bit build needn't). Your program doesn't care, but when reading traces,
**trace by purpose (`-e trace=%file`, `%desc`, `%net`), not by the names you expect.**

### Step 5: close gracefully (`CONN_LINGER`)

First the problem at full size: flood step 4's server with 1000 lines instead of 30, three times each, with
`nc` and with the `parley` client:

```console
$ build/glibc/parleyd -p 6667 -T 2>server.log & server=$!; sleep 0.2
$ { echo 'NICK spam'; yes 'MSG :spam' | head -n 1000; } | nc -C 127.0.0.1 6667 | uniq -c
nc: write failed (0/2): Broken pipe
      1 :parley 001 spam :Welcome to parley, spam
      1 :spam JOIN
      1 :parley 353 spam :spam
      1 :parley 366 spam :End of NAMES
$ yes spam | head -n 1000 | build/musl/parley -p 6667 -n spam2 | uniq -c
parley: error: sending to server: Connection reset by peer
      1 -- Welcome to parley, spam2
      1 --> spam2 joined
      1 -- Users: spam2
```

The other runs gave 19 messages and the `ERROR` once, 7 and no `ERROR` once, and `parley` printed only the
reset error twice. The client never learns *why* it was dropped: the RST again, from closing with the flood
unread.

The fix is a new state between `CONN_CLOSING` and `CONN_DEAD`. In `src/conn.h`:

`project/ch21/src/conn.h` (part)
```c
enum conn_state {
    CONN_OPEN,       /* normal operation */
    CONN_CLOSING,    /* stop reading; close once the output queue is sent */
    CONN_LINGER,     /* all sent: wait (briefly) for the client to close */
    CONN_DEAD        /* close it now, throwing away unsent output */
};
```

The order matters: `set_closed()` only moves *up* this list. A client that never closes must not hold a
descriptor forever, so the struct gets a time stamp (after `flood_clock`) and a time limit:

```c
    long long closing_since;   /* clock_ms() when it stopped being CONN_OPEN */
};

/* How long a connection may take to close: to receive its last messages
 * (CONN_CLOSING) and to close its end (CONN_LINGER). Then it is dropped. */
#define CLOSE_TIMEOUT_MS 2000
```

Update the comment of `conn_close()` too:

```c
/* Close c once its output queue is sent (state CONN_CLOSING, then
 * CONN_LINGER: see linger() in server.c). */
void conn_close(struct conn *c, const char *reason);
```

The limit also covers `CONN_CLOSING`: until now, a client that stopped reading after `QUIT` kept its
descriptor until it closed or the server stopped. In `src/conn.c`, include `"clock.h"` and note when a
connection stops being open:

`project/ch21/src/conn.c` (part)
```c
static void set_closed(struct conn *c, enum conn_state state, const char *reason)
{
    if (c->state == CONN_OPEN) {       /* keep the first reason */
        str_copy(c->reason, reason, sizeof c->reason);
        c->closing_since = clock_ms();
    }
    /* OPEN -> CLOSING -> LINGER -> DEAD, never back */
    if (state > c->state)
        c->state = state;
}
```

`QUIT` set the state directly, skipping the time stamp. In `src/room.c`, `cmd_quit()`'s last line becomes:

`project/ch21/src/room.c` (part)
```c
    conn_close(c, "Quit");
```

Now `src/server.c`. When a closing connection's output queue is empty, `reap()` calls `linger()` instead of
closing it:

`project/ch21/src/server.c` (part)
```c
/* Everything for c is sent, but close() now could still lose it. Closing
 * a socket with input we never read (say, the rest of a flood) makes the
 * kernel answer with a reset (RST) instead of a normal end: the client's
 * next write fails, and the client may stop before reading our last
 * lines, the ERROR that says why. So close gracefully: shutdown(SHUT_WR)
 * sends the end of our data (FIN); then read and throw away input until
 * the client closes too (or CLOSE_TIMEOUT_MS is over). Meanwhile the
 * client has left the room. */
static void linger(struct conn *c)
{
    room_part(&room, c, c->reason[0] != '\0' ? c->reason : "Connection closed");
    if (shutdown(c->fd, SHUT_WR) < 0)
        conn_drop(c, "Connection closed");
    else
        c->state = CONN_LINGER;
}

/* Close and free the connections that are finished. */
static void reap(void)
{
    /* Backwards, so removing conns[i] does not skip the next one. */
    for (size_t i = room.count; i-- > 0; ) {
        struct conn *c = room.conns[i];
        if (c->state == CONN_CLOSING && c->out.len == 0)
            linger(c);
        if (c->state != CONN_DEAD)
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

`room_part()` announces the `QUIT` and logs "left"; it clears the nick and ignores a client without one, so
calling it twice is safe. The lingering client leaves the room at once.

A lingering connection must be read, or we never see the client close. Add a discarding reader, called at the
top of `handle_input()`:

```c
/* A lingering connection is readable: throw the input away. At the end of
 * the connection (or on an error) close it for good. */
static void discard_input(struct conn *c)
{
    char junk[4096];
    ssize_t n = read(c->fd, junk, sizeof junk);
    if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR))
        conn_drop(c, "Connection closed");
}

/* The client's socket is readable: read once, handle complete lines. */
static void handle_input(struct conn *c, long long now)
{
    char line[MSG_LINE_MAX + 1];
    if (c->state == CONN_LINGER) {
        discard_input(c);
        return;
    }
    ssize_t n = read_into(c->fd, &c->in, 4096);
```

`check_timer()` now runs even with `-t 0`, so it checks `idle_secs` itself:

```c
static void check_timer(struct conn *c, long long now)
{
    if (c->state == CONN_CLOSING || c->state == CONN_LINGER) {
        if (now >= c->closing_since + CLOSE_TIMEOUT_MS)
            conn_drop(c, "Close timeout");
        return;
    }
    if (c->state != CONN_OPEN || config->idle_secs == 0 || now < deadline(c))
        return;
```

(the rest of the function is unchanged), and `poll_timeout()` must wake up for it:

```c
static int poll_timeout(long long now, long long shutdown_at)
{
    long long first = shutdown_at;
    for (size_t i = 0; i < room.count; i++) {
        const struct conn *c = room.conns[i];
        long long d = -1;
        if (c->state == CONN_CLOSING || c->state == CONN_LINGER)
            d = c->closing_since + CLOSE_TIMEOUT_MS;
        else if (c->state == CONN_OPEN && config->idle_secs > 0)
            d = deadline(c);
        if (d >= 0 && (first < 0 || d < first))
            first = d;
    }
    if (first < 0)
        return -1;
    return first <= now ? 0 : (int)(first - now);
}
```

`start_shutdown()` must not tell leaving clients "Server shutting down". In its loop:

```c
        struct conn *c = room.conns[i];
        if (c->state != CONN_OPEN)
            continue;                  /* already on its way out */
```

In `server_run()`, lingering connections are polled for input:

```c
            if (c->state == CONN_OPEN || c->state == CONN_LINGER)
                events |= POLLIN;
```

and their input is handled:

```c
            /* POLLHUP/POLLERR: the peer is gone, read() will tell how. */
            bool reading = c->state == CONN_OPEN || c->state == CONN_LINGER;
            if (reading && (rev & (POLLIN | POLLHUP | POLLERR)))
                handle_input(c, now);
            else if (rev & (POLLHUP | POLLERR))
                conn_drop(c, "Connection closed");
```

and the timers always run (the `if (config->idle_secs > 0)` before the loop goes away):

```c
        /* 4. Timers. */
        for (size_t i = 0; i < room.count; i++)
            check_timer(room.conns[i], now);
```

#### Checkpoint

The same floods as above, three times each:

```console
$ make -s; make -s LIBC=musl
$ build/glibc/parleyd -p 6667 -T 2>server.log & server=$!; sleep 0.2
$ { echo 'NICK spam'; yes 'MSG :spam' | head -n 1000; } | nc -C 127.0.0.1 6667 | uniq -c
      1 :parley 001 spam :Welcome to parley, spam
      1 :spam JOIN
      1 :parley 353 spam :spam
      1 :parley 366 spam :End of NAMES
     19 :spam MSG :spam
      1 ERROR :Excess flood
$ yes spam | head -n 1000 | build/musl/parley -p 6667 -n spam2 | uniq -c
      1 -- Welcome to parley, spam2
      1 --> spam2 joined
      1 -- Users: spam2
     19 <spam2> spam
      1 -- Disconnected: Excess flood
```

`nc` got all 19 messages and the `ERROR` every time. `parley` twice lacked the last line: it reaches the end
of its input (the pipe) and leaves, sometimes before the `ERROR` arrives — its choice, not a lost message.

```console
$ kill $server; wait $server; make -s test-all
parleyd: info: got signal 15 (Terminated), shutting down
unit: 30 passed, 0 failed
unit: 30 passed, 0 failed
```

### Step 6: the fuzzer

The fuzzer, `tests/fuzz.c`, checks properties of the two functions that see raw client input,
`msg_take_line()` and `msg_parse()`, plus `msg_format()`:

`project/ch21/tests/fuzz.c`
```c
/* fuzz.c - throw random input at the protocol code.
 *
 *     fuzz [ROUNDS [SEED]]      (default: 20000 rounds, seed 1)
 *
 * Unit tests check the cases we thought of; a fuzzer finds the ones we
 * didn't. Each round makes up a random line and checks properties that
 * must hold for every input:
 *   - msg_parse() returns a valid status, and on MSG_OK every part is
 *     sane and points into the line;
 *   - formatting a parsed message and parsing it again gives the same parts;
 *   - framing a random stream gives the same lines however it is split.
 * rand() with a fixed seed makes every run the same: a failure can be
 * reproduced with the seed it prints. Run it under ASan (make SANITIZE=1
 * test) so that out-of-bounds reads are caught too.
 */
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "msg.h"
#include "str.h"
#include "strbuf.h"

static unsigned long failures;

#define FAIL(...)                                                       \
    do {                                                                \
        failures++;                                                     \
        printf("fuzz: ");                                               \
        printf(__VA_ARGS__);                                            \
        putchar('\n');                                                  \
    } while (0)

static int random_below(int n)
{
    return rand() % n;
}

/* A random byte, mostly ones that mean something to the parser. NUL,
 * '\r' and '\n' never appear: lines come out of the framer without them. */
static char random_byte(void)
{
    static const char interesting[] = "  ::abcNICKMSG01";
    if (random_below(4) > 0)
        return interesting[random_below((int)sizeof interesting - 1)];
    int c;
    do
        c = 1 + random_below(255);
    while (c == '\r' || c == '\n');
    return (char)c;
}

static void random_line(char *line, size_t len)
{
    for (size_t i = 0; i < len; i++)
        line[i] = random_byte();
    line[len] = '\0';
}

static bool points_into(const char *p, const char *buf, size_t size)
{
    return p >= buf && p < buf + size;
}

/* Check the parts of a parsed message. */
static void check_parts(const struct msg *m, const char *buf, size_t size, const char *input)
{
    if (m->command == NULL || m->command[0] == '\0' || !points_into(m->command, buf, size))
        FAIL("bad command for \"%s\"", input);
    if (m->prefix != NULL && !points_into(m->prefix, buf, size))
        FAIL("bad prefix for \"%s\"", input);
    if (m->trailing != NULL && !points_into(m->trailing, buf, size))
        FAIL("bad trailing for \"%s\"", input);
    if (m->nparams < 0 || m->nparams > MSG_PARAMS_MAX) {
        FAIL("nparams %d for \"%s\"", m->nparams, input);
        return;
    }
    for (int i = 0; i < m->nparams; i++) {
        const char *p = m->params[i];
        if (!points_into(p, buf, size) || p[0] == '\0' || p[0] == ':' || strchr(p, ' ') != NULL)
            FAIL("bad parameter %d for \"%s\"", i, input);
    }
}

static bool same(const char *a, const char *b)
{
    return a == NULL || b == NULL ? a == b : strcmp(a, b) == 0;
}

/* Parse a random line; if that works, format and parse it again. */
static void fuzz_parse(void)
{
    char input[MSG_LINE_MAX], line[MSG_LINE_MAX], out[MSG_LINE_MAX + 1];
    random_line(input, (size_t)random_below(400));
    str_copy(line, input, sizeof line);

    struct msg m;
    enum msg_status st = msg_parse(line, &m);
    if (st != MSG_OK) {
        if (st != MSG_EMPTY && st != MSG_NO_COMMAND && st != MSG_TOO_MANY_PARAMS)
            FAIL("status %d for \"%s\"", (int)st, input);
        return;
    }
    check_parts(&m, line, sizeof line, input);

    int len = msg_format(out, sizeof out, &m);
    if (len < 2 || out[len - 2] != '\r' || out[len - 1] != '\n') {
        FAIL("cannot format the parts of \"%s\"", input);
        return;
    }
    out[len - 2] = '\0';
    struct msg again;
    if (msg_parse(out, &again) != MSG_OK) {
        FAIL("\"%s\" became \"%s\", which does not parse", input, out);
        return;
    }
    /* Compare the parts. "NICK bob" and "NICK :bob" mean the same, so
     * compare with msg_param(), which counts the trailing one too. */
    int total = m.nparams + (m.trailing != NULL);
    int total_again = again.nparams + (again.trailing != NULL);
    bool ok = same(m.prefix, again.prefix) && same(m.command, again.command)
              && total == total_again;
    for (int i = 0; ok && i < total; i++)
        ok = same(msg_param(&m, i), msg_param(&again, i));
    if (!ok)
        FAIL("\"%s\" became \"%s\", which parses differently", input, out);
}

/* What the framer must do with one line of the given length and ending,
 * and whether it contains a NUL. */
static enum frame_status expected(size_t len, size_t ending, bool nul)
{
    if (len + ending > MSG_LINE_MAX)
        return FRAME_TOO_LONG;
    return nul ? FRAME_BAD : FRAME_LINE;
}

/* Build a stream of random lines (some too long, some with a NUL), feed it
 * to the framer in random pieces, and compare with what each line should
 * give. */
static void fuzz_frame(void)
{
    enum { LINES = 8 };
    static char stream[LINES * 700];
    static char lines[LINES][700];
    enum frame_status want[LINES];
    size_t len = 0;

    for (int i = 0; i < LINES; i++) {
        size_t n = (size_t)(random_below(3) == 0 ? random_below(700) : random_below(60));
        bool nul = n > 0 && random_below(8) == 0;
        size_t ending = (size_t)(1 + random_below(2));          /* "\n" or "\r\n" */
        random_line(lines[i], n);
        memcpy(stream + len, lines[i], n);
        if (nul)
            stream[len + (size_t)random_below((int)n)] = '\0';
        len += n;
        memcpy(stream + len, ending == 2 ? "\r\n" : "\n", ending);
        len += ending;
        want[i] = expected(n, ending, nul);
    }

    struct strbuf in = { 0 };
    bool skipping = false;
    char line[MSG_LINE_MAX + 1];
    int got = 0;
    for (size_t pos = 0; pos < len;) {
        size_t piece = (size_t)(1 + random_below(random_below(2) ? 4 : 600));
        if (piece > len - pos)
            piece = len - pos;
        if (sb_append(&in, stream + pos, piece) < 0) {
            FAIL("out of memory");
            break;
        }
        pos += piece;
        enum frame_status st;
        while ((st = msg_take_line(&in, &skipping, line)) != FRAME_NONE) {
            if (got >= LINES) {
                FAIL("more lines than were sent");
                break;
            }
            if (st != want[got])
                FAIL("line %d: status %d, expected %d", got, (int)st, (int)want[got]);
            else if (st == FRAME_LINE && strcmp(line, lines[got]) != 0)
                FAIL("line %d: got \"%s\", expected \"%s\"", got, line, lines[got]);
            got++;
        }
    }
    if (got != LINES)
        FAIL("%d lines out, %d in", got, LINES);
    sb_free(&in);
}

int main(int argc, char *argv[])
{
    long rounds = 20000, seed = 1;
    if ((argc > 1 && !str_to_long(argv[1], 1, 100000000, &rounds))
        || (argc > 2 && !str_to_long(argv[2], 0, 2147483647, &seed)) || argc > 3) {
        fprintf(stderr, "usage: fuzz [ROUNDS [SEED]]\n");
        return 2;
    }
    srand((unsigned)seed);
    for (long i = 0; i < rounds && failures < 10; i++) {
        fuzz_parse();
        fuzz_frame();
    }
    printf("fuzz: %ld rounds (seed %ld), %lu failed\n", rounds, seed, failures);
    return failures == 0 ? 0 : 1;
}
```

Design notes:

- **No second parser.** `fuzz_frame()` builds the stream, so it knows every expected line. `fuzz_parse()`
  checks properties: every part points into the line, and parse → format → parse is stable.
- **Random split points:** pieces of 1–4 bytes split lines across reads; up to 600 bytes put several lines
  in one read.
- **Biased bytes** (space, `:`, letters of real commands), or a valid message would almost never appear.
- `stream` and `lines` are `static`: 11 KB would fit on the stack, but needn't go there every round.
- It stops after 10 failures. `rand()` is fine: a seed only replays *with the same C library*.

In the `Makefile`, the fuzzer links only what it tests:

`project/ch21/Makefile` (part)
```make
FUZZ_OBJS   := $(addprefix $(BUILD)/, fuzz.o msg.o strbuf.o str.o)
```

```make
$(BUILD)/fuzz: $(FUZZ_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

test: all $(BUILD)/unit $(BUILD)/fuzz
	$(BUILD)/unit
	$(BUILD)/fuzz
```

and the comment at the top says `make test` now "build[s] and run[s] the unit tests and the fuzzer". The
existing pattern rule for `tests/%.c` compiles `fuzz.c`.

#### Checkpoint

```console
$ make -s test-all
unit: 30 passed, 0 failed
fuzz: 20000 rounds (seed 1), 0 failed
unit: 30 passed, 0 failed
fuzz: 20000 rounds (seed 1), 0 failed
$ make -s SANITIZE=1 test
unit: 30 passed, 0 failed
fuzz: 20000 rounds (seed 1), 0 failed
$ build/glibc-san/fuzz 200000 42
fuzz: 200000 rounds (seed 42), 0 failed
```

`make -s SANITIZE=1 LIBC=musl test` passes too, in trap mode. Now prove the fuzzer works: plant an off-by-one
in `msg_take_line()` in `src/msg.c`:

```c
        if (n > MSG_LINE_MAX + 1) {        /* wrong: allows one byte too many */
```

```console
$ make -s test-all
unit: 30 passed, 0 failed
fuzz: line 2: status 1, expected 2
fuzz: line 1: status 1, expected 2
fuzz: line 7: status 1, expected 2
fuzz: line 2: status 1, expected 2
fuzz: line 6: status 1, expected 2
fuzz: line 5: status 1, expected 2
fuzz: line 1: status 1, expected 2
fuzz: line 1: status 1, expected 2
fuzz: line 1: status 1, expected 2
fuzz: line 2: status 1, expected 2
fuzz: 20000 rounds (seed 1), 10 failed
make[1]: *** [Makefile:65: test] Error 1
make: *** [Makefile:70: test-all] Error 2
```

Status 1 is `FRAME_LINE`, 2 is `FRAME_TOO_LONG`: a line one byte too long got through. And **the unit tests
passed**: they test lines much too long, not the one-byte edge. Undo the change before going on.

### Step 7: the integration test

`tests/itest.c` is about 490 lines, mostly test scripts. Besides the usual POSIX headers it includes the
project's `clock.h`, `io.h`, `msg.h`, `net.h`, `strbuf.h` and `test.h`, reusing the server's framer and I/O
helpers. The machinery first:

`project/ch21/tests/itest.c` (part)
```c
/* itest.c - the integration test: a real parleyd, real TCP clients.
 *
 *     itest PATH-TO-PARLEYD [NAME...]
 *
 * Each test starts its own server with "-p 0", so the kernel picks a free
 * port (the server logs which one: we read it from the log file), then
 * connects scripted clients and checks every line they receive. Nothing
 * here is mocked: this is the server exactly as users run it.
 */
```

```c
#define TIMEOUT_MS 3000     /* the longest we wait for any expected line */

static const char *parleyd;  /* path of the server binary */
static int failed_checks;

void test_fail(const char *file, int line, const char *fmt, ...)
{
    printf("%s:%d: ", file, line);
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    putchar('\n');
    failed_checks++;
}
```

`test.h`'s `CHECK` macros call `test_fail()` ([Chapter 11](11-testing.md)), so they work here too. Starting
the server: `fork()`, `dup2()`, `execv()`, then wait for the log line:

```c
/* The server under test. */

struct server {
    pid_t pid;
    int port;
    char log[256];          /* its stderr goes to this file */
};

/* Find "listening on 127.0.0.1:PORT" in the log; 0 if not there yet. */
static int port_from_log(const char *path)
{
    char text[1024];
    int fd = open(path, O_RDONLY);
    if (fd < 0)
        return 0;
    ssize_t n = read(fd, text, sizeof text - 1);
    close(fd);
    if (n <= 0)
        return 0;
    text[n] = '\0';
    const char *p = strstr(text, "listening on 127.0.0.1:");
    return p == NULL ? 0 : atoi(p + strlen("listening on 127.0.0.1:"));
}

/* Start parleyd with the given extra options (a NULL-terminated list).
 * Returns false if it doesn't come up. */
static bool start_server(struct server *s, const char *const *options)
{
    const char *argv[16] = { parleyd, "-p", "0", "-T" };
    int argc = 4;
    for (int i = 0; options[i] != NULL && argc < 15; i++)
        argv[argc++] = options[i];
    argv[argc] = NULL;

    snprintf(s->log, sizeof s->log, "%s.itest.log", parleyd);
    int log_fd = open(s->log, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (log_fd < 0) {
        printf("itest: %s: %s\n", s->log, strerror(errno));
        return false;
    }
    fflush(stdout);                 /* or the child would print it again */
    s->pid = fork();
    if (s->pid < 0) {
        printf("itest: fork: %s\n", strerror(errno));
        close(log_fd);
        return false;
    }
    if (s->pid == 0) {              /* the child: become the server */
        dup2(log_fd, STDERR_FILENO);
        close(log_fd);
        /* execv() wants char *const argv[]; it does not change them. */
        execv(parleyd, (char *const *)argv);
        fprintf(stderr, "exec %s: %s\n", parleyd, strerror(errno));
        _exit(127);
    }
    close(log_fd);
    long long deadline = clock_ms() + TIMEOUT_MS;
    while ((s->port = port_from_log(s->log)) == 0 && clock_ms() < deadline)
        sleep_ms(10);
    if (s->port == 0) {
        printf("itest: the server did not start (see %s)\n", s->log);
        kill(s->pid, SIGKILL);
        waitpid(s->pid, NULL, 0);
        return false;
    }
    return true;
}

/* Stop the server with SIGTERM; returns its exit status (-1: crashed). */
static int stop_server(struct server *s)
{
    int status;
    kill(s->pid, SIGTERM);
    if (waitpid(s->pid, &status, 0) < 0)
        return -1;
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}
```

`sleep_ms()` is a small `nanosleep()` loop. The cast in `execv()` is a classic C wart: `execv()` predates
`const`, and C doesn't convert `const char **` to `char *const *` by itself.

A scripted client is a socket and a framer. `next_line()`, the heart of every check, waits with `poll()` and a
time limit, and tells apart a line, a clean end, a timeout and an error:

```c
/* A scripted client. */

struct client {
    int fd;
    struct strbuf in;
    bool skipping;
};
```

```c
/* The next line from the server into line[]: 1, or 0 at the end of the
 * connection, or -1 if none came within TIMEOUT_MS, or -2 on an error
 * (such as ECONNRESET: the server reset the connection). */
static int next_line(struct client *c, char line[MSG_LINE_MAX + 1])
{
    long long deadline = clock_ms() + TIMEOUT_MS;
    for (;;) {
        enum frame_status st = msg_take_line(&c->in, &c->skipping, line);
        if (st == FRAME_LINE)
            return 1;
        if (st != FRAME_NONE)
            continue;                           /* a bad line: skip it */
        long long left = deadline - clock_ms();
        struct pollfd p = { .fd = c->fd, .events = POLLIN };
        if (left <= 0 || poll(&p, 1, (int)left) <= 0)
            return -1;
        ssize_t n = read_into(c->fd, &c->in, 4096);
        if (n < 0 && errno == EINTR)
            continue;
        if (n < 0)
            return -2;
        if (n == 0)
            return 0;
    }
}

/* Check that the next line c receives is want. */
#define EXPECT(c, want) expect_line((c), (want), __FILE__, __LINE__)
static const char *what(int r)
{
    return r == 0 ? "the end of connection"
           : r == -1 ? "nothing (timeout)" : strerror(errno);
}

static void expect_line(struct client *c, const char *want, const char *file, int lineno)
{
    char line[MSG_LINE_MAX + 1];
    int r = next_line(c, line);
    if (r <= 0)
        test_fail(file, lineno, "expected \"%s\", got %s", want, what(r));
    else if (strcmp(line, want) != 0)
        test_fail(file, lineno, "expected \"%s\", got \"%s\"", want, line);
}

/* Check that the server closes c's connection, cleanly (no reset) and
 * with no more lines. */
#define EXPECT_CLOSED(c) expect_closed((c), __FILE__, __LINE__)
static void expect_closed(struct client *c, const char *file, int lineno)
{
    char line[MSG_LINE_MAX + 1];
    int r = next_line(c, line);
    if (r != 0)
        test_fail(file, lineno, "expected the end of connection, got %s",
                  r == 1 ? line : what(r));
}
```

The macros pass `__FILE__` and `__LINE__` so a failure points at the test script's line, as `CHECK` does.
Copy the rest from `project/ch21/tests/itest.c`: `sleep_ms()`, `connect_client()`, `close_client()`,
`send_text()`, `join()` (connect, send `NICK`, expect the four welcome lines) and `START()` (start a server or
fail the test).

Now a test reads like a script. Flood control with a graceful close:

```c
static void test_flood(void)
{
    struct server s;
    struct client eve, frank;
    char text[30 * 160], echo[200];
    START(&s, "-f", "5", NULL);
    join(&frank, s.port, "frank", "frank", NULL);
    join(&eve, s.port, "eve", "frank eve", (struct client *[]){ &frank, NULL });

    /* eve sends 30 lines of 150 x's at once: more than the server reads
     * in one go (4096 bytes). NICK used one line of her 5; 4 more are
     * fine, the 5th is not. Then the server closes with eve's input still
     * unread: she must get every line up to the ERROR, and a clean end
     * (no reset, see linger() in server.c). */
    size_t len = 0;
    for (int i = 0; i < 30; i++) {
        memcpy(text + len, "MSG :", 5);
        memset(text + len + 5, 'x', 150);
        memcpy(text + len + 155, "\r\n", 2);
        len += 157;
    }
    if (write_all(eve.fd, text, len) < 0)
        failed_checks++;
    snprintf(echo, sizeof echo, ":eve MSG :%.150s", text + 5);
    for (int i = 0; i < 4; i++) {
        EXPECT(&eve, echo);
        EXPECT(&frank, echo);
    }
    EXPECT(&eve, "ERROR :Excess flood");
    EXPECT_CLOSED(&eve);
    EXPECT(&frank, ":eve QUIT :Excess flood");
    close_client(&eve);
    close_client(&frank);
    CHECK_INT(stop_server(&s), 0);
}
```

`%.150s` prints at most 150 bytes. The other six tests are in the snapshot: `chat` (two users talk, rename,
quit), `one_byte_at_a_time` (each byte its own TCP segment), `bad_input` (too long, NUL, unknown command, bad
nick, malformed), `slow_reader` (a client that never reads is dropped when its output queue is full),
`timeouts` (`-t 1`: registration and PING timeouts) and `full_and_shutdown` (`-m 2`, then SIGTERM).

`main()` runs all tests, or the ones named on the command line:

```c
static const struct {
    const char *name;
    void (*run)(void);
} tests[] = {
    { "chat", test_chat },
    { "one_byte_at_a_time", test_one_byte_at_a_time },
    { "bad_input", test_bad_input },
    { "flood", test_flood },
    { "slow_reader", test_slow_reader },
    { "timeouts", test_timeouts },
    { "full_and_shutdown", test_full_and_shutdown },
};

int main(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr, "usage: itest PATH-TO-PARLEYD [NAME...]\n");
        return 2;
    }
    parleyd = argv[1];
    /* A server that dies would make our writes raise SIGPIPE. */
    signal(SIGPIPE, SIG_IGN);

    int passed = 0, failed = 0;
    for (size_t i = 0; i < sizeof tests / sizeof tests[0]; i++) {
        bool selected = argc == 2;
        for (int j = 2; j < argc; j++)
            selected = selected || strcmp(argv[j], tests[i].name) == 0;
        if (!selected)
            continue;
        failed_checks = 0;
        tests[i].run();
        if (failed_checks == 0) {
            passed++;
        } else {
            failed++;
            printf("FAIL %s (server log: %s.itest.log)\n", tests[i].name, parleyd);
        }
        fflush(stdout);
    }
    printf("itest: %d passed, %d failed\n", passed, failed);
    return failed == 0 ? 0 : 1;
}
```

In the `Makefile`:

```make
ITEST_OBJS  := $(addprefix $(BUILD)/, itest.o clock.o io.o net.o msg.o strbuf.o str.o)
```

```make
$(BUILD)/itest: $(ITEST_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

test: all $(BUILD)/unit $(BUILD)/fuzz $(BUILD)/itest
	$(BUILD)/unit
	$(BUILD)/fuzz
	$(BUILD)/itest $(BUILD)/parleyd
```

and the comment at the top becomes:

```make
#   make test            build and run the unit tests, the fuzzer and the
#                        integration test
```

It tests the server it was built with: `make LIBC=musl test` the musl server, `make SANITIZE=1 test` the
server under ASan and UBSan. A plain `make` builds only the two programs.

What does it catch? Run it against step 4's server, without `CONN_LINGER`:

```console
$ build/glibc/itest ../step4/build/glibc/parleyd flood
tests/itest.c:365: expected the end of connection, got Connection reset by peer
FAIL flood (server log: ../step4/build/glibc/parleyd.itest.log)
itest: 0 passed, 1 failed
```

(`../step4` is wherever you kept a copy of that step.) The bug from the start of step 5, caught every time.

### Checkpoint

The suite on both C libraries, under the sanitizers, and a longer fuzz run:

```console
$ make -s test-all
unit: 30 passed, 0 failed
fuzz: 20000 rounds (seed 1), 0 failed
itest: 7 passed, 0 failed
unit: 30 passed, 0 failed
fuzz: 20000 rounds (seed 1), 0 failed
itest: 7 passed, 0 failed
$ make -s SANITIZE=1 test
unit: 30 passed, 0 failed
fuzz: 20000 rounds (seed 1), 0 failed
itest: 7 passed, 0 failed
$ build/glibc-san/fuzz 200000 42; echo "exit $?"
fuzz: 200000 rounds (seed 42), 0 failed
exit 0
$ build/musl/itest build/musl/parleyd one_byte_at_a_time flood; echo "exit $?"
itest: 2 passed, 0 failed
exit 0
```

Then by hand: flood control and NUL bytes in one server run.

```console
$ build/glibc/parleyd -p 6667 -T 2>server.log & server=$!; sleep 0.2
$ { echo 'NICK spam'; yes 'MSG :spam' | head -n 30; } | nc -C 127.0.0.1 6667 | uniq -c
      1 :parley 001 spam :Welcome to parley, spam
      1 :spam JOIN
      1 :parley 353 spam :spam
      1 :parley 366 spam :End of NAMES
     19 :spam MSG :spam
      1 ERROR :Excess flood
$ printf 'NICK nul\r\nMSG :a\0b\r\nQUIT\r\n' | nc 127.0.0.1 6667
:parley 001 nul :Welcome to parley, nul
:nul JOIN
:parley 353 nul :nul
:parley 366 nul :End of NAMES
:parley 400 nul :Line contains a NUL byte
ERROR :Goodbye
$ printf 'hi\0there\nbye\n' | build/musl/parley -p 6667 -n nully; echo "exit $?"
-- line contains a NUL byte, not sent
-- Welcome to parley, nully
--> nully joined
-- Users: nully
<nully> bye
exit 0
$ kill $server; wait $server; cat server.log
parleyd: info: listening on 127.0.0.1:6667
parleyd: info: spam joined
parleyd: info: spam left (Excess flood)
parleyd: info: nul joined
parleyd: info: nul left (Quit)
parleyd: info: nully joined
parleyd: info: nully left (Quit)
parleyd: info: got signal 15 (Terminated), shutting down
```

The 30-line flood without `QUIT` now gives the `ERROR` every time. The descriptor limit, with the musl server:

```console
$ (ulimit -n 12; exec build/musl/parleyd -p 6667 -T) & server=$!; sleep 0.2
parleyd: info: listening on 127.0.0.1:6667
parleyd: warning: the open file limit (12) allows only 5 clients
$ exec 5<>/dev/tcp/127.0.0.1/6667 6<>/dev/tcp/127.0.0.1/6667 7<>/dev/tcp/127.0.0.1/6667 8<>/dev/tcp/127.0.0.1/6667 9<>/dev/tcp/127.0.0.1/6667
$ nc -d 127.0.0.1 6667
parleyd: warning: accept: No file descriptors available: refused a client
ERROR :Server full
$ exec 5>&- 6>&- 7>&- 8>&- 9>&-; kill $server; wait $server; echo "exit $?"
parleyd: info: got signal 15 (Terminated), shutting down
exit 0
```

Step 4's `strace` test gives the same two traces as in "glibc vs musl: running out of descriptors in the
server". Clean up with `rm -f *.trace server.log` and `make clean`.

The complete project at this point is in `project/ch21/`.

## Summary

- Every network byte is hostile: bound every size and count, and decide what happens when a client sends too
  much, too fast, or never stops.
- Reject lines containing a NUL (`memchr()` with the length), or parts of the program see different lines.
- A **penalty clock** limits floods: each line costs time, the clock never lags "now", a client too far ahead
  is cut off. `parleyd -f LINES` sets the burst.
- `RLIMIT_NOFILE` (`ulimit -n`, `getrlimit()`, XSI) limits descriptors. Fit `-m` to it, and keep a **spare
  descriptor** so EMFILE from `accept()` can't make a `poll()` server spin.
- Closing with unread input sends **RST**, losing your last lines. Close gracefully: `shutdown(SHUT_WR)`, then
  discard input until the peer closes, with a time limit (`CONN_LINGER`).
- Escape control bytes before hostile text reaches a log. Log events, not lines.
- A **fuzzer** checks properties on random input, with a printed seed for replay, under ASan and UBSan. It
  found an off-by-one the unit tests missed.
- An **integration test** runs the real binary: `fork()`/`execv()`, `-p 0`, a time limit on every wait.
- glibc vs musl: the kernel behaves the same. `strerror()` texts differ, so compare `errno`. `rand()`
  differs, so seeds replay only on the same libc (use xorshift32 if that matters). glibc traces show
  `write()`/`openat()`, musl's `writev()`/`open()`: trace by purpose. Full sanitizers need glibc; musl gets
  UBSan in trap mode.

## Exercises

1. **A fuzzer that replays everywhere.** Make `examples/ch21/fuzzme.c` use xorshift32 (from `rng.c`) instead
   of `rand()`, check that glibc and musl builds report the same round, then fix the bug in `parse_nick()`.

   <details><summary>Solution</summary>

   Add `#include <stdint.h>`, a generator, and replace each `rand() % n` with `random_below(n)`:

   ```c
   static uint32_t rng_state = 1;

   /* xorshift32: the same numbers with every C library. */
   static int random_below(int n)
   {
       uint32_t x = rng_state;
       x ^= x << 13;
       x ^= x >> 17;
       x ^= x << 5;
       rng_state = x;
       return (int)(x % (uint32_t)n);
   }
   ```

   In `main()`, `srand(seed)` becomes:

   ```c
       rng_state = seed != 0 ? seed : 1;   /* xorshift32 must not start at 0 */
   ```

   ```console
   $ gcc -std=c99 -Wall -Wextra -pedantic -o fuzzme fuzzme.c && ./fuzzme
   round 1313 (seed 1): "NICK babaabbaaa  b" gave a 10-byte name
   $ musl-gcc -std=c99 -Wall -Wextra -pedantic -static -o fuzzme fuzzme.c && ./fuzzme
   round 1313 (seed 1): "NICK babaabbaaa  b" gave a 10-byte name
   $ ./fuzzme 100000 7
   round 1332 (seed 7): "NICK ababababba" gave a 10-byte name
   ```

   (`% n` slightly favours small numbers when `n` doesn't divide 2³²; a fuzzer doesn't care.) The bug is the
   `+ 1` in `n > NAME_LEN + 1`: a 10-byte name passes and its `'\0'` overflows the 10-byte buffer. With
   `n > NAME_LEN` both builds print `100000 rounds (seed 1): no bug found`.

   </details>

2. **Escape the QUIT reason.** `room_part()` logs a `QUIT` reason as-is: try
   `printf 'NICK eve\r\nQUIT :bye\033[2J\r\n' | nc 127.0.0.1 6667` and `cat -v` the log. Escape control
   bytes (below 0x20, and 0x7f) as `\xHH` in the log, as `logsafe.c` does; other users still get the reason
   unchanged.

   <details><summary>Solution</summary>

   Before the change, `cat -v server.log` shows `eve left (bye^[[2J)`. Add a helper to `src/room.c`, used only
   for the log line:

   ```c
   /* Copy src into dst[size], writing control bytes as \xHH: a client's
    * text must not be able to change what the log (or a terminal) shows. */
   static void escape(char *dst, size_t size, const char *src)
   {
       size_t len = 0;
       for (; *src != '\0'; src++) {
           unsigned char ch = (unsigned char)*src;
           char tmp[5];
           int n = ch < 0x20 || ch == 0x7f ? snprintf(tmp, sizeof tmp, "\\x%02x", ch)
                                           : snprintf(tmp, sizeof tmp, "%c", ch);
           if (len + (size_t)n >= size)
               break;
           for (int i = 0; i < n; i++)
               dst[len++] = tmp[i];
       }
       dst[len] = '\0';
   }

   void room_part(struct room *r, struct conn *c, const char *reason)
   {
       if (!registered(c))
           return;
       char safe[4 * MSG_LINE_MAX + 1];
       escape(safe, sizeof safe, reason);
       log_msg(LOG_INFO, "%s left (%s)", c->nick, safe);
       broadcast(r, c, &(struct msg){ .prefix = c->nick, .command = "QUIT", .trailing = reason });
       c->nick[0] = '\0';
   }
   ```

   Each byte can grow to 4, hence the buffer size. The `unsigned char` cast matters: where `char` is signed,
   bytes above 127 would be negative and count as control bytes.

   ```console
   $ make -s test-all
   unit: 30 passed, 0 failed
   fuzz: 20000 rounds (seed 1), 0 failed
   itest: 7 passed, 0 failed
   unit: 30 passed, 0 failed
   fuzz: 20000 rounds (seed 1), 0 failed
   itest: 7 passed, 0 failed
   $ build/musl/parleyd -p 6667 -T 2>server.log & server=$!; sleep 0.2
   $ printf 'NICK eve\r\nQUIT :bye\033[2J\r\n' | nc 127.0.0.1 6667 >/dev/null
   $ kill $server; wait $server; cat -v server.log
   parleyd: info: listening on 127.0.0.1:6667
   parleyd: info: eve joined
   parleyd: info: eve left (bye\x1b[2J)
   parleyd: info: got signal 15 (Terminated), shutting down
   ```

   </details>

3. **Hunt for descriptor leaks.** One forgotten `close()` per client kills a server after a few thousand
   clients. With `/proc/PID/fd`, show that `parleyd` has as many descriptors after 50 clients that register
   and quit plus 20 that connect and close at once as before.

   <details><summary>Solution</summary>

   ```console
   $ build/glibc/parleyd -p 6667 -T 2>server.log & server=$!; sleep 0.2
   $ ls /proc/$server/fd | wc -l
   7
   $ for i in $(seq 50); do printf "NICK u$i\r\nQUIT\r\n" | nc 127.0.0.1 6667 >/dev/null; done
   $ for i in $(seq 20); do exec 3<>/dev/tcp/127.0.0.1/6667; exec 3>&-; done; sleep 0.2
   $ ls /proc/$server/fd | wc -l
   7
   $ kill $server; wait $server
   ```

   Seven is `FDS_RESERVED`: no client connected, nothing leaked. `sleep 0.2` lets the server notice the
   last closes. (Such a test could go into `itest.c`, but `/proc` is Linux-only; `lsof -p PID` works on
   more systems.)

   </details>

4. **Check the server's numbers.** Set `BURST` in `examples/ch21/penalty.c` to 20 (the default `-f`) and
   send 25 lines at t=0 and 25 at t=2000 ms. How many get through each time? Explain the second number.

   <details><summary>Solution</summary>

   ```c
   #define BURST 20      /* parleyd -f 20 */
   ```

   ```c
   int main(void)
   {
       int ok_at_0 = 0, ok_at_2000 = 0;
       for (int i = 0; i < 25; i++)
           ok_at_0 += allow(0);
       for (int i = 0; i < 25; i++)
           ok_at_2000 += allow(2000);
       printf("t=0: %d lines ok; t=2000: %d more\n", ok_at_0, ok_at_2000);
       return 0;
   }
   ```

   ```console
   $ gcc -std=c99 -Wall -Wextra -pedantic -o penalty penalty.c && ./penalty
   t=0: 20 lines ok; t=2000: 10 more
   ```

   After 20 lines the clock is at 4000 ms. At t=2000 it is 2000 ms ahead, leaving 2000 ms of room: 10 lines
   at 200 ms each. That is "5 lines/s on average": 2 seconds bought back 10 lines. (`allow()` returns
   `bool`, which adds as 1 for `true`. The musl build prints the same.)

   </details>

Sources for this chapter: [REFERENCES.md](REFERENCES.md#chapter-21--robustness)
