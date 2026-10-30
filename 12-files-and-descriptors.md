# Chapter 12 — Files and File Descriptors

So far the project did all its I/O through stdio: `fgets`, `printf`, `fputs`. These sit on simpler POSIX calls, `open`, `read`, `write` and `close`, which work on **file descriptors**: small integers naming an open file, pipe, terminal or (from [Chapter 14](14-networking-basics.md)) network connection.

This chapter covers descriptors, short reads and writes, what end of file means, and when stdio's buffers let output leave the program, which differs a lot between glibc and musl. In the project, the server reads and writes descriptors 0 and 1 through a new `io.c`, reused unchanged for sockets later; the client reads with `getline`; and `io.c` gets tests. [Chapter 13](13-processes-and-signals.md) covers processes and signals.

## Two ways to do I/O

| | stdio | file descriptors |
|---|---|---|
| Defined by | C99 (`<stdio.h>`) | POSIX (`<unistd.h>`, `<fcntl.h>`) |
| Handle | `FILE *` | `int` |
| Open / close | `fopen`, `fclose` | `open`, `close` |
| Read / write | `fgets`, `getline`, `fread`, `printf`, `fputs`, `fwrite` | `read`, `write` |
| Buffering | yes, in your process | none: every call is a system call |
| Formatting | yes (`printf`, `scanf`) | no, bytes only |
| Works on sockets, non-blocking I/O, `poll` | badly | yes |

A `FILE` is a struct in your process holding a descriptor, a buffer and flags. `printf` formats into the buffer and later calls `write()`; `fgets` `read()`s a large block and hands it out a line at a time.

Use stdio for text files and the terminal. Use descriptors when you need to know exactly when bytes go out and how many arrived: pipes, sockets, the server. To mix both on one file, `fflush` the stream before using the descriptor.

## File descriptors are small integers

A descriptor is an index into the process's table of open files. Three are open when `main` starts:

| fd | name in `<unistd.h>` | stdio stream | usually |
|---|---|---|---|
| 0 | `STDIN_FILENO` | `stdin` | the keyboard, or the file or pipe after `<` or `\|` |
| 1 | `STDOUT_FILENO` | `stdout` | the terminal, or the file or pipe after `>` or `\|` |
| 2 | `STDERR_FILENO` | `stderr` | the terminal |

The shell sets them up, and `write(1, ...)` works whatever fd 1 is.

`examples/ch12/fds.c`

```c
/* fds.c - file descriptors are small integers: 0, 1 and 2 are open at
 * start, and open() returns the lowest number that is free. */
#define _POSIX_C_SOURCE 200809L
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(void)
{
    const char *out = "to fd 1 (stdout)\n", *err = "to fd 2 (stderr)\n";
    if (write(STDOUT_FILENO, out, strlen(out)) < 0)
        perror("write");
    if (write(STDERR_FILENO, err, strlen(err)) < 0)
        perror("write");

    int a = open("fds.c", O_RDONLY);
    int b = open("fds.c", O_RDONLY);
    printf("open gave %d and %d\n", a, b);

    close(a);
    int c = open("/dev/null", O_WRONLY);
    printf("after close(%d), open gave %d\n", a, c);

    printf("fileno(stdin)=%d fileno(stdout)=%d fileno(stderr)=%d\n",
           fileno(stdin), fileno(stdout), fileno(stderr));
    close(b);
    close(c);
    return 0;
}
```

```console
$ cd examples/ch12
$ gcc -std=c99 -Wall -Wextra -pedantic -o fds fds.c
$ ./fds
to fd 1 (stdout)
to fd 2 (stderr)
open gave 3 and 4
after close(3), open gave 3
fileno(stdin)=0 fileno(stdout)=1 fileno(stderr)=2
```

musl prints the same. Notice:

- **`open()` returns the lowest free number.** 0, 1 and 2 are taken, so the files get 3 and 4. After `close(3)`, the next `open` gets 3 again. Shells rely on this: to redirect stdout they close 1 and open the file, which becomes 1.
- **`fileno()` gives the descriptor inside a `FILE *`.** `stdout` wraps descriptor 1.
- **`write()` takes bytes and a count, not a string.** It doesn't stop at `'\0'` or add a newline.

Why check `write()` to the terminal? It can fail (see below), and glibc marks `write` with `warn_unused_result`, which Ubuntu's gcc activates at `-O2` through `_FORTIFY_SOURCE`:

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -O2 -c wur.c
wur.c: In function ‘main’:
wur.c:2:18: warning: ignoring return value of ‘write’ declared with attribute ‘warn_unused_result’ [-Wunused-result]
    2 | int main(void) { write(1, "hi\n", 3); return 0; }
      |                  ^~~~~~~~~~~~~~~~~~~
```

(`wur.c` is the one-line program in the warning.) Without `-O2`, or with musl, whose headers lack the attribute, there's no warning. Check every `write()` anyway.

Linux shows each process's descriptor table under `/proc/PID/fd`; `/proc/self` is the process looking:

```console
$ ls -l /proc/self/fd
total 0
lrwx------ 1 you you 64 Oct 30 16:39 0 -> /dev/pts/6
lrwx------ 1 you you 64 Oct 30 16:39 1 -> /dev/pts/6
lrwx------ 1 you you 64 Oct 30 16:39 2 -> /dev/pts/6
lr-x------ 1 you you 64 Oct 30 16:39 3 -> /proc/367259/fd
$ ls -l /proc/self/fd < hello.txt 2>/dev/null | cat
total 0
lr-x------ 1 you you 64 Oct 30 16:39 0 -> /home/you/learn-c/examples/ch12/hello.txt
l-wx------ 1 you you 64 Oct 30 16:39 1 -> pipe:[1730745]
l-wx------ 1 you you 64 Oct 30 16:39 2 -> /dev/null
lr-x------ 1 you you 64 Oct 30 16:39 3 -> /proc/367260/fd
```

(Terminal name, PIDs and pipe number vary.) In the first run all three are the terminal `/dev/pts/6`; in the second, the shell connected 0 to the file, 1 to a pipe into `cat`, and 2 to `/dev/null`. Descriptor 3 is the directory `ls` is reading. `r` and `w` show how each was opened.

## open, read, write, close

The four basic calls, from `<fcntl.h>` and `<unistd.h>`:

```c
int     open(const char *path, int flags, ... /* mode_t mode */);
ssize_t read(int fd, void *buf, size_t count);
ssize_t write(int fd, const void *buf, size_t count);
int     close(int fd);
```

`ssize_t` is a signed `size_t` (from `<sys/types.h>`) so that `-1` can mean an error; print it with `%zd`. All four return `-1` on error and set `errno` ([Chapter 10](10-errors-and-debugging.md)).

`flags` holds exactly one of `O_RDONLY`, `O_WRONLY` or `O_RDWR`, `|`-combined with any of:

| Flag | Meaning |
|---|---|
| `O_CREAT` | create the file if it doesn't exist; then the `mode` argument is required |
| `O_TRUNC` | if it exists, cut it to length 0 |
| `O_APPEND` | every `write` goes to the current end of the file |
| `O_EXCL` | with `O_CREAT`: fail with `EEXIST` if the file already exists |
| `O_NONBLOCK` | don't wait; see below |
| `O_CLOEXEC` | close this descriptor in programs started with `exec` ([Chapter 13](13-processes-and-signals.md)) |

`mode` sets a new file's permissions, like `0644` (owner reads and writes, others read; octal, [Chapter 2](02-types-and-operators.md)), minus the bits in the process's umask, usually `022`. So `fopen(path, "w")` is `open(path, O_WRONLY | O_CREAT | O_TRUNC, 0666)` plus a `FILE`. `read()` returns one of three things:

| `read()` returns | Meaning |
|---|---|
| `> 0` | that many bytes are now in `buf`. It may be fewer than `count`. |
| `0` | end of file |
| `-1` | error: look at `errno` |

### mycat: cat with four system calls

A small `cat`: it copies each named file, or stdin if there are none, to stdout.

`examples/ch12/mycat.c`

```c
/* mycat.c - copy files (or stdin) to stdout with read() and write(). */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

/* Write all n bytes: write() may write fewer than asked for. Returns 0,
 * or -1 with errno set. */
static int write_all(int fd, const char *p, size_t n)
{
    while (n > 0) {
        ssize_t w = write(fd, p, n);
        if (w < 0) {
            if (errno == EINTR)
                continue;              /* a signal arrived first: retry */
            return -1;
        }
        p += w;
        n -= (size_t)w;
    }
    return 0;
}

/* Copy fd in to stdout. name is for error messages. Returns 0 or -1. */
static int copy(int in, const char *name)
{
    char buf[4096];
    for (;;) {
        ssize_t r = read(in, buf, sizeof buf);
        if (r == 0)
            return 0;                  /* end of file */
        if (r < 0) {
            if (errno == EINTR)
                continue;
            fprintf(stderr, "mycat: %s: %s\n", name, strerror(errno));
            return -1;
        }
        if (write_all(STDOUT_FILENO, buf, (size_t)r) < 0) {
            fprintf(stderr, "mycat: write error: %s\n", strerror(errno));
            return -1;
        }
    }
}

int main(int argc, char *argv[])
{
    if (argc == 1)
        return copy(STDIN_FILENO, "stdin") == 0 ? 0 : 1;

    int status = 0;
    for (int i = 1; i < argc; i++) {
        int fd = open(argv[i], O_RDONLY);
        if (fd < 0) {
            fprintf(stderr, "mycat: %s: %s\n", argv[i], strerror(errno));
            status = 1;
            continue;
        }
        if (copy(fd, argv[i]) < 0)
            status = 1;
        close(fd);
    }
    return status;
}
```

The loop in `copy` reads up to 4096 bytes, then writes exactly what `read` returned, never `sizeof buf`. `write_all` and `EINTR` come below.

```console
$ printf 'hello, file\n' > hello.txt
$ gcc -std=c99 -Wall -Wextra -pedantic -o mycat mycat.c
$ ./mycat hello.txt
hello, file
$ ./mycat nosuch hello.txt; echo "exit $?"
mycat: nosuch: No such file or directory
hello, file
exit 1
$ ./mycat < hello.txt > /dev/full; echo "exit $?"
mycat: write error: No space left on device
exit 1
$ ./mycat /; echo "exit $?"
mycat: /: Is a directory
exit 1
```

musl gives the same output. `/dev/full` refuses every write with `ENOSPC`, the easiest way to test a write error path. Note `open("/", O_RDONLY)` succeeds (directories may be opened for reading) and `read()` fails with `EISDIR`: check every call. Like `cat`, `mycat` reports a bad file, goes on, and exits with 1.

### Watching the system calls

`strace` ([Chapter 10](10-errors-and-debugging.md)) shows `mycat`'s calls; `-e trace=` picks which, `-o` writes the trace to a file:

```console
$ strace -o trace.txt -e trace=open,openat,read,write,close ./mycat hello.txt > /dev/null; cat trace.txt
openat(AT_FDCWD, "/etc/ld.so.cache", O_RDONLY|O_CLOEXEC) = 3
close(3)                                = 0
openat(AT_FDCWD, "/lib/x86_64-linux-gnu/libc.so.6", O_RDONLY|O_CLOEXEC) = 3
read(3, "\177ELF\2\1\1\3\0\0\0\0\0\0\0\0\3\0>\0\1\0\0\0P\237\2\0\0\0\0\0"..., 832) = 832
close(3)                                = 0
openat(AT_FDCWD, "hello.txt", O_RDONLY) = 3
read(3, "hello, file\n", 4096)          = 12
write(1, "hello, file\n", 12)           = 12
read(3, "", 4096)                       = 0
close(3)                                = 0
+++ exited with 0 +++
```

The first five lines are the loader loading `libc.so.6`. Then `open` gives 3, `read` returns all 12 bytes, `write` writes 12, the next `read` returns 0 (end of file), and `close`. The static musl build:

```console
$ musl-gcc -std=c99 -Wall -Wextra -pedantic -static -o mycat-musl mycat.c
$ strace -o trace.txt -e trace=open,openat,read,write,close ./mycat-musl hello.txt > /dev/null; cat trace.txt
open("hello.txt", O_RDONLY|O_LARGEFILE) = 3
read(3, "hello, file\n", 4096)          = 12
write(1, "hello, file\n", 12)           = 12
read(3, "", 4096)                       = 0
close(3)                                = 0
+++ exited with 0 +++
```

### glibc vs musl: open() and the system calls under it

| | glibc 2.35 (dynamic) | musl 1.2.6 (static) |
|---|---|---|
| `open(path, flags)` calls | `openat(AT_FDCWD, path, flags)` | `open(path, flags \| O_LARGEFILE)` |
| Before `main` | the loader opens and reads `libc.so.6` | nothing: the library is already in the program |
| `read`, `write`, `close` | the same system calls | the same system calls |

`openat` opens a path relative to a directory descriptor; `AT_FDCWD` means the current directory. glibc always uses it, because some newer CPU architectures lack the old `open` call; musl uses `open` where it exists. `O_LARGEFILE` matters only on 32-bit systems (files over 2 GiB).

Same behaviour: a **tie**. For reading traces the static musl binary is **simpler**, showing only your program; with glibc, skip the loader lines.

## Short reads, and what end of file means

`read(fd, buf, 4096)` means "up to 4096 bytes": the kernel returns what it has now, at least 1, without waiting for more. This program shows each read's size:

`examples/ch12/readsizes.c`

```c
/* readsizes.c - show how many bytes each read() returns. */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <unistd.h>

int main(void)
{
    char buf[4096];
    ssize_t n;
    while ((n = read(STDIN_FILENO, buf, sizeof buf)) > 0)
        fprintf(stderr, "read returned %zd\n", n);
    fprintf(stderr, "read returned %zd: %s\n", n, n == 0 ? "end of file" : "error");
    return n == 0 ? 0 : 1;
}
```

From a pipe whose writer sends the protocol lines in two parts, one second apart:

```console
$ (printf 'NICK al\r\n'; sleep 1; printf 'MSG :hi\r\nQUIT\r\n') | ./readsizes
read returned 9
read returned 15
read returned 0: end of file
```

The first `read` returned the 9 bytes there; the second got both remaining lines. From a 10000-byte file, or a pipe already holding them, reads are as big as the buffer:

```console
$ head -c 10000 /dev/zero > zeros.bin
$ ./readsizes < zeros.bin
read returned 4096
read returned 4096
read returned 1808
read returned 0: end of file
```

From a terminal, the driver hands over a line when you press Enter. Here the user typed `hello` Enter, `a longer line of text` Enter, `no newline yet` and Ctrl-D, then Ctrl-D again:

```console
$ ./readsizes
hello
read returned 6
a longer line of text
read returned 22
no newline yetread returned 14
read returned 0: end of file
```

Ctrl-D isn't a character the program receives; it tells the driver "hand over what's typed, now": 14 bytes after `no newline yet`, and 0 bytes, end of file, with nothing typed. So Ctrl-D ends input only at the start of a line.

The lesson: **one `read()` is not one line, one message or one anything**. A line can arrive in pieces and several lines in one piece, constantly on a network. That's why the project collects bytes in a `struct strbuf` ([Chapter 8](08-memory.md)) and takes whole lines out with `msg_take_line` ([Chapter 9](09-preprocessor-and-building.md)), tested in [Chapter 11](11-testing.md) with every piece size.

**End of file** is `read()` returning 0. What causes it depends on the descriptor:

| Descriptor | `read()` returns 0 when |
|---|---|
| regular file | the offset is at the end of the file |
| pipe | the pipe is empty and every write end is closed |
| terminal | the user pressed Ctrl-D at the start of a line |
| TCP socket ([Chapter 14](14-networking-basics.md)) | the other side closed the connection and all data has been read |

End of file is a return value, not an error. stdio records it in the `FILE`: `fgets` and `getline` return `NULL` or `-1` at end of file and on error alike, and `feof()` or `ferror()` tells which.

## Short writes

`write(fd, buf, n)` returns how many bytes it wrote, maybe fewer than `n`. For regular files and blocking pipes it's usually all, as the kernel waits for room, but not if:

- the disk filled up partway;
- a signal arrived after some bytes were written ([Chapter 13](13-processes-and-signals.md));
- the descriptor is **non-blocking** and only part fitted.

The last is easy to show. A Linux pipe holds 65536 bytes by default. A `write` to a full pipe normally waits; in non-blocking mode it writes what fits and returns.

`examples/ch12/shortwrite.c`

```c
/* shortwrite.c - a write() that writes less than asked for. The pipe has
 * no reader yet, so it fills up; in non-blocking mode write() then returns
 * what fitted, and the next write() fails with EAGAIN. */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static char data[100000];

int main(void)
{
    int fds[2];                        /* fds[0]: read end, fds[1]: write end */
    if (pipe(fds) < 0) {
        perror("pipe");
        return 1;
    }
    int flags = fcntl(fds[1], F_GETFL);
    fcntl(fds[1], F_SETFL, flags | O_NONBLOCK);

    ssize_t w = write(fds[1], data, sizeof data);
    printf("asked for %zu, write returned %zd\n", sizeof data, w);
    w = write(fds[1], data, sizeof data);
    printf("again: write returned %zd (%s)\n", w,
           w < 0 && (errno == EAGAIN || errno == EWOULDBLOCK) ? "EAGAIN" : strerror(errno));

    char buf[10000];
    ssize_t r = read(fds[0], buf, sizeof buf);
    printf("read %zd bytes; now write returns %zd\n", r, write(fds[1], data, sizeof data));
    close(fds[0]);
    close(fds[1]);
    return 0;
}
```

`pipe(fds)` creates a pipe: bytes written to `fds[1]` come out of `fds[0]`. ([Chapter 13](13-processes-and-signals.md) uses pipes between processes.)

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o shortwrite shortwrite.c
$ ./shortwrite
asked for 100000, write returned 65536
again: write returned -1 (EAGAIN)
read 10000 bytes; now write returns 8192
```

Both libraries print the same; the numbers come from the kernel. The first write was **short**, 65536 of 100000 bytes. The second found the pipe full and failed with `EAGAIN`, "try again later". Reading 10000 bytes let 8192 more in: the kernel frees pipe space in whole 4096-byte pages. `EAGAIN` and `EWOULDBLOCK` are equal on Linux, but POSIX lets them differ, so portable code checks both.

### fcntl and O_NONBLOCK

`fcntl` ("file control") changes an open descriptor's settings. To add a flag, read the flags with `F_GETFL`, `|` yours in, and write them back with `F_SETFL` (setting `O_NONBLOCK` alone would clear others, such as `O_APPEND`):

```c
int flags = fcntl(fd, F_GETFL);
if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0)
    /* handle the error */;
```

Non-blocking `read` and `write` never wait: with nothing to read or no room, they fail with `EAGAIN`. A server with many clients mustn't sit in `read()` on one of them. [Chapter 18](18-event-loop.md) uses `poll()` to find ready descriptors, and non-blocking ones so nothing waits by accident; until then the project's descriptors stay blocking.

### The write_all loop

Sending a whole buffer takes a loop, `write_all` from `mycat.c`:

```c
static int write_all(int fd, const char *p, size_t n)
{
    while (n > 0) {
        ssize_t w = write(fd, p, n);
        if (w < 0) {
            if (errno == EINTR)
                continue;              /* a signal arrived first: retry */
            return -1;
        }
        p += w;
        n -= (size_t)w;
    }
    return 0;
}
```

After a partial write it advances the pointer, shrinks the count and writes the rest, until all is written or a real error happens. Every program in this course that writes to a descriptor uses it; the project's is in `io.c`. Retrying `EAGAIN` would spin, so it returns `-1`; [Chapter 18](18-event-loop.md) handles that with a per-client output buffer.

### EINTR: interrupted system calls

If a handled signal ([Chapter 13](13-processes-and-signals.md)) arrives while a `read` or `write` waits, the kernel runs the handler and the call may fail with `errno == EINTR`, "interrupted system call", having transferred nothing. Nothing went wrong; the `continue` in `write_all` and `copy` calls again.

But if the signal was Ctrl-C, the handler sets a "please stop" flag that the loop should check before waiting again. Chapter 13 shows this, and why the project's `read_into` hands `EINTR` back to the caller.

## stdio buffers

stdio collects output and writes it with one `write()` at certain moments, so 1000 short lines cost a few `write`s, not 1000. Each `FILE` has one of three modes:

| Mode | Constant | The buffer is written |
|---|---|---|
| unbuffered | `_IONBF` | at once, on every call |
| line buffered | `_IOLBF` | when a `'\n'` is written, or the buffer is full |
| fully buffered | `_IOFBF` | when the buffer is full |

`fflush(stream)`, `fclose`, and `exit()` or returning from `main` also write it. `_exit()`, a crash or a killing signal don't: the buffer is lost, and a crashed program's last lines may never appear although the `printf`s ran.

C99: `stderr` is never fully buffered; `stdin` and `stdout` are fully buffered only if they "can be determined not to refer to an interactive device". Both libraries make `stderr` unbuffered, and `stdout` line buffered on a terminal, fully buffered otherwise.

`examples/ch12/buffering.c`

```c
/* buffering.c - stdout is buffered, stderr is not. Where the output goes
 * (terminal or pipe) decides when stdout's buffer is written.
 *     ./buffering          default buffering
 *     ./buffering line     setvbuf: line buffered, even into a pipe */
#include <stdio.h>
#include <string.h>

int main(int argc, char *argv[])
{
    if (argc > 1 && strcmp(argv[1], "line") == 0)
        setvbuf(stdout, NULL, _IOLBF, 0);    /* before any output */

    printf("out 1\n");
    fprintf(stderr, "err 1\n");
    printf("out 2\n");
    fprintf(stderr, "err 2\n");
    printf("out 3 (BUFSIZ is %d)\n", BUFSIZ);
    return 0;
}
```

On a terminal, lines come out in program order on both libraries:

```console
$ ./buffering
out 1
err 1
out 2
err 2
out 3 (BUFSIZ is 8192)
$ ./buffering-musl
out 1
err 1
out 2
err 2
out 3 (BUFSIZ is 1024)
```

Into a pipe (`2>&1` makes descriptor 2 a copy of 1, so both go to `cat`):

```console
$ ./buffering 2>&1 | cat
err 1
err 2
out 1
out 2
out 3 (BUFSIZ is 8192)
$ ./buffering-musl 2>&1 | cat
out 1
err 1
err 2
out 2
out 3 (BUFSIZ is 1024)
```

stdout is now fully buffered: glibc kept all three `out` lines until `main` returned, so the `err` lines came first. musl wrote `out 1` at once, then buffered. `strace` shows why:

```console
$ strace -o trace.txt -e trace=write,writev,ioctl ./buffering 2>&1 | cat > /dev/null; cat trace.txt
write(2, "err 1\n", 6)                  = 6
write(2, "err 2\n", 6)                  = 6
write(1, "out 1\nout 2\nout 3 (BUFSIZ is 819"..., 35) = 35
+++ exited with 0 +++
$ strace -o trace.txt -e trace=write,writev,ioctl ./buffering-musl 2>&1 | cat > /dev/null; cat trace.txt
ioctl(1, TIOCGWINSZ, 0x7fff63300b08)    = -1 ENOTTY (Inappropriate ioctl for device)
writev(1, [{iov_base="out 1", iov_len=5}, {iov_base="\n", iov_len=1}], 2) = 6
writev(2, [{iov_base="err 1\n", iov_len=6}], 1) = 6
writev(2, [{iov_base="err 2\n", iov_len=6}], 1) = 6
writev(1, [{iov_base="out 2\nout 3 (BUFSIZ is 1024)\n", iov_len=29}, {iov_base=NULL, iov_len=0}], 2) = 29
+++ exited with 0 +++
```

glibc made one `write` at the end. musl's stdout starts line buffered and, on its first write, asks for the window size (`TIOCGWINSZ`), which only a terminal can answer; on `ENOTTY` it writes that line and switches to full buffering. musl also uses `writev`, which writes several pieces of memory in one call, where glibc copies them together for `write`.

### glibc vs musl: stdout buffering

| | glibc 2.35 | musl 1.2.6 |
|---|---|---|
| `BUFSIZ` | 8192 | 1024 |
| stdout to a terminal | line buffered | line buffered |
| stdout to a pipe or file | fully buffered, 4096-byte writes (the pipe's block size) | first line written at once, then fully buffered, 1024-byte writes |
| stderr | unbuffered | unbuffered |
| system call | `write` | `writev` |

Both follow C99: a **tie**. The lesson: **the order of stdout and stderr lines in a pipe depends on the library.** Never test it ([Chapter 11](11-testing.md)). When order matters, say so in code: `setvbuf(stdout, NULL, _IOLBF, 0)` at the start, or `fflush(stdout)` when output must go out. `./buffering line` does the first, and behaves like the terminal on both:

```console
$ ./buffering line 2>&1 | cat
out 1
err 1
out 2
err 2
out 3 (BUFSIZ is 8192)
```

### setvbuf and fflush

```c
int setvbuf(FILE *stream, char *buf, int mode, size_t size);
int fflush(FILE *stream);
```

`setvbuf` sets the mode; with `NULL` for `buf` the library picks the buffer. Call it **before any other operation on the stream** (C99 leaves later changes undefined). It returns 0 on success.

`fflush(stream)` writes the buffer now; `fflush(NULL)` flushes every output stream. Both return 0, or `EOF` with `errno` set if the write failed, which matters more than it looks.

### Write errors arrive late

A buffered `printf` only copies into the buffer and can't know the `write` will fail. The error shows up when the buffer is written: in a later `printf`, at `fflush`, or at `exit`, unseen.

`examples/ch12/lateerror.c`

```c
/* lateerror.c - stdio reports a write error late: when the buffer is
 * written, not when you call printf. */
#include <errno.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    int n = printf("hello\n");
    fprintf(stderr, "printf returned %d\n", n);
    n = printf("world\n");
    fprintf(stderr, "printf returned %d\n", n);
    if (fflush(stdout) == EOF) {
        fprintf(stderr, "fflush failed: %s\n", strerror(errno));
        return 1;
    }
    fprintf(stderr, "fflush succeeded\n");
    return 0;
}
```

```console
$ ./lateerror > /dev/full; echo "exit $?"
printf returned 6
printf returned 6
fflush failed: No space left on device
exit 1
$ ./lateerror-musl > /dev/full; echo "exit $?"
printf returned -1
printf returned 6
fflush failed: No space left on device
exit 1
```

With glibc both `printf`s succeed and only `fflush` sees the error. musl writes its first line at once, so its first `printf` fails, the second fills the buffer, and `fflush` fails too. Without the `fflush` check, the glibc program would exit with 0, its output silently lost.

Neither library is better; the rule is the same: **check the last `fflush` (or `fclose`) on an output stream before exiting with 0.** The project has since [Chapter 10](10-errors-and-debugging.md); now the server stops needing it, because `write_all` reports every error at once.

## getline: lines of any length

`fgets` returns a line longer than its buffer in pieces, which you must notice (no `'\n'`) and handle. POSIX's `getline` reads a whole line of any length into a buffer it grows with `realloc`:

```c
ssize_t getline(char **lineptr, size_t *n, FILE *stream);
```

Start with `char *line = NULL` and `size_t cap = 0` and pass their addresses. `getline` allocates the buffer, stores its size in `cap`, and reuses it later, growing it as needed. It returns the bytes read, counting the `'\n'` but not the `'\0'` it adds, or `-1` at end of file or on error. `free` the buffer once, at the end.

`getline` isn't C99: it needs `#define _POSIX_C_SOURCE 200809L` before the first `#include`, or under `-std=c99` both libraries hide it:

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -c nomacro.c
nomacro.c:15:41: warning: implicit declaration of function ‘getline’ [-Wimplicit-function-declaration]
```

Only a warning, but the compiler then assumes `getline` returns `int`, not `ssize_t`. [Chapter 9](09-preprocessor-and-building.md)'s `-Werror` makes it an error. `head` with `getline`:

`examples/ch12/myhead.c`

```c
/* myhead.c - print the first N lines of stdin (default 10), like head -n.
 * getline() reads lines of any length. */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>

int main(int argc, char *argv[])
{
    long want = argc > 1 ? strtol(argv[1], NULL, 10) : 10;
    char *line = NULL;                 /* getline allocates and grows it */
    size_t cap = 0;
    ssize_t len;
    long longest = 0;

    for (long i = 0; i < want && (len = getline(&line, &cap, stdin)) != -1; i++) {
        fwrite(line, 1, (size_t)len, stdout);   /* len counts '\0' bytes too */
        if (len > longest)
            longest = len;
    }
    if (ferror(stdin)) {
        perror("myhead");
        free(line);
        return 1;
    }
    fprintf(stderr, "longest line: %ld bytes, buffer: %zu bytes\n", longest, cap);
    free(line);
    return 0;
}
```

Feed it a 5001-byte line (ten pieces for a 512-byte `fgets`), shortened for display by `cut -c1-20`; the stderr report is shown separately:

```console
$ { printf 'short\n'; head -c 5000 /dev/zero | tr '\0' x; printf '\nlast\n'; } | ./myhead 2 | cut -c1-20
short
xxxxxxxxxxxxxxxxxxxx
```

```text
glibc:  longest line: 5001 bytes, buffer: 8194 bytes
musl:   longest line: 5001 bytes, buffer: 6147 bytes
```

**The length counts every byte, including `'\0'`.** `strlen` would stop at a NUL byte inside a line, so `myhead` writes with `fwrite(line, 1, len, ...)`, not `fputs`:

```console
$ printf 'a\0b\nc\n' | ./myhead | od -c
0000000   a  \0   b  \n   c  \n
0000006
```

Both bytes after the NUL survived. The project compares `len` with `strlen(line)` to catch NUL bytes.

### glibc vs musl: getline's buffer

The buffer sizes in the report above differ, and for tiny lines even more:

```console
$ seq 100 | ./myhead 3
1
2
3
$ seq 100 | ./myhead 3 2>&1 >/dev/null
longest line: 2 bytes, buffer: 120 bytes
$ seq 100 | ./myhead-musl 3 2>&1 >/dev/null
longest line: 2 bytes, buffer: 3 bytes
```

glibc starts with 120 bytes and at least doubles when it grows. musl allocates about what the line needs, adding spare room only while a long line is still arriving. POSIX only promises the line fits: glibc does fewer `realloc`s on short lines, musl wastes less memory on long ones. A **tie**, as long as you only pass `cap` back to `getline`.

## Is it a terminal? isatty

`isatty(fd)` returns 1 for a terminal, else 0 (with `errno` set to `ENOTTY`). `ls` uses it to print columns for a person and one name per line into a pipe; many tools use colour only on a terminal.

`examples/ch12/tty.c`

```c
/* tty.c - which of the standard descriptors are terminals? */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <unistd.h>

int main(void)
{
    for (int fd = 0; fd <= 2; fd++)
        fprintf(stderr, "fd %d: %s\n", fd, isatty(fd) ? "terminal" : "not a terminal");
    return 0;
}
```

```console
$ ./tty
fd 0: terminal
fd 1: terminal
fd 2: terminal
$ ./tty < /dev/null | cat
fd 0: not a terminal
fd 1: not a terminal
fd 2: terminal
```

Both libraries agree. The server uses `isatty(STDIN_FILENO)` to print a hint only when a person is typing.

## Descriptors, open files and dup

There are three layers between a descriptor and the bytes on disk:

```text
 process's fd table        open file descriptions          files
 +----+                    +------------------------+
 |  3 | -----------------> | offset 12, O_RDONLY    | --+
 +----+          +-------> +------------------------+   |     +--------------+
 |  4 | ---+     |                                      +---> | offsets.c    |
 +----+    |     |         +------------------------+   |     | (the inode)  |
 |  5 | ---|-----+         | offset 4, O_RDONLY     | --+     +--------------+
 +----+    +-------------> +------------------------+
```

- A **file descriptor** is just a number in your process's table.
- Each `open()` creates a new **open file description** in the kernel, holding the **file offset** (where the next `read` or `write` happens) and flags such as `O_APPEND` and `O_NONBLOCK`.
- Several open file descriptions can refer to the same **file**.

`dup(fd)` creates a new descriptor (the lowest free number) for the **same** open file description, so the two share one offset. `dup2(old, new)` lets you choose the new number, closing it first if needed. The shell's `2>&1` is `dup2(1, 2)`.

`examples/ch12/offsets.c`

```c
/* offsets.c - two open() calls give two file offsets; dup() shares one. */
#define _POSIX_C_SOURCE 200809L
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

/* Read 4 bytes from fd and print them. */
static void show(const char *what, int fd)
{
    char buf[5] = "";
    ssize_t n = read(fd, buf, 4);
    printf("%-14s fd %d read %zd: \"%s\"\n", what, fd, n, n > 0 ? buf : "");
}

int main(void)
{
    int a = open("offsets.c", O_RDONLY);
    int b = open("offsets.c", O_RDONLY);
    int c = dup(a);
    if (a < 0 || b < 0 || c < 0) {
        perror("offsets");
        return 1;
    }
    show("a", a);
    show("b (own open)", b);
    show("c = dup(a)", c);
    show("a again", a);
    printf("lseek(a, 0, SEEK_CUR) = %ld\n", (long)lseek(a, 0, SEEK_CUR));
    close(a);
    close(b);
    close(c);
    return 0;
}
```

```console
$ ./offsets
a              fd 3 read 4: "/* o"
b (own open)   fd 4 read 4: "/* o"
c = dup(a)     fd 5 read 4: "ffse"
a again        fd 3 read 4: "ts.c"
lseek(a, 0, SEEK_CUR) = 12
```

`b` has its own offset and starts at the beginning. `c` shares `a`'s, so it continues where `a` stopped, and `a` then continues after `c`. `lseek(fd, 0, SEEK_CUR)` reports the offset without moving it: 12, after three 4-byte reads. (Pipes, terminals and sockets have no offset; `lseek` fails there with `ESPIPE`.)

Two rules follow:

- **`close(fd)` frees only the number.** The open file description, and the connection or pipe behind it, lives until the **last** descriptor referring to it is closed. A pipe reader sees end of file only when every write end is closed, including copies made by `dup` or inherited by a child process ([Chapter 13](13-processes-and-signals.md)).
- **Copies share the offset.** After `fork`, parent and child share the offsets of all their open files.

## fflush(stdin) is not a way to clear input

Old code uses `fflush(stdin)` to throw away the rest of what the user typed. Does it work?

`examples/ch12/fflush-stdin.c`

```c
/* fflush-stdin.c - fflush() on an input stream. C99 leaves it undefined;
 * POSIX defines it only for files you can seek in. */
#include <stdio.h>

int main(void)
{
    char line[64];

    if (fgets(line, sizeof line, stdin) != NULL)
        printf("first:  %s", line);
    printf("fflush(stdin) returned %d\n", fflush(stdin));
    if (fgets(line, sizeof line, stdin) != NULL)
        printf("second: %s", line);
    else
        printf("second: nothing left\n");
    return 0;
}
```

```console
$ printf 'one\ntwo\nthree\n' > three.txt
$ printf 'one\ntwo\nthree\n' | ./fflush-stdin
first:  one
fflush(stdin) returned 0
second: two
$ printf 'one\ntwo\nthree\n' | ./fflush-stdin-musl
first:  one
fflush(stdin) returned 0
second: nothing left
$ ./fflush-stdin-musl < three.txt
first:  one
fflush(stdin) returned 0
second: two
```

The first `fgets` made stdio `read()` all 14 bytes and returned only `one\n`. What does `fflush` do with the other 10?

### glibc vs musl: fflush(stdin)

| stdin is | glibc 2.35 | musl 1.2.6 |
|---|---|---|
| a pipe or terminal | buffered input kept: `second: two` | buffered input thrown away: `second: nothing left` |
| a regular file | `second: two` | `second: two` (seeks the file back to the line after `one`) |
| return value | 0 | 0 |

C99 makes `fflush` on an input stream undefined (7.19.5.2). POSIX defines it only for seekable streams: it moves the file offset back to where the program has read up to and drops the buffer. musl does that, and on a pipe, which can't seek, the input is just gone. glibc keeps buffered input, so `fflush(stdin)` changes nothing.

Neither clears "the rest of the line". **Neither is better; don't use it.** Read and discard instead:

```c
int c;
while ((c = getchar()) != EOF && c != '\n')
    ;   /* discard up to and including the newline */
```

The client did that in [Chapter 6](06-strings.md) for overlong lines; now `getline` makes it unnecessary.

## Building parley: file descriptors

The server read lines with `fgets` and replied with `fputs`. That won't do for sockets: from [Chapter 15](15-a-first-server.md) each client is a descriptor, and the server must know exactly when bytes went out and how many came in, which stdio hides. So it switches to descriptors now, while its input is still an easily tested pipe or keyboard:

| File | Change |
|---|---|
| `src/io.h`, `src/io.c` | new: `write_all`, `read_exact`, `read_into`, `set_nonblock` |
| `src/conn.h`, `src/conn.c` | a connection has a descriptor and a state; `conn_send` uses `write_all` |
| `src/room.c` | `QUIT` sets the state to `CONN_CLOSING` |
| `src/parleyd.c` | `serve_stdin` reads descriptor 0 with `read_into` |
| `src/log.c` | each log line is written with one `write` |
| `src/parley.c` | the client reads its input with `getline` |
| `tests/test_io.c` | new: 3 tests for `io.c`, using pipes |
| `tests/tests.h`, `Makefile` | the new tests and `io.o` |

### io.h and io.c

`project/ch12/src/io.h`

```c
/* io.h - helpers for reading and writing file descriptors.
 *
 * read() and write() may transfer fewer bytes than asked for (a "short"
 * read or write), and may fail with EINTR when a signal arrives. These
 * helpers deal with that so the callers don't have to.
 */
#ifndef PARLEY_IO_H
#define PARLEY_IO_H

#include <stddef.h>
#include <sys/types.h>

#include "strbuf.h"

/* Write all n bytes, looping over short writes and EINTR. Returns 0, or -1
 * with errno set. */
int write_all(int fd, const void *buf, size_t n);

/* Read exactly n bytes, looping over short reads and EINTR. Returns n, a
 * smaller count if end of file came first, or -1 with errno set. */
ssize_t read_exact(int fd, void *buf, size_t n);

/* One read() of up to max bytes, appended to sb. Returns the number of
 * bytes read, 0 at end of file, or -1 with errno set. EINTR is NOT retried
 * here: the caller may want to check a flag set by a signal handler. */
ssize_t read_into(int fd, struct strbuf *sb, size_t max);

/* Put fd in non-blocking mode. Returns 0 or -1. */
int set_nonblock(int fd);

#endif
```

`project/ch12/src/io.c`

```c
/* io.c - helpers for reading and writing file descriptors. */
#include "io.h"

#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

int write_all(int fd, const void *buf, size_t n)
{
    const char *p = buf;
    while (n > 0) {
        ssize_t w = write(fd, p, n);
        if (w < 0) {
            if (errno == EINTR)
                continue;               /* interrupted before writing: retry */
            return -1;
        }
        p += w;                         /* a short write: send the rest */
        n -= (size_t)w;
    }
    return 0;
}

ssize_t read_exact(int fd, void *buf, size_t n)
{
    char *p = buf;
    size_t got = 0;
    while (got < n) {
        ssize_t r = read(fd, p + got, n - got);
        if (r < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        if (r == 0)
            break;                      /* end of file */
        got += (size_t)r;
    }
    return (ssize_t)got;
}

ssize_t read_into(int fd, struct strbuf *sb, size_t max)
{
    if (sb_reserve(sb, max) < 0) {
        errno = ENOMEM;
        return -1;
    }
    ssize_t r = read(fd, sb->data + sb->len, max);
    if (r > 0) {
        sb->len += (size_t)r;
        sb->data[sb->len] = '\0';
    }
    return r;
}

int set_nonblock(int fd)
{
    int flags = fcntl(fd, F_GETFL);
    if (flags < 0)
        return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}
```

Four helpers, one per lesson of this chapter:

- **`write_all`** is `mycat`'s loop, taking `const void *` so callers needn't cast.
- **`read_exact`** loops until it has `n` bytes or reaches end of file. A chat line has no fixed size, so it's for fixed-size data such as exercise 2's 16-byte rows.
- **`read_into`** does **one** `read()` into the end of a `struct strbuf` ([Chapter 8](08-memory.md)), after `sb_reserve` makes room for `max` bytes (setting `errno` to `ENOMEM` if that fails, so every failure is `-1` and `errno`). It adds the strbuf's `'\0'`. It doesn't loop, as it can't know how much will come; `msg_take_line` takes out whole lines. Nor does it retry `EINTR`: [Chapter 13](13-processes-and-signals.md)'s Ctrl-C flag must be seen before waiting again.
- **`set_nonblock`** is the `fcntl` pair. Nothing calls it until [Chapter 18](18-event-loop.md).

### A connection has a descriptor and a state

`struct conn` gets the descriptor its replies go to, and [Chapter 7](07-structs-unions-enums.md)'s `bool quitting` becomes an `enum`, because there are now two reasons to stop:

```diff
--- project/ch11/src/conn.h
+++ project/ch12/src/conn.h
@@ -10,9 +10,16 @@
 
 #define SERVER_NAME "parley"   /* the prefix of messages from the server */
 
+enum conn_state {
+    CONN_OPEN,       /* normal operation */
+    CONN_CLOSING,    /* we sent ERROR: stop reading, close the link */
+    CONN_DEAD        /* reading or writing failed: just close it */
+};
+
 struct conn {
+    int fd;                    /* where replies are written */
     char nick[NICK_MAX + 1];   /* "" until the client registers */
-    bool quitting;             /* sent QUIT: stop reading */
+    enum conn_state state;
     struct strbuf in;          /* bytes received but not yet handled */
     bool skipping;             /* throwing away a too-long line */
 };
@@ -20,7 +27,8 @@
 /* The name to use in replies: the nick, or "*" before registration. */
 const char *conn_target(const struct conn *c);
 
-/* Send one message to the client. */
+/* Send one message to the client. If writing fails, the error is logged
+ * and the state becomes CONN_DEAD. */
 void conn_send(struct conn *c, const struct msg *m);
 
 /* Send ":parley CODE <target> [param] :text", the server's reply format. */
```

`CONN_CLOSING` is a polite end: the client sent `QUIT` and got `ERROR :Goodbye`. In `room.c`, `QUIT` now sets `c->state = CONN_CLOSING` instead of `c->quitting = true`; nothing else changes. `CONN_DEAD` means a `write` failed, so sending more is pointless. With many clients ([Chapter 18](18-event-loop.md)) this state tells the cleanup code to remove that one without stopping the others.

`conn_send` now writes the formatted line straight to the descriptor:

```diff
--- project/ch11/src/conn.c
+++ project/ch12/src/conn.c
@@ -13,12 +15,17 @@
 void conn_send(struct conn *c, const struct msg *m)
 {
     char line[MSG_LINE_MAX + 1];
-    (void)c;   /* one client on stdout for now; later each conn has a socket */
-    if (msg_format(line, sizeof line, m) < 0) {
+    if (c->state == CONN_DEAD)
+        return;
+    int n = msg_format(line, sizeof line, m);
+    if (n < 0) {
         log_msg(LOG_WARN, "message too long to send: %s", m->command);
         return;
     }
-    fputs(line, stdout);
+    if (write_all(c->fd, line, (size_t)n) < 0) {
+        log_msg(LOG_ERROR, "writing to %s: %s", conn_target(c), strerror(errno));
+        c->state = CONN_DEAD;
+    }
 }
```

`msg_format` returns the length, so no `strlen`. Each reply is one `write`, sent at once. The includes change from `<stdio.h>` to `<errno.h>`, `<string.h>` and `"io.h"`. The error is logged **once**: afterwards the state is `CONN_DEAD` and later calls return at once, instead of logging one error per reply.

### serve_stdin reads descriptor 0

`project/ch12/src/parleyd.c` (part)

```c
/* Run one client session: protocol lines come in on file descriptor 0
 * (stdin) and replies go out on 1 (stdout). Returns 0 when the session
 * ended normally, -1 on an error (already logged). */
static int serve_stdin(void)
{
    int result = -1;
    struct conn client = { .fd = STDOUT_FILENO, .state = CONN_OPEN };
    char line[MSG_LINE_MAX + 1];

    if (isatty(STDIN_FILENO))
        log_msg(LOG_INFO, "type protocol lines, e.g. NICK alice (Ctrl-D ends)");

    while (client.state == CONN_OPEN) {
        ssize_t n = read_into(STDIN_FILENO, &client.in, 4096);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            log_msg(LOG_ERROR, "reading from client: %s", strerror(errno));
            goto out;
        }
        if (n == 0)
            break;                     /* end of file: the client is gone */

        enum frame_status st;
        while (client.state == CONN_OPEN
               && (st = msg_take_line(&client.in, &client.skipping, line)) != FRAME_NONE) {
            if (st == FRAME_TOO_LONG)
                conn_numeric(&client, "417", NULL, "Input line too long");
            else
                room_handle_line(&client, line);
        }
    }
    if (client.state != CONN_DEAD)
        result = 0;
out:
    sb_free(&client.in);           /* one cleanup path for every exit */
    return result;
}
```

Compared with [Chapter 11](11-testing.md)'s version:

- **No `fgets` into a 256-byte chunk plus `sb_append`:** `read_into` reads up to 4096 bytes straight into the buffer, one copy less, and `msg_take_line` splits lines as before.
- **End of file is `n == 0`**, errors `n < 0` with `errno`, instead of `NULL` from `fgets` and a later `ferror`. `EINTR` just loops; there are no signal handlers yet.
- **No `fflush(stdout)` check:** `conn_send` reports write errors and marks the client `CONN_DEAD`. The loops stop reading once the client is closing or dead; a dead client makes `serve_stdin` return `-1`, and `main` exits with 1.
- **The `isatty` hint** appears only when a person is typing, never for piped input.

`parleyd.c` also includes `<unistd.h>` (for `isatty` and `STDOUT_FILENO`) and `"io.h"`. Protocol output now bypasses stdio, so its order relative to the stderr log is the same on both libraries.

### One write per log line

`log_va` in `log.c` made three calls on unbuffered `stderr`, prefix, message and newline: up to three `write`s per line. Once several processes ([Chapter 13](13-processes-and-signals.md)) or threads ([Chapter 17](17-threads.md)) share `stderr`, pieces of lines can mix; one `write` of a short line is never split. So `log_va` builds the whole line first:

```diff
--- project/ch11/src/log.c
+++ project/ch12/src/log.c
@@ -26,12 +26,30 @@
 }
 
 /* The shared part of log_msg() and die(): a va_list can be handed on to
- * another function, but "..." cannot. */
+ * another function, but "..." cannot.
+ *
+ * stderr is unbuffered, so three fprintf calls would be three write()
+ * system calls, and lines from two processes could get mixed up. So we
+ * build the whole line first and write it with one fputs (one write). */
 static void log_va(enum log_level level, const char *fmt, va_list ap)
 {
-    fprintf(stderr, "%s: %s: ", log_prog, level_name(level));
-    vfprintf(stderr, fmt, ap);
-    fputc('\n', stderr);
+    char line[1024];
+    size_t max = sizeof line - 1;      /* keep room for the '\n' */
+    int n = snprintf(line, max, "%s: %s: ", log_prog, level_name(level));
+    if (n < 0)
+        return;
+    size_t len = (size_t)n;
+    if (len < max) {
+        n = vsnprintf(line + len, max - len, fmt, ap);
+        if (n < 0)
+            return;
+        len += (size_t)n;
+    }
+    if (len >= max)
+        len = max - 1;                 /* it was cut short to fit */
+    line[len] = '\n';
+    line[len + 1] = '\0';
+    fputs(line, stderr);
 }
```

`snprintf` and `vsnprintf` return the length the text **would** have had ([Chapter 6](06-strings.md)), so a `len` beyond what fits means truncation. Then the stored text is `max - 1` characters plus `'\0'`, so the newline goes at `max - 1` and a new `'\0'` after it. Log lines are at most 1023 bytes.

### The client reads with getline

The client's 512-byte `fgets` and discard loop ([Chapter 6](06-strings.md)) give way to `getline`, with its own length check:

```diff
--- project/ch11/src/parley.c
+++ project/ch12/src/parley.c
@@ -15,6 +16,9 @@
 
 #define DEFAULT_PORT 6667
 #define PORT_MAX 65535
+/* Longest line we send as a message: the server adds ":nick " to it, and
+ * that must still fit in MSG_LINE_MAX. */
+#define INPUT_MAX 400
 
 /* Settings from the command line. */
 struct config {
@@ -186,17 +190,21 @@
     log_init("parley", false);
     send_msg(&(struct msg){ .command = "NICK", .nparams = 1, .params = { cfg.nick } });
 
-    char line[MSG_LINE_MAX];
-    while (!quitting && fgets(line, sizeof line, stdin) != NULL) {
-        if (strchr(line, '\n') == NULL && !feof(stdin)) {
-            int c;
-            while ((c = getchar()) != EOF && c != '\n')
-                ;   /* throw away the rest of the long line */
-            fprintf(stderr, "parley: line too long (max %zu bytes), not sent\n",
-                    sizeof line - 2);
+    /* getline (POSIX) reads a whole line of any length, growing the
+     * buffer with realloc as needed. We own the buffer and free it. */
+    char *line = NULL;
+    size_t cap = 0;
+    ssize_t len;
+    while (!quitting && (len = getline(&line, &cap, stdin)) != -1) {
+        if (strlen(line) != (size_t)len) {
+            log_msg(LOG_WARN, "line contains a NUL byte, not sent");
             continue;
         }
         str_chomp(line);
+        if (strlen(line) > INPUT_MAX) {
+            log_msg(LOG_WARN, "line too long (max %d bytes), not sent", INPUT_MAX);
+            continue;
+        }
         if (line[0] == '\0')
             continue;              /* ignore empty lines */
         if (line[0] == '/')
@@ -204,6 +212,7 @@
         else
             send_msg(&(struct msg){ .command = "MSG", .trailing = line });
     }
+    free(line);
     if (!quitting)
         send_msg(&(struct msg){ .command = "QUIT" });   /* end of input: goodbye */
     if (ferror(stdin))
```

- **A whole line, whatever its length**, checked with `strlen` after `str_chomp`; nothing left to discard.
- **`INPUT_MAX` is 400.** The server relays a message as `:nick MSG :text\r\n`, adding up to 25 bytes (16-byte nickname, `:`, ` MSG :`, `\r\n`), so a 510-byte message would be too long to relay. 400 leaves room.
- **NUL bytes.** If `getline`'s length and `strlen` differ, the line holds a NUL, which would silently cut the message, so the client refuses it.
- **`free(line)`** once, after the loop.
- **Warnings go through `log_msg`**: `parley: warning: ...`.

The file also includes `<sys/types.h>` for `ssize_t`. Its output still goes through stdio; see exercise 3.

### Testing io.c with pipes

To test descriptor code without files or a network, use a pipe: two connected descriptors in memory.

`project/ch12/tests/test_io.c`

```c
/* test_io.c - tests for io.c, using a pipe: two connected descriptors. */
#include <errno.h>
#include <unistd.h>

#include "io.h"
#include "test.h"
#include "tests.h"

void test_io_write_read(void)
{
    int fds[2];                        /* fds[0]: read end, fds[1]: write end */
    CHECK_INT(pipe(fds), 0);
    CHECK_INT(write_all(fds[1], "NICK al\r\nQUIT\r\n", 15), 0);

    char buf[16] = "";
    CHECK_INT(read_exact(fds[0], buf, 9), 9);
    CHECK_STR(buf, "NICK al\r\n");

    close(fds[1]);                     /* no writers left: EOF after the data */
    struct strbuf sb = { 0 };
    CHECK_INT(read_into(fds[0], &sb, 100), 6);
    CHECK_STR(sb.data, "QUIT\r\n");
    CHECK_INT(read_into(fds[0], &sb, 100), 0);         /* end of file */
    CHECK_INT(read_exact(fds[0], buf, 9), 0);
    sb_free(&sb);
    close(fds[0]);
}

void test_io_short_read(void)
{
    int fds[2];
    CHECK_INT(pipe(fds), 0);
    write_all(fds[1], "abc", 3);
    close(fds[1]);
    char buf[10];
    CHECK_INT(read_exact(fds[0], buf, sizeof buf), 3);  /* EOF came first */
    close(fds[0]);
}

void test_io_nonblock(void)
{
    int fds[2];
    CHECK_INT(pipe(fds), 0);
    CHECK_INT(set_nonblock(fds[0]), 0);
    CHECK_INT(set_nonblock(fds[1]), 0);

    /* Reading an empty pipe would block; non-blocking, it fails at once. */
    struct strbuf sb = { 0 };
    errno = 0;
    CHECK_INT(read_into(fds[0], &sb, 100), -1);
    CHECK(errno == EAGAIN || errno == EWOULDBLOCK);

    /* Fill the pipe: write() stops at its capacity with EAGAIN. */
    static char block[4096];
    size_t total = 0;
    ssize_t w;
    while ((w = write(fds[1], block, sizeof block)) > 0)
        total += (size_t)w;
    CHECK(errno == EAGAIN || errno == EWOULDBLOCK);
    CHECK(total >= 4096);              /* Linux: 65536 by default */
    sb_free(&sb);
    close(fds[0]);
    close(fds[1]);
}
```

- **`io_write_read`** checks the normal path, then that `read_into` and `read_exact` both return 0 once the write end is closed and the data read.
- **`io_short_read`** asks `read_exact` for 10 bytes when only 3 will come: it must return 3, not hang or fail.
- **`io_nonblock`** makes the read end non-blocking **before** reading the empty pipe, which must fail with `EAGAIN` (a test that can hang is worse than none). Then it fills the pipe, like `shortwrite.c`, checking only for at least 4096 bytes (POSIX's minimum `PIPE_BUF`), not Linux's 65536.

`tests.h` gets the three names, and the `Makefile` builds `io.o` into the server and the tests:

```diff
--- project/ch11/tests/tests.h
+++ project/ch12/tests/tests.h
@@ -27,7 +27,10 @@
     X(msg_roundtrip)                \
     X(frame_lines)                  \
     X(frame_one_byte_at_a_time)     \
-    X(frame_too_long)
+    X(frame_too_long)               \
+    X(io_write_read)                \
+    X(io_short_read)                \
+    X(io_nonblock)
 
 #define TEST_DECLARE(name) void test_##name(void);
 TESTS(TEST_DECLARE)
--- project/ch11/Makefile
+++ project/ch12/Makefile
@@ -37,10 +37,10 @@
   BUILD   := $(BUILD)-san
 endif
 
-SERVER_OBJS := $(addprefix $(BUILD)/, parleyd.o room.o conn.o msg.o nick.o strbuf.o str.o log.o)
+SERVER_OBJS := $(addprefix $(BUILD)/, parleyd.o room.o conn.o io.o msg.o nick.o strbuf.o str.o log.o)
 CLIENT_OBJS := $(addprefix $(BUILD)/, parley.o msg.o strbuf.o str.o log.o)
 TEST_OBJS   := $(addprefix $(BUILD)/, unit.o test_str.o test_nick.o test_strbuf.o \
-                 test_msg.o msg.o nick.o strbuf.o str.o)
+                 test_msg.o test_io.o io.o msg.o nick.o strbuf.o str.o)
 
 .PHONY: all test test-all clean
 
```

The client doesn't link `io.o` yet; it gets a socket, and `write_all`, in [Chapter 16](16-a-first-client.md).

### Checkpoint

All tests, both libraries, then sanitizers:

```console
$ cd project/ch12
$ make -s test-all
unit: 23 passed, 0 failed
unit: 23 passed, 0 failed
$ make -s SANITIZE=1 test
unit: 23 passed, 0 failed
$ build/glibc/unit -v io
ok   io_write_read
ok   io_short_read
ok   io_nonblock
unit: 3 passed, 0 failed
```

A session through a pipe, in the same order on both libraries:

```console
$ printf 'NICK al\r\nMSG :hi\r\nQUIT\r\n' | build/glibc/parleyd 2>&1 | cat
parleyd: info: reading one client session from stdin
:parley 001 al :Welcome to parley, al
:al JOIN
:parley 353 al :al
:parley 366 al :End of NAMES
parleyd: info: al joined
:al MSG :hi
parleyd: info: al left (Quit)
ERROR :Goodbye
parleyd: info: session over
$ printf 'NICK al\r\nMSG :hi\r\nQUIT\r\n' | build/musl/parleyd 2>&1 | cat
parleyd: info: reading one client session from stdin
:parley 001 al :Welcome to parley, al
:al JOIN
:parley 353 al :al
:parley 366 al :End of NAMES
parleyd: info: al joined
:al MSG :hi
parleyd: info: al left (Quit)
ERROR :Goodbye
parleyd: info: session over
```

One `read` for all the input, one system call per reply and per log line (musl uses `writev` for the log, still written with stdio's `fputs`):

```console
$ printf 'NICK al\r\nQUIT\r\n' | strace -o trace.txt -e trace=read,write,writev build/musl/parleyd >/dev/null 2>&1; cat trace.txt
writev(2, [{iov_base="parleyd: info: reading one clien"..., iov_len=53}], 1) = 53
read(0, "NICK al\r\nQUIT\r\n", 4096)    = 15
write(1, ":parley 001 al :Welcome to parle"..., 39) = 39
write(1, ":al JOIN\r\n", 10)            = 10
write(1, ":parley 353 al :al\r\n", 20)  = 20
write(1, ":parley 366 al :End of NAMES\r\n", 30) = 30
writev(2, [{iov_base="parleyd: info: al joined\n", iov_len=25}], 1) = 25
writev(2, [{iov_base="parleyd: info: al left (Quit)\n", iov_len=30}], 1) = 30
write(1, "ERROR :Goodbye\r\n", 16)      = 16
writev(2, [{iov_base="parleyd: info: session over\n", iov_len=28}], 1) = 28
+++ exited with 0 +++
$ printf 'NICK al\r\nQUIT\r\n' | strace -o trace.txt -e trace=read,write,writev build/glibc/parleyd >/dev/null 2>&1; cat trace.txt
read(3, "\177ELF\2\1\1\3\0\0\0\0\0\0\0\0\3\0>\0\1\0\0\0P\237\2\0\0\0\0\0"..., 832) = 832
write(2, "parleyd: info: reading one clien"..., 53) = 53
read(0, "NICK al\r\nQUIT\r\n", 4096)    = 15
write(1, ":parley 001 al :Welcome to parle"..., 39) = 39
write(1, ":al JOIN\r\n", 10)            = 10
write(1, ":parley 353 al :al\r\n", 20)  = 20
write(1, ":parley 366 al :End of NAMES\r\n", 30) = 30
write(2, "parleyd: info: al joined\n", 25) = 25
write(2, "parleyd: info: al left (Quit)\n", 30) = 30
write(1, "ERROR :Goodbye\r\n", 16)      = 16
write(2, "parleyd: info: session over\n", 28) = 28
+++ exited with 0 +++
```

Typed by hand on a terminal, the server shows its hint. The user typed `NICK al`, `MSG :typed by hand` and Ctrl-D:

```console
$ build/glibc/parleyd
parleyd: info: reading one client session from stdin
parleyd: info: type protocol lines, e.g. NICK alice (Ctrl-D ends)
NICK al
:parley 001 al :Welcome to parley, al
:al JOIN
:parley 353 al :al
:parley 366 al :End of NAMES
parleyd: info: al joined
MSG :typed by hand
:al MSG :typed by hand
parleyd: info: session over
```

The client still uses stdio, so in a pipe its order depends on the library. A 450-byte line refused between two short ones:

```console
$ { printf 'hello\n'; head -c 450 /dev/zero | tr '\0' x; printf '\nbye\n'; } | build/glibc/parley -n al 2>&1 | cat
parley: warning: line too long (max 400 bytes), not sent
NICK al
MSG :hello
MSG :bye
QUIT
$ { printf 'hello\n'; head -c 450 /dev/zero | tr '\0' x; printf '\nbye\n'; } | build/musl/parley -n al 2>&1 | cat
NICK al
parley: warning: line too long (max 400 bytes), not sent
MSG :hello
MSG :bye
QUIT
$ printf 'nul\0byte\nok\n' | build/glibc/parley -n al
NICK al
parley: warning: line contains a NUL byte, not sent
MSG :ok
QUIT
```

In the last run stdout is the terminal, line buffered, so the order is right on both.

A write error: `/dev/full` fails the first reply; the server logs it once, stops, and exits with 1:

```console
$ printf 'NICK al\r\nQUIT\r\n' | build/musl/parleyd >/dev/full; echo "exit $?"
parleyd: info: reading one client session from stdin
parleyd: error: writing to al: No space left on device
parleyd: info: al joined
exit 1
```

`al joined` appears because `room.c` finishes `NICK` after the failed `conn_send`; then the loop sees `CONN_DEAD`, so `QUIT` is never handled and there's no `session over`.

Clean up:

```console
$ rm -f trace.txt && make clean >/dev/null && ls
Makefile
src
tests
```

The complete project at this point is in `project/ch12/`.

## Summary

- stdio (`FILE *`, `printf`, `fgets`) is C99 and buffers in your process. Descriptors (`int`, `open`, `read`, `write`, `close`) are POSIX; each call is a system call. `fileno()` gives a stream's descriptor.
- 0, 1 and 2 are stdin, stdout and stderr. `open` returns the lowest free number. `/proc/self/fd` shows the table.
- `read` returns `> 0` bytes (maybe fewer than asked), `0` at end of file, or `-1` with `errno`. One `read` is not one line: buffer bytes and take out whole lines.
- End of file is a return value: for a pipe when every write end is closed, for a terminal on Ctrl-D at the start of a line.
- `write` can be short: loop with `write_all`, retry on `EINTR`, return `-1` on other errors, including `EAGAIN`.
- `O_NONBLOCK`, set with `fcntl(F_GETFL/F_SETFL)`, makes `read` and `write` fail with `EAGAIN` instead of waiting. A Linux pipe holds 65536 bytes.
- glibc calls `openat` and loads `libc.so.6` before `main`; static musl calls `open` and loads nothing.
- Streams are unbuffered, line or fully buffered: stderr unbuffered, stdout line buffered on a terminal, else fully. `fflush`, `fclose` and `exit` flush; a crash or `_exit` doesn't.
- `BUFSIZ` is 8192 on glibc, 1024 on musl, and into a pipe musl writes stdout's first line at once, so stdout/stderr interleaving differs. Never depend on it; use `setvbuf` before any output, or `fflush`.
- A buffered write error may appear only at `fflush`. Check the final `fflush` or `fclose` before exiting with 0.
- `getline` reads lines of any length into a buffer it grows; you `free` it. Its length counts NUL bytes, `strlen` doesn't. Buffer growth differs between the libraries; never rely on `cap`.
- `isatty(fd)` tells you if a person is on the other end.
- A descriptor points to an open file description (offset and flags), which points to a file. `dup` shares the description and offset; a second `open` doesn't. It lives until its last descriptor is closed.
- `fflush(stdin)` is undefined in C99: glibc ignores it, musl drops buffered pipe input. Read and discard up to the newline instead.
- The project: `io.c` (`write_all`, `read_exact`, `read_into`, `set_nonblock`); `conn` with a descriptor and a state; the server reads descriptor 0 and writes 1 directly; one `write` per log line; the client uses `getline`; 23 tests.

## Exercises

1. **mycp.** Write `mycp SRC DST [BUFSIZE]` with `open`, `read`, `write` and `close`. Create `DST` with mode `0644`, truncating it if it exists. Report errors like `mycp: nosuch: No such file or directory` and exit with 1. Use one cleanup path. Then copy a 1,000,000-byte file with buffer sizes 1, 4096 and 65536, and count the system calls with `strace -c`.

<details><summary>Solution</summary>

```c
/* mycp.c - copy a file: mycp SRC DST [BUFSIZE] */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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

int main(int argc, char *argv[])
{
    if (argc < 3 || argc > 4) {
        fprintf(stderr, "usage: mycp SRC DST [BUFSIZE]\n");
        return 2;
    }
    size_t size = argc == 4 ? strtoul(argv[3], NULL, 10) : 4096;
    if (size == 0) {
        fprintf(stderr, "mycp: bad buffer size\n");
        return 2;
    }

    int status = 1;
    int in = -1, out = -1;
    char *buf = malloc(size);
    if (buf == NULL) {
        perror("mycp");
        goto done;
    }
    in = open(argv[1], O_RDONLY);
    if (in < 0) {
        fprintf(stderr, "mycp: %s: %s\n", argv[1], strerror(errno));
        goto done;
    }
    out = open(argv[2], O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (out < 0) {
        fprintf(stderr, "mycp: %s: %s\n", argv[2], strerror(errno));
        goto done;
    }
    for (;;) {
        ssize_t r = read(in, buf, size);
        if (r == 0)
            break;
        if (r < 0) {
            if (errno == EINTR)
                continue;
            fprintf(stderr, "mycp: %s: %s\n", argv[1], strerror(errno));
            goto done;
        }
        if (write_all(out, buf, (size_t)r) < 0) {
            fprintf(stderr, "mycp: %s: %s\n", argv[2], strerror(errno));
            goto done;
        }
    }
    if (close(out) < 0) {             /* some file systems report errors only here */
        out = -1;
        fprintf(stderr, "mycp: %s: %s\n", argv[2], strerror(errno));
        goto done;
    }
    out = -1;
    status = 0;
done:
    if (in >= 0)
        close(in);
    if (out >= 0)
        close(out);
    free(buf);
    return status;
}
```

The descriptors start at `-1` so the cleanup path ([Chapter 10](10-errors-and-debugging.md)) knows which are open. `close(out)` is checked, because network file systems may report write errors only there; after a failed `close` the descriptor is gone anyway, so it is set to `-1` to avoid a second close.

```console
$ head -c 1000000 /dev/urandom > big.bin
$ ./mycp big.bin copy.bin && cmp big.bin copy.bin && echo same
same
$ ./mycp nosuch copy.bin; echo "exit $?"
mycp: nosuch: No such file or directory
exit 1
$ ./mycp big.bin /nodir/x; echo "exit $?"
mycp: /nodir/x: No such file or directory
exit 1
```

Counts from `strace -c -e trace=read,write ./mycp big.bin copy.bin SIZE` with glibc (the `read` count includes the final `read` returning 0 and the loader's read of `libc.so.6`), and the time without `strace` on one machine:

| BUFSIZE | `write` calls | `read` calls | time |
|---|---|---|---|
| 1 | 1,000,000 | 1,000,002 | 0.49 s |
| 4096 | 245 | 247 | 0.00 s |
| 65536 | 16 | 18 | 0.00 s |

Each system call has a fixed cost, however few bytes it moves: one byte at a time is a hundred times slower, and takes minutes under `strace`, which stops at every call. Past a few kilobytes a bigger buffer hardly helps, hence stdio's kilobyte `BUFSIZ`.

</details>

2. **myhexdump.** Write a program that prints stdin in `xxd`'s format: an 8-digit hex offset and a colon, 16 bytes as hex in groups of two, then the printable bytes (0x20 to 0x7e) as characters and the rest as `.`. Use `read_exact` from `io.c` (copy it into your file) so every row but the last has 16 bytes, even when input arrives in pieces. Compare with `xxd`.

<details><summary>Solution</summary>

```c
/* myhexdump.c - print stdin in hex and ASCII, in the format of xxd. */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <stdio.h>
#include <unistd.h>

/* Read exactly n bytes unless end of file comes first. Returns the count,
 * or -1 with errno set. */
static ssize_t read_exact(int fd, void *buf, size_t n)
{
    char *p = buf;
    size_t got = 0;
    while (got < n) {
        ssize_t r = read(fd, p + got, n - got);
        if (r < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        if (r == 0)
            break;
        got += (size_t)r;
    }
    return (ssize_t)got;
}

int main(void)
{
    unsigned char row[16];
    unsigned long offset = 0;
    ssize_t n;

    while ((n = read_exact(STDIN_FILENO, row, sizeof row)) > 0) {
        printf("%08lx:", offset);
        for (size_t i = 0; i < sizeof row; i++) {
            if (i % 2 == 0)
                putchar(' ');
            if (i < (size_t)n)
                printf("%02x", row[i]);
            else
                printf("  ");
        }
        printf("  ");
        for (ssize_t i = 0; i < n; i++)
            putchar(row[i] >= 0x20 && row[i] < 0x7f ? row[i] : '.');
        putchar('\n');
        offset += (unsigned long)n;
    }
    if (n < 0) {
        perror("myhexdump");
        return 1;
    }
    return 0;
}
```

The row is `unsigned char`, so bytes above 0x7f print as `80` to `ff`, not negative numbers. The output matches `xxd`'s for files, pipes and empty input, on both libraries:

```console
$ printf 'NICK al\r\nMSG :hi\r\nQUIT\r\n' | ./myhexdump
00000000: 4e49 434b 2061 6c0d 0a4d 5347 203a 6869  NICK al..MSG :hi
00000010: 0d0a 5155 4954 0d0a                      ..QUIT..
$ printf 'NICK al\r\nMSG :hi\r\nQUIT\r\n' | xxd | cmp - <(printf 'NICK al\r\nMSG :hi\r\nQUIT\r\n' | ./myhexdump) && echo same
same
```

With a plain `read` instead of `read_exact` (built here as `myhexdump-with-read`), a pipe delivering its data in two parts gives two short rows, and the second row's offset isn't a multiple of 16:

```console
$ (printf 'NICK '; sleep 0.3; printf 'al\r\n') | ./myhexdump-with-read
00000000: 4e49 434b 20                             NICK 
00000005: 616c 0d0a                                al..
$ (printf 'NICK '; sleep 0.3; printf 'al\r\n') | ./myhexdump
00000000: 4e49 434b 2061 6c0d 0a                   NICK al..
```

</details>

3. **A client with a predictable order.** In the checkpoint, the client's warning and stdout lines came out in a different order with glibc and musl. With one line in `parley.c`, make the order the same on both, matching the order of events. Then count the client's `write` calls with `strace`.

<details><summary>Solution</summary>

Make stdout line buffered right after parsing the arguments, before any output:

```c
    parse_args(argc, argv, &cfg);
    /* One write per line, even into a pipe, so our output and the
     * warnings on stderr come out in the order they happened. */
    setvbuf(stdout, NULL, _IOLBF, 0);
    log_init("parley", false);
```

```console
$ make -s && make -s LIBC=musl
$ { printf 'hello\n'; head -c 450 /dev/zero | tr '\0' x; printf '\nbye\n'; } | build/glibc/parley -n al 2>&1 | cat
NICK al
MSG :hello
parley: warning: line too long (max 400 bytes), not sent
MSG :bye
QUIT
$ { printf 'hello\n'; head -c 450 /dev/zero | tr '\0' x; printf '\nbye\n'; } | build/musl/parley -n al 2>&1 | cat
NICK al
MSG :hello
parley: warning: line too long (max 400 bytes), not sent
MSG :bye
QUIT
$ printf 'hi\n' | strace -o trace.txt -e trace=write,writev build/glibc/parley -n al > /dev/null; cat trace.txt
write(1, "NICK al\r\n", 9)              = 9
write(1, "MSG :hi\r\n", 9)              = 9
write(1, "QUIT\r\n", 6)                 = 6
+++ exited with 0 +++
```

Both libraries now print the warning between `hello` and `bye`. One `write` per message costs nothing when a person types a few lines a minute. [Chapter 16](16-a-first-client.md) sends with `write_all` to a socket instead, unbuffered anyway.

</details>

4. **mytail.** Write `mytail N`, which prints the last `N` lines of stdin (default 10). Keep the last `N` lines in a ring of `N` slots, each holding a `getline` buffer. Don't copy line contents, and don't give `getline` a new buffer for every line. Check it against `tail -n` with `seq 100`, a file without a final newline, and empty input, and run it under the sanitizers.

<details><summary>Solution</summary>

```c
/* mytail.c - print the last N lines of stdin (default 10). */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>

struct line {
    char *text;          /* from getline; NULL until used */
    size_t cap;
    ssize_t len;
};

int main(int argc, char *argv[])
{
    long n = argc > 1 ? strtol(argv[1], NULL, 10) : 10;
    if (n <= 0)
        return 0;
    struct line *ring = calloc((size_t)n, sizeof *ring);
    if (ring == NULL) {
        perror("mytail");
        return 1;
    }

    long count = 0;      /* lines read so far */
    char *buf = NULL;
    size_t cap = 0;
    ssize_t len;
    while ((len = getline(&buf, &cap, stdin)) != -1) {
        /* Swap buffers with the oldest slot: the slot gets the new line,
         * and its old buffer is reused for the next getline. */
        struct line *l = &ring[count % n];
        char *old = l->text;
        size_t old_cap = l->cap;
        l->text = buf;
        l->cap = cap;
        l->len = len;
        buf = old;
        cap = old_cap;
        count++;
    }
    free(buf);
    int status = 0;
    if (ferror(stdin)) {
        perror("mytail");
        status = 1;
    }

    long first = count > n ? count - n : 0;
    for (long i = first; i < count; i++) {
        struct line *l = &ring[i % n];
        fwrite(l->text, 1, (size_t)l->len, stdout);
    }
    for (long i = 0; i < n; i++)
        free(ring[i].text);
    free(ring);
    return status;
}
```

Line `i` lives in slot `i % n`. `getline` reads into a spare buffer, which is then swapped with the oldest slot's, so after `N` lines nothing more is allocated.

Reading straight into the slot, `ring[count % n].len = getline(&ring[count % n].text, ...)`, has a bug: the final call's `-1` lands in the slot of the oldest line you meant to print, and on both libraries the first output line came out wrong or not at all. The spare buffer avoids that.

```console
$ for k in 1 3 10 200; do cmp <(seq 100 | tail -n $k) <(seq 100 | ./mytail $k) || echo "differs for $k"; done
$ printf 'a\nb' | ./mytail 1 | od -c
0000000   b
0000001
$ printf '' | ./mytail
$ gcc -std=c99 -g -fsanitize=address,undefined -o mytail-san mytail.c
$ seq 100000 | ./mytail-san 2
99999
100000
```

No output from the loop means all four matched `tail`. musl gives the same results, and ASan reports no leaks.

</details>

5. **The descriptor quiz.** What does `quiz.txt` contain after this program? Answer first, then run it. Then add `O_APPEND` to the third `open`, and answer again.

```c
#define _POSIX_C_SOURCE 200809L
#include <fcntl.h>
#include <unistd.h>

int main(void)
{
    int a = open("quiz.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    int b = dup(a);
    int c = open("quiz.txt", O_WRONLY);
    write(a, "ab", 2);
    write(b, "cd", 2);
    write(c, "XY", 2);
    close(a);
    close(b);
    close(c);
    return 0;
}
```

<details><summary>Solution</summary>

```console
$ ./quiz && cat quiz.txt; echo
XYcd
```

`a` and `b` share one offset: `ab` goes to bytes 0–1, `cd` to bytes 2–3. `c` has its own open file description, offset still 0, so `XY` overwrites `ab`: `XYcd` on both libraries, since this is the kernel's doing.

With `O_APPEND` on the third `open`, every write on `c` first moves to the current end of the file, so `XY` goes after `cd`:

```console
$ ./quiz && cat quiz.txt; echo
abcdXY
```

That's how shared log files stay intact: each writer opens with `O_APPEND`, so none overwrites another. (The quiz ignores `write`'s result for brevity, so `-O2` on glibc gives the `warn_unused_result` warnings from the start of the chapter.)

</details>

Sources for this chapter: [REFERENCES.md](REFERENCES.md#chapter-12--files-and-file-descriptors)
