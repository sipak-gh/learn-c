# Appendix A — C99 Cheat Sheet

This is a one-stop reference for the language the course uses: strict C99 (`-std=c99 -pedantic`). Every table links to the chapter that teaches the topic. The three complete examples are in `examples/appendix-a/`. Every snippet here compiles cleanly with both `gcc -std=c99 -Wall -Wextra -pedantic` (glibc) and `musl-gcc -std=c99 -Wall -Wextra -pedantic -static` (musl). The only exceptions are the ones that show a warning on purpose. For the POSIX functions (sockets, `poll`, signals, terminals, threads), see [Appendix D](appendix-d-posix-sockets-reference.md). For every glibc/musl difference, see [Appendix B](appendix-b-glibc-vs-musl.md).

## Program skeleton and build commands

```c
#define _POSIX_C_SOURCE 200809L   /* only if you use POSIX functions */
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char *argv[])   /* or: int main(void) */
{
    printf("%s got %d arguments\n", argv[0], argc - 1);
    return EXIT_SUCCESS;           /* 0; EXIT_FAILURE is 1 */
}
```

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -O2 -g -o prog prog.c
$ musl-gcc -std=c99 -Wall -Wextra -pedantic -O2 -g -static -o prog prog.c
```

| Flag | Meaning | Chapter |
|---|---|---|
| `-std=c99 -pedantic` | ISO C99, warn about every extension | [1](01-getting-started.md) |
| `-Wall -Wextra` | the useful warnings | [1](01-getting-started.md) |
| `-O2` / `-g` | optimize / debug info | [1](01-getting-started.md), [10](10-errors-and-debugging.md) |
| `-E` / `-S` / `-c` | stop after preprocessing / compiling / assembling | [1](01-getting-started.md) |
| `-D_POSIX_C_SOURCE=200809L` | show POSIX.1-2008 declarations | [9](09-preprocessor-and-building.md) |
| `-lm` (after the sources) | link the math library (needed on glibc only) | [2](02-types-and-operators.md), [9](09-preprocessor-and-building.md) |
| `-fsanitize=address,undefined` | ASan + UBSan (glibc builds) | [10](10-errors-and-debugging.md) |
| `-static` | static linking (the musl builds) | [22](22-shipping-and-libc-choices.md) |

`main` returns the exit status. Reaching the closing `}` of `main` returns 0 (C99 rule, `main` only).

## Types

Sizes on x86-64 Linux (the LP64 model). They are the same with glibc and musl, because they come from the platform ABI, not from the C library. Taught in [Chapter 2](02-types-and-operators.md).

| Type | Size | Range / note | Literal | `printf` |
|---|---|---|---|---|
| `_Bool` / `bool` | 1 | 0, 1 (`<stdbool.h>`) | `true`, `false` | `%d` |
| `char` | 1 | −128…127 here (signed on x86-64, unsigned on ARM) | `'A'` | `%c` |
| `signed char` / `unsigned char` | 1 | −128…127 / 0…255 | | `%hhd` / `%hhu` |
| `short` / `unsigned short` | 2 | −32768…32767 / 0…65535 | | `%hd` / `%hu` |
| `int` / `unsigned` | 4 | ±2.1·10⁹ / 0…4.29·10⁹ | `42`, `42u` | `%d` / `%u` |
| `long` / `unsigned long` | 8 | ±9.2·10¹⁸ | `42L`, `42UL` | `%ld` / `%lu` |
| `long long` / `unsigned long long` | 8 | at least 64 bits (new in C99) | `42LL`, `42ULL` | `%lld` / `%llu` |
| `float` | 4 | ~7 digits | `1.5f` | `%f` (promoted to double) |
| `double` | 8 | ~15 digits | `1.5`, `1e3`, `0x1p-3` | `%f`, `%g`, `%e` |
| `long double` | 16 | x87 80-bit, padded | `1.5L` | `%Lf` |
| `T *` | 8 | any object pointer | `NULL` | `%p` (cast to `void *`) |
| `size_t` | 8 | unsigned; result of `sizeof` (`<stddef.h>`) | | `%zu` |
| `ptrdiff_t` | 8 | signed; pointer difference | | `%td` |
| `ssize_t` (POSIX) | 8 | `size_t` or −1; returned by `read`/`write` | | `%zd` |
| `intmax_t` / `uintmax_t` | 8 | the widest integer types | | `%jd` / `%ju` |
| `int8_t`…`int64_t`, `uint8_t`…`uint64_t` | 1–8 | exact width (`<stdint.h>`) | `INT64_C(5)` | `PRId64`… (`<inttypes.h>`) |
| `time_t` (POSIX) | 8 | seconds since 1970 | | cast: `(long long)t` and `%lld` |

Limits are in `<limits.h>` (`INT_MAX`, `LONG_MIN`, `CHAR_BIT`, …) and `<stdint.h>` (`INT32_MAX`, `UINT64_MAX`, `SIZE_MAX`, …). Use `sizeof` instead of assuming a size.

### Literals

| Form | Example | Note |
|---|---|---|
| decimal, octal, hex | `42`, `052`, `0x2a` | a leading `0` means octal! |
| integer suffixes | `u`, `l`, `ll`, `ul`, `ull` (any case) | `ll` is C99 |
| floating | `3.0`, `3.`, `.5`, `1e-3`, `2.5f`, `2.5L` | no suffix means `double` |
| hex floating (C99) | `0x1p-3` (= 0.125) | exact binary value; `%a` prints it |
| character | `'a'`, `'\n'`, `'\0'`, `'\x41'`, `'\101'` | type is `int` in C |
| escapes | `\n \t \r \\ \' \" \? \a \b \f \v \0` | `\r\n` ends a parley line |
| string | `"abc"` (4 bytes with the `'\0'`) | read-only; `"ab" "c"` joins to `"abc"` |

## Declarations

| Keyword | Meaning | Chapter |
|---|---|---|
| `static` (file scope) | internal linkage: visible in this `.c` file only | [4](04-functions.md), [9](09-preprocessor-and-building.md) |
| `static` (block scope) | one object for the whole run, keeps its value | [8](08-memory.md) |
| `extern` | declared here, defined elsewhere | [9](09-preprocessor-and-building.md) |
| `auto`, `register` | old storage classes; never needed | — |
| `typedef` | a new name for a type | [7](07-structs-unions-enums.md) |
| `const` | the object must not be modified through this name | [2](02-types-and-operators.md), [5](05-pointers-and-arrays.md) |
| `volatile` | every access really happens (`volatile sig_atomic_t`) | [13](13-processes-and-signals.md) |
| `restrict` (C99) | the pointer is the only way to reach that object | [5](05-pointers-and-arrays.md) |
| `inline` (C99) | a hint to inline; write `static inline` | [4](04-functions.md) |

Reading declarators: start at the name, go right, then left; parentheses group.

```c
int *p[3];              /* array of 3 pointers to int */
int (*pa)[3];           /* pointer to an array of 3 ints */
int (*f)(int) = twice;  /* pointer to a function taking int, returning int */
const char *s = "abc";  /* pointer to const char: *s can't change */
char *const t = buf;    /* const pointer: t can't change */
int grid[2][3] = { { 1, 2, 3 }, { 4, 5, 6 } };
int zero[8] = { 0 };    /* all zero */
```

Structs, unions, enums and bit-fields ([Chapter 7](07-structs-unions-enums.md)):

```c
enum color { RED, GREEN = 5, BLUE };          /* 0, 5, 6 */
union number { int i; double d; };            /* members share memory: size 8 */
struct flags { unsigned ready : 1; unsigned mode : 3; };   /* bit-fields */
struct point { int x, y; };

struct point p = { .y = 2, .x = 1 };          /* designated initializer (C99) */
int days[12] = { [0] = 31, [1] = 28 };        /* the rest are 0 */
p = (struct point){ .x = 5 };                 /* compound literal (C99); y is 0 */
```

## Operators and precedence

From highest to lowest. Operators on one row have the same precedence. Taught in [Chapter 2](02-types-and-operators.md).

| # | Operators | Associativity |
|---|---|---|
| 1 | `a()` `a[]` `a.b` `a->b` `a++` `a--` `(type){list}` | left to right |
| 2 | `++a` `--a` `+a` `-a` `!` `~` `(type)` `*a` `&a` `sizeof` | right to left |
| 3 | `*` `/` `%` | left to right |
| 4 | `+` `-` | left to right |
| 5 | `<<` `>>` | left to right |
| 6 | `<` `<=` `>` `>=` | left to right |
| 7 | `==` `!=` | left to right |
| 8 | `&` | left to right |
| 9 | `^` | left to right |
| 10 | `\|` | left to right |
| 11 | `&&` | left to right |
| 12 | `\|\|` | left to right |
| 13 | `?:` | right to left |
| 14 | `=` `+=` `-=` `*=` `/=` `%=` `<<=` `>>=` `&=` `^=` `\|=` | right to left |
| 15 | `,` | left to right |

Two rules to remember:

- The bitwise operators `&`, `^` and `|` bind *looser* than `==`. Write `(flags & mask) == mask`.
- `&&`, `||`, `?:` and `,` evaluate their left operand first; `&&`, `||` and `?:` skip what they don't need. Elsewhere the order is unspecified, so `i = i++ + 1` and `f(i, i++)` are undefined behaviour.

`examples/appendix-a/precedence.c` spells out what the compiler reads:

```c
/* precedence.c - expressions that surprise because of precedence. */
#include <stdio.h>

int main(void)
{
    int flags = 6, mask = 2;
    int a[] = { 10, 20, 30 };
    int *p = a;

    /* == binds tighter than &, so "flags & mask == 2" means this: */
    printf("flags & (mask == 2) -> %d\n", flags & (mask == 2));
    printf("(flags & mask) == 2 -> %d\n", (flags & mask) == 2);

    /* + binds tighter than <<, so "1 << 2 + 1" means this: */
    printf("1 << (2 + 1)        -> %d\n", 1 << (2 + 1));

    /* postfix ++ binds tighter than unary *: *(p++) */
    int first = *p++;
    printf("*p++ gave %d, now *p is %d\n", first, *p);

    /* ?: is right associative: a ? b : (c ? d : e) */
    int n = 0;
    printf("n is %s\n", n < 0 ? "negative" : n == 0 ? "zero" : "positive");

    /* = is right associative and returns the value assigned */
    int x, y;
    x = y = 7;
    printf("x=%d y=%d\n", x, y);

    /* integer division truncates toward zero (C99); % keeps the sign of a */
    printf("-7 / 2 = %d, -7 %% 2 = %d\n", -7 / 2, -7 % 2);

    /* the comma operator: evaluate left, then right; value of the right */
    int c = (x++, x + 1);
    printf("c=%d\n", c);
    return 0;
}
```

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o precedence precedence.c
$ ./precedence
flags & (mask == 2) -> 0
(flags & mask) == 2 -> 1
1 << (2 + 1)        -> 8
*p++ gave 10, now *p is 20
n is zero
x=7 y=7
-7 / 2 = -3, -7 % 2 = -1
c=9
```

The musl build prints the same. `-Wall` catches the classic trap if you forget the parentheses:

```c
    if (flags & mask == 2)
        puts("set");
```

```text
trap.c:5:22: warning: suggest parentheses around comparison in operand of ‘&’ [-Wparentheses]
    5 |     if (flags & mask == 2)
      |                 ~~~~~^~~~
```

### Integer rules

| Rule | Example | Chapter |
|---|---|---|
| Integer promotion: types smaller than `int` become `int` in expressions | `char c = 'a'; c + 1` is an `int` | [2](02-types-and-operators.md) |
| Usual arithmetic conversions: mixed signed/unsigned of the same rank becomes unsigned | `-1 < 1u` is **0** (-1 becomes `UINT_MAX`); `-Wextra` warns: `comparison of integer expressions of different signedness` | [2](02-types-and-operators.md) |
| Unsigned arithmetic wraps modulo 2ⁿ | `UINT_MAX + 1u == 0` | [2](02-types-and-operators.md) |
| Signed overflow is undefined behaviour | `INT_MAX + 1` | [2](02-types-and-operators.md), [10](10-errors-and-debugging.md) |
| Shifting by ≥ the width, or a negative value left, is UB | `1 << 32` on a 32-bit `int` | [2](02-types-and-operators.md) |
| `/` truncates toward zero; `a % b` has the sign of `a` (C99 fixed this) | `-7 / 2 == -3`, `-7 % 2 == -1` | [2](02-types-and-operators.md) |
| Division by zero is UB (integers) | `n / 0` | [2](02-types-and-operators.md) |
| `sizeof` of an array is its size in bytes; inside a function a parameter "array" is a pointer | `sizeof a / sizeof a[0]` counts elements only for real arrays | [5](05-pointers-and-arrays.md) |

## Statements

```c
for (int i = 0; i < 3; i++) {     /* C99: i exists only in the loop */
    if (i == 1)
        continue;                  /* next iteration */
    printf("i=%d ", i);
}
int k = 0;
do k++; while (k < 3);             /* body runs at least once */
switch (k) {                       /* integer types only */
case 1:
case 2:                            /* several labels, one body */
    puts("small");
    break;                         /* without break, execution falls through */
case 3:
    puts("three");
    /* fall through */
default:
    puts("default");
}
```

`while (cond) body`, `if (...) ... else ...`, `break` (leave the innermost loop or `switch`), `return`, and `goto label;` (jump forward to a cleanup label in the same function) complete the list. See [Chapter 3](03-control-flow.md); the `goto cleanup` idiom is in [Chapter 10](10-errors-and-debugging.md).

## Functions

| Form | Meaning | Chapter |
|---|---|---|
| `int f(void);` | prototype: takes no arguments | [4](04-functions.md) |
| `int f();` | old style: arguments *unchecked*. Don't. | [4](04-functions.md) |
| `static int f(int n) { ... }` | private to this file | [4](04-functions.md) |
| `static inline int sq(int n) { return n * n; }` | small helper in a header | [4](04-functions.md) |
| `int printf(const char *fmt, ...);` | variadic | [4](04-functions.md) |
| `void (*handler)(int)` | function pointer | [5](05-pointers-and-arrays.md) |
| `__func__` (C99) | the current function's name, a `static const char[]` | [9](09-preprocessor-and-building.md) |

Arguments are always passed by value. To let a function change your variable, pass its address. A variadic function with `<stdarg.h>`:

```c
#include <stdarg.h>

/* sum of n ints passed as variadic arguments */
static int sum(int n, ...)
{
    va_list ap;
    va_start(ap, n);               /* n: the last named parameter */
    int total = 0;
    for (int i = 0; i < n; i++)
        total += va_arg(ap, int);  /* char/short arrive as int, float as double */
    va_end(ap);
    return total;
}
/* sum(3, 1, 2, 3) == 6 */
```

To pass the arguments on, use a `v` function: `vfprintf(stderr, fmt, ap)` (parley's `log_msg` does this). Use `va_copy` (C99) if you need to walk the list twice.

## The preprocessor

| Directive / macro | Use | Chapter |
|---|---|---|
| `#include <x.h>` / `"x.h"` | system header / your header (searched next to the file first) | [9](09-preprocessor-and-building.md) |
| `#define N 64` | object-like macro | [9](09-preprocessor-and-building.md) |
| `#define SQUARE(x) ((x) * (x))` | function-like macro: parenthesize every use of a parameter and the whole body | [9](09-preprocessor-and-building.md) |
| `#define SWAP(a, b) do { ... } while (0)` | several statements that act as one | [9](09-preprocessor-and-building.md) |
| `#x` / `a##b` | make a string / paste tokens: `STR(a+b)` is `"a+b"` | [9](09-preprocessor-and-building.md) |
| `#define LOG(fmt, ...) printf(fmt, __VA_ARGS__)` | variadic macro (C99) | [9](09-preprocessor-and-building.md) |
| `#if` `#ifdef` `#ifndef` `#elif` `#else` `#endif` `defined(X)` | conditional compilation | [9](09-preprocessor-and-building.md) |
| `#ifndef X_H` / `#define X_H` / `#endif` | include guard | [9](09-preprocessor-and-building.md) |
| `#undef`, `#error "msg"`, `#pragma`, `_Pragma("...")` (C99) | remove a macro, stop the build, compiler hints | [9](09-preprocessor-and-building.md) |
| `__FILE__` `__LINE__` `__DATE__` `__TIME__` | where and when | [9](09-preprocessor-and-building.md), [11](11-testing.md) |
| `__STDC__` `__STDC_VERSION__` | `1`; `199901L` under `-std=c99` | [1](01-getting-started.md) |
| `NDEBUG` | turns `assert()` off | [10](10-errors-and-debugging.md) |
| `_POSIX_C_SOURCE` `_XOPEN_SOURCE` | feature test macros: define before any `#include` | [9](09-preprocessor-and-building.md) |
| X-macros | one list, expanded several ways (parley's command table) | [9](09-preprocessor-and-building.md) |

A macro argument is evaluated once per use: `SQUARE(i++)` increments twice (undefined behaviour). Use a `static inline` function when you can.

## printf format specifiers

A conversion is `%[flags][width][.precision][length]conversion`. Taught in [Chapter 2](02-types-and-operators.md) and [Chapter 6](06-strings.md).

| Conversion | Argument | Prints |
|---|---|---|
| `%d` `%i` | `int` | signed decimal |
| `%u` `%o` `%x` `%X` | `unsigned` | decimal, octal, hex, HEX |
| `%c` | `int` | one character |
| `%s` | `char *` | a string up to `'\0'`; **never pass NULL** |
| `%f` `%F` (`%F` is C99) | `double` | `123.456000` |
| `%e` `%E` | `double` | `1.234560e+02` |
| `%g` `%G` | `double` | `%f` or `%e`, whichever is shorter |
| `%a` `%A` (C99) | `double` | hex float `0x1p-1` |
| `%p` | `void *` | an address; the format differs by libc |
| `%n` | `int *` | stores the count so far; avoid it |
| `%%` | — | a `%` |

| Flag / field | Meaning | Example → output |
|---|---|---|
| `-` | left-justify | `[%-5d]` → `[-42  ]` |
| `+` | always a sign | `[%+d]` → `[+42]` |
| space | a space before positive numbers | `[% d]` → `[ 42]` |
| `0` | pad with zeros | `[%05d]` → `[-0042]` |
| `#` | alternate form | `[%#x]` → `[0x2a]` |
| width | minimum width | `[%5d]` → `[  -42]` |
| `.prec` | digits after the point / max chars of a string | `%.2f`, `%.3s` |
| `*` | width or precision from an `int` argument | `printf("%.*s", 2, "hello")` → `he` |

| Length | With `d`/`i` | With `u`/`o`/`x` | Note |
|---|---|---|---|
| `hh` (C99) | `signed char` | `unsigned char` | |
| `h` | `short` | `unsigned short` | |
| `l` | `long` | `unsigned long` | `%lf` = `%f` in printf; `%lf` is **required** for `double` in `scanf` |
| `ll` (C99) | `long long` | `unsigned long long` | |
| `j` (C99) | `intmax_t` | `uintmax_t` | |
| `z` (C99) | `ssize_t` | `size_t` | `%zu` for `sizeof` |
| `t` (C99) | `ptrdiff_t` | — | |
| `L` | `long double` with `f e g a` | | |

`<inttypes.h>` (C99) has string macros for the fixed-width types. Paste them into the format: `printf("%" PRIu64 "\n", x)`.

| Type | Signed | Unsigned | Hex |
|---|---|---|---|
| `int8_t`…`int64_t` | `PRId8` `PRId16` `PRId32` `PRId64` | `PRIu8` … `PRIu64` | `PRIx8` … `PRIx64` (`PRIX64` uppercase) |
| `intmax_t` | `PRIdMAX` | `PRIuMAX` | `PRIxMAX` |
| `intptr_t` | `PRIdPTR` | `PRIuPTR` | `PRIxPTR` |
| `scanf` | `SCNd64` … | `SCNu64` … | `SCNx64` … |

`snprintf(buf, size, ...)` never writes more than `size` bytes (including the `'\0'`). It returns the length the whole output *would* have had, so `n >= size` means it was cut. `examples/appendix-a/formats.c` shows each row:

```c
/* formats.c - printf conversions, one line each. */
#include <inttypes.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

int main(int argc, char *argv[])
{
    int i = -42;
    unsigned u = 42;
    long l = LONG_MAX;
    long long ll = LLONG_MIN;
    size_t z = sizeof(long double);
    ptrdiff_t d = -3;
    intmax_t j = INTMAX_MAX;
    uint64_t u64 = UINT64_MAX;
    int32_t i32 = INT32_MIN;
    uint8_t byte = 200;
    double x = 3.14159;
    char buf[8];
    const char *text = argc > 1 ? argv[1] : "a long string";

    printf("[%d] [%i] [%u]\n", i, i, u);
    printf("[%5d] [%-5d] [%05d] [%+d]\n", i, i, i, 42);
    printf("[%o] [%x] [%X] [%#x]\n", u, u, u, u);
    printf("[%ld] [%lld]\n", l, ll);
    printf("[%zu] [%td] [%jd]\n", z, d, j);
    printf("[%" PRIu64 "] [%" PRId32 "] [%" PRIx64 "]\n", u64, i32, u64);
    printf("[%hhu] [%c] [%%]\n", byte, 'A');
    printf("[%f] [%.2f] [%8.3f] [%e] [%g] [%a]\n", x, x, x, x, x, 0.5);
    printf("[%s] [%.3s] [%8s] [%-8s]\n", "hello", "hello", "hi", "hi");
    printf("[%*d] [%.*s]\n", 6, i, 2, "hello");

    int n = snprintf(buf, sizeof buf, "%s", text);
    printf("snprintf wanted %d bytes, buf holds \"%s\"\n", n, buf);

    printf("pointer: %p\n", (void *)&i);       /* the address varies */
    printf("null pointer: %p\n", (void *)NULL);
    return 0;
}
```

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o formats formats.c
$ ./formats
[-42] [-42] [42]
[  -42] [-42  ] [-0042] [+42]
[52] [2a] [2A] [0x2a]
[9223372036854775807] [-9223372036854775808]
[16] [-3] [9223372036854775807]
[18446744073709551615] [-2147483648] [ffffffffffffffff]
[200] [A] [%]
[3.141590] [3.14] [   3.142] [3.141590e+00] [3.14159] [0x1p-1]
[hello] [hel] [      hi] [hi      ]
[   -42] [he]
snprintf wanted 13 bytes, buf holds "a long "
pointer: 0x7ffd4cbfd698
null pointer: (nil)
```

The address varies from run to run. `text` comes from `argv` so that gcc can't see at compile time that the output gets cut; with a literal string, `-Wall` warns `‘%s’ directive output truncated` (`-Wformat-truncation`).

### glibc vs musl: printf

Built with `musl-gcc -std=c99 -Wall -Wextra -pedantic -static`, every line is the same except the last:

```console
$ ./formats
...
pointer: 0x7ffd8275b8a0
null pointer: 0
```

| Case | glibc | musl | Advice |
|---|---|---|---|
| `%p` of a null pointer | `(nil)` | `0` | Don't print null pointers, or don't depend on the text. |
| `%s` of NULL | `(null)` | `(null)` | Undefined behaviour. gcc may turn `printf("%s\n", s)` into `puts(s)`, which crashes on both. Write `s ? s : "(none)"`. |
| `%m` (GNU: `strerror(errno)`) | works | works | `-pedantic` warns: `ISO C does not support the '%m' gnu_printf format`. Use `strerror(errno)`. |
| `-lm` for `sqrt`, `pow`, … | required | not needed (libm is in libc) | Always add `-lm` after the sources. Then the build works on both. |

Neither libc is "better" here. The portable code never depends on either behaviour.

## Standard headers

C99 has 24 headers. These are the ones you will use, and what they give you.

| Header | Main contents | Chapter |
|---|---|---|
| `<stdio.h>` | `printf` family, `snprintf`, `FILE *`, `fopen`, `fgets`, `fputs`, `fflush`, `setvbuf`, `perror`, `EOF` | [1](01-getting-started.md), [12](12-files-and-descriptors.md) |
| `<stdlib.h>` | `malloc` `calloc` `realloc` `free`, `exit`, `atexit`, `EXIT_SUCCESS`, `strtol` `strtoul` `strtod`, `qsort`, `bsearch`, `abs`, `rand`, `getenv` | [2](02-types-and-operators.md), [8](08-memory.md) |
| `<string.h>` | `strlen` `strcmp` `strncmp` `strchr` `strrchr` `strstr` `memcpy` `memmove` `memset` `memcmp` `memchr` `strerror` | [6](06-strings.md) |
| `<ctype.h>` | `isalpha` `isdigit` `isalnum` `isspace` `isprint` `tolower` `toupper` (pass `unsigned char` values) | [6](06-strings.md) |
| `<stddef.h>` | `size_t`, `ptrdiff_t`, `NULL`, `offsetof` | [5](05-pointers-and-arrays.md), [7](07-structs-unions-enums.md) |
| `<stdint.h>` (C99) | `int32_t` `uint64_t` `intptr_t` `intmax_t`, `INT32_MAX` `SIZE_MAX` | [2](02-types-and-operators.md) |
| `<inttypes.h>` (C99) | `PRId64`… format macros, `strtoimax`; includes `<stdint.h>` | [2](02-types-and-operators.md) |
| `<stdbool.h>` (C99) | `bool`, `true`, `false` | [2](02-types-and-operators.md) |
| `<limits.h>` | `INT_MAX` `LONG_MIN` `CHAR_BIT` `UCHAR_MAX` | [2](02-types-and-operators.md) |
| `<float.h>` | `DBL_MAX` `DBL_EPSILON` `FLT_DIG` | [2](02-types-and-operators.md) |
| `<math.h>` | `sqrt` `pow` `floor` `fabs` `isnan` `INFINITY` (link with `-lm`) | [2](02-types-and-operators.md) |
| `<errno.h>` | `errno`, `EINTR` `ERANGE` `EDOM`, … | [10](10-errors-and-debugging.md) |
| `<assert.h>` | `assert(expr)` | [10](10-errors-and-debugging.md) |
| `<stdarg.h>` | `va_list` `va_start` `va_arg` `va_end` `va_copy` (C99) | [4](04-functions.md) |
| `<signal.h>` | `signal`, `raise`, `sig_atomic_t`, `SIGINT`… (POSIX adds `sigaction`) | [13](13-processes-and-signals.md) |
| `<time.h>` | `time`, `clock`, `difftime`, `strftime`, `localtime`, `struct tm` (POSIX adds `localtime_r`, `clock_gettime`) | [19](19-an-interactive-client.md) |
| `<setjmp.h>` | `setjmp`, `longjmp` (not used in the course) | — |
| `<locale.h>` | `setlocale` | [20](20-terminal-ui.md), [22](22-shipping-and-libc-choices.md) |
| `<iso646.h>` | `and`, `or`, `not` spellings | — |
| `<complex.h>` `<fenv.h>` `<tgmath.h>` (C99) | complex numbers, floating-point environment, type-generic math | — |
| `<wchar.h>` `<wctype.h>` | wide characters; `wcwidth` (XSI) gives display columns. parley counts UTF-8 bytes itself | [20](20-terminal-ui.md) |

The POSIX headers the course uses (`<unistd.h>`, `<fcntl.h>`, `<sys/socket.h>`, `<netdb.h>`, `<poll.h>`, `<pthread.h>`, `<termios.h>`, …) are listed in [Appendix D](appendix-d-posix-sockets-reference.md). They need `#define _POSIX_C_SOURCE 200809L` before the first `#include` (or `-D_POSIX_C_SOURCE=200809L`). Under plain `-std=c99`, both libcs hide `getline`, `strdup`, `fileno`, `sigaction`, `localtime_r` and `clock_gettime`. glibc also hides `getaddrinfo` and `getopt`, which musl shows anyway. Taught in [Chapter 9](09-preprocessor-and-building.md).

## Common undefined behaviour

The compiler may assume these never happen. A program that does them can do anything, including "work". See [Chapter 10](10-errors-and-debugging.md).

| Undefined behaviour | Typical cause | Tool that finds it |
|---|---|---|
| Out-of-bounds array access | off-by-one loops, missing `'\0'` room | ASan |
| Using freed memory; freeing twice | dangling pointers | ASan |
| Reading an uninitialized local | missing `= 0` | `-Wall` (sometimes), Valgrind |
| Signed integer overflow | `INT_MAX + 1` | UBSan |
| Null pointer dereference | unchecked `malloc`, `strchr` result | crash, UBSan |
| Shift ≥ width or of a negative value | `1 << 32` | UBSan |
| Modifying a string literal | `char *s = "x"; s[0] = 'y';` | crash (read-only memory) |
| Unsequenced modifications | `i = i++;`, `f(i++, i++)` | `-Wall` (`-Wsequence-point`) |
| Wrong `printf` argument type | `%d` with a `long` | `-Wall` (`-Wformat`; Ubuntu's gcc warns even without it) |
| `printf("%s", NULL)` | an unset string | crash (see above) |
| Overlapping `memcpy` | `memcpy(p, p + 1, n)` | ASan; use `memmove` |
| Negative `char` to `<ctype.h>` | `isalpha(c)` with `c` < 0 | cast: `isalpha((unsigned char)c)` |
| Returning the address of a local | `return buf;` for a local array | gcc warns even without `-Wall` (`-Wreturn-local-addr`) |

## C89, C99 and C11 at a glance

The course is C99: it adds what C89 programmers missed most, and it is supported everywhere. C11 and later are not used. Their features are listed so you recognize them (see [Chapter 22](22-shipping-and-libc-choices.md)).

| Feature | C89 | C99 | C11 |
|---|---|---|---|
| `//` comments | no | yes | yes |
| Declarations anywhere in a block; `for (int i = 0; ...)` | no | yes | yes |
| `_Bool`, `<stdbool.h>` | no | yes | yes |
| `long long`, `%lld`, `<stdint.h>`, `<inttypes.h>` | no | yes | yes |
| Designated initializers, compound literals | no | yes | yes |
| Flexible array members (`char text[];`) | no | yes | yes |
| Variable length arrays (`int a[n];`) | no | yes, required | optional (`__STDC_NO_VLA__`) |
| `inline`, `restrict` | no | yes | yes |
| `__func__`, variadic macros, `_Pragma` | no | yes | yes |
| `snprintf`, `vsnprintf`, `va_copy` | no | yes | yes |
| Hex float literals, `%a` | no | yes | yes |
| `%zu` `%td` `%jd` `%hhd` | no | yes | yes |
| Implicit `int`, implicit function declarations | allowed | removed (gcc only warns) | removed |
| `main` returns 0 at the end | no | yes | yes |
| Integer `/` truncates toward zero | either way | defined | defined |
| `_Static_assert` | no | no | yes |
| `_Generic` | no | no | yes |
| Anonymous structs and unions | no | no | yes |
| `_Alignas`, `_Alignof` (`<stdalign.h>`) | no | no | yes |
| `_Noreturn` (`<stdnoreturn.h>`) | no | no | yes |
| `_Thread_local`; `<threads.h>`, `<stdatomic.h>` (both optional) | no | no | yes |
| `quick_exit`, `aligned_alloc`, `timespec_get` | no | no | yes |
| `gets` | yes | yes (deprecated in 2007) | removed |

With `-std=c99 -pedantic`, gcc warns when you use a C11 feature. With `-std=c99`, an implicit function declaration is a *warning* in gcc 11 (`implicit declaration of function`). Treat it as an error: the program may link and then crash ([Chapter 9](09-preprocessor-and-building.md)).

### Every C99 addition in one program

`examples/appendix-a/c99.c`:

```c
/* c99.c - the features C99 added to C89, in one program. */
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Variadic macro: __VA_ARGS__ is everything after fmt. */
#define LOG(fmt, ...) printf("%s:%d: " fmt "\n", __func__, __LINE__, __VA_ARGS__)

struct point {
    int x, y;
};

/* Flexible array member: the last member has no size. */
struct line {
    size_t len;
    char text[];
};

/* static inline: a hint to inline; static avoids C99's extern-inline rules. */
static inline int square(int n)
{
    return n * n;
}

/* restrict: a promise that dst and src never overlap. */
static void copy(int *restrict dst, const int *restrict src, size_t n)
{
    for (size_t i = 0; i < n; i++)      /* declaration in a for loop */
        dst[i] = src[i];
}

static int sum(size_t n, const int a[n])   /* VLA parameter syntax */
{
    int total = 0;
    for (size_t i = 0; i < n; i++)
        total += a[i];
    return total;
}

int main(void)
{
    // A // comment (C99).
    bool ok = true;                                   /* <stdbool.h> */
    long long big = 9000000000LL;                     /* long long */
    uint32_t mask = UINT32_C(0xff00ff00);             /* <stdint.h> */
    double eighth = 0x1p-3;                           /* hex float */
    printf("ok=%d big=%lld mask=%" PRIx32 " eighth=%g\n", ok, big, mask, eighth);

    struct point p = { .y = 2, .x = 1 };              /* designated */
    int days[12] = { [0] = 31, [1] = 28, [11] = 31 }; /* the rest are 0 */
    printf("p=(%d,%d) days[1]=%d days[5]=%d\n", p.x, p.y, days[1], days[5]);

    p = (struct point){ .x = 5 };                     /* compound literal */
    int total = sum(3, (int[]){ 1, 2, 3 });
    printf("p=(%d,%d) total=%d square=%d\n", p.x, p.y, total, square(7));

    size_t n = 4;
    int vla[n];                                       /* variable length array */
    int src[4] = { 4, 3, 2, 1 };
    copy(vla, src, n);
    printf("sizeof vla=%zu vla[0]=%d\n", sizeof vla, vla[0]);

    const char *word = "hello";                       /* declarations anywhere */
    struct line *ln = malloc(sizeof *ln + strlen(word) + 1);
    if (ln == NULL)
        return 1;
    ln->len = strlen(word);
    memcpy(ln->text, word, ln->len + 1);
    LOG("line \"%s\" has %zu bytes", ln->text, ln->len);
    free(ln);

    printf("__STDC_VERSION__=%ldL\n", __STDC_VERSION__);
    return 0;
}
```

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o c99 c99.c
$ ./c99
ok=1 big=9000000000 mask=ff00ff00 eighth=0.125
p=(1,2) days[1]=28 days[5]=0
p=(5,0) total=6 square=49
sizeof vla=16 vla[0]=4
main:72: line "hello" has 5 bytes
__STDC_VERSION__=199901L
$ musl-gcc -std=c99 -Wall -Wextra -pedantic -static -o c99 c99.c
$ ./c99
ok=1 big=9000000000 mask=ff00ff00 eighth=0.125
p=(1,2) days[1]=28 days[5]=0
p=(5,0) total=6 square=49
sizeof vla=16 vla[0]=4
main:72: line "hello" has 5 bytes
__STDC_VERSION__=199901L
```

The VLA is here only to show the syntax. The course avoids VLAs, because a large `n` overflows the stack with no error you can check ([Chapter 5](05-pointers-and-arrays.md)). musl's thread stacks are only 128 KiB, so the risk is higher there ([Chapter 17](17-threads.md)).

## Summary

- Compile with `-std=c99 -Wall -Wextra -pedantic`. Add `-D_POSIX_C_SOURCE=200809L` for POSIX functions and `-lm` (after the sources) for math.
- Type sizes come from the platform ABI, so they are the same on glibc and musl. Use `<stdint.h>` types when the size matters, `size_t` for sizes, and `%zu` to print them.
- `&`, `^` and `|` bind looser than `==`. Parenthesize bit tests and let `-Wall` catch the rest.
- Mixed signed/unsigned comparisons convert to unsigned (`-1 < 1u` is false). Signed overflow is undefined; unsigned wraps.
- Print fixed-width integers with the `<inttypes.h>` macros (`PRIu64`). `snprintf` returns the length it *wanted*.
- glibc prints a null `%p` as `(nil)`, musl as `0`. `%s` with NULL is undefined on both.
- C99 gave you `//`, declarations anywhere, `bool`, `long long`, designated initializers, compound literals, flexible array members, `inline`, `restrict`, variadic macros and `snprintf`. `_Static_assert`, `_Generic` and `<threads.h>` are C11 and are not used here.

Sources for this appendix: [REFERENCES.md](REFERENCES.md#appendix-a--c99-cheat-sheet)
