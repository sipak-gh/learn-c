# Chapter 10 — Errors and Debugging

C has no exceptions: a failing function says so in its return value, often with `errno`, and unchecked, the program runs on with bad data. This chapter covers error conventions, `goto` cleanup and `assert`, then the silent bugs, **undefined behaviour**, found with sanitizers, gdb and strace. glibc and musl differ in error texts, assert messages and sanitizer support. The project gets `die()`, error paths, assertions and `make SANITIZE=1`. Previous: [Chapter 9](09-preprocessor-and-building.md). Next: [Chapter 11](11-testing.md).

## How C functions report errors

There are three common conventions; the man page says which one a function uses.

| Convention | Failure looks like | Examples |
|---|---|---|
| Return an `int`: 0 or more is success, -1 is failure, `errno` says why | `-1` | `open`, `read`, `write`, `close` (POSIX system calls) |
| Return a pointer: `NULL` is failure, `errno` usually says why | `NULL` | `malloc`, `fopen`, `strdup` |
| Return a special value plus a flag you must check | `EOF`, `0`, a clamped value | `fgets` (NULL, then `ferror`/`feof`), `fclose` (`EOF`), `strtol` (`LONG_MAX` + `ERANGE`) |

The project's functions return 0 or -1 (other effects are in the comment above them), or `NULL` for pointers. Don't invent a fourth style. Returning an error code (`return ENOMEM`), as POSIX threads do, also works, but pushes the real result into an output parameter; -1 plus `errno` leaves the return value free for a count or a descriptor.

## errno

`errno` (`<errno.h>`) is an `int` that failing library functions set to a positive constant such as `ENOENT` ("no such file") or `EINTR` ("interrupted"). Each thread has its own, so it is a macro, not a plain global.

The rules:

1. **Check the return value first.** `errno` means something only after a call has *reported* failure.
2. **Successful calls may leave `errno` alone, or change it.** Nothing resets it to 0.
3. **Save it at once** if you make other calls first. Even `printf` may change it.
4. For functions with no "failed" return value, like `strtol`, set `errno = 0` before the call and check it after.

`examples/ch10/errno-rules.c`
```c
/* errno-rules.c - errno is only meaningful right after a call FAILED. */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void)
{
    /* 1. A failing call sets errno. */
    FILE *f = fopen("no-such-file.txt", "r");
    if (f == NULL)
        printf("fopen failed: errno = %d (%s)\n", errno, strerror(errno));

    /* 2. A successful call may leave the old value in place. */
    char *p = malloc(16);
    printf("malloc %s, errno is still %d\n", p != NULL ? "worked" : "failed", errno);
    free(p);

    /* 3. So check the RETURN VALUE first, and look at errno only on failure.
     *    Functions that have no "failed" return value, like strtol, need
     *    errno = 0 before the call. */
    errno = 0;
    long n = strtol("99999999999999999999", NULL, 10);
    if (errno == ERANGE)
        printf("strtol: out of range, returned %ld\n", n);

    /* 4. Any library call may change errno, even printf. Save it first if
     *    you need it after other calls. */
    errno = 0;
    f = fopen("no-such-file.txt", "r");
    int saved = errno;
    printf("saved errno = %d\n", saved);
    return 0;
}
```

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o errno-rules errno-rules.c
$ ./errno-rules
fopen failed: errno = 2 (No such file or directory)
malloc worked, errno is still 2
strtol: out of range, returned 9223372036854775807
saved errno = 2
```

The second line is the trap: `if (errno != 0) perror("malloc")` would report a failure that never happened. ([Chapter 12](12-files-and-descriptors.md) covers `fopen`.)

## perror and strerror

- `perror("prefix")` prints `prefix: <text for errno>` and a newline to stderr.
- `strerror(e)` returns the text for `e`, for you to format.

`examples/ch10/errtext.c`
```c
/* errtext.c - perror() and strerror(): turn errno into a message. */
#include <errno.h>
#include <stdio.h>
#include <string.h>

#define SHOW(e) printf("%-10s %3d  %s\n", #e, e, strerror(e))

int main(void)
{
    FILE *f = fopen("/no/such/dir/file.txt", "r");
    if (f == NULL) {
        int err = errno;                   /* save it before any other call */
        perror("fopen");                   /* prints "fopen: <text>\n" to stderr */
        fprintf(stderr, "saved errno: %s\n", strerror(err));
        fprintf(stderr, "errno now:   %s\n", strerror(errno));
    }

    SHOW(ENOENT);
    SHOW(EACCES);
    SHOW(EINTR);
    SHOW(ENOMEM);
    SHOW(ERANGE);
    SHOW(EIO);
    SHOW(EADDRINUSE);
    SHOW(EMFILE);
    printf("%-10s %3d  %s\n", "(none)", 0, strerror(0));
    printf("%-10s %3d  %s\n", "(unknown)", 999, strerror(999));
    return 0;
}
```

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o errtext errtext.c
$ ./errtext
fopen: No such file or directory
saved errno: No such file or directory
errno now:   No such file or directory
ENOENT       2  No such file or directory
EACCES      13  Permission denied
EINTR        4  Interrupted system call
ENOMEM      12  Cannot allocate memory
ERANGE      34  Numerical result out of range
EIO          5  Input/output error
EADDRINUSE  98  Address already in use
EMFILE      24  Too many open files
(none)       0  Success
(unknown)  999  Unknown error 999
```

Now send stderr into a pipe and look at the third line:

```console
$ ./errtext 2>&1 | head -n 3
fopen: No such file or directory
saved errno: No such file or directory
errno now:   Invalid argument
```

`perror` worked, yet `errno` changed: with stderr a pipe or file, a helper step inside glibc's `perror` fails with `EINVAL` and is not undone. Rule 3: the saved copy is still right.

`perror` suits small tools. Bigger programs use `strerror` in their own logging function for one format, like the project's `log_msg(LOG_ERROR, "...: %s", strerror(errno))`.

### glibc vs musl: error messages

The numbers come from the kernel and match; the texts come from the libc:

```console
$ musl-gcc -std=c99 -Wall -Wextra -pedantic -static -o errtext-musl errtext.c
$ ./errtext-musl
fopen: No such file or directory
saved errno: No such file or directory
errno now:   No such file or directory
ENOENT       2  No such file or directory
EACCES      13  Permission denied
EINTR        4  Interrupted system call
ENOMEM      12  Out of memory
ERANGE      34  Result not representable
EIO          5  I/O error
EADDRINUSE  98  Address in use
EMFILE      24  No file descriptors available
(none)       0  No error information
(unknown)  999  No error information
```

| errno | glibc | musl |
|---|---|---|
| `ENOMEM` | Cannot allocate memory | Out of memory |
| `ERANGE` | Numerical result out of range | Result not representable |
| `EIO` | Input/output error | I/O error |
| `EADDRINUSE` | Address already in use | Address in use |
| `EMFILE` | Too many open files | No file descriptors available |
| 0 | Success | No error information |
| 999 (not an errno) | Unknown error 999 | No error information |

The `perror` format is the same, and musl's `perror` leaves `errno` alone even into a pipe.

Which is better? POSIX doesn't fix the wording. glibc's number for unknown errors helps debugging; musl's table is smaller and terser. **Never compare or parse error texts**: compare `errno` with the constants (`if (errno == ENOENT)`), and keep the `strerror` part out of tests that check exact messages, or they pass on only one libc.

## strerror_r: the thread-safe version

`strerror` may return a static buffer that the next call overwrites (glibc's does for unknown values), unsafe with threads. POSIX's `strerror_r` fills your buffer:

```c
int strerror_r(int errnum, char *buf, size_t buflen);   /* 0 on success, else an error number */
```

`examples/ch10/strerror-r.c`
```c
/* strerror-r.c - the thread-safe strerror_r(), POSIX (XSI) version. */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    char buf[64], tiny[8];

    /* Returns 0 and fills buf: no shared static buffer, so threads are safe. */
    int rc = strerror_r(EADDRINUSE, buf, sizeof buf);
    printf("rc=%d buf=\"%s\"\n", rc, buf);

    /* Too small: the text is cut and the result is ERANGE. */
    rc = strerror_r(EADDRINUSE, tiny, sizeof tiny);
    printf("tiny: rc=%d%s buf=\"%s\"\n", rc, rc == ERANGE ? " (ERANGE)" : "", tiny);

    /* Not an errno value: the libcs disagree. */
    rc = strerror_r(9999, buf, sizeof buf);
    printf("9999: rc=%d%s buf=\"%s\"\n", rc, rc == EINVAL ? " (EINVAL)" : "", buf);
    return 0;
}
```

It is POSIX, not C99, so the file defines `_POSIX_C_SOURCE` before any `#include` ([Chapter 9](09-preprocessor-and-building.md)).

### glibc vs musl: two strerror_r functions

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o strerror-r strerror-r.c
$ ./strerror-r
rc=0 buf="Address already in use"
tiny: rc=34 (ERANGE) buf="Address"
9999: rc=22 (EINVAL) buf="Unknown error 9999"
$ musl-gcc -std=c99 -Wall -Wextra -pedantic -static -o strerror-r-musl strerror-r.c
$ ./strerror-r-musl
rc=0 buf="Address in use"
tiny: rc=34 (ERANGE) buf="Address"
9999: rc=0 buf="No error information"
```

For an unknown number glibc returns `EINVAL` and musl returns success; both are allowed.

The bigger trap: with `_GNU_SOURCE` defined, glibc swaps in a GNU function of the same name that returns `char *` and may ignore your buffer:

```c
#define _GNU_SOURCE               /* asks glibc for the GNU strerror_r */
#include <errno.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    char buf[64];
    int rc = strerror_r(EADDRINUSE, buf, sizeof buf);
    printf("rc=%d buf=\"%s\"\n", rc, buf);
    return 0;
}
```

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o gnu gnu.c
gnu.c: In function ‘main’:
gnu.c:9:14: warning: initialization of ‘int’ from ‘char *’ makes integer from pointer without a cast [-Wint-conversion]
    9 |     int rc = strerror_r(EADDRINUSE, buf, sizeof buf);
      |              ^~~~~~~~~~
$ ./gnu
rc=916295412 buf="�"
$ musl-gcc -std=c99 -Wall -Wextra -pedantic -static -o gnu-musl gnu.c
$ ./gnu-musl
rc=0 buf="Address in use"
```

(The `rc` number and garbage vary.) glibc returned its own string and never touched `buf`. musl has only the POSIX version. **musl is simpler and better here**: one function, one meaning. On glibc, use `_POSIX_C_SOURCE`, never `_GNU_SOURCE`, in files that call `strerror_r`.

## die(): when there is nothing left to do

For errors that can't be handled, such as a bad command line or no memory at start-up, a `die()` function prints a message and exits with status 1.

`examples/ch10/die.c`
```c
/* die.c - a portable die(): print a message and exit. */
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *progname = "die";

/* Print "prog: message" to stderr and exit(1). A message ending in ':'
 * gets the errno text appended, like perror(). */
static void die(const char *fmt, ...)
{
    int err = errno;                      /* before anything can change it */
    va_list ap;

    fprintf(stderr, "%s: ", progname);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    if (fmt[0] != '\0' && fmt[strlen(fmt) - 1] == ':')
        fprintf(stderr, " %s", strerror(err));
    fputc('\n', stderr);
    exit(1);
}

int main(int argc, char **argv)
{
    if (argc != 2)
        die("usage: %s FILE", argv[0]);

    FILE *f = fopen(argv[1], "r");
    if (f == NULL)
        die("cannot open %s:", argv[1]);
    printf("opened %s\n", argv[1]);
    fclose(f);
    return 0;
}
```

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o die die.c
$ ./die
die: usage: ./die FILE
$ ./die nope.txt; echo "exit $?"
die: cannot open nope.txt: No such file or directory
exit 1
$ ./die die.c
opened die.c
```

The trailing colon that adds the errno text is a suckless-tools trick for short call sites. `exit(1)`, unlike `abort()`, runs the normal shutdown: it flushes stdio buffers and calls `atexit` handlers.

### glibc vs musl: error(), err() and backtrace()

glibc has `error()` (`<error.h>`) and `backtrace()` for call stacks (`<execinfo.h>`); musl has neither header. `errh.c` and `bt.c` just include them:

```console
$ musl-gcc -std=c99 -static -o e errh.c
errh.c:1:10: fatal error: error.h: No such file or directory
    1 | #include <error.h>
      |          ^~~~~~~~~
compilation terminated.
$ musl-gcc -c bt.c
bt.c:1:10: fatal error: execinfo.h: No such file or directory
    1 | #include <execinfo.h>
      |          ^~~~~~~~~~~~
compilation terminated.
```

Both have the BSD `err()`, `errx()`, `warn()` and `warnx()` (`<err.h>`), which aren't POSIX. The portable twenty-line `die()` is simpler, and you own the format. For call stacks, use gdb.

## goto cleanup: one exit path

A function holding several resources must release them all on every way out; early `return`s repeat the cleanup until one is missed. The C idiom is one cleanup label at the end, reached with `goto`. The user's "phase 1 gate" task: read a whole file into a heap buffer, handling every error.

`examples/ch10/slurp.c`
```c
/* slurp.c - read a whole file into a heap buffer; one cleanup path. */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Read all of path into a new NUL-terminated buffer. On success return 0
 * and set *out (the caller frees it) and *len. On failure return -1 with
 * errno set. */
static int slurp(const char *path, char **out, size_t *len)
{
    int result = -1;
    char *buf = NULL;
    size_t used = 0, cap = 0;

    FILE *f = fopen(path, "rb");
    if (f == NULL)
        return -1;                        /* nothing to clean up yet */

    for (;;) {
        if (cap - used < 4096) {
            size_t ncap = cap == 0 ? 4096 : cap * 2;
            char *nbuf = realloc(buf, ncap + 1);    /* +1 for the '\0' */
            if (nbuf == NULL)
                goto cleanup;             /* buf is still valid: freed below */
            buf = nbuf;
            cap = ncap;
        }
        size_t n = fread(buf + used, 1, cap - used, f);
        used += n;
        if (n == 0)
            break;
    }
    if (ferror(f))
        goto cleanup;                     /* fread set errno */
    buf[used] = '\0';
    *out = buf;
    *len = used;
    buf = NULL;                           /* now owned by the caller */
    result = 0;

cleanup:
    {
        int saved = errno;                /* fclose/free must not hide it */
        free(buf);
        fclose(f);
        errno = saved;
    }
    return result;
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: %s FILE\n", argv[0]);
        return 2;
    }
    char *text;
    size_t len;
    if (slurp(argv[1], &text, &len) < 0) {
        fprintf(stderr, "slurp: %s: %s\n", argv[1], strerror(errno));
        return 1;
    }
    size_t lines = 0;
    for (size_t i = 0; i < len; i++)
        if (text[i] == '\n')
            lines++;
    printf("%s: %zu bytes, %zu lines\n", argv[1], len, lines);
    free(text);
    return 0;
}
```

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o slurp slurp.c
$ ./slurp slurp.c
slurp.c: 1954 bytes, 71 lines
$ ./slurp nope
slurp: nope: No such file or directory
$ ./slurp /
slurp: /: Is a directory
```

The pattern:

- `result` starts as failure; only the success path sets it to 0, just before the label.
- Resources start as "nothing to free" (`buf = NULL`); `free(NULL)` does nothing, so cleanup needs no `if`.
- On success `buf` goes to the caller, and `buf = NULL` stops cleanup freeing it.
- `goto` jumps only **forward** to the one label, the use C programmers agree on. Never backwards or into a block.
- Cleanup keeps `errno` from the failing call, not from `fclose`.
- `buf = realloc(buf, ...)` would lose the block on failure, hence `nbuf` ([Chapter 8](08-memory.md)).

Opening `/` works on Linux; reading it fails with `EISDIR`, passed on by `fread`.

## assert: checking your own logic

`assert(expr)` (`<assert.h>`) checks something true if *your code* is correct. If false, it prints the expression, file, line and function, and calls `abort()`.

`examples/ch10/assert.c`
```c
/* assert.c - assert() checks something that must be true if the code is right. */
#include <assert.h>
#include <stdio.h>
#include <string.h>

/* Copy src into dst of size n. Calling it with n == 0 is a bug in the
 * caller, not a run-time error, so assert() is the right check. */
static void copy_name(char *dst, size_t n, const char *src)
{
    assert(dst != NULL && src != NULL);
    assert(n > 0);
    strncpy(dst, src, n - 1);
    dst[n - 1] = '\0';
}

int main(int argc, char **argv)
{
    char name[10];

    (void)argv;
    copy_name(name, sizeof name, "alice");
    printf("name = %s\n", name);
    copy_name(name, (size_t)(argc - 1), "bob");    /* n == 0 with no arguments */
    printf("name = %s\n", name);
    return 0;
}
```

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o assert assert.c
$ ./assert; echo "exit $?"
name = alice
assert: assert.c:11: copy_name: Assertion `n > 0' failed.
Aborted (core dumped)
exit 134
```

Status 134 is 128 + 6: killed by signal 6, `SIGABRT` ([Chapter 13](13-processes-and-signals.md)). "core dumped" is explained under Core files below.

**`NDEBUG` turns every `assert` into nothing:**

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -DNDEBUG -o assert-nd assert.c
$ ./assert-nd; echo "exit $?"
name = alice
Segmentation fault (core dumped)
exit 139
```

`n - 1` wraps to `SIZE_MAX`, `strncpy` writes far past `name`, and the crash comes later and further from the cause (139 = 128 + 11, `SIGSEGV`).

Rules for `assert`:

- Use it for **programmer errors**: broken preconditions, impossible states.
- **Never** for run-time failures (missing file, bad input, full disk, no memory); return errors.
- **Never** with side effects: under `NDEBUG`, `assert(fclose(f) == 0)` closes nothing.
- This course keeps asserts in release builds: cheap, and a clear crash beats silent corruption.

### glibc vs musl: assert messages

```console
$ musl-gcc -std=c99 -Wall -Wextra -pedantic -static -o assert-musl assert.c
$ ./assert-musl; echo "exit $?"
name = alice
Assertion failed: n > 0 (assert.c: copy_name: 11)
Aborted (core dumped)
exit 134
```

| | Message |
|---|---|
| glibc | ``assert: assert.c:11: copy_name: Assertion `n > 0' failed.`` |
| musl | `Assertion failed: n > 0 (assert.c: copy_name: 11)` |

C requires expression, file, line and (since C99) function, in any format. glibc adds the program name and puts `file:line` first, which editors and terminals can link, so **glibc's is slightly more useful**; musl's is shorter. Both exit with 134, which is what scripts should check.

## Undefined behaviour

**Undefined behaviour** (UB) is "behavior … for which this International Standard imposes no requirements". The program may crash, print garbage or seem to work: at `-O0` today, not at `-O2` or with another compiler tomorrow. Nobody has to check, so there's no message.

Checks on every index and addition would slow everything, so C leaves them to you, and the compiler may **assume UB never happens** and optimise on that. That second part surprises people.

The common ones:

| UB | Example |
|---|---|
| Signed integer overflow | `INT_MAX + 1` |
| Array access out of bounds | `int a[4]; a[4] = 0;` |
| Reading an uninitialised local | `int x; printf("%d", x);` |
| Dereferencing `NULL` or a dangling pointer | `free(p); *p = 0;` (use after free) |
| Freeing twice, or freeing what `malloc` didn't return | `free(p); free(p);` |
| Shifting by the type's width or more, or a negative value | `1 << 32`, `1 << 31` for a 32-bit `int` |
| Modifying a string literal | `char *s = "hi"; s[0] = 'H';` |
| Accessing an object through the wrong pointer type (strict aliasing) | `float f; *(int *)&f = 0;` |
| Changing a variable twice without a sequence point | `i = i++ + 1;` |
| A non-`void` function that falls off the end, when the caller uses the result | `int f(void) { }` |

Unsigned overflow is **not** UB: unsigned arithmetic wraps modulo 2ⁿ.

The optimiser using signed overflow against you:

`examples/ch10/overflow.c`
```c
/* overflow.c - signed overflow is undefined, so the optimiser may assume
 * it never happens. */
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

/* "Will x + 1 overflow?" asked the wrong way. */
static int bigger(int x)
{
    int y = x + 1;
    return y > x;
}

/* The right way: compare before doing the arithmetic. */
static int bigger_ok(int x)
{
    return x < INT_MAX;
}

int main(int argc, char **argv)
{
    int x = argc > 1 ? atoi(argv[1]) : INT_MAX;

    printf("x = %d\n", x);
    printf("bigger(x)    = %d\n", bigger(x));
    printf("bigger_ok(x) = %d\n", bigger_ok(x));
    return 0;
}
```

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -O0 -o overflow overflow.c && ./overflow
x = 2147483647
bigger(x)    = 0
bigger_ok(x) = 0
$ gcc -std=c99 -Wall -Wextra -pedantic -O2 -o overflow overflow.c && ./overflow
x = 2147483647
bigger(x)    = 1
bigger_ok(x) = 0
```

Same source and input, different answer. musl agrees, because the compiler does this, not the library.

`-S` shows the assembly (in `overflow.s`); `-fno-inline` keeps `bigger` separate and `-fno-asynchronous-unwind-tables` drops noise:

```console
$ gcc -std=c99 -O0 -fno-asynchronous-unwind-tables -S overflow.c
$ sed -n '/^bigger:/,/ret/p' overflow.s
bigger:
	endbr64
	pushq	%rbp
	movq	%rsp, %rbp
	movl	%edi, -20(%rbp)
	movl	-20(%rbp), %eax
	addl	$1, %eax
	movl	%eax, -4(%rbp)
	movl	-4(%rbp), %eax
	cmpl	-20(%rbp), %eax
	setg	%al
	movzbl	%al, %eax
	popq	%rbp
	ret
$ gcc -std=c99 -O2 -fno-inline -fno-asynchronous-unwind-tables -S overflow.c
$ sed -n '/^bigger/,/ret/p' overflow.s
bigger_ok:
	xorl	%eax, %eax
	cmpl	$2147483647, %edi
	setne	%al
	ret
bigger.constprop.0:
	movl	$1, %eax
	ret
```

At `-O0` the CPU adds 1 (`addl`), wraps to a negative number and compares (`cmpl`): 0. At `-O2` gcc reasons "`x + 1 > x` wherever it is defined, and the rest can't happen" and makes `bigger` `return 1` (`movl $1, %eax`). The compiler followed the rules; the program broke them.

**"It works" proves nothing with UB.** Check before the operation, as `bigger_ok` does, and let tools find what you missed.

## Sanitizers

A **sanitizer** is a compiler option that adds run-time checks:

| Option | Name | Finds | Cost |
|---|---|---|---|
| `-fsanitize=address` | AddressSanitizer (ASan) | out-of-bounds access (heap, stack, globals), use after free, double free, leaks | ~2× slower, 2–3× more memory |
| `-fsanitize=undefined` | UndefinedBehaviorSanitizer (UBSan) | signed overflow, bad shifts, misaligned or NULL pointer use, out-of-bounds array index (when the size is known) | small |

Combine them with `-fsanitize=address,undefined`, plus `-g` for line numbers and `-fno-omit-frame-pointer` for better stacks. Use them on every test run; ship without them.

### Four heap bugs

`examples/ch10/heapbug.c`
```c
/* heapbug.c - four classic heap bugs. Run: ./heapbug overflow|uaf|double|leak */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *make_name(const char *s)
{
    char *p = malloc(strlen(s));          /* BUG: no room for the '\0' */
    if (p != NULL)
        strcpy(p, s);
    return p;
}

int main(int argc, char **argv)
{
    const char *what = argc > 1 ? argv[1] : "";

    if (strcmp(what, "overflow") == 0) {
        char *name = make_name("parley");
        printf("name = %s\n", name);
        free(name);
    } else if (strcmp(what, "uaf") == 0) {
        char *name = malloc(16);
        strcpy(name, "alice");
        free(name);
        printf("after free: %s\n", name);       /* BUG: use after free */
    } else if (strcmp(what, "double") == 0) {
        char *name = malloc(16);
        free(name);
        free(name);                             /* BUG: double free */
        printf("freed twice\n");
    } else if (strcmp(what, "leak") == 0) {
        char *name = malloc(100);
        name = malloc(200);                     /* BUG: the first block is lost */
        free(name);
        printf("leaked 100 bytes\n");
    } else {
        fprintf(stderr, "usage: heapbug overflow|uaf|double|leak\n");
        return 2;
    }
    return 0;
}
```

Without sanitizers most of them hide:

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -g -o heapbug heapbug.c
$ ./heapbug overflow
name = parley
$ ./heapbug uaf
after free: |8�
$ ./heapbug double
free(): double free detected in tcache 2
Aborted (core dumped)
$ ./heapbug leak
leaked 100 bytes
```

(The garbage after `after free:` varies.) The one-byte overflow went unnoticed. With ASan and UBSan:

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -g -fsanitize=address,undefined -fno-omit-frame-pointer -o heapbug-san heapbug.c
$ ./heapbug-san overflow
=================================================================
==204144==ERROR: AddressSanitizer: heap-buffer-overflow on address 0x502000000016 at pc 0x71d9be2544bf bp 0x7ffe55152ab0 sp 0x7ffe55152258
WRITE of size 7 at 0x502000000016 thread T0
    #0 0x71d9be2544be in __interceptor_strcpy ../../../../src/libsanitizer/asan/asan_interceptors.cpp:440
    #1 0x64e73144b3cc in make_name .../heapbug.c:10
    #2 0x64e73144b4b1 in main .../heapbug.c:19
    #3 0x71d9bd629d8f in __libc_start_call_main ../sysdeps/nptl/libc_start_call_main.h:58
    #4 0x71d9bd629e3f in __libc_start_main_impl ../csu/libc-start.c:392
    #5 0x64e73144b284 in _start (.../heapbug-san+0x2284)

0x502000000016 is located 0 bytes to the right of 6-byte region [0x502000000010,0x502000000016)
allocated by thread T0 here:
    #0 0x71d9be2b4887 in __interceptor_malloc ../../../../src/libsanitizer/asan/asan_malloc_linux.cpp:145
    #1 0x64e73144b382 in make_name .../heapbug.c:8
    #2 0x64e73144b4b1 in main .../heapbug.c:19
    #3 0x71d9bd629d8f in __libc_start_call_main ../sysdeps/nptl/libc_start_call_main.h:58

SUMMARY: AddressSanitizer: heap-buffer-overflow ../../../../src/libsanitizer/asan/asan_interceptors.cpp:440 in __interceptor_strcpy
Shadow bytes around the buggy address:
  ...
=>0x0a047fff8000: fa fa[06]fa fa fa fa fa fa fa fa fa fa fa fa fa
  ...
==204144==ABORTING
```

(PIDs and addresses vary; long paths and the shadow-byte legend are cut.) Reading the report:

1. **The first line** names the bug: `heap-buffer-overflow`.
2. **The first stack** is the bad access. Skip `__interceptor_` (ASan's `strcpy`) and `__libc_start` frames: yours is `make_name heapbug.c:10`.
3. **"located 0 bytes to the right of 6-byte region"**: just past a 6-byte block. Off by one.
4. **The second stack** is the allocation, `heapbug.c:8`'s `malloc(strlen(s))`: the line to fix.
5. Shadow bytes map memory: `06` is "6 usable bytes", `fa` "heap red zone"; the write hit the `fa` after `[06]`.

The other bugs, cut to the key lines:

```console
$ ./heapbug-san uaf
==204145==ERROR: AddressSanitizer: heap-use-after-free on address 0x502000000010 at pc 0x7843e665df89 bp 0x7ffe3e8ae800 sp 0x7ffe3e8adf78
READ of size 2 at 0x502000000010 thread T0
    ...
    #3 0x5db668d42577 in main .../heapbug.c:26
    ...
0x502000000010 is located 0 bytes inside of 16-byte region [0x502000000010,0x502000000020)
freed by thread T0 here:
    #0 0x7843e66b4537 in __interceptor_free ../../../../src/libsanitizer/asan/asan_malloc_linux.cpp:127
    #1 0x5db668d4255c in main .../heapbug.c:25
    ...
previously allocated by thread T0 here:
    #0 0x7843e66b4887 in __interceptor_malloc ../../../../src/libsanitizer/asan/asan_malloc_linux.cpp:145
    #1 0x5db668d4251b in main .../heapbug.c:23
    ...
$ ./heapbug-san double
==204146==ERROR: AddressSanitizer: attempting double-free on 0x502000000010 in thread T0:
    #0 0x7d076e2b4537 in __interceptor_free ../../../../src/libsanitizer/asan/asan_malloc_linux.cpp:127
    #1 0x57246adae5d2 in main .../heapbug.c:30
    ...
$ ./heapbug-san leak
leaked 100 bytes

=================================================================
==229888==ERROR: LeakSanitizer: detected memory leaks

Direct leak of 100 byte(s) in 1 object(s) allocated from:
    #0 0x7360964b4887 in __interceptor_malloc ../../../../src/libsanitizer/asan/asan_malloc_linux.cpp:145
    #1 0x62b694e21620 in main .../heapbug.c:33
    #2 0x736095829d8f in __libc_start_call_main ../sysdeps/nptl/libc_start_call_main.h:58

SUMMARY: AddressSanitizer: 100 byte(s) leaked in 1 allocation(s).
```

Use after free shows three stacks: access, `free`, allocation. Leaks are reported at exit. Every report exits with status 1, so tests under ASan fail loudly.

UBSan catches `overflow.c`'s overflow and lets the program continue:

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -g -fsanitize=undefined -o overflow-ub overflow.c
$ ./overflow-ub
x = 2147483647
overflow.c:10:9: runtime error: signed integer overflow: 2147483647 + 1 cannot be represented in type 'int'
bigger(x)    = 0
bigger_ok(x) = 0
```

### glibc vs musl: sanitizers

Without sanitizers the heap bugs differ, because each libc has its own `malloc`:

```console
$ musl-gcc -std=c99 -Wall -Wextra -pedantic -static -g -o heapbug-musl heapbug.c
$ ./heapbug-musl overflow
name = parley
$ ./heapbug-musl uaf
Segmentation fault (core dumped)
$ ./heapbug-musl double
Segmentation fault (core dumped)
$ ./heapbug-musl leak
leaked 100 bytes
```

glibc names the double free ("double free detected in tcache 2"); musl's allocator crashes silently. Neither catches the one-byte overflow or the leak.

The sanitizers differ more. Their run-time libraries (`libasan`, `libubsan`) ship with gcc, built against glibc:

```console
$ musl-gcc -std=c99 -static -g -fsanitize=address -o x heapbug.c
gcc: error: cannot specify -static with -fsanitize=address
$ musl-gcc -std=c99 -static -g -fsanitize=undefined -o x overflow.c
/usr/bin/ld: /usr/lib/gcc/x86_64-linux-gnu/11/./libubsan.a(cp-demangle.o): in function `d_append_num':
(.text+0x888): undefined reference to `__sprintf_chk'
...
```

So no ASan on musl. UBSan works in **trap mode**: `-fsanitize-undefined-trap-on-error` makes each check execute an illegal instruction instead of calling the run-time library, so nothing needs linking:

```console
$ musl-gcc -std=c99 -Wall -Wextra -pedantic -static -g -fsanitize=undefined -fsanitize-undefined-trap-on-error -o overflow-trap overflow.c
$ ./overflow-trap; echo "exit $?"
x = 2147483647
Illegal instruction (core dumped)
exit 132
```

No message (132 = 128 + 4, `SIGILL`); gdb shows where:

```console
$ gdb -q -batch -ex run -ex bt ./overflow-trap
x = 2147483647

Program received signal SIGILL, Illegal instruction.
0x000000000040116c in bigger (x=2147483647) at overflow.c:10
10	    int y = x + 1;
#0  0x000000000040116c in bigger (x=2147483647) at overflow.c:10
#1  0x0000000000401210 in main (argc=1, argv=0x7fffffffe628) at overflow.c:25
```

(Newer gcc spells the option `-fsanitize-trap=undefined`; gcc 11 doesn't know it.)

**glibc is the clear winner for debugging.** Test under ASan and UBSan with glibc, and use musl's trap mode as a cheaper second check, as the project's `make SANITIZE=1` does below.

## gdb: the debugger

gdb lets you stop a program at any line, inspect variables, step, and see the call stack at a crash. Compile with `-g` (line numbers, variable names, types) and `-O0` (each line gets its own instructions; every variable lives in memory).

The practice program crashes:

`examples/ch10/nicks.c`
```c
/* nicks.c - a small program with a crash, for gdb practice. */
#include <stdio.h>
#include <string.h>

/* Return the parameter of a "COMMAND param" line. */
static const char *param(const char *line)
{
    const char *sp = strchr(line, ' ');
    return sp + 1;                    /* BUG: sp is NULL when there is no space */
}

static int total_len(const char *lines[], int n)
{
    int total = 0;

    for (int i = 0; i < n; i++) {
        const char *p = param(lines[i]);
        total += (int)strlen(p);
    }
    return total;
}

int main(void)
{
    const char *lines[] = { "NICK alice", "JOIN #c", "QUIT", "NICK bob" };

    printf("total = %d\n", total_len(lines, 4));
    return 0;
}
```

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -g -O0 -o nicks nicks.c
$ ./nicks
Segmentation fault (core dumped)
```

### Where did it crash? run, bt, frame, print

```console
$ gdb -q ./nicks
Reading symbols from ./nicks...
(gdb) run
Starting program: .../nicks
[Thread debugging using libthread_db enabled]
Using host libthread_db library "/lib/x86_64-linux-gnu/libthread_db.so.1".

Program received signal SIGSEGV, Segmentation fault.
__strlen_avx2 () at ../sysdeps/x86_64/multiarch/strlen-avx2.S:74
74	../sysdeps/x86_64/multiarch/strlen-avx2.S: No such file or directory.
(gdb) bt
#0  __strlen_avx2 () at ../sysdeps/x86_64/multiarch/strlen-avx2.S:74
#1  0x000055555555522a in total_len (lines=0x7fffffffe4f0, n=4) at nicks.c:18
#2  0x0000555555555296 in main () at nicks.c:27
(gdb) frame 1
#1  0x000055555555522a in total_len (lines=0x7fffffffe4f0, n=4) at nicks.c:18
18	        total += (int)strlen(p);
(gdb) info locals
p = 0x1 <error: Cannot access memory at address 0x1>
i = 2
total = 7
(gdb) print lines[i]
$1 = 0x555555556017 "QUIT"
(gdb) quit
A debugging session is active.

	Inferior 1 [process 270390] will be killed.

Quit anyway? (y or n) y
```

(Addresses and PIDs vary; the path after `Starting program:` is cut to `...`.)

- The crash is in glibc's `strlen` (frame #0); "No such file or directory" means gdb lacks glibc's source.
- `bt` (backtrace) shows the callers; frame #1, line 18, is yours.
- `frame 1` goes there; `info locals` shows `p` is `0x1`, not a string.
- `print` takes any C expression: `lines[i]` is `"QUIT"`, with no space, so `strchr` gave `NULL` and `param` `NULL + 1`.

### Stepping: break, next, step, finish, x

```console
$ gdb -q ./nicks
Reading symbols from ./nicks...
(gdb) break total_len
Breakpoint 1 at 0x11eb: file nicks.c, line 14.
(gdb) run
Starting program: .../nicks
[Thread debugging using libthread_db enabled]
Using host libthread_db library "/lib/x86_64-linux-gnu/libthread_db.so.1".

Breakpoint 1, total_len (lines=0x7fffffffe4f0, n=4) at nicks.c:14
14	    int total = 0;
(gdb) next
16	    for (int i = 0; i < n; i++) {
(gdb) next
17	        const char *p = param(lines[i]);
(gdb) step
param (line=0x555555556004 "NICK alice") at nicks.c:8
8	    const char *sp = strchr(line, ' ');
(gdb) next
9	    return sp + 1;                    /* BUG: sp is NULL when there is no space */
(gdb) print sp
$1 = 0x555555556008 " alice"
(gdb) print *sp
$2 = 32 ' '
(gdb) x/s line
0x555555556004:	"NICK alice"
(gdb) x/12xb line
0x555555556004:	0x4e	0x49	0x43	0x4b	0x20	0x61	0x6c	0x69
0x55555555600c:	0x63	0x65	0x00	0x4a
(gdb) finish
Run till exit from #0  param (line=0x555555556004 "NICK alice") at nicks.c:9
0x000055555555521a in total_len (lines=0x7fffffffe4f0, n=4) at nicks.c:17
17	        const char *p = param(lines[i]);
Value returned is $3 = 0x555555556009 "alice"
(gdb) next
18	        total += (int)strlen(p);
(gdb) info locals
p = 0x555555556009 "alice"
i = 0
total = 0
```

- `next` runs one line, stepping *over* calls; `step` goes *into* them.
- `finish` runs to the end of the current function and shows the return value.
- `x/FMT ADDRESS` examines memory: `x/s` as a string, `x/12xb` as 12 hex bytes, showing `0x20` (the space) and the final `0x00`.

### Conditional breakpoints

The crash comes on the third line. Stop exactly there:

```console
(gdb) break 17 if i == 2
Breakpoint 1 at 0x11fb: file nicks.c, line 17.
(gdb) run
...
Breakpoint 1, total_len (lines=0x7fffffffe4f0, n=4) at nicks.c:17
17	        const char *p = param(lines[i]);
(gdb) print lines[i]
$1 = 0x555555556017 "QUIT"
(gdb) step
param (line=0x555555556017 "QUIT") at nicks.c:8
8	    const char *sp = strchr(line, ' ');
(gdb) next
9	    return sp + 1;                    /* BUG: sp is NULL when there is no space */
(gdb) print sp
$2 = 0x0
(gdb) print sp + 1
$3 = 0x1 <error: Cannot access memory at address 0x1>
```

That's the bug; the fix is in the exercises.

### Watchpoints and display

A **watchpoint** stops whenever a variable changes, whichever line does it:

```console
(gdb) break total_len
Breakpoint 1 at 0x11eb: file nicks.c, line 14.
(gdb) run
...
Breakpoint 1, total_len (lines=0x7fffffffe4f0, n=4) at nicks.c:14
14	    int total = 0;
(gdb) watch total
Hardware watchpoint 2: total
(gdb) continue
Continuing.

Hardware watchpoint 2: total

Old value = 1776
New value = 0
total_len (lines=0x7fffffffe4f0, n=4) at nicks.c:16
16	    for (int i = 0; i < n; i++) {
(gdb) continue
Continuing.

Hardware watchpoint 2: total

Old value = 0
New value = 5
total_len (lines=0x7fffffffe4f0, n=4) at nicks.c:16
16	    for (int i = 0; i < n; i++) {
(gdb) display i
1: i = 0
(gdb) continue
Continuing.

Hardware watchpoint 2: total

Old value = 5
New value = 7
total_len (lines=0x7fffffffe4f0, n=4) at nicks.c:16
16	    for (int i = 0; i < n; i++) {
1: i = 1
(gdb) info breakpoints
Num     Type           Disp Enb Address            What
1       breakpoint     keep y   0x00005555555551eb in total_len at nicks.c:14
	breakpoint already hit 1 time
2       hw watchpoint  keep y                      total
	breakpoint already hit 3 times
(gdb) delete
Delete all breakpoints? (y or n) y
(gdb) continue
Continuing.

Program received signal SIGSEGV, Segmentation fault.
__strlen_avx2 () at ../sysdeps/x86_64/multiarch/strlen-avx2.S:74
```

The first change, `Old value = 1776`, is whatever was left on the stack before line 14 ran: that is "uninitialised", and why reading it is UB. `display i` prints `i` at every stop.

### Stopping a program that hangs

`examples/ch10/spin.c`
```c
/* spin.c - a loop that never ends, for Ctrl-C in gdb. */
#include <stdio.h>

static size_t count_spaces(const char *s)
{
    size_t n = 0;

    while (*s != '\0') {
        if (*s == ' ') {
            n++;
            s++;                      /* BUG: s only moves on at a space */
        }
    }
    return n;
}

int main(void)
{
    printf("%zu\n", count_spaces(" JOIN #c"));
    printf("%zu\n", count_spaces("PRIVMSG #c :hi there"));
    return 0;
}
```

Run it in gdb and press Ctrl-C when it hangs; gdb stops it where it is:

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -g -o spin spin.c
$ gdb -q ./spin
Reading symbols from ./spin...
(gdb) run
Starting program: .../spin
[Thread debugging using libthread_db enabled]
Using host libthread_db library "/lib/x86_64-linux-gnu/libthread_db.so.1".
^C
Program received signal SIGINT, Interrupt.
0x0000555555555178 in count_spaces (s=0x555555556005 "JOIN #c") at spin.c:8
8	    while (*s != '\0') {
(gdb) info locals
n = 1
(gdb) print *s
$1 = 74 'J'
```

`s` points at `J`, not a space, so it never moves again.

### Optimised code in gdb

An `-O2` build looks odd:

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -g -O2 -o nicks-o2 nicks.c
$ gdb -q ./nicks-o2
(gdb) run
...
(gdb) bt
#0  __strlen_avx2 () at ../sysdeps/x86_64/multiarch/strlen-avx2.S:74
#1  0x000055555555513a in total_len (n=4, n=4, lines=0x7fffffffe4e0)
    at nicks.c:18
#2  main () at nicks.c:27
(gdb) frame 1
#1  0x000055555555513a in total_len (n=4, n=4, lines=0x7fffffffe4e0)
    at nicks.c:18
18	        total += (int)strlen(p);
(gdb) info locals
p = <optimized out>
i = 2
total = 7
total = <optimized out>
i = <optimized out>
p = <optimized out>
```

`total_len` was inlined into `main`, so frames #1 and #2 share code (#2 has no address), and variables appear once per inlined scope (also `n=4, n=4`). `p` lived in a reused register: `<optimized out>`. Debug at `-O0`; a bug only at `-O2` suggests UB, so use the sanitizers.

### Core files

A **core file** is a snapshot of a crashed process's memory. Loaded into gdb later, it shows the crash as if it had just happened: useful when it happened on another machine, or once in a thousand runs.

Where it goes depends on `/proc/sys/kernel/core_pattern` (`cat` it to see yours):

| `core_pattern` | Where the core goes |
|---|---|
| `core` (or a path pattern such as `core.%p`) | a file in the crashing process's current directory |
| `\|/usr/lib/systemd/systemd-coredump ...` | systemd's store under `/var/lib/systemd/coredump/`; `coredumpctl list`, then `coredumpctl gdb` |
| `\|/usr/share/apport/apport ...` | Ubuntu's crash reporter; for programs that aren't from a package it keeps a core only if `ulimit -c` allows |

A file pattern obeys your shell's core size limit, often 0, which means no file:

```console
$ ulimit -c
0
$ ulimit -c unlimited
```

With a `|` pattern the kernel ignores that limit and pipes the core to the program, which decides what to keep (systemd-coredump and apport both check your limit). So the shell may say "(core dumped)" with no file to be found. gdb can also write a core itself, with no system settings:

```console
$ gdb -q -batch -ex run -ex 'generate-core-file nicks.core' ./nicks
...
Saved corefile nicks.core
$ gdb -q ./nicks nicks.core
Reading symbols from ./nicks...
...
Program terminated with signal SIGSEGV, Segmentation fault.
#0  __strlen_avx2 () at ../sysdeps/x86_64/multiarch/strlen-avx2.S:74
74	../sysdeps/x86_64/multiarch/strlen-avx2.S: No such file or directory.
(gdb) bt
#0  __strlen_avx2 () at ../sysdeps/x86_64/multiarch/strlen-avx2.S:74
#1  0x000055555555522a in total_len (lines=0x7fffffffe4f0, n=4) at nicks.c:18
#2  0x0000555555555296 in main () at nicks.c:27
(gdb) frame 1
#1  0x000055555555522a in total_len (lines=0x7fffffffe4f0, n=4) at nicks.c:18
18	        total += (int)strlen(p);
(gdb) print lines[i]
$1 = 0x555555556017 "QUIT"
```

You can't `step` or `continue` in a core file; the process is dead and you see only its last moment.

### gdb cheat sheet

| Command | Short | Does |
|---|---|---|
| `run [args]` | `r` | start the program (with arguments; `< file` works too) |
| `break FUNC` / `break FILE:LINE` | `b` | stop there |
| `break LINE if COND` | | stop only when `COND` is true |
| `tbreak LOCATION` | | breakpoint that deletes itself after one hit |
| `continue` | `c` | run to the next stop |
| `next` / `step` | `n` / `s` | one line, over / into calls |
| `finish` | | run until this function returns; show the value |
| `bt` | | call stack |
| `frame N` / `up` / `down` | `f` | select a stack frame |
| `print EXPR` | `p` | print any C expression; `p/x` in hex |
| `display EXPR` | | print it at every stop |
| `info locals` / `info args` | | all locals / arguments of the frame |
| `x/Ns ADDR`, `x/Nxb ADDR` | | examine memory as strings / hex bytes |
| `watch VAR` | | stop when `VAR` changes |
| `info breakpoints` / `delete [N]` | `i b` / `d` | list / remove breakpoints |
| `layout src` | | split screen with the source (Ctrl-X A leaves) |
| `quit` | `q` | leave |

Enter on an empty line repeats the last command, handy with `next`.

### glibc vs musl: debugging a static musl binary

gdb works the same; the libc frames differ:

```console
$ musl-gcc -std=c99 -Wall -Wextra -pedantic -static -g -o nicks-musl nicks.c
$ gdb -q ./nicks-musl
(gdb) run
...
Program received signal SIGSEGV, Segmentation fault.
0x0000000000403e3f in strlen ()
(gdb) bt
#0  0x0000000000403e3f in strlen ()
#1  0x00000000004011da in total_len (lines=0x7fffffffe5b0, n=4) at nicks.c:18
#2  0x0000000000401246 in main () at nicks.c:27
```

musl has one plain `strlen` without debug info; glibc picks a CPU-specific one (`__strlen_avx2`) at load time, with line information here. Static musl has fixed addresses like `0x401246`; position-independent glibc loads at `0x5555...` under gdb. Your own frames look the same, and they matter.

## strace: watching system calls

A program touches the outside world (files, terminal, network, kernel memory) through **system calls**. `strace` prints each with its arguments and result; no debug info or source needed.

```console
$ echo hello > hi.txt
$ strace -e trace=openat,read,write,close ./slurp hi.txt
openat(AT_FDCWD, "/etc/ld.so.cache", O_RDONLY|O_CLOEXEC) = 3
close(3)                                = 0
openat(AT_FDCWD, "/lib/x86_64-linux-gnu/libc.so.6", O_RDONLY|O_CLOEXEC) = 3
read(3, "\177ELF\2\1\1\3\0\0\0\0\0\0\0\0\3\0>\0\1\0\0\0P\237\2\0\0\0\0\0"..., 832) = 832
close(3)                                = 0
openat(AT_FDCWD, "hi.txt", O_RDONLY)    = 3
read(3, "hello\n", 4096)                = 6
read(3, "", 4096)                       = 0
read(3, "", 4096)                       = 0
close(3)                                = 0
hi.txt: 6 bytes, 1 lines
write(1, "hi.txt: 6 bytes, 1 lines\n", 25) = 25
+++ exited with 0 +++
```

- The first five lines are the dynamic loader loading glibc.
- `fopen` became `openat` (file descriptor 3), `fread` 4096-byte `read`s (0 is end of file), and `printf` one `write` to fd 1 at exit.
- A failed call shows `= -1` and the `errno` name: `openat(AT_FDCWD, "nope", O_RDONLY) = -1 ENOENT (No such file or directory)`.

| Option | Does |
|---|---|
| `-e trace=open,openat,read` | only these calls |
| `-f` | follow child processes ([Chapter 13](13-processes-and-signals.md)) |
| `-c` | count calls and time instead of listing them |
| `-o FILE` | write the trace to a file |
| `-p PID` | attach to a running process |

When a program "does nothing" or fails silently, `strace` usually shows why in seconds: a missing file, a permission error, a call that blocks forever.

### glibc vs musl: the same program, different system calls

```console
$ musl-gcc -std=c99 -Wall -Wextra -pedantic -static -o slurp-musl slurp.c
$ strace -e trace=open,openat,read,readv,write,writev,close ./slurp-musl hi.txt
open("hi.txt", O_RDONLY|O_LARGEFILE)    = 3
readv(3, [{iov_base="hello\n", iov_len=4095}, {iov_base="", iov_len=1024}], 2) = 6
readv(3, [{iov_base="", iov_len=4089}, {iov_base="", iov_len=1024}], 2) = 0
close(3)                                = 0
hi.txt: 6 bytes, 1 lines
writev(1, [{iov_base="hi.txt: 6 bytes, 1", iov_len=18}, {iov_base=" lines\n", iov_len=7}], 2) = 25
+++ exited with 0 +++
```

| | glibc (dynamic) | musl (static) |
|---|---|---|
| Start-up | loader opens and reads `libc.so.6` | nothing to load |
| `fopen` | `openat(AT_FDCWD, ...)` | `open(..., O_LARGEFILE)` |
| `fread` | `read` into stdio's 4096-byte buffer, then copy | `readv` straight into your buffer *and* stdio's buffer in one call |
| `printf` at exit | `write` | `writev` |

Both are correct; the kernel treats `open` and `openat(AT_FDCWD, ...)` alike. musl's trace is shorter, so **musl is the nicer libc to strace**. Scripts built on `strace` should match both names (`-e trace=open,openat`).

## Building parley: errors, die(), assertions and sanitizer builds

So far `parleyd` and `parley` ignore most failures: on a full disk, the server's replies vanish. This milestone adds `die()` (and the logger to the client), moves the server's loop into `serve_stdin()` with one `goto` cleanup path and read/write checks, checks the client's input and output, adds `assert`s to `msg.c` and `strbuf.c`, and adds `make SANITIZE=1`.

### log.c and log.h: die()

`die()` formats like `log_msg()`. A `...` can't be passed on, but a `va_list` can (hence `vfprintf`), so the shared part moves into `log_va()`.

`project/ch10/src/log.c` (complete)
```c
/* log.c - log lines to stderr. */
#include "log.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

static const char *log_prog = "parley";
static bool log_verbose = false;

void log_init(const char *prog, bool verbose)
{
    log_prog = prog;
    log_verbose = verbose;
}

static const char *level_name(enum log_level level)
{
    switch (level) {
    case LOG_DEBUG: return "debug";
    case LOG_INFO:  return "info";
    case LOG_WARN:  return "warning";
    case LOG_ERROR: return "error";
    }
    return "?";
}

/* The shared part of log_msg() and die(): a va_list can be handed on to
 * another function, but "..." cannot. */
static void log_va(enum log_level level, const char *fmt, va_list ap)
{
    fprintf(stderr, "%s: %s: ", log_prog, level_name(level));
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
}

void log_msg(enum log_level level, const char *fmt, ...)
{
    if (level == LOG_DEBUG && !log_verbose)
        return;

    va_list ap;
    va_start(ap, fmt);
    log_va(level, fmt, ap);
    va_end(ap);
}

void die(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    log_va(LOG_ERROR, fmt, ap);
    va_end(ap);
    exit(1);
}
```

Add the declaration to `src/log.h`, after `log_msg`:

```c
/* Log an error like log_msg(LOG_ERROR, ...) and exit with status 1. For
 * errors the program cannot recover from. */
void die(const char *fmt, ...);
```

Unlike the earlier example, this `die()` doesn't append `strerror`; callers write `die("writing: %s", strerror(errno))`, with no hidden trailing-colon rule.

### parleyd.c: serve_stdin() with one cleanup path

The session loop moves out of `main` into a function that can return an error. Add `#include <errno.h>`, then replace `main` with:

`project/ch10/src/parleyd.c` (end of file)
```c
/* Run one client session on stdin/stdout. Returns 0 when the session
 * ended normally, -1 on an error (already logged). */
static int serve_stdin(void)
{
    int result = -1;
    struct conn client = { .quitting = false };   /* zeroed: nick "", in empty */
    char chunk[256];
    char line[MSG_LINE_MAX + 1];

    while (!client.quitting && fgets(chunk, sizeof chunk, stdin) != NULL) {
        if (sb_append(&client.in, chunk, strlen(chunk)) < 0) {
            log_msg(LOG_ERROR, "out of memory");
            goto out;
        }
        enum frame_status st;
        while (!client.quitting
               && (st = msg_take_line(&client.in, &client.skipping, line)) != FRAME_NONE) {
            if (st == FRAME_TOO_LONG)
                conn_numeric(&client, "417", NULL, "Input line too long");
            else
                room_handle_line(&client, line);
        }
        /* stdout is buffered: a failed write may only show up here, at
         * the flush. errno is only meaningful right after the failure. */
        if (fflush(stdout) == EOF) {
            log_msg(LOG_ERROR, "writing to client: %s", strerror(errno));
            goto out;
        }
    }
    if (ferror(stdin)) {
        log_msg(LOG_ERROR, "reading from client: %s", strerror(errno));
        goto out;
    }
    result = 0;
out:
    sb_free(&client.in);           /* one cleanup path for every exit */
    return result;
}

int main(int argc, char *argv[])
{
    struct config cfg = {
        .addr = "127.0.0.1",
        .port = DEFAULT_PORT,
        .max_clients = DEFAULT_MAX_CLIENTS,
        .verbose = false,
    };
    parse_args(argc, argv, &cfg);
    log_init("parleyd", cfg.verbose);
    log_msg(LOG_DEBUG, "verbose logging is on");
    log_msg(LOG_INFO, "reading one client session from stdin");

    if (serve_stdin() < 0)
        return 1;
    log_msg(LOG_INFO, "session over");
    return 0;
}
```

Three error checks:

- **Out of memory** in `sb_append`: before, the loop just stopped with "session over"; now the session fails.
- **Write errors** at `fflush`, once per chunk, where the real `write` happens. ([Chapter 12](12-files-and-descriptors.md) replaces stdio with direct writes.)
- **Read errors**: `fgets` returns `NULL` for both end of input and error; `ferror(stdin)` tells them apart.

Every failure logs and jumps to `out`, which frees the input buffer.

### parley.c: the client checks too

The client now logs through `log.c`. Add `#include <errno.h>` and `#include "log.h"`; in `main`, call `log_init` after `parse_args` and add two checks before `return 0`:

`project/ch10/src/parley.c` (`main`)
```c
int main(int argc, char *argv[])
{
    struct config cfg = { .host = "localhost", .port = DEFAULT_PORT, .nick = "guest" };
    parse_args(argc, argv, &cfg);
    log_init("parley", false);
    send_msg(&(struct msg){ .command = "NICK", .nparams = 1, .params = { cfg.nick } });

    char line[MSG_LINE_MAX];
    while (!quitting && fgets(line, sizeof line, stdin) != NULL) {
        if (strchr(line, '\n') == NULL && !feof(stdin)) {
            int c;
            while ((c = getchar()) != EOF && c != '\n')
                ;   /* throw away the rest of the long line */
            fprintf(stderr, "parley: line too long (max %zu bytes), not sent\n",
                    sizeof line - 2);
            continue;
        }
        str_chomp(line);
        if (line[0] == '\0')
            continue;              /* ignore empty lines */
        if (line[0] == '/')
            run_command(line);
        else
            send_msg(&(struct msg){ .command = "MSG", .trailing = line });
    }
    if (!quitting)
        send_msg(&(struct msg){ .command = "QUIT" });   /* end of input: goodbye */
    if (ferror(stdin))
        die("reading input: %s", strerror(errno));
    if (fflush(stdout) == EOF)
        die("writing: %s", strerror(errno));
    return 0;
}
```

Otherwise `exit` would flush stdout after `return 0` and a failed write would go unnoticed: status 0, data lost. Always check an output stream's final flush (or `fclose`). `CLIENT_OBJS` now needs `log.o` (below).

### Assertions for caller bugs

A negative `i` in `msg_param(m, i)`, or a `msg_format` buffer too small for `"\r\n"`, is a caller bug that network input can't cause: an `assert`. Add `#include <assert.h>` to `src/msg.c`, then:

`project/ch10/src/msg.c` (`msg_param`, and the top of `msg_format`)
```c
const char *msg_param(const struct msg *m, int i)
{
    assert(i >= 0);
    if (i < m->nparams)
        return m->params[i];
    if (i == m->nparams)
        return m->trailing;
    return NULL;
}
```

```c
int msg_format(char *out, size_t size, const struct msg *m)
{
    assert(size >= 3);                 /* room for at least "\r\n" */

    /* Build the line without "\r\n" in at most MSG_LINE_MAX - 2 bytes. */
    if (size > MSG_LINE_MAX + 1)
        size = MSG_LINE_MAX + 1;
    ...                                /* the rest is unchanged */
```

In `src/strbuf.c`, add `#include <assert.h>` and check the invariant at the top of `sb_consume`, which writes `data[len]` and is safe only if `len < cap`:

`project/ch10/src/strbuf.c` (`sb_consume`)
```c
void sb_consume(struct strbuf *sb, size_t n)
{
    assert(sb->len < sb->cap || sb->cap == 0);   /* room for the '\0' */
    if (n >= sb->len) {
        sb->len = 0;
    } else {
        memmove(sb->data, sb->data + n, sb->len - n);   /* regions overlap */
        sb->len -= n;
    }
    if (sb->data != NULL)
        sb->data[sb->len] = '\0';
}
```

Bad client input is never asserted; the parser still reports it with return codes.

### Makefile: SANITIZE=1

Add a line to the help comment at the top:

```make
#   make SANITIZE=1      build with sanitizers into build/glibc-san/
```

Then, after the two `CFLAGS` lines:

```make
# Sanitizers add run-time checks for memory errors (ASan) and undefined
# behaviour (UBSan). Their run-time libraries need glibc; with musl we can
# only use UBSan in trap mode, which crashes on the spot without a message.
ifeq ($(SANITIZE),1)
  ifeq ($(LIBC),musl)
    SANFLAGS := -fsanitize=undefined -fsanitize-undefined-trap-on-error
  else
    SANFLAGS := -fsanitize=address,undefined -fno-omit-frame-pointer
  endif
  CFLAGS  += $(SANFLAGS)
  LDFLAGS += $(SANFLAGS)
  BUILD   := $(BUILD)-san
endif
```

And add `log.o` to the client:

```make
CLIENT_OBJS := $(addprefix $(BUILD)/, parley.o msg.o strbuf.o str.o log.o)
```

- The flags go to `CFLAGS` and to `LDFLAGS`: the linker must pull in the sanitizer run-time.
- `BUILD := $(BUILD)-san` keeps sanitized objects apart; mixed with plain ones, `make` would wrongly think everything was up to date.
- `LIBC=musl` gives `build/musl-san/` with UBSan in trap mode, musl's only sanitizer.
- `CFLAGS ?= -O2 -g` stays: sanitizers work with optimisation, and `-g` gives line numbers.

### Checkpoint

Build all four variants:

```console
$ make -s && make -s LIBC=musl && make -s SANITIZE=1 && make -s SANITIZE=1 LIBC=musl && ls build
glibc
glibc-san
musl
musl-san
```

`/dev/full` fails every write with `ENOSPC`, the easiest test for write errors; a directory as stdin tests read errors:

```console
$ printf 'NICK al\r\nQUIT\r\n' | build/glibc/parleyd >/dev/full; echo "exit $?"
parleyd: info: reading one client session from stdin
parleyd: info: al joined
parleyd: error: writing to client: No space left on device
exit 1
$ printf 'NICK al\r\nQUIT\r\n' | build/musl/parleyd >/dev/full; echo "exit $?"
parleyd: info: reading one client session from stdin
parleyd: info: al joined
parleyd: error: writing to client: No space left on device
exit 1
$ build/glibc/parleyd </ >/dev/null; echo "exit $?"
parleyd: info: reading one client session from stdin
parleyd: error: reading from client: Is a directory
exit 1
$ build/musl/parleyd </ >/dev/null; echo "exit $?"
parleyd: info: reading one client session from stdin
parleyd: error: reading from client: Is a directory
exit 1
$ echo hi | build/musl/parley >/dev/full; echo "exit $?"
parley: error: writing: No space left on device
exit 1
```

`ENOSPC` and `EISDIR` have the same text on both libcs. The sanitizer builds run a session with a too-long line and report nothing:

```console
$ { printf 'NICK alice\r\nMSG :'; head -c 3000 /dev/zero | tr '\0' x; printf '\r\nMSG :ok\r\nQUIT\r\n'; } | build/glibc-san/parleyd; echo "exit $?"
parleyd: info: reading one client session from stdin
parleyd: info: alice joined
:parley 001 alice :Welcome to parley, alice
:alice JOIN
:parley 353 alice :alice
:parley 366 alice :End of NAMES
:parley 417 alice :Input line too long
:alice MSG :ok
parleyd: info: alice left (Quit)
ERROR :Goodbye
parleyd: info: session over
exit 0
$ { printf 'NICK alice\r\nMSG :'; head -c 3000 /dev/zero | tr '\0' x; printf '\r\nMSG :ok\r\nQUIT\r\n'; } | build/musl-san/parleyd 2>/dev/null; echo "exit $?"
:parley 001 alice :Welcome to parley, alice
:alice JOIN
:parley 353 alice :alice
:parley 366 alice :End of NAMES
:parley 417 alice :Input line too long
:alice MSG :ok
ERROR :Goodbye
exit 0
```

Finally, trip an assertion on purpose with `bad.c` in the project directory:

```c
#include "msg.h"
int main(void)
{
    char out[2];                               /* too small: msg_format needs 3 */
    struct msg m = { .command = "PING" };
    return msg_format(out, sizeof out, &m);
}
```

```console
$ gcc -std=c99 -Isrc -o bad bad.c src/msg.c src/strbuf.c && ./bad; echo "exit $?"
bad: src/msg.c:78: msg_format: Assertion `size >= 3' failed.
Aborted (core dumped)
exit 134
$ musl-gcc -std=c99 -static -Isrc -o bad bad.c src/msg.c src/strbuf.c && ./bad; echo "exit $?"
Assertion failed: size >= 3 (src/msg.c: msg_format: 78)
Aborted (core dumped)
exit 134
$ gcc -std=c99 -DNDEBUG -Isrc -o bad bad.c src/msg.c src/strbuf.c && ./bad; echo "exit $? (assert compiled out)"
*** stack smashing detected ***: terminated
Aborted (core dumped)
exit 134 (assert compiled out)
$ rm -f bad bad.c && make clean
rm -rf build
```

With `-DNDEBUG` the assert is gone and `msg_format` writes past the 2-byte array. Here Ubuntu's gcc caught it with its stack protector (on by default on many distributions); elsewhere it may crash differently or seem to work. That's UB.

The complete project at this point is in `project/ch10/`.

## Summary

- Failures come back as -1 with `errno`, `NULL`, or values like `EOF` plus `ferror`. Check every one.
- `errno` is valid only right after a reported failure; save it at once. Set it to 0 before `strtol`.
- `strerror` and `perror` texts differ between glibc and musl: never compare or parse them.
- Use the POSIX `strerror_r` (returns `int`); `_GNU_SOURCE` gives glibc's `char *` version. musl has only POSIX's.
- A `die()` on `vfprintf` and `strerror` is portable; `error()` and `backtrace()` are glibc-only.
- `goto cleanup`: resources start at "nothing to free", `result` is set only on success, jumps go forward.
- `assert` is for programmer errors only, without side effects; `-DNDEBUG` removes it. Both libcs exit with 134.
- The optimiser assumes undefined behaviour never happens, so `-O0` and `-O2` can disagree. Usual suspects: signed overflow, out-of-bounds, use after free, uninitialised reads, NULL.
- Test under `-fsanitize=address,undefined` on glibc; musl has only UBSan trap mode, found with gdb. An ASan report's first stack is the bad access, the second the allocation.
- gdb: `-g -O0`; `run`, `bt`, `frame`, `print`, `break`, `next`/`step`, `finish`, `watch`, `x`; Ctrl-C for hangs; core files for later, placed by `core_pattern`.
- `strace` shows system calls and `errno`s; musl's (`open`, `readv`, `writev`) are shorter than glibc's (`openat`, `read`, `write`).

## Exercises

1. **xmalloc.** Write `xmalloc`, `xrealloc` and `xstrdup`: like `malloc`, `realloc` and `strdup` (POSIX, not C99), but they print an error and exit instead of returning `NULL`. Test the failure path with `xmalloc(SIZE_MAX / 2)` on both libcs.

<details><summary>Solution</summary>

```c
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void die(const char *msg)
{
    fprintf(stderr, "xmalloc: %s: %s\n", msg, strerror(errno));
    exit(1);
}

static void *xmalloc(size_t n)
{
    void *p = malloc(n);
    if (p == NULL && n != 0)
        die("malloc");
    return p;
}

static void *xrealloc(void *old, size_t n)
{
    void *p = realloc(old, n);
    if (p == NULL && n != 0)
        die("realloc");
    return p;
}

static char *xstrdup(const char *s)
{
    size_t n = strlen(s) + 1;
    return memcpy(xmalloc(n), s, n);
}

int main(void)
{
    char *s = xstrdup("parley");
    s = xrealloc(s, 64);
    strcat(s, " works");
    printf("%s\n", s);
    free(s);
    xmalloc(SIZE_MAX / 2);
    return 0;
}
```

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o xmalloc xmalloc.c && ./xmalloc; echo "exit $?"
parley works
xmalloc: malloc: Cannot allocate memory
exit 1
$ musl-gcc -std=c99 -Wall -Wextra -pedantic -static -o xmalloc-musl xmalloc.c && ./xmalloc-musl; echo "exit $?"
parley works
xmalloc: malloc: Out of memory
exit 1
```

`malloc(0)` may legally return `NULL`, so that isn't an error. `s = xrealloc(s, ...)` is safe, unlike with `realloc`, because `xrealloc` never returns `NULL` for a non-zero size. Use these where running out of memory means "give up"; a server that must stay up handles `NULL`, as `parleyd` does.

</details>

2. **Fix nicks.c.** Change `param` so a line without a parameter gives `""`. What does the fixed program print? Then: what is a segmentation fault, at the hardware and operating system level?

<details><summary>Solution</summary>

```c
static const char *param(const char *line)
{
    const char *sp = strchr(line, ' ');
    return sp != NULL ? sp + 1 : "";
}
```

```console
$ ./nicks
total = 10
```

5 (`alice`) + 2 (`#c`) + 0 + 3 (`bob`), on musl too.

Addresses are virtual; the CPU's memory management unit translates them through the kernel's page tables. `0x1` is in the first page, which is never mapped, so the CPU raises a page fault. The kernel finds no valid mapping and sends `SIGSEGV`, whose default action ends the process (dumping core). The shell reports "Segmentation fault", status 139 (128 + 11).

</details>

3. **Fix spin.c with a watchpoint.** In gdb, `break count_spaces`, `run`, `watch s`, then `continue` a few times. When does `s` change? Fix the bug and check the output on both libcs.

<details><summary>Solution</summary>

The watchpoint fires once, when `s` passes the leading space, and never again. The increment must happen on every pass:

```c
    while (*s != '\0') {
        if (*s == ' ')
            n++;
        s++;
    }
```

```console
$ ./spin
2
3
```

`" JOIN #c"` has 2 spaces and `"PRIVMSG #c :hi there"` has 3, on both libcs.

</details>

4. **Spot the UB.** Which of these lines have undefined behaviour (with 32-bit `int`)? Check with a test program built with `-fsanitize=address,undefined`.

```c
int a[4] = {0};  int k = 4;
unsigned u = UINT_MAX;
int s = 31;

u++;                  /* (a) */
int i = 1 << s;       /* (b) */
printf("%d\n", a[k]); /* (c) */
char *p = "hi"; p[0] = 'H';   /* (d) */
int x; if (x > 0) puts("+"); /* (e) */
```

<details><summary>Solution</summary>

- (a) **Defined.** Unsigned arithmetic wraps: `u` becomes 0.
- (b) **UB.** `1 << 31` doesn't fit in a signed 32-bit `int`. UBSan: `runtime error: left shift of 1 by 31 places cannot be represented in type 'int'`. Use `1u << 31`.
- (c) **UB.** `a[4]` is past the end. UBSan: `runtime error: index 4 out of bounds for type 'int [4]'`; ASan: `stack-buffer-overflow`.
- (d) **UB.** String literals may be read-only; on Linux this crashes with `SIGSEGV`. `char p[] = "hi";` is a modifiable copy.
- (e) **UB** (uninitialised read). gcc's `-Wall` warns when it can see it (`warning: ‘x’ is used uninitialized [-Wuninitialized]`); sanitizers don't catch it.

Take the values from `argc` (`int k = 3 + argc;` and so on) so the compiler can't fold them. Only (b) and (c) produce reports.

</details>

5. **A portable error string.** Write `const char *errstr(int err, char *buf, size_t size)`: thread-safe (uses `strerror_r`), always returns `buf`, and puts `error N` in it if `strerror_r` fails. What does it print for `ENOENT` and `9999` on each libc?

<details><summary>Solution</summary>

```c
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <stdio.h>
#include <string.h>

/* Thread-safe and the same on every POSIX libc: always returns buf. */
static const char *errstr(int err, char *buf, size_t size)
{
    if (strerror_r(err, buf, size) != 0)
        snprintf(buf, size, "error %d", err);
    return buf;
}

int main(void)
{
    char buf[64];
    printf("%s\n", errstr(ENOENT, buf, sizeof buf));
    printf("%s\n", errstr(9999, buf, sizeof buf));
    return 0;
}
```

```console
$ ./errstr
No such file or directory
error 9999
$ ./errstr-musl
No such file or directory
No error information
```

glibc's `strerror_r` reports `EINVAL` for an unknown number, so the fallback runs; musl returns 0 with its generic text. Either is fine for a human reader, which is all an error string is for.

</details>

Sources for this chapter: [REFERENCES.md](REFERENCES.md#chapter-10--errors-and-debugging)
