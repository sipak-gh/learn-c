# Chapter 3 — Control Flow

How C chooses and repeats: `if`, `switch`, three loops, `break`, `continue`, and `goto` (including where
it's the right tool). You'll write `mywc`, a small Unix `wc`; both project programs learn to read and
validate their options. Builds on [Chapter 2](02-types-and-operators.md); [Chapter 4](04-functions.md)
splits programs into functions.

## Expressions and statements

- An **expression** computes a value: `x + 1`, `x = 5`, `printf("hi\n")`, `a > b`.
- A **statement** does something: an expression plus `;`, a declaration, a block (`{ ... }`), `if`,
  `switch`, a loop, `break`, `continue`, `goto` or `return`.

A **block** groups statements into one and opens a **scope**: names declared inside exist only until its
`}`.

`examples/ch03/statements.c`

```c
/* statements.c - expressions, statements, blocks and scope. */
#include <stdio.h>

int main(void)
{
    int x = 1;                  /* a declaration */
    x = x + 1;                  /* an expression statement: expression + ; */
    printf("x = %d\n", x);      /* a function call is an expression too */
    ;                           /* the null statement: does nothing */

    {                           /* a block (compound statement) opens a scope */
        int x = 100;            /* a new x that hides the outer one */
        int y = x + 1;
        printf("inner x = %d, y = %d\n", x, y);
    }                           /* the inner x and y end here */

    printf("outer x = %d\n", x);
    return 0;
}
```

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o statements statements.c
$ ./statements
x = 2
inner x = 100, y = 101
outer x = 2
```

- An inner name can hide (**shadow**) an outer one: legal but confusing; avoid it. `-Wshadow` (not in
  `-Wall` or `-Wextra`) warns.
- Keep scopes small: fewer lines can change a variable, and readers know where to look.
- Computing a value and throwing it away is almost always a bug; `-Wall` says so:

```console
misc.c:10:7: warning: statement with no effect [-Wunused-value]
   10 |     x + 1;
      |     ~~^~~
```

## `if` and `else`

```c
if (condition)
    statement
else
    statement
```

The condition is any scalar expression: zero is false, all else true
([Chapter 2](02-types-and-operators.md#booleans)). Each branch is one statement; use a block for more.

`examples/ch03/ports.c`

```c
/* ports.c - if / else if / else: classify a TCP port number. */
#include <stdio.h>

int main(void)
{
    long port;

    if (scanf("%ld", &port) != 1) {
        fprintf(stderr, "ports: expected a number\n");
        return 1;
    }

    if (port < 1 || port > 65535) {
        printf("%ld: not a valid port\n", port);
        return 1;
    } else if (port < 1024) {
        printf("%ld: well-known port (binding it needs root)\n", port);
    } else if (port < 49152) {
        printf("%ld: registered port\n", port);
    } else {
        printf("%ld: dynamic port (used for outgoing connections)\n", port);
    }

    if (port == 6667)
        printf("that's the traditional IRC port\n");
    return 0;
}
```

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o ports ports.c
$ echo 80 | ./ports
80: well-known port (binding it needs root)
$ echo 6667 | ./ports
6667: registered port
that's the traditional IRC port
$ echo 50000 | ./ports
50000: dynamic port (used for outgoing connections)
$ echo 0 | ./ports
0: not a valid port
$ echo abc | ./ports
ports: expected a number
```

- `else if` is just an `else` whose statement is another `if`; a chain runs the first matching branch.
- **Handle errors first and return early**: the rest can assume good input, with less nesting. Every
  chapter does this.

### Gotcha: `=` instead of `==`

`if (x = 0)` assigns 0 to `x`; the condition is that value, so it's always false. `-Wall` warns:

```console
misc.c:6:9: warning: suggest parentheses around assignment used as truth value [-Wparentheses]
    6 |     if (x = 0)
      |         ^
```

To assign and test on purpose, add parentheses, `if ((x = f()))`, to tell gcc and readers, as in the
`while ((c = getchar()) != EOF)` loop below.

### Gotcha: a stray semicolon

```c
    if (x == 1);
        printf("one?\n");
```

The `;` is the null statement, the `if`'s whole body, so the `printf` always runs. gcc spots it twice:

```console
misc.c:8:16: warning: suggest braces around empty body in an 'if' statement [-Wempty-body]
    8 |     if (x == 1);
      |                ^
misc.c:8:5: warning: this 'if' clause does not guard... [-Wmisleading-indentation]
    8 |     if (x == 1);
      |     ^~
misc.c:9:9: note: ...this statement, but the latter is misleadingly indented as if it were guarded by the 'if'
    9 |         printf("one?\n");
      |         ^~~~~~
```

### Gotcha: the dangling `else`

An `else` belongs to the nearest `if` without an `else`, whatever the indentation says:

```c
    int a = 1, b = 0;
    if (a)
        if (b)
            printf("a and b\n");
    else
        printf("not a?\n");
```

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o dangle dangle.c
dangle.c: In function 'main':
dangle.c:6:8: warning: suggest explicit braces to avoid ambiguous 'else' [-Wdangling-else]
    6 |     if (a)
      |        ^
$ ./dangle
not a?
```

The `else` belongs to `if (b)`, so "not a?" printed though `a` is 1. Brace nested `if`s. Many projects
always brace; this course omits braces only for one-line bodies.

## `switch`

`switch` picks a branch by integer value: clearer than `else if` chains comparing one variable with
constants, and compilable to a jump table.

`examples/ch03/days.c`

```c
/* days.c - switch: the number of days in a month. */
#include <stdio.h>

int main(void)
{
    int month;

    if (scanf("%d", &month) != 1) {
        fprintf(stderr, "days: expected a month number\n");
        return 1;
    }

    int days;
    switch (month) {
    case 2:
        days = 28;                       /* ignoring leap years */
        break;
    case 4: case 6: case 9: case 11:     /* several labels, one action */
        days = 30;
        break;
    case 1: case 3: case 5: case 7: case 8: case 10: case 12:
        days = 31;
        break;
    default:                             /* anything else */
        fprintf(stderr, "days: %d is not a month\n", month);
        return 1;
    }
    printf("month %d has %d days\n", month, days);
    return 0;
}
```

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o days days.c
$ echo 2 | ./days
month 2 has 28 days
$ echo 9 | ./days
month 9 has 30 days
$ echo 7 | ./days
month 7 has 31 days
$ echo 13 | ./days; echo $?
days: 13 is not a month
1
```

- `switch` evaluates an integer expression (including `char` and `enum`) and **jumps** to the matching
  `case` label, else to `default`; without a `default`, nothing runs.
- `case` labels are just labels: execution runs on through later labels until `break` (or `return`).
- Each `case` value must be a distinct integer **constant expression**: a literal, an `enum` constant, a
  character like `'a'`, or arithmetic on those.
- Always write a `default`, if only to report an error, for values you didn't think of.

### Gotcha: fall-through

Without `break`, execution "falls through" into the next case: sometimes wanted, often a bug. `-Wextra`
enables `-Wimplicit-fallthrough`, flagging every unmarked fall-through:

`examples/ch03/fallthrough.c`

```c
/* fallthrough.c - a forgotten break, and an intended fall-through. */
#include <stdio.h>

int main(void)
{
    int level = 1;

    switch (level) {
    case 0:
        printf("debug\n");
    case 1:
        printf("info\n");                /* oops: no break */
    case 2:
        printf("warning\n");
        break;
    }

    int stage = 1;
    switch (stage) {                     /* the countdown falls through on purpose */
    case 1:
        printf("connect\n");
        /* fall through */
    case 2:
        printf("register nick\n");
        /* fall through */
    case 3:
        printf("join channel\n");
        break;
    }
    return 0;
}
```

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o fallthrough fallthrough.c
fallthrough.c: In function 'main':
fallthrough.c:10:9: warning: this statement may fall through [-Wimplicit-fallthrough=]
   10 |         printf("debug\n");
      |         ^~~~~~~~~~~~~~~~~
fallthrough.c:11:5: note: here
   11 |     case 1:
      |     ^~~~
fallthrough.c:12:9: warning: this statement may fall through [-Wimplicit-fallthrough=]
   12 |         printf("info\n");                /* oops: no break */
      |         ^~~~~~~~~~~~~~~~
fallthrough.c:13:5: note: here
   13 |     case 2:
      |     ^~~~
$ ./fallthrough
info
warning
connect
register nick
join channel
```

- With `level` 1, the first switch printed "info" *and* "warning".
- The second falls through on purpose: a `/* fall through */` comment just before the next label tells
  gcc and the reader, silencing the warning. C23 adds a `[[fallthrough]]` attribute; C99 uses the
  comment.
- Consecutive labels with no statements between (`case 4: case 6:`) never warn: nothing falls through.

### Gotcha: `case` needs a constant

A `const` variable isn't a constant expression in C
([Chapter 2](02-types-and-operators.md#gotcha-const-isnt-a-constant)):

```c
    const int one = 1;
    int x = 5;
    switch (x) {
    case one:
```

```console
caseConst.c:6:5: error: case label does not reduce to an integer constant
    6 |     case one:
      |     ^~~~
```

Use an `enum` constant instead.

### Gotcha: declarations after a label

A label must be followed by a statement, and in C99 a declaration isn't one:

```c
    switch (x) {
    case 1:
        int y = 2;
```

```console
caseDecl.c:6:9: warning: a label can only be part of a statement and a declaration is not a statement [-Wpedantic]
    6 |         int y = 2;
      |         ^~~
```

gcc accepts it as an extension (only a `-pedantic` warning); other compilers reject it. Use a block,
`case 1: { int y = 2; ... break; }`, which also limits `y` to that case, as the project does below.

## Loops

All three loops run their body (one statement, usually a block) while a condition is true.

| Loop | Tests the condition | Use it when |
|---|---|---|
| `while (cond) body` | before each round; may run zero times | you don't know how many rounds |
| `do body while (cond);` | after each round; runs at least once | the body must run before you can test |
| `for (init; cond; step) body` | before each round | counting, or any loop with a setup and a step |

`examples/ch03/loops.c`

```c
/* loops.c - while, do-while and for; break and continue. */
#include <stdio.h>

int main(void)
{
    unsigned int n = 6667, digits = 0;
    while (n > 0) {                      /* test first: may run zero times */
        n /= 10;
        digits++;
    }
    printf("6667 has %u digits\n", digits);

    int tries = 0;
    do {                                 /* test last: runs at least once */
        tries++;
    } while (tries < 0);
    printf("do-while ran %d time(s)\n", tries);

    for (int i = 1; i <= 5; i++)         /* i exists only inside the loop */
        printf("%d squared is %d\n", i, i * i);

    for (int i = 1; i <= 20; i++) {
        if (i % 2 == 0)
            continue;                    /* skip the rest of this round */
        if (i > 9)
            break;                       /* leave the loop */
        printf("%d ", i);
    }
    printf("\n");

    for (;;) {                           /* no condition: loop forever */
        printf("once\n");
        break;
    }
    return 0;
}
```

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o loops loops.c
$ ./loops
6667 has 4 digits
do-while ran 1 time(s)
1 squared is 1
2 squared is 4
3 squared is 9
4 squared is 16
5 squared is 25
1 3 5 7 9 
once
```

- `for (init; cond; step) body` equals `{ init; while (cond) { body step; } }`, except `continue` still
  runs `step`.
- **C99 loop-scoped variables:** `for (int i = 0; ...)` gives each loop above its own `i`, which doesn't
  leak out. C89 didn't allow it:

```console
$ gcc -std=c89 -Wall -Wextra -pedantic -c c89for.c
c89for.c: In function 'main':
c89for.c:4:5: error: 'for' loop initial declarations are only allowed in C99 or C11 mode
    4 |     for (int i = 0; i < 3; i++)
      |     ^~~
c89for.c:4:5: note: use option '-std=c99', '-std=gnu99', '-std=c11' or '-std=gnu11' to compile your code
```

- `break` leaves the innermost loop (or `switch`); `continue` starts the innermost loop's next round.
- `for (;;)` and `while (1)` loop until `break` or `return`; servers are built around one
  ([Chapter 15](15-a-first-server.md)). `do ... while (cond);` ends with a semicolon.

### Gotcha: `break` inside a `switch` inside a loop

Inside a `switch`, `break` leaves the `switch`, not the surrounding loop. To stop the loop from there,
use `return`, a flag the loop tests, or `goto` (below).

### Example: rolling dice with `do`/`while`

"Roll until you get a six" fits `do`/`while`: you must roll before you can test. `rand()` from
`<stdlib.h>` returns a pseudo-random number from 0 to `RAND_MAX`; `srand(seed)` sets the starting point.
The same seed gives the same sequence, making runs repeatable.

`examples/ch03/dice.c`

```c
/* dice.c - do-while: roll a die until it shows a six. */
#include <stdio.h>
#include <stdlib.h>

int main(void)
{
    unsigned int seed;

    if (scanf("%u", &seed) != 1) {
        fprintf(stderr, "dice: expected a seed\n");
        return 1;
    }
    srand(seed);                         /* the same seed gives the same rolls */

    int roll, rolls = 0;
    do {
        roll = rand() % 6 + 1;           /* 1..6 */
        rolls++;
        printf("%d ", roll);
    } while (roll != 6);
    printf("\nsix after %d rolls\n", rolls);
    return 0;
}
```

`roll` is declared before the loop because the `while` condition, after the body's `}`, must see it.

### glibc vs musl: the same seed, different dice

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o dice dice.c
$ echo 1 | ./dice
2 5 4 2 6 
six after 5 rolls
$ echo 42 | ./dice
1 1 6 
six after 3 rolls
$ musl-gcc -std=c99 -Wall -Wextra -pedantic -static -o dice-musl dice.c
$ echo 1 | ./dice-musl
1 3 6 
six after 3 rolls
$ echo 42 | ./dice-musl
3 2 5 1 3 2 5 3 1 3 5 4 3 6 
six after 14 rolls
```

The standard defines what `rand` and `srand` do, not the algorithm, so a seed replays a run only on the
same library. Neither is better; just never write tests expecting particular `rand()` values. (`rand()`
suits games and tests, never secrets: it's predictable.) [Chapter 21](21-robustness.md) returns to this
when fuzzing the server.

## `goto`

`goto label;` jumps to `label:` anywhere in the same function. Edsger Dijkstra's 1968 "Go To Statement
Considered Harmful" argued jumps make programs hard to follow: "The go to statement as it stands is just
too primitive, it is too much an invitation to make a mess of one's program." Structured
`if`/`switch`/loops rightly replaced most of them; C keeps `goto` where it's clearest. First, leaving
nested loops:

`examples/ch03/search.c`

```c
/* search.c - goto: leave two nested loops at once. */
#include <stdio.h>

int main(void)
{
    /* Find two numbers from 1..20 whose sum is 13 and product is 42. */
    for (int a = 1; a <= 20; a++) {
        for (int b = a; b <= 20; b++) {
            if (a + b == 13 && a * b == 42) {
                printf("found %d and %d\n", a, b);
                goto done;               /* break would leave only the inner loop */
            }
        }
    }
    printf("not found\n");
done:
    printf("search finished\n");
    return 0;
}
```

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o search search.c
$ ./search
found 6 and 7
search finished
```

Second, and most important, **error handling with cleanup**. A function holding several resources
(memory, files, sockets) must release them on every error path; copying release code to every `return`
means one copy is eventually wrong. Instead, every error jumps forward to one cleanup section at the end:

```c
    /* A sketch: Chapters 8, 10 and 12 fill in the details. */
    int status = -1;
    char *buf = malloc(4096);
    if (buf == NULL)
        goto out;
    FILE *f = fopen(path, "r");
    if (f == NULL)
        goto out_free;
    /* ... use buf and f; on error: goto out_close; ... */
    status = 0;
out_close:
    fclose(f);
out_free:
    free(buf);
out:
    return status;
```

Labels run in reverse order of acquisition, so each path releases exactly what was acquired. The Linux
kernel's coding style recommends this ("centralized exiting of functions"). To keep `goto` readable:

- jump **forward** only, to a label near the end of the same function;
- name labels for what happens there (`out_free`, `usage_error`);
- never jump into a block, past a declaration, or backwards to make a loop.

[Chapter 10](10-errors-and-debugging.md) uses this idiom for real; the project below uses a simpler form,
one exit for all command-line errors.

## The comma operator

The comma operator evaluates and discards its left operand, then evaluates its right operand as the
result. It has the lowest precedence. Mainly it fits two things where C allows one expression, like a
`for` header:

`examples/ch03/comma.c`

```c
/* comma.c - the comma operator in a for loop: two variables, two steps. */
#include <stdio.h>

int main(void)
{
    /* Print 1..9 from both ends towards the middle. */
    for (int lo = 1, hi = 9; lo < hi; lo++, hi--)
        printf("%d-%d ", lo, hi);
    printf("\n");

    int a, b;
    a = (b = 3, b * 2);                  /* evaluate b = 3, then b * 2 */
    printf("a = %d, b = %d\n", a, b);
    return 0;
}
```

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o comma comma.c
$ ./comma
1-9 2-8 3-7 4-6 
a = 6, b = 3
```

- In `int lo = 1, hi = 9` the comma separates declarations, and between function arguments it separates
  arguments; neither is the operator. In `lo++, hi--` it is.
- The comma is a sequence point: the left side finishes before the right starts.
- Avoid it outside `for` headers: `a = (b = 3, b * 2)` is legal, but two statements are clearer.

### Gotcha: a stray comma

`=` binds tighter than `,`, so `x = 1, 2;` means `(x = 1), 2;`. gcc warns about the useless `2`:

```console
misc.c:11:10: warning: right-hand operand of comma expression has no effect [-Wunused-value]
   11 |     x = 1, 2;
      |          ^
```

## Putting it together: `mywc`

Unix `wc` counts lines, words and bytes. This version reads standard input a character at a time with
`getchar()` (`<stdio.h>`), using a loop, a `switch` with deliberate fall-through, and an `if`.

`examples/ch03/mywc.c`

```c
/* mywc.c - count lines, words and bytes on standard input, like wc. */
#include <stdbool.h>
#include <stdio.h>

int main(void)
{
    long lines = 0, words = 0, bytes = 0;
    bool in_word = false;
    int c;                               /* int, not char: EOF is not a char */

    while ((c = getchar()) != EOF) {
        bytes++;
        switch (c) {
        case '\n':
            lines++;                     /* a newline also ends a word */
            /* fall through */
        case ' ':
        case '\t':
        case '\r':
        case '\v':
        case '\f':
            in_word = false;
            break;
        default:
            if (!in_word) {
                in_word = true;
                words++;
            }
            break;
        }
    }
    printf("%7ld %7ld %7ld\n", lines, words, bytes);
    return 0;
}
```

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o mywc mywc.c
$ ./mywc < mywc.c
     34     109     840
$ wc < mywc.c
 34 109 840
$ printf 'hello  world\nbye' | ./mywc
      1       3      16
$ printf 'hello  world\nbye' | wc
      1       3      16
$ ./mywc < /dev/null
      0       0       0
```

- `getchar()` returns the next byte (0–255) as an `int`, or `EOF` (−1) at end of input. **`c` must be
  an `int`**: in a signed `char`, byte 255 and `EOF` look the same; in an unsigned `char` (as on ARM),
  `EOF` never matches.
- `while ((c = getchar()) != EOF)`, the standard C input loop, reads, stores and tests at once; the extra
  parentheses are needed because `!=` binds tighter than `=`.
- `< mywc.c` redirects the file to standard input; `< /dev/null` gives empty input.
- A **word** starts at a non-space after a space (or the start). The `in_word` flag tracks which side of
  a boundary we're on: a tiny **state machine**, a pattern reused for the chat protocol.
- The last line, `bye`, has no newline, so it isn't counted: `wc` counts newlines. Both agree.
- `musl-gcc` builds give the same counts: both libraries read bytes the same way.

## glibc vs musl: a preview of `getopt`

POSIX provides an option parser, `getopt`. The project uses its own loop for now and switches to
`getopt` in [Chapter 22](22-shipping-and-libc-choices.md). The classic `getopt` loop, a `while` around a
`switch`, is readable already, apart from a few names explained later:

`examples/ch03/getopt-order.c`

```c
/* getopt-order.c - does getopt() find options after a non-option word?
 * A preview of Chapter 22, where the project switches to getopt. */
#define _POSIX_C_SOURCE 200809L          /* ask for POSIX functions (Chapter 9) */
#include <stdio.h>
#include <unistd.h>                      /* getopt, optarg, optind */

int main(int argc, char *argv[])
{
    const char *port = "6667";
    int verbose = 0;
    int opt;

    while ((opt = getopt(argc, argv, "p:v")) != -1) {
        switch (opt) {
        case 'p':
            port = optarg;
            break;
        case 'v':
            verbose = 1;
            break;
        default:
            fprintf(stderr, "usage: %s [-p PORT] [-v] [ARGS...]\n", argv[0]);
            return 2;
        }
    }
    printf("port=%s verbose=%d\n", port, verbose);
    for (int i = optind; i < argc; i++)
        printf("operand: %s\n", argv[i]);
    return 0;
}
```

`"p:v"` lists the letters (`:` means `-p` takes a value). Each call returns the next option letter, or
−1 when none are left.

POSIX says parsing **stops at the first word that isn't an option**. GNU's `getopt` "permutes" instead,
finding options anywhere on the command line.

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o getopt-order getopt-order.c
$ ./getopt-order -v -p 7000 extra
port=7000 verbose=1
operand: extra
$ ./getopt-order -v extra -p 7000
port=6667 verbose=1
operand: extra
operand: -p
operand: 7000
$ gcc -std=c99 -Wall -Wextra -pedantic -D_GNU_SOURCE -o getopt-order-gnu getopt-order.c
$ ./getopt-order-gnu -v extra -p 7000
port=7000 verbose=1
operand: extra
$ musl-gcc -std=c99 -Wall -Wextra -pedantic -static -D_GNU_SOURCE -o getopt-order-musl getopt-order.c
$ ./getopt-order-musl -v extra -p 7000
port=6667 verbose=1
operand: extra
operand: -p
operand: 7000
```

- With POSIX settings both libraries stop at `extra`, so `-p 7000` becomes two operands.
- With `-D_GNU_SOURCE` (GNU extensions on; [Chapter 9](09-preprocessor-and-building.md)), glibc permutes
  and finds `-p 7000`. musl never permutes.

The error messages differ too:

```console
$ ./getopt-order -x
./getopt-order: invalid option -- 'x'
usage: ./getopt-order [-p PORT] [-v] [ARGS...]
$ ./getopt-order-musl -x
./getopt-order-musl: unrecognized option: x
usage: ./getopt-order-musl [-p PORT] [-v] [ARGS...]
```

**musl is simpler here**: one behaviour, POSIX's, whatever macros you define. glibc's permutation helps
users, but the same source behaves differently depending on a macro. Put options before operands and it
works the same everywhere.

## Building parley: command-line options

Both programs now read their options with a hand-written loop:

- `parleyd [-p PORT] [-b ADDR] [-m MAXCLIENTS] [-v]`, plus `-h`/`--help` and `--version`;
- `parley [-h HOST] [-p PORT] [-n NICK]`, plus `--help` and `--version`. Here `-h` means *host*, as in
  IRC clients, so help is only `--help`.

A mistake prints a message and "Try 'parleyd --help' for more information.", then exits with status 2.
Otherwise the programs say what they *would* do; networking comes in Part II.

New tools, briefly:

- `argv[i]` is the i-th command-line word; `argc` counts the words, including the program name in
  `argv[0]` ([Chapter 4](04-functions.md)).
- `arg[0]`, `arg[1]` are the first and second characters of the word `arg`; every string ends with
  `'\0'` ([Chapter 6](06-strings.md)). So `arg[0] == '-' && arg[1] != '\0' && arg[2] == '\0'` means
  "`arg` is `-` plus one character".
- `strcmp(a, b) == 0` (`<string.h>`) means "strings `a` and `b` are equal"; `==` doesn't compare text
  ([Chapter 6](06-strings.md) explains why).
- `NULL` means "no string yet" ([Chapter 5](05-pointers-and-arrays.md)).

Replace both files.

`parleyd.c`

```c
/* parleyd.c - the parley chat server (Chapter 3: command-line options). */
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    DEFAULT_PORT = 6667,
    PORT_MAX = 65535,
    DEFAULT_MAX_CLIENTS = 64,
    MAX_CLIENTS_MAX = 1000
};

int main(int argc, char *argv[])
{
    const char *addr = "127.0.0.1";
    long port = DEFAULT_PORT;
    long max_clients = DEFAULT_MAX_CLIENTS;
    bool verbose = false;

    /* argv[0] is the program name; options start at argv[1].
     * strcmp(a, b) == 0 means "a and b are the same string" (Chapter 6). */
    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];

        if (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0) {
            printf("usage: parleyd [-p PORT] [-b ADDR] [-m MAXCLIENTS] [-v]\n"
                   "  -p PORT        TCP port to listen on (default %d)\n"
                   "  -b ADDR        address to listen on (default 127.0.0.1)\n"
                   "  -m MAXCLIENTS  most clients at once, 1-%d (default %d)\n"
                   "  -v             verbose (debug) logging\n"
                   "  -h, --help     show this help and exit\n"
                   "  --version      show the version and exit\n",
                   DEFAULT_PORT, MAX_CLIENTS_MAX, DEFAULT_MAX_CLIENTS);
            return 0;
        }
        if (strcmp(arg, "--version") == 0) {
            printf("parleyd 0.1\n");
            return 0;
        }

        /* Every other option is exactly two characters: '-' and a letter. */
        if (arg[0] != '-' || arg[1] == '\0' || arg[2] != '\0') {
            fprintf(stderr, "parleyd: unexpected argument '%s'\n", arg);
            goto usage_error;
        }

        char opt = arg[1];
        const char *value = NULL;
        if (opt == 'p' || opt == 'b' || opt == 'm') {
            if (i + 1 >= argc) {
                fprintf(stderr, "parleyd: option -%c needs a value\n", opt);
                goto usage_error;
            }
            value = argv[++i];  /* consume the next word as the value */
        }

        switch (opt) {
        case 'b':
            addr = value;
            break;
        case 'p':   /* -p and -m share the number parsing below */
        case 'm': {
            long min = 1;
            long max = opt == 'p' ? PORT_MAX : MAX_CLIENTS_MAX;
            char *end;
            errno = 0;
            long n = strtol(value, &end, 10);
            if (errno != 0 || end == value || *end != '\0' || n < min || n > max) {
                fprintf(stderr, "parleyd: invalid value '%s' for -%c (expected %ld-%ld)\n",
                        value, opt, min, max);
                goto usage_error;
            }
            if (opt == 'p')
                port = n;
            else
                max_clients = n;
            break;
        }
        case 'v':
            verbose = true;
            break;
        default:
            fprintf(stderr, "parleyd: unknown option '%s'\n", arg);
            goto usage_error;
        }
    }

    printf("parleyd: would listen on %s:%ld (max %ld clients%s)\n",
           addr, port, max_clients, verbose ? ", verbose" : "");
    return 0;

usage_error:
    /* One exit path for every command-line mistake: exit status 2 is the
     * Unix convention for "you used me wrong". */
    fprintf(stderr, "Try 'parleyd --help' for more information.\n");
    return 2;
}
```

How the control flow fits together:

- **`for (int i = 1; i < argc; i++)`** visits each word after the program name; `i` lives only in the
  loop.
- **`if` with early `return`** handles `--help` and `--version` first: print and stop.
- **`argv[++i]`** takes the *next* word as the value and moves `i` past it, so `6603` isn't treated as an
  option; the `i + 1 >= argc` test first checks that word exists.
- **`switch (opt)`** dispatches on the letter. `case 'p':` and `case 'm':` are adjacent, so they share
  the number parsing without a warning, in a **block** (`{ ... }`) because it declares variables.
  `default` catches unknown letters.
- **`goto usage_error`** sends all four kinds of mistake to one place, which prints the hint and returns
  2: the cleanup idiom with little to clean up. [Chapter 4](04-functions.md) replaces it with a function.
- The help text is adjacent string literals over several lines, which C joins
  ([Chapter 2](02-types-and-operators.md#exact-width-types-stdinth-and-inttypesh)).
- The port is now a `long`, range-checked and printed with `%ld`; the `uint16_t` returns when the server
  opens a real socket.

`parley.c`

```c
/* parley.c - the parley chat client (Chapter 3: command-line options). */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { DEFAULT_PORT = 6667, PORT_MAX = 65535 };

int main(int argc, char *argv[])
{
    const char *host = "localhost";
    long port = DEFAULT_PORT;
    const char *nick = "guest";

    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];

        /* -h means "host" here, so help is only --help. */
        if (strcmp(arg, "--help") == 0) {
            printf("usage: parley [-h HOST] [-p PORT] [-n NICK]\n"
                   "  -h HOST    server to connect to (default localhost)\n"
                   "  -p PORT    server port (default %d)\n"
                   "  -n NICK    your nickname (default guest)\n"
                   "  --help     show this help and exit\n"
                   "  --version  show the version and exit\n",
                   DEFAULT_PORT);
            return 0;
        }
        if (strcmp(arg, "--version") == 0) {
            printf("parley 0.1\n");
            return 0;
        }
        if (arg[0] != '-' || arg[1] == '\0' || arg[2] != '\0') {
            fprintf(stderr, "parley: unexpected argument '%s'\n", arg);
            goto usage_error;
        }

        char opt = arg[1];
        if (opt != 'h' && opt != 'p' && opt != 'n') {
            fprintf(stderr, "parley: unknown option '%s'\n", arg);
            goto usage_error;
        }
        if (i + 1 >= argc) {
            fprintf(stderr, "parley: option -%c needs a value\n", opt);
            goto usage_error;
        }
        const char *value = argv[++i];

        switch (opt) {
        case 'h':
            host = value;
            break;
        case 'n':
            nick = value;
            break;
        case 'p': {
            char *end;
            errno = 0;
            long n = strtol(value, &end, 10);
            if (errno != 0 || end == value || *end != '\0' || n < 1 || n > PORT_MAX) {
                fprintf(stderr, "parley: invalid port '%s' (expected 1-%d)\n",
                        value, PORT_MAX);
                goto usage_error;
            }
            port = n;
            break;
        }
        }
    }

    printf("parley: would connect to %s:%ld as %s\n", host, port, nick);
    return 0;

usage_error:
    fprintf(stderr, "Try 'parley --help' for more information.\n");
    return 2;
}
```

All three client options take a value, so it checks the letter *before* the `switch`; the `switch` needs
no `default`, since the `if` already rejected every other letter.

### Checkpoint

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o parleyd parleyd.c
$ gcc -std=c99 -Wall -Wextra -pedantic -o parley parley.c
$ musl-gcc -std=c99 -Wall -Wextra -pedantic -static -o parleyd-musl parleyd.c
$ musl-gcc -std=c99 -Wall -Wextra -pedantic -static -o parley-musl parley.c
$ ./parleyd
parleyd: would listen on 127.0.0.1:6667 (max 64 clients)
$ ./parleyd -p 6603 -m 10 -v
parleyd: would listen on 127.0.0.1:6603 (max 10 clients, verbose)
$ ./parleyd --help
usage: parleyd [-p PORT] [-b ADDR] [-m MAXCLIENTS] [-v]
  -p PORT        TCP port to listen on (default 6667)
  -b ADDR        address to listen on (default 127.0.0.1)
  -m MAXCLIENTS  most clients at once, 1-1000 (default 64)
  -v             verbose (debug) logging
  -h, --help     show this help and exit
  --version      show the version and exit
$ ./parleyd --version
parleyd 0.1
$ ./parleyd -p; echo $?
parleyd: option -p needs a value
Try 'parleyd --help' for more information.
2
$ ./parleyd -x; echo $?
parleyd: unknown option '-x'
Try 'parleyd --help' for more information.
2
$ ./parleyd -m 0; echo $?
parleyd: invalid value '0' for -m (expected 1-1000)
Try 'parleyd --help' for more information.
2
$ ./parleyd extra; echo $?
parleyd: unexpected argument 'extra'
Try 'parleyd --help' for more information.
2
$ ./parley -n alice -p 6603
parley: would connect to localhost:6603 as alice
$ ./parley --help
usage: parley [-h HOST] [-p PORT] [-n NICK]
  -h HOST    server to connect to (default localhost)
  -p PORT    server port (default 6667)
  -n NICK    your nickname (default guest)
  --help     show this help and exit
  --version  show the version and exit
$ ./parley -n; echo $?
parley: option -n needs a value
Try 'parley --help' for more information.
2
$ ./parley -h; echo $?
parley: option -h needs a value
Try 'parley --help' for more information.
2
$ ./parleyd-musl -p 6603 -m 10 -v
parleyd: would listen on 127.0.0.1:6603 (max 10 clients, verbose)
$ ./parley-musl -n alice -p 6603
parley: would connect to localhost:6603 as alice
$ ./parleyd-musl -m 5000; echo $?
parleyd: invalid value '5000' for -m (expected 1-1000)
Try 'parleyd --help' for more information.
2
```

Identical on both libraries: `strcmp`, `strtol` and `printf` behave the same. Unlike `getopt` with
`_GNU_SOURCE`, our loop never reorders words, so `parleyd extra -v` is an error on both.

The complete project at this point is in `project/ch03/`.

## Summary

- Expression + `;` = statement. A block `{ }` is one statement and a scope.
- `if` chains test in order; handle errors first, return early. Watch `=` vs `==`, stray semicolons, the
  dangling `else`; brace nested `if`s.
- `switch` jumps to a constant `case` and runs until `break`. Mark fall-through with `/* fall through */`;
  always have `default`; put declarations in a block.
- `while` tests first, `do`/`while` last, `for` packs setup, test and step; `for (int i = ...)` scopes
  `i` to the loop.
- `break` leaves the innermost loop or `switch`; `continue` starts the next round.
- `goto` only forward: out of nested loops, or to shared cleanup or an error exit.
- Comma operator: `for` headers only.
- Read input with `int c; while ((c = getchar()) != EOF)`; `c` must be an `int`.
- glibc vs musl: same seed, different `rand()` sequences; glibc's `getopt` permutes under `_GNU_SOURCE`,
  musl's never does.

## Exercises

1. Write `bits.c`: read an `unsigned int` and count its one-bits two ways: test the lowest bit and shift
   right until 0; then use `v &= v - 1`, which clears the lowest set bit. Print each count and its loop
   rounds. Which needs fewer?

<details><summary>Solution</summary>

```c
/* bits.c - count the one-bits in a number, two ways. */
#include <stdio.h>

int main(void)
{
    unsigned int x;

    if (scanf("%u", &x) != 1) {
        fprintf(stderr, "bits: expected a number\n");
        return 1;
    }

    int count = 0, rounds = 0;
    for (unsigned int v = x; v != 0; v >>= 1) {   /* look at every bit */
        count += v & 1;
        rounds++;
    }
    printf("shift loop: %d bits set, %d rounds\n", count, rounds);

    count = 0;
    for (unsigned int v = x; v != 0; v &= v - 1)  /* clear the lowest set bit */
        count++;
    printf("v &= v - 1: %d bits set, %d rounds\n", count, count);
    return 0;
}
```

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o bits bits.c
$ echo 6667 | ./bits
shift loop: 6 bits set, 13 rounds
v &= v - 1: 6 bits set, 6 rounds
$ echo 2147483648 | ./bits
shift loop: 1 bits set, 32 rounds
v &= v - 1: 1 bits set, 1 rounds
$ echo 0 | ./bits
shift loop: 0 bits set, 0 rounds
v &= v - 1: 0 bits set, 0 rounds
```

The shift loop runs once per bit up to the highest one-bit; `v &= v - 1` runs once per one-bit
(subtracting 1 flips the lowest one-bit and the zeros below it, and `&` clears them). `unsigned int` keeps
the shifts well defined.

</details>

2. Write `collatz.c`: read a positive `long` n. While n isn't 1, halve it if even, else make it 3n + 1.
   Print the number of steps and the highest value reached. Reject n < 1 (why must you?).

<details><summary>Solution</summary>

```c
/* collatz.c - how many steps until n reaches 1? */
#include <stdio.h>

int main(void)
{
    long n;

    if (scanf("%ld", &n) != 1 || n < 1) {
        fprintf(stderr, "collatz: expected a positive number\n");
        return 1;
    }

    long steps = 0, peak = n;
    while (n != 1) {
        if (n % 2 == 0)
            n /= 2;
        else
            n = 3 * n + 1;
        if (n > peak)
            peak = n;
        steps++;
    }
    printf("%ld steps, highest value %ld\n", steps, peak);
    return 0;
}
```

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o collatz collatz.c
$ echo 27 | ./collatz
111 steps, highest value 9232
$ echo 1 | ./collatz
0 steps, highest value 1
$ echo 0 | ./collatz; echo $?
collatz: expected a positive number
1
```

0 stays 0 forever (0 is even, 0 / 2 is 0); negatives never reach 1 either. `||` short-circuits, so
`n < 1` is tested only after `scanf` succeeded. (Huge inputs could overflow `3 * n + 1`; a careful
version checks against `LONG_MAX` first, as in [Chapter 2](02-types-and-operators.md)'s exercise 4.)

</details>

3. Extend `mywc.c` into `longest.c`, which also prints the longest line's length (without the newline),
   like `wc -L`. Remember a last line without a newline.

<details><summary>Solution</summary>

```c
/* longest.c - mywc plus the length of the longest line. */
#include <stdbool.h>
#include <stdio.h>

int main(void)
{
    long lines = 0, words = 0, bytes = 0;
    long length = 0, longest = 0;        /* current and longest line length */
    bool in_word = false;
    int c;

    while ((c = getchar()) != EOF) {
        bytes++;
        if (c == '\n') {
            if (length > longest)
                longest = length;
            length = 0;
        } else {
            length++;
        }
        switch (c) {
        case '\n':
            lines++;
            /* fall through */
        case ' ':
        case '\t':
        case '\r':
        case '\v':
        case '\f':
            in_word = false;
            break;
        default:
            if (!in_word) {
                in_word = true;
                words++;
            }
            break;
        }
    }
    if (length > longest)                /* a last line without '\n' */
        longest = length;
    printf("%7ld %7ld %7ld %7ld\n", lines, words, bytes, longest);
    return 0;
}
```

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o longest longest.c
$ ./longest < mywc.c
     34     109     840      79
$ wc -L < mywc.c
79
$ printf 'hello  world\nbye' | ./longest
      1       3      16      12
```

The check after the loop handles a last line without `'\n'`. GNU `wc -L` measures display width, counting
a tab as up to 8 columns, so results can differ for files with tabs; `mywc.c` has none.

</details>

4. Write `doubles.c`: read a seed, then roll two dice until they match. Print each pair and the number of
   rolls. Run it with seeds 1 and 7 on both libraries.

<details><summary>Solution</summary>

```c
/* doubles.c - roll two dice until they show the same number. */
#include <stdio.h>
#include <stdlib.h>

int main(void)
{
    unsigned int seed;

    if (scanf("%u", &seed) != 1) {
        fprintf(stderr, "doubles: expected a seed\n");
        return 1;
    }
    srand(seed);

    int a, b, rolls = 0;
    do {
        a = rand() % 6 + 1;
        b = rand() % 6 + 1;
        rolls++;
        printf("(%d,%d) ", a, b);
    } while (a != b);
    printf("\ndoubles after %d rolls\n", rolls);
    return 0;
}
```

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o doubles doubles.c
$ musl-gcc -std=c99 -Wall -Wextra -pedantic -static -o doubles-musl doubles.c
$ echo 1 | ./doubles
(2,5) (4,2) (6,2) (5,1) (4,2) (3,2) (3,2) (6,5) (1,1) 
doubles after 9 rolls
$ echo 1 | ./doubles-musl
(1,3) (6,6) 
doubles after 2 rolls
$ echo 7 | ./doubles
(4,4) 
doubles after 1 rolls
$ echo 7 | ./doubles-musl
(6,5) (1,4) (2,3) (4,3) (4,1) (2,5) (6,5) (2,5) (5,1) (1,3) (2,3) (6,6) 
doubles after 12 rolls
```

`do`/`while` fits: you must roll before comparing. Each library repeats its own sequence per seed, but
the sequences differ. `a` and `b` are declared before the loop so the condition sees them.

</details>

5. Rewrite `search.c` without `goto`, using a `bool found` flag that both loops test. Which version is
   easier to read?

<details><summary>Solution</summary>

```c
/* search2.c - search.c without goto: a flag ends both loops. */
#include <stdbool.h>
#include <stdio.h>

int main(void)
{
    bool found = false;

    for (int a = 1; a <= 20 && !found; a++) {
        for (int b = a; b <= 20 && !found; b++) {
            if (a + b == 13 && a * b == 42) {
                printf("found %d and %d\n", a, b);
                found = true;
            }
        }
    }
    if (!found)
        printf("not found\n");
    printf("search finished\n");
    return 0;
}
```

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o search2 search2.c
$ ./search2
found 6 and 7
search finished
```

The flag version needs an extra variable, an extra test in both loops, and an `if` afterwards; `goto`
says "stop searching" in one word. Both are fine. [Chapter 4](04-functions.md) often offers a cleaner
third option: a function that `return`s from inside the loops.

</details>

Sources for this chapter: [REFERENCES.md](REFERENCES.md#chapter-3--control-flow)
