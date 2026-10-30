# Chapter 22 — Shipping, and Choosing a C Library

[Chapter 21](21-robustness.md) made the server survive hostile clients. This last chapter makes version 1.0:
`getopt()` options, a benchmark (which finds a 75× slowdown hiding in TCP), release binaries, and a source
tarball with a README. Then the course's running question gets its answer, **glibc or musl?**, measured on
the finished program. Last: portability, C after C99, and where to go next.

All sizes and times come from one Linux x86-64 machine (glibc 2.35, musl 1.2.6, gcc 11.4). ⚠ **Your numbers
will differ** with versions, machine and load; look at the ratios and reasons.

## Release builds

So far you built for *development*: `-O2 -g` (debug information for gdb), `assert()` on, sanitizers with
`make SANITIZE=1`. A *release* build differs in three ways:

| | development | release |
|---|---|---|
| debug information (`-g`) | yes: gdb shows file names, lines, variables | no |
| `assert()` | on | off: `-DNDEBUG` |
| symbol table | kept | removed with `strip` |

Both use `-O2`. (`-O3` optimizes harder, usually making code bigger; `-Os` optimizes for size, see Exercise 3.)

### `-DNDEBUG`: asserts disappear

If `NDEBUG` is defined when `<assert.h>` is included, `assert(expr)` ([Chapter 10](10-errors-and-debugging.md))
expands to *nothing*: `expr` is not evaluated. So never put a side effect inside an `assert()`:

`examples/ch22/ndebug.c`

```c
/* ndebug.c - what -DNDEBUG does to assert(). */
#include <assert.h>
#include <stdio.h>

static int calls;

static int check_something(void)
{
    calls++;                   /* a side effect */
    return 0;                  /* "the check failed" */
}

int main(void)
{
    assert(check_something() == 1);   /* wrong: the call vanishes with NDEBUG */
    printf("check_something() ran %d time(s)\n", calls);
    return 0;
}
```

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o ndebug ndebug.c
$ ./ndebug; echo "exit $?"
ndebug: ndebug.c:15: main: Assertion `check_something() == 1' failed.
Aborted (core dumped)
exit 134
$ gcc -std=c99 -Wall -Wextra -pedantic -DNDEBUG -o ndebug ndebug.c
ndebug.c:7:12: warning: 'check_something' defined but not used [-Wunused-function]
    7 | static int check_something(void)
      |            ^~~~~~~~~~~~~
$ ./ndebug; echo "exit $?"
check_something() ran 0 time(s)
exit 0
$ musl-gcc -std=c99 -Wall -Wextra -pedantic -static -o ndebug-musl ndebug.c
$ ./ndebug-musl; echo "exit $?"
Assertion failed: check_something() == 1 (ndebug.c: main: 15)
Aborted (core dumped)
exit 134
$ musl-gcc -std=c99 -Wall -Wextra -pedantic -static -DNDEBUG -o ndebug-musl ndebug.c 2>/dev/null
$ ./ndebug-musl; echo "exit $?"
check_something() ran 0 time(s)
exit 0
```

- With `-DNDEBUG` the call is gone; gcc even warns that `check_something()` is unused. With
  `assert(fclose(f) == 0)`, the release build would silently stop closing the file.
- Both libraries behave the same except for the message. (`2>/dev/null` hides the same
  `-Wunused-function` warning in the last musl build.)
- Asserts on in a shipped server? A firing assert kills it for everyone, but stops a broken invariant before
  worse happens. `parleyd` follows the common convention: asserts catch *programmer* errors during
  development; every error a *user* or *client* can cause is handled by real code in both builds. So the
  tests run on the development build.

### Looking inside a binary: `size`, `nm`, `strip`, `file`, `ldd`

What is in an executable:

| Command | Shows |
|---|---|
| `ls -l` | the file size, everything included |
| `size` | the bytes the program occupies in memory: `text` (code, constants), `data` (initialized variables), `bss` (zeroed variables) |
| `nm FILE` | the symbol table: functions and variables by name (`T` = code defined here) |
| `nm -D FILE` | the *dynamic* symbols: what a dynamic program imports from shared libraries (`U` = undefined here) |
| `strip FILE` | removes the symbol table and debug information, in place |
| `file FILE` | the kind of file: dynamic or static, stripped or not, with debug info or not |
| `ldd FILE` | the shared libraries a dynamic program loads at start-up |

`strip` changes nothing the CPU executes: `size` stays, the file shrinks. Without symbols gdb shows only
addresses, so keep an unstripped build for yourself. Step 4 shows `parleyd`'s numbers.

## Command-line options with `getopt()`

Since [Chapter 3](03-control-flow.md), `parleyd` and `parley` parse options by hand, missing what users
expect from a Unix program:

- bundled flags: `-vT` should mean `-v -T`;
- a value glued to its option: `-p6667` should mean `-p 6667`;
- `--` should end the options, so an operand may start with `-`.

POSIX's `getopt()`, in `<unistd.h>`, does all this.

```c
int getopt(int argc, char *const argv[], const char *optstring);
extern char *optarg;   /* the value of the current option */
extern int optind;     /* the index of the next argv element to look at */
extern int optopt;     /* the option letter that caused an error */
extern int opterr;     /* set to 0 to stop getopt() printing errors */
```

Each call returns the next option letter, or −1 at the end. `optstring` lists the letters; a `:` after a
letter means it takes a value, put in `optarg`. Options end at the first argument not starting with `-`, at a
lone `-`, or after `--` (skipped); `optind` is then the index of the first *operand*.

On an error `getopt()` returns `'?'`, puts the letter in `optopt`, and prints a message unless `opterr = 0` or
`optstring` starts with `:`. The leading `:` also makes a missing value return `':'`, telling the errors apart.

`examples/ch22/getopt-quiet.c`

```c
/* getopt-quiet.c - getopt()'s own error messages, or ours.
 *     getopt-quiet [-q] [-p PORT] [-v]
 * -q must come first: it chooses the option string. */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char *argv[])
{
    /* A leading ':' in the option string: getopt() prints nothing, returns
     * ':' for a missing value, '?' for an unknown option, and puts the
     * option letter in optopt. */
    const char *opts = argc > 1 && strcmp(argv[1], "-q") == 0 ? ":qp:v" : "qp:v";
    int opt;

    while ((opt = getopt(argc, argv, opts)) != -1) {
        switch (opt) {
        case 'q':
            break;
        case 'p':
            printf("port %s\n", optarg);
            break;
        case 'v':
            printf("verbose\n");
            break;
        case ':':
            fprintf(stderr, "%s: option -%c needs a value\n", argv[0], optopt);
            return 2;
        default:                       /* '?' */
            if (opts[0] == ':')
                fprintf(stderr, "%s: unknown option '-%c'\n", argv[0], optopt);
            return 2;
        }
    }
    printf("first operand: argv[%d] = %s\n", optind, optind < argc ? argv[optind] : "(none)");
    return 0;
}
```

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o getopt-quiet getopt-quiet.c
$ ./getopt-quiet -vp 7000 extra
verbose
port 7000
first operand: argv[3] = extra
$ ./getopt-quiet -- -v
first operand: argv[2] = -v
$ ./getopt-quiet -x
./getopt-quiet: invalid option -- 'x'
$ ./getopt-quiet -p
./getopt-quiet: option requires an argument -- 'p'
$ ./getopt-quiet -q -x
./getopt-quiet: unknown option '-x'
$ ./getopt-quiet -q -p
./getopt-quiet: option -p needs a value
```

- `-vp 7000` is two options in one word; `7000` is `-p`'s value and `extra` is `argv[3]`.
- After `--`, `-v` is an operand.
- Without the leading `:`, `getopt()` prints its own message; with it, the program words its own.

`getopt()` knows only one-letter options. `--help` would need `getopt_long()` (`<getopt.h>`), a GNU extension
that musl and the BSDs have but POSIX doesn't, so `parley` checks `--help` and `--version` by hand first.

### glibc vs musl: getopt

The same program with musl:

```console
$ musl-gcc -std=c99 -Wall -Wextra -pedantic -static -o getopt-quiet-musl getopt-quiet.c
$ ./getopt-quiet-musl -vp 7000 extra
verbose
port 7000
first operand: argv[3] = extra
$ ./getopt-quiet-musl -x
./getopt-quiet-musl: unrecognized option: x
$ ./getopt-quiet-musl -p
./getopt-quiet-musl: option requires an argument: p
$ ./getopt-quiet-musl -q -x
./getopt-quiet-musl: unknown option '-x'
$ ./getopt-quiet-musl -q -p
./getopt-quiet-musl: option -p needs a value
```

| | glibc | musl |
|---|---|---|
| unknown option | `invalid option -- 'x'` | `unrecognized option: x` |
| missing value | `option requires an argument -- 'p'` | `option requires an argument: p` |
| options after operands ("permutation", [Chapter 3](03-control-flow.md)) | only with `_GNU_SOURCE`, or in GNU mode (`-std=gnu99`, gcc's default) with no feature macro; never with our `_POSIX_C_SOURCE` | never |
| leading `:` in `optstring` | supported | supported |

Same parsing, different default messages; with the leading `:` and our own messages (as in `parley`), both
print text scripts can test. **musl is simpler**: one behaviour, POSIX's, whatever macros you define. glibc's
permutation is convenient, but makes the same source parse differently depending on macros.

## Static or dynamic?

[Chapter 1](01-getting-started.md) built hello three ways. Now in depth, with the smallest possible program:

`examples/ch22/nothing.c`

```c
/* nothing.c - a program that does nothing: what does starting it cost? */
int main(void)
{
    return 0;
}
```

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -O2 -o nothing nothing.c
$ gcc -std=c99 -Wall -Wextra -pedantic -O2 -static -o nothing-static nothing.c
$ musl-gcc -std=c99 -Wall -Wextra -pedantic -O2 -static -o nothing-musl nothing.c
$ ls -l nothing nothing-static nothing-musl | awk '{print $5, $9}'
15776 nothing
16208 nothing-musl
900248 nothing-static
```

Doing nothing still means *starting*. Count the system calls (`grep -v` drops strace's `+++ exited with 0 +++`
line):

```console
$ strace -o glibc.trace ./nothing; grep -vc '^+++' glibc.trace
34
$ strace -o static.trace ./nothing-static; grep -vc '^+++' static.trace
16
$ strace -o musl.trace ./nothing-musl; cat musl.trace
execve("./nothing-musl", ["./nothing-musl"], 0x7fff46bef960 /* 44 vars */) = 0
arch_prctl(ARCH_SET_FS, 0x404158)       = 0
set_tid_address(0x404290)               = 308939
exit_group(0)                           = ?
+++ exited with 0 +++
```

musl's start-up is two calls: `arch_prctl(ARCH_SET_FS)` points a CPU register at the thread's data (where
`errno` lives), and `set_tid_address` registers the thread. Then `main()` runs and `exit_group` ends it.
Dynamic glibc's calls:

```console
$ cut -d'(' -f1 glibc.trace | sort | uniq -c | sort -rn
      8 mmap
      4 pread64
      4 mprotect
      2 openat
      2 newfstatat
      2 close
      2 arch_prctl
      1 set_tid_address
      1 set_robust_list
      1 rseq
      1 read
      1 prlimit64
      1 munmap
      1 exit_group
      1 execve
      1 brk
      1 access
      1 +++ exited with 0 +++
```

- **The dynamic loader** does most. `ld-linux-x86-64.so.2` opens `/etc/ld.so.cache` to find `libc.so.6`,
  opens it, reads its headers (`read`, `pread64`), maps it (`mmap`), and makes the fixed parts read-only
  (`mprotect`).
- **Thread set-up**, even without threads: `set_robust_list` and `rseq` register kernel structures for locks
  and per-CPU data; `prlimit64` reads `RLIMIT_STACK` for the default thread stack size
  ([Chapter 17](17-threads.md)).
- **Static glibc** skips the loader (16 calls) but keeps the thread set-up, reads `/proc/self/exe`, gets
  random bytes (`getrandom`) and grows the heap (`brk`).

How much time is that? Start each program 1000 times:

```console
$ TIMEFORMAT='%R s'
$ time (for i in $(seq 1000); do ./nothing; done)
0.452 s
$ time (for i in $(seq 1000); do ./nothing-static; done)
0.360 s
$ time (for i in $(seq 1000); do ./nothing-musl; done)
0.251 s
$ rm -f *.trace
```

`fork()` and `execve()` cost the same for all three, so the differences are the libraries': about 0.45 ms per
start for dynamic glibc, 0.25 ms for musl. ⚠ Run it a few times: on a busy machine the numbers jump (one
later run gave 0.509, 0.733 and 0.394 s).

### glibc vs musl: start-up

| | glibc, dynamic | glibc, static | musl, static |
|---|---|---|---|
| `nothing` size | 15776 B (+ `libc.so.6`) | 900248 B | 16208 B |
| system calls, `execve` to exit | 34 | 16 | 4 |
| 1000 starts ⚠ | 0.45 s | 0.36 s | 0.25 s |

**musl is better** for start-up: fewer system calls, no files opened, about twice as fast. That matters only
for programs started very often that do little (a tool in a shell loop, a CGI program), not for `parleyd`,
which runs for days. But fewer moving parts means fewer failures: no loader to find, no `libc.so.6` that
could be missing or the wrong version.

## Name lookup in a static program

[Chapter 14](14-networking-basics.md) showed the warning for linking `getaddrinfo()` statically with glibc.
Now the run time. This program looks up `localhost`, like `parley`, and a user name, like `ls -l` for file
owners:

`examples/ch22/lookup.c`

```c
/* lookup.c - name lookups: the host "localhost" and a user.
 *     lookup [USER]          (default: root) */
#define _POSIX_C_SOURCE 200809L
#include <netdb.h>
#include <pwd.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>

int main(int argc, char *argv[])
{
    const char *user = argc > 1 ? argv[1] : "root";
    struct addrinfo hints, *res;
    struct passwd *pw;
    int rc;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    rc = getaddrinfo("localhost", "6667", &hints, &res);
    if (rc != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rc));
        return 1;
    }
    printf("localhost: found\n");
    freeaddrinfo(res);

    pw = getpwnam(user);                    /* look up a user by name */
    if (pw)
        printf("%s: uid %d\n", user, (int)pw->pw_uid);
    else
        printf("%s: no such user\n", user);
    return 0;
}
```

`getpwnam()` (POSIX, `<pwd.h>`) finds a user's name, user ID, home directory and shell. Three builds:

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -O2 -o lookup lookup.c
$ gcc -std=c99 -Wall -Wextra -pedantic -O2 -static -o lookup-static lookup.c
/usr/bin/ld: /tmp/ccURVIuD.o: in function `main':
lookup.c:(.text.startup+0x85): warning: Using 'getpwnam' in statically linked applications requires at runtime the shared libraries from the glibc version used for linking
/usr/bin/ld: lookup.c:(.text.startup+0x55): warning: Using 'getaddrinfo' in statically linked applications requires at runtime the shared libraries from the glibc version used for linking
$ musl-gcc -std=c99 -Wall -Wextra -pedantic -O2 -static -o lookup-musl lookup.c
$ strip lookup lookup-static lookup-musl
$ ls -l lookup lookup-static lookup-musl | awk '{print $5, $9}'
14472 lookup
67232 lookup-musl
1053456 lookup-static
$ ./lookup-static; ./lookup-musl
localhost: found
root: uid 0
localhost: found
root: uid 0
```

Both work. Which files do they open?

```console
$ strace -e trace=openat,open ./lookup-static 2>&1 >/dev/null
openat(AT_FDCWD, "/etc/host.conf", O_RDONLY|O_CLOEXEC) = 3
openat(AT_FDCWD, "/etc/resolv.conf", O_RDONLY|O_CLOEXEC) = 3
openat(AT_FDCWD, "/etc/nsswitch.conf", O_RDONLY|O_CLOEXEC) = 3
openat(AT_FDCWD, "/etc/hosts", O_RDONLY|O_CLOEXEC) = 3
openat(AT_FDCWD, "/etc/passwd", O_RDONLY|O_CLOEXEC) = 3
+++ exited with 0 +++
$ strace -e trace=openat,open ./lookup-musl 2>&1 >/dev/null
open("/etc/hosts", O_RDONLY|O_LARGEFILE|O_CLOEXEC) = 3
open("/etc/passwd", O_RDONLY|O_LARGEFILE|O_CLOEXEC) = 3
+++ exited with 0 +++
```

(`2>&1 >/dev/null`, in this order, sends strace's stderr into the pipe and discards the program's output.)

glibc reads `/etc/nsswitch.conf`, the **Name Service Switch** (NSS) configuration. Here it contains:

```text
passwd:         files systemd
hosts:          files dns
```

Users: `/etc/passwd`, then *systemd*. Hosts: `/etc/hosts`, then DNS. `files` and `dns` are built into glibc
2.35; every other source (`systemd`, `ldap`, `sss`, `mdns4`…) is a *plug-in*, a shared library
`libnss_NAME.so.2` loaded with `dlopen()` when needed. `root` is in `/etc/passwd`, so no plug-in was needed.
For a nonexistent user, glibc must ask systemd too:

```console
$ ./lookup nosuchuser
localhost: found
nosuchuser: no such user
$ ./lookup-musl nosuchuser
localhost: found
nosuchuser: no such user
$ ./lookup-static nosuchuser
localhost: found
Segmentation fault (core dumped)
$ strace -e trace=openat ./lookup-static nosuchuser 2>&1 >/dev/null | tail -5
openat(AT_FDCWD, "/lib/x86_64-linux-gnu/libnss_systemd.so.2", O_RDONLY|O_CLOEXEC) = 3
openat(AT_FDCWD, "/lib/x86_64-linux-gnu/libc.so.6", O_RDONLY|O_CLOEXEC) = 3
openat(AT_FDCWD, "/lib/x86_64-linux-gnu/ld-linux-x86-64.so.2", O_RDONLY|O_CLOEXEC) = 3
--- SIGSEGV {si_signo=SIGSEGV, si_code=SEGV_MAPERR, si_addr=0x1c} ---
+++ killed by SIGSEGV (core dumped) +++
```

The "static" program loaded `libnss_systemd.so.2`, and with it the *shared* `libc.so.6` and the loader: a
second glibc beside the linked-in one. Then it crashed inside the plug-in (gdb shows the fault in
`libnss_systemd`). The exact cause is deep in glibc's internals, but the root is clear: NSS plug-ins expect a
dynamically linked glibc, which a static program can't give them. That is the linker warning's meaning: a
static glibc program that looks up names still depends on the same glibc version's shared libraries, and on
the machine's `nsswitch.conf`. Here machine and version matched and it still crashed; elsewhere it may work,
fail, or crash differently.

musl has no NSS: it reads `/etc/hosts` and `/etc/passwd` and asks the DNS servers in `/etc/resolv.conf`
itself, loading nothing.

### glibc vs musl: static linking and name lookup

| | glibc | musl |
|---|---|---|
| `lookup` stripped size | 14472 B dynamic; 1053456 B static | 67232 B static |
| static `getaddrinfo()`, `getpwnam()` | links with a warning; loads NSS plug-ins at run time; here: a crash for an unknown user | works; no plug-ins |
| sources of names | anything with an NSS plug-in: files, DNS, LDAP, systemd, mDNS, … | `/etc/hosts`, `/etc/passwd`, `/etc/group`, DNS |

For **static binaries, musl is clearly better**: they are truly self-contained. A program that must see users
from LDAP or hosts from mDNS (company networks, some desktops) needs **dynamic glibc**: that is what NSS is
for. `parley` needs only `/etc/hosts` and DNS, which both handle.

### Static linking and licences

The licences differ, which matters most when linking statically:

- **musl** is MIT: link it into anything, static or dynamic, and keep its copyright notice.
- **glibc** is LGPL 2.1 (or later): link it dynamically into any program under any licence. If you distribute
  a program linked *statically* with glibc, LGPL section 6 requires letting users relink it with a modified
  glibc, for example by also giving them your object files.

Practically, a dynamic program gets glibc's security fixes when the system updates `libc.so.6`; a static one,
with either library, keeps its old code until you rebuild and re-ship it.

## How fast is the C library?

A chat server spends its time in a few library functions: `malloc()`/`free()` for clients and buffers,
`snprintf()` or `memcpy()` to build lines, `memchr()` to find line ends. This times 4 million calls of each:

`examples/ch22/speed.c`

```c
/* speed.c - time three things a chat server does all day:
 * small malloc/free pairs, snprintf, and searching a buffer for '\n'.
 * Compare libcs on the same machine only. */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define N 4000000
#define LIVE 1024

static double now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

int main(void)
{
    static void *slots[LIVE];
    static char buf[512];
    char line[128];
    unsigned long sum = 0;
    double t0;
    int i;

    t0 = now_ms();
    for (i = 0; i < N; i++) {
        int k = i % LIVE;
        free(slots[k]);
        slots[k] = malloc((size_t)(16 + i % 200));   /* message-sized blocks */
    }
    printf("malloc+free  x %d: %4.0f ms\n", N, now_ms() - t0);
    for (i = 0; i < LIVE; i++)
        free(slots[i]);

    t0 = now_ms();
    for (i = 0; i < N; i++)
        sum += (unsigned long)snprintf(line, sizeof line, ":%s MSG :%s %d\r\n",
                                       "alice", "hello", i);
    printf("snprintf     x %d: %4.0f ms\n", N, now_ms() - t0);

    memset(buf, 'x', sizeof buf);
    t0 = now_ms();
    for (i = 0; i < N; i++) {
        buf[i % sizeof buf] = '\n';                   /* move the newline */
        sum += (unsigned long)((char *)memchr(buf, '\n', sizeof buf) - buf);
        buf[i % sizeof buf] = 'x';
    }
    printf("memchr 512 B x %d: %4.0f ms\n", N, now_ms() - t0);
    return sum == 0;                   /* use sum, so no loop is optimized away */
}
```

What keeps the measurement honest:

- **Optimized code** (`-O2`): `-O0` timings say nothing about a release build.
- **Used results.** The optimizer may delete a loop whose result is unused; `sum` collects every result and
  decides the exit status.
- **A monotonic clock** (`CLOCK_MONOTONIC`, [Chapter 18](18-event-loop.md)), which never jumps when the
  system time is set.
- **Realistic data**: blocks of 16–215 bytes, like chat lines; a moving newline, so `memchr()` searches 256
  bytes on average.

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -O2 -o speed speed.c
$ musl-gcc -std=c99 -Wall -Wextra -pedantic -O2 -static -o speed-musl speed.c
$ ./speed
malloc+free  x 4000000:   51 ms
snprintf     x 4000000:  317 ms
memchr 512 B x 4000000:   30 ms
$ ./speed-musl
malloc+free  x 4000000:   95 ms
snprintf     x 4000000: 1990 ms
memchr 512 B x 4000000:   82 ms
```

⚠ Run each a few times: here the first run was 30–40% slower (68 vs 51 ms, 133 vs 95 ms); later runs agreed
within a few percent.

### glibc vs musl: speed

| 4 million calls ⚠ | glibc | musl | musl / glibc |
|---|---|---|---|
| `malloc()` + `free()` | 51 ms | 95 ms | ≈ 2× |
| `snprintf()` | 317 ms | 1990 ms | ≈ 6× |
| `memchr()`, 512 B | 30 ms | 82 ms | ≈ 3× |

glibc wins all three, by design:

- **String functions.** glibc has several x86-64 assembly versions of `memchr()`, `memcpy()`, `strlen()` and
  friends for different CPU features (SSE2, AVX2, AVX-512); at start-up the loader picks one per function
  for this CPU: **IFUNC** (indirect function). musl has one portable C `memchr()` and `strlen()` and a short
  assembly `memcpy()`: easy to check, but reading a word at a time where glibc reads 32 or 64 bytes.
- **`malloc()`.** glibc's allocator keeps per-thread caches of freed blocks for speed. musl's ("mallocng",
  since 1.2.1) returns memory to the system and *detects* misuse (overflows, double frees, use-after-free);
  a corrupted heap can't corrupt the allocator itself.
- **`printf()`.** glibc's is heavily tuned; musl's is short and simple.

A fast library isn't always a fast program: kernel work (system calls, TCP) is the same, and clients often
wait on the network. "glibc vs musl: the server under load" measures it for `parleyd`.

### glibc vs musl: locales, once more

[Chapter 20](20-terminal-ui.md) compared locales. A reminder of what `setlocale()` accepts with
`LANG=C.UTF-8`:

`examples/ch22/locale.c`

```c
/* locale.c - which locales can a program switch to? */
#include <locale.h>
#include <stdio.h>

static void try(const char *name)
{
    const char *r = setlocale(LC_ALL, name);
    printf("setlocale(LC_ALL, \"%s\") -> %s\n", name, r ? r : "NULL (failed)");
}

int main(void)
{
    printf("at start-up: %s\n", setlocale(LC_ALL, NULL));
    try("C.UTF-8");
    try("en_US.UTF-8");
    try("xx_YY");                      /* a locale nobody has */
    try("");                           /* the one from LANG / LC_ALL */
    return 0;
}
```

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o locale locale.c
$ musl-gcc -std=c99 -Wall -Wextra -pedantic -static -o locale-musl locale.c
$ locale -a
C
C.utf8
POSIX
$ ./locale
at start-up: C
setlocale(LC_ALL, "C.UTF-8") -> C.UTF-8
setlocale(LC_ALL, "en_US.UTF-8") -> NULL (failed)
setlocale(LC_ALL, "xx_YY") -> NULL (failed)
setlocale(LC_ALL, "") -> C.UTF-8
$ ./locale-musl
at start-up: C
setlocale(LC_ALL, "C.UTF-8") -> C.UTF-8;C;C;C;C;C
setlocale(LC_ALL, "en_US.UTF-8") -> en_US.UTF-8
setlocale(LC_ALL, "xx_YY") -> xx_YY
setlocale(LC_ALL, "") -> C.UTF-8;C;C;C;C;C
```

- Every program starts in the `"C"` locale until it calls `setlocale()`, with both libraries.
- glibc accepts only installed locales (`locale -a`); this machine has no `en_US.UTF-8`.
- musl accepts any name: its locales are always UTF-8 and need no files. (`;C;C;…` means only the character
  type category uses UTF-8; the others stay "C".)

To sort or format text by language, or translate messages, **glibc** with installed locales does far more.
For plain UTF-8, **musl is simpler**: it always works. `parley` handles UTF-8 itself and never calls
`setlocale()`, so it behaves the same with both.

## Building parley: version 1.0

Five steps, each with a checkpoint.

| Step | What | Files |
|---|---|---|
| 1 | `getopt()` in both programs | `src/parleyd.c`, `src/parley.c` |
| 2 | a benchmark client | `tests/bench.c`, `Makefile` |
| 3 | `TCP_NODELAY` | `src/server.c` |
| 4 | release builds and `make install` | `Makefile` |
| 5 | version 1.0, a README and `make dist` | `src/version.h`, `README.md`, `Makefile` |

Checkpoints start the server with `&` and `sleep 0.2`; job lines such as `[1] 12345` are left out.

### Step 1: `getopt()` in both programs

Replace the hand-written loop in `parse_args()` in `src/parleyd.c`. `<unistd.h>`, already included for
`close()`, declares `getopt()`, `optarg`, `optind` and `optopt`:

`project/ch22/src/parleyd.c` (part)
```c
/* Options are parsed with getopt() (POSIX, in <unistd.h>). The string
 * lists the option letters; "p:" means -p takes a value (in optarg).
 * The leading ':' makes getopt() quiet: it returns ':' for a missing
 * value and '?' for an unknown option, and we word the errors ourselves,
 * so they read the same with glibc and musl. */
static void parse_args(int argc, char *argv[], struct config *cfg)
{
    /* getopt() knows only one-letter options: look for these first. */
    if (argc > 1 && strcmp(argv[1], "--help") == 0) {
        print_help();
        exit(0);
    }
    if (argc > 1 && strcmp(argv[1], "--version") == 0) {
        printf("parleyd " PARLEY_VERSION "\n");   /* "a" "b" is "ab" */
        exit(0);
    }

    int opt;
    while ((opt = getopt(argc, argv, ":p:b:m:t:f:vTh")) != -1) {
        switch (opt) {
        case 'b':
            cfg->addr = optarg;
            break;
        case 'p':
            if (!str_to_long(optarg, 0, PORT_MAX, &cfg->port))
                usage_error("invalid port '%s' (expected 0-%d)", optarg, PORT_MAX);
            break;
        case 'm':
            if (!str_to_long(optarg, 1, MAX_CLIENTS_MAX, &cfg->max_clients))
                usage_error("invalid client limit '%s' (expected 1-%d)",
                            optarg, MAX_CLIENTS_MAX);
            break;
        case 't':
            if (!str_to_long(optarg, 0, IDLE_MAX, &cfg->idle))
                usage_error("invalid idle time '%s' (expected 0-%d)", optarg, IDLE_MAX);
            break;
        case 'f':
            if (!str_to_long(optarg, 0, FLOOD_MAX, &cfg->flood))
                usage_error("invalid flood limit '%s' (expected 0-%d)", optarg, FLOOD_MAX);
            break;
        case 'v':
            cfg->verbose = true;
            break;
        case 'T':
            cfg->timestamps = false;
            break;
        case 'h':
            print_help();
            exit(0);
        case ':':
            usage_error("option -%c needs a value", optopt);
            break;
        default:                                    /* '?' */
            usage_error("unknown option '-%c'", optopt);
        }
    }
    /* getopt() stops at the first argument that is not an option. */
    if (optind < argc)
        usage_error("unexpected argument '%s'", argv[optind]);
}
```

- `"p:b:m:t:f:"` take a value, `vTh` are flags. The leading `:` turns off `getopt()`'s messages, so errors
  read the same with both C libraries.
- `-h` still means `--help`. `--help` and `--version` are recognized only as the first argument, before
  `getopt()` starts, which would read `--version` as `-` plus letters and report "unknown option '--'".
- After the loop, `optind` is the first non-option. `parleyd` takes no operands, so any leftover is an
  error. That covers `parleyd extra -T`: `getopt()` stops at `extra`, as POSIX requires.
- `case 'h':` needs no `break` (`exit()` never returns). Neither does the `usage_error()` case, but its
  `break` shows where it ends.

`src/parley.c` gets the same treatment; there `-h` is the host (Chapter 16), so help is only `--help`:

`project/ch22/src/parley.c` (part)
```c
/* Options are parsed with getopt(), as in parleyd.c. */
static void parse_args(int argc, char *argv[], struct config *cfg)
{
    if (argc > 1 && strcmp(argv[1], "--help") == 0) {
        print_help();
        exit(0);
    }
    if (argc > 1 && strcmp(argv[1], "--version") == 0) {
        printf("parley " PARLEY_VERSION "\n");
        exit(0);
    }

    int opt;
    while ((opt = getopt(argc, argv, ":h:p:n:L")) != -1) {
        switch (opt) {
        case 'h':
            cfg->host = optarg;
            break;
        case 'n':
            cfg->nick = optarg;
            break;
        case 'p':
            if (!str_to_long(optarg, 1, PORT_MAX, &cfg->port))
                usage_error("invalid port '%s' (expected 1-%d)", optarg, PORT_MAX);
            break;
        case 'L':
            cfg->line_mode = true;
            break;
        case ':':
            usage_error("option -%c needs a value", optopt);
            break;
        default:                                    /* '?' */
            usage_error("unknown option '-%c'", optopt);
        }
    }
    if (optind < argc)
        usage_error("unexpected argument '%s'", argv[optind]);
}
```

`parley.c` already includes `<unistd.h>`.

#### Checkpoint

```console
$ make -s test-all
unit: 30 passed, 0 failed
fuzz: 20000 rounds (seed 1), 0 failed
itest: 7 passed, 0 failed
unit: 30 passed, 0 failed
fuzz: 20000 rounds (seed 1), 0 failed
itest: 7 passed, 0 failed
$ build/glibc/parleyd -x; echo "exit $?"
parleyd: unknown option '-x'
Try 'parleyd --help' for more information.
exit 2
$ build/musl/parleyd -x; echo "exit $?"
parleyd: unknown option '-x'
Try 'parleyd --help' for more information.
exit 2
$ build/musl/parleyd -p; echo "exit $?"
parleyd: option -p needs a value
Try 'parleyd --help' for more information.
exit 2
$ build/glibc/parleyd extra -T; echo "exit $?"
parleyd: unexpected argument 'extra'
Try 'parleyd --help' for more information.
exit 2
$ build/glibc/parleyd --port 6667; echo "exit $?"
parleyd: unknown option '--'
Try 'parleyd --help' for more information.
exit 2
$ build/glibc/parleyd -p 6667 -- -v; echo "exit $?"
parleyd: unexpected argument '-v'
Try 'parleyd --help' for more information.
exit 2
$ build/glibc/parley -p 6667 bob; echo "exit $?"
parley: unexpected argument 'bob'
Try 'parley --help' for more information.
exit 2
$ build/musl/parleyd -vTp0 -h | head -1
usage: parleyd [-p PORT] [-b ADDR] [-m MAXCLIENTS] [-t IDLE] [-f LINES]
```

Every message is the same with glibc and musl (try them with `build/musl/`). Bundled options:
`-vTp6667` is `-v -T -p 6667`, `-Lp6667` is `-L -p 6667`:

```console
$ build/musl/parleyd -vTp6667 2>server.log & server=$!; sleep 0.2
$ printf 'hi\n' | build/glibc/parley -Lp6667 -nalice
-- Welcome to parley, alice
--> alice joined
-- Users: alice
<alice> hi
$ kill $server; wait $server; cat server.log; rm server.log
parleyd: debug: verbose logging is on
parleyd: info: listening on 127.0.0.1:6667
parleyd: debug: open file limit: 1048576
parleyd: debug: connection from 127.0.0.1:42656
parleyd: info: alice joined
parleyd: info: alice left (Quit)
parleyd: debug: closing connection from 127.0.0.1:42656
parleyd: info: got signal 15 (Terminated), shutting down
```

### Step 2: a benchmark client

How many messages per second can `parleyd` deliver? `tests/bench.c` opens CLIENTS connections from one
process. Each round, every client sends one `MSG`, and the round ends when every client has every message:
with 50 clients, 50 × 50 = 2500 lines.

This is a **lockstep** benchmark: a round starts only when the last is fully delivered. It measures *latency*
under load, not just throughput, so a server that holds lines back even briefly shows up at once.

`project/ch22/tests/bench.c`
```c
/* bench.c - how fast does a parleyd deliver messages?
 *
 *     bench PORT [CLIENTS [ROUNDS]]
 *
 * Connects CLIENTS clients (default 50) to the parleyd on 127.0.0.1:PORT.
 * In each of ROUNDS rounds (default 200) every client sends one message,
 * and we wait until every client has received all of them (the server
 * sends each message to everyone, the sender too). So CLIENTS * ROUNDS
 * messages go in, and CLIENTS * CLIENTS * ROUNDS lines come out.
 * Start the server with "-f 0" (no flood limit) and "-m" at least CLIENTS.
 * The numbers depend on the machine: compare runs on the same machine.
 */
#include <errno.h>
#include <poll.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "clock.h"
#include "io.h"
#include "msg.h"
#include "net.h"
#include "str.h"
#include "strbuf.h"

#define TIMEOUT_MS 5000     /* give up if the server is silent this long */

struct client {
    int fd;
    struct strbuf in;
    bool skipping;
    bool registered;        /* saw the end of the NAMES list */
    long got;               /* MSG lines received */
};

/* Is c done: registered (target < 0), or got target messages? */
static bool done(const struct client *c, long target)
{
    return target < 0 ? c->registered : c->got >= target;
}

/* Read from the clients until every one is done(target). False if the
 * server closed a connection or went quiet. */
static bool wait_all(struct client *cs, struct pollfd *pfds, int n, long target)
{
    char line[MSG_LINE_MAX + 1];
    for (;;) {
        int waiting = 0;
        for (int i = 0; i < n; i++)
            if (!done(&cs[i], target))
                pfds[waiting++] = (struct pollfd){ .fd = cs[i].fd, .events = POLLIN };
        if (waiting == 0)
            return true;
        int ready = poll(pfds, (nfds_t)waiting, TIMEOUT_MS);
        if (ready < 0 && errno == EINTR)
            continue;
        if (ready <= 0) {
            fprintf(stderr, "bench: %s\n", ready == 0 ? "timed out" : strerror(errno));
            return false;
        }
        for (int i = 0; i < n; i++) {
            struct client *c = &cs[i];
            if (done(c, target))
                continue;
            int k = 0;              /* c's entry in pfds (same order) */
            while (pfds[k].fd != c->fd)
                k++;
            if (pfds[k].revents == 0)
                continue;
            ssize_t got = read_into(c->fd, &c->in, 4096);
            if (got <= 0) {
                fprintf(stderr, "bench: client %d: %s\n", i,
                        got == 0 ? "the server closed the connection" : strerror(errno));
                return false;
            }
            enum frame_status st;
            while ((st = msg_take_line(&c->in, &c->skipping, line)) != FRAME_NONE) {
                if (st != FRAME_LINE)
                    continue;
                if (strstr(line, " MSG :") != NULL)
                    c->got++;
                else if (strncmp(line, ":parley 366 ", 12) == 0)
                    c->registered = true;
            }
        }
    }
}

int main(int argc, char *argv[])
{
    long port, n = 50, rounds = 200;
    if (argc < 2 || argc > 4 || !str_to_long(argv[1], 1, 65535, &port)
        || (argc > 2 && !str_to_long(argv[2], 1, 1000, &n))
        || (argc > 3 && !str_to_long(argv[3], 1, 100000, &rounds))) {
        fprintf(stderr, "usage: bench PORT [CLIENTS [ROUNDS]]\n");
        return 2;
    }

    struct client *cs = calloc((size_t)n, sizeof *cs);
    struct pollfd *pfds = calloc((size_t)n, sizeof *pfds);
    if (cs == NULL || pfds == NULL) {
        fprintf(stderr, "bench: out of memory\n");
        return 1;
    }
    char text[64], err[256];
    for (int i = 0; i < n; i++) {
        cs[i].fd = net_connect("127.0.0.1", (int)port, err, sizeof err);
        if (cs[i].fd < 0) {
            fprintf(stderr, "bench: cannot connect to %s\n", err);
            return 1;
        }
        int len = snprintf(text, sizeof text, "NICK b%d\r\n", i);
        if (write_all(cs[i].fd, text, (size_t)len) < 0) {
            fprintf(stderr, "bench: write: %s\n", strerror(errno));
            return 1;
        }
    }
    if (!wait_all(cs, pfds, (int)n, -1))
        return 1;

    long long start = clock_ms();
    for (long r = 0; r < rounds; r++) {
        for (int i = 0; i < n; i++) {
            int len = snprintf(text, sizeof text, "MSG :message %ld from b%d\r\n", r, i);
            if (write_all(cs[i].fd, text, (size_t)len) < 0) {
                fprintf(stderr, "bench: write: %s\n", strerror(errno));
                return 1;
            }
        }
        if (!wait_all(cs, pfds, (int)n, n * (r + 1)))
            return 1;
    }
    long long ms = clock_ms() - start;
    if (ms == 0)
        ms = 1;

    long long lines = (long long)n * n * rounds;
    printf("bench: %ld clients x %ld rounds: %lld lines delivered in %lld ms"
           " (%lld lines/s)\n", n, rounds, lines, ms, lines * 1000 / ms);
    for (int i = 0; i < n; i++) {
        close(cs[i].fd);
        sb_free(&cs[i].in);
    }
    free(cs);
    free(pfds);
    return 0;
}
```

- It reuses the project's modules: `net_connect()` ([Chapter 16](16-a-first-client.md)), `read_into()`,
  `write_all()`, `msg_take_line()`, `clock_ms()`, so it measures the same code.
- `wait_all()` polls only clients still waiting, and gives up after 5 seconds of silence: a benchmark must
  never hang.
- Registration (`NICK`, then wait for the `366` end of names) happens before the clock starts.

In the `Makefile`, the benchmark links like the integration test. It isn't part of `make test`: it checks no
result and takes time.

`project/ch22/Makefile` (part)
```make
#   make bench           build the benchmark client, build/glibc/bench
...
BENCH_OBJS  := $(addprefix $(BUILD)/, bench.o clock.o io.o net.o msg.o strbuf.o str.o)
...
.PHONY: all test test-all release install dist bench clean
...
$(BUILD)/bench: $(BENCH_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

bench: $(BUILD)/bench
```

(The `.PHONY` line already lists the targets of steps 4 and 5.)

#### Checkpoint

Start the server with `-f 0` (no flood limit: the benchmark sends far faster than 5 lines a second). Run 50
clients for 200 rounds, and 2 for comparison:

```console
$ make -s && make -s LIBC=musl && make -s bench
$ build/glibc/parleyd -p 6667 -T -f 0 2>/dev/null & server=$!; sleep 0.2
$ build/glibc/bench 6667 50 200
bench: 50 clients x 200 rounds: 500000 lines delivered in 6840 ms (73099 lines/s)
$ build/glibc/bench 6667 2 200
bench: 2 clients x 200 rounds: 800 lines delivered in 136 ms (5882 lines/s)
$ kill $server; wait $server
$ build/musl/parleyd -p 6667 -T -f 0 2>/dev/null & server=$!; sleep 0.2
$ build/glibc/bench 6667 50 200
bench: 50 clients x 200 rounds: 500000 lines delivered in 8302 ms (60226 lines/s)
$ build/glibc/bench 6667 2 200
bench: 2 clients x 200 rounds: 800 lines delivered in 48 ms (16666 lines/s)
$ kill $server; wait $server
```

It works, but 6840 ms for 200 rounds is **34 ms per round**, with either library. A round is 50 lines in and
2500 out, a few hundred kilobytes over localhost: well under a millisecond of CPU. Something is *waiting*.

### Step 3: `TCP_NODELAY`

In 1984 John Nagle described a TCP problem on slow networks: programs sending one character at a time (think
`telnet`) fill the network with 41-byte packets carrying 1 byte each. His fix, **Nagle's algorithm**
(RFC 896), is on by default in every TCP stack. The rule: while a sent segment is not yet acknowledged, hold
back small segments, and send them together when the acknowledgement arrives.

The receiver has the **delayed ACK** (RFC 1122): wait a little before acknowledging, in case there is data to
send back that can carry the ACK. Linux waits at least 40 ms. Together they trap a chat server:

1. The server writes a line to a client: a small segment, sent at once.
2. More lines of the round arrive for the same client; Nagle holds them back, as the first segment isn't
   acknowledged yet.
3. The client has nothing to send (it waits for the rest of the round), so it delays its ACK: 40 ms.
4. The ACK arrives, the held-back lines go out. Round over, 40 ms late.

With 2 clients there are fewer writes per round, so the trap closes less often: 136 ms for 200 rounds.

The fix: `TCP_NODELAY` (in `<netinet/tcp.h>`, with `IPPROTO_TCP` from `<netinet/in.h>`) turns Nagle off for
one socket, so every `write()` goes out at once. Interactive programs (`ssh`, games, chat) set it. Our server
already batches a client's output into one `write()` per event-loop pass ([Chapter 18](18-event-loop.md)), so
it doesn't need Nagle's.

In `src/server.c`, add the two headers and set the option on every accepted socket in `accept_clients()`,
just after the "server full" check:

`project/ch22/src/server.c` (part)
```c
#include <netinet/in.h>
#include <netinet/tcp.h>
...
        /* Send small writes at once. With Nagle's algorithm (the default)
         * TCP holds back a small segment while an earlier one is not yet
         * acknowledged, and the receiver may delay its ACK for up to 40 ms:
         * chat lines would trickle out. Failing is harmless: just slower. */
        int on = 1;
        if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &on, sizeof on) < 0)
            log_msg(LOG_DEBUG, "%s: TCP_NODELAY: %s", addr, strerror(errno));
```

`setsockopt()` is the call used for `SO_REUSEADDR` in [Chapter 15](15-a-first-server.md), at another
*level*: `IPPROTO_TCP` options belong to TCP, `SOL_SOCKET` ones to every socket. Failure only makes it slower,
so a debug message is enough.

#### Checkpoint

The same runs again:

```console
$ make -s && make -s LIBC=musl
$ build/glibc/parleyd -p 6667 -T -f 0 2>/dev/null & server=$!; sleep 0.2
$ build/glibc/bench 6667 50 200
bench: 50 clients x 200 rounds: 500000 lines delivered in 91 ms (5494505 lines/s)
$ build/glibc/bench 6667 2 200
bench: 2 clients x 200 rounds: 800 lines delivered in 8 ms (100000 lines/s)
$ kill $server; wait $server
$ build/musl/parleyd -p 6667 -T -f 0 2>/dev/null & server=$!; sleep 0.2
$ build/glibc/bench 6667 50 200
bench: 50 clients x 200 rounds: 500000 lines delivered in 120 ms (4166666 lines/s)
$ build/glibc/bench 6667 2 200
bench: 2 clients x 200 rounds: 800 lines delivered in 8 ms (100000 lines/s)
$ kill $server; wait $server
```

From 6840 ms to 91 ms: **75 times faster**, with one line of code. Only now does the C library make a visible
difference (91 vs 120 ms); before, both servers spent nearly all their time waiting for ACKs.

The course's most important performance lesson: **measure before you optimize.** Nobody would have found this
by reading `server.c`, and no faster `memcpy()` would have helped.

### Step 4: release builds and `make install`

The Makefile now learns the release build from the start of the chapter. `RELEASE=1` switches the flags and
the build directory, as `SANITIZE=1` does:

`project/ch22/Makefile` (part)
```make
#   make release         optimized, stripped binaries in build/glibc-release/
#                        (make release LIBC=musl: build/musl-release/)
#   make install         install the release binaries into $(DESTDIR)$(PREFIX)/bin
...
LIBC  ?= glibc
BUILD := build/$(LIBC)
PREFIX ?= /usr/local
...
# A release build: optimized, without debug information, and with the
# assert()s compiled out (-DNDEBUG makes assert() expand to nothing).
ifeq ($(RELEASE),1)
  CFLAGS   := -O2 -std=c99 -Wall -Wextra -pedantic -Werror
  CPPFLAGS += -DNDEBUG
  BUILD    := $(BUILD)-release
endif
...
# Build again with RELEASE=1, then strip the symbol tables: the binaries
# users run need no names of functions and variables.
release:
	$(MAKE) --no-print-directory all RELEASE=1
	strip $(BUILD)-release/parleyd $(BUILD)-release/parley

# DESTDIR lets packagers install into a staging directory.
install: release
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	cp $(BUILD)-release/parleyd $(BUILD)-release/parley $(DESTDIR)$(PREFIX)/bin/
```

- The release block *replaces* `CFLAGS` with `:=` (dropping `-g` and sanitizer flags) and adds `-DNDEBUG`.
  Its objects go to `build/glibc-release/` or `build/musl-release/`, never mixed with the differently
  compiled development objects.
- `release` calls make with `RELEASE=1`, then strips. `strip` isn't in the link rule because the unstripped
  release binary is useful too: gdb can still show function names.
- `install` follows two packager conventions. `PREFIX` is where the program lives (`/usr/local` by default,
  for programs you build yourself; distribution packages use `/usr`). `DESTDIR` is normally empty; a
  packager sets it to a staging directory, so `make install` copies into `stage/usr/bin` instead of
  `/usr/bin`, and the package is built from `stage/`. Installing into `/usr/local/bin` needs root:
  `sudo make install`.
- `make install LIBC=musl` installs the static musl binaries instead.

#### Checkpoint

```console
$ make -s && make -s all RELEASE=1
$ ls -l build/glibc/parleyd build/glibc-release/parleyd | awk '{print $5, $9}'
46000 build/glibc-release/parleyd
132640 build/glibc/parleyd
$ make release
make --no-print-directory all RELEASE=1
make[1]: Nothing to be done for 'all'.
strip build/glibc-release/parleyd build/glibc-release/parley
$ ls -l build/glibc-release/parleyd | awk '{print $5, $9}'
39144 build/glibc-release/parleyd
$ size build/glibc/parleyd build/glibc-release/parleyd
   text	   data	    bss	    dec	    hex	filename
  29079	   1184	    144	  30407	   76c7	build/glibc/parleyd
  28588	   1176	    144	  29908	   74d4	build/glibc-release/parleyd
$ file build/glibc/parleyd build/glibc-release/parleyd | sed "s/BuildID[^,]*, //"
build/glibc/parleyd:         ELF 64-bit LSB pie executable, x86-64, version 1 (SYSV), dynamically linked, interpreter /lib64/ld-linux-x86-64.so.2, for GNU/Linux 3.2.0, with debug_info, not stripped
build/glibc-release/parleyd: ELF 64-bit LSB pie executable, x86-64, version 1 (SYSV), dynamically linked, interpreter /lib64/ld-linux-x86-64.so.2, for GNU/Linux 3.2.0, stripped
$ nm build/glibc/parleyd | grep -c " T "; nm build/glibc-release/parleyd
51
nm: build/glibc-release/parleyd: no symbols
$ nm -D build/glibc/parleyd | grep assert; nm -D build/glibc-release/parleyd | grep -c assert
                 U __assert_fail@GLIBC_2.2.5
0
$ make -s release LIBC=musl
$ ls -l build/musl-release/parleyd | awk '{print $5, $9}'
104144 build/musl-release/parleyd
$ make -s install DESTDIR=$PWD/stage PREFIX=/usr; find stage -type f; stage/usr/bin/parleyd --version
stage/usr/bin/parleyd
stage/usr/bin/parley
parleyd 0.1
$ rm -rf stage
```

(The `sed` hides the build ID, a checksum that differs per machine.) Where did the 93 KB go, from 132640 to
39144 bytes? ⚠

- The debug information (`-g`) was most of it: the unstripped release build is 46000 bytes.
- `strip` removed the symbol table, another 7 KB: `nm` finds nothing to show.
- The asserts were 491 bytes of code (`text` 29079 → 28588), and the release build no longer imports
  `__assert_fail()`.

`size` hardly changed: the code that runs is nearly the same; the rest was information for you and gdb. The
musl release binary, 104144 bytes, is bigger because it contains the C library parts it uses (compared in
"glibc vs musl: the release binaries" below). `make release` printed "Nothing to be done" because the line
before had built everything already.

### Step 5: version 1.0, a README and `make dist`

Time to call it 1.0. In `src/version.h`:

`project/ch22/src/version.h`
```c
/* version.h - the version shown by --version. */
#ifndef PARLEY_VERSION_H
#define PARLEY_VERSION_H

#define PARLEY_VERSION "1.0"

#endif
```

A program people download needs a `README.md`: what it is, how to build and run it. Here is its start; the
rest of `project/ch22/README.md` documents the client, the protocol, the benchmark and the source layout:

`project/ch22/README.md` (part)
````markdown
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
````

The last rule packs a **source tarball**, the usual way to release C code: unpack, run `make`, done. The
version is read from `src/version.h` with `sed`, so it is written in one place only:

`project/ch22/Makefile` (part)
```make
#   make dist            the source tarball parley-VERSION.tar.gz
#   make clean           remove build/ and the tarball
...
# The version, read from src/version.h, so it is written in one place only.
VERSION := $(shell sed -n 's/^#define PARLEY_VERSION "\(.*\)"/\1/p' src/version.h)
...
# Everything that goes into the source tarball.
DIST_FILES := Makefile README.md $(wildcard src/*.[ch] tests/*.[ch])

.PHONY: all test test-all release install dist bench clean
...
# The tarball unpacks into parley-VERSION/.
dist:
	rm -rf build/parley-$(VERSION)
	mkdir -p build/parley-$(VERSION)/src build/parley-$(VERSION)/tests
	for f in $(DIST_FILES); do cp $$f build/parley-$(VERSION)/$$f || exit 1; done
	tar -C build -cf - parley-$(VERSION) | gzip -9 > parley-$(VERSION).tar.gz
	rm -rf build/parley-$(VERSION)
...
clean:
	rm -rf build parley-*.tar.gz
```

- `$(shell ...)` runs a command when make reads the Makefile; `sed -n 's/.../\1/p'` prints only the part
  between the quotes. `$(wildcard ...)` expands a file pattern like the shell's `*`. Both are GNU make
  features.
- The tarball unpacks into one directory, `parley-1.0/`, never into the current one: users rely on that
  convention.
- `DIST_FILES` lists only sources, so `build/`, editor backups and old tarballs stay out. In a recipe, `$$f`
  is the shell's `$f` (make would expand a single `$`), and `|| exit 1` stops at the first failed `cp`.
- `tar -cf - DIR | gzip -9 > FILE` is the portable spelling of `tar czf`: POSIX `tar` (`pax`, really) has no
  `-z`, though GNU and BSD tar do.

#### Checkpoint

The real test of a tarball: unpack it elsewhere and build it there, away from your working directory:

```console
$ make -s dist; tar tzf parley-1.0.tar.gz | sort
parley-1.0/
parley-1.0/Makefile
parley-1.0/README.md
parley-1.0/src/
parley-1.0/src/clock.c
parley-1.0/src/clock.h
parley-1.0/src/conn.c
parley-1.0/src/conn.h
parley-1.0/src/io.c
parley-1.0/src/io.h
parley-1.0/src/log.c
parley-1.0/src/log.h
parley-1.0/src/msg.c
parley-1.0/src/msg.h
parley-1.0/src/net.c
parley-1.0/src/net.h
parley-1.0/src/nick.c
parley-1.0/src/nick.h
parley-1.0/src/parley.c
parley-1.0/src/parleyd.c
parley-1.0/src/room.c
parley-1.0/src/room.h
parley-1.0/src/server.c
parley-1.0/src/server.h
parley-1.0/src/sig.c
parley-1.0/src/sig.h
parley-1.0/src/str.c
parley-1.0/src/str.h
parley-1.0/src/strbuf.c
parley-1.0/src/strbuf.h
parley-1.0/src/term.c
parley-1.0/src/term.h
parley-1.0/src/ui.c
parley-1.0/src/ui.h
parley-1.0/src/version.h
parley-1.0/tests/
parley-1.0/tests/bench.c
parley-1.0/tests/fuzz.c
parley-1.0/tests/itest.c
parley-1.0/tests/test.h
parley-1.0/tests/test_io.c
parley-1.0/tests/test_msg.c
parley-1.0/tests/test_net.c
parley-1.0/tests/test_nick.c
parley-1.0/tests/test_sig.c
parley-1.0/tests/test_str.c
parley-1.0/tests/test_strbuf.c
parley-1.0/tests/tests.h
parley-1.0/tests/unit.c
$ mkdir -p build/unpack && tar -C build/unpack -xzf parley-1.0.tar.gz && make -s -C build/unpack/parley-1.0 test LIBC=musl
unit: 30 passed, 0 failed
fuzz: 20000 rounds (seed 1), 0 failed
itest: 7 passed, 0 failed
$ make -s && build/glibc/parley --version
parley 1.0
```

Unpacked inside `build/` (so `make clean` removes it), it built and passed every test with musl. Exercise 2
turns this into a `make distcheck` rule.

### glibc vs musl: the release binaries

The two finished programs, from `make release` and `make release LIBC=musl`, with the development builds for
reference. ⚠ All sizes depend on the compiler and library versions:

```console
$ ls -l build/glibc/parleyd build/glibc-release/parleyd build/musl/parleyd build/musl-release/parleyd build/glibc-release/parley build/musl-release/parley | awk '{print $5, $9}'
39144 build/glibc-release/parley
39144 build/glibc-release/parleyd
132640 build/glibc/parleyd
100048 build/musl-release/parley
104144 build/musl-release/parleyd
200200 build/musl/parleyd
$ size build/glibc-release/parleyd build/musl-release/parleyd
   text	   data	    bss	    dec	    hex	filename
  28588	   1176	    144	  29908	   74d4	build/glibc-release/parleyd
  90964	    840	   3016	  94820	  17264	build/musl-release/parleyd
$ ldd build/glibc-release/parleyd | sed 's/ (0x.*)//'
	linux-vdso.so.1
	libc.so.6 => /lib/x86_64-linux-gnu/libc.so.6
	/lib64/ld-linux-x86-64.so.2
$ ldd build/musl-release/parleyd; echo "exit $?"
	not a dynamic executable
exit 1
```

(Both glibc release binaries are exactly 39144 bytes: ELF segments start on 4096-byte page boundaries, so
small code differences often vanish in the padding. The `sed` hides load addresses that change every run.)

- The glibc `parleyd` is 38 KB but doesn't run alone: `ldd` shows it needs `libc.so.6` (about 2 MB) and the
  loader, shared by every program on the system.
- The musl `parleyd` is 102 KB and needs *nothing*: it runs on any x86-64 Linux machine. Its extra 62 KB
  of `text` is the part of musl it uses (`printf()`, `poll()`, `getaddrinfo()`, `malloc()`, …): the whole
  C library *as far as this program needs it*.

A static glibc build works, with the warning from earlier, and is big:

```console
$ gcc -static -o build/parley-glibc-static build/glibc-release/{parley,ui,term,io,net,sig,msg,strbuf,str,log}.o
/usr/bin/ld: build/glibc-release/net.o: in function `resolve':
net.c:(.text+0x7f): warning: Using 'getaddrinfo' in statically linked applications requires at runtime the shared libraries from the glibc version used for linking
$ strip build/parley-glibc-static; ls -l build/parley-glibc-static | awk '{print $5, $9}'
1213296 build/parley-glibc-static
```

1.2 MB against 98 KB for musl: twelve times bigger, and it still reads the NSS configuration at run time.
With a server on port 6667, trace which files each static client opens to look up `localhost`:

```console
$ build/musl-release/parleyd -p 6667 -T 2>server.log & server=$!; sleep 0.2
$ printf 'hi\n' | strace -o glibc.trace -e trace=open,openat build/parley-glibc-static -h localhost -p 6667 -n gstatic; grep -v '^+++' glibc.trace
-- Welcome to parley, gstatic
--> gstatic joined
-- Users: gstatic
<gstatic> hi
openat(AT_FDCWD, "/etc/nsswitch.conf", O_RDONLY|O_CLOEXEC) = 5
openat(AT_FDCWD, "/etc/host.conf", O_RDONLY|O_CLOEXEC) = 5
openat(AT_FDCWD, "/etc/resolv.conf", O_RDONLY|O_CLOEXEC) = 5
openat(AT_FDCWD, "/etc/hosts", O_RDONLY|O_CLOEXEC) = 5
$ printf 'hi\n' | strace -o musl.trace -e trace=open,openat build/musl-release/parley -h localhost -p 6667 -n mstatic; grep -v '^+++' musl.trace
-- Welcome to parley, mstatic
--> mstatic joined
-- Users: mstatic
<mstatic> hi
open("/etc/hosts", O_RDONLY|O_LARGEFILE|O_CLOEXEC) = 5
$ kill $server; wait $server; echo "exit $?"; rm -f *.trace server.log
exit 0
```

(The warning's `/usr/bin/ld:` prefix may name another path on your machine.) glibc reads four configuration
files, exactly like the dynamic client; `hosts: files dns` needs no plug-in, so this lookup works. musl reads
`/etc/hosts`, finds `localhost`, done. (musl always passes `O_LARGEFILE`; it means nothing on 64-bit
systems.)

Finally, start-up. `parleyd --version` does almost nothing, so it shows what starting costs:

```console
$ strace -o glibc.trace build/glibc-release/parleyd --version; wc -l < glibc.trace
parleyd 1.0
40
$ strace -o musl.trace build/musl-release/parleyd --version; cat musl.trace; rm -f *.trace
parleyd 1.0
execve("build/musl-release/parleyd", ["build/musl-release/parleyd", "--version"], 0x7ffcafa808b8 /* 44 vars */) = 0
arch_prctl(ARCH_SET_FS, 0x41abf8)       = 0
set_tid_address(0x41ad30)               = 196481
ioctl(1, TIOCGWINSZ, 0x7ffcedbf7628)    = -1 ENOTTY (Inappropriate ioctl for device)
writev(1, [{iov_base="parleyd 1.0", iov_len=11}, {iov_base="\n", iov_len=1}], 2) = 12
exit_group(0)                           = ?
+++ exited with 0 +++
$ TIMEFORMAT='%R s'; time (for i in $(seq 1000); do build/glibc-release/parleyd --version; done >/dev/null)
0.455 s
$ TIMEFORMAT='%R s'; time (for i in $(seq 1000); do build/musl-release/parleyd --version; done >/dev/null)
0.231 s
```

The musl program makes **6 system calls** in all: set up thread-local storage (`arch_prctl`), register the
thread ID (`set_tid_address`), ask whether standard output is a terminal (to choose line or full buffering;
here it isn't), write, exit. glibc's dynamic start-up takes about 40 lines of trace: loading and mapping
`libc.so.6`, memory protection, and more. ⚠ musl starts twice as fast, 0.23 against 0.46 ms, and both are
fast. That matters for programs started thousands of times (shell scripts, build tools), not for a server.

### glibc vs musl: the server under load

The benchmark again, with the release servers and 50 clients × 2000 rounds. The same glibc benchmark client
runs against both, so only the server's C library differs. Twice each, then the server's CPU time from
`/proc/PID/stat` (fields 14 and 15: user and system time, in hundredths of a second):

```console
$ make -s bench
$ build/glibc-release/parleyd -p 6667 -T -f 0 2>/dev/null & server=$!; sleep 0.2
$ build/glibc/bench 6667 50 2000; build/glibc/bench 6667 50 2000
bench: 50 clients x 2000 rounds: 5000000 lines delivered in 784 ms (6377551 lines/s)
bench: 50 clients x 2000 rounds: 5000000 lines delivered in 787 ms (6353240 lines/s)
$ echo "server CPU time, user/system (1/100 s): $(cut -d' ' -f14,15 /proc/$server/stat)"; kill $server; wait $server
server CPU time, user/system (1/100 s): 25 92
$ build/musl-release/parleyd -p 6667 -T -f 0 2>/dev/null & server=$!; sleep 0.2
$ build/glibc/bench 6667 50 2000; build/glibc/bench 6667 50 2000
bench: 50 clients x 2000 rounds: 5000000 lines delivered in 1395 ms (3584229 lines/s)
bench: 50 clients x 2000 rounds: 5000000 lines delivered in 1410 ms (3546099 lines/s)
$ echo "server CPU time, user/system (1/100 s): $(cut -d' ' -f14,15 /proc/$server/stat)"; kill $server; wait $server
server CPU time, user/system (1/100 s): 156 83
```

⚠ On this machine the glibc server delivers **about 1.8 times as many lines per second**. The CPU times say
why:

| server CPU time, two runs ⚠ | glibc | musl |
|---|---|---|
| system (the kernel: `poll()`, `read()`, `write()`, TCP) | 0.92 s | 0.83 s |
| user (our code and the C library) | 0.25 s | 1.56 s |

The kernel's work, the TCP traffic, is about the same. The difference is in user space. Per message, the hot
path searches the input with `memchr()`, builds the line per recipient in `msg_format()` with `strlen()` and
`memcpy()`, and appends it to 50 output buffers with `memcpy()`: millions of small string calls, where "How
fast is the C library?" found glibc several times faster. Exercise 4 shows the gap shrinking with fewer
clients.

A reason to ship the glibc build? For *this* program, no: five million lines in 1.4 seconds is far more than
any chat room sends, and with a handful of clients the two are nearly equal. For a server whose bottleneck
really is formatting and copying, it can be.

### Checkpoint

The whole project, both libraries:

```console
$ make -s test-all
unit: 30 passed, 0 failed
fuzz: 20000 rounds (seed 1), 0 failed
itest: 7 passed, 0 failed
unit: 30 passed, 0 failed
fuzz: 20000 rounds (seed 1), 0 failed
itest: 7 passed, 0 failed
$ build/glibc/parleyd -x; echo "exit $?"; build/glibc/parleyd -m; echo "exit $?"; build/glibc/parleyd extra -T; echo "exit $?"
parleyd: unknown option '-x'
Try 'parleyd --help' for more information.
exit 2
parleyd: option -m needs a value
Try 'parleyd --help' for more information.
exit 2
parleyd: unexpected argument 'extra'
Try 'parleyd --help' for more information.
exit 2
$ build/musl/parleyd -x; echo "exit $?"; build/musl/parleyd -m; echo "exit $?"; build/musl/parleyd extra -T; echo "exit $?"
parleyd: unknown option '-x'
Try 'parleyd --help' for more information.
exit 2
parleyd: option -m needs a value
Try 'parleyd --help' for more information.
exit 2
parleyd: unexpected argument 'extra'
Try 'parleyd --help' for more information.
exit 2
$ build/musl/parleyd -vTp0 -h | head -1; build/glibc/parley --version
usage: parleyd [-p PORT] [-b ADDR] [-m MAXCLIENTS] [-t IDLE] [-f LINES]
parley 1.0
$ make -s install DESTDIR=$PWD/stage PREFIX=/usr; ls stage/usr/bin; stage/usr/bin/parleyd --version; rm -rf stage
parley
parleyd
parleyd 1.0
$ make clean >/dev/null && ls
Makefile
README.md
src
tests
```

The complete project at this point is in `project/ch22/`.

## Which C library should you choose?

Twenty-two chapters ran every program with both libraries; here is what they found. ⚠ The numbers come from
one machine (x86-64, glibc 2.35, musl 1.2.6, gcc 11.4); the reasons hold everywhere.

| | glibc | musl | better |
|---|---|---|---|
| **Size** of `parleyd`, release, stripped | 39144 B, plus the shared `libc.so.6` (2.2 MB) | 104144 B, self-contained | glibc on one system; musl to copy around |
| A static client (`parley`) | 1.2 MB, with a link warning | 98 KB | musl |
| The C library itself: `libc.so` / `libc.a` | 2.2 MB / 6.0 MB | 0.8 MB / 2.8 MB | musl |
| **Start-up**: system calls of `nothing` | 34 dynamic, 16 static | 4 | musl |
| 1000 starts of `nothing` | 0.45 s dynamic, 0.36 s static | 0.25 s | musl |
| **Static linking with name lookup** (`getaddrinfo()`, `getpwnam()`) | loads NSS plug-ins at run time; can crash (this chapter) | works, nothing to load | musl |
| **Where names come from** | anything with an NSS plug-in: files, DNS, LDAP, systemd, mDNS | `/etc/hosts`, `/etc/passwd`, `/etc/group`, DNS | glibc (for networks that need it) |
| **Speed** of `malloc`/`free`, `snprintf()`, `memchr()` | 51, 317, 30 ms | 95, 1990, 82 ms | glibc, 2–6× |
| `parleyd` under load, 50 clients | 6.4 M lines/s | 3.6 M lines/s | glibc |
| `parleyd`, 1–5 clients (Exercise 4) | about the same | about the same | — |
| **Default thread stack** ([Chapter 17](17-threads.md)) | 8 MiB (from `ulimit -s`) | 128 KiB: a 256 KiB local array crashes | glibc for unprepared code; musl makes you size stacks |
| **Heap bugs** without tools ([Chapter 8](08-memory.md)) | many named: "double free detected in tcache 2" | crashes at once, no message; the allocator protects itself | glibc for messages, musl for hardening |
| **Sanitizers** ([Chapter 10](10-errors-and-debugging.md)) | ASan and UBSan, with messages | no ASan; UBSan only in trap mode | glibc, clearly |
| gdb, strace | work | work (one plain `strlen()`, no CPU variants) | — |
| **Locales** ([Chapter 20](20-terminal-ui.md)) | the installed ones, with translations, collation | always UTF-8, any name accepted | glibc for languages, musl for simplicity |
| **`getopt()`** | permutes unless only POSIX feature macros are defined; its own messages | POSIX only; other messages | musl (one behaviour) |
| **Strictness** ([Chapter 9](09-preprocessor-and-building.md)) | hides extensions unless a feature macro asks: `accept4()`, `pipe2()`, `POLLRDHUP` need `_GNU_SOURCE` | declares more: those three even under plain `-std=c99` | glibc catches missing feature macros; musl catches glibc-only extensions |
| **Licence** | LGPL 2.1+: static linking has obligations | MIT: none to speak of | musl |
| **Security fixes** | the system updates `libc.so.6` for every dynamic program | static programs must be rebuilt | glibc (dynamic) |
| **Where it is the default** | Debian, Ubuntu, Fedora, RHEL, Arch, almost every desktop and server | Alpine Linux (common in containers), Void Linux's musl variant | — |

No row wins outright. The choice follows from *how the program is shipped*.

**Choose glibc, dynamically linked (the default), when:**

- the program is installed as a package on a normal Linux distribution: the system updates its library,
  and every program shares one copy;
- users or hosts can come from LDAP, Active Directory, systemd or mDNS: only NSS sees them;
- the program spends real time in `malloc()`, `printf()` or string functions;
- you develop and debug: the sanitizers need it, and its heap checks name what went wrong;
- you need real locales: translated messages, sorting by language, local date formats.

**Choose musl, statically linked, when:**

- one file must run on any Linux of the same CPU, whatever its C library: command-line tools you hand out,
  rescue and installer images, embedded systems;
- containers should be small: a static musl binary needs no base image;
- the program starts very often and does little each time;
- you want a library you can read: musl's code is short and behaves the same whatever macros you define;
- you want no licence questions about static linking.

**And always: develop with glibc, test with both**, as this course did with one Makefile and
`make test-all`. The sanitizers run on glibc, and glibc's stricter headers catch a missing feature macro. The
musl build catches glibc-only extensions (`<error.h>`, `<execinfo.h>`, GNU `strerror_r()`) and code that
relies on glibc's behaviour: every difference this course found was a place where the program relied on
something it shouldn't, or where the standard leaves a choice. A program that passes on both will very likely
run on the next system too.

For `parley`: the server goes to a Linux server as a package, so dynamic glibc; the client gets copied to
other machines, so hand out the static musl build. Both are one `make` away.

## Portability: BSD and macOS

`parley` uses only C99 and POSIX.1-2008, which the BSDs and macOS implement too. This course was tested only
on Linux, so take this section as a map, not a report:

- **Building.** The Makefile uses GNU make features (`ifeq`, `$(shell)`, `$(wildcard)`). The BSDs' own
  `make` reads a different syntax; install GNU make and run `gmake`. `$(CC)` is `cc`, which is clang on
  FreeBSD, OpenBSD and macOS, with the same warning flags. `make LIBC=musl` is Linux-only.
- **Static linking.** Linux promises never to break its system call interface, so a static musl binary runs
  on any Linux kernel of the last decade or so. Apple makes no such promise and doesn't support static
  programs: `-static` fails, and programs always use `libSystem` dynamically.
- **Behaviour.** BSD `getopt()` doesn't permute either, and the leading `:` with our own messages gives one
  behaviour everywhere. There is no `strace` (FreeBSD has `truss`, OpenBSD `ktrace`, macOS `dtruss`), and
  the benchmark section's `/proc/PID/stat` is Linux-only.
- **Feature macros.** On BSD and macOS, `_POSIX_C_SOURCE` hides the system's extensions, as on glibc.
  `parley` needs none; a program that does must ask for them on each system.

Can a program find out which library it was built with? Partly:

`examples/ch22/whichlibc.c`

```c
/* whichlibc.c - can a program tell which C library it was built with? */
#include <stdio.h>
#include <stdlib.h>         /* glibc defines __GLIBC__ in every header */

int main(void)
{
#ifdef __GLIBC__
    printf("glibc %d.%d\n", __GLIBC__, __GLIBC_MINOR__);
#else
    printf("not glibc: musl, a BSD libc, ... (no way to tell)\n");
#endif
#ifdef __linux__
    printf("compiled for Linux\n");
#endif
    return 0;
}
```

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o whichlibc whichlibc.c
$ musl-gcc -std=c99 -Wall -Wextra -pedantic -static -o whichlibc-musl whichlibc.c
$ ./whichlibc
glibc 2.35
compiled for Linux
$ ./whichlibc-musl
not glibc: musl, a BSD libc, ... (no way to tell)
compiled for Linux
```

`__GLIBC__` and `__GLIBC_MINOR__` come from glibc's `<features.h>`, which every glibc header includes;
`__linux__` comes from the compiler. musl has **no** `__MUSL__` macro, on purpose: its FAQ explains that code
asking "which library am I on?" is almost always guessing wrong about what that library does, and breaks
with the next release. Test the *feature* instead: is `strlcpy()` declared, is the thread stack big enough
(ask for the size you need, as in [Chapter 17](17-threads.md)), does `setlocale()` succeed. `parley` never
needs to know; it uses only what C99 and POSIX promise.

## After C99

This course used C99: supported everywhere, and the base of everything since. Three newer standards exist:

- **C11** (2011): `_Static_assert` (compile-time checks), `_Generic` (choose an expression by type),
  `_Alignas`/`_Alignof`, `_Noreturn`, anonymous structs and unions, `<stdatomic.h>` (lock-free atomic
  variables), `<threads.h>` (a small portable cousin of pthreads, in glibc since 2.28 and in musl), and
  `aligned_alloc()`. Variable-length arrays became optional.
- **C17** (2018) only fixed defects in C11. `-std=c17` is a good default today.
- **C23** (2024), the biggest change since C99: `bool`, `true`, `false`, `static_assert` and `alignof` become
  keywords (no `<stdbool.h>` needed), plus `nullptr`, `typeof`, `constexpr` objects, attributes like
  `[[nodiscard]]` and `[[fallthrough]]`, binary constants `0b1010`, digit separators `1'000'000`, `#embed`
  for a file's bytes, and `strdup()` in the standard library. K&R-style definitions are gone, and `int f()`
  finally means `int f(void)`. gcc 11 implements part of it as `-std=c2x`; newer compilers implement it
  fully, and gcc 15 makes C23 its default.

The two C11 features you'll meet most:

`examples/ch22/c11.c`

```c
/* c11.c - two things C11 added: compile-time checks and type-generic code. */
#include <stdio.h>

#define TYPE_NAME(x) _Generic((x), int: "int", double: "double", default: "other")

struct header {
    char tag[4];
    unsigned int length;
};

/* Checked by the compiler; a failing check stops the build. */
_Static_assert(sizeof(struct header) == 8, "struct header must be 8 bytes");

int main(void)
{
    printf("%s %s %s\n", TYPE_NAME(42), TYPE_NAME(4.2), TYPE_NAME("x"));
    return 0;
}
```

```console
$ gcc -std=c11 -Wall -Wextra -pedantic -o c11 c11.c
$ ./c11
int double other
$ musl-gcc -std=c11 -Wall -Wextra -pedantic -static -o c11-musl c11.c
$ ./c11-musl
int double other
$ gcc -std=c99 -Wall -Wextra -pedantic -o c11 c11.c 2>&1 | grep warning
c11.c:4:22: warning: ISO C99 does not support '_Generic' [-Wpedantic]
c11.c:4:22: warning: ISO C99 does not support '_Generic' [-Wpedantic]
c11.c:4:22: warning: ISO C99 does not support '_Generic' [-Wpedantic]
```

- `_Static_assert(expr, "message")` is checked by the compiler: if `struct header` ever grows, the build
  stops with the message. C99 needs a trick such as an array of negative size.
- `_Generic` picks the expression for the type of its first operand; `"x"` is a `char *`, matching neither
  `int` nor `double`. Its main use is in macros such as `<tgmath.h>`'s, where `sqrt(x)` calls `sqrtf()` for
  a `float`.
- With `-std=c99 -pedantic`, gcc warns about each `_Generic`, but (gcc 11) silently accepts
  `_Static_assert` as an extension. Don't rely on that in C99 code; other compilers may refuse it.
- C11 language features come from the compiler, not the library: musl and glibc give the same result.

Moving `parley` to a newer standard costs nothing; it compiles cleanly as C11, C17 and C2x:

```console
$ for s in c11 c17 c2x; do gcc -std=$s -D_POSIX_C_SOURCE=200809L -Wall -Wextra -pedantic -Werror -fsyntax-only src/*.c && echo "$s: ok"; done
c11: ok
c17: ok
c2x: ok
```

(`-fsyntax-only` checks without writing files.) Good C99 is good C17. For a new project, `-std=c17` with the
same warnings is a fine choice; use C23 features once all your compilers support them.

## Where to go next

You have written a complete network program in C, from `int main(void)` to a release tarball. Where next:

- **Read real C.** musl's source shows how a C library works in short, clear functions: `src/string/`,
  `src/stdio/`, `src/network/`. Then a well-written program in your area: the OpenBSD sources, SQLite,
  Redis, `curl`.
- **Scale the event loop.** `poll()` scans every descriptor each time. `epoll` (Linux) and `kqueue` (BSD,
  macOS) scale to many thousands of connections; libevent and libuv wrap both.
- **Encrypt.** Real chat needs TLS. OpenSSL, LibreSSL's `libtls` or BearSSL add it around the same
  `read()`/`write()` calls.
- **Speak real IRC.** The protocol of [Appendix C](appendix-c-parley-protocol.md) is a small subset of IRC
  (RFC 1459, and the modern RFC 2812). Add channels, private messages, `MODE`, and connect a real IRC client.
- **Learn the tools deeper.** gdb (watchpoints, core files), valgrind, `perf`, and fuzzing with AFL++ or
  libFuzzer, the grown-up versions of [Chapter 21](21-robustness.md)'s fuzzer.
- **Read the standards.** Free drafts of the C standards (N1256 for C99, N3220 for C23) and the POSIX
  specification are online. When this course said "the standard says", that is where to check.
- **Another systems language.** Having written C, you know what Rust's ownership rules, Zig's allocators or
  Go's goroutines protect you from, and what they cost.

## Summary

- A **release build** drops `-g`, compiles out `assert()` with `-DNDEBUG`, and is stripped. `size`, `nm`,
  `strip`, `file` and `ldd` show what is inside a binary. No side effects inside `assert()`.
- **`getopt()`** parses POSIX options: bundling (`-vTp6667`), values, `--`. A leading `:` silences the
  library's messages so yours read the same with every libc. glibc's permutes unless only POSIX feature
  macros (like ours) are defined; musl's and the BSDs' never do.
- **Measure before you optimize.** The benchmark found a 75× slowdown no code review would: Nagle's
  algorithm plus delayed ACKs. One `setsockopt(TCP_NODELAY)` fixed it.
- **`make install`** honours `PREFIX` and `DESTDIR`; **`make dist`** packs a tarball that unpacks into
  `NAME-VERSION/`. Test it by building it somewhere else.
- **Start-up**: musl's static start-up is a handful of system calls; dynamic glibc loads `libc.so.6` first.
  musl is about twice as fast, which matters only for programs started very often.
- **Static linking**: musl's static binaries are small and self-contained. Static glibc binaries are large
  and still load NSS plug-ins for name lookups, which can fail at run time (here, one crashed). glibc is
  LGPL (static linking has obligations); musl is MIT.
- **Speed**: glibc's `malloc()`, `printf()` and string functions are several times faster (IFUNC,
  CPU-specific assembly, per-thread caches); musl chooses small, simple, hardened code. For `parleyd` it
  shows only under heavy load.
- **Which libc**: dynamic glibc for distribution packages, NSS, speed, locales and sanitizers; static musl
  for programs that must run anywhere, small containers and fast start-up. Develop with glibc (its stricter
  headers catch missing feature macros), test with both (musl catches glibc-only extensions).
- C99 and POSIX code ports easily: GNU make is `gmake` on BSD, macOS has no static binaries, and there is no
  `__MUSL__`: test for features, not libraries. Good C99 compiles unchanged as C11, C17 and C23 (`-std=c2x`
  in gcc 11).

## Exercises

1. Add `-V` as a short form of `--version` to `parleyd`, so it can be bundled like the other flags
   (`-vV`). Update the help text.

<details><summary>Solution</summary>

In `parse_args()` in `src/parleyd.c`, add `V` to the option string and a case before `case 'h':`:

```c
    while ((opt = getopt(argc, argv, ":p:b:m:t:f:vTVh")) != -1) {
        switch (opt) {
        ...
        case 'V':
            printf("parleyd " PARLEY_VERSION "\n");
            exit(0);
        case 'h':
```

and in `print_help()`:

```c
           "  -V, --version  show the version and exit\n",
```

```console
$ make -s && make -s LIBC=musl
$ build/glibc/parleyd -V; build/glibc/parleyd -vV; build/musl/parleyd -TV
parleyd 1.0
parleyd 1.0
parleyd 1.0
$ build/glibc/parleyd -h | grep version
  -V, --version  show the version and exit
```

`getopt()` finds `-V` anywhere among bundled flags. In `-vV`, `-v` is set first, then ignored as `-V` exits.

</details>

2. Write a `make distcheck` rule: build the tarball, unpack it into `build/distcheck/`, and run `make test`
   in the copy. Then break `DIST_FILES` by changing `tests/*.[ch]` to `tests/*.c`, and watch `distcheck`
   catch it.

<details><summary>Solution</summary>

```make
.PHONY: all test test-all release install dist distcheck bench clean
...
# Check the tarball: unpack it and run the tests in the copy.
distcheck: dist
	rm -rf build/distcheck
	mkdir -p build/distcheck
	tar -C build/distcheck -xzf parley-$(VERSION).tar.gz
	$(MAKE) --no-print-directory -C build/distcheck/parley-$(VERSION) test
```

```console
$ make -s distcheck; echo "exit $?"
unit: 30 passed, 0 failed
fuzz: 20000 rounds (seed 1), 0 failed
itest: 7 passed, 0 failed
exit 0
```

With `tests/*.c`, the test headers are missing from the tarball, and the copy fails to build:

```console
$ make -s distcheck; echo "exit $?"
tests/unit.c:15:10: fatal error: test.h: No such file or directory
   15 | #include "test.h"
      |          ^~~~~~~~
compilation terminated.
make[1]: *** [Makefile:139: build/glibc/unit.o] Error 1
make: *** [Makefile:129: distcheck] Error 2
exit 2
```

Your working directory still builds, because `tests/test.h` is there; only a clean copy shows what you
forgot to ship. (GNU Automake generates a `distcheck` target that does this and more.) Put `tests/*.[ch]`
back afterwards.

</details>

3. Build the release binaries with `-Os` instead of `-O2`. How much smaller are they? Are they slower?
   Run the benchmark (50 clients, 2000 rounds) three times against each server.

<details><summary>Solution</summary>

Change the release line in the Makefile:

```make
  CFLAGS   := -Os -std=c99 -Wall -Wextra -pedantic -Werror
```

and run `make clean; make release; make release LIBC=musl; make bench`, then the benchmark as in "glibc vs
musl: the server under load". On this machine: ⚠

| `parleyd` release | glibc | musl |
|---|---|---|
| size with `-O2` | 39144 B | 104144 B |
| size with `-Os` | 35112 B | 96016 B |
| 3 runs with `-O2` | 851, 1055, 836 ms | 2021, 1416, 1401 ms |
| 3 runs with `-Os` | 1076, 1120, 905 ms | 944, 1241, 1254 ms |

`-Os` saves 8–10% of the file (only our code; musl's library code was compiled when musl was built). The
times are noisy (a busy machine; one build varied by up to 45%). Within that noise `-Os` isn't clearly
slower: the server spends its time in the kernel and the C library, not our code. For tight computing loops
`-O2` usually wins. The lesson is how to measure: repeat runs, and don't trust a difference smaller than the
spread. Set `-O2` back afterwards.

</details>

4. Run the benchmark against both release servers with 1, 5, 20, 50 and 100 clients (2000 rounds each).
   How does the difference between the libraries change? What happens with 100?

<details><summary>Solution</summary>

```console
$ for n in 1 5 20 50 100; do build/glibc/bench 6667 $n 2000; done
```

On this machine: ⚠

| clients | glibc server | musl server | musl / glibc |
|---|---|---|---|
| 1 | 99 ms (20202 lines/s) | 109 ms (18348 lines/s) | 1.1 |
| 5 | 187 ms (267379 lines/s) | 200 ms (250000 lines/s) | 1.07 |
| 20 | 418 ms (1913875 lines/s) | 583 ms (1372212 lines/s) | 1.4 |
| 50 | 1101 ms (4541326 lines/s) | 1429 ms (3498950 lines/s) | 1.3 |

With few clients, each round is a few small reads and writes: the time goes into system calls and the TCP
round trip, the same for both libraries. With many clients, each message is formatted and copied once per
client, so user-space work, where glibc's string functions are faster, grows with the square of the number
of clients. (The machine was busier than in "the server under load"; compare ratios within one table.)

With 100 clients the benchmark fails:

```console
$ build/glibc/bench 6667 100 2000; echo "exit $?"
bench: client 64: Connection reset by peer
exit 1
```

The server accepts at most 64 clients by default (`-m`, [Chapter 21](21-robustness.md)) and closes the 65th
connection at once (clients count from 0). Start it with `-m 200` to run 100:

```console
$ build/glibc-release/parleyd -p 6667 -T -f 0 -m 200 2>/dev/null & server=$!; sleep 0.2
$ build/glibc/bench 6667 100 2000; echo "exit $?"
bench: 100 clients x 2000 rounds: 20000000 lines delivered in 2519 ms (7939658 lines/s)
exit 0
$ kill $server; wait $server
```

</details>

Sources for this chapter: [REFERENCES.md](REFERENCES.md#chapter-22--shipping-and-choosing-a-c-library)
