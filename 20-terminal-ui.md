# Chapter 20 — A Terminal User Interface

In [Chapter 19](19-an-interactive-client.md), bob's message lands in the middle of the line you're typing,
because the terminal echoes your keys. This chapter takes the terminal over: raw mode, so every key comes
straight to the program; escape sequences, to draw anywhere; and the window size. Then `parley` gets a
split screen: chat on top, a status bar, and an input line edited by our own code. You'll also learn to
always put the terminal back, and to repair it by hand.

## Cooked mode: the terminal edits for you

The kernel's *terminal driver* sits between your keyboard and a program. By default it runs in
*canonical* ("cooked") mode:

- It collects a whole line: `read()` gets nothing until you press Enter.
- It handles Backspace itself; the program never sees the typo.
- It echoes what you type.
- It turns Ctrl-C into `SIGINT`, Ctrl-Z into `SIGTSTP`, and Ctrl-D on an empty line into "end of file"
  (`read()` returns 0).

That's why `cat` and the Chapter 19 client got line editing for free. This program prints the bytes of
each `read()`:

`examples/ch20/keys.c`

```c
/* keys.c - show the bytes each read() from the terminal returns.
 * Usage: keys [-r]     -r: raw mode. Type q (as the first key) to quit. */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

static struct termios saved;            /* the settings we found */

static void restore(void)
{
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &saved);
}

static int raw_mode(void)
{
    if (tcgetattr(STDIN_FILENO, &saved) < 0)
        return -1;
    atexit(restore);                    /* runs at exit() and return from main */
    struct termios t = saved;           /* a copy: change only what we need */
    t.c_iflag &= ~(ICRNL | IXON);       /* Enter stays '\r'; Ctrl-S/Ctrl-Q are keys */
    t.c_lflag &= ~(ICANON | ECHO | ISIG | IEXTEN);
    t.c_cc[VMIN] = 1;                   /* read() returns after 1 byte... */
    t.c_cc[VTIME] = 0;                  /* ...with no timeout */
    return tcsetattr(STDIN_FILENO, TCSAFLUSH, &t);
}

int main(int argc, char *argv[])
{
    int raw = argc > 1 && strcmp(argv[1], "-r") == 0;
    if (raw && raw_mode() < 0) {
        perror("keys: raw mode");
        return 1;
    }
    printf("%s mode, q quits\n", raw ? "raw" : "cooked");
    char buf[64];
    ssize_t n;
    while ((n = read(STDIN_FILENO, buf, sizeof buf)) > 0) {
        printf("%zd byte%s:", n, n == 1 ? "" : "s");
        for (ssize_t i = 0; i < n; i++) {
            unsigned char c = (unsigned char)buf[i];
            if (c >= 0x21 && c < 0x7f)
                printf(" %c", c);
            else
                printf(" %02x", c);       /* control or non-ASCII: in hex */
        }
        printf("\n");
        fflush(stdout);
        if (buf[0] == 'q')
            break;
    }
    return 0;
}
```

Ignore `raw_mode()` for now. Run it; type `hello`, Backspace twice, `p`, Enter; then Left and Enter; `é`
and Enter; finally Ctrl-D:

```console
$ ./keys
cooked mode, q quits
help
5 bytes: h e l p 0a
^[[D
4 bytes: 1b [ D 0a
é
3 bytes: c3 a9 0a
```

- One `read()` per line. The program saw `help` and `0a` (`'\n'`), never the Backspaces.
- Left is three bytes: ESC (`1b`), `[`, `D`. The terminal echoes ESC as `^[`.
- `é` is two UTF-8 bytes: `c3 a9`.
- Ctrl-D on an empty line made `read()` return 0, ending the loop.

## termios: switching to raw mode

`tcgetattr()` and `tcsetattr()` (`<termios.h>`) read and change the driver's settings, on a file
descriptor that is a terminal (usually stdin). The settings live in a `struct termios`:

| Field | What it holds | Flags we clear for raw mode |
|---|---|---|
| `c_iflag` | input processing | `ICRNL` (Enter → `'\n'`), `IXON` (Ctrl-S/Ctrl-Q pause output) |
| `c_oflag` | output processing | none: keep `OPOST`, so `'\n'` still moves to the start of the next line |
| `c_lflag` | "local" modes | `ICANON` (line editing), `ECHO`, `ISIG` (Ctrl-C → signal), `IEXTEN` (Ctrl-V) |
| `c_cc[]` | special characters and timers | set `VMIN` = 1, `VTIME` = 0 |

The flags are bits; clear them with `&= ~(A | B)` ([Chapter 2](02-types-and-operators.md)). Start from
the settings you read and change only what you need; never build a `struct termios` from scratch, since
it has fields you don't know about.

Without `ICANON`, `VMIN` and `VTIME` decide when `read()` returns:

| `VMIN` | `VTIME` | `read()` returns… |
|---|---|---|
| 1 | 0 | as soon as at least 1 byte is there (we use this) |
| 0 | 0 | at once, with what is there (maybe 0 bytes) |
| 0 | 5 | when a byte arrives, or 0 bytes after 0.5 s (`VTIME` is in tenths of a second) |

`tcsetattr()`'s second argument says *when* the change happens:

| Value | Meaning |
|---|---|
| `TCSANOW` | at once |
| `TCSADRAIN` | after all pending output was sent |
| `TCSAFLUSH` | after all pending output was sent, and throw away unread input (we use this) |

Now run `keys -r`. Type `h`, `i`, Enter, Left, `é`, Ctrl-C, Backspace, Ctrl-D, and `q`:

```console
$ ./keys -r
raw mode, q quits
1 byte: h
1 byte: i
1 byte: 0d
3 bytes: 1b [ D
2 bytes: c3 a9
1 byte: 03
1 byte: 7f
1 byte: 04
1 byte: q
```

- Every key arrives at once, and nothing is echoed: the program must draw everything itself.
- Enter is `0d` (`'\r'`): without `ICRNL` it no longer becomes `'\n'`.
- Ctrl-C is the byte `03`, not a signal. Ctrl-D is `04`, not end of file. Backspace is `7f` (DEL; a few
  terminals send `08`, Ctrl-H).
- A Ctrl key sends the letter's position in the alphabet: Ctrl-A is 1, Ctrl-C is 3, Ctrl-U is 21 (`0x15`).
- `atexit(restore)` put the terminal back when `main()` returned, so your shell works normally after `q`.

### glibc vs musl: "not a terminal"

`tcgetattr()` on a non-terminal fails with `ENOTTY`. The error text differs:

```console
$ ./keys -r </dev/null                     # glibc
keys: raw mode: Inappropriate ioctl for device
$ ./keys-musl -r </dev/null                # musl
keys: raw mode: Not a tty
```

Same `errno`. The name `ENOTTY` is old: it once meant "not a typewriter". glibc's text says *how* the call
failed (an `ioctl()` the device doesn't support); musl's says *why*, which is the better message. Code
should test `errno == ENOTTY` (or call `isatty()` first), never compare the text.

## Always put the terminal back

Terminal settings belong to the terminal, so they outlive your program. Forget to restore them and the
shell is left with no echo and no line editing. This program turns off `ICANON` and `ECHO`, then leaves
without restoring them, by returning from `main()` or by crashing with `abort()`:

`examples/ch20/rawcrash.c`

```c
/* rawcrash.c - switch off echo and line editing, then leave without
 * putting them back. "rawcrash" just returns; "rawcrash abort" crashes.
 * With -s ("safe"), atexit() and signal handlers restore the terminal. */
#define _POSIX_C_SOURCE 200809L
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

static struct termios saved;

static void restore(void)
{
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &saved);
}

/* tcsetattr() is async-signal-safe: we may call it in a handler. */
static void on_fatal(int signo)
{
    restore();
    raise(signo);       /* SA_RESETHAND made the action SIG_DFL: die of it */
}

int main(int argc, char *argv[])
{
    int safe = argc > 1 && strcmp(argv[1], "-s") == 0;
    int crash = argc > 1 + safe && strcmp(argv[1 + safe], "abort") == 0;

    if (tcgetattr(STDIN_FILENO, &saved) < 0) {
        perror("rawcrash: tcgetattr");
        return 1;
    }
    if (safe) {
        atexit(restore);                /* covers return from main, exit() */
        struct sigaction sa;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = SA_RESETHAND;     /* back to the default action on entry */
        sa.sa_handler = on_fatal;
        int fatal[] = { SIGABRT, SIGSEGV, SIGBUS, SIGFPE, SIGTERM, SIGHUP, SIGQUIT };
        for (size_t i = 0; i < sizeof fatal / sizeof fatal[0]; i++)
            sigaction(fatal[i], &sa, NULL);
    }
    struct termios t = saved;
    t.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &t);
    printf("echo is off. %s now...\n", crash ? "Crashing" : "Leaving");
    fflush(stdout);
    if (crash)
        abort();
    return 0;
}
```

`stty` with no arguments prints the settings that differ from the defaults, so `stty | grep icanon` shows
`-icanon` only when line editing is off:

```console
$ ./rawcrash; stty | grep icanon
echo is off. Leaving now...
-icanon -echo
```

The terminal is broken: what you type next is invisible. To repair it, type `stty sane` and Enter blind.
If Enter does nothing (it sends `'\r'`, and with `ICRNL` off the shell may not see a line end), press
Ctrl-J, which is `'\n'` itself. `reset` works too, and also clears the screen and undoes the screen
changes you'll meet below (scroll region, alternate screen):

```console
$ stty sane; stty | grep icanon
$
```

Now the crash; the result depends on your shell. In `sh` (dash on Debian and Ubuntu):

```console
$ ./rawcrash abort; stty | grep icanon
echo is off. Crashing now...
Aborted (core dumped)
-icanon -echo
```

In interactive bash, the same command prints no `-icanon -echo`. bash saves the terminal settings after
each job that exits normally (which is why plain `./rawcrash` stays broken there too) and puts them back
after a job killed by a signal. dash never touches them. Don't rely on bash: other shells, scripts and
terminal multiplexers don't do this, and bash restores only the `termios` settings, not the screen.

`atexit()` handlers run at `exit()` and on return from `main()`, **not** when a signal kills the process
(`abort()`, a segfault, `kill`). For those, `rawcrash -s` installs a signal handler:

- The handler calls `tcsetattr()`. That's allowed: `tcsetattr()` is on POSIX's list of
  *async-signal-safe* functions ([Chapter 13](13-processes-and-signals.md)).
- `SA_RESETHAND` resets the action to the default as the handler starts. `raise(signo)` then kills the
  process with the same signal, so the shell still sees "Aborted" and exit status 134 (128 + 6).

```console
$ ./rawcrash -s abort; stty | grep icanon
echo is off. Crashing now...
Aborted (core dumped)
$
```

That was `sh` again, and the terminal survived. `SIGKILL` (`kill -9`) can't be caught, so no program can
promise to restore the terminal every time. Remember `stty sane` and `reset`.

`parley` uses three layers:

| How the program ends | What restores the terminal |
|---|---|
| `return` from `main()`, `exit()` | the normal exit path calls `ui_stop()`; `atexit(ui_stop)` too |
| `die()` | a hook: `die()` calls `ui_stop()` *before* it prints, so the message lands on a normal screen |
| `SIGINT`, `SIGTERM`, `SIGHUP` | `SIGINT` and `SIGTERM` are handled ([Chapter 18](18-event-loop.md)) and end in `exit()`; in raw mode Ctrl-C sends no signal at all |

A crash inside `parley` (`SIGSEGV`) still leaves the terminal broken. A handler like `rawcrash.c`'s
would fix that; the project keeps it simple.

## Drawing with escape sequences

The terminal is a text screen with a cursor, where the bytes you write are drawn. Some byte sequences are
commands instead: ESC (byte 27, `0x1b`), `[`, optional numbers separated by `;`, and a letter, the *final
byte*, saying what to do. They come from the DEC VT100 terminal and the ECMA-48 standard, and every
terminal you'll meet (xterm, GNOME Terminal, iTerm2, the Linux console, Windows Terminal, tmux)
understands these:

| Sequence | Name | Effect |
|---|---|---|
| `ESC[r;cH` | CUP | move the cursor to row r, column c (counted from 1) |
| `ESC[nA` `ESC[nB` `ESC[nC` `ESC[nD` | CUU CUD CUF CUB | cursor up, down, right, left n cells |
| `ESC[K` | EL | erase from the cursor to the end of the line (`ESC[2K`: the whole line) |
| `ESC[2J` | ED | erase the whole screen (the cursor doesn't move) |
| `ESC[nm` | SGR | style: 0 normal, 1 bold, 7 reverse, 31 red, 35 magenta, 36 cyan |
| `ESC[t;br` | DECSTBM | scroll region: only rows t to b scroll (`ESC[r`: the whole screen) |
| `ESC[?1049h` / `l` | | switch to the alternate screen / back |
| `ESC[?25l` / `h` | DECTCEM | hide / show the cursor |

`examples/ch20/ansi.c`

```c
/* ansi.c - a few ANSI escape sequences. ESC is byte 27: "\x1b" in C. */
#include <stdio.h>

#define ESC "\x1b"

int main(void)
{
    /* SGR (Select Graphic Rendition): ESC [ n m */
    printf(ESC "[1m" "bold" ESC "[0m "
           ESC "[7m" "reverse" ESC "[0m "
           ESC "[31m" "red" ESC "[0m "
           ESC "[36m" "cyan" ESC "[0m\n");
    /* Cursor back 3 columns, then overwrite: "abcdef" becomes "abcXYf". */
    printf("abcdef" ESC "[3D" "XY\n");
    /* '\r' goes to column 1; ESC [ K erases to the end of the line. */
    printf("this text goes away\r" ESC "[K" "replaced\n");
    /* Cursor up 3 lines and right 25 columns: write into an earlier line.
     * Then down 3 and back to column 1, below everything. */
    printf(ESC "[3A" ESC "[25C" "<-- up here" ESC "[3B" "\r");
    return 0;
}
```

`cat -v` shows the bytes, with ESC as `^[` and `'\r'` as `^M`:

```console
$ ./ansi | cat -v
^[[1mbold^[[0m ^[[7mreverse^[[0m ^[[31mred^[[0m ^[[36mcyan^[[0m
abcdef^[[3DXY
this text goes away^M^[[Kreplaced
^[[3A^[[25C<-- up here^[[3B^M
```

On a terminal, the commands run. In a clear 50-column window you see this (the frame marks the window's
edges; yours has the colours and styles):

```text
+--------------------------------------------------+
|bold reverse red cyan    <-- up here              |
|abcXYf                                            |
|replaced                                          |
|                                                  |
+--------------------------------------------------+
```

- `ESC[3D` moved back 3 columns, and `XY` overwrote `de`.
- `'\r'` went to column 1 and `ESC[K` erased the old text.
- `ESC[3A` went up three lines: you can write anywhere, any time.
- Always end styles with `ESC[0m`. A style stays on until something turns it off, even after your program
  ends.

gcc accepts `"\e"` for ESC, but it's a GNU extension, and `-pedantic` says so:

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o esc esc.c
esc.c: In function ‘main’:
esc.c:4:30: warning: non-ISO-standard escape sequence, '\e'
    4 |     printf("\e[1mbold\e[0m\n");
      |                              ^
```

Use `"\x1b"`. A hex escape takes *every* hex digit that follows: `"\x1b" "[1m"` is safe, but `"\x1bA"`
is one byte, `0x1bA`, too big for a `char`. Splitting the literal, as `ansi.c`'s `ESC` macro does, avoids
that.

Real programs often look sequences up in the *terminfo* database through ncurses, because old terminals
used other codes. The VT100 subset above works everywhere today, and writing it by hand shows what
ncurses does underneath.

## A scroll region and the alternate screen

When the cursor is on the bottom row and you write `'\n'`, the screen scrolls up. A *scroll region*
(`ESC[t;br`) limits that to rows t to b; everything else stays put. That's how the client keeps its status
bar and input line still while the chat scrolls.

`examples/ch20/split.c`

```c
/* split.c - a scroll region: lines scroll above a bar that stays put. */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

int main(void)
{
    struct winsize ws;
    int rows = 24;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_row > 2)
        rows = ws.ws_row;

    printf("\x1b[2J");                                    /* clear the screen */
    printf("\x1b[%d;1H\x1b[7m status bar: I stay put \x1b[0m", rows);
    printf("\x1b[1;%dr", rows - 1);                       /* rows 1..rows-1 scroll */
    for (int i = 1; i <= 2 * rows; i++) {
        /* Go to the bottom row of the region; '\n' there scrolls the
         * region (and only the region) up by one line. */
        printf("\x1b[%d;1H\nline %d", rows - 1, i);
        fflush(stdout);
        nanosleep(&(struct timespec){ .tv_sec = 0, .tv_nsec = 100000000 }, NULL);
    }
    printf("\x1b[r");                                     /* the whole screen again */
    printf("\x1b[%d;1H\n", rows);                         /* below the bar */
    return 0;
}
```

`nanosleep()` pauses 0.1 s per line so you can watch. A 6-row window after about 0.75 s:

```text
+------------------------------+
|line 5                        |
|line 6                        |
|line 7                        |
|line 8                        |
|line 9                        |
| status bar: I stay put       |
+------------------------------+
```

Lines scroll through rows 1–5; the bar on row 6 never moves. At the end, `ESC[r` resets the region, and
the final `'\n'` on the last row scrolls the whole screen once, so your prompt appears below the bar.

The *alternate screen* is a second screen without scrollback. `ESC[?1049h` saves the cursor and switches
to it; `ESC[?1049l` switches back and restores the cursor. `less`, `vim`, `top` and `parley` use it: when
they quit, your earlier shell output is back as it was.

## How big is the window?

`ioctl(fd, TIOCGWINSZ, &ws)` fills a `struct winsize` with `ws_row` and `ws_col`. When the window is
resized, the kernel sends the foreground process `SIGWINCH` ("window changed").

`examples/ch20/winsize.c`

```c
/* winsize.c - print the terminal size, and again whenever it changes.
 * Ctrl-D quits. */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <sys/ioctl.h>          /* ioctl, TIOCGWINSZ: not POSIX, but everywhere */
#include <unistd.h>

static volatile sig_atomic_t resized = 0;

static void on_winch(int signo)
{
    (void)signo;
    resized = 1;
}

static void print_size(void)
{
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) < 0) {
        perror("TIOCGWINSZ");
        return;
    }
    printf("%d rows, %d columns\n", ws.ws_row, ws.ws_col);
}

int main(void)
{
    struct sigaction sa;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;            /* no SA_RESTART: a signal interrupts read() */
    sa.sa_handler = on_winch;
    sigaction(SIGWINCH, &sa, NULL);

    print_size();
    char buf[256];
    for (;;) {
        ssize_t n = read(STDIN_FILENO, buf, sizeof buf);
        if (n < 0 && errno == EINTR) {
            if (resized) {
                resized = 0;
                print_size();
            }
            continue;
        }
        if (n <= 0)
            break;              /* end of input (Ctrl-D) or an error */
    }
    return 0;
}
```

The handler only sets a flag. Without `SA_RESTART`, the signal interrupts the blocked `read()`, which fails
with `EINTR`, and the loop prints the new size. Start it, make the window bigger, then smaller, then press
Ctrl-D (your numbers will differ):

```console
$ ./winsize
24 rows, 80 columns
30 rows, 100 columns
12 rows, 40 columns
```

`<sys/ioctl.h>`, `TIOCGWINSZ` and `SIGWINCH` aren't in POSIX.1-2008, the version this course targets, but
every Unix-like system has them. POSIX.1-2024 finally standardised `SIGWINCH` and a function for this:
`tcgetwinsize()`.

### glibc vs musl: the window size

musl 1.2.6 already has the new `tcgetwinsize()`; glibc 2.35 doesn't. A program that uses it:

```c
/* tcwin.c - the window size the POSIX.1-2024 way: tcgetwinsize(). */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <termios.h>
#include <unistd.h>

int main(void)
{
    struct winsize ws;
    if (tcgetwinsize(STDOUT_FILENO, &ws) < 0) {
        perror("tcgetwinsize");
        return 1;
    }
    printf("%d rows, %d columns\n", ws.ws_row, ws.ws_col);
    return 0;
}
```

```console
$ musl-gcc -std=c99 -Wall -Wextra -pedantic -static -o tcwin tcwin.c
$ ./tcwin
24 rows, 80 columns
$ ./tcwin | cat
tcgetwinsize: Not a tty
$ gcc -std=c99 -Wall -Wextra -pedantic -o tcwin tcwin.c
tcwin.c: In function ‘main’:
tcwin.c:9:20: error: storage size of ‘ws’ isn’t known
    9 |     struct winsize ws;
      |                    ^~
tcwin.c:10:9: warning: implicit declaration of function ‘tcgetwinsize’ [-Wimplicit-function-declaration]
   10 |     if (tcgetwinsize(STDOUT_FILENO, &ws) < 0) {
      |         ^~~~~~~~~~~~
```

`ioctl(TIOCGWINSZ)` works on both, so the course uses it. When stdout is a pipe, the `ENOTTY` text differs
again:

```console
$ echo | ./winsize | cat                   # glibc
TIOCGWINSZ: Inappropriate ioctl for device
$ echo | ./winsize-musl | cat              # musl
TIOCGWINSZ: Not a tty
```

musl is ahead on the standard here. Until glibc catches up, `ioctl()` is the portable choice.

## Reading keys: a tiny line editor

In raw mode the program does the editing. Keep the text in an array, and after every key redraw the line:
column 1 (`'\r'`), the prompt and text, then erase the rest (`ESC[K`).

`examples/ch20/minied.c`

```c
/* minied.c - a tiny line editor in raw mode. Type, Backspace, Ctrl-U
 * (clear), Enter. Ctrl-C, or Ctrl-D on an empty line, quits. ASCII only. */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

static struct termios saved;

static void restore(void)
{
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &saved);
}

static void raw_mode(void)
{
    if (tcgetattr(STDIN_FILENO, &saved) < 0) {
        perror("minied: tcgetattr");
        exit(1);
    }
    atexit(restore);
    struct termios t = saved;
    t.c_iflag &= ~(ICRNL | IXON);
    t.c_lflag &= ~(ICANON | ECHO | ISIG | IEXTEN);
    t.c_cc[VMIN] = 1;
    t.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &t) < 0) {
        perror("minied: tcsetattr");
        exit(1);
    }
}

/* Draw the line again: column 1, prompt, text, erase the rest. */
static void refresh(const char *line, size_t len)
{
    printf("\r> %.*s\x1b[K", (int)len, line);
    fflush(stdout);
}

int main(void)
{
    char line[60];
    size_t len = 0;
    char c;

    raw_mode();
    refresh(line, len);
    while (read(STDIN_FILENO, &c, 1) == 1) {
        if (c == '\r') {                            /* Enter */
            printf("\ngot \"%.*s\"\n", (int)len, line);
            len = 0;
        } else if (c == 0x7f || c == '\b') {        /* Backspace */
            if (len > 0)
                len--;
        } else if (c == 0x15) {                     /* Ctrl-U */
            len = 0;
        } else if (c == 0x03 || (c == 0x04 && len == 0)) {
            break;                                  /* Ctrl-C, Ctrl-D */
        } else if (c >= 0x20 && c < 0x7f && len < sizeof line) {
            line[len++] = c;
        }
        refresh(line, len);
    }
    printf("\nbye\n");
    return 0;                   /* exit runs restore() */
}
```

Type `hello`, Backspace twice, `p`, Enter. Then `typo`, Ctrl-U, `fixed`, Enter. Then Ctrl-D:

```console
$ ./minied
> help
got "help"
> fixed
got "fixed"
>
bye
```

- Output processing (`OPOST`) is still on, so `printf("\n...")` starts at column 1. Without `OPOST` you'd
  need `"\r\n"`.
- `minied` turns off `ISIG` and handles Ctrl-C itself. A program that wants Ctrl-C to stay a signal keeps
  `ISIG` on.

Keys that send escape sequences (arrows, Home, Delete) need more work. A sequence is ESC, `[` or `O`,
maybe digits and `;`, then a final byte from `@` to `~`:

| Key | Bytes |
|---|---|
| Left, Right, Up, Down | `ESC [ D`, `ESC [ C`, `ESC [ A`, `ESC [ B` |
| Home, End | `ESC [ H`, `ESC [ F` (some terminals: `ESC O H`, `ESC [ 1 ~`, `ESC [ 4 ~`) |
| Delete | `ESC [ 3 ~` |

Two gotchas:

- A sequence may arrive split over two `read()`s; the editor must remember the part it has.
- The Esc key alone sends just ESC, so a program can't tell "Esc, then `[`" from the start of a sequence.
  Editors like vim wait a few milliseconds to decide. The client keeps it simple: after ESC, a byte that
  isn't `[` or `O` is dropped.

## Bytes, characters and columns

The cursor moves in *columns*, but our text is UTF-8 *bytes*: `é` is two bytes and one column. To place
the cursor after the text, the editor must count characters. In UTF-8 every byte after a character's
first looks like `10xxxxxx` ([Chapter 6](06-strings.md)), so counting the bytes that don't is enough:

`examples/ch20/width.c`

```c
/* width.c - how many characters are in a UTF-8 string? Two ways. */
#define _POSIX_C_SOURCE 200809L
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Our way: count the bytes that are not 10xxxxxx (continuation bytes). */
static size_t utf8_chars(const char *s)
{
    size_t n = 0;
    for (; *s != '\0'; s++)
        if (((unsigned char)*s & 0xC0) != 0x80)
            n++;
    return n;
}

int main(void)
{
    /* "café €5" in UTF-8. "\xac" "5": a hex escape eats every hex digit
     * after it, so "\xac5" would be one (too big) escape. */
    const char *s = "caf\xc3\xa9 \xe2\x82\xac" "5";
    printf("bytes: %zu, our count: %zu\n", strlen(s), utf8_chars(s));
    /* The C library's way: mbstowcs(NULL, s, 0) counts characters, by the
     * rules of the current locale; (size_t)-1 means "invalid". */
    printf("mbstowcs, \"C\" locale: %ld\n", (long)mbstowcs(NULL, s, 0));
    const char *name = setlocale(LC_CTYPE, "");      /* from LC_ALL/LC_CTYPE/LANG */
    printf("setlocale(LC_CTYPE, \"\") = %s\n", name != NULL ? name : "NULL (failed)");
    printf("mbstowcs, now: %ld\n", (long)mbstowcs(NULL, s, 0));
    return 0;
}
```

C's own tool is `mbstowcs()`: it converts "multibyte" text to wide characters by the rules of the current
*locale*. Every C program starts in the "C" locale; `setlocale(LC_CTYPE, "")` switches to the one the
environment asks for (`LC_ALL`, `LC_CTYPE` or `LANG`).

### glibc vs musl: locales

```console
$ LANG=C.UTF-8 ./width                       # glibc
bytes: 10, our count: 7
mbstowcs, "C" locale: -1
setlocale(LC_CTYPE, "") = C.UTF-8
mbstowcs, now: 7
$ LANG=en_US.UTF-8 ./width                   # glibc, locale not installed
bytes: 10, our count: 7
mbstowcs, "C" locale: -1
setlocale(LC_CTYPE, "") = NULL (failed)
mbstowcs, now: -1
$ LANG=en_US.UTF-8 ./width-musl              # musl
bytes: 10, our count: 7
mbstowcs, "C" locale: 10
setlocale(LC_CTYPE, "") = en_US.UTF-8
mbstowcs, now: 7
```

(`LC_ALL` or `LC_CTYPE`, if set, wins over `LANG`; unset them to try this.)

| | glibc | musl |
|---|---|---|
| "C" locale, byte ≥ 0x80 | invalid: `mbstowcs()` returns -1 | one byte, one character: 10 |
| `setlocale(LC_CTYPE, "")` | needs the locale installed on the system; can fail | accepts any name; the character set is always UTF-8 |
| `LANG` unset | stays "C" | `C.UTF-8` |

musl is simpler: UTF-8 always works, with no locale files. glibc does more with locales (translations,
sorting rules), but only for installed ones, and a missing one silently leaves you with -1. The client
counts UTF-8 bytes itself and never calls `setlocale()`, so it behaves the same with both, anywhere.

Counting characters isn't the whole story. Some characters take two columns (Chinese, Japanese, most
emoji) and some take none (a combining accent). The XSI (X/Open) function `wcwidth()` knows; exercise 3
uses it.

## One write per frame

A screen update is many small pieces: move, style, text, move again. Sent one by one over a slow link
(ssh), the screen shows half-drawn, with the cursor jumping around. The client collects the whole update
in a `struct strbuf` ([Chapter 8](08-memory.md)) and sends it with one `write_all()`. That's also why
`ui.c` avoids `printf()`: stdio flushes when *it* decides, and can't know where an update ends.

## Building parley: client v3

The client gets the split screen when stdin and stdout are both terminals. Otherwise, or with `-L`, it
behaves exactly as in Chapter 19, so scripts and pipes keep working.

```text
+-------------------------------------+
|[12:00] --> alice joined             |  rows 1 .. rows-2: the chat, a scroll region
|[12:01] <alice> hi                   |
|[12:01] <bob> hello                  |
| parley | alice @ localhost:6667     |  row rows-1: the status bar
|> hello wor_                         |  row rows: what you are typing
+-------------------------------------+
```

| File | Change |
|---|---|
| `src/term.h`, `src/term.c` | new: raw mode on and off, the window size |
| `src/sig.h`, `src/sig.c` | `SIGWINCH` sets `sig_winch` and wakes the self-pipe |
| `src/log.h`, `src/log.c` | `log_on_die()`: a function `die()` calls first |
| `src/ui.h`, `src/ui.c` | new: the layout, drawing, the line editor |
| `src/parley.c` | v3: `-L`, starts the UI on a terminal, sends output through it |
| `Makefile` | `term.o` and `ui.o` in the client |

We build it in five steps, each with a checkpoint.

### Step 1: raw mode in term.c

`term.c` holds the `termios` code: `keys.c`'s idea plus three input flags that matter only for old serial
lines (`ISTRIP` would clear the 8th bit and break UTF-8). Create `src/term.h`:

```c
/* term.h - the terminal: raw mode and window size. */
#ifndef PARLEY_TERM_H
#define PARLEY_TERM_H

/* Put the terminal on stdin in raw mode: every key press is delivered at
 * once (no line editing by the terminal), nothing is echoed, and Ctrl-C
 * is just a byte. Returns 0, or -1 with errno set (e.g. ENOTTY). */
int term_raw(void);

/* Put the terminal back the way term_raw() found it. Safe to call when
 * it is not in raw mode. */
void term_restore(void);

/* The window size in character cells. If the terminal does not know
 * (some report 0x0), assume the classic 24 rows of 80 columns. */
void term_size(int *rows, int *cols);

#endif
```

and `src/term.c`:

```c
/* term.c - the terminal: raw mode and window size. */
#include "term.h"

#include <stdbool.h>
#include <sys/ioctl.h>     /* not POSIX, but on every Unix-like system */
#include <termios.h>
#include <unistd.h>

static struct termios saved;       /* the settings to restore */
static bool raw_on = false;

int term_raw(void)
{
    if (tcgetattr(STDIN_FILENO, &saved) < 0)
        return -1;
    struct termios raw = saved;
    /* Input: no CR -> NL translation (Enter arrives as '\r'), no Ctrl-S /
     * Ctrl-Q flow control, no stripping of the 8th bit. */
    raw.c_iflag &= ~(ICRNL | IXON | ISTRIP | INPCK | BRKINT);
    /* Local modes: no line editing (ICANON), no echo, no signals from
     * Ctrl-C / Ctrl-Z (ISIG), no Ctrl-V (IEXTEN). */
    raw.c_lflag &= ~(ICANON | ECHO | ISIG | IEXTEN);
    /* read() returns as soon as 1 byte is there, with no timer. */
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    /* Output processing (OPOST: "\n" -> "\r\n") stays on. */
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) < 0)
        return -1;
    raw_on = true;
    return 0;
}

void term_restore(void)
{
    if (raw_on) {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &saved);
        raw_on = false;
    }
}

void term_size(int *rows, int *cols)
{
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_row > 0 && ws.ws_col > 0) {
        *rows = ws.ws_row;
        *cols = ws.ws_col;
    } else {
        *rows = 24;
        *cols = 80;
    }
}
```

- `raw_on` makes `term_restore()` safe to call twice, or before `term_raw()`.
- `term_size()` asks about stdout, the fd we draw on. Some terminals (serial consoles) report 0×0; we
  then fall back to 24×80.

In the `Makefile`, add `term.o` to the client:

```make
CLIENT_OBJS := $(addprefix $(BUILD)/, parley.o term.o io.o net.o sig.o msg.o strbuf.o str.o log.o)
```

#### Checkpoint

Nothing calls the new code yet; it must build cleanly with both libcs (the `Makefile` uses `-Werror`):

```console
$ make -s test-all
unit: 29 passed, 0 failed
unit: 29 passed, 0 failed
$ make -s LIBC=musl
```

### Step 2: SIGWINCH, and a hook in die()

The self-pipe already wakes the client's `poll()` loop on `SIGINT` or `SIGTERM`
([Chapter 18](18-event-loop.md)); `SIGWINCH` joins in. In `src/sig.c`, add a second flag:

```c
volatile sig_atomic_t sig_quit = 0;
volatile sig_atomic_t sig_winch = 0;
```

Split the old `on_quit()` into a shared `wake()` and two small handlers:

```c
/* A signal handler may run between any two instructions of the program,
 * so it must do almost nothing: here, set a flag and write one byte.
 * Only async-signal-safe functions may be called in a handler (write is,
 * printf and malloc are not), and it must not change errno behind the
 * back of the code it interrupted. */
static void wake(void)
{
    int saved = errno;
    if (wake_write_fd >= 0) {
        /* If the pipe is full, a wake-up is already pending: fine. */
        ssize_t r = write(wake_write_fd, "!", 1);
        (void)r;
    }
    errno = saved;
}

static void on_quit(int signo)
{
    sig_quit = signo;
    wake();
}

static void on_winch(int signo)
{
    (void)signo;
    sig_winch = 1;
    wake();
}
```

Add `sig_watch_winch()` before `sig_drain()`:

```c
int sig_watch_winch(void)
{
    struct sigaction sa;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sa.sa_handler = on_winch;
    return sigaction(SIGWINCH, &sa, NULL);
}
```

It's separate from `sig_init()` because the server shares `sig.c` and has no window. In `src/sig.h`,
before `sig_drain()`:

```c
/* Set by SIGWINCH (the terminal window changed size) once
 * sig_watch_winch() was called; the handler also wakes the self-pipe. */
extern volatile sig_atomic_t sig_winch;
int sig_watch_winch(void);
```

`die()` prints a message and exits; on the alternate screen, the message would vanish with the screen. So
`die()` gets a hook. In `src/log.h`, before `#endif`:

```c
/* Have die() call fn before it prints the error, e.g. to put the terminal
 * back in order so the message can be seen. */
void log_on_die(void (*fn)(void));
```

In `src/log.c`, a function pointer ([Chapter 5](05-pointers-and-arrays.md)) after the other `static`
variables:

```c
static const char *log_prog = "parley";
static bool log_verbose = false;
static bool log_stamp = false;
static void (*die_hook)(void) = NULL;
```

and the setter, plus the call at the start of `die()`:

```c
void log_on_die(void (*fn)(void))
{
    die_hook = fn;
}

void die(const char *fmt, ...)
{
    if (die_hook != NULL)
        die_hook();
    va_list ap;
    va_start(ap, fmt);
    log_va(LOG_ERROR, fmt, ap);
    va_end(ap);
    exit(1);
}
```

`log.c` doesn't know about terminals; it just calls whatever was registered. The client registers
`ui_stop`; the server registers nothing.

#### Checkpoint

The unit tests cover `sig.c`, which the server uses too:

```console
$ make -s test-all
unit: 29 passed, 0 failed
unit: 29 passed, 0 failed
$ make -s LIBC=musl
```

### Step 3: the screen layout in ui.c

`ui.h` is the module's whole interface; the rest of the client never writes an escape sequence. Create
`src/ui.h`:

```c
/* ui.h - the split-screen terminal user interface of the client.
 *
 *   +-------------------------------------+
 *   |[12:00] --> alice joined             |  rows 1 .. rows-2: the chat, a
 *   |[12:01] <alice> hi                   |  scroll region: each new line
 *   |[12:01] <bob> hello                  |  pushes the older ones up
 *   | parley | alice @ localhost:6667     |  row rows-1: the status bar
 *   |> hello wor_                         |  row rows: what you are typing
 *   +-------------------------------------+
 *
 * Drawing only collects escape sequences and text in a buffer; ui_flush()
 * sends it all to the terminal with one write(), so the screen never shows
 * a half-drawn state. */
#ifndef PARLEY_UI_H
#define PARLEY_UI_H

#include <stdbool.h>
#include <stddef.h>

/* Longest chat line we keep (a protocol line plus decoration). */
#define UI_LINE_MAX 600

/* Switch the terminal to raw mode and the alternate screen, and draw the
 * layout. input_max: the longest line the user may type. Returns 0, or -1
 * with errno set if stdin is not a terminal. */
int ui_start(const char *status, size_t input_max);

/* Put the terminal back: the user's screen and settings return. Safe to
 * call more than once. */
void ui_stop(void);

/* The window changed size: redraw everything. */
void ui_resize(void);

/* Add a line at the bottom of the chat area. */
void ui_print(const char *line);

/* Change the text of the status bar. */
void ui_set_status(const char *status);

/* Handle n bytes typed by the user. Each finished line (Enter) is passed
 * to on_line. Returns false if the user wants to leave (Ctrl-C, or
 * Ctrl-D on an empty line). */
bool ui_keys(const char *keys, size_t n, void (*on_line)(char *line));

/* Send everything drawn so far to the terminal. */
void ui_flush(void);

#endif
```

`ui.c` starts with its state: the window size, the output buffer, the last 200 chat lines (to redraw them
after a resize), the status text, and the input line. Create `src/ui.c` with this first part:

```c
/* ui.c - the split-screen terminal user interface of the client.
 *
 * The screen is drawn with ANSI (VT100) escape sequences: ESC [ ... a
 * letter. The ones used here:
 *   ESC[r;cH   move the cursor to row r, column c (counted from 1)
 *   ESC[2J     clear the screen        ESC[2K   clear the cursor's line
 *   ESC[t;br   scroll region: only rows t..b scroll
 *   ESC[7m     reverse video           ESC[0m   back to normal
 *   ESC[31m    red text (36m: cyan, 35m: magenta)
 *   ESC[?1049h switch to the alternate screen (l: back to the normal one)
 */
#include "ui.h"

#include <string.h>
#include <unistd.h>

#include "io.h"
#include "str.h"
#include "strbuf.h"
#include "term.h"

#define HISTORY_MAX 200          /* chat lines kept for redrawing */
#define INPUT_BUF 512

static bool active = false;
static int rows, cols;
static struct strbuf screen;     /* drawn, not yet written to the terminal */

/* The last HISTORY_MAX chat lines, in a ring: the newest is just before
 * history[next]. */
static char history[HISTORY_MAX][UI_LINE_MAX];
static int hist_count, hist_next;

static char status[128];

/* The line editor: input[0..len) is the text, cursor a byte index. */
static char input[INPUT_BUF];
static size_t input_len, cursor, input_max;

/* UTF-8: a character is 1-4 bytes; the bytes after the first one look
 * like 10xxxxxx. We count characters to find screen columns. (Wide
 * characters such as CJK take 2 columns; we ignore that.) */
static bool is_continuation(char c)
{
    return ((unsigned char)c & 0xC0) == 0x80;
}

static int columns(const char *s, size_t n)
{
    int count = 0;
    for (size_t i = 0; i < n; i++)
        if (!is_continuation(s[i]))
            count++;
    return count;
}

/* A color for the line, from its kind: errors, notices or actions. */
static const char *color_for(const char *line)
{
    if (line[0] == '[') {                       /* skip "[12:00] " */
        const char *end = strstr(line, "] ");
        if (end != NULL)
            line = end + 2;
    }
    if (strncmp(line, "!!", 2) == 0)
        return "\x1b[31m";
    if (strncmp(line, "--", 2) == 0 || strncmp(line, "<--", 3) == 0)
        return "\x1b[36m";
    if (strncmp(line, "* ", 2) == 0)
        return "\x1b[35m";
    return "";
}

static void draw_chat_line(const char *line)
{
    /* At the bottom of the scroll region, "\n" scrolls the region up by
     * one row; the new line is then written on the freed bottom row. */
    sb_appendf(&screen, "\x1b[%d;1H\n%s%s\x1b[0m", rows - 2, color_for(line), line);
}

static void draw_status(void)
{
    /* %-*.*s: pad with spaces to exactly cols characters, or cut. */
    sb_appendf(&screen, "\x1b[%d;1H\x1b[7m%-*.*s\x1b[0m", rows - 1, cols, cols, status);
}

/* Draw the input line and put the cursor where the user types. If the
 * text is wider than the screen, show the part around the cursor. */
static void draw_input(void)
{
    int width = cols - 3;              /* "> " in front, 1 column spare */
    size_t start = 0;
    while (columns(input + start, cursor - start) > width) {
        start++;
        while (start < cursor && is_continuation(input[start]))
            start++;
    }
    sb_appendf(&screen, "\x1b[%d;1H\x1b[2K> ", rows);
    int col = 0;
    size_t end = start;
    while (end < input_len) {
        if (!is_continuation(input[end]) && col++ == width)
            break;
        end++;
    }
    sb_append(&screen, input + start, end - start);
    sb_appendf(&screen, "\x1b[%d;%dH", rows, 3 + columns(input + start, cursor - start));
}

static void redraw(void)
{
    term_size(&rows, &cols);
    if (rows < 4)
        rows = 4;
    if (cols < 10)
        cols = 10;
    /* Clear, then set the scroll region to the chat area. */
    sb_appendf(&screen, "\x1b[r\x1b[2J\x1b[1;%dr", rows - 2);
    int n = hist_count < rows - 2 ? hist_count : rows - 2;
    for (int i = n; i > 0; i--)
        draw_chat_line(history[(hist_next - i + HISTORY_MAX) % HISTORY_MAX]);
    draw_status();
    draw_input();
}
```

- **The history ring.** `hist_next` is where the next line goes; it wraps with `%`. The newest line is at
  `hist_next - 1`, the oldest of the last n at `hist_next - n`. Adding `HISTORY_MAX` before `%` keeps the
  index from going negative.
- **`draw_chat_line()`** moves to the scroll region's bottom row and writes `'\n'`, which scrolls the
  region up and leaves the cursor on the freed row, where the line goes. A line wider than the window
  wraps onto the next row (the terminal's *autowrap*), scrolling the region once more.
- **`draw_status()`** uses `%-*.*s`: each `*` takes the width or precision from an `int` argument. Both
  come from `cols`, so the reverse-video bar is exactly one row wide, padded or cut.
- **`draw_input()`** shows the input line. When the text is too wide, it skips characters at the start
  until the part up to the cursor fits: the line scrolls sideways. Last, it puts the cursor where the user
  types. Every other draw function ends by calling it, so the cursor is always there.
- **`redraw()`** draws everything from scratch, at the start and after each resize.

Now the public functions:

```c
int ui_start(const char *status_text, size_t max)
{
    if (term_raw() < 0)
        return -1;
    active = true;
    input_max = max < INPUT_BUF - 1 ? max : INPUT_BUF - 1;
    str_copy(status, status_text, sizeof status);
    sb_appendf(&screen, "\x1b[?1049h");
    redraw();
    ui_flush();
    return 0;
}

void ui_stop(void)
{
    if (!active)
        return;
    active = false;
    /* Reset the scroll region, leave the alternate screen. */
    sb_appendf(&screen, "\x1b[r\x1b[?1049l");
    ui_flush();
    term_restore();
    sb_free(&screen);
}

void ui_resize(void)
{
    if (active)
        redraw();
}

void ui_print(const char *line)
{
    str_copy(history[hist_next], line, UI_LINE_MAX);
    hist_next = (hist_next + 1) % HISTORY_MAX;
    if (hist_count < HISTORY_MAX)
        hist_count++;
    if (!active)
        return;
    draw_chat_line(line);
    draw_input();                      /* puts the cursor back */
}

void ui_set_status(const char *status_text)
{
    str_copy(status, status_text, sizeof status);
    if (!active)
        return;
    draw_status();
    draw_input();
}

void ui_flush(void)
{
    if (screen.len > 0) {
        write_all(STDOUT_FILENO, screen.data, screen.len);
        sb_clear(&screen);
    }
}
```

- `ui_start()` switches to the alternate screen first, keeping the user's shell output safe.
- `ui_stop()` undoes everything in reverse order and is safe to call twice (`active`): `die()`,
  `atexit()` and the normal exit path may all call it.
- `ui_print()` records the line even when the UI isn't active, so a redraw can show it later.

In the `Makefile`, add `ui.o`. This is the final `CLIENT_OBJS`:

```make
CLIENT_OBJS := $(addprefix $(BUILD)/, parley.o ui.o term.o io.o net.o sig.o msg.o strbuf.o str.o log.o)
```

#### Checkpoint

```console
$ make -s test-all
unit: 29 passed, 0 failed
unit: 29 passed, 0 failed
$ make -s LIBC=musl
```

`ui_keys()` is declared but not written yet; that's fine while nobody calls it.

### Step 4: the line editor

The editor keeps the input as UTF-8 bytes in `input[0..input_len)`; `cursor` is a byte index, always at
the start of a character. Add the escape-sequence state after the input variables near the top of
`src/ui.c`:

```c
/* An escape sequence (e.g. an arrow key: ESC [ D) read so far. */
static char esc[8];
static size_t esc_len;
```

And the editor at the end of the file:

```c
/* Line editing. */

static void insert_byte(char c)
{
    if (input_len >= input_max) {
        sb_append(&screen, "\a", 1);   /* full: ring the bell */
        return;
    }
    memmove(input + cursor + 1, input + cursor, input_len - cursor);
    input[cursor++] = c;
    input_len++;
}

static void cursor_left(void)
{
    while (cursor > 0 && is_continuation(input[--cursor]))
        continue;
}

static void cursor_right(void)
{
    if (cursor < input_len)
        cursor++;
    while (cursor < input_len && is_continuation(input[cursor]))
        cursor++;
}

/* Delete the character before the cursor (Backspace). */
static void delete_back(void)
{
    size_t end = cursor;
    cursor_left();
    memmove(input + cursor, input + end, input_len - end);
    input_len -= end - cursor;
}

/* A complete escape sequence is in esc[0..esc_len). */
static void handle_escape(void)
{
    esc[esc_len] = '\0';
    const char *seq = esc + 1;         /* skip the ESC */
    if (strcmp(seq, "[D") == 0)
        cursor_left();
    else if (strcmp(seq, "[C") == 0)
        cursor_right();
    else if (strcmp(seq, "[H") == 0 || strcmp(seq, "OH") == 0 || strcmp(seq, "[1~") == 0)
        cursor = 0;
    else if (strcmp(seq, "[F") == 0 || strcmp(seq, "OF") == 0 || strcmp(seq, "[4~") == 0)
        cursor = input_len;
    else if (strcmp(seq, "[3~") == 0 && cursor < input_len) {   /* Delete */
        cursor_right();
        delete_back();
    }
    /* anything else (up, down, F1, ...): ignored */
}

/* Collect one more byte of an escape sequence. A sequence is ESC, then
 * '[' or 'O', then parameters, then a final byte from '@' to '~'. */
static void escape_byte(char c)
{
    esc[esc_len++] = c;
    if (esc_len == 2 && c != '[' && c != 'O') {
        esc_len = 0;                   /* not a sequence we know: drop it */
        return;
    }
    if (esc_len >= 3 && c >= '@' && c <= '~') {
        handle_escape();
        esc_len = 0;
    } else if (esc_len == sizeof esc - 1) {
        esc_len = 0;                   /* too long: drop it */
    }
}

bool ui_keys(const char *keys, size_t n, void (*on_line)(char *line))
{
    for (size_t i = 0; i < n; i++) {
        char c = keys[i];
        if (esc_len > 0) {
            escape_byte(c);
            continue;
        }
        switch (c) {
        case '\x1b':                   /* ESC: start of a sequence */
            esc[0] = c;
            esc_len = 1;
            break;
        case '\r':                     /* Enter */
        case '\n': {
            char line[INPUT_BUF];
            memcpy(line, input, input_len);
            line[input_len] = '\0';
            input_len = cursor = 0;
            on_line(line);
            break;
        }
        case '\x7f':                   /* Backspace (some send Ctrl-H) */
        case '\b':
            delete_back();
            break;
        case '\x15':                   /* Ctrl-U: clear the line */
            input_len = cursor = 0;
            break;
        case '\x01':                   /* Ctrl-A: start of line */
            cursor = 0;
            break;
        case '\x05':                   /* Ctrl-E: end of line */
            cursor = input_len;
            break;
        case '\x03':                   /* Ctrl-C */
            return false;
        case '\x04':                   /* Ctrl-D: leave if the line is empty */
            if (input_len == 0)
                return false;
            break;
        default:
            /* Printable ASCII, or a byte of a UTF-8 character. Other
             * control characters are ignored. */
            if ((unsigned char)c >= 0x20)
                insert_byte(c);
            break;
        }
    }
    if (active)
        draw_input();
    return true;
}
```

- **Moving over UTF-8.** `cursor_left()` steps back one byte, then keeps going while on a continuation
  byte; `cursor_right()` does the opposite. Backspace (`delete_back()`) is "move left, then remove the
  bytes between the new and old cursor", so it deletes both bytes of an `é`.
- **Escape sequences** are collected byte by byte in `esc[]` across calls, since a sequence can be split
  between two `read()`s. `handle_escape()` knows Left, Right, Home, End and Delete, with the common
  variants; anything else, like Up, is ignored.
- **Enter** copies the line and clears the input before calling `on_line()`, which may modify its
  argument (the slash-command parser does) and may call `ui_print()`.
- **`insert_byte()`** refuses to go past `input_max` and rings the bell (`'\a'`) instead.
- Ctrl-A and Ctrl-E (start and end of line) are bash's and Emacs's shortcuts.

#### Checkpoint

The UI module doesn't need the network, so you can try it alone. Put this test driver next to the
`Makefile`:

`examples/ch20/uidemo.c`

```c
/* uidemo.c - try the client's ui module on its own, without a server.
 * Put it in the project directory (next to the Makefile) and build it
 * with the module's sources:
 *   gcc -std=c99 -Wall -Wextra -pedantic -D_POSIX_C_SOURCE=200809L -Isrc \
 *       -o uidemo uidemo.c src/ui.c src/term.c src/strbuf.c src/str.c src/io.c
 * Type lines and press Enter; resize the window; Ctrl-D quits. */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "ui.h"

static volatile sig_atomic_t resized = 0;
static int count = 0;

static void on_winch(int signo)
{
    (void)signo;
    resized = 1;
}

/* Called by ui_keys() for each line the user finishes with Enter. */
static void on_line(char *line)
{
    char text[UI_LINE_MAX];
    snprintf(text, sizeof text, "<you> %s", line);
    ui_print(text);
    snprintf(text, sizeof text, " uidemo | %d lines | Ctrl-D quits", ++count);
    ui_set_status(text);
}

int main(void)
{
    struct sigaction sa;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;               /* no SA_RESTART: read() fails with EINTR */
    sa.sa_handler = on_winch;
    sigaction(SIGWINCH, &sa, NULL);

    if (ui_start(" uidemo | 0 lines | Ctrl-D quits", 100) < 0) {
        perror("uidemo");
        return 1;
    }
    atexit(ui_stop);
    ui_print("-- type something and press Enter");
    ui_flush();
    for (;;) {
        char keys[64];
        ssize_t n = read(STDIN_FILENO, keys, sizeof keys);
        if (n < 0 && errno == EINTR) {
            if (resized) {
                resized = 0;
                ui_resize();
                ui_flush();
            }
            continue;
        }
        if (n <= 0 || !ui_keys(keys, (size_t)n, on_line))
            break;
        ui_flush();
    }
    return 0;                      /* exit() runs ui_stop() */
}
```

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -D_POSIX_C_SOURCE=200809L -Isrc \
      -o uidemo uidemo.c src/ui.c src/term.c src/strbuf.c src/str.c src/io.c
$ ./uidemo
```

These screens are from an 8×40 window; yours is bigger, so you'll see more empty rows above the chat.
Right after the start:

```text
+----------------------------------------+
|                                        |
|                                        |
|                                        |
|                                        |
|                                        |
|-- type something and press Enter       |
| uidemo | 0 lines | Ctrl-D quits        |
|>                                       |
+----------------------------------------+
cursor: row 8, column 3
```

Type `hello` and Enter. Then type `cafe`, Backspace, `é au lit`, press Left twice and type `a`:

```text
+----------------------------------------+
|                                        |
|                                        |
|                                        |
|                                        |
|-- type something and press Enter       |
|<you> hello                             |
| uidemo | 1 lines | Ctrl-D quits        |
|> café au lait                          |
+----------------------------------------+
cursor: row 8, column 13
```

Press Enter, then shrink the window to 6×30. The screen is redrawn from the history; the first line is
too long and wraps:

```text
+------------------------------+
|-- type something and press En|
|ter                           |
|<you> hello                   |
|<you> café au lait            |
| uidemo | 2 lines | Ctrl-D qui|
|>                             |
+------------------------------+
cursor: row 6, column 3
```

Press Ctrl-D: your shell screen is back, exactly as it was. Same results with musl:

```console
$ musl-gcc -std=c99 -Wall -Wextra -pedantic -D_POSIX_C_SOURCE=200809L -Isrc -static \
      -o uidemo-musl uidemo.c src/ui.c src/term.c src/strbuf.c src/str.c src/io.c
$ ./uidemo </dev/null
uidemo: Inappropriate ioctl for device
$ ./uidemo-musl </dev/null
uidemo: Not a tty
```

Delete `uidemo`, `uidemo-musl` and `uidemo.c` when done; they aren't part of the project.

### Step 5: parley.c v3

Start from Chapter 19's `src/parley.c`. Replace the header comment:

```c
/* parley.c - the parley chat client.
 * Version 3: on a terminal, a split-screen user interface (ui.c); when
 * stdin or stdout is not a terminal (or with -L), plain lines as before.
 * poll() waits for the keyboard (stdin) and the server at the same time. */
```

Add `#include "ui.h"` after `#include "str.h"`. Add a field to `struct config`:

```c
/* Settings from the command line. */
struct config {
    const char *host;
    long port;
    const char *nick;
    bool line_mode;
};
```

`print_help()` gets the new option:

```c
static void print_help(void)
{
    printf("usage: parley [-h HOST] [-p PORT] [-n NICK] [-L]\n"
           "  -h HOST    server to connect to (default localhost)\n"
           "  -p PORT    server port (default %d)\n"
           "  -n NICK    your nickname (default guest)\n"
           "  -L         line mode: no full-screen interface\n"
           "  --help     show this help and exit\n"
           "  --version  show the version and exit\n",
           DEFAULT_PORT);
}
```

In `parse_args()`, `-L` takes no value; handle it right after the check that an argument looks like an
option:

```c
        if (arg[0] != '-' || arg[1] == '\0' || arg[2] != '\0')
            usage_error("unexpected argument '%s'", arg);
        if (arg[1] == 'L') {
            cfg->line_mode = true;
            continue;
        }
```

Two new variables after `my_nick`:

```c
static char server_name[NET_ADDR_MAX + 8];   /* "host:port", for the status bar */
static bool ui_on = false;          /* the full-screen interface is running */
```

`vprint_line()` now builds the line in a buffer, so the text can go to the UI or stdout. After it, add
`update_status()`:

```c
/* Print one line for the user, with the time in front if stamp is true
 * and show_time is on. */
static void vprint_line(bool stamp, const char *fmt, va_list ap)
{
    char line[UI_LINE_MAX];
    size_t len = 0;
    if (stamp && show_time) {
        time_t now = time(NULL);
        struct tm tm;
        if (localtime_r(&now, &tm) != NULL)
            len = strftime(line, sizeof line, "[%H:%M] ", &tm);
    }
    vsnprintf(line + len, sizeof line - len, fmt, ap);
    if (ui_on)
        ui_print(line);
    else
        printf("%s\n", line);
}

/* The status bar: who we are, where. */
static void update_status(void)
{
    char text[128];
    snprintf(text, sizeof text, " parley | %s @ %s",
             my_nick[0] != '\0' ? my_nick : "(not registered)", server_name);
    if (ui_on)
        ui_set_status(text);
}
```

`strftime()` returns the number of bytes it wrote (0 if the buffer is too small), so `len` is where the
message starts. In `handle_server_line()`, our nick changes in two places; call `update_status()` in
both:

```c
    } else if (strcmp(cmd, "NICK") == 0 && p0 != NULL) {
        if (strcmp(from, my_nick) == 0) {
            str_copy(my_nick, p0, sizeof my_nick);
            update_status();
            show("-- You are now known as %s", p0);
        } else {
            show("-- %s is now known as %s", from, p0);
        }
```

```c
    } else if (strcmp(cmd, "001") == 0 && p0 != NULL) {
        str_copy(my_nick, p0, sizeof my_nick);
        update_status();
        show("-- %s", text);
```

After `read_keyboard()`, add `read_keys()`. Raw mode has no lines to frame: the bytes go straight to the
editor, which calls `handle_input_line()` for each Enter:

```c
/* Read keys typed in the full-screen interface. Returns false when the
 * user wants to leave. */
static bool read_keys(void)
{
    char keys[256];
    ssize_t n = read(STDIN_FILENO, keys, sizeof keys);
    if (n < 0) {
        if (errno == EINTR || errno == EAGAIN)
            return true;
        die("reading input: %s", strerror(errno));
    }
    return n > 0 && ui_keys(keys, (size_t)n, handle_input_line);
}
```

Finally, `main()`. The UI starts after the connection is made (so a "cannot connect" error appears on a
normal screen); `ui_flush()` replaces `fflush()` in UI mode; the signal branch handles two flags; the
keyboard branch picks the reader.

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
    snprintf(server_name, sizeof server_name, "%s:%ld", cfg.host, cfg.port);

    /* The full-screen interface needs a terminal on both sides. */
    if (!cfg.line_mode && isatty(STDIN_FILENO) && isatty(STDOUT_FILENO)) {
        if (sig_watch_winch() < 0 || ui_start("", INPUT_MAX) < 0)
            die("setting up the terminal: %s", strerror(errno));
        ui_on = true;
        /* However we exit, give the user their terminal back: atexit()
         * runs ui_stop at exit(), die() runs it before printing. */
        atexit(ui_stop);
        log_on_die(ui_stop);
        update_status();
        note("-- type /help for help, /quit or Ctrl-D to leave");
    }
    send_msg(&(struct msg){ .command = "NICK", .nparams = 1, .params = { cfg.nick } });

    bool kbd_open = true;
    for (;;) {
        /* stdout is a pipe or file in scripts: then stdio buffers it, so
         * flush before we sleep in poll(). */
        if (ui_on)
            ui_flush();
        else
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
        if (fds[2].revents & POLLIN) {      /* a signal arrived */
            sig_drain(wake_fd);
            if (sig_winch) {                  /* the window changed size */
                sig_winch = 0;
                ui_resize();
            }
            if (sig_quit) {                   /* Ctrl-C (line mode) or SIGTERM */
                sig_quit = 0;
                if (quitting)
                    die("interrupted");       /* the second time: give up */
                leave();
            }
        }
        if (fds[1].revents & (POLLIN | POLLHUP)) {
            if (!(ui_on ? read_keys() : read_keyboard())) {
                kbd_open = false;
                leave();                  /* end of input, Ctrl-D or Ctrl-C */
            }
        }
        if (fds[0].revents & (POLLIN | POLLHUP | POLLERR)) {
            if (!read_server())
                break;
        }
    }

    ui_stop();                            /* back to the normal screen */
    ui_on = false;
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

- **Terminal on both sides.** `parley | tee chat.log` must not get escape sequences in the file, and
  `parley <script.txt` has no keyboard to put in raw mode. Either way, line mode.
- **`ui_start("", ...)`** draws an empty status bar. `update_status()` fills it once `server_name` is
  known, and again when the server confirms our nick.
- **Every exit path restores.** The normal exit calls `ui_stop()` after the loop, so `Disconnected`
  appears on the normal screen. `atexit()` covers `exit()` anywhere; the `die()` hook covers errors.
  Extra calls do nothing, thanks to `active`.
- **`sig_quit = 0`.** Two flags can wake the self-pipe now, so the branch checks and clears each one.
  In UI mode Ctrl-C is a key (`ui_keys()` returns false), but `SIGTERM` from `kill` still comes this way.

### Checkpoint

Build and test, then check that pipes still get the Chapter 19 behaviour:

```console
$ make -s test-all
unit: 29 passed, 0 failed
unit: 29 passed, 0 failed
$ make -s SANITIZE=1 test
unit: 29 passed, 0 failed
$ make -s LIBC=musl
$ build/glibc/parleyd -p 6620 -T 2>server.log & server=$!; sleep 0.2
$ printf 'hello\n/nick\n' | build/musl/parley -p 6620 -n alice; echo "exit $?"
-- usage: /nick NEWNICK
-- Welcome to parley, alice
--> alice joined
-- Users: alice
<alice> hello
exit 0
```

Now the full-screen client. bob needs a second terminal window; he talks raw protocol with `nc -C`, as in
[Chapter 16](16-a-first-client.md):

```console
$ nc -C 127.0.0.1 6620
NICK bob
:parley 001 bob :Welcome to parley, bob
:bob JOIN
:parley 353 bob :bob
:parley 366 bob :End of NAMES
```

In the first terminal, start alice. These screens are from a 10×50 window; where they show `hh:mm`,
yours show real times:

```console
$ build/musl/parley -p 6620 -n alice
```

```text
+--------------------------------------------------+
|                                                  |
|                                                  |
|                                                  |
|                                                  |
|-- type /help for help, /quit or Ctrl-D to leave  |
|[hh:mm] -- Welcome to parley, alice               |
|[hh:mm] --> alice joined                          |
|[hh:mm] -- Users: bob alice                       |
| parley | alice @ localhost:6620                  |
|>                                                 |
+--------------------------------------------------+
cursor: row 10, column 3
```

The first frame `parley` sent starts like this (10 rows: chat in rows 1–8, status on 9, input on 10):

```text
\x1b[?1049h\x1b[r\x1b[2J\x1b[1;8r\x1b[9;1H\x1b[7m                                                  \x1b[0m\x1b[10;1H\x1b[2K> \x1b[10;3H
```

Alternate screen, reset the region, clear, region 1–8, an empty reverse-video status bar, the prompt,
the cursor after it. Then:

1. As alice, type `hello bob` and Enter.
2. As bob, type `MSG :hi alice`.
3. As alice, type `typing in progre` without Enter.
4. As bob, type `MSG :still there?`.

bob's lines appear above alice's half-typed text, which stays untouched: the Chapter 19 flaw, fixed:

```text
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
cursor: row 10, column 19
```

Edit: Left, Left, Backspace, `é`, Ctrl-A, `X`. Backspace deleted the `g`; `é` went in its place; Ctrl-A
jumped to the start:

```text
|> Xtyping in proére                               |
+--------------------------------------------------+
cursor: row 10, column 4
```

Shrink the window to 8×30. The redraw shows the last 6 chat lines, but the first, the welcome, is wider
than 30 columns: it wraps, pushes itself up, and only its end, `alice`, is left in row 1. The status bar
is cut to fit:

```text
+------------------------------+
|alice                         |
|[hh:mm] --> alice joined      |
|[hh:mm] -- Users: bob alice   |
|[hh:mm] <alice> hello bob     |
|[hh:mm] <bob> hi alice        |
|[hh:mm] <bob> still there?    |
| parley | alice @ localhost:66|
|> Xtyping in proére           |
+------------------------------+
cursor: row 8, column 4
```

Press Ctrl-E and type ` and a much longer tail`. The input line scrolls sideways to keep the cursor
visible:

```text
|> oére and a much longer tail |
+------------------------------+
cursor: row 8, column 30
```

Press Ctrl-U to clear the line, type `/nick ally` and Enter. The status bar follows:

```text
+------------------------------+
|[hh:mm] -- Users: bob alice   |
|[hh:mm] <alice> hello bob     |
|[hh:mm] <bob> hi alice        |
|[hh:mm] <bob> still there?    |
|[hh:mm] -- You are now known a|
|s ally                        |
| parley | ally @ localhost:662|
|>                             |
+------------------------------+
cursor: row 8, column 3
```

Press Ctrl-C. `parley` sends `QUIT`, the server closes the connection, and your shell screen is back.
`echo $?` prints 0, and `stty | grep icanon` prints nothing: the settings are restored.

With `-L` on a terminal, the terminal itself echoes and edits, as in Chapter 19. Run
`build/glibc/parley -L -p 6620 -n carol`, wait for the welcome, type `hi from carol`, Enter, then Ctrl-D:

```text
+--------------------------------------------------+
|[hh:mm] -- Welcome to parley, carol               |
|[hh:mm] --> carol joined                          |
|[hh:mm] -- Users: bob carol                       |
|hi from carol                                     |
|[hh:mm] <carol> hi from carol                     |
|                                                  |
+--------------------------------------------------+
```

Last, `die()` on the full screen. In the first terminal, start `build/glibc/parley -p 6620 -n dave`. In
bob's terminal, press Ctrl-C to end `nc`, then freeze the server and send `dave` two `SIGTERM`s. `$server`
exists only in the first terminal, so look the server up by name:

```console
$ kill -STOP $(pgrep -x parleyd)
$ kill -TERM $(pgrep -x parley)
$ kill -TERM $(pgrep -x parley)
```

The first `SIGTERM` makes `dave` send `QUIT` and wait for the frozen server; the second gives up with
`die("interrupted")`. The hook ran `ui_stop()` first, so the message is on the normal screen and the
terminal settings are back. `parley` exits with status 1:

```text
+--------------------------------------------------+
|parley: error: interrupted                        |
|                                                  |
+--------------------------------------------------+
```

Back in the first terminal, wake the server up and stop it:

```console
$ kill -CONT $server; sleep 0.2; kill -TERM $server; wait $server
$ cat server.log
parleyd: info: listening on 127.0.0.1:6620
parleyd: info: alice joined
parleyd: info: alice left (Quit)
parleyd: info: bob joined
parleyd: info: alice joined
parleyd: info: alice is now ally
parleyd: info: ally left (Quit)
parleyd: info: carol joined
parleyd: info: carol left (Quit)
parleyd: info: dave joined
parleyd: info: bob left (Connection closed)
parleyd: info: dave left (Quit)
parleyd: info: got signal 15 (Terminated), shutting down
```

bob ended `nc` before the server was frozen, so his leaving is logged before dave's `QUIT`, which waited
for `kill -CONT`. Delete `server.log` when you're done.

Did you notice? While `dave` waited for the frozen server, Ctrl-C in his window did nothing. In UI mode
Ctrl-C is a key, and after the first one `parley` stops reading keys. Exercise 2 fixes that.

### glibc vs musl: what isatty() and tcsetattr() cost

`strace -e trace=ioctl` lists the terminal calls. Start the server again
(`build/glibc/parleyd -p 6620 -T 2>/dev/null & server=$!`), then start `parley` under `strace`, wait for
the welcome, and press Ctrl-C. Do it once per library:

```console
$ strace -e trace=ioctl -o glibc.txt build/glibc/parley -p 6620 -n gina; cat glibc.txt
ioctl(1, TCGETS, {B38400 opost isig icanon echo ...}) = 0
ioctl(0, TCGETS, {B38400 opost isig icanon echo ...}) = 0
ioctl(1, TCGETS, {B38400 opost isig icanon echo ...}) = 0
ioctl(0, TCGETS, {B38400 opost isig icanon echo ...}) = 0
ioctl(0, TCGETS, {B38400 opost isig icanon echo ...}) = 0
ioctl(0, SNDCTL_TMR_CONTINUE or TCSETSF, {B38400 opost -isig -icanon -echo ...}) = 0
ioctl(0, TCGETS, {B38400 opost -isig -icanon -echo ...}) = 0
ioctl(1, TIOCGWINSZ, {ws_row=10, ws_col=50, ws_xpixel=0, ws_ypixel=0}) = 0
ioctl(0, TCGETS, {B38400 opost -isig -icanon -echo ...}) = 0
ioctl(0, SNDCTL_TMR_CONTINUE or TCSETSF, {B38400 opost isig icanon echo ...}) = 0
ioctl(0, TCGETS, {B38400 opost isig icanon echo ...}) = 0
+++ exited with 0 +++
$ strace -e trace=ioctl -o musl.txt build/musl/parley -p 6620 -n mo; cat musl.txt
ioctl(1, TIOCGWINSZ, {ws_row=10, ws_col=50, ws_xpixel=0, ws_ypixel=0}) = 0
ioctl(0, TIOCGWINSZ, {ws_row=10, ws_col=50, ws_xpixel=0, ws_ypixel=0}) = 0
ioctl(1, TIOCGWINSZ, {ws_row=10, ws_col=50, ws_xpixel=0, ws_ypixel=0}) = 0
ioctl(0, TCGETS, {B38400 opost isig icanon echo ...}) = 0
ioctl(0, SNDCTL_TMR_CONTINUE or TCSETSF, {B38400 opost -isig -icanon -echo ...}) = 0
ioctl(1, TIOCGWINSZ, {ws_row=10, ws_col=50, ws_xpixel=0, ws_ypixel=0}) = 0
ioctl(0, SNDCTL_TMR_CONTINUE or TCSETSF, {B38400 opost isig icanon echo ...}) = 0
+++ exited with 0 +++
```

(strace can't tell `TCSETSF` from an unrelated sound-card request with the same number, so it prints
both names.) Stop the server afterwards with `kill $server`.

| Call | glibc | musl |
|---|---|---|
| `isatty(fd)` | `TCGETS`: "can I read terminal settings?" | `TIOCGWINSZ`: "can I read a window size?" |
| `tcgetattr()` | `TCGETS` | `TCGETS` |
| `tcsetattr(TCSAFLUSH)` | `TCGETS`, `TCSETSF`, `TCGETS` | `TCSETSF` |

The three `isatty()` calls come from `main()`: one for `show_time`, two for the UI check. Both libraries
answer the same question. Upstream glibc 2.35's `tcsetattr()` makes a single `ioctl()`; the two extra
`TCGETS` come from a Debian patch that Ubuntu's glibc carries, which reads the settings back to check
that the kernel accepted them. musl does exactly what the code asks: 7 calls against 11. Neither cost
matters for a chat client, but musl's version is simpler to read and to predict.

The complete project at this point is in `project/ch20/`.

## Summary

- In canonical ("cooked") mode the terminal driver edits lines, echoes and makes signals. For raw mode,
  `tcsetattr()` clears `ICANON`, `ECHO`, `ISIG`, `IEXTEN`, `ICRNL`, `IXON` and sets `VMIN` = 1,
  `VTIME` = 0: every key arrives at once as bytes.
- In raw mode Enter is `'\r'`, Ctrl-C is byte 3, arrows are escape sequences (`ESC [ D`), `é` is two
  bytes.
- The settings outlive your program. Restore them on every path: normal exit, `atexit()`, a `die()` hook,
  signal handlers (`tcsetattr()` is async-signal-safe). Last resort: `stty sane` (typed blind; Ctrl-J if
  Enter fails) or `reset`.
- `ESC [ params letter` sequences move the cursor, erase, set colours, set a scroll region (`ESC[t;br`)
  and switch to the alternate screen (`ESC[?1049h`/`l`). Write `"\x1b"`, not GNU `"\e"`.
- `ioctl(TIOCGWINSZ)` gives the window size; `SIGWINCH` says it changed. POSIX.1-2024's
  `tcgetwinsize()` is in musl, not glibc 2.35.
- Columns aren't bytes. Count UTF-8 characters by skipping `10xxxxxx` bytes; wide characters need
  `wcwidth()` and a UTF-8 locale.
- glibc and musl differ in `ENOTTY` text and locales: musl is always UTF-8 and accepts any locale name;
  glibc needs the locale installed.
- Send each screen update with one `write()`.
- Use the full screen only when stdin and stdout are terminals.

## Exercises

1. **Ctrl-W.** In bash, Ctrl-W deletes the word before the cursor. Add it to the line editor in `ui.c`:
   first any spaces before the cursor, then the non-space characters before those.

   <details><summary>Solution</summary>

   In `ui_keys()`, add a case before Ctrl-A. `delete_back()` already handles UTF-8, so the loops work one
   character at a time:

   ```c
           case '\x17':                   /* Ctrl-W: delete the word before the cursor */
               while (cursor > 0 && input[cursor - 1] == ' ')
                   delete_back();
               while (cursor > 0 && input[cursor - 1] != ' ')
                   delete_back();
               break;
   ```

   Ctrl-W is byte 23 (`0x17`): W is the 23rd letter. In a 6×40 window, run `uidemo`, type
   `send it to   bob now` and press Ctrl-W three times:

   ```text
   |> send it                               |
   +----------------------------------------+
   cursor: row 6, column 11
   ```

   The cursor is in column 11, after `send it ` and its trailing space. Testing `input[cursor - 1]` in
   the middle of an `é` is fine: a continuation byte is never a space.

   </details>

2. **The stuck Ctrl-C.** In the checkpoint, Ctrl-C couldn't stop `dave` while the server was frozen. In
   line mode, the second Ctrl-C gives up (`die("interrupted")`). Make UI mode do the same: once `parley`
   is quitting, a Ctrl-C key should end it at once.

   <details><summary>Solution</summary>

   Two things stop it now: `main()` stops polling stdin once `quitting` is set, and `read_keys()` reports
   Ctrl-C by returning false, which also sets `kbd_open = false`.

   Let `read_keys()` call `leave()` itself and return false only at end of file. While quitting, it only
   looks for Ctrl-C:

   ```c
   static bool read_keys(void)
   {
       char keys[256];
       ssize_t n = read(STDIN_FILENO, keys, sizeof keys);
       if (n < 0) {
           if (errno == EINTR || errno == EAGAIN)
               return true;
           die("reading input: %s", strerror(errno));
       }
       if (n == 0)
           return false;
       if (quitting) {
           /* We wait for the server to close the connection. If it does not,
            * Ctrl-C gives up, like the second Ctrl-C in line mode. */
           if (memchr(keys, '\x03', (size_t)n) != NULL)
               die("interrupted");
           return true;
       }
       if (!ui_keys(keys, (size_t)n, handle_input_line))
           leave();                      /* Ctrl-C, or Ctrl-D on an empty line */
       return true;
   }
   ```

   In `main()`, keep polling stdin in UI mode:

   ```c
               { .fd = kbd_open && (ui_on || !quitting) ? STDIN_FILENO : -1, .events = POLLIN },
   ```

   Line mode is unchanged: there, lines typed after `/quit` must still be ignored, and Ctrl-C is a signal.
   To test, start `parley`, freeze the server with `kill -STOP`, press Ctrl-C (nothing visible happens:
   `QUIT` was sent, no answer comes), then Ctrl-C again:

   ```text
   +--------------------------------------------------+
   |parley: error: interrupted                        |
   |                                                  |
   +--------------------------------------------------+
   ```

   The exit status is 1. Ctrl-D, `/quit` and Ctrl-C against a working server still exit with 0, as does
   a pipe, `printf 'x\n' | build/glibc/parley`. Don't forget `kill -CONT` for the server.

   </details>

3. **Columns, not characters.** Type `日本語` into `parley`: the cursor ends up 3 columns too far left,
   because each character takes two columns. Write `cols.c`, which prints how many bytes and columns each
   argument takes. After `setlocale(LC_CTYPE, "")`, decode one character at a time with `mbrtowc()` and
   get its width with `wcwidth()`.

   <details><summary>Solution</summary>

   `wcwidth()` is an X/Open (XSI) function, so ask for it with `_XOPEN_SOURCE`:

   ```c
   /* cols.c - how many screen columns does each argument take? */
   #define _XOPEN_SOURCE 700          /* wcwidth() is an X/Open function */
   #include <locale.h>
   #include <stdio.h>
   #include <string.h>
   #include <wchar.h>

   static int columns(const char *s)
   {
       mbstate_t state;
       memset(&state, 0, sizeof state);
       size_t left = strlen(s);
       int cols = 0;
       while (left > 0) {
           wchar_t wc;
           size_t n = mbrtowc(&wc, s, left, &state);
           if (n == (size_t)-1 || n == (size_t)-2)
               return -1;                  /* not valid in this locale */
           int w = wcwidth(wc);
           if (w > 0)
               cols += w;                  /* -1 (control) and 0 add nothing */
           s += n;
           left -= n;
       }
       return cols;
   }

   int main(int argc, char *argv[])
   {
       setlocale(LC_CTYPE, "");
       for (int i = 1; i < argc; i++)
           printf("%-10s %2zu bytes, %2d columns\n", argv[i], strlen(argv[i]), columns(argv[i]));
       return 0;
   }
   ```

   The last argument is `e` followed by a combining acute accent (`U+0301`, bytes `cc 81`):

   ```console
   $ LANG=C.UTF-8 ./cols hello café 日本語 "$(printf 'e\xcc\x81')"
   hello       5 bytes,  5 columns
   café       5 bytes,  4 columns
   日本語   9 bytes,  6 columns
   é         3 bytes,  1 columns
   ```

   musl prints the same. Note the alignment: `%-10s` pads to 10 *bytes*, not columns, so `printf()` has
   the very problem this exercise is about.

   Without a UTF-8 locale the libraries differ, as in `width.c`. With `LANG` unset, glibc stays in the
   "C" locale, where `é` is invalid; musl uses `C.UTF-8`:

   ```console
   $ env -u LANG ./cols café                 # glibc
   café       5 bytes, -1 columns
   $ env -u LANG ./cols-musl café            # musl
   café       5 bytes,  4 columns
   ```

   To use this in `ui.c`, `columns()` would sum `wcwidth()` instead of counting characters, and the client
   would call `setlocale(LC_CTYPE, "")` at startup. With glibc that works only if the user's locale is
   installed, so a careful version falls back to byte counting when `setlocale()` fails.

   </details>

4. **Who decides?** Predict what each command does, then check: does it use the full screen, and are
   there timestamps?

   - `build/glibc/parley -n eve`
   - `build/glibc/parley -n eve | tee chat.log`
   - `build/glibc/parley -n eve </dev/null`
   - `build/glibc/parley -L -n eve`

   <details><summary>Solution</summary>

   | Command | Full screen | Timestamps |
   |---|---|---|
   | `parley -n eve` | yes | yes |
   | `parley -n eve \| tee chat.log` | no: stdout is a pipe | no: `show_time` follows `isatty(STDOUT_FILENO)` |
   | `parley -n eve </dev/null` | no: stdin is not a terminal | yes: stdout is |
   | `parley -L -n eve` | no: `-L` | yes |

   With `</dev/null`, stdin is at end of file at once, so `parley` registers, sends `QUIT` and exits with
   the welcome lines and status 0, as in a script. With `| tee`, you type in line mode and `chat.log` gets
   clean lines without escape sequences or times, ready for `grep`.

   </details>

Sources for this chapter: [REFERENCES.md](REFERENCES.md#chapter-20--a-terminal-user-interface)
