# Chapter 2 — Types and Operators

A variable is named memory; its type gives its size and what its bits mean. This chapter covers integer,
character, floating-point and boolean types, named constants, every operator, and the surprises:
overflow, hidden conversions, precedence. You'll use `printf` and `scanf` and meet the first real
glibc/musl difference, the math library. `parleyd` learns to read a port number safely. Builds on
[Chapter 1](01-getting-started.md); [Chapter 3](03-control-flow.md) adds control flow.

## Variables and declarations

A declaration gives a name a type: `int nicks = 3;`. C99 made this easier: declare variables where you
first need them (not only at the top of a block); `//` comments run to the end of the line (`/* ... */`
still works and can span lines); and `long long` is a new integer type of at least 64 bits.

`examples/ch02/decls.c`

```c
/* decls.c - C99 declarations: where you need them, with // comments. */
#include <stdio.h>

int main(void)
{
    int nicks = 3;                    // a // comment runs to the end of the line
    printf("nicks: %d\n", nicks);

    int channels = nicks * 2;         // declared after a statement: fine in C99
    printf("channels: %d\n", channels);

    long long bytes = 6000000000LL;   // long long is new in C99 too
    printf("bytes: %lld\n", bytes);

    int a = 7, b = 2;                 /* several variables in one declaration */
    double ratio = (double)a / b;
    printf("ratio: %f\n", ratio);
    return 0;
}
```

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o decls decls.c
$ ./decls
nicks: 3
channels: 6
bytes: 6000000000
ratio: 3.500000
```

Part of the output under C89 (`-std=c89`) shows what C99 added:

```console
$ gcc -std=c89 -Wall -Wextra -pedantic -o decls decls.c
decls.c: In function 'main':
decls.c:6:39: error: C++ style comments are not allowed in ISO C90
    6 |     int nicks = 3;                    // a // comment runs to the end of the line
      |                                       ^
decls.c:6:39: note: (this will be reported only once per input file)
decls.c:9:5: warning: ISO C90 forbids mixed declarations and code [-Wdeclaration-after-statement]
    9 |     int channels = nicks * 2;         // declared after a statement: fine in C99
      |     ^~~
decls.c:12:10: warning: ISO C90 does not support 'long long' [-Wlong-long]
   12 |     long long bytes = 6000000000LL;   // long long is new in C99 too
      |          ^~~~
```

Declaring at first use keeps meaning close by and uninitialized variables rare.

### Gotcha: uninitialized variables

An uninitialized local holds whatever was in that memory. Reading it is a bug; gcc usually catches it:

```c
    int total;
    total += 5;
```

```console
uninit.c:6:11: warning: 'total' is used uninitialized [-Wuninitialized]
    6 |     total += 5;
      |     ~~~~~~^~~~
```

Initialize variables when you declare them, if you can.

### Gotcha: implicit int is gone

C89 let you omit the type, meaning `int`. C99 removed that rule; gcc still accepts it with a warning:

```c
main(void)
{
    static count = 3;
```

```console
implicit.c:3:1: warning: return type defaults to 'int' [-Wimplicit-int]
    3 | main(void)
      | ^~~~
implicit.c: In function 'main':
implicit.c:5:12: warning: type defaults to 'int' in declaration of 'count' [-Wimplicit-int]
    5 |     static count = 3;
      |            ^~~~~
```

Always write the type.

## Integer types

C has five standard signed integer types (`signed char`, `short`, `int`, `long`, `long long`), each with
a same-sized `unsigned` partner without negative values. The standard sets only minimum ranges; the
platform picks real sizes, so C suits 16-bit microcontrollers and 64-bit servers alike: your code says
"an int", each machine uses its natural size. `<limits.h>` gives the ranges, `sizeof` the sizes:

`examples/ch02/limits.c`

```c
/* limits.c - the size and range of each integer type on this machine. */
#include <limits.h>
#include <stdio.h>

int main(void)
{
    printf("CHAR_BIT  %d\n", CHAR_BIT);
    printf("char      %zu byte   %d .. %d\n", sizeof(char), CHAR_MIN, CHAR_MAX);
    printf("short     %zu bytes  %d .. %d\n", sizeof(short), SHRT_MIN, SHRT_MAX);
    printf("int       %zu bytes  %d .. %d\n", sizeof(int), INT_MIN, INT_MAX);
    printf("unsigned  %zu bytes  0 .. %u\n", sizeof(unsigned), UINT_MAX);
    printf("long      %zu bytes  %ld .. %ld\n", sizeof(long), LONG_MIN, LONG_MAX);
    printf("unsigned long       0 .. %lu\n", ULONG_MAX);
    printf("long long %zu bytes  %lld .. %lld\n", sizeof(long long), LLONG_MIN, LLONG_MAX);
    printf("unsigned long long  0 .. %llu\n", ULLONG_MAX);
    return 0;
}
```

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o limits limits.c
$ ./limits
CHAR_BIT  8
char      1 byte   -128 .. 127
short     2 bytes  -32768 .. 32767
int       4 bytes  -2147483648 .. 2147483647
unsigned  4 bytes  0 .. 4294967295
long      8 bytes  -9223372036854775808 .. 9223372036854775807
unsigned long       0 .. 18446744073709551615
long long 8 bytes  -9223372036854775808 .. 9223372036854775807
unsigned long long  0 .. 18446744073709551615
```

| Type | C99 guarantees at least | x86-64 Linux |
|---|---|---|
| `char` | 8 bits | 8 bits |
| `short` | 16 bits | 16 bits |
| `int` | 16 bits | 32 bits |
| `long` | 32 bits | 64 bits |
| `long long` | 64 bits | 64 bits |

- `sizeof` is an operator giving bytes as a `size_t` (unsigned; print with `%zu`). `sizeof x` works;
  `sizeof(type)` needs the parentheses.
- 64-bit Linux is **LP64**: `long` and pointers are 64 bits. 64-bit Windows keeps `long` at 32, breaking
  code that assumes otherwise.
- `musl-gcc -static` prints the same: sizes come from the platform ABI (CPU and compiler), not the
  library ([Chapter 1](01-getting-started.md#same-sizes-on-both-the-abi)). All programs here print the
  same on both libraries unless a section says otherwise.

## Exact-width types: `<stdint.h>` and `<inttypes.h>`

When size matters (a 16-bit port, a 32-bit IPv4 address), use C99's `<stdint.h>` types, the same on every
platform: `int8_t`, `uint8_t`, `int16_t`, `uint16_t`, `int32_t`, `uint32_t`, `int64_t`, `uint64_t`.
Printing needs care: `uint64_t` is `unsigned long` here but `unsigned long long` on 32-bit systems.
`<inttypes.h>` macros give the right `printf` format: `PRIu64`, `PRId32`, `PRIx16` and so on.

`examples/ch02/fixed.c`

```c
/* fixed.c - exact-width integers from <stdint.h>, printed with <inttypes.h>. */
#include <inttypes.h>   /* includes <stdint.h> and adds the PRI... macros */
#include <stdio.h>

int main(void)
{
    uint8_t  level = 255;                   /* exactly 8 bits, unsigned */
    int16_t  delta = -300;                  /* exactly 16 bits, signed */
    uint32_t ip = 0x7F000001;               /* 127.0.0.1 as one number */
    int64_t  ms = INT64_C(86400000) * 365;  /* milliseconds in a year */
    uint64_t total = UINT64_MAX;
    size_t   n = sizeof total;              /* sizeof gives a size_t */

    printf("level %" PRIu8 ", delta %" PRId16 "\n", level, delta);
    printf("ip    %" PRIu32 " = 0x%08" PRIX32 "\n", ip, ip);
    printf("ms    %" PRId64 "\n", ms);
    printf("total %" PRIu64 " (%zu bytes)\n", total, n);
    printf("PRIu64 is \"%s\" here\n", PRIu64);
    return 0;
}
```

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o fixed fixed.c
$ ./fixed
level 255, delta -300
ip    2130706433 = 0x7F000001
ms    31536000000
total 18446744073709551615 (8 bytes)
PRIu64 is "lu" here
```

- `"%" PRIu64` works because adjacent string literals join: `"%" "lu"` becomes `"%lu"`.
- `INT64_C(86400000)` makes a constant of the right type; otherwise `86400000 * 365` overflows `int`.
- Also useful: `size_t` (sizes, `%zu`), `ptrdiff_t` (pointer differences, `%td`), `intmax_t` (`%jd`);
  limits like `SIZE_MAX`, `INT32_MAX` are in `<stdint.h>`.

Rule of thumb: `int` for counting, `size_t` for sizes and indexes, exact-width types for fixed formats
(network protocols, file formats).

## Integer literals

`examples/ch02/literals.c`

```c
/* literals.c - how to write numbers and characters in C source. */
#include <stdio.h>

int main(void)
{
    printf("decimal 10 = %d, octal 010 = %d, hex 0x10 = %d\n", 10, 010, 0x10);
    printf("0xFF = %d, 0777 = %d\n", 0xFF, 0777);
    printf("sizes: 1 is %zu, 1L is %zu, 1LL is %zu, 1.0f is %zu, 1.0 is %zu\n",
           sizeof 1, sizeof 1L, sizeof 1LL, sizeof 1.0f, sizeof 1.0);
    printf("4000000000U = %u, 1e3 = %f, 2.5e-3 = %f\n", 4000000000U, 1e3, 2.5e-3);
    printf("'A' = %d, '0' = %d, '\\n' = %d, '\\0' = %d\n", 'A', '0', '\n', '\0');
    printf("'\\x41' = %c, '\\101' = %c, sizeof 'A' = %zu\n", '\x41', '\101', sizeof 'A');
    return 0;
}
```

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o literals literals.c
$ ./literals
decimal 10 = 10, octal 010 = 8, hex 0x10 = 16
0xFF = 255, 0777 = 511
sizes: 1 is 4, 1L is 8, 1LL is 8, 1.0f is 4, 1.0 is 8
4000000000U = 4000000000, 1e3 = 1000.000000, 2.5e-3 = 0.002500
'A' = 65, '0' = 48, '\n' = 10, '\0' = 0
'\x41' = A, '\101' = A, sizeof 'A' = 4
```

| Literal | Meaning |
|---|---|
| `42` | decimal `int` (or a bigger type if it doesn't fit) |
| `052` | **octal**: a leading `0` means base 8 (here 42) |
| `0x2A` | hexadecimal |
| `42u`, `42L`, `42LL`, `42uLL` | suffixes: `unsigned`, `long`, `long long` |
| `3.5`, `1e3`, `2.5e-3` | `double` |
| `3.5f` | `float` |
| `'A'` | character constant, of type `int`: `sizeof 'A'` is 4 |
| `'\n'`, `'\t'`, `'\0'`, `'\\'`, `'\''` | escapes: newline, tab, zero byte, backslash, quote |
| `'\x41'`, `'\101'` | a character by hex or octal code |

C99 has no binary literals (`0b101` is a GCC extension, standard in C23); use hex, four bits per digit.

### Gotcha: the leading zero

`010` is 8, not 10: zero-padding numbers "to line them up" makes them octal. Some digits can't be octal:

```console
oct.c: In function 'main':
oct.c:5:13: error: invalid digit "8" in octal constant
    5 |     int a = 08;
      |             ^~
```

Octal survives in Unix file permissions (`0755`, [Chapter 12](12-files-and-descriptors.md)).

## Signed numbers: two's complement

N unsigned bits hold 0 to 2^N − 1. For negatives every machine you'll meet uses **two's complement**:
the top bit counts −2^(N−1) instead of +2^(N−1). In 8 bits, `0xFF` is −128 + 127 = −1.

`examples/ch02/twos.c`

```c
/* twos.c - how negative numbers are stored: two's complement. */
#include <inttypes.h>
#include <stdio.h>

int main(void)
{
    int8_t one = 1, max = 127, minus1 = -1, minus2 = -2, min = -128;

    /* Converting to uint8_t keeps the same 8 bits; %02X shows them in hex. */
    printf("%4d = 0x%02X\n", one, (unsigned)(uint8_t)one);
    printf("%4d = 0x%02X\n", max, (unsigned)(uint8_t)max);
    printf("%4d = 0x%02X\n", minus1, (unsigned)(uint8_t)minus1);
    printf("%4d = 0x%02X\n", minus2, (unsigned)(uint8_t)minus2);
    printf("%4d = 0x%02X\n", min, (unsigned)(uint8_t)min);

    int32_t wide = minus1;           /* sign extension: the sign bit is copied */
    printf("int8_t -1 as int32_t: %" PRId32 " = 0x%08" PRIX32 "\n", wide, (uint32_t)wide);
    return 0;
}
```

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o twos twos.c
$ ./twos
   1 = 0x01
 127 = 0x7F
  -1 = 0xFF
  -2 = 0xFE
-128 = 0x80
int8_t -1 as int32_t: -1 = 0xFFFFFFFF
```

- Negate by flipping all bits and adding 1: `~x + 1 == -x`. −1 is all one-bits at any width.
- Widening a signed value copies the sign bit into the new high bits (**sign extension**): −1 stays −1.
- The range is asymmetric: `int8_t` is −128 to 127, so `-(-128)` doesn't fit.
- `(type)value` is a **cast**, an explicit conversion.

C99 also allowed ones' complement and sign-magnitude; C23 requires two's complement. Assume it.

## Overflow: unsigned wraps, signed is undefined

When a result doesn't fit:

- **Unsigned** arithmetic is defined to wrap modulo 2^N: `UINT_MAX + 1` is 0. Useful for hashes,
  checksums and sequence numbers.
- **Signed** overflow is **undefined behaviour** (UB): the standard places no requirement on the program,
  and the compiler optimizes assuming it never happens.

`examples/ch02/overflow.c`

```c
/* overflow.c - unsigned arithmetic wraps; signed overflow is undefined. */
#include <limits.h>
#include <stdio.h>

int main(void)
{
    unsigned int u = UINT_MAX;
    printf("UINT_MAX + 1 = %u\n", u + 1);       /* defined: wraps to 0 */
    printf("0u - 1       = %u\n", 0u - 1);      /* defined: wraps to UINT_MAX */

    int x = INT_MAX;
    int y = x + 1;                              /* undefined behaviour! */
    printf("y = %d\n", y);
    printf("x + 1 > x is %d\n", x + 1 > x);
    return 0;
}
```

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o overflow overflow.c
$ ./overflow
UINT_MAX + 1 = 0
0u - 1       = 4294967295
y = -2147483648
x + 1 > x is 1
```

In the last two lines `x + 1` is negative, yet `x + 1 > x` is true: signed overflow can't happen in a
valid program, so the compiler replaced the comparison with 1, silently. The program is simply wrong, in
a way that depends on compiler and optimization level.

The rule lets compilers generate fast, unchecked arithmetic on any CPU: "trust the programmer"
([Chapter 1](01-getting-started.md)). Prevent overflow by testing *before* computing (exercise 4).

Other common integer UB:

| Code | Why |
|---|---|
| `INT_MAX + 1`, `INT_MIN - 1`, `-INT_MIN` | signed overflow |
| `x / 0`, `x % 0` | division by zero (on x86-64 Linux, SIGFPE kills the program) |
| `INT_MIN / -1` | the result doesn't fit |
| `1 << 32` with a 32-bit `int` | shift count ≥ width |
| `1 << 31`, `-1 << 1` | shifting a signed value into or past the sign bit; use `1u << 31` |

gcc warns about some it sees at compile time:

```console
sh.c:3:15: warning: left shift count >= width of type [-Wshift-count-overflow]
    3 |     int a = 1 << 32;
      |               ^~
```

### glibc vs musl: catching overflow at run time

`-fsanitize=undefined` (UBSan, [Chapter 10](10-errors-and-debugging.md)) adds run-time UB checks. They
need a support library, and here the libraries differ:

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -fsanitize=undefined -o overflow overflow.c
$ ./overflow
UINT_MAX + 1 = 0
0u - 1       = 4294967295
overflow.c:12:9: runtime error: signed integer overflow: 2147483647 + 1 cannot be represented in type 'int'
y = -2147483648
x + 1 > x is 1
$ musl-gcc -std=c99 -Wall -Wextra -pedantic -static -fsanitize=undefined -fsanitize-undefined-trap-on-error -o overflow-musl overflow.c
$ ./overflow-musl
UINT_MAX + 1 = 0
0u - 1       = 4294967295
Illegal instruction (core dumped)
$ echo $?
132
```

- **glibc:** the sanitizer runtime (built for glibc) prints file, line and exact values, then continues.
- **musl:** that runtime won't link with musl. Only "trap" mode works: the check executes an invalid
  instruction and the program dies with SIGILL, silently; a debugger finds the line.

**glibc is better here**, thanks to its tooling. Good habit: test with glibc and the sanitizers, and also
build with musl.

## Promotions and conversions

C doesn't compute in `char` or `short`; operands are converted first, in two steps:

1. **Integer promotions.** Any type smaller than `int` (`char`, `short`, `uint8_t`, `_Bool`...) becomes
   `int`.
2. **Usual arithmetic conversions.** Operands of different types convert to a common type:
   - if either is floating point, the other converts to that type (the larger floating type wins);
   - if both are signed or both unsigned, the higher-ranked type wins (`long long` > `long` > `int`);
   - if the unsigned type's rank is the same or higher, **the signed one becomes unsigned**;
   - if the signed type's rank is higher, the unsigned one converts to it if it can hold all its values;
     otherwise both become the signed type's unsigned version (`long long` with `unsigned long` on LP64
     gives `unsigned long long`).

The rules that make signed values unsigned are the dangerous ones.

`examples/ch02/convert.c`

```c
/* convert.c - integer promotions and the usual arithmetic conversions. */
#include <stdint.h>
#include <stdio.h>

int main(void)
{
    uint8_t a = 200, b = 100;
    uint8_t c = a + b;                 /* a + b is an int (300); c keeps 300 % 256 */
    printf("a + b = %d, sizeof(a + b) = %zu, c = %d\n", a + b, sizeof(a + b), c);

    int minus = -1;
    unsigned int one = 1;
    printf("-1 < 1u is %d\n", minus < one);          /* the classic trap */
    printf("(unsigned)-1 = %u\n", (unsigned)minus);

    printf("7 / 2 = %d, -7 / 2 = %d, 7 %% 2 = %d, -7 %% 2 = %d\n", 7 / 2, -7 / 2, 7 % 2, -7 % 2);
    printf("7 / 2.0 = %f, (double)7 / 2 = %f\n", 7 / 2.0, (double)7 / 2);
    printf("(int)3.99 = %d, (int)-3.99 = %d\n", (int)3.99, (int)-3.99);

    long big = 3000000000L;
    int narrow = (int)big;             /* out of int's range: implementation-defined */
    printf("(int)3000000000L = %d\n", narrow);
    return 0;
}
```

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o convert convert.c
convert.c: In function 'main':
convert.c:13:37: warning: comparison of integer expressions of different signedness: 'int' and 'unsigned int' [-Wsign-compare]
   13 |     printf("-1 < 1u is %d\n", minus < one);          /* the classic trap */
      |                                     ^
$ ./convert
a + b = 300, sizeof(a + b) = 4, c = 44
-1 < 1u is 0
(unsigned)-1 = 4294967295
7 / 2 = 3, -7 / 2 = -3, 7 % 2 = 1, -7 % 2 = -1
7 / 2.0 = 3.500000, (double)7 / 2 = 3.500000
(int)3.99 = 3, (int)-3.99 = -3
(int)3000000000L = -1294967296
```

- `a + b` is 300: both `uint8_t`s became `int` first, so nothing overflowed. Storing 300 in `c` keeps it
  modulo 256: 44.
- `-1 < 1u` is **false**: `minus` converts to `unsigned int`, 4294967295. `-Wextra` warns
  (`-Wsign-compare`), one reason the course always uses it. Don't mix signed and unsigned in comparisons,
  or cast one side deliberately.
- Integer division truncates toward zero (official since C99): `-7 / 2` is −3, `-7 % 2` is −1; always
  `(a / b) * b + a % b == a`.
- Division is floating point if either operand is, so cast one side: `(double)7 / 2`. `(double)(7 / 2)`
  is 3.0: the division came first.
- Floating-to-integer conversion truncates toward zero; if the value doesn't fit, it's UB.

Out-of-range conversion to an integer type:

| Target | Result |
|---|---|
| unsigned | defined: the value modulo 2^N (keep the low N bits) |
| signed | implementation-defined; gcc keeps the low N bits, as the last line shows |

## Characters

`char` is a one-byte integer, usually a character code: on Linux ASCII, or one byte of UTF-8 (strings:
[Chapter 6](06-strings.md)).

`examples/ch02/chars.c`

```c
/* chars.c - a char is a small integer; whether it is signed depends on the platform. */
#include <limits.h>
#include <stdio.h>

int main(void)
{
    char letter = 'A';
    printf("%c is %d; next is %c\n", letter, letter, letter + 1);
    printf("'a' - 'A' = %d, '7' - '0' = %d\n", 'a' - 'A', '7' - '0');

    int big = 200;
    char c = (char)big;                     /* doesn't fit if char is signed */
    unsigned char uc = (unsigned char)big;
    signed char sc = (signed char)big;
    printf("char %d, unsigned char %d, signed char %d\n", c, uc, sc);
    printf("CHAR_MIN = %d, CHAR_MAX = %d\n", CHAR_MIN, CHAR_MAX);
    return 0;
}
```

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o chars chars.c
$ ./chars
A is 65; next is B
'a' - 'A' = 32, '7' - '0' = 7
char -56, unsigned char 200, signed char -56
CHAR_MIN = -128, CHAR_MAX = 127
$ gcc -std=c99 -Wall -Wextra -pedantic -funsigned-char -o chars chars.c
$ ./chars
A is 65; next is B
'a' - 'A' = 32, '7' - '0' = 7
char 200, unsigned char 200, signed char -56
CHAR_MIN = 0, CHAR_MAX = 255
```

- Characters are numbers: `'7' - '0'` is the digit value 7.
- `char`, `signed char` and `unsigned char` are three types. The ABI sets plain `char`'s sign: signed on
  x86-64, **unsigned on ARM** Linux (`-funsigned-char` shows the other behaviour on your machine).
- Use `char` for text and `unsigned char` (or `uint8_t`) for bytes; never rely on `char`'s sign.

## Floating point

C has `float` (usually 32 bits), `double` (64) and `long double` (80-bit extended precision on x86-64,
stored in 16 bytes), in IEEE 754 binary formats almost everywhere. Most decimal fractions have no exact
binary form, so results are approximate.

`examples/ch02/floats.c`

```c
/* floats.c - floating point is binary and approximate. */
#include <float.h>
#include <stdio.h>

int main(void)
{
    double sum = 0.1 + 0.2;
    printf("0.1 + 0.2 = %f (%%f)\n", sum);
    printf("0.1 + 0.2 = %.17g (%%.17g)\n", sum);
    printf("0.1 + 0.2 == 0.3 is %d\n", sum == 0.3);
    printf("0.1 in hex: %a\n", 0.1);

    float f = 16777217;                     /* 2^24 + 1: too many digits for float */
    printf("float 16777217 -> %.1f\n", f);
    printf("FLT_DIG %d, DBL_DIG %d, DBL_EPSILON %g, DBL_MAX %g\n",
           FLT_DIG, DBL_DIG, DBL_EPSILON, DBL_MAX);

    double zero = 0.0;
    printf("1/0.0 = %f, -1/0.0 = %f, 0/0.0 = %f\n", 1 / zero, -1 / zero, zero / zero);
    printf("%e, %g, %g\n", 6.02e23, 0.0001, 1e20);
    return 0;
}
```

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o floats floats.c
$ ./floats
0.1 + 0.2 = 0.300000 (%f)
0.1 + 0.2 = 0.30000000000000004 (%.17g)
0.1 + 0.2 == 0.3 is 0
0.1 in hex: 0x1.999999999999ap-4
float 16777217 -> 16777216.0
FLT_DIG 6, DBL_DIG 15, DBL_EPSILON 2.22045e-16, DBL_MAX 1.79769e+308
1/0.0 = inf, -1/0.0 = -inf, 0/0.0 = -nan
6.020000e+23, 0.0001, 1e+20
```

- `%f` rounds to 6 decimals, hiding the error; `%.17g` reveals it. `%a` (C99) prints the exact binary
  value in hex; the repeating `9`s show 0.1 isn't exact.
- Don't compare with `==`; test closeness, e.g. `fabs(a - b) < 1e-9` (`fabs` is in `<math.h>`).
- `float` has about 6 significant decimal digits (`FLT_DIG`), `double` about 15; prefer `double`.
- Under IEEE rules (C99 Annex F, followed by gcc), dividing by zero gives infinity, or NaN ("not a
  number") for 0/0. Integer division by zero is UB.
- Never use floating point for money or anything exact; count in integer cents.

## Booleans

C99 added a boolean type, `_Bool` (an odd name, so old programs defining their own `bool` didn't break).
`<stdbool.h>` defines the friendlier `bool`, `true` (1) and `false` (0).

`examples/ch02/bools.c`

```c
/* bools.c - _Bool, <stdbool.h>, and what counts as true. */
#include <stdbool.h>
#include <stdio.h>

int main(void)
{
    bool ready = false;
    _Bool done = 1;                  /* the keyword behind bool */
    printf("ready %d, done %d, sizeof(bool) %zu\n", ready, done, sizeof(bool));

    int n = 256;
    bool b = n;                      /* any non-zero value becomes 1 */
    unsigned char c = (unsigned char)n;  /* keeps only the low 8 bits: 0 */
    printf("bool from 256: %d, unsigned char from 256: %d\n", b, c);

    printf("5 > 3 is %d, 5 == 3 is %d, !7 is %d, !!7 is %d\n", 5 > 3, 5 == 3, !7, !!7);
    return 0;
}
```

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o bools bools.c
$ ./bools
ready 0, done 1, sizeof(bool) 1
bool from 256: 1, unsigned char from 256: 0
5 > 3 is 1, 5 == 3 is 0, !7 is 0, !!7 is 1
```

- Any non-zero value becomes `bool` 1, while an `unsigned char` keeps only low bits (256 becomes 0,
  "false"); `bool` is safer for flags.
- Conditions are true when non-zero. Comparisons and `!` give `int` 0 or 1; `!!x` maps any value to 0
  or 1.
- Print `bool` with `%d`; no conversion prints `true`/`false`.

## Named constants: `enum`, `const` and `#define`

Don't scatter magic numbers; C has three ways to name a constant:

`examples/ch02/consts.c`

```c
/* consts.c - named constants: enum, const and #define. */
#include <stdio.h>

#define LINE_MAX_BYTES 512           /* preprocessor text replacement (Chapter 9) */

enum { NICK_MAX = 9, CHANNELS_MAX = 20 };   /* true integer constants */
enum color { RED, GREEN, BLUE = 10, CYAN };  /* RED=0, GREEN=1, CYAN=11 */

int main(void)
{
    const double pi = 3.14159265358979;  /* read-only variable */
    enum color c = CYAN;

    printf("NICK_MAX %d, CHANNELS_MAX %d, LINE_MAX_BYTES %d\n",
           NICK_MAX, CHANNELS_MAX, LINE_MAX_BYTES);
    printf("RED %d, GREEN %d, BLUE %d, c %d\n", RED, GREEN, BLUE, c);
    printf("pi %.5f, sizeof(enum color) %zu\n", pi, sizeof(enum color));
    return 0;
}
```

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o consts consts.c
$ ./consts
NICK_MAX 9, CHANNELS_MAX 20, LINE_MAX_BYTES 512
RED 0, GREEN 1, BLUE 10, c 11
pi 3.14159, sizeof(enum color) 4
```

| Way | What it is | Use it for |
|---|---|---|
| `enum { N = 9 };` | a real `int` constant whose name the compiler and debugger know | integer constants: sizes, limits, codes |
| `const int n = 9;` | a **variable** you promise not to change | values that must not change; any type |
| `#define N 9` | text the preprocessor pastes in before compiling | non-`int` constants, things `enum` can't do ([Chapter 9](09-preprocessor-and-building.md)) |

- Enumerators count up from 0, or from the last value given.
- `enum color` is a type, but unchecked: an `enum color` variable can hold any `int`
  ([Chapter 7](07-structs-unions-enums.md) covers enum types).

### Gotcha: `const` isn't a constant

In C (unlike C++) a `const` variable is still a variable: not allowed where a constant expression is
required (enumerator values, `case` labels in [Chapter 3](03-control-flow.md)), and not assignable:

```c
    const int size = 4;
    enum { E = size };
    size = 5;
```

```console
ce.c: In function 'main':
ce.c:4:12: error: enumerator value for 'E' is not an integer constant
    4 |     enum { E = size };
      |            ^
ce.c:5:10: error: assignment of read-only variable 'size'
    5 |     size = 5;
      |          ^
```

So this course uses `enum` for integer constants.

## Operators

Most operators do what you'd expect. These need explaining: increment, compound assignment, and the
logical and conditional operators.

`examples/ch02/ops.c`

```c
/* ops.c - arithmetic, increment, assignment, logical and conditional operators. */
#include <stdio.h>

int main(void)
{
    int i = 5;
    int a = i++;                     /* a gets the old value: 5; i is now 6 */
    int b = ++i;                     /* i becomes 7 first; b gets 7 */
    printf("a %d, b %d, i %d\n", a, b, i);

    i += 3;                          /* i = i + 3 -> 10 */
    i *= 2;                          /* 20 */
    i %= 7;                          /* 6 */
    printf("i %d\n", i);

    int zero = 0;
    /* && stops at the first false operand, so 10 / zero never runs. */
    printf("zero != 0 && 10 / zero > 1 is %d\n", zero != 0 && 10 / zero > 1);
    printf("zero == 0 || 10 / zero > 1 is %d\n", zero == 0 || 10 / zero > 1);

    int bigger = a > b ? a : b;      /* condition ? if-true : if-false */
    printf("bigger %d, %s\n", bigger, i % 2 == 0 ? "i is even" : "i is odd");
    return 0;
}
```

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o ops ops.c
$ ./ops
a 5, b 7, i 7
i 6
zero != 0 && 10 / zero > 1 is 0
zero == 0 || 10 / zero > 1 is 1
bigger 7, i is even
```

- `i++` yields the old value, then increments; `++i` increments, then yields the new value. Alone
  (`i++;`) they're the same.
- `x op= y` means `x = x op y` for every binary arithmetic and bitwise operator.
- `&&` and `||` **short-circuit**: the right side runs only if the left doesn't decide the answer, so
  guards like `n != 0 && total / n > 10` are safe.
- Assignment is an expression: `a = b = 0` sets both, and `if (x = 0)` compiles
  ([Chapter 3](03-control-flow.md) shows the warning).

### Bitwise operators

`&` (and), `|` (or), `^` (exclusive or), `~` (flip all bits), `<<` and `>>` (shift) work on bits,
classically to pack yes/no flags into one number.

`examples/ch02/bits.c`

```c
/* bits.c - bitwise operators: flags packed into one unsigned int. */
#include <stdio.h>

enum {
    FLAG_VERBOSE = 1u << 0,          /* 0x1 */
    FLAG_DAEMON  = 1u << 1,          /* 0x2 */
    FLAG_IPV6    = 1u << 2           /* 0x4 */
};

int main(void)
{
    unsigned int flags = 0;

    flags |= FLAG_VERBOSE | FLAG_IPV6;           /* set two bits */
    printf("set:    %#04x\n", flags);
    printf("verbose? %d, daemon? %d\n",
           (flags & FLAG_VERBOSE) != 0, (flags & FLAG_DAEMON) != 0);

    flags &= ~FLAG_VERBOSE;                      /* clear a bit */
    printf("clear:  %#04x\n", flags);
    flags ^= FLAG_DAEMON;                        /* toggle a bit */
    printf("toggle: %#04x\n", flags);

    printf("0xF0 >> 4 = %#x, 1u << 31 = %#x, ~0u = %#x\n", 0xF0 >> 4, 1u << 31, ~0u);
    printf("0xC & 0xA = %#x, 0xC | 0xA = %#x, 0xC ^ 0xA = %#x\n", 0xC & 0xA, 0xC | 0xA, 0xC ^ 0xA);
    return 0;
}
```

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o bits bits.c
$ ./bits
set:    0x05
verbose? 1, daemon? 0
clear:  0x04
toggle: 0x06
0xF0 >> 4 = 0xf, 1u << 31 = 0x80000000, ~0u = 0xffffffff
0xC & 0xA = 0x8, 0xC | 0xA = 0xe, 0xC ^ 0xA = 0x6
```

| Task | Idiom |
|---|---|
| set bit | `flags \|= BIT` |
| clear bit | `flags &= ~BIT` |
| toggle bit | `flags ^= BIT` |
| test bit | `(flags & BIT) != 0` |
| bit number n | `1u << n` |

- Use **unsigned** types: shifting into a signed sign bit is UB, and `>>` of a negative value is
  implementation-defined (gcc copies the sign bit). For unsigned `x`, `x << n` multiplies and `x >> n`
  divides by 2^n.
- Don't confuse `&` with `&&`: `1 & 2` is 0, `1 && 2` is 1.

### Precedence and order of evaluation

Precedence decides how operators group, highest first:

| Operators | Groups |
|---|---|
| `()` `[]` `->` `.` `x++` `x--` (postfix) | left to right |
| `++x` `--x` `+x` `-x` `!` `~` `(type)` `*p` `&x` `sizeof` (unary) | right to left |
| `*` `/` `%` | left to right |
| `+` `-` | left to right |
| `<<` `>>` | left to right |
| `<` `<=` `>` `>=` | left to right |
| `==` `!=` | left to right |
| `&` | left to right |
| `^` | left to right |
| `\|` | left to right |
| `&&` | left to right |
| `\|\|` | left to right |
| `?:` | right to left |
| `=` `+=` `-=` `*=` `/=` `%=` `<<=` `>>=` `&=` `^=` `\|=` | right to left |
| `,` | left to right |

Pointers (`*p`, `&x`, `[]`, `->`) come in [Chapter 5](05-pointers-and-arrays.md), structs (`.`) in
[Chapter 7](07-structs-unions-enums.md), the comma operator in [Chapter 3](03-control-flow.md).

Bitwise operators sit *below* comparisons, a historical accident (early C had no `&&`; `&` did both
jobs), causing the most common precedence bug:

`examples/ch02/precedence.c`

```c
/* precedence.c - three expressions that don't mean what they look like. */
#include <stdio.h>

enum { FLAG_IPV6 = 0x4 };

int main(void)
{
    unsigned int flags = 0x4;

    /* == binds tighter than &: this is flags & (FLAG_IPV6 == FLAG_IPV6). */
    printf("flags & FLAG_IPV6 == FLAG_IPV6   -> %u\n", flags & FLAG_IPV6 == FLAG_IPV6);
    printf("(flags & FLAG_IPV6) == FLAG_IPV6 -> %d\n", (flags & FLAG_IPV6) == FLAG_IPV6);

    /* + binds tighter than <<: this is 1 << (2 + 1). */
    printf("1 << 2 + 1 -> %d\n", 1 << 2 + 1);

    /* Relational operators go left to right: (3 > 2) > 1 is 1 > 1. */
    printf("3 > 2 > 1 -> %d\n", 3 > 2 > 1);
    return 0;
}
```

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o precedence precedence.c
precedence.c: In function 'main':
precedence.c:11:62: warning: suggest parentheses around comparison in operand of '&' [-Wparentheses]
   11 |     printf("flags & FLAG_IPV6 == FLAG_IPV6   -> %u\n", flags & FLAG_IPV6 == FLAG_IPV6);
      |                                                              ^
precedence.c:15:36: warning: suggest parentheses around '+' inside '<<' [-Wparentheses]
   15 |     printf("1 << 2 + 1 -> %d\n", 1 << 2 + 1);
      |                                    ^~
precedence.c:18:39: warning: comparisons like 'X<=Y<=Z' do not have their mathematical meaning [-Wparentheses]
   18 |     printf("3 > 2 > 1 -> %d\n", 3 > 2 > 1);
      |                                       ^
$ ./precedence
flags & FLAG_IPV6 == FLAG_IPV6   -> 0
(flags & FLAG_IPV6) == FLAG_IPV6 -> 1
1 << 2 + 1 -> 8
3 > 2 > 1 -> 0
```

`-Wall` catches all three. Still: **when mixing bitwise, shift and comparison operators, add
parentheses.** Nobody remembers the whole table.

Precedence decides grouping, **not** evaluation order: in `f() + g()` either may run first. Modifying a
variable twice, or reading it while modifying it, without a **sequence point** between is UB. Sequence
points are at `;`, `&&`, `||`, `?:`, the comma operator and function calls. gcc warns:

```c
    int i = 1;
    i = i++ + 1;
```

```console
seq.c:6:7: warning: operation on 'i' may be undefined [-Wsequence-point]
    6 |     i = i++ + 1;
      |     ~~^~~~~~~~~
```

Keep `++` and `--` in their own statements or a simple `for` header and this can't happen.

## Formatted output: `printf`

`printf`'s first argument is a **format string**: text plus `%` conversions, each consuming the next
argument.

`examples/ch02/formats.c`

```c
/* formats.c - printf conversions, flags, width and precision. */
#include <stdio.h>

int main(void)
{
    printf("[%d] [%5d] [%-5d] [%05d] [%+d] [% d]\n", 42, 42, 42, 42, 42, 42);
    printf("[%u] [%x] [%X] [%#x] [%o] [%#o]\n", 255u, 255u, 255u, 255u, 255u, 255u);
    printf("[%f] [%.2f] [%8.3f] [%e] [%g] [%g]\n", 3.14159, 3.14159, 3.14159,
           3.14159, 0.0001, 1e20);
    printf("[%s] [%8s] [%-8s] [%.3s]\n", "parley", "parley", "parley", "parley");
    printf("[%c] [%*d] [%%]\n", 'P', 6, 42);
    printf("[%hhd] [%ld] [%lld] [%zu]\n", (signed char)-5, 123456789L, 1LL << 40, sizeof(long));
    return 0;
}
```

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o formats formats.c
$ ./formats
[42] [   42] [42   ] [00042] [+42] [ 42]
[255] [ff] [FF] [0xff] [377] [0377]
[3.141590] [3.14] [   3.142] [3.141590e+00] [0.0001] [1e+20]
[parley] [  parley] [parley  ] [par]
[P] [    42] [%]
[-5] [123456789] [1099511627776] [8]
```

A conversion is `%[flags][width][.precision][length]type`:

| Part | Values |
|---|---|
| type | `d`/`i` signed int, `u` unsigned, `x`/`X` hex, `o` octal, `c` character, `s` string, `f` fixed, `e` exponent, `g` `f` or `e` style by exponent, trailing zeros removed, `a` hex float, `p` pointer ([Chapter 5](05-pointers-and-arrays.md)), `%%` a literal % |
| flags | `-` left-align, `+` always show sign, space: space for positive, `0` pad with zeros, `#` alternate form (`0x`, leading `0`) |
| width | minimum field width; `*` takes it from an argument |
| precision | digits after the point (`f`, `e`), significant digits (`g`), most characters (`s`) |
| length | `hh` char, `h` short, `l` long, `ll` long long, `z` size_t, `j` intmax_t, `t` ptrdiff_t, `L` long double |

- `float` arguments are promoted to `double`, so `%f` prints both.
- **Formats must match argument types**; a mismatch is UB, and `-Wall` warns
  ([Chapter 1](01-getting-started.md#compiler-flags) showed `%d` given a string).

## Formatted input: `scanf`

`scanf` is `printf` in reverse, converting standard input by the format. It returns the number of items
converted, or `EOF` (−1) if input ended before the first. **Always check it**, or you compute with
uninitialized variables.

`examples/ch02/sum.c`

```c
/* sum.c - read two integers with scanf and always check what it matched. */
#include <stdio.h>

int main(void)
{
    int a, b;

    /* scanf needs the address of each variable (&a): Chapter 5 explains why. */
    int matched = scanf("%d %d", &a, &b);
    printf("scanf matched %d\n", matched);
    if (matched != 2) {                          /* if: Chapter 3 */
        fprintf(stderr, "sum: expected two integers\n");
        return 1;
    }
    printf("%d + %d = %d\n", a, b, a + b);
    return 0;
}
```

`if` ([Chapter 3](03-control-flow.md)) runs the block when the condition is true; `fprintf(stderr, ...)`
prints to standard error, the stream for error messages.

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o sum sum.c
$ echo '3 4' | ./sum
scanf matched 2
3 + 4 = 7
$ echo '  -3   40 junk' | ./sum
scanf matched 2
-3 + 40 = 37
$ echo '3 x' | ./sum; echo $?
scanf matched 1
sum: expected two integers
1
$ ./sum < /dev/null; echo $?
scanf matched -1
sum: expected two integers
1
$ echo '2147483648 1' | ./sum
scanf matched 2
-2147483648 + 1 = -2147483647
```

- `echo ... | ./sum` feeds a line to standard input (or type it).
- `%d` skips leading whitespace, newlines included; a space in the format skips any amount.
- `scanf` stops at the first non-matching input and leaves it unread (`x`, `junk`).
- `double` takes `%lf` in `scanf` but `%f` in `printf`: `scanf` stores through an address, so it needs
  the exact size.
- The last run is UB: a number that doesn't fit an `int`. Both libraries happened to wrap it to
  −2147483648 silently. **`scanf` can't detect overflow**; for user numbers use `strtol`, as the project
  does below.

## glibc vs musl: the math library

Math functions (`sqrt`, `pow`, `sin`, `floor`, ...) are declared in `<math.h>`, and show the first real
difference between the libraries.

`examples/ch02/roots.c`

```c
/* roots.c - a function from the math library: sqrt. */
#include <math.h>
#include <stdio.h>

int main(void)
{
    double x;

    if (scanf("%lf", &x) != 1) {                 /* %lf reads a double */
        fprintf(stderr, "roots: expected a number\n");
        return 1;
    }
    printf("sqrt(%g) = %g\n", x, sqrt(x));       /* printf uses %g/%f for double */
    return 0;
}
```

With glibc, compiling works but linking fails unless you add `-lm` ("link the library `libm`") **after**
the file that uses it:

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -c roots.c
$ gcc -o roots roots.o
/usr/bin/ld: roots.o: in function `main':
roots.c:(.text+0x6f): undefined reference to `sqrt'
collect2: error: ld returned 1 exit status
$ gcc -lm -o roots roots.o
/usr/bin/ld: roots.o: in function `main':
roots.c:(.text+0x6f): undefined reference to `sqrt'
collect2: error: ld returned 1 exit status
$ gcc -o roots roots.o -lm
$ echo 2 | ./roots
sqrt(2) = 1.41421
$ echo -1 | ./roots
sqrt(-1) = -nan
```

With musl, no `-lm` is needed:

```console
$ musl-gcc -std=c99 -Wall -Wextra -pedantic -static -o roots-musl roots.c
$ echo 2 | ./roots-musl
sqrt(2) = 1.41421
$ echo -1 | ./roots-musl
sqrt(-1) = -nan
```

- **Why glibc needs `-lm`:** for historical reasons glibc's math functions live in `libm.so.6`. musl
  puts *everything* in one `libc`; its `libm.a` is empty, so `-lm` is harmless.
- **Why the order matters:** the linker reads its inputs left to right. From a static library it takes
  only the functions that files *before* it still need. For shared libraries, Ubuntu's gcc passes
  `--as-needed`, which drops a library nothing has needed yet, so `-lm` before `roots.o` is ignored.
  Other distributions may accept `-lm` first, but last always works.
- Compiling and linking in one step (`gcc ... -o roots roots.c`) fails the same way, naming a temporary
  `/tmp/cc....o` instead of `roots.o`; same fix.
- Results are identical: correctly rounded square roots, and NaN for −1.

**musl is simpler here**: one library, no flags. For portable builds put `-lm` last anyway (musl ignores
it), as [Chapter 9](09-preprocessor-and-building.md)'s Makefile does with `LDLIBS` after the objects.

## Building parley: reading the port number

`parleyd` will listen on a TCP port, 1 to 65535 (16 bits; port 0 is special). Users type it on the
command line, which is untrusted input, so the server must turn text like `"6600"` into a number and
reject anything else. Unlike `scanf`, the standard `strtol` ("string to long") detects overflow; it
reports three kinds of problem:

| Problem | Example | How you detect it |
|---|---|---|
| out of `long`'s range | `99999999999999999999` | `strtol` sets `errno` to `ERANGE` |
| no digits at all | `""`, `abc` | `end` still points at the start of the text |
| junk after the digits | `66x` | `end` doesn't point at the text's end (`'\0'`) |

The program checks the range 1–65535 itself. [Chapter 6](06-strings.md) covers strings, `errno` and
`strtol` fully; [Chapter 10](10-errors-and-debugging.md) covers `errno`.

For now the port is the third command-line word: `parleyd -p 6600`. The words are `argv[0]`
(`./parleyd`), `argv[1]` (`-p`) and `argv[2]` (`6600`); `argc` counts them. [Chapter 4](04-functions.md)
explains `argc`/`argv`, [Chapter 5](05-pointers-and-arrays.md) the `*` and `[]`; read `argv[2]` as "the
third word". [Chapter 3](03-control-flow.md) adds proper option handling.

Replace `parleyd.c` with this version (`parley.c` doesn't change):

`parleyd.c`

```c
/* parleyd.c - the parley chat server (Chapter 2: read the port number). */
#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

enum { DEFAULT_PORT = 6667, PORT_MIN = 1, PORT_MAX = 65535 };

int main(int argc, char *argv[])
{
    /* argv is the list of command-line words (Chapter 4 explains argc/argv,
     * Chapter 5 pointers). For now we expect "parleyd -p PORT", so the
     * port text is the third word, argv[2]. Without it we use the default. */
    const char *text = argc > 2 ? argv[2] : "6667";

    /* strtol converts text to a long. It reports problems three ways:
     * errno (out of long's range), end == text (no digits at all), and
     * *end != '\0' (junk after the digits). */
    char *end;
    errno = 0;
    long value = strtol(text, &end, 10);
    bool ok = errno == 0 && end != text && *end == '\0'
              && value >= PORT_MIN && value <= PORT_MAX;

    /* A TCP port is a 16-bit unsigned number: uint16_t is the exact type.
     * The cast is safe because ok guarantees 1 <= value <= 65535. */
    uint16_t port = ok ? (uint16_t)value : DEFAULT_PORT;

    /* if/else is covered in Chapter 3. */
    if (!ok) {
        fprintf(stderr, "parleyd: invalid port '%s' (expected %d-%d)\n",
                text, PORT_MIN, PORT_MAX);
        return 2;
    }
    printf("parleyd: port %" PRIu16 " (%zu bytes as uint16_t, %zu as long)\n",
           port, sizeof port, sizeof value);
    return 0;
}
```

This chapter's ideas at work:

- **`enum`** names `DEFAULT_PORT`, `PORT_MIN`, `PORT_MAX`.
- **`argc > 2 ? argv[2] : "6667"`** picks the third word if any, else the default.
- **`bool ok = ... && ...`** combines every check in one short-circuiting expression that reads like a
  sentence.
- **`long value`** holds out-of-range input (up to `LONG_MAX`) without overflow. Only after the range
  check do we convert to **`uint16_t`**; converting first would silently turn 70000 into 4464 (modulo
  65536).
- **`PRIu16`** prints the `uint16_t` portably; `%zu` the `sizeof` results.
- Exit status **2** means "bad command line", as in most Unix tools.

### Why range-check the port ourselves?

In [Chapter 14](14-networking-basics.md) the port text goes to `getaddrinfo`, which turns host names and
ports into socket addresses. Surely it rejects bad ports? `examples/ch02/gai-port.c` asks it about four
port strings (run it now; Chapter 14 explains the code):

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o gai-port gai-port.c
$ ./gai-port
"6667" -> port 6667
"65535" -> port 65535
"65536" -> port 0
"99999" -> port 34463
$ musl-gcc -std=c99 -Wall -Wextra -pedantic -static -o gai-port-musl gai-port.c
$ ./gai-port-musl
"6667" -> port 6667
"65535" -> port 65535
"65536" -> error: Unrecognized service
"99999" -> error: Unrecognized service
```

glibc silently keeps the low 16 bits (99999 − 65536 = 34463), the wrap from
[Promotions and conversions](#promotions-and-conversions): mistype 6667 as 66667 and the server quietly
uses port 1131. musl rejects the port. **musl is better here**: it fails loudly. Our own range check makes
`parleyd` behave the same on both.

### Checkpoint

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o parleyd parleyd.c
$ musl-gcc -std=c99 -Wall -Wextra -pedantic -static -o parleyd-musl parleyd.c
$ ./parleyd
parleyd: port 6667 (2 bytes as uint16_t, 8 as long)
$ ./parleyd -p 6600
parleyd: port 6600 (2 bytes as uint16_t, 8 as long)
$ ./parleyd -p 70000; echo $?
parleyd: invalid port '70000' (expected 1-65535)
2
$ ./parleyd -p 66x; echo $?
parleyd: invalid port '66x' (expected 1-65535)
2
$ ./parleyd -p ""; echo $?
parleyd: invalid port '' (expected 1-65535)
2
$ ./parleyd -p 99999999999999999999; echo $?
parleyd: invalid port '99999999999999999999' (expected 1-65535)
2
$ ./parleyd -p -1; echo $?
parleyd: invalid port '-1' (expected 1-65535)
2
$ ./parleyd-musl -p 6600
parleyd: port 6600 (2 bytes as uint16_t, 8 as long)
$ ./parleyd-musl -p 70000; echo $?
parleyd: invalid port '70000' (expected 1-65535)
2
```

Identical on both libraries: only the standard library and integer types are used, and those come from
the ABI. `argv[1]` is ignored, so `./parleyd x 6600` also works until [Chapter 3](03-control-flow.md)
checks options properly.

The complete project at this point is in `project/ch02/`.

## Summary

- C99: declarations anywhere, `//` comments, no implicit `int`.
- Integer sizes are per platform (LP64 on 64-bit Linux): `<limits.h>` for ranges; `<stdint.h>` types
  and `<inttypes.h>` `PRI` macros when size matters.
- Leading `0` means octal; character constants are `int`s.
- Two's complement. Unsigned wraps; signed overflow is UB, assumed impossible by the optimizer.
- Small types promote to `int`; mixing signed and unsigned makes the signed side unsigned (`-1 < 1u` is
  false; `-Wextra` catches it).
- Integer division truncates toward zero. Floating point is approximate: no `==`.
- Plain `char`'s sign varies; use `unsigned char` for bytes. `bool` turns non-zero into 1.
- `enum` for integer constants; `const` is a read-only variable, not a constant.
- Parenthesize mixed bitwise/comparison operators; never modify a variable twice in one expression.
- Match `printf`/`scanf` formats to types; check `scanf`'s result; validate numbers with `strtol`.
- glibc needs `-lm` (last); musl doesn't. glibc's sanitizers report UB clearly; musl has only trap mode.
  glibc's `getaddrinfo` wraps port 99999; musl rejects it.

## Exercises

1. Predict each line's output, then compile and run to check. Explain each result.

   ```c
   printf("%d\n", -1L < 1u);
   printf("%zu\n", sizeof 'A');
   printf("%d\n", (unsigned char)-1);
   printf("%d\n", -7 / 2);
   printf("%d\n", 010 + 1);
   printf("%d\n", 5 / 9 * 100);
   printf("%d\n", 0x10 | 1 << 2);
   ```

<details><summary>Solution</summary>

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o predict predict.c
$ ./predict
1
4
255
-3
9
0
20
```

- `-1L < 1u` is 1: unlike `-1 < 1u`, the signed `long` is bigger than `unsigned int` and holds all its
  values, so `1u` converts to `long`. (With 32-bit `long` it's 0: another reason not to mix.)
- A character constant is a 4-byte `int`. −1 as `unsigned char` is 255 (modulo 256).
- Division truncates toward zero: −3. `010` is octal 8, plus 1 is 9. `5 / 9` is integer 0, times 100 is 0.
- `<<` binds tighter than `|`: `0x10 | (1 << 2)` = 16 + 4 = 20.

Same on both libraries: these are compiler and ABI rules.

</details>

2. Store a set of numbers 0–63 in a `uint64_t` (bit n set when n is in it). Add 0, 5 and 63, print it
   as 16 hex digits, test for 5 and 6, remove 63 and print again. What goes wrong with `1 << 63`?

<details><summary>Solution</summary>

```c
/* bitset.c - a set of the numbers 0..63 stored in one uint64_t. */
#include <inttypes.h>
#include <stdio.h>

int main(void)
{
    uint64_t set = 0;

    set |= UINT64_C(1) << 0;                 /* add 0 */
    set |= UINT64_C(1) << 5;                 /* add 5 */
    set |= UINT64_C(1) << 63;                /* add 63 */
    printf("set = 0x%016" PRIx64 "\n", set);
    printf("has 5? %d, has 6? %d\n",
           (set >> 5 & 1) == 1, (set >> 6 & 1) == 1);

    set &= ~(UINT64_C(1) << 63);             /* remove 63 */
    printf("set = 0x%016" PRIx64 "\n", set);
    return 0;
}
```

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o bitset bitset.c
$ ./bitset
set = 0x8000000000000021
has 5? 1, has 6? 0
set = 0x0000000000000021
```

`1` is a 32-bit `int`, so `1 << 63` shifts past its width: UB. gcc warns; here it gave 0:

```console
bad63.c:6:22: warning: left shift count >= width of type [-Wshift-count-overflow]
    6 |     uint64_t set = 1 << 63;
      |                      ^~
```

`UINT64_C(1)` makes the 1 a 64-bit unsigned constant first.

</details>

3. Write `celsius.c`: read a Fahrenheit `double` with `scanf` and print Celsius, C = 5/9 × (F − 32).
   First write exactly `5 / 9 * (f - 32)`. Why is the answer always 0? Fix it.

<details><summary>Solution</summary>

```c
/* celsius.c - convert Fahrenheit to Celsius. */
#include <stdio.h>

int main(void)
{
    double f;

    if (scanf("%lf", &f) != 1) {
        fprintf(stderr, "celsius: expected a number\n");
        return 1;
    }
    printf("wrong: %f\n", 5 / 9 * (f - 32));     /* 5 / 9 is int division: 0 */
    printf("right: %f\n", 5.0 / 9 * (f - 32));
    return 0;
}
```

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o celsius celsius.c
$ echo 212 | ./celsius
wrong: 0.000000
right: 100.000000
$ echo 98.6 | ./celsius
wrong: 0.000000
right: 37.000000
```

`*` and `/` group left to right, so `5 / 9` comes first, in `int`: 0. The conversion to `double` comes too
late. `5.0 / 9` divides in `double`; `(f - 32) * 5 / 9` also works.

</details>

4. Write `safeadd.c`: read two `int`s and print their sum, or, if it would overflow, an error with exit
   status 1. Don't compute `a + b` before you know it fits (that would already be UB).

<details><summary>Solution</summary>

```c
/* safeadd.c - add two ints only if the result fits. */
#include <limits.h>
#include <stdio.h>

int main(void)
{
    int a, b;

    if (scanf("%d %d", &a, &b) != 2) {
        fprintf(stderr, "safeadd: expected two integers\n");
        return 1;
    }
    /* Test before adding: a + b itself would already be undefined. */
    if ((b > 0 && a > INT_MAX - b) || (b < 0 && a < INT_MIN - b)) {
        fprintf(stderr, "safeadd: %d + %d overflows int\n", a, b);
        return 1;
    }
    printf("%d\n", a + b);
    return 0;
}
```

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o safeadd safeadd.c
$ echo '2000000000 2000000000' | ./safeadd; echo $?
safeadd: 2000000000 + 2000000000 overflows int
1
$ echo '-5 7' | ./safeadd
2
$ echo '-2147483648 -1' | ./safeadd
safeadd: -2147483648 + -1 overflows int
$ echo '2147483647 -1' | ./safeadd
2147483646
```

`INT_MAX - b` can't overflow when `b > 0`, nor `INT_MIN - b` when `b < 0`; `&&` short-circuits to the
test matching `b`'s sign.

</details>

Sources for this chapter: [REFERENCES.md](REFERENCES.md#chapter-2--types-and-operators)
