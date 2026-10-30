# Chapter 9 — The Preprocessor and Building

So far every program was one `.c` file built with one `gcc` command. Real programs are many files, glued together in two halves. The **preprocessor** handles `#include`, macros, conditional compilation, feature-test macros and the X-macro trick. **Building** covers translation units, headers, linkage, the linker and `make`. In the project, `parleyd.c` and `parley.c` (one 631 lines, the other 218) become modules under `src/`, the server's command table becomes an X-macro, and a Makefile builds everything with `-Werror` for glibc or musl. It builds on [Chapter 8](08-memory.md)'s input buffer; [Chapter 10](10-errors-and-debugging.md) adds sanitizers and debugging to the Makefile.

## From source to program: four stages

`gcc` runs four tools in a row ([Chapter 1](01-getting-started.md)), and you can stop it after any of them:

| Stage | What it does | Stop here with | Output |
|---|---|---|---|
| preprocess | runs `#include`, `#define`, `#if`; removes comments | `gcc -E` | C text (`.i`) |
| compile | turns C into assembly for one CPU | `gcc -S` | assembly (`.s`) |
| assemble | turns assembly into machine code | `gcc -c` | object file (`.o`) |
| link | joins object files and libraries into a program | (default) | executable |

`macros.c` (explained in the next section) through all four:

```console
$ gcc -std=c99 -E macros.c -o macros.i
$ wc -l macros.i
576 macros.i
$ gcc -std=c99 -S macros.c
$ gcc -std=c99 -c macros.c
$ file macros.o
macros.o: ELF 64-bit LSB relocatable, x86-64, version 1 (SYSV), not stripped
$ nm macros.o
                 U __stack_chk_fail
0000000000000000 b calls
000000000000001f T main
0000000000000000 t next
                 U printf
                 U puts
$ gcc -o macros macros.o
$ ./macros | head -1
sizeof nick = 17
```

The 40-line `macros.c` became 576 lines: `<stdio.h>` and everything it includes. The preprocessor works on text and knows nothing about types or functions; its output is what the compiler proper sees.

One `.c` file after preprocessing is a **translation unit**. The compiler handles one at a time and knows nothing about other `.c` files. That is why headers exist: they repeat the declarations every translation unit needs.

An **object file** (`.o`) holds machine code plus a **symbol table**, which `nm` lists:

| Letter | Meaning | Here |
|---|---|---|
| `T` | a function defined here, visible to other files (external linkage) | `main` |
| `t` | a function defined here, private to this file (`static`, internal linkage) | `next` |
| `b` / `B` | a zero-initialized variable, private / visible (the `.bss` section) | `calls` (`static int calls = 0`) |
| `d` / `D` | an initialized variable, private / visible (`.data`) | — |
| `U` | **undefined**: used here, defined somewhere else | `printf`, `puts`, `__stack_chk_fail` |

The linker must find a definition for every `U`. `printf` and `puts` come from the C library: gcc turned `printf("never printed\n")` into the cheaper `puts("never printed")`, allowed because the output is the same. `__stack_chk_fail` comes from the *stack protector*, a buffer-overflow check Ubuntu's gcc turns on by default.

Two more `gcc -E` options: `-P` drops the `# 1 "/usr/include/stdio.h"` *line markers* (which file and line each piece came from), and `-dM` prints every defined macro instead of the program text. To see what a macro really expands to, grep the output of `gcc -E -P`.

### glibc vs musl: what `#include <stdio.h>` pulls in

```console
$ musl-gcc -std=c99 -E macros.c | wc -l
142
$ gcc -std=c99 -H -fsyntax-only macros.c 2>&1 | grep -c '^\.'
30
$ musl-gcc -std=c99 -H -fsyntax-only macros.c 2>&1 | grep '^\.'
. <musl>/include/stdio.h
.. <musl>/include/features.h
.. <musl>/include/bits/alltypes.h
```

`-H` prints every header opened, one dot per nesting level; `-fsyntax-only` stops after checking the code. `<musl>` stands for your musl install directory.

- **glibc**'s `<stdio.h>` opens 30 headers: `features.h`, `sys/cdefs.h`, `bits/wordsize.h`, `bits/types.h`, `bits/types/struct_FILE.h` and more. Each holds a piece of its support for many CPUs, old binary interfaces, 64-bit `off_t` on 32-bit systems, 64-bit `time_t` and a dozen feature-test macros.
- **musl**'s opens 3: `stdio.h`, `features.h` (40 lines) and `bits/alltypes.h`, the types for this CPU.

**musl is simpler.** To see what a library type or function really looks like, read musl's header: it fits on a screen, where glibc's answer can take five files. Compile time is the same for programs this size.

## `#include` and where headers come from

`#include` pastes another file in place of the line. Two forms:

| Form | Searches | Use for |
|---|---|---|
| `#include <stdio.h>` | the system include directories | the C library, POSIX, installed libraries |
| `#include "count.h"` | the directory of the file that has the `#include` line, then the system directories | your own headers |

`-I dir` adds `dir` to the search, before the system directories: `gcc -I src -c tests/test_msg.c` lets a test say `#include "msg.h"` though `msg.h` lives in `src/` ([Chapter 11](11-testing.md) does this). `gcc -E -v` shows the list:

```console
$ echo | gcc -std=c99 -E -v - 2>&1 | sed -n '/search starts/,/End of/p'
#include "..." search starts here:
#include <...> search starts here:
 /usr/lib/gcc/x86_64-linux-gnu/11/include
 /usr/local/include
 /usr/include/x86_64-linux-gnu
 /usr/include
End of search list.
$ echo | musl-gcc -std=c99 -E -v - 2>&1 | sed -n '/search starts/,/End of/p'
#include "..." search starts here:
#include <...> search starts here:
 <musl>/include
 /usr/lib/gcc/x86_64-linux-gnu/11/include
End of search list.
```

(`-` as the file name means "read from stdin". The empty `"..."` list means only the including file's own directory, since no `-iquote` was given.)

The first gcc directory belongs to the **compiler**, not the C library. Its headers, such as `<stddef.h>`, `<stdarg.h>`, `<stdbool.h>` and `<float.h>`, describe the compiler and CPU. glibc relies on these; musl ships its own, first in its search list.

### glibc vs musl: one include directory

`musl-gcc` is not a separate compiler but a two-line shell script:

```sh
#!/bin/sh
exec "${REALGCC:-gcc}" "$@" -specs "<musl>/lib/musl-gcc.specs"
```

The *specs file* changes the options gcc passes to its tools. It adds `-nostdinc -isystem <musl>/include`, so `/usr/include` (glibc's headers) is never searched, and links with musl's `crt1.o` and `libc.a` instead of glibc's. So compile **and** link with the same driver ([Chapter 1](01-getting-started.md)): an object compiled against glibc's headers has glibc's struct layouts and function names baked in.

**musl is simpler**: one directory, and nothing from glibc can leak in. The catch: libraries your distribution installs in `/usr/include` and `/usr/lib` are built for glibc, and `musl-gcc` can't see them; a musl program needs musl builds of its libraries. parley needs only libc, so this never bites the course.

Rules for `#include`:

- `"..."` for your own headers, `<...>` for everything else.
- Never `#include` a `.c` file. Each `.c` file is compiled on its own and linked; including one compiles its functions twice.
- Order: in `foo.c`, include `"foo.h"` first, so if `foo.h` forgot a header it needs, you find out at once. Then system headers, then your other headers. The project follows this order.

## Macros

`#define` makes a **macro**: a name the preprocessor replaces with text before the compiler sees the code.

### Object-like macros

`#define NAME replacement` replaces every later *token* `NAME` with `replacement`, up to the end of the translation unit. String literals and longer identifiers are left alone: `#define NICK_MAX 16` doesn't touch `"NICK_MAX"` or `NICK_MAXLEN`. A macro has no type and no scope; the ALL_CAPS convention tells readers a name is replaced as text.

When each way to name a constant ([Chapter 2](02-types-and-operators.md)) fits:

| | `#define N 16` | `enum { N = 16 };` | `static const int n = 16;` |
|---|---|---|---|
| array size at file scope, `case` label | yes | yes | **no**: not a constant expression in C |
| type | none (the literal's) | `int` only | any |
| string or floating value | yes | no | yes |
| usable in `#if` | yes | no | no |
| seen by the debugger | no | yes | yes |

The project's headers use `#define` for `MSG_LINE_MAX`, `NICK_MAX`, `SERVER_NAME "parley"` and `PARLEY_VERSION "0.1"`: they size arrays, and two are strings. A string macro can be glued to other literals, since adjacent literals are joined at compile time ([Chapter 6](06-strings.md)):

```c
printf("parley " PARLEY_VERSION "\n");     /* the compiler sees "parley 0.1\n" */
```

The project keeps `enum` for sets of related values such as `enum msg_status` ([Chapter 7](07-structs-unions-enums.md)), where a debugger showing `MSG_EMPTY` instead of `1` helps.

### Function-like macros and their traps

A macro can take parameters: `#define SQUARE(x) ((x) * (x))` turns `SQUARE(n + 1)` into `((n + 1) * (n + 1))`. No space between the name and `(`: `#define SQUARE (x) ...` defines an object-like macro whose text starts with `(x)`.

`examples/ch09/macros.c`
```c
/* macros.c - object-like and function-like macros, and their traps. */
#include <stdio.h>

#define NICK_MAX 16                     /* object-like: a named constant */
#define BAD_SQUARE(x) x * x             /* no parentheses: wrong */
#define SQUARE(x) ((x) * (x))           /* every use of x, and the whole body */
#define MAX(a, b) ((a) > (b) ? (a) : (b))

/* Several statements that act as one: wrap them in do { ... } while (0). */
#define SWAP_INT(a, b) do { int swap_tmp_ = (a); (a) = (b); (b) = swap_tmp_; } while (0)

static int calls = 0;
static int next(void)
{
    return ++calls;
}

int main(void)
{
    char nick[NICK_MAX + 1] = "alice";
    printf("sizeof nick = %zu\n", sizeof nick);

    printf("BAD_SQUARE(1 + 2) = %d\n", BAD_SQUARE(1 + 2));   /* 1 + 2 * 1 + 2 */
    printf("SQUARE(1 + 2)     = %d\n", SQUARE(1 + 2));

    /* A macro argument is pasted in as text, so it runs once per use. */
    int i = 5;
    int m = MAX(i++, 0);                 /* ((i++) > (0) ? (i++) : (0)) */
    printf("MAX(i++, 0) = %d, and i is now %d (not 6)\n", m, i);
    int n = MAX(next(), 0);
    printf("MAX(next(), 0) = %d, next() was called %d times\n", n, calls);

    int a = 1, b = 2;
    if (a < b)
        SWAP_INT(a, b);                  /* one statement, so the else still fits */
    else
        printf("never printed\n");
    printf("after SWAP_INT: a = %d, b = %d\n", a, b);
    return 0;
}
```

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o macros macros.c
$ ./macros
sizeof nick = 17
BAD_SQUARE(1 + 2) = 5
SQUARE(1 + 2)     = 9
MAX(i++, 0) = 6, and i is now 7 (not 6)
MAX(next(), 0) = 2, next() was called 2 times
after SWAP_INT: a = 2, b = 1
$ musl-gcc -std=c99 -Wall -Wextra -pedantic -static -o macros-musl macros.c
$ ./macros-musl
sizeof nick = 17
BAD_SQUARE(1 + 2) = 5
SQUARE(1 + 2)     = 9
MAX(i++, 0) = 6, and i is now 7 (not 6)
MAX(next(), 0) = 2, next() was called 2 times
after SWAP_INT: a = 2, b = 1
```

The preprocessed text shows what happened:

```console
$ gcc -std=c99 -E -P macros.c | grep -E 'nick\[|1 \+ 2|i\+\+|swap_tmp_|next\(\)'
    char nick[16 + 1] = "alice";
    printf("BAD_SQUARE(1 + 2) = %d\n", 1 + 2 * 1 + 2);
    printf("SQUARE(1 + 2)     = %d\n", ((1 + 2) * (1 + 2)));
    int m = ((i++) > (0) ? (i++) : (0));
    printf("MAX(i++, 0) = %d, and i is now %d (not 6)\n", m, i);
    int n = ((next()) > (0) ? (next()) : (0));
    printf("MAX(next(), 0) = %d, next() was called %d times\n", n, calls);
        do { int swap_tmp_ = (a); (a) = (b); (b) = swap_tmp_; } while (0);
```

Three rules follow:

1. **Parenthesize every use of a parameter, and the whole body.** `BAD_SQUARE(1 + 2)` became `1 + 2 * 1 + 2`, which is 5, because `*` binds tighter than `+`. Without the outer parentheses, `100 / SQUARE(2)` would mean `100 / (2) * (2)`.
2. **Never pass an argument with side effects.** The argument is pasted once per use: `MAX(i++, 0)` increments `i` twice, `MAX(next(), 0)` calls `next` twice. The bug is invisible at the call site.
3. **Wrap several statements in `do { ... } while (0)`.** The result is one statement that needs a `;` after it, like a function call.

Plain braces look like enough for rule 3. Change `SWAP_INT` in `macros.c` to `{ int swap_tmp_ = (a); (a) = (b); (b) = swap_tmp_; }` and compile:

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o macros macros.c
macros.c: In function ‘main’:
macros.c:36:5: error: ‘else’ without a previous ‘if’
   36 |     else
      |     ^~~~
```

`SWAP_INT(a, b);` became `{ ... };`: the braces are the `if`'s statement, and the `;` is a second, empty statement that ends the `if`, leaving the `else` with no `if`. With `do { ... } while (0)` the `;` belongs to the `do`.

**Prefer a function.** A `static inline` function ([Chapter 4](04-functions.md)) checks argument types, evaluates each argument once, shows up in the debugger, and with `-O2` is just as fast. Use a function-like macro only when a function can't do the job:

- you need the argument's *text* (`#`, next section);
- you need the *caller's* `__FILE__` and `__LINE__`;
- it must work on any type (`MAX` on `int`, `double` and `size_t`), and you accept rule 2;
- it must declare things or work at compile time (array sizes, X-macros).

### `#`, `##`, predefined macros and variadic macros

In a function-like macro, `#param` turns the argument into a string literal and `a##b` glues two tokens into one. C99 also allows a variable number of arguments.

`examples/ch09/stringify.c`
```c
/* stringify.c - # and ##, predefined macros, and a variadic logging macro. */
#include <stdarg.h>
#include <stdio.h>

#define SHOW(expr) printf("%-14s = %d\n", #expr, (expr))   /* #: text to string */

#define STR(x) #x                       /* stringify the argument as written */
#define XSTR(x) STR(x)                  /* expand the argument first, then stringify */
#define PORT 6667

#define CMD_NAME(name) cmd_##name       /* ##: glue two tokens into one */

/* C99 variadic macro: ... collects the arguments, __VA_ARGS__ pastes them.
 * fmt is part of the ..., so LOG("text") with no other arguments is fine. */
#define LOG(...) log_at(__FILE__, __LINE__, __func__, __VA_ARGS__)

static void log_at(const char *file, int line, const char *func, const char *fmt, ...)
{
    fprintf(stderr, "%s:%d: %s: ", file, line, func);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

static void cmd_ping(void)
{
    LOG("ping handled");
}

int main(void)
{
    SHOW(2 + 3 * 4);
    SHOW(PORT % 1000);

    printf("STR(PORT)  = \"%s\"\n", STR(PORT));
    printf("XSTR(PORT) = \"%s\"\n", XSTR(PORT));

    CMD_NAME(ping)();                   /* calls cmd_ping() */

    LOG("listening on port %d", PORT);
    printf("__STDC_VERSION__ = %ldL\n", (long)__STDC_VERSION__);
    printf("compiled on %s at %s\n", __DATE__, __TIME__);
    return 0;
}
```

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o stringify stringify.c
$ ./stringify
2 + 3 * 4      = 14
PORT % 1000    = 667
STR(PORT)  = "PORT"
XSTR(PORT) = "6667"
stringify.c:29: cmd_ping: ping handled
stringify.c:42: main: listening on port 6667
__STDC_VERSION__ = 199901L
compiled on Oct 30 2026 at 16:27:47
```

The musl build prints the same; your date and time will differ. Line by line:

- **`#expr`** made `"2 + 3 * 4"` from the argument, so `SHOW` prints the code next to its value. [Chapter 11](11-testing.md)'s `CHECK` macro prints failed conditions this way.
- **`STR(PORT)` gives `"PORT"`, `XSTR(PORT)` gives `"6667"`.** Arguments are macro-expanded *before* being pasted into the body, except where the parameter is used with `#` or `##`. In `XSTR`'s body `x` is not next to `#`, so `XSTR(PORT)` becomes `STR(6667)`, then `"6667"`. This two-level trick is the standard way to stringify a macro's *value*.
- **`cmd_##name`** made the identifier `cmd_ping`. The pasted result must be one valid token.
- **`__FILE__` and `__LINE__`** are the file name (as given to the compiler) and line *where `LOG` was used*. A function would always report its own line, which is why logging and test macros are macros.
- **`__func__`** is not a macro (the preprocessor knows nothing about functions). C99 gives every function a hidden `static const char __func__[] = "cmd_ping";`, filled in by the compiler.
- **`__STDC_VERSION__`** is `199901L` under `-std=c99`; gcc 11 with no `-std` gives `201710L` (C17 with GNU extensions). `#if __STDC_VERSION__ >= 199901L` is how a header checks for C99.
- **`__DATE__` and `__TIME__`** are the moment of compilation. They make every build different, which defeats tools that compare builds. Use them sparingly.
- **`...` and `__VA_ARGS__`**: `LOG` passes all its arguments on to `log_at`, which takes them with `<stdarg.h>` ([Chapter 4](04-functions.md)).

At a terminal the `stringify.c:` lines (stderr) appear in program order with the rest (stdout), because both streams flush every line there. Through a pipe stdout is buffered and the order changes ([Chapter 12](12-files-and-descriptors.md)).

#### Gotcha: a variadic macro with zero variable arguments

It is tempting to name the format parameter: change `LOG` in `stringify.c` to `#define LOG(fmt, ...) log_at(__FILE__, __LINE__, __func__, fmt, __VA_ARGS__)`:

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o stringify stringify.c
stringify.c: In function ‘cmd_ping’:
stringify.c:29:23: warning: ISO C99 requires at least one argument for the "..." in a variadic macro
   29 |     LOG("ping handled");
      |                       ^
stringify.c:15:76: error: expected expression before ‘)’ token
   15 | #define LOG(fmt, ...) log_at(__FILE__, __LINE__, __func__, fmt, __VA_ARGS__)
      |                                                                            ^
stringify.c:29:5: note: in expansion of macro ‘LOG’
   29 |     LOG("ping handled");
      |     ^~~
```

`LOG("ping handled")` passes nothing for `...`, so the expansion ends in `fmt, )`. C99 has no fix. GCC's `, ##__VA_ARGS__` swallows the comma, but it is an extension `-pedantic` warns about; C23 adds `__VA_OPT__`. The portable C99 answer is `stringify.c`'s: let `...` include the format string.

### `#undef`, `#pragma` and `_Pragma`

`#undef NAME` forgets a macro. `#pragma` passes an instruction to the compiler. C99's `_Pragma("...")` operator does the same as `#pragma` but can be produced by a macro.

`examples/ch09/pragma.c`
```c
/* pragma.c - #undef, #pragma and the C99 _Pragma operator. */
#include <stdio.h>

/* A GCC pragma: using these names anywhere below is now a compile error. */
#pragma GCC poison gets sprintf

/* _Pragma("text") does what #pragma text does, but it is an operator, so a
 * macro can produce one. Here: a TODO that shows up as a compiler warning. */
#define DO_PRAGMA(x) _Pragma(#x)
#define TODO(text) DO_PRAGMA(GCC warning text)

TODO("check the port range")

#define LIMIT 10
static const int old_limit = LIMIT;
#undef LIMIT                    /* forget the old value ... */
#define LIMIT 20                /* ... before giving a new one */

int main(void)
{
    printf("old_limit = %d, LIMIT = %d\n", old_limit, LIMIT);
    return 0;
}
```

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o pragma pragma.c
pragma.c:12:13: warning: check the port range
   12 | TODO("check the port range")
      |             ^~~~~~~~~~~~~~~~
$ ./pragma
old_limit = 10, LIMIT = 20
```

musl-gcc prints the same, since this is all the compiler's work. Details:

- Add `sprintf(buf, "%d", LIMIT);` to `main` and gcc refuses: `error: attempt to use poisoned "sprintf"`. Poisoning is a cheap way to ban unsafe functions from a project.
- Delete the `#undef` line and gcc warns `"LIMIT" redefined`. Redefining a macro with *different* text is not allowed; repeating the identical definition is fine.
- An unknown pragma is ignored, and `-Wall` warns: `warning: ignoring ‘#pragma parley fast’ [-Wunknown-pragmas]`.
- C99 defines three standard pragmas, all for floating-point math (`#pragma STDC FP_CONTRACT` and friends). The rest, like `GCC poison`, belong to one compiler.
- `#pragma once` atop a header means "include this file only once". gcc and clang support it, but it is not in C99; this course uses standard include guards (below).

## Conditional compilation

`#if`, `#ifdef` and friends keep or drop lines *before* the compiler sees them; dropped lines are never compiled.

`examples/ch09/conditional.c`
```c
/* conditional.c - #if, #ifdef, macros set with -D, and #error. */
#include <stdio.h>

#if !defined(__STDC_VERSION__) || __STDC_VERSION__ < 199901L
#error "this program needs C99 or later: compile with -std=c99"
#endif

#ifndef GREETING_LEVEL
#define GREETING_LEVEL 1        /* the default; override with -DGREETING_LEVEL=2 */
#endif

#ifdef DEBUG
#define DPRINT(...) fprintf(stderr, __VA_ARGS__)
#else
#define DPRINT(...) ((void)0)   /* compiles to nothing at all */
#endif

int main(void)
{
    int users = 3;
    DPRINT("debug: users = %d\n", users);

#if GREETING_LEVEL >= 2
    printf("Hello, and welcome! %d users are here.\n", users);
#elif GREETING_LEVEL == 1
    printf("hello (%d users)\n", users);
#else
    (void)users;
#endif

#ifdef __linux__
    puts("built for Linux");
#endif
    return 0;
}
```

`-D NAME` defines a macro on the command line (as `1`), `-D NAME=value` gives it a value, and `-U NAME` removes one:

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o conditional conditional.c && ./conditional
hello (3 users)
built for Linux
$ gcc -std=c99 -Wall -Wextra -pedantic -DDEBUG -o conditional conditional.c && ./conditional
debug: users = 3
hello (3 users)
built for Linux
$ gcc -std=c99 -Wall -Wextra -pedantic -DGREETING_LEVEL=2 -o conditional conditional.c && ./conditional
Hello, and welcome! 3 users are here.
built for Linux
$ gcc -std=c99 -Wall -Wextra -pedantic -DGREETING_LEVEL=0 -o conditional conditional.c && ./conditional
built for Linux
$ musl-gcc -std=c99 -Wall -Wextra -pedantic -static -DDEBUG -o conditional-musl conditional.c && ./conditional-musl
debug: users = 3
hello (3 users)
built for Linux
$ gcc -std=c89 -Wall -Wextra -pedantic -o conditional conditional.c
conditional.c:5:2: error: #error "this program needs C99 or later: compile with -std=c99"
    5 | #error "this program needs C99 or later: compile with -std=c99"
      |  ^~~~~
conditional.c:15:16: warning: anonymous variadic macros were introduced in C99 [-Wvariadic-macros]
   15 | #define DPRINT(...) ((void)0)   /* compiles to nothing at all */
      |                ^~~
```

| Directive | Keeps the lines if |
|---|---|
| `#ifdef X` / `#ifndef X` | `X` is / is not defined as a macro (its value doesn't matter) |
| `#if expr` | the integer constant expression is not 0 |
| `#elif expr` / `#else` | the earlier branches were false |
| `defined(X)` | inside `#if`: 1 if `X` is defined, else 0. Lets you combine tests with `&&`, `\|\|` and `!` |
| `#error "text"` | (not a test) stops compilation with the message |

`#if` does only integer arithmetic on literals and macros. It can't see variables, `sizeof`, casts or enum constants, which don't exist yet when the preprocessor runs. Any identifier that is not a macro counts as `0`, so a typo like `#if DEUBG` is silently false; `-Wundef` warns about it.

With `DEBUG`, `DPRINT` calls `fprintf`; without, it becomes `((void)0)`, which does nothing. Either way `DPRINT(...);` is a valid statement.

The compiler predefines macros describing the target; `gcc -dM -E` lists them all (364 here):

```console
$ echo | gcc -std=c99 -dM -E - | grep -E '__STDC_VERSION__|__STRICT_ANSI__|__linux__ |__x86_64__ |__GNUC__ |__STDC_HOSTED__'
#define __STDC_HOSTED__ 1
#define __GNUC__ 11
#define __STRICT_ANSI__ 1
#define __x86_64__ 1
#define __STDC_VERSION__ 199901L
#define __linux__ 1
```

Note the underscores in `__linux__`. Plain `linux` is defined under `-std=gnu99` but not `-std=c99`, because it is not a reserved name.

Use conditional compilation sparingly. Code in a branch that is off is never compiled, so it rots unnoticed: rename a function or change a struct field, and the `DEBUG` build breaks months later. Keep `#if` blocks small, and put platform-specific parts in their own functions or files.

## Feature-test macros, properly

[Chapter 4](04-functions.md) showed the problem: under `-std=c99`, glibc hides `getopt` and `strdup`, musl hides `strdup`, and calling a hidden function can crash. The fix is a **feature-test macro**, defined *before the first `#include`*, which tells the libc headers which standards to expose. What each libc does on its own, and with `_POSIX_C_SOURCE`:

`examples/ch09/features.c`
```c
/* features.c - which feature-test macros are in effect, and which libc? */
#include <stdio.h>   /* any libc header sets up the libc's feature macros */

int main(void)
{
#ifdef __STRICT_ANSI__
    puts("__STRICT_ANSI__   defined: strict ISO C, no extensions");
#endif
#ifdef _POSIX_C_SOURCE
    printf("_POSIX_C_SOURCE   %ldL\n", (long)_POSIX_C_SOURCE);
#else
    puts("_POSIX_C_SOURCE   not defined");
#endif
#ifdef _XOPEN_SOURCE
    printf("_XOPEN_SOURCE     %d\n", (int)_XOPEN_SOURCE);
#endif
#ifdef _DEFAULT_SOURCE
    puts("_DEFAULT_SOURCE   defined");
#endif
#ifdef _BSD_SOURCE
    puts("_BSD_SOURCE       defined");
#endif
#ifdef __GLIBC__
    printf("C library         glibc %d.%d\n", __GLIBC__, __GLIBC_MINOR__);
#else
    puts("C library         not glibc (musl defines no macro to say so)");
#endif
    return 0;
}
```

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o features features.c && ./features
__STRICT_ANSI__   defined: strict ISO C, no extensions
_POSIX_C_SOURCE   not defined
C library         glibc 2.35
$ gcc -std=c99 -Wall -Wextra -pedantic -D_POSIX_C_SOURCE=200809L -o features features.c && ./features
__STRICT_ANSI__   defined: strict ISO C, no extensions
_POSIX_C_SOURCE   200809L
C library         glibc 2.35
$ gcc -std=gnu99 -Wall -Wextra -pedantic -o features features.c && ./features
_POSIX_C_SOURCE   200809L
_DEFAULT_SOURCE   defined
C library         glibc 2.35
$ musl-gcc -static -std=c99 -Wall -Wextra -pedantic -o features features.c && ./features
__STRICT_ANSI__   defined: strict ISO C, no extensions
_POSIX_C_SOURCE   not defined
C library         not glibc (musl defines no macro to say so)
$ musl-gcc -static -std=c99 -Wall -Wextra -pedantic -D_POSIX_C_SOURCE=200809L -o features features.c && ./features
__STRICT_ANSI__   defined: strict ISO C, no extensions
_POSIX_C_SOURCE   200809L
C library         not glibc (musl defines no macro to say so)
$ musl-gcc -static -std=gnu99 -Wall -Wextra -pedantic -o features features.c && ./features
_POSIX_C_SOURCE   not defined
_XOPEN_SOURCE     700
_BSD_SOURCE       defined
C library         not glibc (musl defines no macro to say so)
```

`-std=c99` makes gcc define `__STRICT_ANSI__`. Every libc header includes the libc's `<features.h>`, which looks at `__STRICT_ANSI__` and any feature-test macros you defined and decides what to declare. The macros you will meet:

| Macro | Asks for |
|---|---|
| `_POSIX_C_SOURCE 200809L` | ISO C plus POSIX.1-2008. **This course's choice.** |
| `_XOPEN_SOURCE 700` | POSIX.1-2008 plus the X/Open System Interfaces (XSI) option: a few more functions such as `strptime` |
| `_DEFAULT_SOURCE` (glibc), `_BSD_SOURCE` (musl) | POSIX plus traditional BSD and System V extras |
| `_GNU_SOURCE` | everything the libc has, including GNU-only functions |

### glibc vs musl: default feature macros

| Build | glibc 2.35 | musl 1.2.6 |
|---|---|---|
| `-std=c99` | ISO C headers: ISO C only. POSIX headers: only partly (`read`, `poll` yes; `getopt`, `getaddrinfo` no) | ISO C headers: ISO C only. POSIX headers (`<unistd.h>`, `<netdb.h>`, `<poll.h>`, …): everything |
| `-std=c99 -D_POSIX_C_SOURCE=200809L` | POSIX.1-2008 | POSIX.1-2008 |
| `-std=gnu99`, or no `-std` | `_DEFAULT_SOURCE` → POSIX.1-2008 + BSD/SVID extras | `_BSD_SOURCE` + `_XOPEN_SOURCE 700` |
| identify the libc | `__GLIBC__`, `__GLIBC_MINOR__` (after any `#include`) | nothing, on purpose |

Under plain `-std=c99`, both libcs hide POSIX extras in ISO C headers (`strdup` in `<string.h>`, `fileno` in `<stdio.h>`, `sigaction` in `<signal.h>`). musl declares everything in headers C99 doesn't have (such a header is only ever included for POSIX); glibc still hides parts. With `-D_POSIX_C_SOURCE=200809L`, both libcs expose the same POSIX interface: the portable setting. The defaults differ, which is why `gcc foo.c` "just works" and `gcc -std=c99 foo.c` suddenly can't find `strdup`. Defaults change behaviour as well as visibility: [Chapter 13](13-processes-and-signals.md) shows glibc's `signal()` depending on `_DEFAULT_SOURCE`.

musl's whole `<features.h>` is 40 lines. The default case:

```c
#if !defined(_POSIX_SOURCE) && !defined(_POSIX_C_SOURCE) \
 && !defined(_XOPEN_SOURCE) && !defined(_GNU_SOURCE) \
 && !defined(_BSD_SOURCE) && !defined(__STRICT_ANSI__)
#define _BSD_SOURCE 1
#define _XOPEN_SOURCE 700
#endif
```

glibc's is 513 lines and handles about twenty macros (`_ISOC99_SOURCE`, `_LARGEFILE64_SOURCE`, `_FILE_OFFSET_BITS`, `_FORTIFY_SOURCE`, …), their historical values and interactions.

musl deliberately has no `__MUSL__` macro (its FAQ says so): code should test whether a *feature* exists, not which library it runs on, and "if not glibc, then …" breaks on the next libc. **musl is simpler here**: you can read the whole feature logic in a minute, and you can't write libc-specific `#ifdef`s by accident. glibc's extra knobs only help if you need GNU extensions, and this course doesn't.

Rules for the course, and for portable code:

- Define `_POSIX_C_SOURCE=200809L` **for every file, on the command line**: `-D_POSIX_C_SOURCE=200809L` in the Makefile's `CPPFLAGS`. (This replaces the per-file `#define` of [Chapter 4](04-functions.md).) A `#define` in the file works only before *every* `#include`; one line too low and it silently does nothing, because `<features.h>` has already run.
- Don't "fix" a missing declaration with `-std=gnu99` or `_GNU_SOURCE`. That turns on glibc-only names, and the code may then fail to build, or behave differently, on musl.
- Compile with `-Werror`, or at least treat `implicit declaration of function` as an error. An implicitly declared function returns `int`; if the real one returns a pointer, the program links and then crashes ([Chapter 4](04-functions.md)).

## X-macros: one list, many expansions

Often several things must stay in step: an enum of reply codes and tables of their numbers, texts and names (for debugging). Add a reply to three and forget the fourth, and you get a subtle bug. An **X-macro** keeps one list and generates the rest:

`examples/ch09/xmacro.c`
```c
/* xmacro.c - one list of replies, expanded four different ways. */
#include <stdio.h>

/* The list. Each line is X(name, code, text); X is chosen by the user. */
#define REPLIES(X)                                          \
    X(RPL_WELCOME,          "001", "Welcome")               \
    X(ERR_NONICKNAMEGIVEN,  "431", "No nickname given")     \
    X(ERR_ERRONEUSNICKNAME, "432", "Erroneous nickname")    \
    X(ERR_NOTREGISTERED,    "451", "You have not registered")

#define AS_ENUM(name, code, text) name,
enum reply { REPLIES(AS_ENUM) REPLY_COUNT };
#undef AS_ENUM

#define AS_CODE(name, code, text) code,
static const char *const reply_code[] = { REPLIES(AS_CODE) };
#undef AS_CODE

#define AS_TEXT(name, code, text) text,
static const char *const reply_text[] = { REPLIES(AS_TEXT) };
#undef AS_TEXT

#define AS_NAME(name, code, text) #name,
static const char *const reply_name[] = { REPLIES(AS_NAME) };
#undef AS_NAME

int main(void)
{
    for (int r = 0; r < REPLY_COUNT; r++)
        printf("%-20s %s :%s\n", reply_name[r], reply_code[r], reply_text[r]);
    printf("ERR_NOTREGISTERED = %d, REPLY_COUNT = %d\n", ERR_NOTREGISTERED, REPLY_COUNT);
    return 0;
}
```

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o xmacro xmacro.c && ./xmacro
RPL_WELCOME          001 :Welcome
ERR_NONICKNAMEGIVEN  431 :No nickname given
ERR_ERRONEUSNICKNAME 432 :Erroneous nickname
ERR_NOTREGISTERED    451 :You have not registered
ERR_NOTREGISTERED = 3, REPLY_COUNT = 4
$ gcc -std=c99 -E -P xmacro.c | grep -E '^enum reply|reply_code\[\]|reply_name\[\]'
enum reply { RPL_WELCOME, ERR_NONICKNAMEGIVEN, ERR_ERRONEUSNICKNAME, ERR_NOTREGISTERED, REPLY_COUNT };
static const char *const reply_code[] = { "001", "431", "432", "451", };
static const char *const reply_name[] = { "RPL_WELCOME", "ERR_NONICKNAMEGIVEN", "ERR_ERRONEUSNICKNAME", "ERR_NOTREGISTERED", };
```

The musl build prints the same. How it works:

- `REPLIES(X)` takes a macro *name* as its argument and calls it once per line. Each `AS_...` macro picks the fields it needs and adds a comma.
- A backslash at the very end of a line continues the macro on the next line. Nothing may follow it, not even a space.
- The trailing comma in `{ "001", ..., "451", }` is allowed in an initializer, and since C99 in an enum.
- `REPLY_COUNT` after the list is one more than the last reply: a free count, always up to date.
- `#undef` after each use lets the next expansion reuse the name and keeps the helpers from leaking.

Adding a reply is now one line in one place. The parley server's command table below is an X-macro, and [Chapter 11](11-testing.md) uses one for its list of tests.

## Many files: headers, linkage and the linker

[Chapter 4](04-functions.md) built `mywc`, a small `wc`, as one file. Here it is split the way real programs are: a **module** `count` that counts, and `main.c` that handles files and printing. A module is a header (`count.h`, the **interface**: what other files may use) plus a source file (`count.c`, the **implementation**).

`examples/ch09/mywc/count.h`
```c
/* count.h - counting lines, words and bytes: the interface of count.c. */
#ifndef MYWC_COUNT_H
#define MYWC_COUNT_H

#include <stdio.h>

struct counts {
    long lines;
    long words;
    long bytes;
};

/* Read f to the end and add what it contains to *c. */
void count_stream(FILE *f, struct counts *c);

/* Add the counts in *from to *to. */
void count_add(struct counts *to, const struct counts *from);

/* How many streams count_stream has read. Defined in count.c. */
extern int count_streams;

#endif
```

`examples/ch09/mywc/count.c`
```c
/* count.c - counting lines, words and bytes. */
#include "count.h"

#include <stdbool.h>

int count_streams = 0;                  /* the one definition */

/* Private to this file: no other file can call it or clash with it. */
static bool is_space(int c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' || c == '\r';
}

void count_stream(FILE *f, struct counts *c)
{
    bool in_word = false;
    int ch;
    while ((ch = getc(f)) != EOF) {
        c->bytes++;
        if (ch == '\n')
            c->lines++;
        if (is_space(ch)) {
            in_word = false;
        } else if (!in_word) {
            in_word = true;
            c->words++;
        }
    }
    count_streams++;
}

void count_add(struct counts *to, const struct counts *from)
{
    to->lines += from->lines;
    to->words += from->words;
    to->bytes += from->bytes;
}
```

`examples/ch09/mywc/main.c`
```c
/* main.c - mywc: count lines, words and bytes in files, or in stdin. */
#include <stdio.h>

#include "count.h"

static void print_counts(const struct counts *c, const char *name)
{
    printf("%7ld %7ld %7ld%s%s\n", c->lines, c->words, c->bytes,
           name[0] != '\0' ? " " : "", name);
}

int main(int argc, char *argv[])
{
    struct counts total = { 0, 0, 0 };
    if (argc < 2) {
        count_stream(stdin, &total);
        print_counts(&total, "");
        return 0;
    }

    int status = 0;
    for (int i = 1; i < argc; i++) {
        FILE *f = fopen(argv[i], "r");
        if (f == NULL) {
            perror(argv[i]);
            status = 1;
            continue;
        }
        struct counts c = { 0, 0, 0 };
        count_stream(f, &c);
        fclose(f);
        print_counts(&c, argv[i]);
        count_add(&total, &c);
    }
    if (count_streams > 1)
        print_counts(&total, "total");
    return status;
}
```

(`fopen`, `perror` and `getc` open a file, report why that failed, and read a byte; the full story is in [Chapter 10](10-errors-and-debugging.md) and [Chapter 12](12-files-and-descriptors.md).)

Compile each `.c` file to an object with `-c`, then link the objects:

```console
$ cd examples/ch09/mywc
$ gcc -std=c99 -Wall -Wextra -pedantic -c main.c
$ gcc -std=c99 -Wall -Wextra -pedantic -c count.c
$ gcc -o mywc main.o count.o
$ ./mywc count.c main.c
     37     133     872 count.c
     38     128     938 main.c
     75     261    1810 total
$ wc count.c main.c
  37  133  872 count.c
  38  128  938 main.c
  75  261 1810 total
$ printf 'hello world\nbye' | ./mywc
      1       3      15
$ ./mywc nosuch count.h; echo "exit $?"
nosuch: No such file or directory
     22      81     510 count.h
exit 1
```

When `count.c` changes, only `count.c` needs recompiling, then a quick link. With hundreds of files, that is one second instead of minutes. `make` (next section) works out what to recompile.

### What goes where

**Declaration vs definition.** A *declaration* says a name exists and gives its type. A *definition* also creates the thing: storage for a variable, or a function's body.

| | Declaration only | Definition |
|---|---|---|
| function | `void count_add(struct counts *to, const struct counts *from);` | the same line with a `{ body }` |
| variable | `extern int count_streams;` | `int count_streams = 0;` |
| type | — | `struct counts { ... };` (a type is not an object, so every file that uses it needs the full text) |

Each external variable and function must have **exactly one definition** in the whole program, and any number of matching declarations. Hence the rules for headers:

- **A header holds declarations**: types, function prototypes, `extern` variable declarations, macros and `static inline` functions. Never a variable or function *definition* (the gotchas below show why).
- **A `.c` file holds the definitions** and includes its own header first, so the compiler checks that prototypes and definitions agree: change `count_add`'s parameters in `count.c` only, and you get a "conflicting types" error instead of a crash.
- **Everything not in the header should be `static`** ([Chapter 4](04-functions.md)). `is_space` has internal linkage: other files can't call it, and their own `is_space` won't clash with it. `nm` shows it as lowercase `t`.
- **A header includes what it needs.** `count.h` uses `FILE`, so it includes `<stdio.h>`; its users shouldn't have to know that.
- **Global variables are rare.** `count_streams` is here to show `extern`. A function such as `int count_streams_read(void)` is usually better, because nobody else can change the value. parley has no external variables at all.

**Include guards.** `#ifndef MYWC_COUNT_H` / `#define MYWC_COUNT_H` / `#endif` makes a second inclusion of the header empty. Headers include other headers, so one header often reaches a file twice, and a type defined twice is an error. Name the guard after the project and file. Don't start it with `_` and a capital letter, or with two underscores: those names are reserved for the compiler and libc.

### Gotcha: no include guard

Delete the three guard lines from `count.h`, include it twice in `main.c` (as when two headers both include it), and compile:

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -c main.c 2>&1 | head -6
In file included from main.c:5:
count.h:5:8: error: redefinition of ‘struct counts’
    5 | struct counts {
      |        ^~~~~~
In file included from main.c:4:
count.h:5:8: note: originally defined here
```

### Gotcha: a variable defined in a header

Write `int count_streams;` in `count.h` (forgetting `extern`) and delete the definition from `count.c`. Now *every* file that includes the header defines the variable:

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -c main.c count.c
$ gcc -o mywc main.o count.o
/usr/bin/ld: count.o:(.bss+0x0): multiple definition of `count_streams'; main.o:(.bss+0x0): first defined here
collect2: error: ld returned 1 exit status
```

Both files compiled silently, because each alone is fine; only the **linker** (`ld`) sees both definitions. Before gcc 10 this linked: an uninitialized global like `int count_streams;` became a "common" symbol, and the linker silently merged them. gcc 10 made `-fno-common` the default. Old code still builds with `-fcommon`, but the real fix is `extern` in the header and one definition in a `.c` file.

### Gotcha: forgetting an object file

Link without `count.o`:

```console
$ gcc -o mywc main.o
/usr/bin/ld: main.o: warning: relocation against `count_streams' in read-only section `.text'
/usr/bin/ld: main.o: in function `main':
main.c:(.text+0xb9): undefined reference to `count_stream'
/usr/bin/ld: main.c:(.text+0x173): undefined reference to `count_stream'
/usr/bin/ld: main.c:(.text+0x1b8): undefined reference to `count_add'
/usr/bin/ld: main.c:(.text+0x1ce): undefined reference to `count_streams'
/usr/bin/ld: warning: creating DT_TEXTREL in a PIE
collect2: error: ld returned 1 exit status
```

An **undefined reference** is a linker error: the code compiled, but no object file defines that name. Add the missing object (or library: `-lm` for `sqrt` on glibc, [Chapter 2](02-types-and-operators.md)). The two `warning` lines come from glibc's default PIE build (position-independent executable, [Chapter 1](01-getting-started.md)) and vanish once the variable is found. Unlike a *compiler* error, with its file name and line, the linker knows only symbols and section offsets such as `.text+0xb9`. Compile with `-g` and it names the source line instead: ``.../main.c:16: undefined reference to `count_stream'`` (with the full path).

Both libcs use the same linker, `ld`, so `musl-gcc` gives the same errors, minus the `relocation` and `DT_TEXTREL` warnings when linked `-static` (not PIE).

## make

Typing compile commands doesn't scale, and a script that rebuilds everything is slow. **make** reads a `Makefile` of rules and rebuilds only what is out of date. The course uses GNU make, the `make` on Linux.

### A first Makefile

`examples/ch09/mywc/Makefile.1`
```make
# Makefile.1 - the simplest Makefile: one rule per file, nothing clever.
# Run it with: make -f Makefile.1

mywc: main.o count.o
	gcc -o mywc main.o count.o

main.o: main.c count.h
	gcc -std=c99 -Wall -Wextra -pedantic -c main.c

count.o: count.c count.h
	gcc -std=c99 -Wall -Wextra -pedantic -c count.c

clean:
	rm -f mywc main.o count.o
```

A **rule** says: to make the *target* (before the `:`), first bring the *prerequisites* (after it) up to date, then run the *recipe* (the indented lines) if the target is missing or older than any prerequisite, by file modification time. With no arguments make builds the first target. `make clean` builds the target `clean`, which never exists as a file, so its recipe always runs.

```console
$ make -f Makefile.1
gcc -std=c99 -Wall -Wextra -pedantic -c main.c
gcc -std=c99 -Wall -Wextra -pedantic -c count.c
gcc -o mywc main.o count.o
$ make -f Makefile.1
make: 'mywc' is up to date.
$ touch count.h
$ make -f Makefile.1
gcc -std=c99 -Wall -Wextra -pedantic -c main.c
gcc -std=c99 -Wall -Wextra -pedantic -c count.c
gcc -o mywc main.o count.o
```

`touch` updates a file's time, as if you had edited it; both objects depend on `count.h`, so both are rebuilt. make prints each command before running it, and stops at the first one that fails (non-zero exit status, [Chapter 1](01-getting-started.md)).

#### Gotcha: recipes start with a TAB

Every recipe line must start with a TAB, not spaces, and many editors turn TABs into spaces. Make a copy of `Makefile.1` with spaces and try it:

```console
$ sed 's/^\t/        /' Makefile.1 > Makefile.spaces
$ make -f Makefile.spaces
Makefile.spaces:5: *** missing separator (did you mean TAB instead of 8 spaces?).  Stop.
```

(The exact wording is GNU make 4.3's; other versions word it differently.) Tell your editor to keep real TABs in files named `Makefile`; most already do.

### Variables, pattern rules and automatic variables

`Makefile.1` repeats itself: the flags appear twice, and every new `.c` file needs a rule. Variables and a **pattern rule** fix both:

`examples/ch09/mywc/Makefile.2`
```make
# Makefile.2 - variables, a pattern rule and automatic variables.
# Run it with: make -f Makefile.2

CC     = gcc
CFLAGS = -std=c99 -Wall -Wextra -pedantic -O2
OBJS   = main.o count.o

.PHONY: all clean

all: mywc

mywc: $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

# Any foo.o is made from foo.c (and every object depends on count.h).
%.o: %.c count.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

clean:
	rm -f mywc $(OBJS)
```

```console
$ make -f Makefile.2
gcc  -std=c99 -Wall -Wextra -pedantic -O2 -c -o main.o main.c
gcc  -std=c99 -Wall -Wextra -pedantic -O2 -c -o count.o count.c
gcc  -o mywc main.o count.o 
$ make -f Makefile.2
make: Nothing to be done for 'all'.
$ touch count.c
$ make -f Makefile.2
gcc  -std=c99 -Wall -Wextra -pedantic -O2 -c -o count.o count.c
gcc  -o mywc main.o count.o 
```

Only `count.o` was rebuilt, then the program relinked. The new parts:

- **`$(NAME)`** expands a variable. An unset variable is empty: hence the double space after `gcc` (`$(CPPFLAGS)` and `$(LDFLAGS)`).
- **The conventional names**, used by make's built-in rules and every C programmer:

  | Variable | Holds | Example |
  |---|---|---|
  | `CC` | the C compiler | `gcc`, `musl-gcc` |
  | `CPPFLAGS` | preprocessor flags | `-D_POSIX_C_SOURCE=200809L`, `-I src` |
  | `CFLAGS` | compiler flags | `-std=c99 -Wall -O2 -g` |
  | `LDFLAGS` | linker flags, before the objects | `-static`, `-L dir` |
  | `LDLIBS` | libraries, **after** the objects | `-lm`, `-lpthread` |

  `LDLIBS` comes after `$^` because the linker reads left to right, taking from a library only what the objects before it need ([Chapter 2](02-types-and-operators.md)).
- **`%.o: %.c count.h`** is a **pattern rule**: `%` matches any stem, so `main.o` is made from `main.c`. make has a similar built-in rule, which [Chapter 11](11-testing.md)'s test Makefile relies on; your own controls the details.
- **Automatic variables**, set for each rule:

  | | Means | In `count.o`'s recipe |
  |---|---|---|
  | `$@` | the target | `count.o` |
  | `$<` | the first prerequisite | `count.c` |
  | `$^` | all prerequisites, without duplicates | `count.c count.h` |

- **`.PHONY: all clean`** declares targets that are not files. Otherwise a file called `clean` would make `make clean` say "up to date" and do nothing.
- **`all`** is the first target, so the default; by convention it builds everything.

### The final Makefile: build directories, header dependencies, two libcs

`Makefile.2` has two flaws. It lists `count.h` by hand, so a new header means editing the Makefile, and forgetting one means stale objects. And it puts glibc objects next to the sources, leaving nowhere for a musl build. The final version fixes both:

`examples/ch09/mywc/Makefile`
```make
# Makefile - mywc built into build/glibc/ or build/musl/.
#   make                build with gcc and glibc
#   make LIBC=musl      build with musl-gcc, statically linked
#   make clean          remove build/

LIBC  ?= glibc
BUILD := build/$(LIBC)

ifeq ($(LIBC),musl)
  CC      := musl-gcc
  LDFLAGS += -static
else ifneq ($(LIBC),glibc)
  $(error LIBC must be glibc or musl, not '$(LIBC)')
endif

CPPFLAGS += -MMD -MP
CFLAGS   ?= -O2
CFLAGS   += -std=c99 -Wall -Wextra -pedantic
OBJS     := $(BUILD)/main.o $(BUILD)/count.o

.PHONY: all clean

all: $(BUILD)/mywc

$(BUILD)/mywc: $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(BUILD)/%.o: %.c | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD):
	mkdir -p $@

clean:
	rm -rf build

-include $(wildcard $(BUILD)/*.d)
```

```console
$ make
mkdir -p build/glibc
cc -MMD -MP -O2 -std=c99 -Wall -Wextra -pedantic -c -o build/glibc/main.o main.c
cc -MMD -MP -O2 -std=c99 -Wall -Wextra -pedantic -c -o build/glibc/count.o count.c
cc  -o build/glibc/mywc build/glibc/main.o build/glibc/count.o 
$ make LIBC=musl
mkdir -p build/musl
musl-gcc -MMD -MP -O2 -std=c99 -Wall -Wextra -pedantic -c -o build/musl/main.o main.c
musl-gcc -MMD -MP -O2 -std=c99 -Wall -Wextra -pedantic -c -o build/musl/count.o count.c
musl-gcc -static -o build/musl/mywc build/musl/main.o build/musl/count.o 
$ make
make: Nothing to be done for 'all'.
$ cat build/glibc/main.d
build/glibc/main.o: main.c count.h
count.h:
$ touch count.h
$ make
cc -MMD -MP -O2 -std=c99 -Wall -Wextra -pedantic -c -o build/glibc/main.o main.c
cc -MMD -MP -O2 -std=c99 -Wall -Wextra -pedantic -c -o build/glibc/count.o count.c
cc  -o build/glibc/mywc build/glibc/main.o build/glibc/count.o 
$ make LIBC=uclibc; echo "exit $?"
Makefile:13: *** LIBC must be glibc or musl, not 'uclibc'.  Stop.
exit 2
$ ./build/musl/mywc count.c main.c
     37     133     872 count.c
     38     128     938 main.c
     75     261    1810 total
```

Piece by piece:

- **Four kinds of assignment.**

  | Operator | Meaning |
  |---|---|
  | `X = value` | *recursive*: `value` is expanded each time `X` is used, so it can refer to variables set later |
  | `X := value` | *simple*: expanded once, now. Predictable; prefer it for computed values |
  | `X ?= value` | set only if `X` has no value yet (from the environment, for example) |
  | `X += value` | append, with a space |

  A variable given on the command line (`make LIBC=musl`) overrides every assignment in the Makefile, even `+=` (see the gotcha below). Environment variables (`LIBC=musl make`) are weaker: `=` and `:=` override them, `?=` respects them.
- **`CC` is `cc` by default.** make predefines `CC = cc`, so `?=` would never take effect; the glibc build keeps it (on Linux `cc` is gcc), and the musl branch sets `CC := musl-gcc` explicitly.
- **`ifeq` / `else ifneq` / `endif`** are make's conditionals, evaluated as make reads the file. Lines inside may be indented with spaces, since they are not recipes. **`$(error text)`** stops make with a message, so a typo in `LIBC` can't silently build something else.
- **`-MMD -MP`** make the compiler write a **dependency file** (`build/glibc/main.d`) next to each object: a Makefile fragment listing every header the source included (`-MMD` leaves out system headers). `-MP` adds an empty rule such as `count.h:` per header; without it, renaming or deleting a header breaks the next build: ``make: *** No rule to make target 'count.h', needed by 'build/glibc/main.o'.  Stop.``
- **`-include $(wildcard $(BUILD)/*.d)`** reads those fragments. The `-` means "no error if there are none yet", as on a clean checkout, when every object is out of date anyway. Now `touch count.h` rebuilt both objects (`count.c` includes `count.h` too), and nobody listed `count.h` by hand.
- **`| $(BUILD)`** is an **order-only prerequisite**: the directory must exist before compiling, but its time doesn't matter. Each new file changes the directory's time, so as a normal prerequisite it would make every object out of date whenever one was written.
- **`$(BUILD)/%.o: %.c`**: the stem `main` connects `build/glibc/main.o` to `main.c`.
- **`CFLAGS ?= -O2`** is a default optimization level you can change from the environment (`CFLAGS=-O0 make`); the `+=` then adds the flags the code needs.

### glibc vs musl: one Makefile, two libcs

```console
$ file build/glibc/mywc build/musl/mywc | sed "s/BuildID[^,]*, //"
build/glibc/mywc: ELF 64-bit LSB pie executable, x86-64, version 1 (SYSV), dynamically linked, interpreter /lib64/ld-linux-x86-64.so.2, for GNU/Linux 3.2.0, not stripped
build/musl/mywc:  ELF 64-bit LSB executable, x86-64, version 1 (SYSV), statically linked, not stripped
$ ls -l build/glibc/mywc build/musl/mywc | awk '{print $5, $9}'
16408 build/glibc/mywc
47928 build/musl/mywc
$ ldd build/glibc/mywc | sed 's/ (0x.*//'
	linux-vdso.so.1
	libc.so.6 => /lib/x86_64-linux-gnu/libc.so.6
	/lib64/ld-linux-x86-64.so.2
$ ldd build/musl/mywc
	not a dynamic executable
```

- The **glibc** build is small because libc's code is not in it: at start-up the dynamic loader `/lib64/ld-linux-x86-64.so.2` maps `libc.so.6` into the process ([Chapter 1](01-getting-started.md)). It runs only where a compatible glibc is installed.
- The **musl** build is 48 KB because the parts of `libc.a` it uses (`printf`, `fopen`, `perror` and helpers) are copied in. It needs nothing at run time and runs on any x86-64 Linux machine.
- The builds use **separate directories** because objects compiled against one libc's headers (its types, struct layouts and symbol names) must never be linked with the other. Both builds also coexist, and `make` and `make LIBC=musl` never trigger each other's rebuilds.
- Libraries go in `LDLIBS`. A program using `sqrt` needs `LDLIBS += -lm` on glibc. musl's math functions are in `libc.a` and `-lm` links an empty library, so the same line works for both ([Chapter 2](02-types-and-operators.md)).

**musl is simpler for distribution**: one self-contained file, no "which glibc version does the server have?" glibc's dynamic build is smaller, and security updates to the system's `libc.so.6` reach it without a rebuild. [Chapter 22](22-shipping-and-libc-choices.md) weighs this for parley. The Makefile handles both in four lines, and that is the real lesson: keep the libc a build-time choice.

### Gotcha: `make CFLAGS=...` replaces your flags

For an unoptimized debugger build, you type:

```console
$ make CFLAGS=-O0
mkdir -p build/glibc
cc -MMD -MP -O0 -c -o build/glibc/main.o main.c
```

`-std=c99 -Wall -Wextra -pedantic` are gone: a command-line variable overrides **every** assignment in the Makefile, `+=` included. The program may still compile, without the warnings you rely on. The environment version keeps them, because `?=` respects the environment and `+=` appends:

```console
$ CFLAGS=-O0 make
mkdir -p build/glibc
cc -MMD -MP -O0 -std=c99 -Wall -Wextra -pedantic -c -o build/glibc/main.o main.c
```

(Run `make clean` between the two; changing a variable changes no file time, so make would see up-to-date objects and build nothing.) Exercise 4 makes the Makefile robust against this, and [Chapter 10](10-errors-and-debugging.md) adds a `SANITIZE=1` switch so you needn't edit `CFLAGS`.

## Building parley: modules and a Makefile

At the end of [Chapter 8](08-memory.md) the server was one 631-line `parleyd.c` and the client one 218-line `parley.c`, with duplicated helpers (`str_to_long` twice), an `enum` of unrelated constants, and sections separated by comment banners. Now each banner becomes a module. Behaviour doesn't change: the checkpoint feeds Chapter 8's input and gets the same output.

```
project/ch09/
├── Makefile
└── src/
    ├── str.h      str.c       safe string helpers (both programs)
    ├── nick.h     nick.c      nickname rules
    ├── strbuf.h   strbuf.c    the growable buffer (both programs)
    ├── msg.h      msg.c       protocol: parse, format, frame (both programs)
    ├── log.h      log.c       "parleyd: info: ..." on stderr
    ├── conn.h     conn.c      one client connection, sending replies
    ├── room.h     room.c      the commands: what the server does with a line
    ├── version.h              PARLEY_VERSION
    ├── parleyd.c              server: options and the main loop
    └── parley.c               client: options, slash commands
```

| Module | Came from (Chapter 8) | Changed |
|---|---|---|
| `str` | `str_copy`, `str_ieq`, `str_to_long` (server), `str_chomp`, `str_to_long` (client) | one copy for both programs |
| `nick` | `nick_valid` | adds `nick_equal`, for the multi-client chapters |
| `strbuf` | `sb_reserve`, `sb_append`, `sb_consume`, `sb_free` | adds `sb_appendf` and `sb_clear` |
| `msg` | `struct msg`, `msg_parse`, `msg_param`, `msg_format`, `take_line` | `take_line` → `msg_take_line` |
| `log` | `log_msg`, `level_name` | adds `log_init`: the program name and verbosity are settings |
| `conn` | `struct conn`, `target`, `send_msg`, `send_numeric` | → `conn_target`, `conn_send`, `conn_numeric` |
| `room` | the `cmd_*` handlers, the command table, `handle_line` | the table is an X-macro; `handle_line` → `room_handle_line` |

Every public name now starts with its module's name (`sb_`, `msg_`, `conn_`, `room_`): C has one global namespace for external names, and a prefix keeps modules from colliding. Everything else stays `static`.

Dependencies go one way: `room` uses `conn`, `msg`, `nick`, `log` and `str`; `conn` uses `msg` and `log`; `msg` uses `strbuf`. Nothing low-level knows about the server, which is why the **client** can link with just `msg.o`, `strbuf.o` and `str.o`.

### Step 1: the headers

The headers are the design: read them and you know what each module offers. All eight follow the rules above: a comment saying what the module is, an include guard named `PARLEY_..._H`, only the includes they need, and a comment on every function.

`project/ch09/src/str.h`
```c
/* str.h - small, safe string helpers. */
#ifndef PARLEY_STR_H
#define PARLEY_STR_H

#include <stdbool.h>
#include <stddef.h>

/* Copy src into dst[size], always NUL-terminated (if size > 0), like BSD
 * strlcpy. Returns strlen(src); a result >= size means it was truncated. */
size_t str_copy(char *dst, const char *src, size_t size);

/* Remove any trailing '\r' and '\n' characters. */
void str_chomp(char *s);

/* ASCII case-insensitive equality. */
bool str_ieq(const char *a, const char *b);

/* Parse a whole decimal number in [min, max] into *out. Returns false (and
 * leaves *out alone) if s is not such a number. */
bool str_to_long(const char *s, long min, long max, long *out);

#endif
```

`project/ch09/src/nick.h`
```c
/* nick.h - nickname rules. */
#ifndef PARLEY_NICK_H
#define PARLEY_NICK_H

#include <stdbool.h>

#define NICK_MAX 16   /* longest nickname, in bytes */

/* 1-16 characters: a letter, then letters, digits, '_' or '-'. */
bool nick_valid(const char *nick);

/* Nicknames are case-insensitive: "Alice" and "alice" are the same. */
bool nick_equal(const char *a, const char *b);

#endif
```

`project/ch09/src/strbuf.h`
```c
/* strbuf.h - a growable byte buffer.
 *
 * A strbuf owns data, a heap block of cap bytes holding len bytes of
 * content. data[len] is always '\0' once data is allocated, so text
 * content can be used as a C string. A zeroed strbuf is a valid empty
 * buffer:
 *     struct strbuf sb = { 0 };
 *     sb_append(&sb, "hi", 2);
 *     sb_free(&sb);
 */
#ifndef PARLEY_STRBUF_H
#define PARLEY_STRBUF_H

#include <stddef.h>

struct strbuf {
    char *data;
    size_t len;
    size_t cap;
};

/* Make room for extra more bytes. Returns 0, or -1 if the size would
 * overflow or memory is exhausted (the buffer is then unchanged). */
int sb_reserve(struct strbuf *sb, size_t extra);

/* Append n bytes (they may include '\0' bytes). Returns 0 or -1. */
int sb_append(struct strbuf *sb, const void *bytes, size_t n);

/* Append printf-style formatted text. Returns 0 or -1. */
int sb_appendf(struct strbuf *sb, const char *fmt, ...);

/* Drop the first n bytes (all if n >= len); the rest moves to the front. */
void sb_consume(struct strbuf *sb, size_t n);

/* Empty the buffer but keep its memory for reuse. */
void sb_clear(struct strbuf *sb);

/* Free the memory; the strbuf is empty (and reusable) afterwards. */
void sb_free(struct strbuf *sb);

#endif
```

`struct strbuf` is defined in the header, so other modules can embed one (`struct conn` does) and read `len` and `data`. The alternative, an **opaque type** ([Chapter 7](07-structs-unions-enums.md)), declares only `struct strbuf;` and lets the module allocate and own every instance: more hiding, but a `malloc` per buffer. For a small value type like this, a visible struct is usual.

`project/ch09/src/msg.h`
```c
/* msg.h - the parley protocol: parse, format and frame lines.
 *
 *     message = [":" prefix " "] command {" " param} [" :" trailing] "\r\n"
 *
 * A line is at most MSG_LINE_MAX bytes including the "\r\n". Receivers
 * also accept a bare "\n".
 */
#ifndef PARLEY_MSG_H
#define PARLEY_MSG_H

#include <stdbool.h>
#include <stddef.h>

#include "strbuf.h"

#define MSG_LINE_MAX 512     /* longest line, including "\r\n" */
#define MSG_PARAMS_MAX 8     /* most middle parameters in one message */

/* One protocol line split into its parts. After msg_parse() all pointers
 * point into the parsed line, so the line must outlive the msg. */
struct msg {
    const char *prefix;                   /* who sent it; NULL if absent */
    const char *command;                  /* "NICK", "001", ... */
    int nparams;                          /* number of middle parameters */
    const char *params[MSG_PARAMS_MAX];   /* the middle parameters */
    const char *trailing;                 /* text after " :"; NULL if absent */
};

enum msg_status {
    MSG_OK,
    MSG_EMPTY,             /* blank line: not an error, just ignore it */
    MSG_NO_COMMAND,        /* a prefix with nothing after it */
    MSG_TOO_MANY_PARAMS
};

/* Parse line (without "\r\n") into *m. Modifies line: the spaces between
 * the parts become '\0' terminators. */
enum msg_status msg_parse(char *line, struct msg *m);

/* Parameter i, counting the trailing parameter as the last one: for both
 * "NICK bob" and "NICK :bob", msg_param(m, 0) is "bob". NULL if absent. */
const char *msg_param(const struct msg *m, int i);

/* Format *m as a line ending in "\r\n" into out[size], size >= 3. A too
 * long trailing parameter is cut so the line fits in MSG_LINE_MAX bytes
 * (and in size - 1). Returns the line length, or -1 if the parts before
 * the trailing parameter don't fit. */
int msg_format(char *out, size_t size, const struct msg *m);

/* Framing: take the next complete line out of the input buffer in. */
enum frame_status {
    FRAME_NONE,       /* no complete line yet: read more input */
    FRAME_LINE,       /* a line (without "\r\n") was copied into line[] */
    FRAME_TOO_LONG    /* a line longer than MSG_LINE_MAX was thrown away */
};

/* *skipping must start false and is kept between calls: it remembers that
 * we are throwing away the rest of a too-long line. Works however the input
 * was split up, even one byte at a time. */
enum frame_status msg_take_line(struct strbuf *in, bool *skipping,
                                char line[MSG_LINE_MAX + 1]);

#endif
```

`msg.h` includes `strbuf.h` because `msg_take_line` takes a `struct strbuf *`. A pointer parameter strictly needs only the declaration `struct strbuf;`, but every caller needs the full struct anyway. The protocol limits moved from an `enum` to `#define`; `MSG_LINE_MAX` now sizes arrays in both programs.

`project/ch09/src/log.h`
```c
/* log.h - log lines to stderr: "prog: level: message". */
#ifndef PARLEY_LOG_H
#define PARLEY_LOG_H

#include <stdbool.h>

enum log_level { LOG_DEBUG, LOG_INFO, LOG_WARN, LOG_ERROR };

/* Set the program name used as prefix, and whether debug lines show. */
void log_init(const char *prog, bool verbose);

/* printf-style; adds the prefix and a newline. */
void log_msg(enum log_level level, const char *fmt, ...);

#endif
```

`project/ch09/src/conn.h`
```c
/* conn.h - one client connection, as the server sees it. */
#ifndef PARLEY_CONN_H
#define PARLEY_CONN_H

#include <stdbool.h>

#include "msg.h"
#include "nick.h"
#include "strbuf.h"

#define SERVER_NAME "parley"   /* the prefix of messages from the server */

struct conn {
    char nick[NICK_MAX + 1];   /* "" until the client registers */
    bool quitting;             /* sent QUIT: stop reading */
    struct strbuf in;          /* bytes received but not yet handled */
    bool skipping;             /* throwing away a too-long line */
};

/* The name to use in replies: the nick, or "*" before registration. */
const char *conn_target(const struct conn *c);

/* Send one message to the client. */
void conn_send(struct conn *c, const struct msg *m);

/* Send ":parley CODE <target> [param] :text", the server's reply format. */
void conn_numeric(struct conn *c, const char *code, const char *param,
                  const char *text);

#endif
```

`conn.h` includes `strbuf.h` even though `msg.h` already does: `struct conn` uses a `struct strbuf` directly, and a header should not depend on what another header happens to include today. The include guards make the second inclusion harmless.

`project/ch09/src/room.h`
```c
/* room.h - the chat room: what the server does with each line. */
#ifndef PARLEY_ROOM_H
#define PARLEY_ROOM_H

#include "conn.h"

/* Handle one protocol line (without "\r\n") from client c. Modifies line. */
void room_handle_line(struct conn *c, char *line);

#endif
```

`project/ch09/src/version.h`
```c
/* version.h - the version shown by --version. */
#ifndef PARLEY_VERSION_H
#define PARLEY_VERSION_H

#define PARLEY_VERSION "0.1"

#endif
```

`room.h` is the whole public face of the chat logic: one function. The seven command handlers and the table are `static` in `room.c`. `version.h` holds one macro, so both programs print the same version and changing it rebuilds only the files that include it.

### Step 2: the implementations

Most code moved unchanged. Each `.c` file starts with its own header, then system headers, then the other project headers it uses. The two smallest modules in full:

`project/ch09/src/log.c`
```c
/* log.c - log lines to stderr. */
#include "log.h"

#include <stdarg.h>
#include <stdio.h>

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

void log_msg(enum log_level level, const char *fmt, ...)
{
    if (level == LOG_DEBUG && !log_verbose)
        return;

    fprintf(stderr, "%s: %s: ", log_prog, level_name(level));
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}
```

`log_prog` and `log_verbose` are file-scope `static` variables: module state no other file can touch, changed only through `log_init`. Before, `log_msg` read the server's `cfg.verbose` directly; now `log.c` knows nothing about `struct config`, so the client could use it too.

`project/ch09/src/conn.c`
```c
/* conn.c - one client connection, as the server sees it. */
#include "conn.h"

#include <stdio.h>

#include "log.h"

const char *conn_target(const struct conn *c)
{
    return c->nick[0] != '\0' ? c->nick : "*";
}

void conn_send(struct conn *c, const struct msg *m)
{
    char line[MSG_LINE_MAX + 1];
    (void)c;   /* one client on stdout for now; later each conn has a socket */
    if (msg_format(line, sizeof line, m) < 0) {
        log_msg(LOG_WARN, "message too long to send: %s", m->command);
        return;
    }
    fputs(line, stdout);
}

void conn_numeric(struct conn *c, const char *code, const char *param,
                  const char *text)
{
    struct msg m = {
        .prefix = SERVER_NAME,
        .command = code,
        .nparams = param != NULL ? 2 : 1,
        .params = { conn_target(c), param },
        .trailing = text,
    };
    conn_send(c, &m);
}
```

`conn_send` takes a `struct conn *` it doesn't use yet, so the interface is ready for [Chapter 15](15-a-first-server.md), where each connection gets its own socket: then only `conn.c` changes, not the dozens of callers.

`strbuf.c` gains two functions. `sb_clear` empties the buffer (`msg_take_line` uses it instead of the less clear `sb_consume(in, in->len)`). `sb_appendf` is `printf` into a growable buffer:

`project/ch09/src/strbuf.c` (excerpt)
```c
int sb_appendf(struct strbuf *sb, const char *fmt, ...)
{
    /* First pass: vsnprintf with size 0 only counts the bytes needed. A
     * va_list can be used once, so the second pass needs its own copy. */
    va_list ap, ap2;
    va_start(ap, fmt);
    va_copy(ap2, ap);
    int n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);

    int result = -1;
    if (n >= 0 && sb_reserve(sb, (size_t)n) == 0) {
        vsnprintf(sb->data + sb->len, (size_t)n + 1, fmt, ap2);
        sb->len += (size_t)n;
        result = 0;
    }
    va_end(ap2);
    return result;
}
```

`vsnprintf(NULL, 0, ...)` is C99's way to measure formatted output: it writes nothing and returns the length. `va_copy` ([Chapter 4](04-functions.md)) makes the second `va_list`. Nothing calls `sb_appendf` yet: [Chapter 11](11-testing.md) is the first to use it (in tests), and the client's screen code uses it later. `sb_append` also gained one guard:

`project/ch09/src/strbuf.c` (excerpt)
```c
    if (n > 0)
        memcpy(sb->data + sb->len, bytes, n);
```

`sb_append(&sb, NULL, 0)` is a reasonable call, but passing a null pointer to `memcpy` is undefined behaviour even with length 0 (C99 7.21.1). As a public module, `strbuf` will have callers you didn't write.

### The command table as an X-macro

In Chapter 8 the server declared the seven handlers, then listed them again in the table: adding a command meant edits in two places. Now there is one list:

`project/ch09/src/room.c` (excerpt)
```c
/* The command table as an X-macro: one line per command, expanded twice
 * below (once for prototypes, once for table entries), so the two can
 * never disagree.  X(NAME, handler, needs_nick) */
#define COMMANDS(X)                  \
    X("NICK",  cmd_nick,  false)     \
    X("MSG",   cmd_msg,   true)      \
    X("ME",    cmd_me,    true)      \
    X("NAMES", cmd_names, true)      \
    X("PING",  cmd_ping,  false)     \
    X("PONG",  cmd_pong,  false)     \
    X("QUIT",  cmd_quit,  false)

#define DECLARE(name, fn, needs_nick) \
    static void fn(struct conn *c, const struct msg *m);
COMMANDS(DECLARE)
#undef DECLARE

struct command {
    const char *name;
    void (*handler)(struct conn *c, const struct msg *m);
    bool needs_nick;        /* only allowed after registration */
};

#define ENTRY(name, fn, needs_nick) { name, fn, needs_nick },
static const struct command commands[] = { COMMANDS(ENTRY) };
#undef ENTRY

#define NCOMMANDS (sizeof commands / sizeof commands[0])
```

The preprocessor turns that into the prototypes and table you wrote by hand before (`false` and `true` are `<stdbool.h>` macros, so they become `0` and `1`):

```console
$ cd project/ch09
$ gcc -std=c99 -D_POSIX_C_SOURCE=200809L -E -P src/room.c | grep -E '^static void cmd_nick\(struct conn \*c, const struct msg \*m\); static|^static const struct command commands'
static void cmd_nick(struct conn *c, const struct msg *m); static void cmd_msg(struct conn *c, const struct msg *m); static void cmd_me(struct conn *c, const struct msg *m); static void cmd_names(struct conn *c, const struct msg *m); static void cmd_ping(struct conn *c, const struct msg *m); static void cmd_pong(struct conn *c, const struct msg *m); static void cmd_quit(struct conn *c, const struct msg *m);
static const struct command commands[] = { { "NICK", cmd_nick, 0 }, { "MSG", cmd_msg, 1 }, { "ME", cmd_me, 1 }, { "NAMES", cmd_names, 1 }, { "PING", cmd_ping, 0 }, { "PONG", cmd_pong, 0 }, { "QUIT", cmd_quit, 0 }, };
```

`NCOMMANDS` is now a macro (it was `enum { NCOMMANDS = sizeof commands / sizeof commands[0] }`). `sizeof` gives a `size_t`, so the dispatch loop counts with a `size_t i` too, leaving `-Wextra` no signed/unsigned comparison to complain about:

`project/ch09/src/room.c` (excerpt)
```c
    for (size_t i = 0; i < NCOMMANDS; i++) {
        const struct command *cmd = &commands[i];
        if (!str_ieq(m.command, cmd->name))
            continue;
        if (cmd->needs_nick && c->nick[0] == '\0')
            conn_numeric(c, "451", NULL, "You have not registered");
        else
            cmd->handler(c, &m);
        return;
    }
    conn_numeric(c, "421", m.command, "Unknown command");
```

The client's slash-command table stays an ordinary array: its handlers are defined above it, so only `cmd_help` (which prints the table) needs a prototype. With no second list to keep in step, an X-macro would add nothing.

### The two programs

`parleyd.c` is now just options and the main loop. Its includes show what it uses:

`project/ch09/src/parleyd.c` (excerpt)
```c
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "conn.h"
#include "log.h"
#include "msg.h"
#include "room.h"
#include "str.h"
#include "version.h"

#define DEFAULT_PORT 6667
#define PORT_MAX 65535
#define DEFAULT_MAX_CLIENTS 64
#define MAX_CLIENTS_MAX 1000
```

`project/ch09/src/parleyd.c` (excerpt)
```c
    parse_args(argc, argv, &cfg);
    log_init("parleyd", cfg.verbose);
    log_msg(LOG_DEBUG, "verbose logging is on");
    log_msg(LOG_INFO, "reading one client session from stdin");

    struct conn client = { .quitting = false };   /* zeroed: nick "", in empty */
    char chunk[256];
    char line[MSG_LINE_MAX + 1];
    while (!client.quitting && fgets(chunk, sizeof chunk, stdin) != NULL) {
        if (sb_append(&client.in, chunk, strlen(chunk)) < 0) {
            log_msg(LOG_ERROR, "out of memory");
            break;
        }
        enum frame_status st;
        while (!client.quitting
               && (st = msg_take_line(&client.in, &client.skipping, line)) != FRAME_NONE) {
            if (st == FRAME_TOO_LONG)
                conn_numeric(&client, "417", NULL, "Input line too long");
            else
                room_handle_line(&client, line);
        }
    }
    sb_free(&client.in);
    log_msg(LOG_INFO, "session over");
    return 0;
```

The `--version` option glues the macro into a literal: `printf("parleyd " PARLEY_VERSION "\n");`.

The client used to build protocol lines with `printf("NICK %s\r\n", ...)`. Now it shares `msg.c` with the server, builds a `struct msg` and lets `msg_format` produce the line, so client and server agree on the format by construction:

`project/ch09/src/parley.c` (excerpt)
```c
/* Send one protocol message (for now: print it on stdout). */
static void send_msg(const struct msg *m)
{
    char line[MSG_LINE_MAX + 1];
    if (msg_format(line, sizeof line, m) > 0)
        fputs(line, stdout);
}

static void cmd_nick(const char *arg)
{
    if (*arg == '\0') {
        fprintf(stderr, "parley: usage: /nick NEWNICK\n");
        return;
    }
    send_msg(&(struct msg){ .command = "NICK", .nparams = 1, .params = { arg } });
}
```

`&(struct msg){ ... }` is a compound literal ([Chapter 7](07-structs-unions-enums.md)): a temporary struct built in place, whose address goes to `send_msg`. Unnamed members are zero, so `prefix` and `trailing` are `NULL`.

### Step 3: build by hand

Before writing the Makefile, build once by hand to see exactly what it must do. Each module compiles on its own, then each program links its objects:

```console
$ cd project/ch09/src
$ for f in *.c; do gcc -std=c99 -Wall -Wextra -pedantic -Werror -D_POSIX_C_SOURCE=200809L -c $f; done
$ ls *.o
conn.o  log.o  msg.o  nick.o  parley.o  parleyd.o  room.o  str.o  strbuf.o
$ gcc -o parleyd parleyd.o room.o conn.o msg.o nick.o strbuf.o str.o log.o
$ gcc -o parley parley.o msg.o strbuf.o str.o
$ ./parley --version
parley 0.1
$ nm log.o
                 U __stack_chk_fail
                 U fprintf
                 U fputc
0000000000000029 t level_name
0000000000000000 T log_init
0000000000000087 T log_msg
0000000000000000 d log_prog
0000000000000000 b log_verbose
                 U stderr
                 U vfprintf
```

`nm log.o` shows the module's interface from the linker's side: only `log_init` and `log_msg` are exported (uppercase `T`). `level_name` (`t`), `log_prog` (`d`, initialized) and `log_verbose` (`b`, zero) are private.

Leave out an object the client needs, and the linker says which functions are missing and where they were called from:

```console
$ gcc -o parley parley.o msg.o str.o
/usr/bin/ld: msg.o: in function `msg_take_line':
msg.c:(.text+0x5dc): undefined reference to `sb_clear'
/usr/bin/ld: msg.c:(.text+0x636): undefined reference to `sb_consume'
/usr/bin/ld: msg.c:(.text+0x65f): undefined reference to `sb_consume'
/usr/bin/ld: msg.c:(.text+0x6d8): undefined reference to `sb_consume'
collect2: error: ld returned 1 exit status
```

The client never calls `msg_take_line`, but the linker works with whole object files: link `msg.o` and you need everything `msg.o` refers to. The same commands with `musl-gcc`, plus `-static` on the link lines, build the musl versions.

### Step 4: the Makefile

The project's Makefile is the final `mywc` Makefile plus two programs, sources in `src/`, and the project's flags.

`project/ch09/Makefile`
```make
# Makefile for parley (GNU make).
#
#   make                 build with glibc into build/glibc/
#   make LIBC=musl       build with musl (static) into build/musl/
#   make clean           remove build/

LIBC  ?= glibc
BUILD := build/$(LIBC)

ifeq ($(LIBC),musl)
  CC      := musl-gcc
  LDFLAGS += -static
else ifneq ($(LIBC),glibc)
  $(error LIBC must be glibc or musl, not '$(LIBC)')
endif

# -D_POSIX_C_SOURCE: ask every header for POSIX.1-2008 on top of C99.
# -MMD -MP: write build/*/foo.d files listing the headers foo.c includes.
CPPFLAGS += -D_POSIX_C_SOURCE=200809L -MMD -MP
CFLAGS   ?= -O2 -g
CFLAGS   += -std=c99 -Wall -Wextra -pedantic -Werror

SERVER_OBJS := $(addprefix $(BUILD)/, parleyd.o room.o conn.o msg.o nick.o strbuf.o str.o log.o)
CLIENT_OBJS := $(addprefix $(BUILD)/, parley.o msg.o strbuf.o str.o)

.PHONY: all clean

all: $(BUILD)/parleyd $(BUILD)/parley

$(BUILD)/parleyd: $(SERVER_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(BUILD)/parley: $(CLIENT_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

# Pattern rule: build/glibc/foo.o is made from src/foo.c. The build
# directory is an order-only prerequisite (after |): it must exist, but
# its timestamp doesn't make objects out of date.
$(BUILD)/%.o: src/%.c | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD):
	mkdir -p $@

clean:
	rm -rf build

# The .d files made by -MMD: rebuild an object when a header it uses changes.
-include $(wildcard $(BUILD)/*.d)
```

New compared with `mywc`:

- **`-D_POSIX_C_SOURCE=200809L` in `CPPFLAGS`**: the feature-test macro for every file, in one place. Nothing uses POSIX yet; from [Chapter 12](12-files-and-descriptors.md) on, files call `read`, `sigaction` and `socket` without their own `#define`.
- **`-Werror`**: every warning is an error. The code compiles cleanly on both libcs and must stay that way; a warning you can ignore is one you will soon stop reading.
- **`-g`** is in the default `CFLAGS`. Debug information doesn't slow the program, and [Chapter 10](10-errors-and-debugging.md)'s debugger needs it.
- **`$(addprefix $(BUILD)/, ...)`**, a GNU make text function, puts `build/glibc/` in front of every word.
- **Shared objects.** `msg.o`, `strbuf.o` and `str.o` appear in both lists but are compiled once: make builds each target once per run.
- **`all`** names both programs, so plain `make` builds both.
- **`src/%.c`** in the pattern: sources in `src/`, objects in `build/LIBC/`.

The dependency files now track real header chains. `room.c` includes three project headers directly, and seven through them:

```console
$ cat build/glibc/room.d
build/glibc/room.o: src/room.c src/room.h src/conn.h src/msg.h \
 src/strbuf.h src/nick.h src/log.h src/str.h
src/room.h:
src/conn.h:
src/msg.h:
src/strbuf.h:
src/nick.h:
src/log.h:
src/str.h:
```

So editing `nick.h` rebuilds `room.o`, though `room.c` never mentions `nick.h`. Keeping such a list by hand would be hopeless.

### Checkpoint

Build with both libcs, check that make rebuilds only what changed, then run the usual session through the musl server:

```console
$ cd project/ch09
$ make
mkdir -p build/glibc
cc -D_POSIX_C_SOURCE=200809L -MMD -MP -O2 -g -std=c99 -Wall -Wextra -pedantic -Werror -c -o build/glibc/parleyd.o src/parleyd.c
cc -D_POSIX_C_SOURCE=200809L -MMD -MP -O2 -g -std=c99 -Wall -Wextra -pedantic -Werror -c -o build/glibc/room.o src/room.c
cc -D_POSIX_C_SOURCE=200809L -MMD -MP -O2 -g -std=c99 -Wall -Wextra -pedantic -Werror -c -o build/glibc/conn.o src/conn.c
cc -D_POSIX_C_SOURCE=200809L -MMD -MP -O2 -g -std=c99 -Wall -Wextra -pedantic -Werror -c -o build/glibc/msg.o src/msg.c
cc -D_POSIX_C_SOURCE=200809L -MMD -MP -O2 -g -std=c99 -Wall -Wextra -pedantic -Werror -c -o build/glibc/nick.o src/nick.c
cc -D_POSIX_C_SOURCE=200809L -MMD -MP -O2 -g -std=c99 -Wall -Wextra -pedantic -Werror -c -o build/glibc/strbuf.o src/strbuf.c
cc -D_POSIX_C_SOURCE=200809L -MMD -MP -O2 -g -std=c99 -Wall -Wextra -pedantic -Werror -c -o build/glibc/str.o src/str.c
cc -D_POSIX_C_SOURCE=200809L -MMD -MP -O2 -g -std=c99 -Wall -Wextra -pedantic -Werror -c -o build/glibc/log.o src/log.c
cc  -o build/glibc/parleyd build/glibc/parleyd.o build/glibc/room.o build/glibc/conn.o build/glibc/msg.o build/glibc/nick.o build/glibc/strbuf.o build/glibc/str.o build/glibc/log.o 
cc -D_POSIX_C_SOURCE=200809L -MMD -MP -O2 -g -std=c99 -Wall -Wextra -pedantic -Werror -c -o build/glibc/parley.o src/parley.c
cc  -o build/glibc/parley build/glibc/parley.o build/glibc/msg.o build/glibc/strbuf.o build/glibc/str.o 
$ make LIBC=musl
mkdir -p build/musl
musl-gcc -D_POSIX_C_SOURCE=200809L -MMD -MP -O2 -g -std=c99 -Wall -Wextra -pedantic -Werror -c -o build/musl/parleyd.o src/parleyd.c
musl-gcc -D_POSIX_C_SOURCE=200809L -MMD -MP -O2 -g -std=c99 -Wall -Wextra -pedantic -Werror -c -o build/musl/room.o src/room.c
musl-gcc -D_POSIX_C_SOURCE=200809L -MMD -MP -O2 -g -std=c99 -Wall -Wextra -pedantic -Werror -c -o build/musl/conn.o src/conn.c
musl-gcc -D_POSIX_C_SOURCE=200809L -MMD -MP -O2 -g -std=c99 -Wall -Wextra -pedantic -Werror -c -o build/musl/msg.o src/msg.c
musl-gcc -D_POSIX_C_SOURCE=200809L -MMD -MP -O2 -g -std=c99 -Wall -Wextra -pedantic -Werror -c -o build/musl/nick.o src/nick.c
musl-gcc -D_POSIX_C_SOURCE=200809L -MMD -MP -O2 -g -std=c99 -Wall -Wextra -pedantic -Werror -c -o build/musl/strbuf.o src/strbuf.c
musl-gcc -D_POSIX_C_SOURCE=200809L -MMD -MP -O2 -g -std=c99 -Wall -Wextra -pedantic -Werror -c -o build/musl/str.o src/str.c
musl-gcc -D_POSIX_C_SOURCE=200809L -MMD -MP -O2 -g -std=c99 -Wall -Wextra -pedantic -Werror -c -o build/musl/log.o src/log.c
musl-gcc -static -o build/musl/parleyd build/musl/parleyd.o build/musl/room.o build/musl/conn.o build/musl/msg.o build/musl/nick.o build/musl/strbuf.o build/musl/str.o build/musl/log.o 
musl-gcc -D_POSIX_C_SOURCE=200809L -MMD -MP -O2 -g -std=c99 -Wall -Wextra -pedantic -Werror -c -o build/musl/parley.o src/parley.c
musl-gcc -static -o build/musl/parley build/musl/parley.o build/musl/msg.o build/musl/strbuf.o build/musl/str.o 
$ make
make: Nothing to be done for 'all'.
$ touch src/nick.h && make
cc -D_POSIX_C_SOURCE=200809L -MMD -MP -O2 -g -std=c99 -Wall -Wextra -pedantic -Werror -c -o build/glibc/parleyd.o src/parleyd.c
cc -D_POSIX_C_SOURCE=200809L -MMD -MP -O2 -g -std=c99 -Wall -Wextra -pedantic -Werror -c -o build/glibc/room.o src/room.c
cc -D_POSIX_C_SOURCE=200809L -MMD -MP -O2 -g -std=c99 -Wall -Wextra -pedantic -Werror -c -o build/glibc/conn.o src/conn.c
cc -D_POSIX_C_SOURCE=200809L -MMD -MP -O2 -g -std=c99 -Wall -Wextra -pedantic -Werror -c -o build/glibc/nick.o src/nick.c
cc  -o build/glibc/parleyd build/glibc/parleyd.o build/glibc/room.o build/glibc/conn.o build/glibc/msg.o build/glibc/nick.o build/glibc/strbuf.o build/glibc/str.o build/glibc/log.o 
$ make LIBC=uclibc; echo "exit $?"
Makefile:14: *** LIBC must be glibc or musl, not 'uclibc'.  Stop.
exit 2
$ file build/glibc/parleyd build/musl/parleyd | sed "s/BuildID[^,]*, //"
build/glibc/parleyd: ELF 64-bit LSB pie executable, x86-64, version 1 (SYSV), dynamically linked, interpreter /lib64/ld-linux-x86-64.so.2, for GNU/Linux 3.2.0, with debug_info, not stripped
build/musl/parleyd:  ELF 64-bit LSB executable, x86-64, version 1 (SYSV), statically linked, with debug_info, not stripped
```

Touching `nick.h` rebuilt the four objects whose `.d` files list it, then relinked the server. `msg.o` and `parley.o` don't depend on `nick.h`, so the client was untouched. Now the session: the glibc client produces protocol lines and the musl server answers them.

```console
$ printf 'hello\n/me waves\n/nick bob\n/quit gotta go\n' | build/glibc/parley -n alice | build/musl/parleyd
parleyd: info: reading one client session from stdin
:parley 001 alice :Welcome to parley, alice
parleyd: info: alice joined
parleyd: info: alice is now bob
parleyd: info: bob left (gotta go)
parleyd: info: session over
:alice JOIN
:parley 353 alice :alice
:parley 366 alice :End of NAMES
:alice MSG :hello
:alice ME :waves
:alice NICK bob
ERROR :Goodbye
$ printf 'NICK alice\r\nMSG\r\nNAMES\r\nmsg :lower case works\r\nPING :x\r\nFOO\r\nQUIT\r\n' | build/glibc/parleyd 2>/dev/null
:parley 001 alice :Welcome to parley, alice
:alice JOIN
:parley 353 alice :alice
:parley 366 alice :End of NAMES
:parley 412 alice :No text to send
:parley 353 alice :alice
:parley 366 alice :End of NAMES
:alice MSG :lower case works
:parley PONG :x
:parley 421 alice FOO :Unknown command
ERROR :Goodbye
$ make clean && ls
rm -rf build
Makefile  src
```

The log lines (stderr) and protocol lines (stdout) may interleave differently on your screen: each stream is in order, but stdout is buffered and stderr is not ([Chapter 12](12-files-and-descriptors.md)). The protocol output is the same as in Chapter 8: modules changed how parley is built, not what it does. The `\r\n` line endings are not shown.

The complete project at this point is in `project/ch09/`.

## Summary

- A C program is built in four stages: preprocess (`-E`), compile (`-S`), assemble (`-c`), link. Each `.c` file plus its headers is a translation unit, compiled on its own.
- `#include "x.h"` searches the current file's directory first, then `-I` dirs; `#include <x.h>` only `-I` and system directories. musl keeps all its headers in one directory behind a short wrapper script; glibc's `<stdio.h>` alone pulls in about 30 headers.
- Function-like macros are text substitution: parenthesize every parameter and the whole body, never pass arguments with side effects, wrap statement macros in `do { ... } while (0)`. Prefer a function (or an `enum` or `const`) when you can.
- `#` stringifies an argument, `##` glues tokens, `__VA_ARGS__` makes variadic macros. `__FILE__`, `__LINE__` and `__func__` help logging.
- `#if`/`#ifdef` select code at compile time. `-D` sets macros from the command line; `#error` stops a build early.
- Feature-test macros choose what system headers declare: put `-D_POSIX_C_SOURCE=200809L` in `CPPFLAGS`. musl's defaults are short and readable, glibc's spread over about twenty macros. Neither has a "which libc" macro; musl refuses on purpose.
- X-macros keep one list and expand it several times (enum, string table, prototypes), so the copies can't disagree.
- Headers hold declarations, types, macros and `static inline` helpers, with include guards. Definitions go in exactly one `.c` file. Make everything a module doesn't export `static`.
- The linker joins object files by name. "Undefined reference": an object or library is missing. "Multiple definition": something is defined twice, typically in a header.
- make rebuilds a target when a prerequisite is newer. Recipes start with a TAB. Use `CC`, `CPPFLAGS`, `CFLAGS`, `LDFLAGS` and `LDLIBS`, pattern rules and `$@ $< $^`; let `-MMD -MP` track header dependencies.
- One Makefile builds both libcs into separate directories (`make LIBC=musl`). The static musl binary is bigger but runs on any x86-64 Linux.
- parley: nine `.c` files and eight headers in `src/`, with modules shared by both programs. The command table is an X-macro, the client builds messages with `msg_format`, and everything compiles with `-Werror`.

## Exercises

1. **ARRAY_LEN.** Write a macro `ARRAY_LEN(a)` giving the number of elements in an array. Test it in `main` on `const char *names[] = { "alice", "bob", "carol" };`, then pass `names` to `show(const char *names[])` and use the macro there. What happens, and why can't the macro detect it?

   <details><summary>Solution</summary>

   ```c
   #include <stdio.h>

   #define ARRAY_LEN(a) (sizeof (a) / sizeof (a)[0])

   static void show(const char *names[])
   {
       printf("in show: %zu\n", ARRAY_LEN(names));
   }

   int main(void)
   {
       const char *names[] = { "alice", "bob", "carol" };
       printf("in main: %zu\n", ARRAY_LEN(names));
       show(names);
       return 0;
   }
   ```

   ```console
   $ gcc -std=c99 -Wall -Wextra -pedantic -o alen alen.c
   alen.c: In function ‘show’:
   alen.c:3:30: warning: ‘sizeof’ on array function parameter ‘names’ will return size of ‘const char **’ [-Wsizeof-array-argument]
       3 | #define ARRAY_LEN(a) (sizeof (a) / sizeof (a)[0])
         |                              ^
   alen.c:7:30: note: in expansion of macro ‘ARRAY_LEN’
       7 |     printf("in show: %zu\n", ARRAY_LEN(names));
         |                              ^~~~~~~~~
   alen.c:5:30: note: declared here
       5 | static void show(const char *names[])
         |                  ~~~~~~~~~~~~^~~~~~~
   $ ./alen
   in main: 3
   in show: 1
   ```

   musl prints the same. A parameter declared as an array is really a pointer ([Chapter 5](05-pointers-and-arrays.md)), so in `show` the macro divides the size of a pointer (8) by the size of a pointer (8). The macro only sees text, and both uses look identical to it. The parentheses around `(a)` matter: `ARRAY_LEN(p + 1)` must expand to `sizeof (p + 1)`, not `sizeof p + 1`. gcc's warning catches the pointer case, one reason to keep `-Wall -Werror` on. The usual C fix: pass the length along with the array.

   </details>

2. **Log levels as an X-macro.** `log.c` names the levels in a `switch` that repeats `log.h`'s `enum`. Make the levels one X-macro list in `log.h` and generate both the `enum` and a name table from it. Build both libcs with `-Werror` and check that the checkpoint session prints the same log lines.

   <details><summary>Solution</summary>

   In `src/log.h`, replace the `enum` line:

   ```c
   /* X(name, text): one line per level. */
   #define LOG_LEVELS(X)          \
       X(LOG_DEBUG, "debug")      \
       X(LOG_INFO,  "info")       \
       X(LOG_WARN,  "warning")    \
       X(LOG_ERROR, "error")

   #define LOG_AS_ENUM(name, text) name,
   enum log_level { LOG_LEVELS(LOG_AS_ENUM) };
   #undef LOG_AS_ENUM
   ```

   In `src/log.c`, add `#include <stddef.h>` and replace `level_name`:

   ```c
   static const char *level_name(enum log_level level)
   {
   #define LOG_AS_TEXT(name, text) text,
       static const char *const names[] = { LOG_LEVELS(LOG_AS_TEXT) };
   #undef LOG_AS_TEXT
       if (level >= 0 && (size_t)level < sizeof names / sizeof names[0])
           return names[level];
       return "?";
   }
   ```

   The trailing comma after the last enumerator is allowed in C99 (not C89). The names table follows the enum's order automatically, since both come from one list. The bounds check keeps a bad value (say, from a cast) from reading past the table. `#undef` right after each use keeps the helper macros out of everyone's way. `make && make LIBC=musl` builds without warnings, and the session prints the same `parleyd: info: ...` and `parleyd: debug: ...` lines as before.

   Worth it for four levels? Barely: the `switch` was fine, and `-Wall` (`-Wswitch`) already warns about a missing `case`. X-macros pay off when a list is long or expands in three or more places, like the command table.

   </details>

3. **`make both`.** Add a phony target `both` that builds the glibc and the musl versions with one command.

   <details><summary>Solution</summary>

   Add `both` to `.PHONY` and append the rule:

   ```make
   .PHONY: all clean both

   both:
   	$(MAKE) LIBC=glibc
   	$(MAKE) LIBC=musl
   ```

   ```console
   $ make both
   make LIBC=glibc
   make[1]: Entering directory '.../ch09'
   mkdir -p build/glibc
   cc -D_POSIX_C_SOURCE=200809L -MMD -MP -O2 -g -std=c99 -Wall -Wextra -pedantic -Werror -c -o build/glibc/parleyd.o src/parleyd.c
   ...
   make[1]: Leaving directory '.../ch09'
   make LIBC=musl
   make[1]: Entering directory '.../ch09'
   mkdir -p build/musl
   musl-gcc -D_POSIX_C_SOURCE=200809L -MMD -MP -O2 -g -std=c99 -Wall -Wextra -pedantic -Werror -c -o build/musl/parleyd.o src/parleyd.c
   ...
   make[1]: Leaving directory '.../ch09'
   ```

   One make run has one value of `LIBC`, on which `CC`, `BUILD` and the object lists depend, so `both` runs make again once per libc. Always write `$(MAKE)`, never plain `make`: it is the same make program (with its path), and make passes flags such as `-j` and `-k` and command-line variables down to it. `make[1]` is the nesting level.

   </details>

4. **Protect your flags.** Change the Makefile so that `make CFLAGS=-O0` still compiles with `-std=c99 -Wall -Wextra -pedantic -Werror`. (See the gotcha in "The final Makefile".)

   <details><summary>Solution</summary>

   Change the second `CFLAGS` line to:

   ```make
   override CFLAGS += -std=c99 -Wall -Wextra -pedantic -Werror
   ```

   ```console
   $ make CFLAGS=-O0 build/glibc/str.o
   mkdir -p build/glibc
   cc -D_POSIX_C_SOURCE=200809L -MMD -MP -O0 -std=c99 -Wall -Wextra -pedantic -Werror -c -o build/glibc/str.o src/str.c
   ```

   A command-line variable normally overrides every assignment in the Makefile; `override` makes this one apply anyway. Being `+=`, it appends to the user's `-O0` instead of replacing it: the user still chooses optimization and debug flags, and the language and warning flags stay. (Asking for one object, `build/glibc/str.o`, is a quick way to see one compile line.) The same trick works for `CPPFLAGS`, where losing `-D_POSIX_C_SOURCE` would break the build from [Chapter 12](12-files-and-descriptors.md) on.

   </details>

5. **Answer without your notes.**
   1. What does `-MP` add to a `.d` file, and which error does it prevent?
   2. Why does `log.c` declare `log_prog` and `log_verbose` `static`?
   3. Why must the body of `#define SQUARE(x) ((x) * (x))` have every set of parentheses, and what does `SQUARE(i++)` do?
   4. The client never calls `msg_take_line`. Why does linking `parley` still need `strbuf.o`?
   5. Why does `make` rebuild `room.o` when you touch `nick.h`, even though `room.c` never includes it?

   <details><summary>Solution</summary>

   1. An empty rule for every header (`src/nick.h:`). Otherwise, after you delete or rename a header, make stops with "No rule to make target 'src/nick.h', needed by ...". With the empty rule, a missing header just counts as changed, and the object is rebuilt.
   2. To give them internal linkage: no other file can see or change them, only `log_init` can, and two modules can each have a private `verbose` without a "multiple definition" error. `nm` shows them in lowercase (`d`, `b`).
   3. The parentheses around each `x` keep `SQUARE(a + 1)` from becoming `a + 1 * a + 1`. The outer pair keeps `100 / SQUARE(2)` from becoming `100 / (2) * (2)`. `SQUARE(i++)` becomes `((i++) * (i++))`: two unsequenced changes to `i`, which is undefined behaviour. Use a function.
   4. The linker links whole object files. `parley` needs `msg_format` from `msg.o`, and `msg.o` also contains `msg_take_line`, which calls `sb_clear` and `sb_consume`. Every undefined symbol in a linked object must be defined somewhere.
   5. `room.c` includes `room.h`, which includes `conn.h`, which includes `nick.h`. `-MMD` records every header the preprocessor actually opened, at any depth, so `build/glibc/room.d` lists `src/nick.h`.

   </details>

Sources for this chapter: [REFERENCES.md](REFERENCES.md#chapter-9--the-preprocessor-and-building)
