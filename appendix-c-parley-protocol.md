# Appendix C — The parley Protocol

This is the complete specification of the protocol that `parleyd` and `parley` speak, exactly as `project/ch22` implements it. It is a small, line-based protocol modelled on IRC, so you can speak it by hand with `nc`. The second half compares it with real IRC (RFC 1459, RFC 2812 and the modern IRCv3 documents): what parley keeps, what it simplifies and what it drops. The server first speaks it on stdin and stdout in [Chapter 6](06-strings.md). The parser arrives in [Chapter 7](07-structs-unions-enums.md) and the framing in [Chapter 8](08-memory.md). The protocol goes on the network in [Chapter 15](15-a-first-server.md), serves many clients in [Chapter 17](17-threads.md) and [Chapter 18](18-event-loop.md), and is hardened in [Chapter 21](21-robustness.md).

Every transcript below is real. It was run against both the glibc build and the static musl build of `parleyd` on ports 6990–6999, and the two produced identical protocol output. The protocol has no libc-dependent behaviour; only the client port numbers in the log differ.

## Overview

| Property | Value |
|---|---|
| Transport | TCP, one connection per client |
| Default address | `127.0.0.1:6667` (`parleyd -b ADDR -p PORT`) |
| Encoding | bytes; text is passed through unchanged (UTF-8 works) |
| Unit | one line = one message, ending in `\r\n` |
| Rooms | exactly one; every registered client is in it |
| Identity | a nickname, chosen with `NICK` |
| Server name | `parley` (the prefix of messages from the server) |

A session has four steps:

1. Connect.
2. Register with `NICK name`.
3. Talk with `MSG`, `ME` and `NAMES`; keep the link alive with `PING`/`PONG`.
4. Leave with `QUIT`, or be disconnected with `ERROR :reason`.

## Message format

```text
message  = [ ":" prefix SPACE ] command *( SPACE middle ) [ SPACE ":" trailing ] CRLF
prefix   = 1*( any byte except SPACE, NUL, CR, LF )
command  = 1*( any byte except SPACE, NUL, CR, LF )
middle   = 1*( any byte except SPACE, NUL, CR, LF ), not starting with ":"
trailing = *( any byte except NUL, CR, LF )         ; may be empty, may contain spaces
SPACE    = 1*" "                                    ; one or more spaces
CRLF     = "\r\n"                                   ; receivers also accept "\n"
```

This is the grammar of RFC 1459 §2.3.1, with a smaller limit on middle parameters. Examples of how `msg_parse` splits a line:

| Line | prefix | command | middle params | trailing |
|---|---|---|---|---|
| `NICK alice` | — | `NICK` | `alice` | — |
| `NICK :alice` | — | `NICK` | — | `alice` |
| `MSG :hello, world` | — | `MSG` | — | `hello, world` |
| `MSG hello world` | — | `MSG` | `hello`, `world` | — |
| `:parley 433 * bob :Nickname is already in use` | `parley` | `433` | `*`, `bob` | `Nickname is already in use` |
| `:alice NICK Alice` | `alice` | `NICK` | `Alice` | — |
| `  msg    :  spaced   out` | — | `msg` | — | `  spaced   out` |

Parsing rules, all checked against `msg.c` and the transcripts below:

| Rule | Detail |
|---|---|
| Separators | One or more spaces (0x20). Leading spaces are skipped. A tab is *not* a separator; it is part of a word. |
| Empty lines | A line that is empty or only spaces is ignored: no reply. |
| Prefix from a client | Parsed, then ignored. The server always uses the sender's own nickname. |
| Bad prefix | `:` alone, `:name` with nothing after it, or an empty prefix (`: MSG`): `400 Malformed message`. |
| Command case | Case-insensitive: `names`, `Names` and `NAMES` are the same. |
| Middle parameters | At most 8. A 9th middle parameter gives `400 Malformed message`. The trailing parameter does not count. |
| Trailing parameter | Starts at the first parameter that begins with `:`; runs to the end of the line, spaces and all. May be empty. |
| Parameter numbering | The trailing parameter counts as the next one after the middles. `NICK bob` and `NICK :bob` both have parameter 0 `bob`. |
| Extra parameters | Ignored. `MSG hello world` sends only `hello`; `PING a b` answers `PONG :a`. |

Senders should always put free text in the trailing parameter (`MSG :hello world`). The `parley` client always does.

## Limits

| Limit | Value | Where | What happens when exceeded |
|---|---|---|---|
| Line length | 512 bytes, including `\r\n` | `MSG_LINE_MAX` in `msg.h` | Incoming: `417 Input line too long`, line dropped. Outgoing: the trailing parameter is cut to fit. |
| Middle parameters | 8 | `MSG_PARAMS_MAX` in `msg.h` | `400 Malformed message` |
| Nickname | 1–16 bytes | `NICK_MAX` in `nick.h` | `432 Erroneous nickname` |
| Client input line | 400 bytes | `INPUT_MAX` in `parley.c` | The client refuses: `-- line too long (max 400 bytes), not sent` |
| Names per `353` line | 400 bytes of nicknames | `NAMES_MAX` in `room.c` | The list continues in another `353` |
| Output queue per client | 64 KiB | `OUTQ_MAX` in `conn.h` | The client is dropped: `Output queue full` |
| Clients | 64 (`-m`, 1–1000; lowered to fit `ulimit -n`) | `DEFAULT_MAX_CLIENTS` in `parleyd.c` | `ERROR :Server full` |
| Burst of lines | 20 (`-f`, 0–1000, 0 = no limit), refilled at 5 lines/s | `DEFAULT_FLOOD` in `parleyd.c`, `FLOOD_COST_MS` (200) in `server.h` | `ERROR :Excess flood` |
| Idle time | 120 s (`-t`, 0–86400, 0 = never) | `DEFAULT_IDLE` in `parleyd.c` | `PING :parley`, then `ERROR :Ping timeout` after 2× |
| Closing time | 2 s | `CLOSE_TIMEOUT_MS` in `conn.h` | The connection is closed hard |
| Shutdown grace | 2 s | `SHUTDOWN_GRACE_MS` in `server.c` | The server exits even if clients are still connected |
| Listen backlog | 16 | `LISTEN_BACKLOG` in `net.c` | The kernel queues at most this many unaccepted connections |

## Framing

TCP delivers a stream of bytes, not messages ([Chapter 15](15-a-first-server.md#tcp-is-a-byte-stream)). The server cuts the stream into lines with `msg_take_line` ([Chapter 8](08-memory.md)):

- A line ends at `\n`. A `\r` just before it is removed. So `\r\n` and a bare `\n` both work. A `\r` anywhere else stays in the line.
- A line may arrive in pieces, even one byte at a time. The server keeps the partial line until the `\n` arrives.
- **Too long.** A line of more than 512 bytes counting its `\n` is thrown away, and the server answers `417`. So 510 bytes of text plus `\r\n` is fine; 511 bytes plus `\r\n` is too long; 511 bytes plus a bare `\n` is fine. If 512 bytes arrive with no `\n`, the server drops them at once, answers `417` once, and skips everything up to the next `\n`.
- **NUL byte.** A line that contains a `\0` is thrown away with `400 Line contains a NUL byte`. A NUL would end the line early for every C string function, so one part of the program could see a different line than another.
- After a bad line, the next line works normally.
- Every line, including a too-long or bad one, counts for flood control.

```console
$ { printf 'NICK long\r\n'; printf 'MSG :%0600d\r\n' 0; printf 'MSG :%0505d\r\n' 0 | tr 0 x; printf 'MSG :%0506d\r\n' 0 | tr 0 y; printf 'MSG :a\0b\r\n'; printf 'QUIT\r\n'; } | nc -N 127.0.0.1 6990 | awk '{ print length($0), substr($0, 1, 40) }'
42 :parley 001 long :Welcome to parley, lon
11 :long JOIN
23 :parley 353 long :long
31 :parley 366 long :End of NAMES
38 :parley 417 long :Input line too long
511 :long MSG :xxxxxxxxxxxxxxxxxxxxxxxxxxxxx
38 :parley 417 long :Input line too long
43 :parley 400 long :Line contains a NUL by
15 ERROR :Goodbye
```

The lengths count the `\r` but not the `\n`. Read the output line by line:

- The 600-byte line got `417`.
- `MSG :` plus 505 `x` is exactly 512 bytes with `\r\n`, so it was accepted. The broadcast `:long MSG :xxx…` would be 518 bytes, so the server cut the text to make the line exactly 512 bytes (511 shown, plus the `\n`).
- One byte more (506 `y`) got `417`.
- The line with a NUL got `400`.

## Connection lifecycle

```text
 connect ──► UNREGISTERED ──NICK ok──► REGISTERED ──QUIT / error / timeout──► CLOSING ──► LINGER ──► closed
                  │                                                              ▲
                  └─────────────── QUIT / error / timeout ───────────────────────┘
```

| State | Meaning |
|---|---|
| Unregistered | Connected, no nickname yet. Only `NICK`, `PING`, `PONG` and `QUIT` work; `MSG`, `ME` and `NAMES` get `451`. Not in the room: no broadcasts are received. |
| Registered | Has a nickname and is in the room. Receives every broadcast. |
| Closing | The server has decided to close (after `QUIT`, `ERROR`, …). It sends what is still queued, then goes on. Input is no longer handled. |
| Linger | The client has left the room (the others got `QUIT`). The server called `shutdown(SHUT_WR)`, so the client sees the end of the stream after the last line. Input is read and thrown away until the client closes too. |
| Closed | The socket is closed. Closing and linger together last at most 2 s. |

Why linger? Closing a socket that still has unread input makes the kernel send a reset (RST) instead of a normal end. The client may then lose the last lines, including the `ERROR` that says why. See [Chapter 21](21-robustness.md#closing-gracefully-fin-rst-and-lingering).

If the client just disconnects (end of stream or a read error), the server closes at once. The others get `QUIT` with the reason `Connection closed`, or the error text (for example `Connection reset by peer`).

## Commands

Client-to-server commands. "Needs NICK" means the command gets `451` before registration.

| Command | Syntax | Needs NICK | Effect |
|---|---|---|---|
| `NICK` | `NICK name` | no | Register, or change your nickname |
| `MSG` | `MSG :text` | yes | Say something to the room |
| `ME` | `ME :action` | yes | Describe an action (`* alice waves`) |
| `NAMES` | `NAMES` | yes | List who is in the room |
| `PING` | `PING :token` | no | Ask the server to answer `PONG :token` |
| `PONG` | `PONG :token` | no | Answer the server's `PING` |
| `QUIT` | `QUIT [:reason]` | no | Leave |

Anything else gets `421 Unknown command`, including numerics and IRC commands like `JOIN`.

### NICK

`NICK name` registers you or changes your name.

A valid nickname has 1–16 bytes. The first is an ASCII letter; the rest are ASCII letters, digits, `_` or `-`. Nicknames are compared without regard to ASCII case: `alice`, `Alice` and `ALICE` are the same name.

| Situation | Reply |
|---|---|
| No parameter, or an empty one | `:parley 431 * :No nickname given` |
| Invalid name | `:parley 432 * 9lives :Erroneous nickname` |
| Name used by another client (any case) | `:parley 433 * ALICE :Nickname is already in use` |
| First valid `NICK` (registration) | To you: `001`. To everyone, you included: `:name JOIN`. To you: `353` and `366`. |
| Later `NICK` with a different name | To everyone, you included: `:old NICK new` |
| Later `NICK` with a case-only change (`alice` → `Alice`) | Counts as a change: `:alice NICK Alice` |
| Later `NICK` with exactly your name | Nothing |

The `*` in the error replies is your nickname, or `*` before you have one.

### MSG and ME

`MSG :text` and `ME :action` broadcast `:name MSG :text` (or `:name ME :action`) to everyone, the sender included. Every client then sees the same conversation in the same order, and the sender knows the line arrived. Missing or empty text gets `:parley 412 name :No text to send`. Text that would make the broadcast longer than 512 bytes is cut.

### NAMES

`NAMES` answers with one or more `:parley 353 name :nick1 nick2 ...` lines (at most 400 bytes of names each, in connection order), then `:parley 366 name :End of NAMES`. Unregistered connections are not listed.

### PING and PONG

`PING :token` gets `:parley PONG :token`. The token is echoed unchanged; `PING :` (an empty token) gets `:parley PONG :`. `PING` without a parameter gets `:parley 461 name PING :Not enough parameters`.

`PONG` is accepted and ignored. What keeps a connection alive is that *anything* arrived. `PONG` is the cheapest line to send.

### QUIT

`QUIT :reason` (or `QUIT` alone, reason `Quit`) makes the server:

1. send `:name QUIT :reason` to everyone else (the sender is no longer in the room),
2. send `ERROR :Goodbye` to the sender,
3. close the connection (closing, then linger).

`QUIT :` gives an empty reason. `QUIT see you` uses only the first word, `see`. An unregistered `QUIT` gets `ERROR :Goodbye` and nothing is broadcast.

## Server-to-client messages

| Message | When |
|---|---|
| `:parley 001 name :Welcome to parley, name` | registration succeeded |
| `:name JOIN` | `name` registered (also sent to `name`) |
| `:parley 353 name :nick1 nick2 ...` | the names list |
| `:parley 366 name :End of NAMES` | end of the names list |
| `:old NICK new` | `old` is now called `new` |
| `:name MSG :text` | a message |
| `:name ME :action` | an action |
| `:name QUIT :reason` | `name` left |
| `:parley PONG :token` | answer to your `PING` |
| `PING :parley` | you were idle; answer with any line, `PONG :parley` is the polite one |
| `ERROR :reason` | the server is closing your connection; see below |
| `:parley <code> <target> [param] :<text>` | a numeric reply; `<target>` is your nickname or `*` |

Messages from the server itself have the prefix `parley`; messages caused by a client have that client's nickname. `PING` and `ERROR` have no prefix.

### Numeric replies

| Code | Format | Meaning |
|---|---|---|
| `001` | `:parley 001 name :Welcome to parley, name` | you are registered |
| `353` | `:parley 353 name :nick1 nick2 ...` | names in the room |
| `366` | `:parley 366 name :End of NAMES` | end of names |
| `400` | `:parley 400 name :Malformed message` | an empty prefix, a prefix without a command, or more than 8 middle parameters |
| `400` | `:parley 400 name :Line contains a NUL byte` | a `\0` in the line |
| `412` | `:parley 412 name :No text to send` | `MSG`/`ME` without text |
| `417` | `:parley 417 name :Input line too long` | line over 512 bytes |
| `421` | `:parley 421 name CMD :Unknown command` | unknown command `CMD` |
| `431` | `:parley 431 name :No nickname given` | `NICK` without a name |
| `432` | `:parley 432 name nick :Erroneous nickname` | invalid nickname `nick` |
| `433` | `:parley 433 name nick :Nickname is already in use` | `nick` is taken |
| `451` | `:parley 451 * :You have not registered` | `MSG`/`ME`/`NAMES` before `NICK` |
| `461` | `:parley 461 name PING :Not enough parameters` | `PING` without a token |

Every 4xx code is an error: the command did nothing, and the connection stays open.

### ERROR reasons

`ERROR :reason` is always the last line on a connection. After it, the server closes.

| Reason | Cause | The others see | Server log |
|---|---|---|---|
| `Goodbye` | you sent `QUIT` | `:name QUIT :your reason` | `name left (your reason)` |
| `Registration timeout` | no valid `NICK` within `-t` seconds | nothing | nothing |
| `Ping timeout` | nothing received for 2 × `-t` seconds (a `PING :parley` was sent after `-t`) | `:name QUIT :Ping timeout` | `name left (Ping timeout)` |
| `Excess flood` | more than `-f` lines in a burst | `:name QUIT :Excess flood` | `name left (Excess flood)` |
| `Server full` | `-m` clients already connected, or the server is out of file descriptors | nothing | `warning: ADDR: server full` or `warning: accept: ...: refused a client` |
| `Server shutting down` | SIGINT or SIGTERM | nothing: everyone leaves at once, no `QUIT` lines | `got signal 15 (Terminated), shutting down` |

Two more ways to be disconnected send no `ERROR`:

- **Output queue full.** A client that does not read its socket has a growing queue in the server. Over 64 KiB, the server drops it at once, because there is no room left for another line. The others see `:name QUIT :Output queue full`, and the log says `warning: name: output queue full, disconnecting`. This protects the server from a slow or stuck client ([Chapter 18](18-event-loop.md#partial-writes-and-backpressure)).
- **A write error** (the client vanished). The others see `QUIT` with the error text.

"Server full" is best effort. The server writes the line and closes at once. If the refused client has already sent something (the `parley` client sends `NICK` right away), that unread input makes the kernel answer with a reset. The client may then see `Connection reset by peer` instead of the `ERROR` line.

## Flood control

parley uses the "penalty clock" of RFC 1459 §8.10, with smaller numbers:

- Each client has a clock that never lags behind the real time.
- Every line moves the clock 200 ms ahead.
- If the clock gets more than `-f` lines (`-f` × 200 ms) ahead of now, the client gets `ERROR :Excess flood` and is disconnected.

A client sending 5 lines per second or fewer never trips it. A burst of up to `-f` lines is fine: with `-f 20` the 21st line of a burst is one too many. `-f 0` turns the limit off (the benchmark in [Chapter 22](22-shipping-and-libc-choices.md) does this). See [Chapter 21](21-robustness.md#flood-control).

## Timeouts

With `-t IDLE` (default 120 s, 0 = off):

| Situation | After | The server sends |
|---|---|---|
| Not registered | `IDLE` s after connecting | `ERROR :Registration timeout` |
| Registered, nothing received | `IDLE` s | `PING :parley` |
| Still nothing after the `PING` | another `IDLE` s (2 × `IDLE` in total) | `ERROR :Ping timeout` |

Once you are registered, any data resets the idle timer: an unknown command, even part of a line. The registration deadline counts from the connect and is never reset.

## Transcripts

The server for these runs:

```console
$ parleyd -p 6990 -T 2>server.log &
```

`nc -C` turns each `\n` you type into `\r\n`; `-N` closes the sending side at the end of the input, so the server sees `QUIT` and then the end of the stream. (These are the flags of the OpenBSD `nc` found on Debian and Ubuntu.)

### A solo session

```console
$ nc -C -N 127.0.0.1 6990 <<'END'
NICK carol
MSG :hello, world
ME :waves
names
PING :12345
PING tok
PONG :parley
QUIT :bye
END
:parley 001 carol :Welcome to parley, carol
:carol JOIN
:parley 353 carol :carol
:parley 366 carol :End of NAMES
:carol MSG :hello, world
:carol ME :waves
:parley 353 carol :carol
:parley 366 carol :End of NAMES
:parley PONG :12345
:parley PONG :tok
ERROR :Goodbye
```

### Errors before registration

```console
$ nc -C -N 127.0.0.1 6990 <<'END'
MSG :hi
ME :waves
NAMES
NICK
NICK 9lives
NICK abcdefghijklmnopq
NICK bad.nick
JOIN
PING
FOO bar
nick dave
END
:parley 451 * :You have not registered
:parley 451 * :You have not registered
:parley 451 * :You have not registered
:parley 431 * :No nickname given
:parley 432 * 9lives :Erroneous nickname
:parley 432 * abcdefghijklmnopq :Erroneous nickname
:parley 432 * bad.nick :Erroneous nickname
:parley 421 * JOIN :Unknown command
:parley 461 * PING :Not enough parameters
:parley 421 * FOO :Unknown command
:parley 001 dave :Welcome to parley, dave
:dave JOIN
:parley 353 dave :dave
:parley 366 dave :End of NAMES
```

`abcdefghijklmnopq` has 17 letters, one too many. This session ended without `QUIT`, so the log says `dave left (Connection closed)`.

### Parser corner cases

```console
$ nc -C -N 127.0.0.1 6990 <<'END'
NICK erin
MSG hello world
MSG
MSG :
  msg    :  spaced   out
:mallory MSG :not mallory
:lonelyprefix
MSG a b c d e f g h i
MSG a b c d e f g h :nine
PING :
PING a b
001 erin :hi
QUIT see you
END
:parley 001 erin :Welcome to parley, erin
:erin JOIN
:parley 353 erin :erin
:parley 366 erin :End of NAMES
:erin MSG :hello
:parley 412 erin :No text to send
:parley 412 erin :No text to send
:erin MSG :  spaced   out
:erin MSG :not mallory
:parley 400 erin :Malformed message
:parley 400 erin :Malformed message
:erin MSG :a
:parley PONG :
:parley PONG :a
:parley 421 erin 001 :Unknown command
ERROR :Goodbye
```

Line by line:

- `MSG hello world` sent only `hello`: free text belongs in the trailing parameter.
- The spoofed prefix `:mallory` was ignored.
- Nine middle parameters were malformed; eight plus a trailing one were fine, and only the first counts as the text.
- The log shows the reason taken from `QUIT see you`: `erin left (see)`.

A bare `\n` works as a line end too:

```console
$ printf 'NICK lf\nMSG :no CR here\nQUIT\n' | nc -N 127.0.0.1 6990
:parley 001 lf :Welcome to parley, lf
:lf JOIN
:parley 353 lf :lf
:parley 366 lf :End of NAMES
:lf MSG :no CR here
ERROR :Goodbye
```

### Two clients

`alice` connects first. `bob` connects 0.3 s later and first tries the name `ALICE`. The input is sent with pauses, so the lines interleave as shown.

```text
alice sends: NICK alice · MSG :hi bob · NICK Alice · NICK Alice · NICK al · (end of input)
bob sends:   NICK ALICE · NICK bob · NAMES · QUIT :off to lunch
```

What `alice` received:

```text
:parley 001 alice :Welcome to parley, alice
:alice JOIN
:parley 353 alice :alice
:parley 366 alice :End of NAMES
:bob JOIN
:alice MSG :hi bob
:alice NICK Alice
:Alice NICK al
:bob QUIT :off to lunch
```

What `bob` received:

```text
:parley 433 * ALICE :Nickname is already in use
:parley 001 bob :Welcome to parley, bob
:bob JOIN
:parley 353 bob :alice bob
:parley 366 bob :End of NAMES
:parley 353 bob :alice bob
:parley 366 bob :End of NAMES
:alice MSG :hi bob
:alice NICK Alice
:Alice NICK al
ERROR :Goodbye
```

The server log (`-T` turns off the timestamps):

```text
parleyd: info: listening on 127.0.0.1:6990
parleyd: info: alice joined
parleyd: info: bob joined
parleyd: info: alice is now Alice
parleyd: info: Alice is now al
parleyd: info: bob left (off to lunch)
parleyd: info: al left (Connection closed)
parleyd: info: got signal 15 (Terminated), shutting down
```

The second `NICK Alice` changed nothing, so nobody was told.

### Timeouts with `-t 1`

Three clients: one never registers, `sleepy` registers and then stays silent, and `awake` answers the first `PING` with `PONG` and the second with `QUIT`.

```console
$ parleyd -p 6992 -T -t 1 2>server.log &
```

```text
unregistered client received:
ERROR :Registration timeout

sleepy received:
:parley 001 sleepy :Welcome to parley, sleepy
:sleepy JOIN
:parley 353 sleepy :sleepy
:parley 366 sleepy :End of NAMES
:awake JOIN
PING :parley
ERROR :Ping timeout

awake received:
:parley 001 awake :Welcome to parley, awake
:awake JOIN
:parley 353 awake :sleepy awake
:parley 366 awake :End of NAMES
PING :parley
:sleepy QUIT :Ping timeout
PING :parley
ERROR :Goodbye
```

### Flood with `-f 5`

```console
$ parleyd -p 6992 -T -f 5 2>server.log &
$ { echo 'NICK fast'; for i in 1 2 3 4 5 6 7 8; do echo "MSG :$i"; done; sleep 0.5; } | sed -u 's/$/\r/' | nc 127.0.0.1 6992
:parley 001 fast :Welcome to parley, fast
:fast JOIN
:parley 353 fast :fast
:parley 366 fast :End of NAMES
:fast MSG :1
:fast MSG :2
:fast MSG :3
:fast MSG :4
ERROR :Excess flood
```

`NICK` plus four messages are five lines; the sixth line was one too many.

### Output queue full

A test script registered `slow` and never read from its socket. Then `fast` sent 400-byte messages as quickly as it could, with `-f 0`. After a few thousand lines (the number varies with the kernel's socket buffers), `fast` saw:

```text
:slow QUIT :Output queue full
```

and the server logged:

```text
parleyd: warning: slow: output queue full, disconnecting
parleyd: info: slow left (Output queue full)
```

### Server full and shutdown with `-m 1`

```console
$ parleyd -p 6992 -T -m 1 2>server.log &
$ nc -d 127.0.0.1 6992
ERROR :Server full
```

This is the refusal while one client (`first`) is connected. `nc -d` sends nothing, so the `ERROR` line arrives reliably (see "Server full is best effort" above). Then `kill -TERM` stops the server. `first` receives `ERROR :Server shutting down`, the server exits with status 0, and the log ends with:

```text
parleyd: info: first joined
parleyd: warning: 127.0.0.1:40092: server full
parleyd: info: got signal 15 (Terminated), shutting down
```

The port number in the warning varies.

## How the `parley` client uses the protocol

| You type | The client sends |
|---|---|
| (on connect) | `NICK name` (`-n NICK`, default `guest`) |
| `hello` | `MSG :hello` |
| an empty line | nothing |
| `/me waves` | `ME :waves` |
| `/nick bob` | `NICK bob` |
| `/nick` or `/me` with no argument | nothing: `-- usage: /nick NEWNICK` (or `/me ACTION`) |
| `/who` | `NAMES` |
| `/quit` / `/quit gone` | `QUIT` / `QUIT :gone` |
| `/help`, an unknown `/cmd` | nothing: handled locally |
| a line over 400 bytes, or with a NUL byte | nothing: `-- line too long (max 400 bytes), not sent` or `-- line contains a NUL byte, not sent` |
| Ctrl-D, Ctrl-C, end of input | `QUIT`, then `shutdown(SHUT_WR)`; it reads until the server closes, exit status 0 |

| The server sends | The client shows |
|---|---|
| `:a MSG :t` | `<a> t` |
| `:a ME :t` | `* a t` |
| `:a JOIN` | `--> a joined` |
| `:a QUIT :r` | `<-- a left (r)` |
| `:a NICK b` | `-- a is now known as b`, or `-- You are now known as b` for your own change |
| `001` | `-- Welcome to parley, you` |
| `353` | `-- Users: a b c` |
| `366`, `PONG` | nothing |
| `PING :x` | nothing; it answers `PONG :x` |
| any `4xx` | `!! param: text`, or `!! text` |
| `ERROR :r` | remembered; when the server closes: `-- Disconnected: r`, exit status 1 |
| anything else | `?? raw line` |
| a line over 512 bytes, or with a NUL | `!! the server sent a bad line (ignored)` |
| a line `msg_parse` rejects | nothing |

The client is taught in [Chapter 16](16-a-first-client.md), [Chapter 19](19-an-interactive-client.md) and [Chapter 20](20-terminal-ui.md). A real line-mode session (`-L`). A second client, `zed`, says `hi`, sends `/me nods`, renames itself `zed2` and quits:

```console
$ { sleep 0.6; printf 'hello\n\n/me waves\n/who\n/nick 1bad\n/nick newme\n/bogus\n/help\n'; sleep 0.6; } | parley -L -p 6996 -n you
-- Welcome to parley, you
--> you joined
-- Users: zed you
<zed> hi
* zed nods
-- zed is now known as zed2
-- unknown command /bogus (try /help)
-- commands:
--   /nick NEWNICK   change your nickname
--   /me ACTION      describe an action
--   /who            list who is in the room
--   /quit [REASON]  leave
--   /help           show this help
-- anything else is sent as a message
<you> hello
* you waves
-- Users: zed2 you
!! 1bad: Erroneous nickname
-- You are now known as newme
<-- zed2 left (done)
$ echo $?
0
```

Local messages (`/bogus`, `/help`) appear at once. The answers to what was sent come back a moment later.

Against a full server (`-m 1`), the client's `NICK` is already on its way when the server closes. So the reset described above is what the user sees, on both libcs:

```console
$ parley -L -p 6996 -n second
-- Disconnected: Connection reset by peer
$ echo $?
1
```

## parley and IRC

IRC (Internet Relay Chat) was defined in RFC 1459 (1993) and updated by RFC 2812 (2000). Today's servers follow the "Modern IRC" documents and the IRCv3 extensions. parley borrows IRC's wire format, so the parsing and framing skills transfer directly. It drops everything a one-room, one-server chat does not need.

### What parley keeps

| Feature | IRC | parley |
|---|---|---|
| Message grammar | `[:prefix] command params [:trailing]`, RFC 1459 §2.3.1 | the same |
| Line limit | 512 bytes including CR LF | the same |
| Line end | CR LF; many servers accept a bare LF | CR LF sent; bare LF accepted |
| Empty lines | silently ignored | the same |
| Separators | one or more spaces in RFC 1459; exactly one in RFC 2812 | one or more |
| `NICK` | register / change nickname; `:old NICK new` | the same |
| `PING` / `PONG` | keepalive in both directions | the same |
| `QUIT [:reason]`, `ERROR :reason` | leave; the server's last word | the same |
| Numerics | `001`, `353`, `366`, `412`, `421`, `431`, `432`, `433`, `451`, `461` | the same codes and meanings; some texts shorter (`366` says `End of NAMES`, not `End of NAMES list`, and has no channel) |
| `417 ERR_INPUTTOOLONG` | modern IRC (not in the RFCs): `Input line was too long` | the same code; the text is `Input line too long` |
| `400 ERR_UNKNOWNERROR` | modern IRC: a command could not be processed | used for malformed lines |
| Flood control | the "penalty clock": 2 s per message, at most 10 s ahead (RFC 1459 §8.10) | the same idea: 200 ms per line, at most `-f` lines ahead |
| Case-insensitive nicknames | yes | yes |

### What parley simplifies

| Topic | IRC | parley | Why |
|---|---|---|---|
| Registration | `NICK` + `USER` (plus optional `PASS`, `CAP`); then `001`–`005` and more | `NICK` only; then `001`, `JOIN`, `353`, `366` | one room, no user/host identity |
| Sending text | `PRIVMSG <target> :text` to a channel or a user | `MSG :text` to the one room | no targets |
| Actions | CTCP: `PRIVMSG #chan :\x01ACTION waves\x01` | `ME :waves` | no CTCP framing |
| Joining | `JOIN #chan` by the client; `:nick!user@host JOIN #chan` | no client command; `:nick JOIN` sent by the server on registration | everyone is in the one room |
| Names list | `:server 353 me = #chan :@op +voice nick` | `:parley 353 me :nick nick` | no channel, no visibility symbol, no status prefixes |
| `PONG` reply | `:server PONG server :token` | `:parley PONG :token` | one server |
| Prefix | `nick!user@host` or a server name | `nick` or `parley` | no user/host |
| Parameters | up to 15 | up to 8 middle + trailing | enough for every command |
| Nickname | 9 characters in the RFCs (longer today); letters, digits and `` -[]\`^_{\|} `` (RFC 2812) | 1–16; letters, digits, `_` and `-` | simple to validate |
| Case mapping | `rfc1459`: `{}\|^` are the lowercase of `[]\~`; advertised in `005 CASEMAPPING` | ASCII only | no special characters in nicknames |
| Numeric target | your nickname, or `*` before registration | the same, without a `005` advertisement | |
| Overflow of incoming lines | `417`, truncate, or disconnect (servers differ) | `417`, the line is dropped | predictable for tests |

### What parley drops

- **Channels:** several rooms, `JOIN`, `PART`, `TOPIC`, `KICK`, `INVITE`, `LIST`, channel keys and limits.
- **Modes and operators:** user and channel modes, `MODE`, `OPER`, `KILL`, bans, voice.
- **Private messages:** `PRIVMSG` to a user, `NOTICE`, `AWAY`.
- **Queries:** `WHO`, `WHOIS`, `WHOWAS`, `MOTD`, `LUSERS`, `VERSION`, `TIME`, `INFO`, `ISON`, `USERHOST`, and the `002`–`005` welcome numerics.
- **Server networks:** server-to-server links, `SQUIT`, `CONNECT`, `LINKS`, nickname collision handling. parley is one server.
- **CTCP:** the `\x01`-delimited client-to-client sub-protocol (`ACTION`, `VERSION`, `PING`, `DCC`).
- **IRCv3:**
  - capability negotiation (`CAP LS`, `CAP REQ`),
  - message tags (`@time=...;msgid=...` before the prefix, up to 8191 more bytes),
  - SASL authentication (`AUTHENTICATE`),
  - `server-time`, `echo-message`, `batch`, `chathistory`.
  `echo-message` is the IRCv3 way to get what parley always does: the sender receives its own messages.
- **Security:** TLS (IRC on port 6697, RFC 7194) and passwords. parley listens on `127.0.0.1` by default. Before you use `-b` to open it to a network, it needs TLS and authentication.

A real IRC client cannot talk to `parleyd`: it sends `USER` and `PRIVMSG`, and it waits for `JOIN` on a channel. But the parser and framer in `msg.c` would handle IRC's own lines unchanged. Only the command table in `room.c` would need to grow.

## Summary

- One message per line, at most 512 bytes with `\r\n`: `[:prefix] COMMAND params [:trailing]`. Put free text in the trailing parameter.
- Seven commands: `NICK`, `MSG`, `ME`, `NAMES`, `PING`, `PONG`, `QUIT`. Only `NICK`, `PING`, `PONG` and `QUIT` work before registration.
- `MSG` and `ME` go to everyone, the sender too. `JOIN`, `NICK` changes and `QUIT` are broadcast by the server.
- Errors are 4xx numerics and leave the connection open. `ERROR :reason` is the last line before the server closes.
- Limits protect the server: line length, 8 parameters, 16-byte nicknames, flood control, idle timeouts, a 64 KiB output queue, and a client limit.
- glibc and musl builds behave identically on the wire.
- parley keeps IRC's wire format, flood clock and numerics. It drops channels, modes, private messages, server networks, CTCP and IRCv3.

Sources for this appendix: [REFERENCES.md](REFERENCES.md#appendix-c--the-parley-protocol)
