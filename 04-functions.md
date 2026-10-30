# Chapter 4 — Functions

Functions split a program into named pieces you can read, test and reuse. This chapter, following [Chapter 3](03-control-flow.md), covers prototypes, argument passing, `static` functions, recursion and the stack, `inline`, variadic functions and the full `main`. It also meets the C99/POSIX line, where glibc and musl first differ. In the project, both programs become small functions, and the server gets a printf-style logger. [Chapter 5](05-pointers-and-arrays.md) shows how a function changes its caller's variables.

## Declarations and definitions

A **definition** creates the function: name, types and body. A **declaration** (**prototype**) gives only the name, return type and parameter types. The compiler reads top to bottom and needs at least a declaration to check a call.

`examples/ch04/prototypes.c`
```c
/* prototypes.c - declare a function before you use it, define it anywhere. */
#include <stdio.h>

int square(int x);                  /* declaration (prototype): name, types */
static void greet(const char *name);

int main(void)
{
    greet("parley");
    printf("square(7) = %d\n", square(7));
    return 0;
}

int square(int x)                   /* definition: the body */
{
    return x * x;
}

static void greet(const char *name)
{
    printf("hello, %s\n", name);
}
```

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o prototypes prototypes.c
$ ./prototypes
hello, parley
square(7) = 49
```

- Declare a function as often as you like; define it once.
- Parameter names in a declaration are optional (`int square(int);`) but document it; keep them.
- `static` makes `greet` private to this file ([below](#static-functions-and-linkage)).
- Defining each function before its first use, `main` last, makes prototypes unnecessary; the project does this. For a function in another file, the prototype goes in a header ([Chapter 9](09-preprocessor-and-building.md)).

### Gotcha: calling a function the compiler hasn't seen

Delete the two prototype lines, save as `noproto.c`, and compile:

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -c noproto.c
noproto.c: In function ‘main’:
noproto.c:7:5: warning: implicit declaration of function ‘greet’ [-Wimplicit-function-declaration]
    7 |     greet("parley");
      |     ^~~~~
noproto.c:8:32: warning: implicit declaration of function ‘square’ [-Wimplicit-function-declaration]
    8 |     printf("square(7) = %d\n", square(7));
      |                                ^~~~~~
noproto.c: At top level:
noproto.c:17:13: warning: conflicting types for ‘greet’; have ‘void(const char *)’
   17 | static void greet(const char *name)
      |             ^~~~~
noproto.c:17:13: error: static declaration of ‘greet’ follows non-static declaration
noproto.c:7:5: note: previous implicit declaration of ‘greet’ with type ‘void(const char *)’
    7 |     greet("parley");
      |     ^~~~~
noproto.c:17:13: warning: ‘greet’ defined but not used [-Wunused-function]
   17 | static void greet(const char *name)
      |             ^~~~~
```

C89 let you call an undeclared function, guessing `int f()` (returns `int`, takes anything). C99 dropped this "implicit declaration", but gcc still guesses and only *warns*. Here the guess clashes with `static void greet`, so it's an error; without a clash, the program builds silently wrong ([see below](#glibc-vs-musl-hidden-functions)). **Treat every `implicit declaration` warning as an error**: it means a missing `#include` or prototype.

## `f()` is not `f(void)`

In a declaration, empty parentheses mean "parameters not specified", not "none". The compiler checks nothing at the call:

`examples/ch04/empty-parens.c`
```c
/* empty-parens.c - why f() is not f(void) in C. This program has a bug
 * that the compiler does not report with -Wall -Wextra -pedantic. */
#include <stdio.h>

int add();                          /* "unknown parameters": no checking */

int main(void)
{
    printf("add(1) = %d\n", add(1));    /* one argument too few */
    return 0;
}

int add(int a, int b)
{
    return a + b;
}
```

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o empty-parens empty-parens.c
$ ./empty-parens
add(1) = 1266841097
```

No warning, and garbage that changes per run: `add` read its second argument from a register the caller never set. That is **undefined behaviour**; the standard says nothing about what happens.

- Write `int f(void)` for "no arguments", hence `int main(void)`.
- Always write full prototypes. Change the declaration to `int add(int a, int b);`, save as `prototype.c`, and the call is checked:

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -c prototype.c
prototype.c: In function ‘main’:
prototype.c:9:29: error: too few arguments to function ‘add’
    9 |     printf("add(1) = %d\n", add(1));    /* one argument too few */
      |                             ^~~
prototype.c:5:5: note: declared here
    5 | int add(int a, int b);
      |     ^~~
```

- `-Wstrict-prototypes` (not part of `-Wall` or `-Wextra`) flags the old style:

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -Wstrict-prototypes -c empty-parens.c
empty-parens.c:5:1: warning: function declaration isn’t a prototype [-Wstrict-prototypes]
    5 | int add();                          /* "unknown parameters": no checking */
      | ^~~
```

C23 made `f()` mean `f(void)`; C99 doesn't, so this course writes `(void)`.

## Arguments are copies; results come back with `return`

C passes every argument **by value**: the function gets a copy, and changing it doesn't touch the caller's variable:

`examples/ch04/byvalue.c`
```c
/* byvalue.c - arguments are copies; results come back with return. */
#include <stdio.h>

static void reset(int n)
{
    n = 0;                          /* changes the local copy only */
    printf("inside reset: n = %d\n", n);
}

static int doubled(int n)
{
    return n * 2;
}

static void say(const char *what)   /* void: returns nothing */
{
    printf("%s\n", what);
}

int main(void)
{
    int count = 5;
    reset(count);
    printf("after reset: count = %d\n", count);

    count = doubled(count);         /* use the result */
    printf("after doubled: count = %d\n", count);

    doubled(count);                 /* legal: the result is thrown away */
    say("done");
    return 0;
}
```

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o byvalue byvalue.c
$ ./byvalue
inside reset: n = 0
after reset: count = 5
after doubled: count = 10
done
```

This is safe: a function can't change your variables behind your back. To let it, pass a pointer ([Chapter 5](05-pointers-and-arrays.md)). Arrays seem an exception: they are passed as a pointer to their first element (also Chapter 5).

- A function returns one value; `return expr;` converts `expr` to the declared type, like an assignment.
- A `void` function returns nothing; `return;` leaves early.
- For several results, return a struct ([Chapter 7](07-structs-unions-enums.md)) or write through pointer parameters (Chapter 5).

### Gotcha: falling off the end of a non-void function

`sign.c`
```c
#include <stdio.h>

static int sign(int x)
{
    if (x > 0)
        return 1;
    if (x < 0)
        return -1;
}

int main(void)
{
    printf("sign(0) = %d\n", sign(0));
    return 0;
}
```

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o sign sign.c
sign.c: In function ‘sign’:
sign.c:9:1: warning: control reaches end of non-void function [-Wreturn-type]
    9 | }
      | ^
$ ./sign
sign(0) = 481907056
```

Using the missing result is undefined behaviour; the number varies. Only `main` is exempt: reaching its closing `}` returns 0 (a C99 rule).

### Gotcha: the order in which arguments are evaluated

Argument evaluation order is unspecified, so changing one variable twice in a call is undefined behaviour:

`seq.c`
```c
#include <stdio.h>

static void show(int a, int b)
{
    printf("a = %d, b = %d\n", a, b);
}

int main(void)
{
    int i = 1;
    show(i++, i++);     /* which i++ runs first? */
    return 0;
}
```

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o seq seq.c
seq.c: In function ‘main’:
seq.c:11:16: warning: operation on ‘i’ may be undefined [-Wsequence-point]
   11 |     show(i++, i++);     /* which i++ runs first? */
      |               ~^~
$ ./seq
a = 2, b = 1
```

gcc on x86-64 evaluated the *right* argument first; another compiler may not. In `f() + g()`, too, either may run first. If order matters, use separate statements.

## `static` functions and linkage

A function's name has **linkage**, which decides who can see it:

| written as | linkage | visible from |
|---|---|---|
| `int percent(int part, int whole)` | external (the default) | every file in the program |
| `static int clamp(int v, int lo, int hi)` | internal | this file only |

Each `.c` file compiles on its own; the **linker** joins them. External names must be unique program-wide, internal ones only per file. Here two files each have a private `clamp`:

`examples/ch04/linkage/percent.c`
```c
/* percent.c - one public function, one private helper. */

/* 'static': only this file can call clamp. */
static int clamp(int v, int lo, int hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

/* No 'static': other files may call percent. */
int percent(int part, int whole)
{
    if (whole <= 0)
        return 0;
    return clamp(part * 100 / whole, 0, 100);
}
```

`examples/ch04/linkage/main.c`
```c
/* main.c - calls a function defined in another file. */
#include <stdio.h>

int percent(int part, int whole);       /* declared here, defined in percent.c */

/* A second private clamp: no clash with the one in percent.c. */
static int clamp(int v, int lo, int hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

int main(void)
{
    int users = 7, max = 5;
    printf("room is %d%% full\n", percent(users, max));
    printf("showing %d of %d users\n", clamp(users, 0, max), users);
    return 0;
}
```

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o room main.c percent.c
$ ./room
room is 100% full
showing 5 of 7 users
$ musl-gcc -std=c99 -Wall -Wextra -pedantic -static -o room-musl main.c percent.c
$ ./room-musl
room is 100% full
showing 5 of 7 users
```

The folder's `Makefile` builds both with `make` and removes them with `make clean` ([Chapter 9](09-preprocessor-and-building.md)).

Remove `static` from both `clamp`s, and the linker finds two public functions with one name:

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o room main.c percent.c
/usr/bin/ld: /tmp/ccMMr08i.o: in function `clamp':
percent.c:(.text+0x0): multiple definition of `clamp'; /tmp/cc3gn5qu.o:main.c:(.text+0x0): first defined here
collect2: error: ld returned 1 exit status
```

Put the `static`s back and try to borrow the private `clamp` from another file:

`borrow.c`
```c
#include <stdio.h>

int clamp(int v, int lo, int hi);       /* try to borrow percent.c's helper */

int main(void)
{
    printf("%d\n", clamp(7, 0, 5));
    return 0;
}
```

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o borrow borrow.c percent.c
/usr/bin/ld: /tmp/ccykwUSd.o: in function `main':
borrow.c:(.text+0x18): undefined reference to `clamp'
collect2: error: ld returned 1 exit status
```

(The `/tmp/cc….o` temporary object names change every run.) Both errors come from `ld`, the linker; each file compiled fine alone.

**Make every function `static` unless another file calls it.** Private names can't clash with other files or libraries, a reader knows every caller is in this file, and the compiler can inline or drop the function and warns if it's unused.

File-scope variables work the same: a `static` one lives for the whole run and is private to the file. The project uses both.

## Recursion and the call stack

A function that calls itself is **recursive**. It needs a **base case** that stops it, and each call must move toward it:

`examples/ch04/recursion.c`
```c
/* recursion.c - a function that calls itself, and the call stack. */
#include <stdio.h>

static unsigned long long factorial(unsigned n)
{
    if (n <= 1)
        return 1;                       /* base case: stops the recursion */
    return n * factorial(n - 1);        /* recursive case: a smaller problem */
}

/* Each call gets its own 'n' and 'depth' in its own stack frame. */
static void count_down(int n, int depth)
{
    printf("%*senter count_down(%d)\n", depth * 2, "", n);
    if (n > 0)
        count_down(n - 1, depth + 1);
    printf("%*sleave count_down(%d)\n", depth * 2, "", n);
}

int main(void)
{
    for (unsigned i = 0; i <= 20; i += 5)
        printf("%2u! = %llu\n", i, factorial(i));
    count_down(2, 0);
    return 0;
}
```

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o recursion recursion.c
$ ./recursion
 0! = 1
 5! = 120
10! = 3628800
15! = 1307674368000
20! = 2432902008176640000
enter count_down(2)
  enter count_down(1)
    enter count_down(0)
    leave count_down(0)
  leave count_down(1)
leave count_down(2)
```

(`%*s` with `depth * 2, ""` pads an empty string to `depth * 2` spaces: an easy indent.)

Each call gets a **stack frame** with its parameters, locals and return address, pushed on call and popped on return. So each `count_down` has its own `n`, the `leave` lines come out reversed, and locals vanish on return.

The stack is small and fixed: on Linux the main thread gets 8 MiB by default (`ulimit -s` shows KiB). Recursion without a base case runs out:

`examples/ch04/overflow.c`
```c
/* overflow.c - recursion without a base case runs out of stack. */
#include <stdio.h>

static void dive(long depth)
{
    char frame[64];                     /* make each stack frame bigger */
    frame[0] = (char)depth;
    if (depth % 20000 == 0)
        fprintf(stderr, "depth %ld\n", depth);  /* stderr: printed at once */
    dive(depth + 1);                    /* no base case: never stops */
    (void)frame;
}

int main(void)
{
    dive(1);
    return 0;
}
```

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o overflow overflow.c
$ ./overflow
depth 20000
depth 40000
depth 60000
Segmentation fault (core dumped)
$ echo $?
139
$ ulimit -s
8192
```

The next frame hits an unmapped guard page, and the kernel kills the process with `SIGSEGV` (signal 11); the shell reports 128 + 11 = 139. musl dies at the same depth, since the kernel sizes the main thread's stack. (Threads differ: [Chapter 17](17-threads.md).)

Recurse for naturally recursive problems (trees, nested data, divide and conquer), where it's often clearer. Use a loop when depth depends on input, such as network data: a hostile client could crash the server.

## `inline` functions

`inline` asks the compiler to paste a tiny function's body into each call, saving the call. It's only a hint: `-O2` inlines small `static` functions anyway; `-O0` inlines nothing.

`examples/ch04/inline.c`
```c
/* inline.c - small helpers the compiler may paste into the caller. */
#include <stdio.h>

/* 'static inline': private to this file, and a hint to inline it.
 * If the compiler doesn't inline a call, it emits a private copy. */
static inline int max_int(int a, int b)
{
    return a > b ? a : b;
}

int main(void)
{
    printf("max_int(3, 7) = %d\n", max_int(3, 7));
    return 0;
}
```

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o inline inline.c
$ ./inline
max_int(3, 7) = 7
```

Always write `static inline`. In C99, a plain `inline` definition is *not* a real definition for the linker: if the compiler doesn't inline a call, some file must supply a real copy. Drop the `static` and build without optimisation:

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o inline inline.c
/usr/bin/ld: /tmp/ccMFhyeE.o: in function `main':
inline.c:(.text+0x13): undefined reference to `max_int'
collect2: error: ld returned 1 exit status
$ gcc -std=c99 -Wall -Wextra -pedantic -O2 -o inline inline.c
$ ./inline
max_int(3, 7) = 7
```

musl-gcc fails the same way; this is a compiler and linker rule. Code that builds at `-O2` but not `-O0` is a trap. With `static inline`, a call that isn't inlined gets a private copy. (gcc's older `gnu89` mode had the opposite rule, so old code and blog posts disagree.)

## Variadic functions: `<stdarg.h>`

Like `printf`, a **variadic** function takes any number of arguments. Its prototype ends with `...` after at least one named parameter, and `<stdarg.h>` gives a cursor over the rest:

| macro | does |
|---|---|
| `va_list ap;` | declares the cursor |
| `va_start(ap, last)` | starts it after `last`, the last named parameter |
| `va_arg(ap, type)` | returns the next argument, read as `type` |
| `va_end(ap)` | cleans up; required before returning |
| `va_copy(dst, src)` | makes a second cursor at the same spot (C99) |

`examples/ch04/variadic.c`
```c
/* variadic.c - functions that take a variable number of arguments. */
#include <stdarg.h>
#include <stdio.h>

/* The first parameter says how many ints follow. */
static int sum_ints(int count, ...)
{
    va_list ap;
    va_start(ap, count);            /* start after the last named parameter */
    int total = 0;
    for (int i = 0; i < count; i++)
        total += va_arg(ap, int);   /* fetch the next argument as an int */
    va_end(ap);
    return total;
}

/* A list of strings that ends with a null pointer (a "sentinel"). */
static void print_words(const char *first, ...)
{
    va_list ap;
    va_start(ap, first);
    for (const char *w = first; w != NULL; w = va_arg(ap, const char *))
        printf("[%s]", w);
    va_end(ap);
    printf("\n");
}

/* A printf-like function: hand the va_list to vfprintf. */
static void warn(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "variadic: warning: ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
}

int main(void)
{
    printf("%d\n", sum_ints(3, 10, 20, 12));
    printf("%d\n", sum_ints(0));
    print_words("alice", "bob", "carol", (const char *)NULL);
    warn("%d of %d users are %s", 2, 3, "idle");
    return 0;
}
```

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o variadic variadic.c
$ ./variadic
42
0
[alice][bob][carol]
variadic: warning: 2 of 3 users are idle
```

A variadic function can't know how many arguments it got, or their types. The caller says so with:

- a count (`sum_ints`);
- a sentinel at the end (`print_words`); cast `NULL` to the pointer type, since a bare `NULL` may be an `int` 0;
- a format string (`printf`).

Get it wrong and the result is undefined behaviour; the compiler can't help. (It does check `printf`'s own arguments against the format with `-Wformat`, part of `-Wall`.)

`warn` shows the most useful pattern. Each `printf`-family function has a `v` twin taking a `va_list`: `vprintf`, `vfprintf`, `vsnprintf`. You can't forward `...`, but you can forward a `va_list`, as the project's logger does.

### `va_copy`: using the arguments twice

`vfprintf` uses up the `va_list` you pass it. To walk the arguments twice, copy it first:

`examples/ch04/vacopy.c`
```c
/* vacopy.c - using one va_list twice needs va_copy. */
#include <stdarg.h>
#include <stdio.h>

/* Write the same formatted line to two streams. */
static void vtee(FILE *a, FILE *b, const char *fmt, va_list ap)
{
    va_list ap2;
    va_copy(ap2, ap);               /* a second, independent cursor */
    vfprintf(a, fmt, ap);           /* uses up ap */
    vfprintf(b, fmt, ap2);          /* uses the copy */
    va_end(ap2);                    /* every va_copy needs a va_end */
}

static void tee(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vtee(stdout, stderr, fmt, ap);
    va_end(ap);
}

int main(void)
{
    tee("%s has %d unread messages\n", "alice", 3);
    return 0;
}
```

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o vacopy vacopy.c
$ ./vacopy
alice has 3 unread messages
alice has 3 unread messages
$ ./vacopy 2>/dev/null
alice has 3 unread messages
```

Reusing `ap` after `vfprintf` without the C99 `va_copy` is undefined behaviour that works on some platforms, so the bug can hide for years.

### Gotcha: small types are promoted

Arguments in the `...` part get the **default argument promotions**: `char` and `short` become `int`, `float` becomes `double`. So `va_arg(ap, char)` is wrong:

`vachar.c`
```c
#include <stdarg.h>
#include <stdio.h>

static void show_chars(int count, ...)
{
    va_list ap;
    va_start(ap, count);
    for (int i = 0; i < count; i++)
        putchar(va_arg(ap, char));
    va_end(ap);
}

int main(void)
{
    show_chars(2, 'h', 'i');
    return 0;
}
```

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o vachar vachar.c
In file included from vachar.c:1:
vachar.c: In function ‘show_chars’:
vachar.c:9:28: warning: ‘char’ is promoted to ‘int’ when passed through ‘...’
    9 |         putchar(va_arg(ap, char));
      |                            ^
vachar.c:9:28: note: (so you should pass ‘int’ not ‘char’ to ‘va_arg’)
vachar.c:9:28: note: if this code is reached, the program will abort
$ ./vachar
Illegal instruction (core dumped)
```

gcc compiled the undefined `va_arg` into a trap instruction. Read `int` (or `double` for floats), then convert.

## `main(int argc, char *argv[])`

`main` has two standard forms, `int main(void)` and `int main(int argc, char *argv[])`. The second gets the command line:

- `argc` counts the words, including the program name.
- `argv[0]` is the program name as typed; `argv[1]` to `argv[argc - 1]` are the arguments.
- `argv[argc]` is always a null pointer.
- `char *argv[]` and `char **argv` are the same in a parameter list ([Chapter 5](05-pointers-and-arrays.md) explains why).

`examples/ch04/args.c`
```c
/* args.c - main's parameters and its return value. */
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)     /* same as: char *argv[] */
{
    if (argc < 2) {
        fprintf(stderr, "usage: %s WORD...\n", argv[0]);
        return EXIT_FAILURE;        /* 1 on Linux: "something went wrong" */
    }
    printf("argc = %d\n", argc);
    for (int i = 0; i < argc; i++)
        printf("argv[%d] = \"%s\"\n", i, argv[i]);
    printf("argv[argc] is %s\n", argv[argc] == NULL ? "NULL" : "not NULL");
    return EXIT_SUCCESS;            /* 0 */
}
```

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o args args.c
$ ./args
usage: ./args WORD...
$ echo $?
1
$ ./args hello "two words" '*'
argc = 4
argv[0] = "./args"
argv[1] = "hello"
argv[2] = "two words"
argv[3] = "*"
argv[argc] is NULL
```

The shell splits the line into words and strips quotes before your program starts. Unquoted, `*` would expand to every file name in the folder.

`main`'s return value is the exit status ([Chapter 1](01-getting-started.md)); `EXIT_SUCCESS` and `EXIT_FAILURE` (`<stdlib.h>`) are the portable names. `exit(status)`, also in `<stdlib.h>`, flushes stdio buffers and ends the program from *any* function, as if `main` returned `status`. The project's `usage_error` uses it deep inside the option parser.

## Putting it together: `mywc`

A small `wc` clone, one named job per function:

`examples/ch04/mywc.c`
```c
/* mywc.c - count lines, words and bytes on stdin, like wc. */
#include <stdbool.h>
#include <stdio.h>

/* File-scope counters. 'static' keeps them private to this file. */
static long lines, words, bytes;

static bool is_space(int c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' || c == '\r';
}

/* Read stdin to the end, updating the counters. */
static void count_input(void)
{
    bool in_word = false;
    int c;                              /* int, not char: EOF is not a char */
    while ((c = getchar()) != EOF) {
        bytes++;
        if (c == '\n')
            lines++;
        if (is_space(c)) {
            in_word = false;
        } else if (!in_word) {          /* first letter of a new word */
            in_word = true;
            words++;
        }
    }
}

static void print_counts(void)
{
    printf("%7ld %7ld %7ld\n", lines, words, bytes);
}

int main(void)
{
    count_input();
    print_counts();
    return 0;
}
```

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o mywc mywc.c
$ printf 'hello world\nbye' | ./mywc
      1       3      15
$ printf 'hello world\nbye' | wc
      1       3      15
$ musl-gcc -std=c99 -Wall -Wextra -pedantic -static -o mywc-musl mywc.c
$ ./mywc-musl < mywc.c
     41     152     971
```

- `getchar()` returns the next byte as an `int`, or negative `EOF` at the end. `c` must be an `int` to tell `EOF` from byte 255.
- File-scope variables start at zero, so the counters need no initializer.
- File-scope state suits a small file. In bigger programs, pass values in and return them: globals make functions harder to test and reuse. [Chapter 7](07-structs-unions-enums.md)'s structs help.

## Functions outside C99: feature-test macros

Your C library offers more than C99: POSIX functions (`getopt`, `strdup`, `fileno`, sockets) and extras. `-std=c99` asks for *strict* C99, so headers hide names C99 doesn't define, to avoid clashes with yours (each libc hides different amounts; see below). To get POSIX back, define a **feature-test macro** before the first `#include`:

`examples/ch04/posix-hidden.c`
```c
/* posix-hidden.c - POSIX functions need _POSIX_C_SOURCE under -std=c99. */
#define _POSIX_C_SOURCE 200809L     /* must come before every #include */
#include <stdio.h>
#include <string.h>                 /* strdup: POSIX, not C99 */
#include <unistd.h>                 /* getopt: POSIX, not C99 */

int main(int argc, char **argv)
{
    int opt;
    while ((opt = getopt(argc, argv, "v")) != -1)
        printf("option -%c\n", opt);
    char *copy = strdup("a copy of a string");
    printf("%s\n", copy);
    return 0;                       /* (the copy should be freed: Chapter 8) */
}
```

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o posix-hidden posix-hidden.c
$ ./posix-hidden -v
option -v
a copy of a string
$ musl-gcc -std=c99 -Wall -Wextra -pedantic -static -o posix-hidden-musl posix-hidden.c
$ ./posix-hidden-musl -v
option -v
a copy of a string
```

`200809L` means POSIX.1-2008, this course's version. `getopt` parses options; `strdup` copies a string into new memory. Later chapters use both.

### glibc vs musl: hidden functions

Delete the `#define` line and compile again. The libcs hide different things:

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o posix-hidden posix-hidden.c
posix-hidden.c: In function ‘main’:
posix-hidden.c:9:19: warning: implicit declaration of function ‘getopt’ [-Wimplicit-function-declaration]
    9 |     while ((opt = getopt(argc, argv, "v")) != -1)
      |                   ^~~~~~
posix-hidden.c:11:18: warning: implicit declaration of function ‘strdup’; did you mean ‘strcmp’? [-Wimplicit-function-declaration]
   11 |     char *copy = strdup("a copy of a string");
      |                  ^~~~~~
      |                  strcmp
posix-hidden.c:11:18: warning: initialization of ‘char *’ from ‘int’ makes pointer from integer without a cast [-Wint-conversion]
$ ./posix-hidden -v
option -v
Segmentation fault (core dumped)
$ musl-gcc -std=c99 -Wall -Wextra -pedantic -static -o posix-hidden-musl posix-hidden.c
posix-hidden.c: In function ‘main’:
posix-hidden.c:11:18: warning: implicit declaration of function ‘strdup’; did you mean ‘strcmp’? [-Wimplicit-function-declaration]
   11 |     char *copy = strdup("a copy of a string");
      |                  ^~~~~~
      |                  strcmp
posix-hidden.c:11:18: warning: initialization of ‘char *’ from ‘int’ makes pointer from integer without a cast [-Wint-conversion]
$ ./posix-hidden-musl -v
option -v
a copy of a string
```

| | glibc 2.35 | musl 1.2.6 |
|---|---|---|
| `getopt` under plain `-std=c99` | hidden | visible (`<unistd.h>` is a POSIX header anyway) |
| `strdup` under plain `-std=c99` | hidden | hidden |
| the program | crashes | works, by luck |

Both builds have the same bug. Without a declaration, the compiler assumed `strdup` returns `int`, so only the low 32 bits of the real 64-bit pointer survived:

- glibc builds are position-independent executables (PIE) by default, with the heap high up, e.g. `0x5690006eb2a0`. The cut pointer points at nothing, and `printf` crashes.
- The static musl binary isn't PIE; its heap is low, e.g. `0x10411000`, which fits in an `int`. It works by luck, until the address grows.

**glibc is better here**: hiding more turns the bug into an instant crash, while musl's luck hides it. Both need the feature-test macro. The portable rules:

- Put `#define _POSIX_C_SOURCE 200809L` atop every file using POSIX (or pass `-D_POSIX_C_SOURCE=200809L`). From [Chapter 9](09-preprocessor-and-building.md), the project's Makefile does this.
- Avoid `_GNU_SOURCE` unless you need a GNU extension: it unlocks glibc-only names that may not build on musl.
- Never ignore an `implicit declaration` warning.

## Building parley: functions and a logger

At the end of [Chapter 3](03-control-flow.md), each program was one long `main` with a `goto` to a shared error exit. Now split both into functions:

| function | job |
|---|---|
| `parse_args(argc, argv)` | the option loop; stores results in `static` `opt_*` variables |
| `parse_number(text, min, max)` | `strtol` plus range checks; returns -1 on bad input |
| `print_help()` | the `--help` text |
| `usage_error(fmt, ...)` | prints a printf-style message and the "Try --help" hint, then `exit(2)` |
| `log_msg(level, fmt, ...)` (server only) | prints `parleyd: LEVEL: message` to stderr; debug lines need `-v` |

Design choices:

- **Every function except `main` is `static`**: nothing else links with these files.
- **Definitions come before use**, `main` last, so no prototypes.
- **Options are file-scope `static` variables** (`opt_port`, …), filled by `parse_args` and read later. Fine for settings written once at start-up; [Chapter 7](07-structs-unions-enums.md) gathers them into a struct.
- **`usage_error` calls `exit(2)`**, replacing Chapter 3's `goto usage_error`: any function can stop with status 2, Unix's "wrong usage".
- **`log_msg` writes to stderr**, keeping stdout for real output (later, protocol lines). `-v` turns on debug lines without changing any call.
- **`parse_number` returns -1 for "invalid"**, which works only because every valid value here is at least 1. [Chapter 6](06-strings.md) replaces it with a cleaner pattern.

### parleyd.c

`enum log_level` names the levels. `level_name` turns one into text with a `switch` that returns from each `case`. It has no `default:` on purpose: add a level, forget its case, and `-Wall` (`-Wswitch`) warns. The `return "?"` after the switch only catches an invalid number.

Replace `project/ch03/parleyd.c` with this complete file:

`project/ch04/parleyd.c`
```c
/* parleyd.c - the parley chat server (Chapter 4: functions and logging). */
#include <errno.h>
#include <stdarg.h>
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

enum log_level { LOG_DEBUG, LOG_INFO, LOG_WARN, LOG_ERROR };

/* Settings from the command line. 'static' at file scope means "private to
 * this file" (internal linkage): no other .c file can see these names. */
static const char *opt_addr = "127.0.0.1";
static long opt_port = DEFAULT_PORT;
static long opt_max_clients = DEFAULT_MAX_CLIENTS;
static bool opt_verbose = false;

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

/* log_msg(level, format, ...) works like printf, but writes one line to
 * stderr with a "parleyd: level: " prefix. Debug lines need -v. */
static void log_msg(enum log_level level, const char *fmt, ...)
{
    if (level == LOG_DEBUG && !opt_verbose)
        return;

    fprintf(stderr, "parleyd: %s: ", level_name(level));
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);   /* the v-version takes a va_list */
    va_end(ap);
    fputc('\n', stderr);
}

/* Report a command-line mistake and exit with status 2. Never returns. */
static void usage_error(const char *fmt, ...)
{
    fprintf(stderr, "parleyd: ");
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\nTry 'parleyd --help' for more information.\n");
    exit(2);
}

static void print_help(void)
{
    printf("usage: parleyd [-p PORT] [-b ADDR] [-m MAXCLIENTS] [-v]\n"
           "  -p PORT        TCP port to listen on (default %d)\n"
           "  -b ADDR        address to listen on (default 127.0.0.1)\n"
           "  -m MAXCLIENTS  most clients at once, 1-%d (default %d)\n"
           "  -v             verbose (debug) logging\n"
           "  -h, --help     show this help and exit\n"
           "  --version      show the version and exit\n",
           DEFAULT_PORT, MAX_CLIENTS_MAX, DEFAULT_MAX_CLIENTS);
}

/* Convert text to a number in [min, max]. Returns -1 if the text is not a
 * whole number in range (fine as an error value because min >= 0 here). */
static long parse_number(const char *text, long min, long max)
{
    char *end;
    errno = 0;
    long n = strtol(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || n < min || n > max)
        return -1;
    return n;
}

static void parse_args(int argc, char *argv[])
{
    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];

        if (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0) {
            print_help();
            exit(0);
        }
        if (strcmp(arg, "--version") == 0) {
            printf("parleyd 0.1\n");
            exit(0);
        }
        if (arg[0] != '-' || arg[1] == '\0' || arg[2] != '\0')
            usage_error("unexpected argument '%s'", arg);

        char opt = arg[1];
        const char *value = NULL;
        if (opt == 'p' || opt == 'b' || opt == 'm') {
            if (i + 1 >= argc)
                usage_error("option -%c needs a value", opt);
            value = argv[++i];
        }

        switch (opt) {
        case 'b':
            opt_addr = value;
            break;
        case 'p':
            opt_port = parse_number(value, 1, PORT_MAX);
            if (opt_port < 0)
                usage_error("invalid port '%s' (expected 1-%d)", value, PORT_MAX);
            break;
        case 'm':
            opt_max_clients = parse_number(value, 1, MAX_CLIENTS_MAX);
            if (opt_max_clients < 0)
                usage_error("invalid client limit '%s' (expected 1-%d)",
                            value, MAX_CLIENTS_MAX);
            break;
        case 'v':
            opt_verbose = true;
            break;
        default:
            usage_error("unknown option '%s'", arg);
        }
    }
}

int main(int argc, char *argv[])
{
    parse_args(argc, argv);
    log_msg(LOG_DEBUG, "verbose logging is on");
    log_msg(LOG_INFO, "would listen on %s:%ld (max %ld clients)",
            opt_addr, opt_port, opt_max_clients);
    return 0;
}
```

`main` now reads like a summary of the program. The output changed too: "would listen" is now an `info` log line on stderr, and each option has its own error text.

### parley.c

The client gets the same structure, minus the logger. Replace `project/ch03/parley.c` with:

`project/ch04/parley.c`
```c
/* parley.c - the parley chat client (Chapter 4: functions). */
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { DEFAULT_PORT = 6667, PORT_MAX = 65535 };

static const char *opt_host = "localhost";
static long opt_port = DEFAULT_PORT;
static const char *opt_nick = "guest";

static void usage_error(const char *fmt, ...)
{
    fprintf(stderr, "parley: ");
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\nTry 'parley --help' for more information.\n");
    exit(2);
}

static void print_help(void)
{
    printf("usage: parley [-h HOST] [-p PORT] [-n NICK]\n"
           "  -h HOST    server to connect to (default localhost)\n"
           "  -p PORT    server port (default %d)\n"
           "  -n NICK    your nickname (default guest)\n"
           "  --help     show this help and exit\n"
           "  --version  show the version and exit\n",
           DEFAULT_PORT);
}

static long parse_number(const char *text, long min, long max)
{
    char *end;
    errno = 0;
    long n = strtol(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || n < min || n > max)
        return -1;
    return n;
}

static void parse_args(int argc, char *argv[])
{
    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];

        if (strcmp(arg, "--help") == 0) {
            print_help();
            exit(0);
        }
        if (strcmp(arg, "--version") == 0) {
            printf("parley 0.1\n");
            exit(0);
        }
        if (arg[0] != '-' || arg[1] == '\0' || arg[2] != '\0')
            usage_error("unexpected argument '%s'", arg);

        char opt = arg[1];
        if (opt != 'h' && opt != 'p' && opt != 'n')
            usage_error("unknown option '%s'", arg);
        if (i + 1 >= argc)
            usage_error("option -%c needs a value", opt);
        const char *value = argv[++i];

        switch (opt) {
        case 'h':
            opt_host = value;
            break;
        case 'n':
            opt_nick = value;
            break;
        case 'p':
            opt_port = parse_number(value, 1, PORT_MAX);
            if (opt_port < 0)
                usage_error("invalid port '%s' (expected 1-%d)", value, PORT_MAX);
            break;
        }
    }
}

int main(int argc, char *argv[])
{
    parse_args(argc, argv);
    printf("parley: would connect to %s:%ld as %s\n", opt_host, opt_port, opt_nick);
    return 0;
}
```

`parse_number` and `usage_error` now exist in both programs. That duplication is deliberate for now; [Chapter 9](09-preprocessor-and-building.md) moves shared code into its own files.

### Checkpoint

Build both programs with both libcs. The compilers should print nothing:

```console
$ cd project/ch04
$ gcc -std=c99 -Wall -Wextra -pedantic -o parleyd parleyd.c
$ gcc -std=c99 -Wall -Wextra -pedantic -o parley parley.c
$ musl-gcc -std=c99 -Wall -Wextra -pedantic -static -o parleyd-musl parleyd.c
$ musl-gcc -std=c99 -Wall -Wextra -pedantic -static -o parley-musl parley.c
$ ./parleyd -p 6604
parleyd: info: would listen on 127.0.0.1:6604 (max 64 clients)
$ ./parleyd -p 6604 -v
parleyd: debug: verbose logging is on
parleyd: info: would listen on 127.0.0.1:6604 (max 64 clients)
$ ./parleyd -p 6604 -v 2>/dev/null; echo $?
0
$ ./parleyd -p 0; echo $?
parleyd: invalid port '0' (expected 1-65535)
Try 'parleyd --help' for more information.
2
$ ./parleyd -m lots; echo $?
parleyd: invalid client limit 'lots' (expected 1-1000)
Try 'parleyd --help' for more information.
2
$ ./parley -n alice -p 6604
parley: would connect to localhost:6604 as alice
$ ./parleyd-musl -p 6604 -v
parleyd: debug: verbose logging is on
parleyd: info: would listen on 127.0.0.1:6604 (max 64 clients)
$ ./parley-musl -p 99999; echo $?
parley: invalid port '99999' (expected 1-65535)
Try 'parley --help' for more information.
2
```

`2>/dev/null` discards stderr, and nothing is left: every log line goes to stderr. The musl builds behave the same, because this chapter's project uses only C99 functions that agree on both libcs.

The complete project at this point is in `project/ch04/`.

## Summary

- A **prototype** declares name and types; a **definition** adds the body. Declare or define before the first call.
- An `implicit declaration` warning means a missing prototype or `#include`. Treat it as an error.
- In C99, `f()` means "unknown parameters". Write `f(void)`.
- Arguments are copied (**by value**). One return value; falling off the end of a non-void function (except `main`) is undefined behaviour.
- Argument evaluation order is unspecified.
- `static` functions and file-scope variables have **internal linkage**. Make everything `static` unless another file needs it.
- Each call gets a **stack frame**; endless recursion overflows the 8 MiB stack with `SIGSEGV`.
- Write `static inline`, not plain `inline`, which can fail to link at `-O0`.
- `<stdarg.h>`: `va_start`, `va_arg`, `va_end`, `va_copy`. The caller must convey the count; `char`/`short`/`float` arrive as `int`/`double`; forward a `va_list` to `vfprintf` and friends.
- `argv[0]` is the program name; `argv[argc]` is `NULL`. `exit()` ends the program from anywhere.
- Under `-std=c99`, POSIX needs `#define _POSIX_C_SOURCE 200809L`. glibc hides more (`getopt`); forgetting it crashed glibc and worked by luck on musl.

## Exercises

1. **Fast powers.** Write a recursive `unsigned long long power(unsigned long long base, unsigned exp)` that makes fewer than `exp` calls. Hint: `base^exp = (base^(exp/2))^2`, times `base` once more if `exp` is odd. Print `2^10`, `3^5`, `7^0` and `2^63`.

<details><summary>Solution</summary>

```c
#include <stdio.h>

/* base^exp by "exponentiation by squaring": about log2(exp) calls. */
static unsigned long long power(unsigned long long base, unsigned exp)
{
    if (exp == 0)
        return 1;
    unsigned long long half = power(base, exp / 2);
    if (exp % 2 == 0)
        return half * half;
    return half * half * base;
}

int main(void)
{
    printf("2^10 = %llu\n", power(2, 10));
    printf("3^5  = %llu\n", power(3, 5));
    printf("7^0  = %llu\n", power(7, 0));
    printf("2^63 = %llu\n", power(2, 63));
    return 0;
}
```

```console
$ ./power
2^10 = 1024
3^5  = 243
7^0  = 1
2^63 = 9223372036854775808
```

Each call halves `exp`, so `2^63` takes 7 calls instead of 63. The depth is tiny, so recursion is safe.

</details>

2. **`mywc` with options.** Extend `mywc` to accept `-l`, `-w` and `-c` in any combination, printing only the chosen counts, in the order lines, words, bytes. No options means all three. Any other argument prints `usage: mywc2 [-l] [-w] [-c]` to stderr and exits with 2. Compare strings with `strcmp(a, b) == 0`, as the project does.

<details><summary>Solution</summary>

```c
/* mywc2.c - mywc with -l, -w and -c, like wc. */
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

static long lines, words, bytes;
static bool show_lines, show_words, show_bytes;

static bool is_space(int c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' || c == '\r';
}

static void count_input(void)
{
    bool in_word = false;
    int c;
    while ((c = getchar()) != EOF) {
        bytes++;
        if (c == '\n')
            lines++;
        if (is_space(c)) {
            in_word = false;
        } else if (!in_word) {
            in_word = true;
            words++;
        }
    }
}

static void print_counts(void)
{
    const char *sep = "";               /* nothing before the first number */
    if (show_lines) {
        printf("%s%7ld", sep, lines);
        sep = " ";
    }
    if (show_words) {
        printf("%s%7ld", sep, words);
        sep = " ";
    }
    if (show_bytes)
        printf("%s%7ld", sep, bytes);
    printf("\n");
}

static bool parse_args(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-l") == 0)
            show_lines = true;
        else if (strcmp(argv[i], "-w") == 0)
            show_words = true;
        else if (strcmp(argv[i], "-c") == 0)
            show_bytes = true;
        else
            return false;
    }
    if (!show_lines && !show_words && !show_bytes)
        show_lines = show_words = show_bytes = true;
    return true;
}

int main(int argc, char **argv)
{
    if (!parse_args(argc, argv)) {
        fprintf(stderr, "usage: mywc2 [-l] [-w] [-c]\n");
        return 2;
    }
    count_input();
    print_counts();
    return 0;
}
```

```console
$ printf 'hello world\nbye' | ./mywc2 -w -c
      3      15
$ printf 'hello world\nbye' | wc -w -c
      3      15
$ ./mywc2 -x; echo $?
usage: mywc2 [-l] [-w] [-c]
2
```

`parse_args` returns a `bool` instead of exiting, so `main` decides what failure means.

</details>

3. **`max_of`.** Write a variadic `int max_of(int count, ...)` returning the largest of `count` ints, or `INT_MIN` (`<limits.h>`) when `count` is 0.

<details><summary>Solution</summary>

```c
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>

/* The largest of count ints; INT_MIN if count is 0. */
static int max_of(int count, ...)
{
    va_list ap;
    va_start(ap, count);
    int best = INT_MIN;
    for (int i = 0; i < count; i++) {
        int v = va_arg(ap, int);
        if (v > best)
            best = v;
    }
    va_end(ap);
    return best;
}

int main(void)
{
    printf("%d\n", max_of(4, 3, 17, -2, 9));
    printf("%d\n", max_of(1, -5));
    printf("%d\n", max_of(0));
    return 0;
}
```

```console
$ ./maxof
17
-5
-2147483648
```

</details>

4. **`die`.** Write `void die(const char *fmt, ...)` that prints `die: `, a printf-style message and a newline to stderr, then exits with status 1. Use it in a `main` that wants exactly one argument and greets it.

<details><summary>Solution</summary>

```c
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

/* Print "die: <message>" to stderr and exit with status 1. */
static void die(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "die: ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
    exit(1);
}

int main(int argc, char **argv)
{
    if (argc != 2)
        die("expected 1 argument, got %d", argc - 1);
    printf("hello, %s\n", argv[1]);
    return 0;
}
```

```console
$ ./die
die: expected 1 argument, got 0
$ echo $?
1
$ ./die alice
hello, alice
```

The project gets its own `die` in [Chapter 10](10-errors-and-debugging.md).

</details>

5. **Fixing plain `inline`.** The plain-`inline` version of `inline.c` fails to link at `-O0`. Without adding `static`, make it link by adding one line. Why does that work?

<details><summary>Solution</summary>

Add an `extern` declaration after the definition:

```c
#include <stdio.h>

inline int max_int(int a, int b)
{
    return a > b ? a : b;
}

extern int max_int(int a, int b);   /* this file now provides the real copy */

int main(void)
{
    printf("max_int(3, 7) = %d\n", max_int(3, 7));
    return 0;
}
```

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o inline-extern inline-extern.c
$ ./inline-extern
max_int(3, 7) = 7
```

In C99, if *any* declaration of an inline function in a file says `extern` (or omits `inline`), that file's definition becomes the real, external one. This is how you share an inline function across files: the `inline` definition goes in a header every file includes, and exactly one `.c` file adds the `extern` line, so the program gets one real copy. For a function used in one file, `static inline` is simpler.

</details>

Sources for this chapter: [REFERENCES.md](REFERENCES.md#chapter-4--functions)
