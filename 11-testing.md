# Chapter 11 — Testing

[Chapter 10](10-errors-and-debugging.md) found bugs after they happened. A test finds them first: a small program that calls your code with known inputs and checks the results. Run the tests after every change and you know at once whether it broke something.

C has no built-in test framework, and you don't need a big one. This chapter builds one in about 100 lines: check macros that report `file:line`, a list of tests, and a runner that sets the exit status. You use it for table-driven tests, for input that arrives in pieces, and, from a shell script, for a whole program, always with both C libraries: a test can pass with glibc and fail with musl. `parley` gets 20 unit tests plus `make test` and `make test-all`. Next: [Chapter 12](12-files-and-descriptors.md).

## Why write tests

A test turns "I think this works" into "this works for these inputs, and I can check again in one second". That matters most when you change code later: change one of the parser's rules from [Chapter 7](07-structs-unions-enums.md) (optional prefix, up to 8 parameters, trailing parameter after `:`) and the tests show whether the others still hold.

This course writes **unit tests**: each calls a function directly, not through network or terminal. They take milliseconds, need no server, and a failure points at one function.

A good test:

- **checks one behaviour**, named for it: `str_copy_truncates`, not `test2`;
- **is deterministic**: same code, same result. No clocks, no network, no random input without a printed seed;
- **says what went wrong**: "`tests/test_nick.c:12: CHECK(!nick_valid("abcdefghijklmnopq"))`", not "test failed";
- **covers the edges**: empty strings, the longest allowed input, one byte too long, NULL.

## A first try: assert

You already know `assert` from [Chapter 10](10-errors-and-debugging.md). Here it tests a buggy `starts_with`.

`examples/ch11/assert-test.c`

```c
/* assert-test.c - testing with assert(): it works, but badly. */
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

/* Does s start with prefix? (This version has a bug.) */
static bool starts_with(const char *s, const char *prefix)
{
    return strncmp(s, prefix, strlen(s)) == 0;
}

int main(void)
{
    assert(starts_with("NICK al", "NICK"));
    assert(!starts_with("NI", "NICK"));
    assert(!starts_with("", "NICK"));
    assert(starts_with("MSG", ""));
    puts("all tests passed");
    return 0;
}
```

The bug: `strncmp` should compare `strlen(prefix)` bytes, not `strlen(s)`.

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o assert-test assert-test.c
$ ./assert-test; echo "exit $?"
assert-test: assert-test.c:15: main: Assertion `starts_with("NICK al", "NICK")' failed.
Aborted (core dumped)
exit 134
```

It found the bug and the line, but:

1. **It stops at the first failure.** Three of four checks fail and you see one.
2. **It shows the expression, not the values.** For `assert(n == 5)` you want to know `n`.
3. **It can vanish.** `-DNDEBUG` (common for release builds) removes every `assert`:

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -DNDEBUG -o assert-test assert-test.c
assert-test.c:8:13: warning: ‘starts_with’ defined but not used [-Wunused-function]
    8 | static bool starts_with(const char *s, const char *prefix)
      |             ^~~~~~~~~~~
$ ./assert-test; echo "exit $?"
all tests passed
exit 0
```

"All tests passed", and none ran; only the warning hints at it. musl lays out the message differently ([Chapter 10](10-errors-and-debugging.md)), with the same exit status:

```console
$ musl-gcc -std=c99 -Wall -Wextra -pedantic -static -o assert-test-musl assert-test.c
$ ./assert-test-musl; echo "exit $?"
Assertion failed: starts_with("NICK al", "NICK") (assert-test.c: main: 15)
Aborted (core dumped)
exit 134
```

`assert` is for "can't happen" checks inside a program. Tests need their own macros.

## A CHECK macro that keeps going

Your own macro fixes all three: on failure it prints where and what, counts it and carries on.

`examples/ch11/check.c`

```c
/* check.c - a tiny test framework: a CHECK macro, a table of tests and a
 * runner that reports every failed check and sets the exit status. */
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

static int failed_checks;    /* failed checks in the current test */

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            printf("%s:%d: CHECK(%s)\n", __FILE__, __LINE__, #cond);    \
            failed_checks++;                                            \
        }                                                               \
    } while (0)

/* Does s start with prefix? (This version has a bug.) */
static bool starts_with(const char *s, const char *prefix)
{
    return strncmp(s, prefix, strlen(s)) == 0;
}

static void test_starts_with_yes(void)
{
    CHECK(starts_with("NICK al", "NICK"));
    CHECK(starts_with("NICK", "NICK"));
    CHECK(starts_with("MSG", ""));
}

static void test_starts_with_no(void)
{
    CHECK(!starts_with("NI", "NICK"));
    CHECK(!starts_with("", "NICK"));
    CHECK(!starts_with("MSG :hi", "NICK"));
}

static const struct test {
    const char *name;
    void (*run)(void);
} tests[] = {
    { "starts_with_yes", test_starts_with_yes },
    { "starts_with_no", test_starts_with_no },
};

int main(void)
{
    int passed = 0, failed = 0;
    for (size_t i = 0; i < sizeof tests / sizeof tests[0]; i++) {
        failed_checks = 0;
        tests[i].run();
        if (failed_checks == 0) {
            passed++;
        } else {
            failed++;
            printf("FAIL %s\n", tests[i].name);
        }
    }
    printf("%d passed, %d failed\n", passed, failed);
    return failed == 0 ? 0 : 1;
}
```

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o check check.c
$ ./check; echo "exit $?"
check.c:25: CHECK(starts_with("NICK al", "NICK"))
check.c:27: CHECK(starts_with("MSG", ""))
FAIL starts_with_yes
check.c:32: CHECK(!starts_with("NI", "NICK"))
check.c:33: CHECK(!starts_with("", "NICK"))
FAIL starts_with_no
0 passed, 2 failed
exit 1
```

All four failures in one run. musl prints the same, because the output is ours, not the library's. With `strlen(prefix)` you get `2 passed, 0 failed` and status 0.

The parts, all from [Chapter 9](09-preprocessor-and-building.md):

- **`#cond`** turns the argument into a string literal, so the message shows the code as written.
- **`__FILE__` and `__LINE__`** give the place where the macro is *used*. That's why `CHECK` must be a macro: a function would report its own line.
- **`failed_checks`** counts failures in the current test; the runner resets it before each test.
- **The table of tests** pairs names with function pointers ([Chapter 5](05-pointers-and-arrays.md)); the runner loops over it and exits with 0 only if all passed.

Everything goes to stdout. Split over stderr and stdout, the streams can be reordered in a pipe ([Chapter 12](12-files-and-descriptors.md)), and a failure could print after its summary.

## Two macro traps

### Arguments evaluated twice

For comparisons you want the values too: `n == 5: 4 != 5`. The obvious way and the right way:

`examples/ch11/twice.c`

```c
/* twice.c - two traps in check macros: arguments evaluated twice, and an
 * "if" inside a macro that steals the caller's "else". */
#include <stdio.h>

#define CHECK_INT_BAD(a, b)                                             \
    if ((a) != (b))                                                     \
        printf("%d: %s == %s: %d != %d\n", __LINE__, #a, #b, (a), (b))

#define CHECK_INT(a, b)                                                 \
    do {                                                                \
        long long a_ = (a), b_ = (b);                                   \
        if (a_ != b_)                                                   \
            printf("%d: %s == %s: %lld != %lld\n", __LINE__, #a, #b,    \
                   a_, b_);                                             \
    } while (0)

static int counter;

static int next_id(void)
{
    return ++counter;
}

int main(void)
{
    puts("bad macro:");
    CHECK_INT_BAD(next_id(), 1);
    CHECK_INT_BAD(next_id(), 1);       /* fails: but which value is shown? */
    printf("next_id() was called %d times\n", counter);

    counter = 0;
    puts("good macro:");
    CHECK_INT(next_id(), 1);
    CHECK_INT(next_id(), 1);
    printf("next_id() was called %d times\n", counter);
    return 0;
}
```

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o twice twice.c
$ ./twice
bad macro:
28: next_id() == 1: 3 != 1
next_id() was called 3 times
good macro:
34: next_id() == 1: 2 != 1
next_id() was called 2 times
```

The second `next_id()` returned 2 and the check rightly failed. But `CHECK_INT_BAD` pastes `(a)` into both the `if` and the `printf`, so `next_id()` runs again and the message says `3`, a value never compared. With side effects (a parser that advances, a buffer that consumes) the bad macro lies.

`CHECK_INT` evaluates each argument once into a local and uses those. `long long` holds any `int`, `long`, `size_t` or `enum` this course compares. Name the locals so they can't clash with the caller's; the project uses `check_a_` and `check_b_`.

### The stolen else

Why `do { ... } while (0)` around every macro? Without it:

`examples/ch11/dangling.c`

```c
/* dangling.c - why check macros are wrapped in do { ... } while (0). */
#include <stdio.h>

#define CHECK_BAD(cond) \
    if (!(cond)) printf("%d: CHECK(%s)\n", __LINE__, #cond)

int main(void)
{
    int verbose = 0;
    if (verbose)
        CHECK_BAD(1 + 1 == 2);
    else
        puts("quiet mode");
    return 0;
}
```

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o dangling dangling.c
dangling.c: In function ‘main’:
dangling.c:10:8: warning: suggest explicit braces to avoid ambiguous ‘else’ [-Wdangling-else]
   10 |     if (verbose)
      |        ^
$ ./dangling
$
```

It prints nothing. Expanded, the code is `if (verbose) if (!(1 + 1 == 2)) printf(...); else puts("quiet mode");`, and an `else` belongs to the nearest `if`, the macro's.

`do { ... } while (0)` makes the macro one statement needing a `;`, like a function call. It runs once, can hold declarations (like `CHECK_INT`'s locals), and can't capture an `else`. Plain `{ ... }` isn't enough: `{ ... };` before an `else` is a syntax error.

## Exit status: how tests talk to make

A test program's key output is its exit status: 0 for all passed, anything else for a failure. `make`, shell scripts and CI read it.

- `make` stops at the first command that exits non-zero, prints `make: *** [Makefile:59: test] Error 1` and exits with 2.
- `./check && echo ok` prints `ok` only if all tests passed.
- `$?` is the last command's status; bash's `${PIPESTATUS[0]}` is the first in a pipeline, needed for `make test | tail`.

An `assert` failure exits with 134 (128 + SIGABRT), a crash with 139 (128 + SIGSEGV). A crashed runner still fails, as it should, but loses its buffered stdout, so the project runner calls `fflush(stdout)` after each test.

## Table-driven tests

Most functions need the same check on many inputs. Instead of twenty `CHECK_INT(count("hello\n"), 1)` lines that hide the inputs, put inputs and expected results in a table and loop over it.

The example is `mywc`, a small `wc` counting lines, words and bytes on stdin. The counting has its own file so tests can call it.

`examples/ch11/mywc/wc.h`

```c
/* wc.h - count lines, words and bytes, like wc(1). */
#ifndef WC_H
#define WC_H

#include <stdbool.h>
#include <stddef.h>

struct wc {
    long lines, words, bytes;
    bool in_word;        /* was the last byte part of a word? */
};

/* Start counting from zero. */
void wc_init(struct wc *w);

/* Count len more bytes. The input may arrive in pieces of any size: the
 * counts after all the pieces are the same as for one big piece. */
void wc_feed(struct wc *w, const char *buf, size_t len);

#endif
```

`examples/ch11/mywc/wc.c`

```c
/* wc.c - count lines, words and bytes. */
#include "wc.h"

#include <ctype.h>

void wc_init(struct wc *w)
{
    w->lines = w->words = w->bytes = 0;
    w->in_word = false;
}

void wc_feed(struct wc *w, const char *buf, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)buf[i];
        w->bytes++;
        if (c == '\n')
            w->lines++;
        if (isspace(c)) {
            w->in_word = false;
        } else if (!w->in_word) {
            w->in_word = true;       /* the first byte of a new word */
            w->words++;
        }
    }
}
```

A word starts at a non-space byte after a space (or the start). `in_word` remembers whether the previous byte was in a word, and lives in the struct, not a local; the next section shows why.

`examples/ch11/mywc/test_wc.c`

```c
/* test_wc.c - unit tests for wc.c. */
#include <stdio.h>
#include <string.h>

#include "wc.h"

static int failed_checks;

#define CHECK_INT(a, b)                                                 \
    do {                                                                \
        long long a_ = (a), b_ = (b);                                   \
        if (a_ != b_) {                                                 \
            printf("%s:%d: %s == %s: %lld != %lld\n",                   \
                   __FILE__, __LINE__, #a, #b, a_, b_);                 \
            failed_checks++;                                            \
        }                                                               \
    } while (0)

/* The test cases: one row per input. */
static const struct {
    const char *text;
    long lines, words, bytes;
} cases[] = {
    { "",                          0, 0, 0 },
    { "hello\n",                   1, 1, 6 },
    { "hello world\n",             1, 2, 12 },
    { "  two   spaces  \n",        1, 2, 17 },
    { "no newline",                0, 2, 10 },
    { "a\tb\nc\r\n",               2, 3, 7 },
    { "\n\n\n",                    3, 0, 3 },
    { "NICK al\r\nMSG :hi there\r\n", 2, 5, 24 },
};
#define NCASES (sizeof cases / sizeof cases[0])

/* Count text, feeding it to wc_feed in pieces of at most step bytes. */
static struct wc count(const char *text, size_t step)
{
    struct wc w;
    size_t len = strlen(text);
    wc_init(&w);
    for (size_t pos = 0; pos < len; pos += step)
        wc_feed(&w, text + pos, len - pos < step ? len - pos : step);
    return w;
}

/* Count case i in pieces of step bytes and check the counts. A failure
 * also says which case and step it was: the line number alone is the same
 * for every row of the table. */
static void check_case(size_t i, size_t step)
{
    int before = failed_checks;
    struct wc w = count(cases[i].text, step);
    CHECK_INT(w.lines, cases[i].lines);
    CHECK_INT(w.words, cases[i].words);
    CHECK_INT(w.bytes, cases[i].bytes);
    if (failed_checks > before)
        printf("    (case %zu, step %zu)\n", i, step);
}

/* Table-driven: every row, all in one piece. */
static void test_table(void)
{
    for (size_t i = 0; i < NCASES; i++)
        check_case(i, 1000);
}

/* Every row again, cut into pieces of every size from 1 to 5 bytes.
 * The counts must not depend on where the pieces end. */
static void test_pieces(void)
{
    for (size_t i = 0; i < NCASES; i++)
        for (size_t step = 1; step <= 5; step++)
            check_case(i, step);
}

static const struct test {
    const char *name;
    void (*run)(void);
} tests[] = {
    { "table", test_table },
    { "pieces", test_pieces },
};

int main(void)
{
    int passed = 0, failed = 0;
    for (size_t i = 0; i < sizeof tests / sizeof tests[0]; i++) {
        failed_checks = 0;
        tests[i].run();
        if (failed_checks == 0) {
            passed++;
        } else {
            failed++;
            printf("FAIL %s\n", tests[i].name);
        }
    }
    printf("test_wc: %d passed, %d failed\n", passed, failed);
    return failed == 0 ? 0 : 1;
}
```

The table is an array of anonymous structs, one row per case, lined up like a spreadsheet. Good rows:

- the empty input;
- the simplest normal input (`"hello\n"`);
- each special rule alone (several spaces, tabs, `\r\n`, no final newline, only newlines);
- one realistic input (two protocol lines).

Every failing row reports the same `__LINE__`, the loop's, so `check_case` also prints the row (and piece size). Always print the row index or input in a table-driven test.

## Testing code that gets its input in pieces

`mywc` reads 4096 bytes at a time; from [Chapter 15](15-a-first-server.md) on, the server gets network bytes in unpredictable pieces. Stream code must give the same answer however the stream is cut. `test_pieces` feeds every row in pieces of 1 to 5 bytes, and catches a `wc_feed` that forgets the previous piece's state:

```c
void wc_feed(struct wc *w, const char *buf, size_t len)
{
    w->in_word = false;          /* BUG: forgets the last piece */
    for (size_t i = 0; i < len; i++) {
```

`test_table` passes, and so would a manual test unless a word straddled a 4096-byte boundary. `test_pieces` doesn't:

```console
$ make test
...
./test_wc
test_wc.c:54: w.words == cases[i].words: 5 != 1
    (case 1, step 1)
test_wc.c:54: w.words == cases[i].words: 3 != 1
    (case 1, step 2)
...
    (case 7, step 5)
FAIL pieces
test_wc: 1 passed, 1 failed
make: *** [Makefile:25: test] Error 1
```

Case 1 is `"hello\n"`: fed one byte at a time, each byte looked like a new word, giving 5. The parley framer has the same kind of test, `frame_one_byte_at_a_time`, below.

## Testing the whole program from outside

Unit tests check functions; you also want the whole program right: stdin, output format, exit status. `mywc` has a perfect reference, the system's `wc`, and a shell script can run both on the same input and compare.

`examples/ch11/mywc/mywc.c`

```c
/* mywc.c - print the lines, words and bytes on standard input. */
#include <stdio.h>

#include "wc.h"

int main(void)
{
    char buf[4096];
    size_t n;
    struct wc w;

    wc_init(&w);
    while ((n = fread(buf, 1, sizeof buf, stdin)) > 0)
        wc_feed(&w, buf, n);
    if (ferror(stdin)) {
        perror("mywc: stdin");
        return 1;
    }
    printf("%ld %ld %ld\n", w.lines, w.words, w.bytes);
    return 0;
}
```

`examples/ch11/mywc/test.sh`

```sh
#!/bin/sh
# test.sh - compare ./mywc with the system's wc on the same input.
# Prints a line for each difference. Exit status 0 if they all agree.

LC_ALL=C          # wc counts words by locale; use the plain C rules
export LC_ALL
passed=0
failed=0

# same NAME: run the file test.in through wc and ./mywc and compare.
same() {
    want=$(wc < test.in | awk '{ print $1, $2, $3 }')
    got=$(./mywc < test.in)
    if [ "$got" = "$want" ]; then
        passed=$((passed + 1))
    else
        echo "FAIL $1: mywc says '$got', wc says '$want'"
        failed=$((failed + 1))
    fi
}

printf ''                    > test.in; same empty
printf 'one line\n'          > test.in; same one-line
printf 'no newline'          > test.in; same no-newline
printf 'a\tb\r\n\n  c  \n\n' > test.in; same whitespace
cat ./*.c                    > test.in; same sources
cat ./*.c ./*.c ./*.c ./*.c  > test.in; same bigger-than-4096
rm -f test.in

echo "test.sh: $passed passed, $failed failed"
[ "$failed" -eq 0 ]
```

`awk '{ print $1, $2, $3 }'` turns `wc`'s padded numbers into `mywc`'s `1 2 3` format. The last line, `[ "$failed" -eq 0 ]`, is the script's exit status. Four copies of the sources are about 16 KB, more than one 4096-byte `fread`, so the read loop gets tested too.

`examples/ch11/mywc/Makefile`

```make
# Makefile for mywc.
#
#   make             build mywc and test_wc
#   make test        build, then run the unit tests and test.sh
#   make clean       remove what make built
#
# For musl:  make clean && make CC=musl-gcc LDFLAGS=-static test

CFLAGS ?= -O2 -g
CFLAGS += -std=c99 -Wall -Wextra -pedantic

.PHONY: all test clean

all: mywc test_wc

mywc: mywc.o wc.o
	$(CC) $(LDFLAGS) -o $@ $^

test_wc: test_wc.o wc.o
	$(CC) $(LDFLAGS) -o $@ $^

mywc.o test_wc.o wc.o: wc.h

test: all
	./test_wc
	./test.sh

clean:
	rm -f mywc test_wc *.o test.in
```

The `.o` files come from make's built-in rule ([Chapter 9](09-preprocessor-and-building.md)). `test` depends on `all`, so it tests fresh binaries. Unit tests run first; if they fail, `make` stops before the slower script.

```console
$ cd examples/ch11/mywc
$ make test
cc -O2 -g -std=c99 -Wall -Wextra -pedantic   -c -o mywc.o mywc.c
cc -O2 -g -std=c99 -Wall -Wextra -pedantic   -c -o wc.o wc.c
cc  -o mywc mywc.o wc.o
cc -O2 -g -std=c99 -Wall -Wextra -pedantic   -c -o test_wc.o test_wc.c
cc  -o test_wc test_wc.o wc.o
./test_wc
test_wc: 2 passed, 0 failed
./test.sh
test.sh: 6 passed, 0 failed
$ make clean && make CC=musl-gcc LDFLAGS=-static test
rm -f mywc test_wc *.o test.in
musl-gcc -O2 -g -std=c99 -Wall -Wextra -pedantic   -c -o mywc.o mywc.c
musl-gcc -O2 -g -std=c99 -Wall -Wextra -pedantic   -c -o wc.o wc.c
musl-gcc -static -o mywc mywc.o wc.o
musl-gcc -O2 -g -std=c99 -Wall -Wextra -pedantic   -c -o test_wc.o test_wc.c
musl-gcc -static -o test_wc test_wc.o wc.o
./test_wc
test_wc: 2 passed, 0 failed
./test.sh
test.sh: 6 passed, 0 failed
```

Use a reference program when you have one. This one is GNU `wc` 8.32 (Ubuntu 22.04); another `wc` (BusyBox, BSD, newer GNU) may disagree on odd inputs, and exercise 5 finds one. Without a reference, write expected outputs by hand, as the table does.

## Testing with both C libraries

C leaves many details to the implementation. Code can rely on one by accident and pass its tests until someone uses another C library, so run tests with both. This one passes with glibc and fails with musl.

`examples/ch11/qsort-test.c`

```c
/* qsort-test.c - a test that passes with one C library and fails with
 * another. qsort() need not keep equal elements in their original order. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct user {
    int idle;            /* minutes since the user last spoke */
    int joined;          /* order of joining: 1, 2, 3, ... */
    const char *nick;
};

/* Sort by idle time only. Users with the same idle time are "equal". */
static int by_idle(const void *a, const void *b)
{
    const struct user *x = a, *y = b;
    return (x->idle > y->idle) - (x->idle < y->idle);
}

/* Sort by idle time, then by join order: no two users are ever equal. */
static int by_idle_then_joined(const void *a, const void *b)
{
    const struct user *x = a, *y = b;
    int c = by_idle(a, b);
    if (c != 0)
        return c;
    return (x->joined > y->joined) - (x->joined < y->joined);
}

int main(int argc, char *argv[])
{
    struct user users[] = {
        { 5, 1, "alice" }, { 0, 2, "bob" }, { 5, 3, "carol" }, { 0, 4, "dave" },
        { 5, 5, "erin" },  { 0, 6, "frank" }, { 5, 7, "grace" }, { 0, 8, "heidi" },
    };
    /* What the test expects: idle 0 first, each group in join order. */
    const char *want[] = { "bob", "dave", "frank", "heidi",
                           "alice", "carol", "erin", "grace" };
    size_t n = sizeof users / sizeof users[0];
    int fixed = argc > 1 && strcmp(argv[1], "fixed") == 0;
    int failed = 0;

    qsort(users, n, sizeof users[0], fixed ? by_idle_then_joined : by_idle);
    for (size_t i = 0; i < n; i++) {
        if (strcmp(users[i].nick, want[i]) != 0) {
            printf("position %zu: got %s, want %s\n", i, users[i].nick, want[i]);
            failed = 1;
        }
    }
    puts(failed ? "FAIL" : "ok");
    return failed;
}
```

The comparators use `(x > y) - (x < y)`: -1, 0 or 1, with no overflow, unlike `x - y`. The test expects idle users in joining order, which is their order in the array.

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o qsort-test qsort-test.c
$ ./qsort-test; echo "exit $?"
ok
exit 0
$ musl-gcc -std=c99 -Wall -Wextra -pedantic -static -o qsort-test-musl qsort-test.c
$ ./qsort-test-musl; echo "exit $?"
position 0: got frank, want bob
position 1: got bob, want dave
position 2: got heidi, want frank
position 3: got dave, want heidi
FAIL
exit 1
$ ./qsort-test-musl fixed; echo "exit $?"
ok
exit 0
```

The test was wrong, not musl. Under `by_idle`, bob, dave, frank and heidi are equal, and C99 leaves the order of equal elements after `qsort` unspecified; glibc happened to keep it. The fix is a **tie-breaker**: compare one more field so no two different elements are equal. `./qsort-test fixed` passes on both.

### glibc vs musl: qsort

| | glibc 2.35 | musl 1.2.6 |
|---|---|---|
| Algorithm | merge sort when it can allocate a temporary array (it nearly always can), quicksort otherwise | smoothsort |
| Equal elements | keep their order (in practice, not promised) | come out in any order |
| Extra memory | up to the size of the array | none |

A **stable** sort keeps equal elements in input order. glibc's is stable in practice, unpromised, and falls back to unstable quicksort when memory is short. musl's smoothsort is never stable but needs no memory, so it can't fail or slow down on a big array.

**musl is better here**, not because unstable is good, but because it shows the bug every time; glibc lets a wrong test pass on your machine and fail elsewhere. Either way, add a tie-breaker. parley sorts nothing, but if you ever sort its users, compare something unique, such as the nickname, last.

### glibc vs musl: what not to test

Some outputs legitimately differ between the libraries; a test comparing them tests the library:

- **Message texts**: `strerror`, `perror`, `assert`, `gai_strerror`, `strsignal`. Compare `errno` values and exit statuses, which match.
- **`rand()` sequences.** Same seed, different numbers. Print random tests' seed; repeat failures with the same library.
- **`printf("%s", NULL)`.** Undefined: `(null)` or a crash, by library and optimiser ([Chapter 6](06-strings.md)). Hence the project's `CHECK_STR` prints `(null)` itself.
- **`printf("%p", ...)` and addresses.** Different format, different values every run.
- **The order of stdout and stderr lines** in a pipe. Buffering differs ([Chapter 12](12-files-and-descriptors.md)).

Which is better? A **tie**: both libraries are within their rights. The rule is yours: test behaviour (return values, `errno`, exit status, bytes written), never library prose.

## Building parley: unit tests

The project gets a `tests/` directory with a framework like `check.c`, split into files:

| File | What it is |
|---|---|
| `tests/test.h` | `CHECK`, `CHECK_INT`, `CHECK_STR` |
| `tests/tests.h` | the list of all tests, as an X-macro |
| `tests/unit.c` | the runner: `main`, `test_fail`, name selection, `-v` |
| `tests/test_str.c` | 5 tests for `str.c` |
| `tests/test_nick.c` | 2 tests for `nick.c` |
| `tests/test_strbuf.c` | 4 tests for `strbuf.c` |
| `tests/test_msg.c` | 9 tests for `msg.c`: parsing, formatting, framing |

`src/` doesn't change. The tests link the programs' own `msg.o`, `nick.o`, `strbuf.o` and `str.o`, so they test the code that ships.

### test.h: the check macros

`project/ch11/tests/test.h`

```c
/* test.h - a tiny unit-test framework: check macros.
 *
 * A failed check prints "file:line: what went wrong" and marks the current
 * test as failed, then the test carries on (so one run shows every failed
 * check). The macros are wrapped in do { ... } while (0) so they act like a
 * single statement, even after an if without braces.
 */
#ifndef PARLEY_TEST_H
#define PARLEY_TEST_H

#include <string.h>

/* Report a failed check (defined in unit.c). */
void test_fail(const char *file, int line, const char *fmt, ...);

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond))                                                    \
            test_fail(__FILE__, __LINE__, "CHECK(%s)", #cond);          \
    } while (0)

/* Compare two integers; both are evaluated exactly once. */
#define CHECK_INT(a, b)                                                 \
    do {                                                                \
        long long check_a_ = (a), check_b_ = (b);                       \
        if (check_a_ != check_b_)                                       \
            test_fail(__FILE__, __LINE__, "%s == %s: %lld != %lld",     \
                      #a, #b, check_a_, check_b_);                      \
    } while (0)

/* Compare two strings; either may be NULL (NULL only equals NULL). */
#define CHECK_STR(a, b)                                                 \
    do {                                                                \
        const char *check_a_ = (a), *check_b_ = (b);                    \
        if (check_a_ == NULL || check_b_ == NULL                        \
                ? check_a_ != check_b_                                  \
                : strcmp(check_a_, check_b_) != 0)                      \
            test_fail(__FILE__, __LINE__, "%s == %s: \"%s\" != \"%s\"", \
                      #a, #b, check_a_ ? check_a_ : "(null)",           \
                      check_b_ ? check_b_ : "(null)");                  \
    } while (0)

#endif
```

Compared with `check.c`:

- The macros call `test_fail`, which is **variadic** (`...`, [Chapter 4](04-functions.md)): each macro passes its own format and values, and counting lives in `unit.c`.
- `CHECK_STR` handles `NULL`, because parser fields such as `m.prefix` are `NULL` when absent. `strcmp(NULL, ...)` and printing `NULL` with `%s` are undefined, so the `?:` treats two `NULL`s as equal, a `NULL` and a string as different, and otherwise uses `strcmp`; the message prints `(null)` itself.

### tests.h: the list of tests

`check.c` listed each test twice: as a function and in the table. With tests in four files, `unit.c` also needs declarations. An **X-macro** ([Chapter 9](09-preprocessor-and-building.md)) keeps one list and generates the others.

`project/ch11/tests/tests.h`

```c
/* tests.h - the list of every unit test, as an X-macro.
 *
 * To add a test: write void test_NAME(void) in a test_*.c file and add
 * X(NAME) here. unit.c expands this list into a table, and the line below
 * declares every test function, so a typo is a compile error.
 */
#ifndef PARLEY_TESTS_H
#define PARLEY_TESTS_H

#define TESTS(X)                    \
    X(str_copy_fits)                \
    X(str_copy_truncates)           \
    X(str_ieq)                      \
    X(str_chomp)                    \
    X(str_to_long)                  \
    X(nick_valid)                   \
    X(nick_equal)                   \
    X(strbuf_append)                \
    X(strbuf_appendf)               \
    X(strbuf_consume)               \
    X(strbuf_binary)                \
    X(msg_parse_table)              \
    X(msg_parse_errors)             \
    X(msg_param)                    \
    X(msg_format)                   \
    X(msg_format_truncates)         \
    X(msg_roundtrip)                \
    X(frame_lines)                  \
    X(frame_one_byte_at_a_time)     \
    X(frame_too_long)

#define TEST_DECLARE(name) void test_##name(void);
TESTS(TEST_DECLARE)

#endif
```

`TESTS(TEST_DECLARE)` calls `TEST_DECLARE` per name, and `##` glues `test_` to it. Every `test_*.c` includes `tests.h`, so each function is checked against its declaration, and `unit.c` expands the same list into the table. `gcc -E` shows both expansions (cut at 200 columns):

```console
$ cd project/ch11
$ gcc -E -P -D_POSIX_C_SOURCE=200809L -Isrc tests/unit.c | grep -E 'test_str_copy_fits|tests\[\]' | cut -c1-200
void test_str_copy_fits(void); void test_str_copy_truncates(void); void test_str_ieq(void); void test_str_chomp(void); void test_str_to_long(void); void test_nick_valid(void); void test_nick_equal(voi
static const struct test tests[] = { { "str_copy_fits", test_str_copy_fits }, { "str_copy_truncates", test_str_copy_truncates }, { "str_ieq", test_str_ieq }, { "str_chomp", test_str_chomp }, { "str_to
```

The X-macro catches:

- **a listed name with no function** (a typo, or an unwritten test), which fails to link:
  `undefined reference to 'test_str_upper'`;
- **a function with the wrong signature**, which fails to compile against its declaration.

It misses a test function you **forget to list**: it compiles, links and never runs. This course's flags don't warn (gcc's `-Wmissing-declarations` would, since it then has no declaration). After adding a test, run `unit -v` and look for its name.

### unit.c: the runner

`project/ch11/tests/unit.c`

```c
/* unit.c - the unit-test runner.
 *
 *     unit            run every test
 *     unit -v         ... and list each test as it runs
 *     unit NAME...    run only the tests whose name starts with NAME
 *
 * Everything goes to stdout, so failures and results stay in order.
 * Exit status: 0 if every check passed, 1 otherwise.
 */
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "test.h"
#include "tests.h"

struct test {
    const char *name;
    void (*run)(void);
};

#define TEST_ENTRY(name) { #name, test_##name },
static const struct test tests[] = { TESTS(TEST_ENTRY) };
#undef TEST_ENTRY

#define NTESTS (sizeof tests / sizeof tests[0])

static int failed_checks;    /* failed checks in the current test */

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

/* Should the test called name run, given the command-line patterns? */
static bool selected(const char *name, int npatterns, char **patterns)
{
    if (npatterns == 0)
        return true;
    for (int i = 0; i < npatterns; i++)
        if (strncmp(name, patterns[i], strlen(patterns[i])) == 0)
            return true;
    return false;
}

int main(int argc, char *argv[])
{
    bool verbose = argc > 1 && strcmp(argv[1], "-v") == 0;
    int first = verbose ? 2 : 1;
    int passed = 0, failed = 0;

    for (size_t i = 0; i < NTESTS; i++) {
        if (!selected(tests[i].name, argc - first, argv + first))
            continue;
        failed_checks = 0;
        tests[i].run();
        if (failed_checks == 0) {
            passed++;
            if (verbose)
                printf("ok   %s\n", tests[i].name);
        } else {
            failed++;
            printf("FAIL %s\n", tests[i].name);
        }
        fflush(stdout);    /* if the next test crashes, this is already out */
    }
    printf("unit: %d passed, %d failed\n", passed, failed);
    return failed == 0 ? 0 : 1;
}
```

- `test_fail` prints `file:line: ` and passes the rest to `vprintf` through a `va_list`, the standard way to forward `...`.
- `selected` matches by **prefix**: `unit frame` runs the three `frame_*` tests, `unit str_c` runs `str_copy_fits`, `str_copy_truncates` and `str_chomp`.
- `fflush(stdout)` after each test: into a pipe (`make test | tail`), stdout is fully buffered, and a later crash would lose which tests passed.

The runner keeps only counters between tests. Each test builds what it needs (a zeroed `struct strbuf`, a local array) and frees it, so any subset runs in any order.

### What the tests check

The techniques in the four test files:

**Edge cases, one check each.** `test_nick.c` checks the longest valid nickname (16 characters), the shortest invalid one (17), the empty string, a digit or `_` first, a space, a colon and UTF-8 bytes:

`project/ch11/tests/test_nick.c`

```c
/* test_nick.c - tests for nick.c. */
#include "nick.h"
#include "test.h"
#include "tests.h"

void test_nick_valid(void)
{
    CHECK(nick_valid("alice"));
    CHECK(nick_valid("a"));
    CHECK(nick_valid("Bob_2-x"));
    CHECK(nick_valid("abcdefghijklmnop"));       /* 16: the longest */
    CHECK(!nick_valid("abcdefghijklmnopq"));     /* 17 */
    CHECK(!nick_valid(""));
    CHECK(!nick_valid("2pac"));                  /* must start with a letter */
    CHECK(!nick_valid("_x"));
    CHECK(!nick_valid("al ice"));
    CHECK(!nick_valid("al:ice"));
    CHECK(!nick_valid("caf\xc3\xa9"));           /* UTF-8 bytes are not letters */
}

void test_nick_equal(void)
{
    CHECK(nick_equal("Alice", "alice"));
    CHECK(nick_equal("BOB", "bob"));
    CHECK(!nick_equal("alice", "alicia"));
}
```

Off-by-one bugs live at limits: test the maximum and the maximum plus one.

**Tables.** `test_str_to_long` in `test_str.c` checks [Chapter 6](06-strings.md)'s port parser on 12 inputs: valid ports, the limits 1 and 65535, values just outside, a sign, leading spaces, trailing junk, hex, and a number too big for `long`:

`project/ch11/tests/test_str.c` (part)

```c
/* A table-driven test: one row per case, one loop to check them all. */
void test_str_to_long(void)
{
    static const struct {
        const char *input;
        bool ok;
        long value;
    } cases[] = {
        { "6667", true, 6667 },
        { "1", true, 1 },
        { "65535", true, 65535 },
        { "+80", true, 80 },              /* strtol allows a sign ... */
        { " 80", true, 80 },              /* ... and leading spaces */
        { "0", false, 0 },                /* below min */
        { "65536", false, 0 },            /* above max */
        { "-1", false, 0 },
        { "", false, 0 },                 /* no digits */
        { "80x", false, 0 },              /* trailing junk */
        { "0x50", false, 0 },             /* base 10 only */
        { "99999999999999999999999", false, 0 },   /* ERANGE */
    };
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        long value = 0;
        bool ok = str_to_long(cases[i].input, 1, 65535, &value);
        CHECK_INT(ok, cases[i].ok);
        CHECK_INT(value, cases[i].value);   /* untouched (0) on failure */
    }
}
```

The `+80` and `" 80"` rows record a surprise, that `strtol` accepts them; if you later reject them, they show which rows to change.

`test_msg_parse_table` in `test_msg.c` does the same for the protocol parser, one row per message shape:

`project/ch11/tests/test_msg.c` (part)

```c
void test_msg_parse_table(void)
{
    static const struct {
        const char *line;
        const char *prefix, *command;
        int nparams;
        const char *param0, *param1, *trailing;
    } cases[] = {
        { "NICK alice",              NULL,     "NICK", 1, "alice", NULL, NULL },
        { "MSG :hello there",        NULL,     "MSG",  0, NULL, NULL, "hello there" },
        { ":alice MSG :hi",          "alice",  "MSG",  0, NULL, NULL, "hi" },
        { ":parley 001 al :Welcome", "parley", "001",  1, "al", NULL, "Welcome" },
        { "  PING   a  b ",          NULL,     "PING", 2, "a",  "b",  NULL },
        { "MSG :",                   NULL,     "MSG",  0, NULL, NULL, "" },
        { "MSG ::-)",                NULL,     "MSG",  0, NULL, NULL, ":-)" },
        { "MSG :a  :b",              NULL,     "MSG",  0, NULL, NULL, "a  :b" },
        { "QUIT",                    NULL,     "QUIT", 0, NULL, NULL, NULL },
    };
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        char line[MSG_LINE_MAX + 1];
        struct msg m;
        strcpy(line, cases[i].line);       /* msg_parse modifies its input */
        CHECK_INT(msg_parse(line, &m), MSG_OK);
        CHECK_STR(m.prefix, cases[i].prefix);
        CHECK_STR(m.command, cases[i].command);
        CHECK_INT(m.nparams, cases[i].nparams);
        CHECK_STR(msg_param(&m, 0), cases[i].nparams > 0 ? cases[i].param0 : cases[i].trailing);
        if (m.nparams > 1)
            CHECK_STR(m.params[1], cases[i].param1);
        CHECK_STR(m.trailing, cases[i].trailing);
    }
}
```

`msg_parse` writes `'\0'`s into its line, so each row is copied into a local array first. Passing the literal would write to a string literal: undefined behaviour, and a crash on Linux.

**Error paths.** `test_msg_parse_errors` checks the error code for an empty line, only spaces, a prefix with no command, and nine parameters. A test is often an error path's only run.

**Round trips.** `test_msg_roundtrip` formats a message, parses the result and checks the fields match, catching mismatches between the two.

**Pieces.** The framer, `msg_take_line`, takes whole lines from a buffer that fills a few bytes at a time; like `wc_feed`, it must not care how input is cut. The helper `frame_all` feeds a text in pieces of a given size and collects the lines as `"line|line|"`:

`project/ch11/tests/test_msg.c` (part)

```c
/* TCP may deliver a line in any number of pieces, even one byte at a time.
 * The framer must give exactly the same lines for every split. */
void test_frame_one_byte_at_a_time(void)
{
    const char *text = "NICK al\r\nMSG :one byte\r\nQUIT :bye\r\n";
    char whole[256], got[256];
    frame_all(text, 1000, whole, sizeof whole);
    CHECK_STR(whole, "NICK al|MSG :one byte|QUIT :bye|");
    for (size_t step = 1; step <= 10; step++) {
        frame_all(text, step, got, sizeof got);
        CHECK_STR(got, whole);
    }
}
```

`test_frame_too_long` sends a 600-byte line in pieces of 1, 234, 467 and 700 bytes. The framer must report it once, skip its rest even when that arrives later, then read the next line normally. It also checks the exact limit: 512 bytes with `\r\n` is fine, 513 is not.

**Buffers.** `test_strbuf.c` tests start from `struct strbuf sb = { 0 };`, append past the first allocation, check the `'\0'` after the data, consume more bytes than there are, and append a `'\0'`. Each ends with `sb_free`, so `make SANITIZE=1 test` checks for leaks too.

### Makefile: test and test-all

The Makefile changes, as a diff against [Chapter 10](10-errors-and-debugging.md):

```diff
 #   make SANITIZE=1      build with sanitizers into build/glibc-san/
+#   make test            build and run the unit tests
+#   make test-all        run the tests with glibc and with musl
 #   make clean           remove build/
@@
 SERVER_OBJS := $(addprefix $(BUILD)/, parleyd.o room.o conn.o msg.o nick.o strbuf.o str.o log.o)
 CLIENT_OBJS := $(addprefix $(BUILD)/, parley.o msg.o strbuf.o str.o log.o)
+TEST_OBJS   := $(addprefix $(BUILD)/, unit.o test_str.o test_nick.o test_strbuf.o \
+                 test_msg.o msg.o nick.o strbuf.o str.o)
 
-.PHONY: all clean
+.PHONY: all test test-all clean
 
@@
+$(BUILD)/unit: $(TEST_OBJS)
+	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)
+
+test: all $(BUILD)/unit
+	$(BUILD)/unit
+
+# Run the whole test suite once per C library. Variables given on the
+# command line (like SANITIZE=1) are passed on to the sub-makes.
+test-all:
+	$(MAKE) --no-print-directory test LIBC=glibc
+	$(MAKE) --no-print-directory test LIBC=musl
+
 # Pattern rule: build/glibc/foo.o is made from src/foo.c. The build
@@
 $(BUILD)/%.o: src/%.c | $(BUILD)
 	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<
 
+# Tests include the headers in src/ as "msg.h" etc., hence -Isrc.
+$(BUILD)/%.o: tests/%.c | $(BUILD)
+	$(CC) $(CPPFLAGS) -Isrc $(CFLAGS) -c -o $@ $<
+
```

- `TEST_OBJS` has the test files and the four modules under test, not `parleyd.o` or `parley.o`: those have their own `main`, as does `unit.o`.
- `test: all $(BUILD)/unit` builds the programs too, so breaking `parleyd`'s build fails `make test` even without a test for it.
- A second pattern rule builds `build/glibc/test_msg.o` from `tests/test_msg.c`; make uses the first rule whose source exists. `-Isrc` lets tests write `#include "msg.h"`. Tests get the same `-Werror` flags as the program.
- `test-all` reruns `make` (as `$(MAKE)`, so options such as `-j` carry over) once per library. Each sub-make has its own `build/glibc/` or `build/musl/`, so objects never mix.

### Tests and sanitizers together

Tests decide which code runs; sanitizers watch how it runs. Together they catch bugs neither catches alone. `sb_consume` moves the rest of the buffer to the front with `memmove`, because source and destination overlap. Change it to `memcpy`:

```c
        memcpy(sb->data, sb->data + n, sb->len - n);
```

Overlapping `memcpy` is undefined, but both libraries happen to give the right bytes, so every test passes:

```console
$ make test 2>&1 | tail -n 1
unit: 20 passed, 0 failed
$ make LIBC=musl test 2>&1 | tail -n 1
unit: 20 passed, 0 failed
```

The same tests under AddressSanitizer:

```console
$ make SANITIZE=1 test
...
build/glibc-san/unit
=================================================================
==302652==ERROR: AddressSanitizer: memcpy-param-overlap: memory ranges [0x5060000001a0,0x5060000001ae) and [0x5060000001a9, 0x5060000001b7) overlap
    #0 0x73282063a1ed in __interceptor_memcpy ../../../../src/libsanitizer/sanitizer_common/sanitizer_common_interceptors.inc:827
    #1 0x56a012a58527 in memcpy /usr/include/x86_64-linux-gnu/bits/string_fortified.h:29
    #2 0x56a012a58527 in sb_consume src/strbuf.c:69
    #3 0x56a012a57269 in msg_take_line src/msg.c:138
    #4 0x56a012a51e7f in frame_all tests/test_msg.c:137
    #5 0x56a012a54d9a in test_frame_lines tests/test_msg.c:150
    #6 0x56a012a4edd6 in main tests/unit.c:63
...
SUMMARY: AddressSanitizer: memcpy-param-overlap ../../../../src/libsanitizer/sanitizer_common/sanitizer_common_interceptors.inc:827 in __interceptor_memcpy
==302652==ABORTING
make: *** [Makefile:59: test] Error 1
```

(Addresses and the number after `==` vary.) The framer test called `msg_take_line`, then `sb_consume`, then `memcpy` on ranges 9 bytes apart. `test_strbuf_consume` moved its 3 remaining bytes by 9, without overlap. The tests supplied the input; ASan saw the bug. Put the `memmove` back, and before calling a change done, run `make test-all` (both libraries) and `make SANITIZE=1 test`.

### Checkpoint

Build and run the tests. The last lines of `make test`:

```console
$ cd project/ch11
$ make test | tail -n 3
cc  -o build/glibc/unit build/glibc/unit.o build/glibc/test_str.o build/glibc/test_nick.o build/glibc/test_strbuf.o build/glibc/test_msg.o build/glibc/msg.o build/glibc/nick.o build/glibc/strbuf.o build/glibc/str.o 
build/glibc/unit
unit: 20 passed, 0 failed
```

With `-v`, every test by name:

```console
$ build/glibc/unit -v
ok   str_copy_fits
ok   str_copy_truncates
ok   str_ieq
ok   str_chomp
ok   str_to_long
ok   nick_valid
ok   nick_equal
ok   strbuf_append
ok   strbuf_appendf
ok   strbuf_consume
ok   strbuf_binary
ok   msg_parse_table
ok   msg_parse_errors
ok   msg_param
ok   msg_format
ok   msg_format_truncates
ok   msg_roundtrip
ok   frame_lines
ok   frame_one_byte_at_a_time
ok   frame_too_long
unit: 20 passed, 0 failed
```

Only tests matching a name prefix:

```console
$ build/glibc/unit frame
unit: 3 passed, 0 failed
$ build/glibc/unit -v nick str_c
ok   str_copy_fits
ok   str_copy_truncates
ok   str_chomp
ok   nick_valid
ok   nick_equal
unit: 5 passed, 0 failed
```

Both libraries, then the sanitizer build:

```console
$ make test-all 2>&1 | grep -E "^(make|unit)"
make --no-print-directory test LIBC=glibc
unit: 20 passed, 0 failed
make --no-print-directory test LIBC=musl
unit: 20 passed, 0 failed
$ make SANITIZE=1 test 2>&1 | tail -n 2
build/glibc-san/unit
unit: 20 passed, 0 failed
```

`make SANITIZE=1 test-all` runs the sanitizer build on both (ASan and UBSan with glibc, UBSan trap mode with musl, [Chapter 10](10-errors-and-debugging.md)); both report `unit: 20 passed, 0 failed`.

Now make sure the tests can fail. Break `nick_valid` so it allows 17-character nicknames:

```console
$ sed -i 's/len > NICK_MAX/len > NICK_MAX + 1/' src/nick.c && make test 2>&1 | tail -n 5; echo "exit ${PIPESTATUS[0]}"
build/glibc/unit
tests/test_nick.c:12: CHECK(!nick_valid("abcdefghijklmnopq"))
FAIL nick_valid
unit: 19 passed, 1 failed
make: *** [Makefile:59: test] Error 1
exit 2
```

One check, one line, the exact input. A test you've never seen fail may test nothing. Undo the change (`sed -i 's/len > NICK_MAX + 1/len > NICK_MAX/' src/nick.c`) and clean up:

```console
$ make clean >/dev/null && ls
Makefile
src
tests
```

The complete project at this point is in `project/ch11/`.

## Summary

- A unit test calls a function with known inputs and checks the results. Good tests check one behaviour, are deterministic, cover the edges and say what failed.
- `assert` stops at the first failure, shows no values, and vanishes with `-DNDEBUG`.
- A `CHECK` macro reports the code (`#cond`) and place (`__FILE__`, `__LINE__`), counts the failure and continues.
- Wrap multi-statement macros in `do { ... } while (0)`; evaluate each argument once, into a local.
- A runner exits with 0 only if all tests passed; `make` and scripts read that.
- An X-macro keeps one list of names and generates declarations and table. A listed name without a function won't link; an unlisted function never runs.
- Table-driven tests keep cases in an array of structs; print the row on failure.
- Stream code must give the same result however input is cut: feed it pieces of every small size.
- A shell script can test a whole program against a reference such as `wc`.
- Test with both libraries (`make test-all`). glibc's `qsort` keeps equal elements in order, musl's doesn't, C promises neither: add a tie-breaker.
- Test return values, `errno` and exit statuses, never message texts, `rand()` sequences or `NULL` printing.
- Run tests under sanitizers too: tests supply inputs, sanitizers spot undefined behaviour.

## Exercises

1. **CHECK_MEM.** Add `CHECK_MEM(a, b, n)` to `project/ch11/tests/test.h`. It compares `n` bytes that may include `'\0'`, and on failure prints the first differing byte, like `sb.data == "a\0c": byte 2: 0x62 != 0x63`. Use it in `test_strbuf_binary`.

<details><summary>Solution</summary>

A helper finds the first difference, declared in `test.h` and defined in `unit.c`, like `test_fail`. A `static` function in the header would trigger `-Wunused-function` (an error with `-Werror`) in every test file that doesn't use it.

```c
/* test.h, before #endif */

/* Index of the first byte where a and b differ, or n if they don't. */
size_t test_mem_diff(const void *a, const void *b, size_t n);

/* Compare n bytes; they may contain '\0'. */
#define CHECK_MEM(a, b, n)                                              \
    do {                                                                \
        const unsigned char *check_a_ = (const void *)(a);              \
        const unsigned char *check_b_ = (const void *)(b);              \
        size_t check_n_ = (n);                                          \
        size_t check_i_ = test_mem_diff(check_a_, check_b_, check_n_);  \
        if (check_i_ < check_n_)                                        \
            test_fail(__FILE__, __LINE__,                               \
                      "%s == %s: byte %zu: 0x%02x != 0x%02x",           \
                      #a, #b, check_i_, check_a_[check_i_],             \
                      check_b_[check_i_]);                              \
    } while (0)
```

```c
/* unit.c */
size_t test_mem_diff(const void *a, const void *b, size_t n)
{
    const unsigned char *x = a, *y = b;
    size_t i = 0;
    while (i < n && x[i] == y[i])
        i++;
    return i;
}
```

In `test_strbuf_binary`:

```c
    CHECK_MEM(sb.data, "a\0b", 4);     /* 4: the '\0' at the end too */
```

To see it fail, add a second check with `"a\0c"`:

```console
$ make test 2>&1 | tail -n 4
tests/test_strbuf.c:61: sb.data == "a\0c": byte 2: 0x62 != 0x63
FAIL strbuf_binary
unit: 19 passed, 1 failed
make: *** [Makefile:59: test] Error 1
```

`unsigned char` reaches `printf` as `int`, so `%02x` is correct. `#b` prints `"a\0c"`, escape and all. Remove the failing check afterwards.

</details>

2. **No test matched.** `build/glibc/unit nosuch` prints `unit: 0 passed, 0 failed` and exits with 0, so a typo in a test name looks like success. Make the runner print `unit: no test matches` and exit with 1 when no test ran.

<details><summary>Solution</summary>

In `main` in `unit.c`, before the summary line:

```c
    if (passed + failed == 0) {
        printf("unit: no test matches\n");
        return 1;
    }
    printf("unit: %d passed, %d failed\n", passed, failed);
```

```console
$ build/glibc/unit nosuch; echo "exit $?"
unit: no test matches
exit 1
```

With no arguments every test runs, so this triggers only on a bad pattern (or an empty list). A stricter version would check each pattern, failing `unit frame nosuch` too.

</details>

3. **An X-macro for mywc.** `examples/ch11/mywc/test_wc.c` lists its tests by hand. Give it a `TESTS(X)` list like the project's, so adding a test means writing the function and adding one name.

<details><summary>Solution</summary>

Replace the hand-written table with:

```c
#define TESTS(X) X(table) X(pieces)

#define TEST_DECLARE(name) static void test_##name(void);
TESTS(TEST_DECLARE)

#define TEST_ENTRY(name) { #name, test_##name },
static const struct test {
    const char *name;
    void (*run)(void);
} tests[] = { TESTS(TEST_ENTRY) };
```

In one file the functions can stay `static`, and the declarations are needed only if the table comes first. A bonus of `static`: an unlisted test function now gets gcc's `defined but not used [-Wunused-function]`. A listed name with no function gets `'test_x' used but never defined`, and the link fails.

</details>

4. **Random pieces.** `test_pieces` uses piece sizes 1 to 5 on fixed text. Add `test_random` to `test_wc.c`: 100 rounds of random text (up to 199 bytes of `a`, `b`, space, tab, `\n`, `\r`), counted once whole and once in random pieces of 1 to 8 bytes; the results must match. Take the seed from the environment variable `SEED` if set, else from `time(NULL)`, and print it on failure. Run it with both libraries. Can you repeat a musl failure with glibc?

<details><summary>Solution</summary>

Add `#include <stdlib.h>` and `#include <time.h>`, then:

```c
/* Random text, cut into random pieces, must give the same counts as the
 * whole text. SEED=n in the environment repeats a run (with the same libc:
 * glibc and musl have different rand() sequences). */
static void test_random(void)
{
    const char *env = getenv("SEED");
    unsigned seed = env ? (unsigned)strtoul(env, NULL, 10) : (unsigned)time(NULL);
    static const char alphabet[] = "ab \t\n\r";
    char text[200];
    int before = failed_checks;

    srand(seed);
    for (int round = 0; round < 100; round++) {
        size_t len = (size_t)rand() % sizeof text;
        for (size_t i = 0; i < len; i++)
            text[i] = alphabet[rand() % (sizeof alphabet - 1)];

        struct wc whole, pieces;
        wc_init(&whole);
        wc_feed(&whole, text, len);
        wc_init(&pieces);
        for (size_t pos = 0; pos < len; ) {
            size_t n = 1 + (size_t)rand() % 8;
            if (n > len - pos)
                n = len - pos;
            wc_feed(&pieces, text + pos, n);
            pos += n;
        }
        CHECK_INT(pieces.lines, whole.lines);
        CHECK_INT(pieces.words, whole.words);
        CHECK_INT(pieces.bytes, whole.bytes);
        if (failed_checks > before) {
            printf("    (SEED=%u, round %d)\n", seed, round);
            return;
        }
    }
}
```

Add `{ "random", test_random }` to the table (or `X(random)` after exercise 3). `sizeof alphabet - 1` leaves out the string's `'\0'`. The test stops at the first failing round, so one bug doesn't print 100 failures.

With the correct `wc.c` it passes on both libraries. With this chapter's buggy `wc_feed`, built with musl:

(The line number depends on where you put the function.)

```console
$ SEED=7 ./test_wc | grep -B1 -A1 SEED
test_wc.c:107: pieces.words == whole.words: 27 != 23
    (SEED=7, round 0)
FAIL random
```

`SEED=7` repeats that run with musl. With glibc it makes different text, because the `rand()` sequences differ; it will probably still fail (this bug is easy to hit), with other numbers. For runs that repeat on every library, write a small generator such as xorshift, as [Chapter 21](21-robustness.md) does for its fuzzer.

Keep the table too: random tests find surprises, tables document known cases.

</details>

5. **Where mywc and wc disagree.** Run `mywc` and `wc` on `printf 'a \001 b\n'` and `printf '\200\n'` (with `LC_ALL=C`). Explain the difference, add both inputs (and a piece of a binary such as `/bin/ls`) to `test.sh`, and change `wc_feed` until `test.sh` passes.

<details><summary>Solution</summary>

```console
$ printf 'a \001 b\n' | LC_ALL=C wc; printf 'a \001 b\n' | ./mywc
      1       2       6
1 3 6
$ printf '\200\n' | LC_ALL=C wc; printf '\200\n' | ./mywc
      1       0       2
1 1 2
```

GNU `wc` 8.32 starts a word only at a **printable** character. A control byte like `\001`, or a byte above 127 in the C locale, neither starts nor ends a word. `mywc` counts any non-space byte. The fix is one condition in `wc_feed`:

```c
        if (isspace(c)) {
            w->in_word = false;
        } else if (isprint(c) && !w->in_word) {
            w->in_word = true;       /* the first byte of a new word */
            w->words++;
        }
```

New lines for `test.sh`, before the `sources` line:

```sh
printf 'a \001 b\n'          > test.in; same control-byte
printf '\200\n'              > test.in; same high-byte
head -c 3000 /bin/ls         > test.in; same binary
```

```console
$ make test 2>&1 | tail -n 4
./test_wc
test_wc: 2 passed, 0 failed
./test.sh
test.sh: 9 passed, 0 failed
```

The unit tests still pass, as the table has no control bytes; add a row such as `{ "a \001 b\n", 1, 2, 6 }` to document the rule. Other `wc` programs have other rules: testing against a reference, you inherit its quirks, so write down which version you matched.

</details>

Sources for this chapter: [REFERENCES.md](REFERENCES.md#chapter-11--testing)
