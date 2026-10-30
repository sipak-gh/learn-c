# Chapter 13 — Processes and Signals

[Chapter 12](12-files-and-descriptors.md) covered file descriptors. This chapter covers the program
itself: a running **process**. You create processes with `fork`, turn them into other programs with
`exec`, collect their exit status with `wait`, and connect them with pipes, like a shell. Then come
**signals**: small asynchronous messages the kernel sends when someone presses Ctrl-C, a child exits, or
you write to a pipe nobody reads.

The server needs little of this: it ignores SIGPIPE and turns SIGINT/SIGTERM into a clean shutdown. But
the rules behind that are the trickiest in POSIX, and here glibc and musl differ most: the old `signal()`
means different things in each. Unless a `glibc vs musl` section says otherwise, every example here
prints the same with both libraries. In [Chapter 14](14-networking-basics.md) the server meets the network.

## Processes and fork

A **process** is a running program: its memory, open file descriptors, current directory, and a number
that names it, the **process ID** (PID). Every process except the first was created by another, its
**parent**.

| call | what it does |
|---|---|
| `pid_t getpid(void)` | your own PID |
| `pid_t getppid(void)` | your parent's PID |
| `pid_t fork(void)` | makes a copy of the calling process |
| `pid_t waitpid(pid_t pid, int *status, int options)` | waits until child `pid` ends; fills in `*status` |
| `void exit(int status)` | ends the process (after cleanup, see [atexit](#cleanup-at-exit-atexit-and-_exit)) |

`fork()` is called once but **returns twice**: in the parent and in the new child. Both run the same code
from the same point; only the return value differs:

| `fork()` returns | you are in |
|---|---|
| `0` | the child |
| a PID greater than 0 | the parent (the value is the child's PID) |
| `-1` | the parent, and no child was made (`errno` says why) |

The child gets a **copy** of the parent's memory. After the fork, neither sees the other's changes.

`examples/ch13/processes.c`:

```c
/* processes.c - fork() makes a copy of the running process. */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

int main(void)
{
    int x = 1;
    printf("parent: pid %d, my parent is %d\n", (int)getpid(), (int)getppid());
    fflush(stdout);                     /* see "fork and stdio buffers" */

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return 1;
    }
    if (pid == 0) {                     /* in the child: fork returned 0 */
        x = 100;                        /* changes the child's copy only */
        printf("child:  pid %d, my parent is %d, x = %d\n",
               (int)getpid(), (int)getppid(), x);
        exit(7);
    }

    /* in the parent: fork returned the child's pid */
    int status;
    if (waitpid(pid, &status, 0) < 0) {
        perror("waitpid");
        return 1;
    }
    printf("parent: child %d exited with status %d, x = %d\n",
           (int)pid, WEXITSTATUS(status), x);
    return 0;
}
```

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o processes processes.c
$ ./processes
parent: pid 217881, my parent is 217877
child:  pid 217882, my parent is 217881, x = 100
parent: child 217882 exited with status 7, x = 1
```

Your PIDs will differ. Build the musl version with `musl-gcc ... -static -o processes-musl processes.c`.
Notice:

- the child's parent is the process that forked it (217881);
- the child set `x` to 100, but the parent still sees 1: separate memory;
- the child's `exit(7)` reaches the parent through `waitpid`.

`pid_t` is a signed integer of unknown size, so cast it to `int` (or `long`) for `printf`.

Gotchas:

- Both processes share the terminal, so their output can interleave in any order. Here the parent waits
  first, so the order is fixed.
- The child inherits **copies of all open file descriptors**. Each copy points at the same open file, so
  they share its offset ([Chapter 12](12-files-and-descriptors.md)). This matters for pipes below.

## fork and stdio buffers

Why `fflush(stdout)` before `fork()`? `printf` fills a buffer in your process's memory instead of writing
at once ([Chapter 12](12-files-and-descriptors.md)). `fork()` copies all memory, **including that
buffer**. If it isn't empty, both processes later flush the same text.

`examples/ch13/forkbuf.c`:

```c
/* forkbuf.c - fork() copies the stdio buffers too. */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <sys/wait.h>
#include <unistd.h>

int main(void)
{
    printf("line 1, before fork\n");    /* may still sit in the buffer... */
    printf("line 2, before fork\n");
    pid_t pid = fork();                 /* ...and then there are two buffers */
    if (pid < 0)
        return 1;
    printf("after fork, in the %s\n", pid == 0 ? "child" : "parent");
    if (pid > 0)
        waitpid(pid, NULL, 0);
    return 0;                           /* each process flushes its copy */
}
```

On a terminal, stdout is **line buffered**: each `\n` flushes, so the buffer is empty at the fork.

```console
$ ./forkbuf
line 1, before fork
line 2, before fork
after fork, in the parent
after fork, in the child
```

(The last two lines may swap.) Pipe the output instead: stdout is then **fully buffered**, and the two
libraries differ.

### glibc vs musl: stdio buffers copied by fork

```console
$ ./forkbuf | cat
line 1, before fork
line 2, before fork
after fork, in the child
line 1, before fork
line 2, before fork
after fork, in the parent
$ ./forkbuf-musl | cat
line 1, before fork
line 2, before fork
after fork, in the child
line 2, before fork
after fork, in the parent
```

| | glibc | musl |
|---|---|---|
| lines printed twice | both lines | only line 2 |
| why | checks "is it a terminal?" at the first `printf`, before writing; nothing is written until exit | starts line buffered and checks at the first write; line 1 goes out at once, then it switches to full buffering |

Neither is wrong: both duplicate whatever was still buffered. The fix is the same for both; always apply
it:

- call `fflush(stdout)` (or `fflush(NULL)` for all streams) **before** `fork()`, or
- end the child with `_exit()` instead of `exit()`; `_exit` doesn't flush stdio (see
  [below](#cleanup-at-exit-atexit-and-_exit)).

## exec, wait and exit status

`fork` gives you two copies of the **same** program. To run a **different** one, the child calls an
`exec` function. `exec` replaces the program in the current process: new code, new memory, same PID, same
open file descriptors. On success it never returns.

| function | how you pass arguments | searches `PATH`? |
|---|---|---|
| `execv(path, argv)` | an array, ending with `NULL` | no |
| `execvp(file, argv)` | an array, ending with `NULL` | yes |
| `execl(path, arg0, arg1, ..., (char *)NULL)` | a list | no |
| `execlp(file, arg0, arg1, ..., (char *)NULL)` | a list | yes |

The parent then waits with `waitpid` and decodes `status` with macros from `<sys/wait.h>`:

| macro | meaning |
|---|---|
| `WIFEXITED(s)` | true if the child called `exit`/`_exit` or returned from `main` |
| `WEXITSTATUS(s)` | that exit status, 0–255 |
| `WIFSIGNALED(s)` | true if a signal killed the child |
| `WTERMSIG(s)` | that signal's number |

`examples/ch13/run.c` is a tiny version of what a shell does for each command:

```c
/* run.c - run a command, wait for it, and explain how it ended.
 * usage: run COMMAND [ARG...] */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

int main(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr, "usage: run COMMAND [ARG...]\n");
        return 2;
    }

    pid_t pid = fork();
    if (pid < 0) {
        perror("run: fork");
        return 1;
    }
    if (pid == 0) {
        /* The child becomes the command. execvp searches PATH and only
         * returns if it failed. */
        execvp(argv[1], &argv[1]);
        fprintf(stderr, "run: %s: %s\n", argv[1], strerror(errno));
        _exit(errno == ENOENT ? 127 : 126);  /* the shell's conventions */
    }

    int status;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) {
            perror("run: waitpid");
            return 1;
        }
    }
    if (WIFEXITED(status)) {
        printf("run: %s exited with status %d\n", argv[1], WEXITSTATUS(status));
        return WEXITSTATUS(status);
    }
    if (WIFSIGNALED(status)) {
        int sig = WTERMSIG(status);
        printf("run: %s was killed by signal %d (%s)\n", argv[1], sig, strsignal(sig));
        return 128 + sig;                /* again the shell's convention */
    }
    return 1;
}
```

`&argv[1]` is the tail of our own argument array, e.g. `{"echo", "hello", NULL}`. `argv` always ends with
a null pointer, just what `execvp` needs.

```console
$ ./run echo hello
hello
run: echo exited with status 0
$ ./run ls /nonexistent
ls: cannot access '/nonexistent': No such file or directory
run: ls exited with status 2
$ ./run nosuchcmd
run: nosuchcmd: No such file or directory
run: nosuchcmd exited with status 127
$ ./run /etc/passwd
run: /etc/passwd: Permission denied
run: /etc/passwd exited with status 126
$ ./run sh -c 'kill -TERM $$'
run: sh was killed by signal 15 (Terminated)
$ echo $?
143
```

`run` copies the shell's conventions:

| exit status | meaning |
|---|---|
| 0 | success |
| 1–125 | the program's own failure codes |
| 126 | found but couldn't be run (`EACCES`: not executable) |
| 127 | not found (`ENOENT`) |
| 128 + N | killed by signal N (143 = 128 + 15, SIGTERM) |

Why two calls, not one "spawn"? So the child can change its own setup in between: redirect file
descriptors, change directory, reset signals. The shell does exactly that for `>`, `<` and `|`. POSIX
also has `posix_spawn` (fork+exec in one call, with a fixed menu of such changes), but fork+exec is
simpler to learn and can do anything.

Gotchas:

- After a failed exec, the child must **end** with `_exit`. Otherwise two copies of your program run the
  code after the fork.
- Save `errno` before calling anything else: `fprintf` may change it. Here `_exit` reads `errno` after
  `fprintf`; that works because a successful `fprintf` doesn't set `errno`, but don't rely on it in bigger
  code.

### glibc vs musl: strsignal texts

`strsignal(sig)` describes a signal number. Most texts match, but not all:

```console
$ ./run sh -c 'kill -FPE $$'
run: sh was killed by signal 8 (Floating point exception)
$ ./run-musl sh -c 'kill -FPE $$'
run: sh was killed by signal 8 (Arithmetic exception)
```

All differences for signals 1–31, plus an invalid number:

| `strsignal(n)` | glibc | musl |
|---|---|---|
| 8 (SIGFPE) | `Floating point exception` | `Arithmetic exception` |
| 17 (SIGCHLD) | `Child exited` | `Child process status` |
| 99 (invalid) | `Unknown signal 99` | `Unknown signal` |

musl's SIGFPE text is more accurate (integer division by zero raises it too). glibc's "Unknown signal 99"
is more helpful. Either way, **don't compare these strings in code**: test the number. Print the number
next to the text, as `run.c` does, so logs are clear on both.

## Zombies and orphans

When a child exits, the kernel can't discard it yet: the parent may still ask for its exit status. The dead
child stays in the process table as a **zombie** until the parent calls `wait`/`waitpid`. Then it's
**reaped**: gone for good.

`examples/ch13/zombie.c`:

```c
/* zombie.c - a child that has exited but was not waited for yet. */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

int main(void)
{
    pid_t pid = fork();
    if (pid < 0)
        return 1;
    if (pid == 0)
        _exit(0);                       /* the child ends at once */

    sleep(2);                           /* the child is a zombie meanwhile */
    waitpid(pid, NULL, 0);              /* reaped: the zombie is gone */
    printf("reaped %d\n", (int)pid);
    return 0;
}
```

```console
$ ./zombie & sleep 0.5; ps -o pid,ppid,stat,comm --ppid $!
    PID    PPID STAT COMMAND
 217904  217902 Z    zombie <defunct>
$ reaped 217904
```

`Z` and `<defunct>` mark the zombie. It uses no memory, only a process table slot and its PID. A
long-running server that forks children and never waits fills the table with them. The clean way to reap
is in [SIGCHLD](#sigchld-reaping-children-without-blocking).

The opposite is an **orphan**: a child whose parent exits first. It's harmless:

| | zombie | orphan |
|---|---|---|
| who is dead | the child | the parent |
| who is still running | the parent (it hasn't called `wait`) | the child |
| fixed by | the parent calling `wait`/`waitpid` | nothing: `init` (PID 1) or a "subreaper" such as `systemd --user` adopts and later reaps it |

## Pipes and dup2

A shell runs `printf ... | sort` by joining two processes with a **pipe**: a kernel buffer with a write
end and a read end, both file descriptors.

| call | what it does |
|---|---|
| `int pipe(int fds[2])` | makes a pipe: `fds[0]` reads, `fds[1]` writes |
| `int dup2(int old, int new)` | makes fd `new` refer to the same file as `old` (closing `new` first) |

Each child uses `dup2` to put a pipe end in place of its stdin (0) or stdout (1), then calls `exec`. The
new program knows nothing of the pipe; it just reads fd 0 or writes fd 1.

`examples/ch13/pipeline.c`:

```c
/* pipeline.c - run "A | B" like a shell: A's stdout feeds B's stdin.
 * usage: pipeline CMD-A [ARG...] '|' CMD-B [ARG...] */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

/* Start argv[0] in a child whose fd `target` (0 or 1) is `fd`. */
static pid_t spawn(char **argv, int fd, int target, int fds[2])
{
    pid_t pid = fork();
    if (pid != 0)
        return pid;                     /* parent (or -1 on error) */
    if (dup2(fd, target) < 0)           /* now fd `target` IS the pipe end */
        _exit(126);
    close(fds[0]);                      /* close every original pipe fd, */
    close(fds[1]);                      /* or the reader never sees EOF */
    execvp(argv[0], argv);
    fprintf(stderr, "pipeline: %s: %s\n", argv[0], strerror(errno));
    _exit(127);
}

static void report(const char *name, int status)
{
    if (WIFEXITED(status))
        fprintf(stderr, "%s: exit %d\n", name, WEXITSTATUS(status));
    else if (WIFSIGNALED(status))
        fprintf(stderr, "%s: killed by signal %d (%s)\n", name,
                WTERMSIG(status), strsignal(WTERMSIG(status)));
}

int main(int argc, char *argv[])
{
    int bar = 1;
    while (bar < argc && strcmp(argv[bar], "|") != 0)
        bar++;
    if (bar == 1 || bar >= argc - 1) {
        fprintf(stderr, "usage: pipeline CMD-A [ARG...] '|' CMD-B [ARG...]\n");
        return 2;
    }
    argv[bar] = NULL;                   /* ends A's argument list */
    char **a = &argv[1], **b = &argv[bar + 1];

    int fds[2];                         /* fds[0]: read end, fds[1]: write end */
    if (pipe(fds) < 0) {
        perror("pipe");
        return 1;
    }
    pid_t pa = spawn(a, fds[1], STDOUT_FILENO, fds);
    pid_t pb = spawn(b, fds[0], STDIN_FILENO, fds);
    close(fds[0]);                      /* the parent uses neither end */
    close(fds[1]);
    if (pa < 0 || pb < 0) {
        perror("fork");
        return 1;
    }

    int sa, sb;
    waitpid(pa, &sa, 0);
    waitpid(pb, &sb, 0);
    report(a[0], sa);
    report(b[0], sb);
    return WIFEXITED(sb) ? WEXITSTATUS(sb) : 1;
}
```

The `'|'` is quoted so your shell passes it to the program instead of making its own pipe.

```console
$ ./pipeline printf 'pear\napple\nfig\n' '|' sort
apple
fig
pear
printf: exit 0
sort: exit 0
$ ./pipeline yes '|' head -n 3
y
y
y
yes: killed by signal 13 (Broken pipe)
head: exit 0
$ ./pipeline nosuch '|' wc -l
pipeline: nosuch: No such file or directory
0
nosuch: exit 127
wc: exit 0
```

`yes` writes forever. Once `head` has 3 lines it exits; the pipe has no reader, and `yes`'s next write
gets it killed by **SIGPIPE**. That's how `yes | head` stops. More [below](#sigpipe).

Gotcha: **close every pipe end you don't use.** A reader sees end of file only when *all* write ends are
closed, in every process. Delete the parent's `close(fds[1])` and `sort` waits forever, because the parent
still holds a write end:

```console
$ timeout 2 ./pipeline printf 'pear\napple\nfig\n' '|' sort; echo "exit $?"
exit 124
```

(`timeout` killed it after 2 seconds; 124 is its "timed out" status.)

## Signals

A **signal** is a small asynchronous notification to a process: just a number, no data. Signals come from:

- **the kernel**: SIGSEGV for a bad memory access, SIGPIPE for writing to a pipe or socket with no
  reader, SIGCHLD when a child ends, SIGALRM when an `alarm()` timer runs out;
- **the terminal**: Ctrl-C sends SIGINT, Ctrl-\ sends SIGQUIT, Ctrl-Z sends SIGTSTP;
- **other processes**: the `kill` command, or the `kill(pid, sig)` function;
- **yourself**: `raise(sig)`.

Each signal has a **default action**. For most you can change it: ignore the signal, or **catch** it with
a function of yours, a **handler**. SIGKILL and SIGSTOP can't be caught or ignored, so you can always stop
a process.

| signal | number (Linux x86-64) | default action | typical cause |
|---|---|---|---|
| SIGHUP | 1 | terminate | the terminal closed |
| SIGINT | 2 | terminate | Ctrl-C |
| SIGQUIT | 3 | core dump | Ctrl-\ |
| SIGABRT | 6 | core dump | `abort()`, a failed `assert` |
| SIGFPE | 8 | core dump | integer division by zero |
| SIGKILL | 9 | terminate (can't be caught) | `kill -9` |
| SIGUSR1, SIGUSR2 | 10, 12 | terminate | yours to use |
| SIGSEGV | 11 | core dump | a bad pointer |
| SIGPIPE | 13 | terminate | write with no reader |
| SIGALRM | 14 | terminate | `alarm()` timer |
| SIGTERM | 15 | terminate | `kill` (the default signal it sends) |
| SIGCHLD | 17 | ignore | a child ended |
| SIGSTOP | 19 | stop (can't be caught) | `kill -STOP` |

In code, use the names, never the numbers: numbers differ between systems (SIGCHLD is 20 on macOS).
`kill -l` lists them. A process killed by signal N ends with shell status 128 + N, as `run` showed.

### Catching a signal with sigaction

`sigaction` installs a handler:

```c
int sigaction(int sig, const struct sigaction *act, struct sigaction *oldact);
```

| field of `struct sigaction` | meaning |
|---|---|
| `sa_handler` | your function `void f(int signo)`, or `SIG_IGN` (ignore), or `SIG_DFL` (default action) |
| `sa_mask` | extra signals to block while the handler runs (set with `sigemptyset`/`sigaddset`) |
| `sa_flags` | options; `0`, or for example `SA_RESTART` ([below](#eintr-and-sa_restart)) |

`examples/ch13/catch.c`:

```c
/* catch.c - catch SIGINT and SIGTERM with sigaction() and stop cleanly. */
#define _POSIX_C_SOURCE 200809L
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

/* volatile: the value may change "behind the compiler's back".
 * sig_atomic_t: an integer the CPU reads and writes in one step. */
static volatile sig_atomic_t got_signal = 0;

static void on_signal(int signo)
{
    got_signal = signo;                 /* nothing else: see the text */
}

int main(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_signal;          /* or SIG_IGN, or SIG_DFL */
    sigemptyset(&sa.sa_mask);           /* block no extra signals in the handler */
    sa.sa_flags = 0;
    if (sigaction(SIGINT, &sa, NULL) < 0 || sigaction(SIGTERM, &sa, NULL) < 0) {
        perror("sigaction");
        return 1;
    }

    printf("pid %d: working; press Ctrl-C or send SIGTERM\n", (int)getpid());
    fflush(stdout);
    int ticks = 0;
    while (got_signal == 0) {
        sleep(1);                       /* a signal cuts the sleep short */
        if (got_signal == 0)
            printf("tick %d\n", ++ticks);
        fflush(stdout);
    }
    printf("got signal %d (%s): saving work and exiting\n",
           (int)got_signal, strsignal(got_signal));
    return 0;
}
```

Run it and press Ctrl-C after two ticks, or let the shell send SIGINT after 2.5 seconds (`$!` is the PID
of the last background job):

```console
$ ./catch & sleep 2.5; kill -INT $!; wait $!; echo "exit $?"
pid 237286: working; press Ctrl-C or send SIGTERM
tick 1
tick 2
got signal 2 (Interrupt): saving work and exiting
exit 0
```

Without the handler, Ctrl-C kills the program at once (status 130 = 128 + 2). With it, the program
decides when and how to stop.

`sigaction` needs `_POSIX_C_SOURCE`: under plain `-std=c99`, `<signal.h>` declares only the ISO C parts
(`signal`, `raise`, `sig_atomic_t`).

### What a handler may do: async-signal-safety

A handler can run **between any two instructions** of your program. Say the main code is halfway through
updating the stdout buffer inside `printf`, and the handler calls `printf` too: the buffer is corrupted.
The same goes for `malloc` (a half-updated free list) and most library functions.

So POSIX lists the **async-signal-safe** functions: safe to call from a handler. The list is short. Safe,
among those you know: `write`, `read`, `close`, `_exit`, `kill`, `raise`, `signal`, `sigaction`,
`waitpid`. Not safe: `printf`, `fprintf`, `malloc`, `free`, `exit`, `strsignal`, `strerror`.

The safe pattern, used throughout this course:

1. the handler **only sets a flag**;
2. the main loop checks the flag and does the real work (log, clean up, exit) in normal code.

The flag has a special type:

- `sig_atomic_t`: an integer read or written in a single step, so the main code never sees half an
  update;
- `volatile`: tells the compiler the value can change without a visible assignment. Without it, the optimizer
  may read `got_signal` once, keep it in a register, and loop forever.

If the handler calls something that may set `errno` (such as `write`), save and restore `errno`, or the
main code may see a wrong value. The [self-pipe](#the-self-pipe-trick) example does this.

### glibc vs musl: signal()

ISO C has an older, simpler function, `signal(sig, handler)`. Its meaning was never fixed; two traditions
exist:

| | System V semantics | BSD semantics |
|---|---|---|
| after the handler runs once | reset to `SIG_DFL` | stays installed |
| a `read()` blocked when the signal arrives | fails with EINTR | is restarted |

`examples/ch13/signal-semantics.c` tests both questions:

```c
/* signal-semantics.c - what does plain signal() give you? */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static volatile sig_atomic_t usr1_count;
static int pipefd[2];

static void on_usr1(int sig)
{
    (void)sig;
    usr1_count++;
}

static void on_alrm(int sig)
{
    (void)sig;
    if (write(pipefd[1], "x", 1) < 0) { /* data arrives while read() is interrupted */ }
}

int main(void)
{
    char c;
    ssize_t n;

    /* 1. Is a blocking read() restarted after the handler returns? */
    if (pipe(pipefd) != 0)
        return 1;
    signal(SIGALRM, on_alrm);
    alarm(1);
    n = read(pipefd[0], &c, 1);
    if (n < 0)
        printf("read: -1, errno=%s (not restarted)\n", strerror(errno));
    else
        printf("read: %d byte(s) (restarted after the handler)\n", (int)n);
    fflush(stdout);

    /* 2. Does the handler stay installed after it runs once? */
    signal(SIGUSR1, on_usr1);
    raise(SIGUSR1);
    printf("after 1st SIGUSR1: count=%d\n", (int)usr1_count);
    fflush(stdout);
    raise(SIGUSR1);
    printf("after 2nd SIGUSR1: count=%d (handler stayed installed)\n", (int)usr1_count);

    return 0;
}
```

```console
$ ./signal-semantics
read: -1, errno=Interrupted system call (not restarted)
after 1st SIGUSR1: count=1
User defined signal 1
$ echo $?
138
$ ./signal-semantics-musl
read: 1 byte(s) (restarted after the handler)
after 1st SIGUSR1: count=1
after 2nd SIGUSR1: count=2 (handler stayed installed)
$ echo $?
0
```

With glibc the handler was removed after the first SIGUSR1, so the second one killed the program
(138 = 128 + 10; "User defined signal 1" is your shell reporting it). `strace` shows what each library
asked the kernel for:

```console
$ strace -e trace=rt_sigaction ./signal-semantics 2>&1 | grep USR1
rt_sigaction(SIGUSR1, {sa_handler=0x5fb504aa0289, sa_mask=[], sa_flags=SA_RESTORER|SA_INTERRUPT|SA_NODEFER|SA_RESETHAND|0xffffffff00000000, sa_restorer=0x7205c8642520}, {sa_handler=SIG_DFL, sa_mask=[], sa_flags=0}, 8) = 0
$ strace -e trace=rt_sigaction ./signal-semantics-musl 2>&1 | grep USR1
rt_sigaction(SIGUSR1, {sa_handler=0x401159, sa_mask=[], sa_flags=SA_RESTORER|SA_RESTART, sa_restorer=0x404b07}, {sa_handler=SIG_DFL, sa_mask=[], sa_flags=0}, 8) = 0
```

| `signal()` | glibc | musl |
|---|---|---|
| with `-std=c99 -D_POSIX_C_SOURCE=...` (this course) | System V: `SA_RESETHAND`, no restart | BSD: `SA_RESTART`, stays installed |
| with `_DEFAULT_SOURCE` or `_GNU_SOURCE`, or `-std=gnu99` | BSD | BSD |

glibc picks the semantics from your feature test macros ([Chapter 9](09-preprocessor-and-building.md)):
compile the same file with `-D_DEFAULT_SOURCE` and glibc behaves like musl. musl is simpler: `signal()`
always means one thing. But the portable answer is **don't use `signal()` to install handlers**. Use
`sigaction`, where you choose the flags yourself; it behaves the same on both libraries and every POSIX
system. (`signal(sig, SIG_IGN)` and `signal(sig, SIG_DFL)` are fine on both: there's no handler to reset.)

### glibc vs musl: the signal restorer

Every `rt_sigaction` call above has `SA_RESTORER` and a `sa_restorer` address you never set. A returning
handler must return into a tiny piece of code that asks the kernel to resume the interrupted program (the
`rt_sigreturn` system call). The C library supplies it. The address shows where it lives:

| | glibc (dynamic) | musl (static) |
|---|---|---|
| `sa_restorer` | `0x7205c8642520`: inside `libc.so`, mapped high in memory | `0x404b07`: inside your own binary |

Same mechanism; only the linking differs ([Chapter 22](22-shipping-and-libc-choices.md)). On glibc the
addresses change between runs because of address space layout randomization.

## EINTR and SA_RESTART

What happens to a **blocking** call, such as a `read()` waiting for input, when a signal's handler runs?
The `SA_RESTART` flag decides:

- **without** it: the call fails with `-1` and `errno == EINTR` ("Interrupted system call");
- **with** it: the kernel restarts the call after the handler; your code never notices.

`examples/ch13/restart.c`:

```c
/* restart.c - what happens to a blocked read() when a signal arrives?
 * usage: restart          (sa_flags = 0)
 *        restart -r       (sa_flags = SA_RESTART) */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static void on_alarm(int signo)
{
    (void)signo;                        /* just interrupt the read */
}

int main(int argc, char *argv[])
{
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_alarm;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = (argc > 1 && strcmp(argv[1], "-r") == 0) ? SA_RESTART : 0;
    sigaction(SIGALRM, &sa, NULL);

    alarm(1);                           /* SIGALRM in 1 second */
    char buf[64];
    ssize_t n = read(STDIN_FILENO, buf, sizeof buf);
    if (n < 0)
        printf("read failed: %s\n", strerror(errno));
    else
        printf("read returned %d bytes\n", (int)n);
    return 0;
}
```

The input arrives after 2 seconds; the alarm rings after 1:

```console
$ (sleep 2; echo hi) | ./restart
read failed: Interrupted system call
$ (sleep 2; echo hi) | ./restart -r
read returned 3 bytes
```

Same on both libraries: `sigaction` gives you exactly the flags you set.

Which do you want?

| you want | use |
|---|---|
| the signal to **wake up** a blocked loop (to quit) | no `SA_RESTART`; treat EINTR as "check the flags" |
| the signal to **not disturb** blocking calls | `SA_RESTART` |

The server wants the first: Ctrl-C must wake it from a blocked `read()` so it can shut down. So every
blocking call in the program must expect EINTR. That's why `write_all` ([Chapter
12](12-files-and-descriptors.md)) retries on EINTR.

Gotcha: `SA_RESTART` doesn't cover every call. On Linux, `poll()`, `select()` and `nanosleep()` always fail
with EINTR, whatever the flags (see signal(7)), and `sleep()` returns early. Code that waits must handle
EINTR anyway.

## SIGPIPE

Write to a pipe (or, later, a socket) whose read end is closed everywhere, and the kernel sends you
**SIGPIPE**. By default it kills the process. Good for `yes | head`; terrible for a server, where one
client disconnecting at the wrong moment would kill the whole server.

`examples/ch13/sigpipe.c`:

```c
/* sigpipe.c - write to a pipe nobody reads.
 * usage: sigpipe           (default action: SIGPIPE kills us)
 *        sigpipe ignore    (SIGPIPE ignored: write fails with EPIPE) */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char *argv[])
{
    if (argc > 1 && strcmp(argv[1], "ignore") == 0) {
        struct sigaction sa;
        memset(&sa, 0, sizeof sa);
        sa.sa_handler = SIG_IGN;
        sigemptyset(&sa.sa_mask);
        sigaction(SIGPIPE, &sa, NULL);
    }

    int fds[2];
    if (pipe(fds) < 0)
        return 1;
    close(fds[0]);                      /* the only reader is gone */

    fprintf(stderr, "writing...\n");
    if (write(fds[1], "hello", 5) < 0)
        fprintf(stderr, "write: %s (errno %d)\n", strerror(errno), errno);
    fprintf(stderr, "still alive\n");
    return 0;
}
```

```console
$ ./sigpipe; echo "exit $?"
writing...
exit 141
$ ./sigpipe ignore; echo "exit $?"
writing...
write: Broken pipe (errno 32)
still alive
exit 0
```

141 = 128 + 13: silently killed by SIGPIPE. With SIGPIPE ignored, `write` fails with `EPIPE`, and the
program handles it like any other error. Servers almost always ignore SIGPIPE.

## SIGCHLD: reaping children without blocking

`waitpid(pid, &status, 0)` blocks until that child ends. A program with several children and other work
to do can't block like that. Two tools help:

- the kernel sends the parent **SIGCHLD** whenever a child ends;
- `waitpid(-1, &status, WNOHANG)` reaps **any** finished child and returns at once: its PID, or `0` if
  children exist but none has finished, or `-1` (ECHILD) if none are left.

Signals of the same kind **don't queue**: if three children end while SIGCHLD is pending, you get one
signal. So the flag means "at least one child ended", and you loop `waitpid(WNOHANG)` until it returns 0
or -1.

One more trap. Look at this loop:

```c
while (child_exited == 0)
    pause();                            /* sleep until a signal arrives */
```

If SIGCHLD arrives **after** the check but **before** `pause()`, the handler sets the flag, and `pause()`
then sleeps forever waiting for a signal that already came. This is a **race condition**. The fix:

1. **block** SIGCHLD with `sigprocmask` (a blocked signal stays "pending" until unblocked);
2. check the flag;
3. call `sigsuspend(&old_mask)`, which unblocks the signal **and** sleeps in one atomic step, so nothing
   can slip in between.

`examples/ch13/reap.c`:

```c
/* reap.c - start children, collect each one as it exits (SIGCHLD). */
#define _POSIX_C_SOURCE 200809L
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static volatile sig_atomic_t child_exited = 0;

static void on_child(int signo)
{
    (void)signo;
    child_exited = 1;
}

int main(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_child;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGCHLD, &sa, NULL);

    /* Block SIGCHLD while we check the flag, so it can't slip in between
     * the check and the wait (see sigsuspend below). */
    sigset_t block, old;
    sigemptyset(&block);
    sigaddset(&block, SIGCHLD);
    sigprocmask(SIG_BLOCK, &block, &old);

    const char *naps[] = { "0.3", "0.1", "0.2" };
    int running = 0;
    for (int i = 0; i < 3; i++) {
        pid_t pid = fork();
        if (pid == 0) {
            sigprocmask(SIG_SETMASK, &old, NULL);   /* the child: undo */
            execlp("sleep", "sleep", naps[i], (char *)NULL);
            _exit(127);
        }
        if (pid > 0) {
            printf("started sleep %s\n", naps[i]);
            running++;
        }
    }

    while (running > 0) {
        while (child_exited == 0)
            sigsuspend(&old);           /* unblock + wait, in one step */
        child_exited = 0;

        /* One SIGCHLD may stand for several exits: reap all that are done.
         * WNOHANG: return 0 at once instead of waiting for a live child. */
        int status;
        pid_t pid;
        while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
            printf("reaped child %d, exit %d\n", (int)pid, WEXITSTATUS(status));
            running--;
        }
    }
    printf("no children left\n");
    return 0;
}
```

```console
$ ./reap
started sleep 0.3
started sleep 0.1
started sleep 0.2
reaped child 205505, exit 0
reaped child 205506, exit 0
reaped child 205504, exit 0
no children left
```

Children are reaped in the order they finish (0.1 s, 0.2 s, 0.3 s), not the order they started. The
signal mask is inherited across `fork` and `exec`, so each child restores the old mask before running
`sleep`.

## The self-pipe trick

`sigsuspend` waits for a signal. A server waits for **input** on file descriptors. How do you wait for
either? The classic answer is the **self-pipe trick**:

1. make a pipe;
2. the handler writes one byte into it (`write` is async-signal-safe);
3. the main loop waits for the read end to become readable **together with** the other descriptors.

The signal becomes just another input. This works with `poll()`, which sleeps until one of several
descriptors is ready. [Chapter 18](18-event-loop.md) covers `poll` properly; for now, it fills in
`revents` for each ready descriptor.

`examples/ch13/selfpipe.c`:

```c
/* selfpipe.c - wait for input OR a signal, without a race.
 * Echoes stdin lines until end of file or SIGINT/SIGTERM. */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int wake_fds[2];                 /* the "self-pipe" */

static void on_signal(int signo)
{
    int saved = errno;                  /* write() may change errno */
    unsigned char b = (unsigned char)signo;
    if (write(wake_fds[1], &b, 1) < 0) {
        /* pipe full: a wake-up is already waiting, nothing to do */
    }
    errno = saved;
}

int main(void)
{
    if (pipe(wake_fds) < 0)
        return 1;
    /* Non-blocking, so the handler can never get stuck on a full pipe. */
    fcntl(wake_fds[1], F_SETFL, fcntl(wake_fds[1], F_GETFL) | O_NONBLOCK);

    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_signal;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    /* poll() (Chapter 18) sleeps until one of several fds is readable. */
    struct pollfd pfd[2] = {
        { .fd = STDIN_FILENO, .events = POLLIN },
        { .fd = wake_fds[0], .events = POLLIN },
    };
    for (;;) {
        if (poll(pfd, 2, -1) < 0) {
            if (errno == EINTR)
                continue;               /* the pipe will tell us why */
            perror("poll");
            return 1;
        }
        if (pfd[1].revents & POLLIN) {
            unsigned char b;
            if (read(wake_fds[0], &b, 1) == 1)
                printf("signal %d (%s): bye\n", b, strsignal(b));
            return 0;
        }
        if (pfd[0].revents & (POLLIN | POLLHUP)) {
            char buf[256];
            ssize_t n = read(STDIN_FILENO, buf, sizeof buf);
            if (n <= 0) {
                printf("end of input\n");
                return 0;
            }
            printf("read %d bytes: %.*s", (int)n, (int)n, buf);
            fflush(stdout);
        }
    }
}
```

Feed it one line, then send SIGINT half a second later while it waits for more:

```console
$ { echo hello; sleep 1; echo late; } | ./selfpipe & sleep 0.5; kill -INT $!
read 6 bytes: hello
signal 2 (Interrupt): bye
$ printf 'a\nb\n' | ./selfpipe
read 4 bytes: a
b
end of input
```

The `printf` and `strsignal` run in the main loop, not the handler. (`%.*s` prints exactly `n` bytes: the
buffer has no `'\0'`.) The second run gets both lines in one `read`: a read returns whatever is there,
not one line. You'll meet this again with sockets.

Linux also has `signalfd()` for this job, but it's not POSIX, so this course uses the self-pipe.

## Cleanup at exit: atexit and _exit

`atexit(f)` registers a function to run when the program ends normally. Not every way of ending a
process runs the same cleanup:

`examples/ch13/atexit.c`:

```c
/* atexit.c - functions that run when the program exits normally.
 * usage: atexit [return | exit | _exit | signal] */
#define _POSIX_C_SOURCE 200809L
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void first(void)  { printf("cleanup 1 (registered first)\n"); }
static void second(void) { printf("cleanup 2 (registered second)\n"); }

int main(int argc, char *argv[])
{
    const char *how = argc > 1 ? argv[1] : "return";
    atexit(first);
    atexit(second);
    printf("ending with %s\n", how);    /* no fflush: watch what survives */

    if (strcmp(how, "exit") == 0)
        exit(0);                        /* handlers run, stdio is flushed */
    if (strcmp(how, "_exit") == 0)
        _exit(0);                       /* straight to the kernel */
    if (strcmp(how, "signal") == 0)
        raise(SIGTERM);                 /* killed: nothing runs */
    return 0;                           /* same as exit(0) */
}
```

```console
$ ./atexit return
ending with return
cleanup 2 (registered second)
cleanup 1 (registered first)
$ ./atexit exit
ending with exit
cleanup 2 (registered second)
cleanup 1 (registered first)
$ ./atexit _exit
ending with _exit
$ ./atexit _exit | cat
$ ./atexit signal; echo "exit $?"
ending with signal
Terminated
exit 143
```

| how the process ends | atexit handlers | stdio buffers flushed |
|---|---|---|
| `return` from `main` | yes, in reverse order | yes |
| `exit(n)` | yes, in reverse order | yes |
| `_exit(n)` | no | no |
| killed by a signal | no | no |

`./atexit _exit | cat` printed **nothing**: the line was still buffered (stdout is a pipe), and `_exit`
discarded it. On a terminal it shows, because the `\n` flushed it.

That's why a child after `fork` uses `_exit`: it must not flush the parent's buffered output again or run
the parent's cleanup handlers. And it's why the server turns SIGTERM into a flag: if the signal killed it,
no cleanup would run.

## Building parley: clean shutdown on signals

Until now, Ctrl-C kills `parleyd` on the spot. Now it stops cleanly. The plan:

- a new module `sig.c` with one function, `sig_init()`, which:
  - ignores SIGPIPE, so writing to a client that went away is an `EPIPE` error, not death;
  - makes SIGINT and SIGTERM set a flag, `sig_quit`, **without** `SA_RESTART`, so a blocked `read()`
    wakes up with EINTR;
- the server's read loop checks the flag on EINTR, sends the client `ERROR :Server shutting down`, and
  ends normally.

It's a separate file because the client will use it later.

`src/sig.h` (new):

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

#endif
```

`src/sig.c` (new):

```c
/* sig.c - signal handling shared by server and client. */
#include "sig.h"

#include <stddef.h>

volatile sig_atomic_t sig_quit = 0;

/* A signal handler may run between any two instructions of the program,
 * so it must do almost nothing: here, set a flag. Only async-signal-safe
 * functions may be called in a handler (no printf, no malloc). */
static void on_quit(int signo)
{
    sig_quit = signo;
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
```

Notes:

- `sig_quit` is defined here and declared `extern` in the header ([Chapter 4](04-functions.md)), so the
  server's main loop can read it.
- It stores the signal **number**, not just 1, so the log can name the signal.
- `sa` is filled field by field instead of with `memset`: `sa_mask`, `sa_flags` and `sa_handler` are all
  POSIX asks you to set. The library fills in `sa_restorer` itself.
- One `struct sigaction` serves three calls: `sigaction` copies it, so changing `sa_handler` afterwards is
  safe.
- `sig.c` doesn't define `_POSIX_C_SOURCE`: the Makefile passes `-D_POSIX_C_SOURCE=200809L` for every file
  ([Chapter 9](09-preprocessor-and-building.md)).

In `src/parleyd.c`, include the header with the others:

```c
#include "room.h"
#include "sig.h"
#include "str.h"
```

In `serve_stdin()`, the read loop already retried on EINTR. Now an EINTR from SIGINT/SIGTERM ends the
session:

```c
        ssize_t n = read_into(STDIN_FILENO, &client.in, 4096);
        if (n < 0) {
            if (errno == EINTR && sig_quit != 0) {
                log_msg(LOG_INFO, "got signal %d (%s), shutting down",
                        (int)sig_quit, strsignal(sig_quit));
                conn_send(&client, &(struct msg){
                    .command = "ERROR", .trailing = "Server shutting down" });
                break;
            }
            if (errno == EINTR)
                continue;              /* some other signal: just retry */
            log_msg(LOG_ERROR, "reading from client: %s", strerror(errno));
            goto out;
        }
```

`log_msg` and `strsignal` are safe here: this is normal code, after the handler has returned. `break`
leaves the loop the same way end of input does, so the usual "session over" path runs.

In `main()`, install the handlers right after logging is set up:

```c
    log_init("parleyd", cfg.verbose);
    log_msg(LOG_DEBUG, "verbose logging is on");
    if (sig_init() < 0)
        die("setting up signals: %s", strerror(errno));
    log_msg(LOG_INFO, "reading one client session from stdin");
```

In the `Makefile`, add `sig.o` to the server and to the tests:

```make
SERVER_OBJS := $(addprefix $(BUILD)/, parleyd.o room.o conn.o io.o sig.o msg.o nick.o strbuf.o str.o log.o)
CLIENT_OBJS := $(addprefix $(BUILD)/, parley.o msg.o strbuf.o str.o log.o)
TEST_OBJS   := $(addprefix $(BUILD)/, unit.o test_str.o test_nick.o test_strbuf.o \
                 test_msg.o test_io.o test_sig.o io.o sig.o msg.o nick.o strbuf.o str.o)
```

`tests/test_sig.c` (new) checks both behaviours without killing the test program:

```c
/* test_sig.c - tests for sig.c. */
#include <errno.h>
#include <unistd.h>

#include "sig.h"
#include "test.h"
#include "tests.h"

void test_sig_quit_flag(void)
{
    CHECK_INT(sig_init(), 0);
    sig_quit = 0;
    raise(SIGTERM);                    /* runs the handler, does not exit */
    CHECK_INT(sig_quit, SIGTERM);
    sig_quit = 0;
}

void test_sig_pipe_ignored(void)
{
    CHECK_INT(sig_init(), 0);
    int fds[2];
    CHECK_INT(pipe(fds), 0);
    close(fds[0]);                     /* nobody will ever read */
    errno = 0;
    CHECK_INT(write(fds[1], "x", 1), -1);   /* without sig_init: killed */
    CHECK_INT(errno, EPIPE);
    close(fds[1]);
}
```

`raise()` runs the handler before returning, so the flag is set on the next line. Register the two tests
at the end of the list in `tests/tests.h`:

```c
    X(io_nonblock)                  \
    X(sig_quit_flag)                \
    X(sig_pipe_ignored)
```

### Checkpoint

Run the tests on both libraries, then with the sanitizers:

```console
$ make -s test-all
unit: 25 passed, 0 failed
unit: 25 passed, 0 failed
$ make -s SANITIZE=1 test
unit: 25 passed, 0 failed
```

Now interrupt the server while it's blocked in `read()`. A named pipe (`mkfifo`) stands in for the client
and keeps the server's stdin open:

```console
$ mkfifo in
$ build/glibc/parleyd <in >out.txt 2>err.txt &
$ exec 3>in
$ printf 'NICK al\r\n' >&3; sleep 0.3
$ kill -INT $!; wait $!; echo "exit $?"
exit 0
$ exec 3>&-; cat out.txt err.txt
:parley 001 al :Welcome to parley, al
:al JOIN
:parley 353 al :al
:parley 366 al :End of NAMES
ERROR :Server shutting down
parleyd: info: reading one client session from stdin
parleyd: info: al joined
parleyd: info: got signal 2 (Interrupt), shutting down
parleyd: info: session over
```

`build/musl/parleyd` gives the same output. Exit status 0, and the client got a goodbye: the signal was
handled, not fatal.

With SIGPIPE ignored, writing to a closed pipe is an error the server reports:

```console
$ { sleep 0.3; printf 'NICK al\r\n'; } | build/glibc/parleyd | true; echo "exit ${PIPESTATUS[1]}"
parleyd: info: reading one client session from stdin
parleyd: error: writing to al: Broken pipe
parleyd: info: al joined
exit 1
```

`true` exits at once without reading, so the welcome hits a pipe with no reader. Before this chapter, the
server would have died silently with status 141. The musl build prints the same.

Finally, see what `sigaction()` asked the kernel for:

```console
$ strace -e trace=rt_sigaction build/glibc/parleyd </dev/null 2>&1 | grep rt_sig
rt_sigaction(SIGPIPE, {sa_handler=SIG_IGN, sa_mask=[], sa_flags=SA_RESTORER, sa_restorer=0x7faae9842520}, NULL, 8) = 0
rt_sigaction(SIGINT, {sa_handler=0x61e393ea0410, sa_mask=[], sa_flags=SA_RESTORER, sa_restorer=0x7faae9842520}, NULL, 8) = 0
rt_sigaction(SIGTERM, {sa_handler=0x61e393ea0410, sa_mask=[], sa_flags=SA_RESTORER, sa_restorer=0x7faae9842520}, NULL, 8) = 0
$ strace -e trace=rt_sigaction build/musl/parleyd </dev/null 2>&1 | grep rt_sig
rt_sigaction(SIGPIPE, {sa_handler=SIG_IGN, sa_mask=[], sa_flags=SA_RESTORER, sa_restorer=0x4050e2}, NULL, 8) = 0
rt_sigaction(SIGINT, {sa_handler=0x4020b0, sa_mask=[], sa_flags=SA_RESTORER, sa_restorer=0x4050e2}, NULL, 8) = 0
rt_sigaction(SIGTERM, {sa_handler=0x4020b0, sa_mask=[], sa_flags=SA_RESTORER, sa_restorer=0x4050e2}, NULL, 8) = 0
```

Both ask for the same thing: no `SA_RESTART`, no `SA_RESETHAND`, as `sig_init` said. Only the addresses
differ (and change between runs on glibc): the restorer is in `libc.so` for glibc, in the static binary
for musl.

```console
$ rm -f in out.txt err.txt && make clean >/dev/null
```

The complete project at this point is in `project/ch13/`.

## Summary

- `fork()` returns twice: 0 in the child, the child's PID in the parent. The child gets a copy of memory
  and of all file descriptors.
- `fflush` before `fork`, or the copied stdio buffer prints twice. How much is duplicated differs between
  glibc and musl; the fix is the same.
- `exec` replaces the process's program; `waitpid` collects the status. Decode it with `WIFEXITED`,
  `WEXITSTATUS`, `WIFSIGNALED`, `WTERMSIG`. Shell conventions: 126, 127, 128 + signal.
- An exited child stays a zombie until its parent waits. Orphans are adopted by init.
- `pipe` + `dup2` + `exec` builds a shell pipeline. Close every unused pipe end, or readers never see EOF.
- Install handlers with `sigaction`, not `signal()`: `signal()` gives System V semantics on glibc (strict
  mode) and BSD semantics on musl.
- A handler only sets a `volatile sig_atomic_t` flag (or `write`s to a self-pipe); the main loop does the
  work. Handlers may call only async-signal-safe functions.
- Without `SA_RESTART`, a signal makes a blocked call fail with EINTR: use that to wake up and quit.
  `poll` fails with EINTR even with `SA_RESTART`.
- Servers ignore SIGPIPE; a write to a closed connection then fails with EPIPE.
- Same-type signals don't queue: after SIGCHLD, loop `waitpid(-1, ..., WNOHANG)`. Use `sigprocmask` +
  `sigsuspend` to wait without a race.
- `exit` and `return` run `atexit` handlers and flush stdio; `_exit` and fatal signals don't.
- `strsignal` texts differ between glibc and musl (SIGFPE, SIGCHLD, unknown numbers): compare signal
  numbers, never strings.

## Exercises

1. **A mini shell.** Write `msh`: print a prompt `msh> `, read a line with `getline`, split it into words
   on spaces and tabs (no quotes, no pipes), and run it with fork/execvp/waitpid. Add built-ins `cd` and
   `exit`. Print `[status N]` when a command fails. The shell must survive Ctrl-C; the command it runs
   must not. Why must `cd` be a built-in?

   <details><summary>Solution</summary>

   ```c
   /* msh.c - a minimal shell: one command per line, no pipes or quotes. */
   #define _POSIX_C_SOURCE 200809L
   #include <errno.h>
   #include <signal.h>
   #include <stdio.h>
   #include <stdlib.h>
   #include <string.h>
   #include <sys/wait.h>
   #include <unistd.h>

   #define MAX_ARGS 32

   static void set_sigint(void (*action)(int))
   {
       struct sigaction sa;
       memset(&sa, 0, sizeof sa);
       sa.sa_handler = action;
       sigemptyset(&sa.sa_mask);
       sigaction(SIGINT, &sa, NULL);
   }

   int main(void)
   {
       char *line = NULL;
       size_t cap = 0;
       int last = 0;                       /* like $? */

       set_sigint(SIG_IGN);                /* Ctrl-C must not kill the shell */
       for (;;) {
           printf("msh> ");
           fflush(stdout);
           if (getline(&line, &cap, stdin) < 0)
               break;                      /* Ctrl-D or end of input */

           char *argv[MAX_ARGS + 1];
           int argc = 0;
           for (char *w = strtok(line, " \t\n"); w != NULL && argc < MAX_ARGS;
                w = strtok(NULL, " \t\n"))
               argv[argc++] = w;
           argv[argc] = NULL;
           if (argc == 0)
               continue;

           if (strcmp(argv[0], "exit") == 0)
               break;
           if (strcmp(argv[0], "cd") == 0) {  /* must change OUR directory */
               const char *dir = argc > 1 ? argv[1] : getenv("HOME");
               if (dir == NULL || chdir(dir) < 0)
                   fprintf(stderr, "cd: %s: %s\n", dir ? dir : "", strerror(errno));
               continue;
           }

           pid_t pid = fork();
           if (pid < 0) {
               perror("fork");
               continue;
           }
           if (pid == 0) {
               set_sigint(SIG_DFL);        /* the command gets normal Ctrl-C */
               execvp(argv[0], argv);
               fprintf(stderr, "msh: %s: %s\n", argv[0], strerror(errno));
               _exit(127);
           }
           int status;
           waitpid(pid, &status, 0);
           last = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
           if (last != 0)
               printf("[status %d]\n", last);
       }
       free(line);
       printf("\n");
       return last;
   }
   ```

   ```console
   $ printf 'cd /usr\npwd\nls /nope\nnosuch\nexit\n' | ./msh
   msh> msh> /usr
   msh> ls: cannot access '/nope': No such file or directory
   [status 2]
   msh> msh: nosuch: No such file or directory
   [status 127]
   msh> 
   ```

   (The prompts run together because input comes from a pipe, not a keyboard.) Ctrl-C sends SIGINT to
   every process in the terminal's foreground group: the shell ignores it; the child has `SIG_DFL` again
   and dies. Ignored signals stay ignored across `exec`, so the child must reset it.

   `cd` must be a built-in because the current directory belongs to a process. A `cd` program would change
   **its own** directory and exit; the shell's wouldn't change.

   </details>

2. **A timeout command.** Write `mytimeout SECONDS COMMAND [ARG...]`: run the command; if it's still
   running after SECONDS, send it SIGTERM and exit with status 124 (like GNU `timeout`). Otherwise exit
   with the command's status. Use `alarm()` and a SIGALRM handler.

   <details><summary>Solution</summary>

   ```c
   /* mytimeout.c - run a command; kill it if it runs too long.
    * usage: mytimeout SECONDS COMMAND [ARG...] */
   #define _POSIX_C_SOURCE 200809L
   #include <errno.h>
   #include <signal.h>
   #include <stdio.h>
   #include <stdlib.h>
   #include <string.h>
   #include <sys/wait.h>
   #include <unistd.h>

   static volatile sig_atomic_t timed_out = 0;

   static void on_alarm(int signo)
   {
       (void)signo;
       timed_out = 1;
   }

   int main(int argc, char *argv[])
   {
       if (argc < 3 || atoi(argv[1]) <= 0) {
           fprintf(stderr, "usage: mytimeout SECONDS COMMAND [ARG...]\n");
           return 2;
       }
       struct sigaction sa;
       memset(&sa, 0, sizeof sa);
       sa.sa_handler = on_alarm;
       sigemptyset(&sa.sa_mask);
       sigaction(SIGALRM, &sa, NULL);      /* no SA_RESTART: waitpid gets EINTR */

       pid_t pid = fork();
       if (pid < 0)
           return 1;
       if (pid == 0) {
           execvp(argv[2], &argv[2]);
           perror(argv[2]);
           _exit(127);
       }

       alarm((unsigned)atoi(argv[1]));
       int status;
       while (waitpid(pid, &status, 0) < 0) {
           if (errno != EINTR)
               return 1;
           if (timed_out) {
               fprintf(stderr, "mytimeout: time is up, sending SIGTERM\n");
               kill(pid, SIGTERM);
           }
       }
       if (timed_out)
           return 124;                     /* like GNU timeout */
       return WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
   }
   ```

   ```console
   $ ./mytimeout 1 sleep 5; echo "exit $?"
   mytimeout: time is up, sending SIGTERM
   exit 124
   $ ./mytimeout 2 sh -c 'exit 3'; echo "exit $?"
   exit 3
   ```

   The alarm interrupts `waitpid` with EINTR (no `SA_RESTART`); the loop sends SIGTERM, then waits again
   for the child to die. With `SA_RESTART`, `waitpid` would restart and the
   program would wait the full 5 seconds.

   </details>

3. **Do signals add up?** Predict the output, then run it. Block SIGUSR1, `raise` it 5 times, unblock it,
   and print how many times the handler ran.

   <details><summary>Solution</summary>

   ```c
   /* pending.c - are signals counted? */
   #define _POSIX_C_SOURCE 200809L
   #include <signal.h>
   #include <stdio.h>
   #include <string.h>

   static volatile sig_atomic_t count = 0;

   static void on_usr1(int signo)
   {
       (void)signo;
       count++;
   }

   int main(void)
   {
       struct sigaction sa;
       memset(&sa, 0, sizeof sa);
       sa.sa_handler = on_usr1;
       sigemptyset(&sa.sa_mask);
       sigaction(SIGUSR1, &sa, NULL);

       sigset_t set;
       sigemptyset(&set);
       sigaddset(&set, SIGUSR1);
       sigprocmask(SIG_BLOCK, &set, NULL);
       for (int i = 0; i < 5; i++)
           raise(SIGUSR1);                 /* pending: delivered later */
       sigprocmask(SIG_UNBLOCK, &set, NULL);
       printf("sent 5, handled %d\n", (int)count);
       return 0;
   }
   ```

   ```console
   $ ./pending
   sent 5, handled 1
   ```

   A pending signal is one bit per signal number, not a counter: five blocked SIGUSR1s become one. That's
   why `reap.c` loops `waitpid(WNOHANG)` after each SIGCHLD.

   </details>

4. **Answer without notes.**
   - Why are `fork` and `exec` separate calls?
   - What's the difference between a zombie and an orphan?
   - Why may a handler not call `printf`?
   - The server's handler doesn't use `SA_RESTART`. What would go wrong if it did?

   <details><summary>Solution</summary>

   - So the child can set itself up in between: redirect fds with `dup2`, close fds, change directory,
     reset signals. The new program inherits all of it.
   - A zombie is a dead child whose parent hasn't called `wait` yet; it holds a PID until reaped. An orphan
     is a live child whose parent died; init adopts it.
   - The handler can interrupt `printf` halfway through updating the stdout buffer; a second `printf`
     would corrupt it. `printf` isn't async-signal-safe. Set a flag instead.
   - The kernel would restart the blocked `read()` after the handler. The flag would be set, but the loop
     wouldn't look at it until the client sent something: Ctrl-C would seem to do nothing.

   </details>

Sources for this chapter: [REFERENCES.md](REFERENCES.md#chapter-13--processes-and-signals)
