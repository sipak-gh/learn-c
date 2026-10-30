# Learn C by Building a Chat Server

This course teaches the C language from first principles, and then the Linux systems programming that real C
programs are made of: files and descriptors, signals, sockets, threads, event loops and terminals. In it you
build **parley**, a small IRC-style chat with one room. It has two parts:

- `parleyd`, a server that many people can connect to over TCP;
- `parley`, a client with a split-screen terminal UI.

You write all of it yourself, using nothing but the C library:
- dynamic buffers and line framing;
- the message parser and the client table;
- a unit-test framework and an integration-test harness;
- the event loop and the terminal UI.

Each chapter explains a part of C and then uses it to grow the project. Chapter 1 has two programs that print a
banner. Chapter 22 ships a hardened, tested, benchmarked v1.0.

The course also compares two C libraries throughout. Every program is built and run twice: with **glibc**,
the default C library on most Linux distributions, and with **musl**, a small, strict one built for static
linking. Where the two behave differently, the chapters show the real output of both in sections titled
"glibc vs musl". The differences include error message texts, stdio buffering, assert messages, thread stack
sizes, system calls, binary sizes and speed.

## What you will build

The finished client is used in a terminal. Here, alice's screen (10×50) is shown after a few messages:

```
+--------------------------------------------------+
|                                                  |
|-- type /help for help, /quit or Ctrl-D to leave  |
|[hh:mm] -- Welcome to parley, alice               |
|[hh:mm] --> alice joined                          |
|[hh:mm] -- Users: bob alice                       |
|[hh:mm] <alice> hello bob                         |
|[hh:mm] <bob> hi alice                            |
|[hh:mm] <bob> still there?                        |
| parley | alice @ localhost:6620                  |
|> typing in progre                                |
+--------------------------------------------------+
```

When its input isn't a terminal, the client switches to plain lines, so it can be scripted. Here alice sits in
the room while bob drops by. The `build/glibc/` and `build/musl/` binaries talk to each other:

```console
$ build/glibc/parleyd -p 6619 -T 2>server.log &
$ mkfifo a; build/glibc/parley -p 6619 -n alice <a >alice.txt & exec 3>a
$ printf 'hi alice\n' | build/musl/parley -p 6619 -n Alice; echo "exit $?"
!! Alice: Nickname is already in use
!! You have not registered
exit 0
$ printf 'hi alice\n' | build/musl/parley -p 6619 -n bob >/dev/null; cat alice.txt
-- Welcome to parley, alice
--> alice joined
-- Users: alice
--> bob joined
<bob> hi alice
<-- bob left (Quit)
```

The protocol is plain text lines, so you can also talk to the server with `nc`:

```console
$ nc -C 127.0.0.1 6667
NICK carol
:parley 001 carol :Welcome to parley, carol
:carol JOIN
:parley 353 carol :carol
:parley 366 carol :End of NAMES
MSG :hello, world
:carol MSG :hello, world
```

## Requirements

The course states versions and doesn't cover installing them. Use your distribution's packages.

| What | Version | Notes |
|---|---|---|
| Language | **C99**, strictly | Every file compiles with `-std=c99 -Wall -Wextra -pedantic` (project: plus `-Werror`). C11/C17/C23 are mentioned, never used. |
| System interfaces | **POSIX.1-2008** | `_POSIX_C_SOURCE=200809L`. No libraries except the C library. |
| Platform | Linux, x86-64 | Other Unix-like systems need small changes (see Chapter 22). |
| Compiler | gcc 11 or newer (verified with 11.4), or clang | |
| Build tool | GNU make | Used from Chapter 9. |
| C library 1 | **glibc 2.35** or newer | The default on most distributions (verified with 2.35). |
| C library 2 | **musl 1.2.x**, used through the `musl-gcc` wrapper | Verified with musl 1.2.6. Usually packaged as `musl-tools` or `musl-dev`. |
| Debugging | gdb, strace | Chapters 10 and up. |
| Networking | `nc` (OpenBSD netcat), `ss` | Used to poke the server by hand. |

## How to use this course

Read the chapters in order. Each one has the same structure:
1. Concept sections with short, complete examples.
2. A project step, "Building parley: …".
3. A checkpoint with the exact commands and output you should see.
4. A summary.
5. Exercises with solutions.

- **`examples/chNN/`** holds every complete example from chapter NN, ready to compile and run as shown in the
  chapter.
- **`project/chNN/`** is the complete project at the *end* of chapter NN. It exists for every chapter that
  changes the project, and `project/ch22/` is the finished program. Type the code yourself, and use the
  snapshot to compare with your work or to catch up.

Building with both C libraries:

- **Chapters 1–8**: one source file per program, compiled by hand:

  ```console
  $ gcc -std=c99 -Wall -Wextra -pedantic -o parleyd parleyd.c
  $ musl-gcc -std=c99 -Wall -Wextra -pedantic -static -o parleyd-musl parleyd.c
  ```

- **Chapters 9–22**: a Makefile, with sources in `src/` and binaries in `build/<libc>/`:

  ```console
  $ make                  # glibc: build/glibc/parleyd, build/glibc/parley
  $ make LIBC=musl        # musl, static: build/musl/parleyd, build/musl/parley
  $ make test             # unit tests (from Chapter 11), fuzzer and integration tests (Chapter 21)
  $ make test-all         # the tests with glibc and with musl
  $ make SANITIZE=1 test  # the tests under AddressSanitizer + UndefinedBehaviorSanitizer (Chapter 10)
  $ make clean
  ```

The server listens on port 6667 by default (`-p PORT` changes it) and only on 127.0.0.1, so everything stays on
your machine.

## Table of contents

### Part I — The C Language

| Chapter | Title | parley milestone |
|---|---|---|
| [1](01-getting-started.md) | Getting Started | `parleyd` and `parley` print a banner; built with glibc and with musl |
| [2](02-types-and-operators.md) | Types and Operators | Parse and range-check the port number |
| [3](03-control-flow.md) | Control Flow | Hand-written command-line option parsing for both programs |
| [4](04-functions.md) | Functions | Split into functions; a variadic logging function |
| [5](05-pointers-and-arrays.md) | Pointers and Arrays | The client turns typed lines and `/commands` into protocol lines (a dispatch table of function pointers) |
| [6](06-strings.md) | Strings | The server handles a session on stdin: registration, nickname rules, errors |
| [7](07-structs-unions-enums.md) | Structs, Unions and Enums | `struct msg` parser and formatter, a command table, NAMES/PING/ME |
| [8](08-memory.md) | Memory | A growable string buffer and robust line framing |
| [9](09-preprocessor-and-building.md) | The Preprocessor and Building | Modules in `src/`, an X-macro command table, a Makefile for glibc and musl |
| [10](10-errors-and-debugging.md) | Errors and Debugging | Error checking everywhere, `die()`, assertions, sanitizer builds |
| [11](11-testing.md) | Testing | Our own unit-test framework and `make test` / `make test-all` |

### Part II — Systems and Networking

| Chapter | Title | parley milestone |
|---|---|---|
| [12](12-files-and-descriptors.md) | Files and Descriptors | `read`/`write` with short-I/O helpers (`write_all`, `read_exact`) |
| [13](13-processes-and-signals.md) | Processes and Signals | Clean shutdown on SIGINT/SIGTERM; SIGPIPE ignored |
| [14](14-networking-basics.md) | Networking Basics | The server resolves and listens on a TCP port (IPv4 and IPv6) |
| [15](15-a-first-server.md) | A First Server | Server v1: blocking, one client at a time, spoken to with `nc` |
| [16](16-a-first-client.md) | A First Client | Client v1: connects, sends lines, shows replies |
| [17](17-threads.md) | Threads | Server v2: a thread per client and a real multi-user room |
| [18](18-event-loop.md) | The Event Loop | Server v3: one thread, `poll()`, output queues, timeouts, slow-reader eviction |
| [19](19-an-interactive-client.md) | An Interactive Client | Client v2: `poll()` on keyboard and socket, nicely rendered chat |
| [20](20-terminal-ui.md) | A Terminal UI | Client v3: raw mode, split screen, line editing, resizing |
| [21](21-robustness.md) | Robustness | Flood control, resource limits, graceful close; fuzz and integration tests |
| [22](22-shipping-and-libc-choices.md) | Shipping, and Choosing a libc | v1.0: getopt, release builds, `make dist`/`install`, a benchmark; glibc vs musl wrap-up |

### Appendices

- [Appendix A — C99 Cheat Sheet](appendix-a-c99-cheatsheet.md): syntax, operator precedence, format specifiers
  and common headers.
- [Appendix B — glibc vs musl](appendix-b-glibc-vs-musl.md): every difference shown in the course, in one table.
- [Appendix C — The parley Protocol](appendix-c-parley-protocol.md): the full protocol specification, compared
  with real IRC.
- [Appendix D — POSIX Sockets Reference](appendix-d-posix-sockets-reference.md): the socket, poll, termios and
  signal APIs used, with their signatures and errors.

References: [REFERENCES.md](REFERENCES.md) lists the sources behind each chapter, with archived copies
(Wayback Machine / Software Heritage) of every link.
