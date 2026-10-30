# parley 1.0

A small chat server (`parleyd`) and client (`parley`) in C99 and POSIX, with
nothing but the C library. One room, nicknames, messages, actions; a
line-based protocol modelled on IRC that you can also speak with `nc`.

    $ parleyd &
    2025-10-30 14:02:11 parleyd: info: listening on 127.0.0.1:6667
    $ parley -n alice

## Building

Needs a C99 compiler, GNU make, and glibc or musl (`musl-gcc`). Linux is
the tested system; the code uses POSIX.1-2008 interfaces only.

    make                     # build/glibc/parleyd, build/glibc/parley
    make LIBC=musl           # static binaries in build/musl/
    make test                # unit tests, fuzzer, integration test
    make test-all            # the tests with glibc and with musl
    make SANITIZE=1 test     # the tests under ASan and UBSan
    make release             # optimized, stripped: build/glibc-release/
    make release LIBC=musl   # the same, static with musl: build/musl-release/
    make install PREFIX=/usr/local DESTDIR=   # release binaries into PREFIX/bin
    make dist                # parley-1.0.tar.gz
    make bench               # build/glibc/bench, see "Benchmark" below

## Running the server

    parleyd [-p PORT] [-b ADDR] [-m MAXCLIENTS] [-t IDLE] [-f LINES] [-v] [-T]

| Option | Meaning | Default |
|---|---|---|
| `-p PORT` | TCP port; 0: any free port (the log says which) | 6667 |
| `-b ADDR` | address to listen on | 127.0.0.1 |
| `-m MAXCLIENTS` | most clients at once, 1–1000 (lowered to fit `ulimit -n`) | 64 |
| `-t IDLE` | PING clients idle for IDLE seconds, drop them after twice that; 0: never | 120 |
| `-f LINES` | drop clients sending more than LINES lines at once (5 lines/s on average); 0: no limit | 20 |
| `-v` | debug logging | off |
| `-T` | no timestamps in the log | off |

The log goes to stderr. SIGINT or SIGTERM shut the server down cleanly:
every client gets `ERROR :Server shutting down`, and the exit status is 0.

## Running the client

    parley [-h HOST] [-p PORT] [-n NICK] [-L]

On a terminal, `parley` shows a split screen: messages at the top, a status
bar, and an input line (Left/Right, Home/End, Backspace, Delete, Ctrl-A,
Ctrl-E, Ctrl-U). Ctrl-C, or Ctrl-D on an empty line, quits. With `-L`, or
when stdin or stdout is not a terminal, it reads and prints plain lines, so
it can be scripted:

    printf 'hello\n/me waves\n' | parley -n bot

Commands: `/nick NEWNICK`, `/me ACTION`, `/who`, `/quit [REASON]`, `/help`.

## The protocol

Lines of at most 512 bytes including the `\r\n` (a bare `\n` is accepted).
`[:prefix] COMMAND [params...] [:trailing]`, parameters separated by one
space. Nicknames: 1–16 characters, a letter, then letters, digits, `_`, `-`;
compared without regard to case.

| Client sends | Server answers |
|---|---|
| `NICK name` | first time: `001`, then `:name JOIN` to all, `353`, `366`; later: `:old NICK new` to all |
| `MSG :text` | `:name MSG :text` to everyone, the sender too |
| `ME :action` | `:name ME :action` to everyone |
| `NAMES` | `353 name :nick1 nick2 ...`, `366 name :End of NAMES` |
| `PING :token` | `PONG :token` |
| `PONG :token` | (answers the server's `PING :parley`) |
| `QUIT [:reason]` | `ERROR :Goodbye`, then `:name QUIT :reason` to the others |

Errors: `400` malformed message or NUL byte, `412` no text, `417` line too
long, `421` unknown command, `431` no nickname, `432` erroneous nickname,
`433` nickname in use, `451` not registered, `461` not enough parameters.
The server closes connections with `ERROR :reason`: `Server full`,
`Excess flood`, `Ping timeout`, `Registration timeout`, `Server shutting
down`. A client that does not read is dropped without an `ERROR` once 64 KiB
are queued for it (its queue is full); the others see
`:name QUIT :Output queue full`.

## Benchmark

`bench PORT [CLIENTS [ROUNDS]]` connects CLIENTS clients (default 50); in
each round every client sends one message and waits for all of them. Run
the server without the flood limit:

    build/glibc-release/parleyd -p 6600 -f 0 -T &
    build/glibc/bench 6600 50 2000

## Source layout

| File | What it does |
|---|---|
| `src/parleyd.c` | server main: options, resource limits, signals |
| `src/server.c` | the poll() event loop: accept, read, write, timers, shutdown |
| `src/room.c` | who is in the room; the command handlers |
| `src/conn.c` | one client connection and its output queue |
| `src/parley.c` | client main: options, poll() loop, commands |
| `src/ui.c`, `src/term.c` | the split-screen interface; raw mode |
| `src/msg.c` | parsing, formatting and framing protocol lines |
| `src/nick.c` | nickname rules |
| `src/net.c` | sockets: listen, connect, addresses |
| `src/io.c` | write_all, read_exact, read_into, non-blocking mode |
| `src/strbuf.c` | a growable byte buffer |
| `src/str.c` | safe string copy, number parsing |
| `src/sig.c` | signal handling with a self-pipe |
| `src/log.c`, `src/clock.c` | logging; a monotonic millisecond clock |
| `tests/` | `unit` (unit tests), `fuzz`, `itest` (integration), `bench` |
