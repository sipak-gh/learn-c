# Chapter 1 — Getting Started

C's history and ideas, C99, POSIX, Linux and the C library. You'll compile a first program with glibc
and musl, follow it through the compiler's four stages, read error messages, and start the project
(`parleyd` and `parley`). Next: [Chapter 2](02-types-and-operators.md), types and operators.

## How this course works

- Read the chapters in order ([README](README.md)); each uses only earlier material, apart from a few
  marked forward references.
- Each concept gets a small, complete program with its real output. Each chapter grows the project,
  **parley**, a chat server (`parleyd`) and client (`parley`), and ends with exercises (solutions
  collapsed).

### Conventions

- **Commands:** type the lines starting with `$ ` (without the `$`); the rest is output.
- **Examples:** the path above an example (`examples/ch01/hello.c`) is its file; run commands in that
  folder.
- **Project snapshots:** `project/chNN/` is the project after chapter NN. Type the code yourself; use the
  snapshot to compare or catch up.
- **Varying output:** addresses, process IDs, dates and file sizes vary; the text says where.
- **Clean up:** delete the executables you build (`rm hello hello-musl`).

### What you need

Install the tools from your distribution's packages (not covered here). Verified with:

| Tool | Version | Check with |
|---|---|---|
| gcc (or clang) | 11 or newer (verified with 11.4) | `gcc --version` |
| glibc | 2.35 or newer | `ldd --version` |
| musl, with its `musl-gcc` wrapper | 1.2.x (verified with 1.2.6) | run musl's loader `/lib/ld-musl-x86_64.so.1` |
| GNU make, gdb, strace, nc | any recent | from [Chapter 9](09-preprocessor-and-building.md) on |

```console
$ gcc --version | head -n 1
gcc (Ubuntu 11.4.0-1ubuntu1~22.04.3) 11.4.0
$ ldd --version | head -n 1
ldd (Ubuntu GLIBC 2.35-0ubuntu3.15) 2.35
```

Without arguments, musl's loader prints `musl libc (x86_64)` and `Version 1.2.6` (or yours). Distributions
usually package musl as `musl-tools` or `musl-dev`.

## What C is

C is a small, compiled, statically typed language. Dennis Ritchie created it at Bell Labs in 1969–1973 to
write Unix, from Ken Thompson's B, which came from BCPL. In 1973 the Unix kernel was rewritten in C, and
the two spread together.

C runs wherever software is close to the hardware: kernels (Linux, the BSDs), C libraries, databases
(SQLite, PostgreSQL), language runtimes (CPython, Lua, Ruby), servers (nginx), embedded devices. Almost
every language can call C, making it the common language between languages.

### The ideas behind C

The C standard committee's *Rationale* describes the "spirit of C":

> - Trust the programmer.
> - Don't prevent the programmer from doing what needs to be done.
> - Keep the language small and simple.
> - Provide only one way to do an operation.
> - Make it fast, even if it is not guaranteed to be portable.

In practice:

| Idea | Meaning |
|---|---|
| Small language | 37 keywords in C99; most work is in libraries. |
| Close to the machine | Types are the CPU's bytes, integers, floats and addresses; a line of C is a few instructions. |
| Trust the programmer | No bounds checks, garbage collector or exceptions; you manage memory and errors. |
| Fast over safe | Some mistakes are **undefined behaviour**: the result is undefined, so the compiler may assume they never happen. |
| Portable through a standard | ISO-conforming code compiles on any conforming compiler. |
| Portable *or* machine-specific | The committee didn't want to "preclude the use of C as a high-level assembler". You choose. |

This course chooses portable: strict C99 plus POSIX, checked on two C libraries.

## Which C: the standards

| Name | Year | Document | What it brought |
|---|---|---|---|
| K&R C | 1978 | *The C Programming Language* (book) | the first description of C |
| C89 / C90 | 1989 / 1990 | ANSI X3.159-1989, then ISO/IEC 9899:1990 | the first standard: prototypes, `void`, `const`, the standard library |
| C95 | 1995 | Amendment 1 | wide-character library additions |
| **C99** | 1999 | ISO/IEC 9899:1999 | `//` comments, declarations anywhere, `for (int i = ...)`, `long long`, `<stdint.h>`, `<stdbool.h>`, `snprintf`, designated initializers, compound literals, `inline`, `restrict`, flexible array members, variable-length arrays |
| C11 | 2011 | ISO/IEC 9899:2011 | threads, atomics, `_Generic`, `_Static_assert`, anonymous structs |
| C17 | 2018 | ISO/IEC 9899:2018 | bug fixes only |
| C23 | 2024 | ISO/IEC 9899:2024 | `bool`/`true`/`false` and `nullptr` as keywords, `typeof`, `#embed`, `[[attributes]]`, binary literals |

Why **C99**, strictly:
- Most C code is C99. It has what modern programs need: fixed-width integers, `bool`, `snprintf`,
  declarations where you use them.
- gcc and clang have supported it for well over a decade.
- POSIX.1-2008 (files, sockets, threads) builds on C99, and even specifies a `c99` compiler command.
- Later standards are mentioned (mostly in [Chapter 22](22-shipping-and-libc-choices.md)), never used.

gcc 11 defaults to `gnu17` (C17 plus GNU extensions), so every command says `-std=c99`. That disables GNU
extensions that conflict with C99; `-pedantic` warns about the rest.

## C, POSIX and Linux

A C program on Linux uses three layers of interfaces; the course labels each function's layer.

| Layer | Defined by | Examples | Portable to |
|---|---|---|---|
| ISO C (C99) | the C standard | `printf`, `fopen`, `malloc`, `strlen`, `strtol`, `qsort` | every C implementation, even without an OS |
| POSIX.1-2008 | IEEE/The Open Group | `read`, `write`, `fork`, `socket`, `poll`, `pthread_create`, `getline`, `strdup` | Linux, the BSDs, macOS, other Unixes |
| Linux | the Linux kernel | `epoll`, `signalfd`, `accept4` | Linux only |

Part I (Chapters 1–11) is almost all ISO C; Part II adds POSIX for files, signals, sockets, threads and
terminals. Linux-only calls are mentioned, never used in the project. C99 programs request POSIX functions
with a *feature-test macro* (why: [Chapter 4](04-functions.md); project setup:
[Chapter 9](09-preprocessor-and-building.md)).

## What a C library is

C itself can't print, open files or allocate memory; the **C library** ("libc") does. On Linux it
contains:

- **the ISO C standard library**: `<stdio.h>`, `<stdlib.h>`, `<string.h>`, `<math.h>` and the rest;
- **the POSIX functions**: `<unistd.h>`, `<sys/socket.h>`, `<pthread.h>` and many more;
- **system call wrappers**: small functions like `write()` that ask the kernel to act;
- **start-up code**: `_start`, which calls `main` and passes its return value to `exit`;
- **headers**: the `.h` files declaring all this;
- for dynamic linking, **the dynamic loader**, which loads shared libraries at program start.

The compiler and the C library are separate projects, so one compiler can use different libraries.

### glibc and musl

| | glibc (the GNU C Library) | musl |
|---|---|---|
| Project | GNU, from about 1988 | Rich Felker, first released 2011 |
| License | LGPL | MIT |
| Used by | most distributions (Debian, Ubuntu, Fedora, Arch) | Alpine, Void (option); static tools everywhere |
| Goal | complete, fast, compatible with decades of software; many GNU extensions | small, simple, correct, standards-first; "size-efficient when static-linked" |
| Usual linking | dynamic (`libc.so.6`, ≈2.2 MB, shared by all programs) | static (only what you use is copied in) |
| Used here via | `gcc` | `musl-gcc -static` |

`musl-gcc` is a small wrapper making gcc use musl's headers, start-up code and libraries.

Why two? They implement the same standards but differ where the standards leave room, and in extensions.
Comparing them shows what C promises and what is one library's choice. Each difference gets a
**"glibc vs musl:"** heading with both outputs and a verdict on which is simpler or better;
[Appendix B](appendix-b-glibc-vs-musl.md) lists them all.

## Your first program

`examples/ch01/hello.c`

```c
/* hello.c - the first program: print one line, exit with status 0. */
#include <stdio.h>

int main(void)
{
    printf("hello, world\n");
    return 0;
}
```

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o hello hello.c
$ ./hello
hello, world
$ musl-gcc -std=c99 -Wall -Wextra -pedantic -static -o hello-musl hello.c
$ ./hello-musl
hello, world
```

- `/* ... */` is a comment. C99 also allows `// to the end of the line`.
- `#include <stdio.h>` pastes in the header declaring `printf`. `#` lines are for the *preprocessor*,
  which runs before the compiler proper.
- `int main(void)` defines `main`, where every program starts; it returns an `int` and takes no arguments
  (`void`). The other form, `main(int argc, char *argv[])`, is in [Chapter 4](04-functions.md).
- `{ ... }` is the function body.
- `printf("hello, world\n");` prints a string; `\n` is a newline. Statements end with `;`.
- `return 0;` ends `main`. The value becomes the *exit status*: 0 means success.

No compiler output means no warnings. Both programs behave the same.

## From source to program: four stages

`gcc` is a driver that runs four tools in turn:

```text
hello.c --preprocess--> hello.i --compile--> hello.s --assemble--> hello.o --link--> hello
          (cpp)        C text     (cc1)       assembly    (as)      object     (ld)     executable
                                                                    + libc
```

`-save-temps` keeps the intermediate files:

```console
$ gcc -std=c99 -save-temps -o hello hello.c
$ ls hello*
hello  hello-musl  hello.c  hello.i  hello.o  hello.s
```

You can stop after any stage:

| Flag | Stops after | Output |
|---|---|---|
| `-E` | preprocessing | C text, to stdout |
| `-S` | compiling | assembly, `file.s` |
| `-c` | assembling | object file, `file.o` |
| (none) | linking | executable, `a.out` or the `-o` name |

### 1. Preprocessing (`-E`)

The preprocessor handles `#` lines and removes comments. `#include <stdio.h>` becomes the header's text,
so five lines turn into hundreds:

```console
$ gcc -std=c99 -E hello.c | wc -l
552
$ gcc -std=c99 -E hello.c | grep 'int printf'
extern int printf (const char *__restrict __format, ...);
$ gcc -std=c99 -E hello.c | tail -n 6
# 4 "hello.c"
int main(void)
{
    printf("hello, world\n");
    return 0;
}
```

The comment is gone and `printf` is declared: a format string plus more arguments, returning `int`. That's
all the compiler needs.

### glibc vs musl: what the headers contain

```console
$ gcc -std=c99 -E hello.c | wc -l
552
$ musl-gcc -std=c99 -E hello.c | wc -l
118
$ musl-gcc -std=c99 -E hello.c | grep 'int printf'
int printf(const char *restrict, ...);
```

Same function, same meaning. glibc's `stdio.h` pulls in internal headers, compiler annotations and GNU
extensions; musl's has plain declarations. **musl is simpler to read**: to see what a function looks like,
open musl's header. glibc's annotations buy extra compiler checks and, with optimization, hardened versions
of some functions (`__printf_chk` on Ubuntu).

### 2. Compiling (`-S`)

The compiler proper turns C into assembly for your CPU (x86-64 here). `-fno-asynchronous-unwind-tables`
just hides some debugging directives:

```console
$ gcc -std=c99 -S -fno-asynchronous-unwind-tables hello.c
$ head -n 18 hello.s
	.file	"hello.c"
	.text
	.section	.rodata
.LC0:
	.string	"hello, world"
	.text
	.globl	main
	.type	main, @function
main:
	endbr64
	pushq	%rbp
	movq	%rsp, %rbp
	leaq	.LC0(%rip), %rax
	movq	%rax, %rdi
	call	puts@PLT
	movl	$0, %eax
	popq	%rbp
	ret
```

You needn't read assembly, but notice:
- The string is stored once, in read-only data (`.rodata`), **without** its `\n`.
- The call is to `puts`, not `printf`: `printf` of a plain string ending in `\n` equals the cheaper `puts`
  of the string without it, and the standard allows any change that keeps visible behaviour.

### 3. Assembling (`-c`)

The assembler turns assembly into machine code in an *object file*. `-c` stops before linking:

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -c hello.c
$ file hello.o
hello.o: ELF 64-bit LSB relocatable, x86-64, version 1 (SYSV), not stripped
$ nm hello.o
0000000000000000 T main
                 U puts
```

`nm` lists the object's *symbols*. `T main`: defined here, in the code (text) section. `U puts`: used but
not defined here.

### 4. Linking

The linker (`ld`) combines objects and libraries into an executable: it finds `puts` in the C library,
adds the start-up code that calls `main`, and fills in addresses:

```console
$ gcc -o hello hello.o
$ ./hello
hello, world
```

The C library is chosen at link time (`gcc`: glibc, `musl-gcc`: musl), but the object was compiled with
one library's headers, so compile *and* link with the same driver. Real projects compile each `.c` file
with `-c` and link at the end, like the Makefile in [Chapter 9](09-preprocessor-and-building.md).

## Compiler flags

| Flag | Meaning |
|---|---|
| `-std=c99` | ISO C99 (`__STDC_VERSION__` is `199901L`); conflicting GNU extensions off |
| `-Wall` | the most useful warnings (not *all*) |
| `-Wextra` | more: unused parameters, signed/unsigned comparisons, missing `break`s... |
| `-pedantic` | warn about anything ISO C99 forbids (e.g. GNU extensions) |
| `-Werror` | warnings become errors (project, from [Chapter 9](09-preprocessor-and-building.md)) |
| `-O2` | optimize: faster, harder to debug (default `-O0`: none) |
| `-g` | debug information for gdb ([Chapter 10](10-errors-and-debugging.md)) |
| `-o NAME` | output file name (default `a.out`) |
| `-static` | copy libraries into the program, not load them at run time |

Warnings find real bugs. This program has four problems:

```c
#include <stdio.h>

int main(int argc, char *argv[])
{
    int flags = 0b101;
    printf("%d\n", "flags");
    return 0;
}
```

| Command | Warnings |
|---|---|
| `gcc -o warn warn.c` | format `'%d'` expects `int`, got `char *` (on by default in Ubuntu's gcc; elsewhere it needs `-Wall`) |
| `gcc -std=c99 -Wall ...` | + unused variable `'flags'` |
| `gcc -std=c99 -Wall -Wextra ...` | + unused parameters `'argc'`, `'argv'` |
| `gcc -std=c99 -Wall -Wextra -pedantic ...` | + "binary constants are a C2X feature or GCC extension" |

With all four flags:

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o warn warn.c
warn.c: In function 'main':
warn.c:5:17: warning: binary constants are a C2X feature or GCC extension
    5 |     int flags = 0b101;
      |                 ^~~~~
warn.c:6:14: warning: format '%d' expects argument of type 'int', but argument 2 has type 'char *' [-Wformat=]
    6 |     printf("%d\n", "flags");
      |             ~^     ~~~~~~~
      |              |     |
      |              int   char *
      |             %s
warn.c:5:9: warning: unused variable 'flags' [-Wunused-variable]
    5 |     int flags = 0b101;
      |         ^~~~~
warn.c:3:14: warning: unused parameter 'argc' [-Wunused-parameter]
    3 | int main(int argc, char *argv[])
      |          ~~~~^~~~
warn.c:3:26: warning: unused parameter 'argv' [-Wunused-parameter]
    3 | int main(int argc, char *argv[])
      |                    ~~~~~~^~~~~~
```

Each message gives file, line, column, problem and warning option. `0b101` is why `-pedantic` exists: gcc
and C23 accept it, C99 doesn't, and other C99 compilers may reject it.

Course rule: **everything compiles with `-std=c99 -Wall -Wextra -pedantic` and no warnings.** A warning is
a free bug report.

## Reading errors: which stage failed?

The stage reporting an error tells you the kind of mistake. A missing semicolon is a **compile error**;
no program is made:

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o semi semi.c
semi.c: In function 'main':
semi.c:5:29: error: expected ';' before 'return'
    5 |     printf("hello, world\n")
      |                             ^
      |                             ;
    6 |     return 0;
      |     ~~~~~~                   
```

A forgotten `#include <stdio.h>` is only a **warning** with gcc 11:

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o noinc noinc.c
noinc.c: In function 'main':
noinc.c:3:5: warning: implicit declaration of function 'printf' [-Wimplicit-function-declaration]
    3 |     printf("hello, world\n");
      |     ^~~~~~
noinc.c:1:1: note: include '<stdio.h>' or provide a declaration of 'printf'
  +++ |+#include <stdio.h>
    1 | int main(void)
noinc.c:3:5: warning: incompatible implicit declaration of built-in function 'printf' [-Wbuiltin-declaration-mismatch]
    3 |     printf("hello, world\n");
      |     ^~~~~~
noinc.c:3:5: note: include '<stdio.h>' or provide a declaration of 'printf'
```

Before C99 an undeclared function was assumed to return `int`. C99 dropped that rule, but gcc only warns
so old code still builds. The guess is often wrong (functions returning pointers break on 64-bit
machines); take it seriously. `-Werror` makes it an error:

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -Werror -o noinc noinc.c
noinc.c: In function 'main':
noinc.c:3:5: error: implicit declaration of function 'printf' [-Werror=implicit-function-declaration]
    3 |     printf("hello, world\n");
      |     ^~~~~~
noinc.c:1:1: note: include '<stdio.h>' or provide a declaration of 'printf'
  +++ |+#include <stdio.h>
    1 | int main(void)
noinc.c:3:5: error: incompatible implicit declaration of built-in function 'printf' [-Werror=builtin-declaration-mismatch]
    3 |     printf("hello, world\n");
      |     ^~~~~~
noinc.c:3:5: note: include '<stdio.h>' or provide a declaration of 'printf'
cc1: all warnings being treated as errors
```

A misspelled function name compiles (with a similar warning) but fails to **link**. The `/tmp/cc....o`
name (gcc's temporary object) varies:

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o typo typo.c
typo.c: In function 'main':
typo.c:5:5: warning: implicit declaration of function 'prinft'; did you mean 'printf'? [-Wimplicit-function-declaration]
    5 |     prinft("hello, world\n");
      |     ^~~~~~
      |     printf
/usr/bin/ld: /tmp/ccIsahfh.o: in function `main':
typo.c:(.text+0x18): undefined reference to `prinft'
collect2: error: ld returned 1 exit status
```

`undefined reference` always comes from the linker (`ld`, run by gcc's `collect2`): a name used but never
defined. Misspell `main` (`int mian(void)`) and the start-up code can't find it:

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o nomain nomain.c
/usr/bin/ld: /usr/lib/gcc/x86_64-linux-gnu/11/../../../x86_64-linux-gnu/Scrt1.o: in function `_start':
(.text+0x1b): undefined reference to `main'
collect2: error: ld returned 1 exit status
```

`Scrt1.o` is glibc's start-up code, the `_start` that calls `main`.

| Message contains | Stage | Typical cause |
|---|---|---|
| `fatal error: ...: No such file or directory` on an `#include` | preprocessor | misspelled header |
| `error:` with a line and column | compiler | syntax or type error |
| `warning:` | compiler | legal but suspicious; fix it anyway |
| `undefined reference to` | linker | misspelled name, missing `.c` file or library |

Fix the **first** error first; later ones are often caused by it.

## main and the exit status

The start-up code passes `main`'s return value to `exit`. The parent process (your shell) gets it as the
*exit status*, kept in `$?`.

`examples/ch01/status.c`

```c
/* status.c - main's return value becomes the program's exit status. */
#include <stdio.h>
#include <stdlib.h>   /* EXIT_SUCCESS, EXIT_FAILURE */

int main(void)
{
    printf("something went wrong\n");
    return EXIT_FAILURE;    /* 1 on every Unix system */
}
```

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o status status.c
$ ./status
something went wrong
$ echo $?
1
$ ./status && echo ok || echo "failed with $?"
something went wrong
failed with 1
$ ./hello && echo ok
hello, world
ok
```

- **0 means success**, anything else failure; shell `&&`, `||`, `if` and `make` rely on this.
- `<stdlib.h>` defines `EXIT_SUCCESS` (0) and `EXIT_FAILURE` (1); C promises only these and 0.
- Unix passes only the low 8 bits to the parent: 0–255.
- Convention (parley follows it): 1 for "it failed", 2 for "you used me wrong" (bad options).

C99 added a rule: reaching the closing `}` of `main` without a `return` returns 0.

`examples/ch01/noreturn.c`

```c
/* noreturn.c - main without a return statement. */
#include <stdio.h>

int main(void)
{
    printf("hello, world\n");
    /* No return: in C99, reaching the end of main returns 0. */
}
```

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o noreturn noreturn.c
$ ./noreturn; echo $?
hello, world
0
$ musl-gcc -std=c99 -Wall -Wextra -pedantic -static -o noreturn-musl noreturn.c
$ ./noreturn-musl; echo $?
hello, world
0
```

In C89 it returns garbage: whatever `printf` left in the return register, here 13 (characters printed).

```console
$ gcc -std=c89 -Wall -Wextra -pedantic -o noreturn89 noreturn.c
noreturn.c: In function 'main':
noreturn.c:8:1: warning: control reaches end of non-void function [-Wreturn-type]
    8 | }
      | ^
$ ./noreturn89; echo $?
hello, world
13
```

The course still writes `return 0;`: it says what you mean.

## Which standard, which library?

The compiler and library announce themselves with *predefined macros*: names the preprocessor replaces
with values. `#ifdef` keeps lines only if a macro exists ([Chapter 9](09-preprocessor-and-building.md)).

`examples/ch01/whoami.c`

```c
/* whoami.c - which C standard and which C library was this built with? */
#include <stdio.h>

int main(void)
{
    /* __STDC_VERSION__ is set by the compiler: 199901L means C99. */
    printf("C standard: %ld\n", __STDC_VERSION__);

    /* #ifdef keeps or drops lines before compiling (Chapter 9).
     * glibc's headers define __GLIBC__; musl defines no such macro. */
#ifdef __GLIBC__
    printf("C library:  glibc %d.%d\n", __GLIBC__, __GLIBC_MINOR__);
#else
    printf("C library:  not glibc (musl has no version macro, on purpose)\n");
#endif
    return 0;
}
```

`%ld` and `%d` print numbers ([Chapter 2](02-types-and-operators.md)).

```console
$ gcc -o whoami-default whoami.c
$ ./whoami-default
C standard: 201710
C library:  glibc 2.35
$ gcc -std=c99 -Wall -Wextra -pedantic -o whoami whoami.c
$ ./whoami
C standard: 199901
C library:  glibc 2.35
$ musl-gcc -std=c99 -Wall -Wextra -pedantic -static -o whoami-musl whoami.c
$ ./whoami-musl
C standard: 199901
C library:  not glibc (musl has no version macro, on purpose)
```

Without `-std`, gcc 11 compiles C17 (201710). musl has no `__MUSL__` macro on purpose: test for the
*feature* you need, not the library's name, because such guesses break when the library changes. Good
advice for any library.

## glibc vs musl: the executables

Build hello three ways: glibc dynamic (normal), glibc static, musl static.

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o hello hello.c
$ gcc -std=c99 -Wall -Wextra -pedantic -static -o hello-static hello.c
$ musl-gcc -std=c99 -Wall -Wextra -pedantic -static -o hello-musl hello.c
$ ls -l hello hello-static hello-musl
-rwxr-xr-x 1 tbangma tbangma  15960 Oct 30 16:05 hello
-rwxr-xr-x 1 tbangma tbangma  18536 Oct 30 16:05 hello-musl
-rwxr-xr-x 1 tbangma tbangma 900368 Oct 30 16:05 hello-static
$ size hello hello-static hello-musl
   text	   data	    bss	    dec	    hex	filename
   1375	    600	      8	   1983	    7bf	hello
 781725	  23240	  23016	 827981	  ca24d	hello-static
   3953	    336	   1680	   5969	   1751	hello-musl
$ file hello hello-static hello-musl
hello:        ELF 64-bit LSB pie executable, x86-64, version 1 (SYSV), dynamically linked, interpreter /lib64/ld-linux-x86-64.so.2, BuildID[sha1]=b430ec54d9476bd9d194bc18f8371ac521b58c56, for GNU/Linux 3.2.0, not stripped
hello-static: ELF 64-bit LSB executable, x86-64, version 1 (GNU/Linux), statically linked, BuildID[sha1]=98aafdd4ee6ecc570368da2670dc34b2d76f78ad, for GNU/Linux 3.2.0, not stripped
hello-musl:   ELF 64-bit LSB executable, x86-64, version 1 (SYSV), statically linked, not stripped
$ ldd hello
	linux-vdso.so.1 (0x00007ffe0a3fd000)
	libc.so.6 => /lib/x86_64-linux-gnu/libc.so.6 (0x0000748571200000)
	/lib64/ld-linux-x86-64.so.2 (0x00007485714e5000)
$ ldd hello-musl; echo $?
	not a dynamic executable
1
```

Sizes, dates, BuildIDs and `ldd` addresses vary.

- `size` shows `text` (code and constants), `data` (initialized variables) and `bss` (zero-initialized
  variables).
- **glibc dynamic** (16 KB) is small because libc isn't inside. At start-up the dynamic loader
  `/lib64/ld-linux-x86-64.so.2` loads the shared `libc.so.6`; `ldd` lists what it will load. It's a *PIE*
  (position-independent executable), so it can be loaded at a random address, for security.
- **glibc static** (900 KB) copies in much of glibc: `printf` drags in locale, wide-character and other
  machinery.
- **musl static** (18 KB) holds only what hello uses and needs nothing at run time. `ldd` says "not a
  dynamic executable" and exits 1.

Which is better?
- **glibc dynamic** for programs installed on your own glibc system: small, with free library security
  updates.
- **musl static** for a program you copy anywhere (container, rescue disk, other distribution): one small,
  self-contained file. glibc static is 50 times bigger, and some glibc functions still need shared
  libraries at run time ([Chapter 14](14-networking-basics.md)).

[Chapter 22](22-shipping-and-libc-choices.md) revisits the choice for the finished server.

### Same sizes on both: the ABI

`examples/ch01/sizes.c`

```c
/* sizes.c - how many bytes each basic type takes on this machine. */
#include <stdio.h>

int main(void)
{
    /* sizeof gives a size in bytes; %zu is the printf format for it. */
    printf("char        %zu\n", sizeof(char));
    printf("short       %zu\n", sizeof(short));
    printf("int         %zu\n", sizeof(int));
    printf("long        %zu\n", sizeof(long));
    printf("long long   %zu\n", sizeof(long long));
    printf("float       %zu\n", sizeof(float));
    printf("double      %zu\n", sizeof(double));
    printf("long double %zu\n", sizeof(long double));
    printf("pointer     %zu\n", sizeof(void *));  /* pointers: Chapter 5 */
    return 0;
}
```

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o sizes sizes.c
$ ./sizes
char        1
short       2
int         4
long        8
long long   8
float       4
double      8
long double 16
pointer     8
$ musl-gcc -std=c99 -Wall -Wextra -pedantic -static -o sizes-musl sizes.c
$ ./sizes-musl
char        1
short       2
int         4
long        8
long long   8
float       4
double      8
long double 16
pointer     8
```

Identical by design. Type sizes, struct layout and argument passing are fixed by the platform's **ABI**
(application binary interface); on x86-64 Linux both libraries follow the System V x86-64 psABI. Other
platforms differ: on 64-bit Windows `long` is 4 bytes. [Chapter 2](02-types-and-operators.md) shows how
to write code that doesn't care.

## Building parley: two programs that introduce themselves

parley is two programs: `parleyd`, the server ("d" for daemon, Unix's word for a background service), and
`parley`, the client. By [Chapter 22](22-shipping-and-libc-choices.md) they're a working chat. Today each
prints a banner and a usage line.

Make a project folder, such as `~/parley`, with two files:

`parleyd.c`

```c
/* parleyd.c - the parley chat server (Chapter 1: it only introduces itself). */
#include <stdio.h>

int main(void)
{
    printf("parleyd 0.1 - a tiny IRC-style chat server\n");
    printf("usage: parleyd [-p PORT] [-b ADDR] [-m MAXCLIENTS] [-v]\n");
    return 0;
}
```

`parley.c`

```c
/* parley.c - the parley chat client (Chapter 1: it only introduces itself). */
#include <stdio.h>

int main(void)
{
    printf("parley 0.1 - a tiny IRC-style chat client\n");
    printf("usage: parley [-h HOST] [-p PORT] [-n NICK]\n");
    return 0;
}
```

The options ([Chapter 3](03-control-flow.md) implements them): `-p PORT`, the TCP port, default 6667 (the
traditional IRC port); `-b ADDR`, the server's listening address; `-h HOST`, the server the client connects
to; `-m MAXCLIENTS`, the most users at once; `-v`, verbose logging; `-n NICK`, your nickname.

Until the Makefile in [Chapter 9](09-preprocessor-and-building.md), each program is one file, built with
one command per C library; musl builds get a `-musl` suffix.

### Checkpoint

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o parleyd parleyd.c
$ gcc -std=c99 -Wall -Wextra -pedantic -o parley parley.c
$ ./parleyd
parleyd 0.1 - a tiny IRC-style chat server
usage: parleyd [-p PORT] [-b ADDR] [-m MAXCLIENTS] [-v]
$ echo $?
0
$ ./parley
parley 0.1 - a tiny IRC-style chat client
usage: parley [-h HOST] [-p PORT] [-n NICK]
$ musl-gcc -std=c99 -Wall -Wextra -pedantic -static -o parleyd-musl parleyd.c
$ musl-gcc -std=c99 -Wall -Wextra -pedantic -static -o parley-musl parley.c
$ ./parleyd-musl
parleyd 0.1 - a tiny IRC-style chat server
usage: parleyd [-p PORT] [-b ADDR] [-m MAXCLIENTS] [-v]
$ ./parley-musl
parley 0.1 - a tiny IRC-style chat client
usage: parley [-h HOST] [-p PORT] [-n NICK]
$ file parleyd parleyd-musl
parleyd:      ELF 64-bit LSB pie executable, x86-64, version 1 (SYSV), dynamically linked, interpreter /lib64/ld-linux-x86-64.so.2, BuildID[sha1]=48fd70e1eb66b686eb02971fba4d49d05184a6cc, for GNU/Linux 3.2.0, not stripped
parleyd-musl: ELF 64-bit LSB executable, x86-64, version 1 (SYSV), statically linked, not stripped
$ ls -l parleyd parleyd-musl
-rwxr-xr-x 1 tbangma tbangma 15960 Oct 30 15:42 parleyd
-rwxr-xr-x 1 tbangma tbangma 18536 Oct 30 15:42 parleyd-musl
$ ldd parleyd-musl; echo $?
	not a dynamic executable
1
```

No compiler output means no warnings. BuildID, dates and sizes may differ.

The complete project at this point is in `project/ch01/`.

## Summary

- C is small, fast, portable, and trusts the programmer; uncaught mistakes are often *undefined
  behaviour*.
- Strict **C99** plus **POSIX.1-2008**. Layers: ISO C (`printf`), POSIX (`read`, `socket`), Linux-only
  (`epoll`).
- `gcc` stages: preprocess (`-E`), compile (`-S`), assemble (`-c`), link; the library is chosen at link.
- Build with `-std=c99 -Wall -Wextra -pedantic`; fix every warning.
- `undefined reference`: linker. `error:` with a line number: compiler.
- Exit status = `main`'s return value (0–255): 0 success, 1 failure, 2 usage error.
- libc = standard library + POSIX + system call wrappers + start-up code + headers.
- **glibc**: the usual Linux libc; dynamic, big, fast, many extensions. **musl**: small, strict, built for
  static linking (`musl-gcc -static`).
- Same x86-64 Linux ABI on both: same type sizes, some different behaviour.

## Exercises

1. Write `greet.c`: print `hello, NAME` (your name), then `goodbye`; exit with status 3. Build with both
   libraries and check `$?`.

<details><summary>Solution</summary>

```c
#include <stdio.h>

int main(void)
{
    printf("hello, Ada\n");
    printf("goodbye\n");
    return 3;
}
```

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o greet greet.c
$ ./greet; echo $?
hello, Ada
goodbye
3
$ musl-gcc -std=c99 -Wall -Wextra -pedantic -static -o greet-musl greet.c
$ ./greet-musl; echo $?
hello, Ada
goodbye
3
```

</details>

2. Change `status.c` to `return 300;`. What does `echo $?` print, and why? What would `return -1;` give?

<details><summary>Solution</summary>

```console
$ ./status; echo $?
something went wrong
44
```

Only the low 8 bits reach the shell: 300 − 256 = 44. `return -1;` gives 255 (−1 is all one-bits). The
kernel truncates, so both libraries agree. Keep statuses in 0–255; most programs use only 0, 1 and 2.

</details>

3. Which stage reports each broken program in "Reading errors"? Prove it for `prinft`: compile with `-c`
   only. Does it succeed? What does `nm` say?

<details><summary>Solution</summary>

Missing semicolon: compiler. Missing `#include`: compiler (a warning; an error with `-Werror`).
`prinft`: linker. `mian`: linker.

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -c typo.c
typo.c: In function 'main':
typo.c:5:5: warning: implicit declaration of function 'prinft'; did you mean 'printf'? [-Wimplicit-function-declaration]
    5 |     prinft("hello, world\n");
      |     ^~~~~~
      |     printf
$ echo $?
0
$ nm typo.o
0000000000000000 T main
                 U prinft
```

Compiling succeeds; the object just records that it needs `prinft`. Only the linker finds nothing
defines it.

</details>

4. Build `sizes.c` three ways (glibc dynamic, glibc static, musl static) and `strip` each (`strip` removes
   the symbol table). Which is smallest?

<details><summary>Solution</summary>

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o sizes sizes.c
$ gcc -std=c99 -Wall -Wextra -pedantic -static -o sizes-static sizes.c
$ musl-gcc -std=c99 -Wall -Wextra -pedantic -static -o sizes-musl sizes.c
$ strip sizes sizes-static sizes-musl
$ ls -l sizes sizes-static sizes-musl
-rwxr-xr-x 1 tbangma tbangma  14472 Oct 30 16:10 sizes
-rwxr-xr-x 1 tbangma tbangma  26008 Oct 30 16:10 sizes-musl
-rwxr-xr-x 1 tbangma tbangma 819664 Oct 30 16:10 sizes-static
```

glibc dynamic is smallest on disk (14.5 KB) but needs the 2.2 MB `libc.so.6` at run time. musl static
(26 KB) carries all its libc code; it's bigger than the stripped `hello-musl` (13.7 KB) because `printf`
with a format (`%zu`) can't become `puts`, so musl's formatting code is linked in. glibc static is 31
times bigger for the same work. Sizes vary with compiler versions.

</details>

5. Run `gcc -std=c99 -E` and `musl-gcc -std=c99 -E` on `sizes.c`, count the lines, and find `printf`'s
   declaration in each. Which header would you rather read?

<details><summary>Solution</summary>

```console
$ gcc -std=c99 -E sizes.c | wc -l
561
$ musl-gcc -std=c99 -E sizes.c | wc -l
127
$ gcc -std=c99 -E sizes.c | grep 'int printf'
extern int printf (const char *__restrict __format, ...);
$ musl-gcc -std=c99 -E sizes.c | grep 'int printf'
int printf(const char *restrict, ...);
```

Both counts are 9 more than for `hello.c` (552 and 118): `<stdio.h>` adds the same text regardless, and
`sizes.c` has 9 more lines. musl's declaration is shorter and plainer. glibc's parameter names have `__`
prefixes, reserved for the implementation so they can't clash with your macros.

</details>

Sources for this chapter: [REFERENCES.md](REFERENCES.md#chapter-1--getting-started)
