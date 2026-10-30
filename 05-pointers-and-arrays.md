# Chapter 5 — Pointers and Arrays

A pointer holds the address of another object. Pointers let C pass big data without copying, let a function change its caller's variables, walk arrays and strings, and pick functions at run time. They also cause most C bugs, so this chapter shows the crashes and compiler messages alongside the rules. In the project, the client learns slash commands (`/nick`, `/me`, `/quit`…) through a table of function pointers. [Chapter 4](04-functions.md) showed that arguments are copies; here you work around that. [Chapter 6](06-strings.md) builds strings on pointers.

## Memory and addresses

To your program, memory is one long row of numbered bytes; a byte's number is its **address**. A multi-byte object such as an `int` has the address of its first byte.

Where an object lives depends on its declaration:

| kind | example | lives | where |
|---|---|---|---|
| file scope, or `static` | `int global;` `static int counter;` | the whole run | the data area |
| string literal | `"hello"` | the whole run | a read-only area |
| automatic (local) | `int local;` inside a function | until its block ends | the stack |
| allocated | `malloc(…)` | until `free` | the heap ([Chapter 8](08-memory.md)) |

`%p` prints an address; it expects a `void *`, so cast with `(void *)`:

`examples/ch05/addresses.c`
```c
/* addresses.c - where different kinds of objects live in memory. */
#include <stdio.h>

int global = 1;                         /* file scope: lives for the whole run */

static void stack_depth(int level)
{
    int local = level;
    printf("  level %d local at %p\n", level, (void *)&local);
    if (level < 3)
        stack_depth(level + 1);
}

int main(void)
{
    static int counter = 0;             /* static local: lives for the whole run */
    int local = 2;                      /* automatic: lives until main returns */
    const char *literal = "hello";      /* the literal's bytes are read-only */

    printf("global   %p\n", (void *)&global);
    printf("static   %p\n", (void *)&counter);
    printf("literal  %p\n", (void *)literal);
    printf("local    %p\n", (void *)&local);
    printf("nested calls (the stack grows down):\n");
    stack_depth(1);
    return 0;
}
```

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o addresses addresses.c
$ ./addresses
global   0x635b26e1c010
static   0x635b26e1c018
literal  0x635b26e1a020
local    0x7ffd0eabc64c
nested calls (the stack grows down):
  level 1 local at 0x7ffd0eabc624
  level 2 local at 0x7ffd0eabc5f4
  level 3 local at 0x7ffd0eabc5c4
```

The addresses change every run: Linux places the program and stack randomly (**ASLR**, address space layout randomisation) to hinder attacks. The pattern stays:

- `global` and `counter` sit together in the data area; the literal is nearby, read-only.
- Locals are far away, near the top of the address space, on the stack.
- Each nested call's `local` is 0x30 (48) bytes *lower*: the x86-64 stack grows down, and each frame here takes 48 bytes.

The `static` local `counter` keeps its value between calls because it isn't on the stack; it's only *visible* inside `main`.

### glibc vs musl: where the program sits

```console
$ musl-gcc -std=c99 -Wall -Wextra -pedantic -static -o addresses-musl addresses.c
$ ./addresses-musl
global   0x407028
static   0x407144
literal  0x405018
local    0x7fff00b8829c
nested calls (the stack grows down):
  level 1 local at 0x7fff00b88274
  level 2 local at 0x7fff00b88244
  level 3 local at 0x7fff00b88214
```

The stack looks the same. The globals don't:

| | glibc build | musl `-static` build |
|---|---|---|
| global, static, literal | `0x635b26e1c010` (changes every run) | `0x407028` (the same every run) |
| why | a **PIE**: Ubuntu's gcc builds position-independent executables by default, loadable anywhere | static non-PIE: loaded at the linker's fixed address |

The link mode, not the libc, causes this: `musl-gcc -static` makes a non-PIE binary. A PIE is more secure (attackers can't guess data addresses); a fixed layout is easier to debug. ([Chapter 22](22-shipping-and-libc-choices.md) compares link modes.) Never depend on a particular address.

## Pointers: `&` and `*`

- `&x` gives the address of `x`.
- `int *p` declares `p` as a "pointer to `int`".
- `*p` is the object `p` points to; you can read and assign it (**dereferencing**).

`examples/ch05/basics.c`
```c
/* basics.c - addresses, & and *, pointer types, NULL. */
#include <stdio.h>

int main(void)
{
    int n = 42;
    int *p = &n;                    /* p holds the address of n */

    printf("n = %d, *p = %d\n", n, *p);
    *p = 7;                         /* write through the pointer */
    printf("after *p = 7: n = %d\n", n);

    double d = 2.5;
    double *q = &d;                 /* a pointer type names what it points to */
    *q *= 2;
    printf("d = %.1f\n", d);

    printf("sizeof n = %zu, sizeof d = %zu\n", sizeof n, sizeof d);
    printf("sizeof p = %zu, sizeof q = %zu\n", sizeof p, sizeof q);

    int *none = NULL;               /* points to nothing */
    if (none == NULL)
        printf("none is a null pointer\n");

    printf("&n   = %p\n", (void *)&n);
    printf("NULL = %p\n", (void *)none);
    return 0;
}
```

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o basics basics.c
$ ./basics
n = 42, *p = 42
after *p = 7: n = 7
d = 5.0
sizeof n = 4, sizeof d = 8
sizeof p = 8, sizeof q = 8
none is a null pointer
&n   = 0x7ffc882d84f4
NULL = (nil)
```

- A pointer's type says how many bytes `*p` covers and how to read them: an `int *` and a `double *` hold the same kind of address, but `*p` reads 4 bytes as an integer, `*q` 8 bytes as floating point.
- On x86-64, every pointer is 8 bytes.
- In a declaration, `*` belongs to the name: `int *a, b;` makes `a` a pointer and `b` a plain `int`. Declare one pointer per line.
- `NULL` (from `<stdio.h>`, `<stddef.h>` and others) is the **null pointer**, guaranteed to differ from every object's address: "no object". Functions return it for "not found" or "failed".

### glibc vs musl: printing a null pointer

```console
$ musl-gcc -std=c99 -Wall -Wextra -pedantic -static -o basics-musl basics.c
$ ./basics-musl | tail -2
&n   = 0x7fffbf968344
NULL = 0
```

glibc prints a null pointer as `(nil)`, musl as `0`; nothing else differs. `%p` output is "implementation-defined", so both are right. `(nil)` stands out in logs; `0` matches musl's plain-hex pointers. Don't make tests or parsers expect either: compare with `NULL` in code.

### Gotchas: the three classic pointer crashes

**1. Dereferencing `NULL`.** Address 0 is never mapped on Linux, so this crashes at once, which is the *good* case: you see the bug.

`nullptr.c`
```c
#include <stdio.h>

int main(void)
{
    int *p = NULL;
    printf("about to write through NULL\n");
    *p = 1;
    return 0;
}
```

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o nullptr nullptr.c
$ ./nullptr
about to write through NULL
Segmentation fault (core dumped)
$ echo $?
139
```

No compiler warning. musl crashes the same way: the kernel sends `SIGSEGV` whatever the libc.

**2. Using an uninitialized pointer.** A local pointer holds whatever bits were on the stack, so it may point anywhere:

`uninit.c`
```c
#include <stdio.h>

int main(void)
{
    int *p;
    *p = 1;
    return 0;
}
```

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -c uninit.c
uninit.c: In function ‘main’:
uninit.c:6:8: warning: ‘p’ is used uninitialized [-Wuninitialized]
    6 |     *p = 1;
      |     ~~~^~~
```

Initialize every pointer, to `NULL` if there's nothing better yet.

**3. Returning the address of a local.** The local dies on return, and the next call reuses its stack slot:

`dangling.c`
```c
#include <stdio.h>

static int *make_number(void)
{
    int n = 42;
    return &n;
}

int main(void)
{
    int *p = make_number();
    printf("%d\n", *p);
    return 0;
}
```

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o dangling dangling.c
dangling.c: In function ‘make_number’:
dangling.c:6:12: warning: function returns address of local variable [-Wreturn-local-addr]
    6 |     return &n;
      |            ^~
$ ./dangling
Segmentation fault (core dumped)
```

gcc saw the undefined behaviour and compiled `return &n` as `return NULL`, hence the crash. A pointer to a dead object is **dangling**. To hand data back, return it by value, write through a pointer parameter (next section), or allocate it (Chapter 8).

## Pointer parameters: changing the caller's variables

Arguments are copies ([Chapter 4](04-functions.md)), but a copied *address* still points at the original. So a function taking `int *` can change the caller's `int`:

`examples/ch05/swap.c`
```c
/* swap.c - pointer parameters let a function change the caller's variables. */
#include <stdio.h>

static void swap(int *a, int *b)
{
    int tmp = *a;
    *a = *b;
    *b = tmp;
}

/* Two results: the quotient and remainder go out through pointers. */
static void divide(int num, int den, int *quot, int *rem)
{
    *quot = num / den;
    *rem = num % den;
}

int main(void)
{
    int x = 1, y = 2;
    swap(&x, &y);
    printf("x = %d, y = %d\n", x, y);

    int q, r;
    divide(17, 5, &q, &r);
    printf("17 / 5 = %d remainder %d\n", q, r);
    return 0;
}
```

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o swap swap.c
$ ./swap
x = 2, y = 1
17 / 5 = 3 remainder 2
```

This is how C returns several results. The project's `strtol(text, &end, 10)` already did: it stores where it stopped reading into your `end`.

## Arrays and pointers

An array is a row of elements, *not* a pointer. But in almost every expression its name **decays** to a pointer to its first element: `arr` becomes `&arr[0]`. The exceptions: `sizeof arr`, `&arr`, and a string literal initializing an array.

`examples/ch05/decay.c`
```c
/* decay.c - an array is not a pointer, but it turns into one easily. */
#include <stdio.h>

/* 'int a[]' as a parameter means exactly 'int *a': the size is lost,
 * so the caller passes it. */
static int sum(const int *a, size_t n)
{
    int total = 0;
    for (size_t i = 0; i < n; i++)
        total += a[i];
    return total;
}

int main(void)
{
    int arr[5] = { 1, 2, 3, 4, 5 };
    int *p = arr;                       /* arr "decays" to &arr[0] */
    size_t n = sizeof arr / sizeof arr[0];

    printf("sizeof arr = %zu (5 ints), sizeof p = %zu (one pointer)\n",
           sizeof arr, sizeof p);
    printf("elements: %zu\n", n);
    printf("p == &arr[0]: %s\n", p == &arr[0] ? "yes" : "no");
    printf("arr[2] = %d, p[2] = %d\n", arr[2], p[2]);
    printf("sum = %d\n", sum(arr, n));

    int (*whole)[5] = &arr;             /* a pointer to the whole array */
    printf("p + 1 is %td bytes on, whole + 1 is %td bytes on\n",
           (char *)(p + 1) - (char *)p, (char *)(whole + 1) - (char *)whole);
    return 0;
}
```

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o decay decay.c
$ ./decay
sizeof arr = 20 (5 ints), sizeof p = 8 (one pointer)
elements: 5
p == &arr[0]: yes
arr[2] = 3, p[2] = 3
sum = 15
p + 1 is 4 bytes on, whole + 1 is 20 bytes on
```

Remember:

- `sizeof arr / sizeof arr[0]` counts elements **only where the array itself is in scope**; in a function, the parameter is a pointer.
- A parameter `int a[]` or `int a[5]` *is* `int *a`; the 5 is ignored. Hence `char *argv[]` equals `char **argv`.
- Since the size is lost, array-taking functions also take a length (`sum(arr, n)`), a pattern seen everywhere in C.
- `a[i]` is defined as `*(a + i)`, for arrays and pointers alike.
- `&arr` has type "pointer to array of 5 ints", `int (*)[5]`: the same address as `&arr[0]` but a different type, so `+ 1` moves 20 bytes. You'll rarely need it.

This keeps arrays cheap: passing one never copies it, and there's no hidden length field, so a C array is just its elements. The price: *you* carry the length.

### Gotchas: `sizeof` on a parameter, and assigning arrays

`arraybad.c`
```c
#include <stdio.h>

static void show(int a[])
{
    printf("sizeof a = %zu\n", sizeof a);
}

int main(void)
{
    int arr[5] = { 0 }, other[5] = { 0 };
    show(arr);
    arr = other;
    return 0;
}
```

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -c arraybad.c
arraybad.c: In function ‘show’:
arraybad.c:5:39: warning: ‘sizeof’ on array function parameter ‘a’ will return size of ‘int *’ [-Wsizeof-array-argument]
    5 |     printf("sizeof a = %zu\n", sizeof a);
      |                                       ^
arraybad.c:3:22: note: declared here
    3 | static void show(int a[])
      |                  ~~~~^~~
arraybad.c: In function ‘main’:
arraybad.c:12:9: error: assignment to expression with array type
   12 |     arr = other;
      |         ^
```

`=` can't assign arrays. Copy elements in a loop or with `memcpy` ([Chapter 6](06-strings.md)). (Arrays *inside* a struct are copied with the struct: [Chapter 7](07-structs-unions-enums.md).)

## Pointer arithmetic

Adding `n` to a pointer moves it `n` *elements*; the compiler multiplies by the element size. Subtracting two pointers into one array gives the distance in elements as a `ptrdiff_t` (`<stddef.h>`, printed with `%td`).

`examples/ch05/arith.c`
```c
/* arith.c - pointer arithmetic counts elements, not bytes. */
#include <stddef.h>
#include <stdio.h>

int main(void)
{
    int nums[] = { 10, 20, 30, 40 };
    int *first = nums;
    int *end = nums + 4;                /* one past the last element: valid to
                                           compute and compare, not to read */

    printf("*(first + 2) = %d, first[2] = %d\n", *(first + 2), first[2]);

    for (int *p = first; p < end; p++)  /* walk with a pointer */
        printf("%d ", *p);
    printf("\n");

    ptrdiff_t count = end - first;      /* difference = number of elements */
    printf("end - first = %td elements\n", count);

    int *third = &nums[2];
    printf("third[-1] = %d\n", third[-1]);  /* fine: still inside nums */

    char text[] = "parley";
    char *c = text;
    while (*c != '\0')                  /* walk to the terminating '\0' */
        c++;
    printf("\"%s\" has %td characters\n", text, c - text);
    return 0;
}
```

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o arith arith.c
$ ./arith
*(first + 2) = 30, first[2] = 30
10 20 30 40 
end - first = 4 elements
third[-1] = 20
"parley" has 6 characters
```

The rules:

- A pointer may point at any element or **one past the last**. The one-past pointer may be compared and subtracted, never dereferenced; that makes the `p < end` loop legal.
- Moving a pointer anywhere else (before the start, two past the end) is undefined, even without reading through it.
- Subtracting or comparing (`<`) pointers into *different* arrays is undefined; `==` and `!=` are always fine.
- C doesn't check bounds. `nums[4]` or `nums[100]` compiles and reads whatever is there, or crashes. Chapter 6 shows what that does to strings.

The last loop is the classic string walk: step until the `'\0'` that ends every C string. The project's client does the same.

## `const` and pointers

A pointer declaration involves two objects, the pointer and its target, and `const` can protect either or both. Read **right to left**:

| declaration | read as | `*p = …` | `p = …` |
|---|---|---|---|
| `int *p` | p is a pointer to int | yes | yes |
| `const int *p` | p is a pointer to a const int | **no** | yes |
| `int *const p` | p is a const pointer to int | yes | **no** |
| `const int *const p` | p is a const pointer to a const int | **no** | **no** |

`examples/ch05/constptr.c`
```c
/* constptr.c - the four combinations of const and pointers. */
#include <stdio.h>

int main(void)
{
    int a = 1, b = 2;

    int *p1 = &a;                   /* change the pointer and the int */
    *p1 = 10;
    p1 = &b;

    const int *p2 = &a;             /* read-only int: *p2 = 3 is an error */
    p2 = &b;                        /* ...but p2 itself can move */

    int *const p3 = &a;             /* read-only pointer: p3 = &b is an error */
    *p3 = 20;                       /* ...but the int can change */

    const int *const p4 = &a;       /* neither can change */

    printf("a = %d, b = %d, *p1 = %d, *p2 = %d, *p3 = %d, *p4 = %d\n",
           a, b, *p1, *p2, *p3, *p4);
    return 0;
}
```

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o constptr constptr.c
$ ./constptr
a = 20, b = 2, *p1 = 2, *p2 = 2, *p3 = 20, *p4 = 20
```

Break the rules and the compiler refuses:

`constbad.c`
```c
int main(void)
{
    int a = 1, b = 2;
    const int *p2 = &a;
    int *const p3 = &a;
    *p2 = 3;
    p3 = &b;
    return 0;
}
```

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -c constbad.c
constbad.c: In function ‘main’:
constbad.c:6:9: error: assignment of read-only location ‘*p2’
    6 |     *p2 = 3;
      |         ^
constbad.c:7:8: error: assignment of read-only variable ‘p3’
    7 |     p3 = &b;
      |        ^
constbad.c:5:16: warning: variable ‘p3’ set but not used [-Wunused-but-set-variable]
    5 |     int *const p3 = &a;
      |                ^~
```

`const int *` is the common one: as a parameter it promises "this function only reads your data", like `sum(const int *a, size_t n)`. Passing a plain `int *` adds `const` automatically; removing it needs a cast, and writing to an object *defined* `const` is undefined. Put `const` on every pointer parameter not written through: it documents and catches mistakes.

## Pointers to pointers, and arrays of pointers

A pointer is a variable too, with its own address; a `char **` points at a `char *`. Two common uses:

- A function must change the caller's *pointer*, so it takes the pointer's address.
- Walking an array of pointers, such as `argv`.

`examples/ch05/ptrptr.c`
```c
/* ptrptr.c - pointers to pointers, and arrays of pointers. */
#include <stdio.h>

/* Skip spaces, then return the length of the word at *cursor and move
 * *cursor past it. The caller's pointer changes, so we get its address. */
static int next_word(const char **cursor)
{
    const char *p = *cursor;
    while (*p == ' ')
        p++;
    const char *start = p;
    while (*p != '\0' && *p != ' ')
        p++;
    *cursor = p;                        /* update the caller's pointer */
    return (int)(p - start);
}

int main(int argc, char **argv)
{
    const char *text = "  NICK   alice ";
    const char *cur = text;
    int len;
    while ((len = next_word(&cur)) > 0)
        printf("word: %.*s\n", len, cur - len);

    const char *names[] = { "alice", "bob", "carol" };  /* array of pointers */
    size_t n = sizeof names / sizeof names[0];
    for (size_t i = 0; i < n; i++)
        printf("names[%zu] = %s (first letter %c)\n", i, names[i], names[i][0]);

    /* argv is a char ** pointing at an array of pointers ending in NULL. */
    for (char **a = argv; *a != NULL; a++)
        printf("arg: %s\n", *a);
    (void)argc;
    return 0;
}
```

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o ptrptr ptrptr.c
$ ./ptrptr one two
word: NICK
word: alice
names[0] = alice (first letter a)
names[1] = bob (first letter b)
names[2] = carol (first letter c)
arg: ./ptrptr
arg: one
arg: two
```

- `%.*s` prints at most `len` characters, handy for printing part of a line without copying.
- `names` is an array of three pointers to string literals stored elsewhere, of any lengths. `names[i][0]` picks a pointer, then a character.
- `const char **cursor` is "pointer to pointer to const char": the characters are read-only, but `*cursor` (the caller's pointer) can change, exactly what `next_word` needs.

## Multidimensional arrays

`int grid[3][4]` is an array of 3 elements, each an array of 4 ints, stored row after row (**row-major**).

`examples/ch05/matrix.c`
```c
/* matrix.c - two-dimensional arrays are arrays of arrays. */
#include <stdio.h>

enum { ROWS = 3, COLS = 4 };

/* The column count must be known; the row count is passed. */
static void print_grid(int rows, int grid[][COLS])
{
    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < COLS; c++)
            printf("%3d", grid[r][c]);
        printf("\n");
    }
}

int main(void)
{
    int grid[ROWS][COLS];
    for (int r = 0; r < ROWS; r++)
        for (int c = 0; c < COLS; c++)
            grid[r][c] = r * 10 + c;

    print_grid(ROWS, grid);
    printf("sizeof grid = %zu, sizeof grid[0] = %zu\n", sizeof grid, sizeof grid[0]);

    /* Row-major: the rows are stored one after another. */
    const int *flat = &grid[0][0];
    printf("element 5 in memory order = %d (grid[1][1])\n", flat[5]);

    /* An array of pointers can have rows of different lengths. */
    const char *menu[] = { "nick", "me", "quit" };
    printf("menu[2][0] = %c\n", menu[2][0]);
    return 0;
}
```

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o matrix matrix.c
$ ./matrix
  0  1  2  3
 10 11 12 13
 20 21 22 23
sizeof grid = 48, sizeof grid[0] = 16
element 5 in memory order = 11 (grid[1][1])
menu[2][0] = q
```

- `grid` decays to a pointer to its first *row*, type `int (*)[4]`, so the parameter must give the column count: `int grid[][COLS]`. `grid[r][c]` is at `r * COLS + c`.
- `menu`, by contrast, is an array of pointers, each "row" a separate string of any length. It *looks* 2-D (`menu[2][0]`) but is laid out quite differently.
- Gotcha: a read-only `const int grid[][COLS]` parameter seems natural, but in C99 passing a plain `int[3][4]` gives `warning: pointers to arrays with different qualifiers are incompatible in ISO C [-Wpedantic]`. C23 fixed this, so `print_grid` omits `const`.

## Variable-length arrays (C99), and why to avoid them

C99 lets a local array's size be a run-time value, a **variable-length array** (VLA):

`examples/ch05/vla.c`
```c
/* vla.c - variable-length arrays (C99): the size is known only at run time. */
#include <stdio.h>
#include <stdlib.h>

/* A VLA parameter: the column count comes from an earlier parameter. */
static void fill(int rows, int cols, int t[rows][cols])
{
    for (int r = 0; r < rows; r++)
        for (int c = 0; c < cols; c++)
            t[r][c] = (r + 1) * (c + 1);
}

int main(int argc, char **argv)
{
    long n = argc > 1 ? strtol(argv[1], NULL, 10) : 4;
    if (n < 1)
        return 1;

    double samples[n];                  /* n * 8 bytes on the stack */
    for (long i = 0; i < n; i++)
        samples[i] = i * 0.5;
    printf("n = %ld, sizeof samples = %zu, last = %.1f\n",
           n, sizeof samples, samples[n - 1]);

    int table[3][4];
    fill(3, 4, table);
    printf("table[2][3] = %d\n", table[2][3]);
    return 0;
}
```

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o vla vla.c
$ ./vla
n = 4, sizeof samples = 32, last = 1.5
table[2][3] = 12
$ ./vla 1000000
n = 1000000, sizeof samples = 8000000, last = 499999.5
table[2][3] = 12
$ ./vla 1100000
Segmentation fault (core dumped)
```

`sizeof samples` is computed at run time. 8 MB of samples fit the 8 MiB stack; 8.8 MB don't, and there's no error to check: the program dies, on both libcs. If `n` comes from input, a user can crash you on purpose.

This course's rules:

- **Don't use VLAs for storage.** Use a fixed-size array with a checked maximum, or `malloc` ([Chapter 8](08-memory.md)), which reports failure. C11 made VLAs optional, and the Linux kernel removed them all. `-Wvla` flags them:

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -Wvla -c vla.c
vla.c:6:1: warning: ISO C90 forbids variable length array ‘t’ [-Wvla]
    6 | static void fill(int rows, int cols, int t[rows][cols])
      | ^~~~~~
vla.c:6:1: warning: ISO C90 forbids variable length array ‘t’ [-Wvla]
vla.c: In function ‘main’:
vla.c:19:5: warning: ISO C90 forbids variable length array ‘samples’ [-Wvla]
   19 |     double samples[n];                  /* n * 8 bytes on the stack */
      |     ^~~~~~
```

- A VLA *parameter* like `int t[rows][cols]` allocates nothing; it describes the shape of the caller's memory, useful for 2-D data. The project doesn't need it.

## `restrict` (C99)

`restrict` on a pointer parameter promises: "while this function runs, my target is reached only through me". So two `restrict` pointers never share an object, and the compiler optimises on that.

`examples/ch05/restrict.c`
```c
/* restrict.c - a promise to the compiler: "these pointers don't overlap". */
#include <stdio.h>

/* With restrict, the compiler may assume that storing to *b cannot change
 * *a, so it can return 1 without reading *a again. */
static int set_both(int *restrict a, int *restrict b)
{
    *a = 1;
    *b = 2;
    return *a;
}

int main(void)
{
    int x = 0, y = 0;
    int r = set_both(&x, &y);           /* two different objects: OK */
    printf("set_both(&x, &y) = %d, x = %d, y = %d\n", r, x, y);
    return 0;
}
```

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o restrict restrict.c
$ ./restrict
set_both(&x, &y) = 1, x = 1, y = 2
```

Break the promise by adding a call `set_both(&x, &x)` before `return 0;`:

```c
    printf("set_both(&x, &x) = %d\n", set_both(&x, &x));   /* broken promise */
```

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o restrict restrict.c
restrict.c: In function ‘main’:
restrict.c:18:48: warning: passing argument 1 to ‘restrict’-qualified parameter aliases with argument 2 [-Wrestrict]
   18 |     printf("set_both(&x, &x) = %d\n", set_both(&x, &x));   /* broken promise */
      |                                                ^~  ~~
$ ./restrict
set_both(&x, &y) = 1, x = 1, y = 2
set_both(&x, &x) = 2
$ gcc -std=c99 -Wall -Wextra -pedantic -O2 -o restrict restrict.c 2>/dev/null
$ ./restrict
set_both(&x, &y) = 1, x = 1, y = 2
set_both(&x, &x) = 1
```

The result depends on optimisation: at `-O2` the compiler trusted the promise and returned 1 without rereading `x`. Undefined behaviour in action.

You'll mostly *read* `restrict`, not write it. The C library uses it to mark arguments that must not overlap:

- `void *memcpy(void *restrict dst, const void *restrict src, size_t n);` — must not overlap.
- `void *memmove(void *dst, const void *src, size_t n);` — may overlap.

Chapter 6 uses both.

## Function pointers

A function has an address too, which you can store and call later. The syntax is awkward: `int (*op)(int, int)` declares `op` as a pointer to a function taking two ints and returning an int. The parentheses matter: `int *op(int, int)` declares a function returning `int *`.

`examples/ch05/funcptr.c`
```c
/* funcptr.c - pointers to functions, callbacks and a dispatch table. */
#include <stdio.h>
#include <string.h>

static int add(int a, int b) { return a + b; }
static int sub(int a, int b) { return a - b; }
static int mul(int a, int b) { return a * b; }

/* A typedef makes the type readable: 'binop' is "function (int, int)
 * returning int", and 'binop *' is a pointer to one. */
typedef int binop(int a, int b);

/* A callback: apply f to every element. */
static void apply(int *v, size_t n, int (*f)(int, int), int arg)
{
    for (size_t i = 0; i < n; i++)
        v[i] = f(v[i], arg);
}

/* A dispatch table: names and functions in parallel arrays. */
static const char *const op_names[] = { "add", "sub", "mul" };
static binop *const op_funcs[] = { add, sub, mul };

static binop *find_op(const char *name)
{
    for (size_t i = 0; i < sizeof op_names / sizeof op_names[0]; i++)
        if (strcmp(name, op_names[i]) == 0)
            return op_funcs[i];
    return NULL;                        /* no such operation */
}

int main(void)
{
    int (*op)(int, int) = add;          /* same as = &add */
    printf("op(2, 3) = %d\n", op(2, 3));    /* same as (*op)(2, 3) */
    op = mul;
    printf("op(2, 3) = %d\n", op(2, 3));

    int v[] = { 1, 2, 3 };
    apply(v, 3, mul, 10);
    printf("after apply: %d %d %d\n", v[0], v[1], v[2]);

    const char *wanted[] = { "sub", "mul", "div" };
    for (int i = 0; i < 3; i++) {
        binop *f = find_op(wanted[i]);
        if (f != NULL)
            printf("%s(12, 4) = %d\n", wanted[i], f(12, 4));
        else
            printf("%s: unknown operation\n", wanted[i]);
    }
    return 0;
}
```

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o funcptr funcptr.c
$ ./funcptr
op(2, 3) = 5
op(2, 3) = 6
after apply: 10 20 30
sub(12, 4) = 8
mul(12, 4) = 48
div: unknown operation
```

- A function's name decays to a pointer, like an array's. So `= add` equals `= &add`, and `op(2, 3)` equals `(*op)(2, 3)`.
- `typedef` ([Chapter 7](07-structs-unions-enums.md)) names a type; here it turns `int (*const op_funcs[])(int, int)` into the clearer `binop *const op_funcs[]`.
- A **callback** is a function passed to another function, which calls it back (`apply`).
- A **dispatch table** maps names to functions: a new operation is one table entry, not another `if`. The client's commands use this.
- Every function in a table must have *exactly* the same type; calling through a wrongly typed pointer is undefined behaviour.

### `qsort`: a callback from the C library

`qsort` (`<stdlib.h>`) sorts an array of any type. It knows nothing about your elements, so you give it a comparison function, called with pointers to two elements:

`examples/ch05/sort.c`
```c
/* sort.c - qsort calls our comparison function through a pointer. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* qsort passes pointers to two elements, as const void *. Return <0, 0
 * or >0. (x > y) - (x < y) never overflows, unlike x - y. */
static int cmp_int(const void *a, const void *b)
{
    int x = *(const int *)a;
    int y = *(const int *)b;
    return (x > y) - (x < y);
}

/* The elements are 'const char *', so qsort hands us 'const char **'. */
static int cmp_str(const void *a, const void *b)
{
    const char *s = *(const char *const *)a;
    const char *t = *(const char *const *)b;
    return strcmp(s, t);
}

int main(void)
{
    int nums[] = { 42, -7, 1000, 0, 3 };
    size_t n = sizeof nums / sizeof nums[0];
    qsort(nums, n, sizeof nums[0], cmp_int);
    for (size_t i = 0; i < n; i++)
        printf("%d ", nums[i]);
    printf("\n");

    const char *nicks[] = { "carol", "alice", "dave", "bob" };
    size_t m = sizeof nicks / sizeof nicks[0];
    qsort(nicks, m, sizeof nicks[0], cmp_str);
    for (size_t i = 0; i < m; i++)
        printf("%s ", nicks[i]);
    printf("\n");
    return 0;
}
```

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o sort sort.c
$ ./sort
-7 0 3 42 1000 
alice bob carol dave 
```

Gotcha: the common shortcut `x - y` overflows for large values of opposite sign. Overflow is undefined; in practice the sign flips and the sort silently goes wrong:

`badcmp.c`
```c
#include <stdio.h>
#include <stdlib.h>

static int cmp_int(const void *a, const void *b)
{
    return *(const int *)a - *(const int *)b;   /* can overflow */
}

int main(void)
{
    int nums[] = { 2000000000, -2000000000, 0, 7 };
    qsort(nums, 4, sizeof nums[0], cmp_int);
    for (int i = 0; i < 4; i++)
        printf("%d ", nums[i]);
    printf("\n");
    return 0;
}
```

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o badcmp badcmp.c
$ ./badcmp
0 7 2000000000 -2000000000 
$ musl-gcc -std=c99 -Wall -Wextra -pedantic -static -o badcmp-musl badcmp.c
$ ./badcmp-musl
0 2000000000 -2000000000 7 
```

No warning, and both outputs are wrong, differently: glibc 2.35 uses a merge sort, musl a smoothsort, and a broken comparison exposes that. With a correct comparison, both agree.

## `void *`: a pointer to anything

`void *` is a pointer with no element type. Any object pointer converts to and from it without a cast, which is how `qsort`, `memcpy` and `malloc` handle every type. With no element size, it can't be dereferenced or used in arithmetic; convert it to a real pointer type first.

`examples/ch05/voidptr.c`
```c
/* voidptr.c - void * points to "some object of unknown type". */
#include <stdio.h>

/* Swap any two objects of the same size, one byte at a time. */
static void swap_any(void *a, void *b, size_t size)
{
    unsigned char *p = a;               /* void * converts without a cast */
    unsigned char *q = b;
    for (size_t i = 0; i < size; i++) {
        unsigned char tmp = p[i];
        p[i] = q[i];
        q[i] = tmp;
    }
}

int main(void)
{
    int i1 = 1, i2 = 2;
    swap_any(&i1, &i2, sizeof i1);
    printf("ints: %d %d\n", i1, i2);

    double d1 = 1.5, d2 = -0.25;
    swap_any(&d1, &d2, sizeof d1);
    printf("doubles: %.2f %.2f\n", d1, d2);

    void *v = &i1;
    int *ip = v;                        /* back to the real type */
    printf("through void *: %d\n", *ip);
    return 0;
}
```

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o voidptr voidptr.c
$ ./voidptr
ints: 2 1
doubles: -0.25 1.50
through void *: 2
```

The price: the compiler can't check types, so nothing stops a `double *` going where an `int *` is expected. Dereferencing a `void *` directly is an error:

`voidderef.c`
```c
int main(void)
{
    int n = 1;
    void *v = &n;
    return *v;
}
```

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -c voidderef.c
voidderef.c: In function ‘main’:
voidderef.c:5:12: warning: dereferencing ‘void *’ pointer
    5 |     return *v;
      |            ^~
voidderef.c:5:12: error: void value not ignored as it ought to be
```

(Function pointers don't convert to `void *` in standard C; POSIX allows it for `dlsym`, unused in this course.)

## Strict aliasing, in brief

A pointer cast can reinterpret memory: `*(unsigned int *)&f` reads a `float`'s bytes as an `unsigned int`. C99 forbids most such casts. The **strict aliasing rule** says an object may be accessed only through its own type (or its signed/unsigned or `const` variant) or a character type. So the compiler assumes an `int *` and a `float *` never share memory, and optimises on that.

gcc warns at `-O2`, which turns on those optimisations:

`pun.c`
```c
#include <stdio.h>

int main(void)
{
    float f = 1.0f;
    unsigned int bits = *(unsigned int *)&f;
    printf("%08x\n", bits);
    return 0;
}
```

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -O2 -c pun.c
pun.c: In function ‘main’:
pun.c:6:26: warning: dereferencing type-punned pointer will break strict-aliasing rules [-Wstrict-aliasing]
    6 |     unsigned int bits = *(unsigned int *)&f;
      |                          ^~~~~~~~~~~~~~~~~~
pun.c:6:18: warning: ‘f’ is used uninitialized [-Wuninitialized]
    6 |     unsigned int bits = *(unsigned int *)&f;
      |                  ^~~~
pun.c:5:11: note: ‘f’ declared here
    5 |     float f = 1.0f;
      |           ^
```

The second warning shows the danger: under the rule, an `unsigned int *` read can't see the `float`, so gcc treats `f` as unused. The allowed ways:

- read the bytes through `unsigned char *` (always allowed);
- `memcpy(&bits, &f, sizeof bits)` (Chapter 6), which compilers turn into a single move.

`examples/ch05/aliasing.c`
```c
/* aliasing.c - look at an object's bytes the allowed way. */
#include <stdio.h>

int main(void)
{
    float f = 1.0f;
    unsigned int n = 0x11223344;

    /* Any object may be read through an unsigned char pointer. */
    const unsigned char *b = (const unsigned char *)&f;
    printf("bytes of 1.0f:      ");
    for (size_t i = 0; i < sizeof f; i++)
        printf("%02x ", b[i]);
    printf("\n");

    b = (const unsigned char *)&n;
    printf("bytes of 0x11223344: ");
    for (size_t i = 0; i < sizeof n; i++)
        printf("%02x ", b[i]);
    printf("(little-endian: lowest byte first)\n");
    return 0;
}
```

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o aliasing aliasing.c
$ ./aliasing
bytes of 1.0f:      00 00 80 3f 
bytes of 0x11223344: 44 33 22 11 (little-endian: lowest byte first)
```

x86-64 is **little-endian**: a number's lowest byte comes first. Network protocols use the opposite order, which matters in [Chapter 7](07-structs-unions-enums.md) and [Chapter 14](14-networking-basics.md).

## Building parley: slash commands

The client now reads your typed lines and prints the protocol line it *would* send. There's no network yet ([Chapter 16](16-a-first-client.md)), so lines go to stdout:

| you type | the client prints |
|---|---|
| (at start-up) | `NICK <nick>` |
| `/nick bob` | `NICK bob` |
| `/me waves` | `ME :waves` |
| `/who` | `NAMES` |
| `/quit [reason]` | `QUIT` or `QUIT :reason`, then stops reading |
| `/help` | a command list, on stderr |
| any other line | `MSG :<line>` |
| (end of input) | `QUIT` |

Every protocol line ends with CR LF (`"\r\n"`), as in IRC. `:` starts free text that may contain spaces.

The design:

- **One handler per command**, all of one type: `void cmd_x(const char *arg)`, where `arg` is the text after the command name, or `""`.
- **A dispatch table** of two parallel arrays, names and function pointers. `run_command` looks up the name and calls through the pointer. [Chapter 7](07-structs-unions-enums.md) merges them into one array of structs.
- **The line is split in place**, without copies: `run_command` walks to the first space, writes `'\0'` there to end the name, and points `arg` just after it.
- **`fgets(line, sizeof line, stdin)`** reads one line, including the `'\n'`, into the array, and returns `NULL` at end of input. ([Chapter 6](06-strings.md) covers it, including lines that don't fit.)
- **Errors and help go to stderr**, so stdout carries only protocol lines.

Only `parley.c` changes. At the top, update the comment, include `<stdbool.h>`, and add `INPUT_MAX` to the enum:

`project/ch05/parley.c` (top)
```c
/* parley.c - the parley chat client.
 * Chapter 5: no network yet. The client reads what you type on stdin and
 * prints the protocol lines it would send to the server on stdout. */
#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { DEFAULT_PORT = 6667, PORT_MAX = 65535, INPUT_MAX = 512 };
```

`usage_error`, `print_help`, `parse_number` and `parse_args` stay as they were. After `parse_args`, add the handlers, the table and the helpers, and replace `main`:

`project/ch05/parley.c` (from after `parse_args` to the end)
```c
/* Set by /quit: stop reading input. */
static bool quitting = false;

/* The protocol ends every line with CR LF ("\r\n"). */
static void cmd_nick(const char *arg)
{
    if (*arg == '\0') {
        fprintf(stderr, "parley: usage: /nick NEWNICK\n");
        return;
    }
    printf("NICK %s\r\n", arg);
}

static void cmd_me(const char *arg)
{
    if (*arg == '\0') {
        fprintf(stderr, "parley: usage: /me ACTION\n");
        return;
    }
    printf("ME :%s\r\n", arg);
}

static void cmd_who(const char *arg)
{
    (void)arg;   /* unused: every handler must have the same type */
    printf("NAMES\r\n");
}

static void cmd_quit(const char *arg)
{
    if (*arg == '\0')
        printf("QUIT\r\n");
    else
        printf("QUIT :%s\r\n", arg);
    quitting = true;
}

static void cmd_help(const char *arg)
{
    (void)arg;
    /* stdout carries protocol lines, so help goes to stderr. */
    fprintf(stderr, "commands:\n"
                    "  /nick NEWNICK   change your nickname\n"
                    "  /me ACTION      describe an action\n"
                    "  /who            list who is in the room\n"
                    "  /quit [REASON]  leave\n"
                    "  /help           show this help\n"
                    "anything else is sent as a message\n");
}

/* The dispatch table: two parallel arrays. command_names[i] is handled by
 * command_funcs[i]. command_funcs is "an array of const pointers to
 * functions taking (const char *) and returning void". Chapter 7 merges
 * the two arrays into one array of structs. */
static const char *const command_names[] = { "nick", "me", "who", "quit", "help" };
static void (*const command_funcs[])(const char *arg) = {
    cmd_nick, cmd_me, cmd_who, cmd_quit, cmd_help,
};
enum { NCOMMANDS = sizeof command_names / sizeof command_names[0] };

/* Cut the string at the first '\r' or '\n'. */
static void strip_newline(char *s)
{
    for (char *p = s; *p != '\0'; p++) {
        if (*p == '\r' || *p == '\n') {
            *p = '\0';
            return;
        }
    }
}

/* "/nick bob" -> run cmd_nick("bob"). Modifies line in place. */
static void run_command(char *line)
{
    char *name = line + 1;   /* skip the '/' */
    char *arg = name;
    while (*arg != '\0' && *arg != ' ')
        arg++;
    if (*arg == ' ')
        *arg++ = '\0';      /* end the name; arg now points after the space */

    for (int i = 0; i < NCOMMANDS; i++) {
        if (strcmp(name, command_names[i]) == 0) {
            command_funcs[i](arg);    /* call through the function pointer */
            return;
        }
    }
    fprintf(stderr, "parley: unknown command /%s (try /help)\n", name);
}

int main(int argc, char *argv[])
{
    parse_args(argc, argv);
    printf("NICK %s\r\n", opt_nick);

    char line[INPUT_MAX];
    while (!quitting && fgets(line, sizeof line, stdin) != NULL) {
        strip_newline(line);
        if (line[0] == '\0')
            continue;              /* ignore empty lines */
        if (line[0] == '/')
            run_command(line);
        else
            printf("MSG :%s\r\n", line);
    }
    if (!quitting)
        printf("QUIT\r\n");      /* end of input means goodbye */
    return 0;
}
```

Points to notice:

- **`const` in both places.** `command_names` is `const char *const []`: neither strings nor entries can change. `command_funcs` is an array of `const` function pointers. Fully `const` tables can live in read-only memory, so a stray write can't swap a handler.
- **`NCOMMANDS`** is computed from the table with `sizeof`, so it can't drift from the names. An `enum` constant works because the value is a compile-time integer.
- **`*arg++ = '\0'`** stores `'\0'` at `arg`, then moves `arg` on one byte: a common idiom worth reading slowly once.
- **`(void)arg;`** tells the compiler (and `-Wextra`'s `-Wunused-parameter`) the unused parameter is deliberate; every handler must share one type.
- **`strcmp(a, b) == 0`** means "equal strings". Chapter 6 explains it, and why `name == command_names[i]` would compare addresses.
- **`strip_newline`** walks the line with a pointer. Chapter 6 replaces it with a reusable `str_chomp`.

### Checkpoint

The server is unchanged. Build both programs, then feed the client typed lines with `printf`:

```console
$ cd project/ch05
$ gcc -std=c99 -Wall -Wextra -pedantic -o parleyd parleyd.c
$ gcc -std=c99 -Wall -Wextra -pedantic -o parley parley.c
$ musl-gcc -std=c99 -Wall -Wextra -pedantic -static -o parley-musl parley.c
$ printf 'hello everyone\n/me waves\n/who\n/nick bob\n/quit see you\nnever sent\n' | ./parley -n alice
NICK alice
MSG :hello everyone
ME :waves
NAMES
NICK bob
QUIT :see you
```

The line after `/quit` was never read. Now mistakes, an empty line, and end of input instead of `/quit`:

```console
$ printf '/dance\n/nick\n/help\n\nbye\n' | ./parley -n alice
NICK alice
parley: unknown command /dance (try /help)
parley: usage: /nick NEWNICK
commands:
  /nick NEWNICK   change your nickname
  /me ACTION      describe an action
  /who            list who is in the room
  /quit [REASON]  leave
  /help           show this help
anything else is sent as a message
MSG :bye
QUIT
```

That is the order on a terminal. Redirect stdout to a file or pipe and glibc prints the stderr lines *before* `NICK alice`, because stdout then waits in a buffer until the program ends; musl still prints `NICK alice` first. [Chapter 6](06-strings.md) explains the difference.

`od -c` shows every byte, including the CR LF endings (`\r \n`):

```console
$ printf 'hi\n' | ./parley -n alice | od -c
0000000   N   I   C   K       a   l   i   c   e  \r  \n   M   S   G    
0000020   :   h   i  \r  \n   Q   U   I   T  \r  \n
0000033
$ printf 'hello everyone\n/me waves\n/who\n/nick bob\n/quit see you\n' | ./parley-musl -n alice
NICK alice
MSG :hello everyone
ME :waves
NAMES
NICK bob
QUIT :see you
```

The musl build matches; everything here is plain C99.

The complete project at this point is in `project/ch05/`.

## Summary

- Memory is numbered bytes. Globals and `static`s live for the whole run, literals are read-only, locals live on the stack until their block ends.
- `&x` takes an address; `*p` reaches the object; pointers are typed; `NULL` means "no object". `%p` needs a `(void *)` cast; glibc prints `NULL` as `(nil)`, musl as `0`.
- Pointer parameters let a function change the caller's objects and return several results.
- Arrays decay to a pointer to their first element, except with `sizeof` and `&`. Array parameters are pointers, so pass the length. `a[i]` means `*(a + i)`.
- Pointer arithmetic counts elements. You may point one past the end but not read there. Bounds aren't checked.
- Read `const` right to left. Use `const T *` for pointer parameters you only read through.
- `T **` lets a function change the caller's pointer. `argv` is a `NULL`-terminated array of pointers.
- 2-D arrays are arrays of rows; parameters must give the column count.
- Avoid VLAs: a big size crashes without warning. Use fixed maximums or `malloc`.
- `restrict` promises no overlap; breaking it gives different results at different `-O` levels.
- Function pointers enable callbacks (`qsort`) and dispatch tables. Comparisons must not use `x - y`.
- `void *` holds any object pointer but can't be dereferenced. Inspect raw bytes via `unsigned char *` or `memcpy`, never casts to other types (strict aliasing).

## Exercises

1. **Reverse and find.** Write `void reverse(int *a, size_t n)` that reverses an array in place with two pointers walking toward each other. Then write `const int *find_max(const int *a, size_t n)`, returning a pointer to the largest element, or `NULL` when `n` is 0. Print the maximum's index using pointer subtraction.

<details><summary>Solution</summary>

```c
#include <stdio.h>

static void reverse(int *a, size_t n)
{
    if (n == 0)
        return;
    int *lo = a, *hi = a + n - 1;
    while (lo < hi) {
        int tmp = *lo;
        *lo++ = *hi;
        *hi-- = tmp;
    }
}

/* Pointer to the largest element, or NULL if n == 0. */
static const int *find_max(const int *a, size_t n)
{
    const int *best = NULL;
    for (const int *p = a; p < a + n; p++)
        if (best == NULL || *p > *best)
            best = p;
    return best;
}

int main(void)
{
    int v[] = { 1, 2, 3, 4, 5 };
    reverse(v, 5);
    for (int i = 0; i < 5; i++)
        printf("%d ", v[i]);
    printf("\n");

    const int *m = find_max(v, 5);
    printf("max %d at index %td\n", *m, m - v);
    printf("empty: %s\n", find_max(v, 0) == NULL ? "NULL" : "?");
    return 0;
}
```

```console
$ ./reverse
5 4 3 2 1 
max 5 at index 0
empty: NULL
```

The `n == 0` check in `reverse` matters: `a + n - 1` would point *before* the array, undefined even unread. Returning a pointer lets `find_max` say "none" with `NULL` and lets the caller change the element found.

</details>

2. **Sort by length.** Sort `{ "carol", "bob", "alice", "eve", "dave", "al" }` with `qsort`, shortest first, ties alphabetical. (`strlen` from `<string.h>` gives the length.)

<details><summary>Solution</summary>

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Shorter nicks first; equal lengths in alphabetical order. */
static int cmp_len(const void *a, const void *b)
{
    const char *s = *(const char *const *)a;
    const char *t = *(const char *const *)b;
    size_t ls = strlen(s), lt = strlen(t);
    if (ls != lt)
        return ls < lt ? -1 : 1;
    return strcmp(s, t);            /* tie-breaker: a full, stable order */
}

int main(void)
{
    const char *nicks[] = { "carol", "bob", "alice", "eve", "dave", "al" };
    size_t n = sizeof nicks / sizeof nicks[0];
    qsort(nicks, n, sizeof nicks[0], cmp_len);
    for (size_t i = 0; i < n; i++)
        printf("%s ", nicks[i]);
    printf("\n");
    return 0;
}
```

```console
$ ./bylen
al bob eve dave alice carol 
```

`qsort` isn't *stable*: equal elements may come out in any order, and the libcs' algorithms differ. The tie-breaker fully defines the order, so both print the same line. `size_t` is unsigned, so `ls - lt` would be wrong too; compare instead.

</details>

3. **A tiny regex matcher.** Write `int match(const char *re, const char *text)` that supports:
   - `c`, a literal character;
   - `.`, any one character;
   - `^`, the start of the text;
   - `$`, the end of the text;
   - `*`, zero or more of the previous character.

   It returns 1 if `re` matches anywhere in `text`. Use only pointers and recursion, no copying. (A classic design from Kernighan and Pike's *The Practice of Programming*.)

<details><summary>Solution</summary>

```c
/* match.c - a tiny regular-expression matcher: c . * ^ $ */
#include <stdio.h>

static int match_here(const char *re, const char *text);

/* Does re match c* followed by the rest? Try 0, 1, 2... copies of c. */
static int match_star(int c, const char *re, const char *text)
{
    do {
        if (match_here(re, text))
            return 1;
    } while (*text != '\0' && (*text++ == c || c == '.'));
    return 0;
}

/* Does re match at the start of text? */
static int match_here(const char *re, const char *text)
{
    if (re[0] == '\0')
        return 1;
    if (re[1] == '*')
        return match_star(re[0], re + 2, text);
    if (re[0] == '$' && re[1] == '\0')
        return *text == '\0';
    if (*text != '\0' && (re[0] == '.' || re[0] == *text))
        return match_here(re + 1, text + 1);
    return 0;
}

/* Does re match anywhere in text? */
static int match(const char *re, const char *text)
{
    if (re[0] == '^')
        return match_here(re + 1, text);
    do {
        if (match_here(re, text))
            return 1;
    } while (*text++ != '\0');
    return 0;
}

int main(void)
{
    const char *tests[][2] = {
        { "ali", "hello alice" }, { "^ali", "hello alice" }, { "ice$", "alice" },
        { "a.*e", "alice" }, { "^b.b$", "bob" }, { "x*y", "y" }, { "^$", "" },
        { "a.c", "abd" },
    };
    for (size_t i = 0; i < sizeof tests / sizeof tests[0]; i++)
        printf("%-6s %-13s %s\n", tests[i][0], tests[i][1],
               match(tests[i][0], tests[i][1]) ? "match" : "no");
    return 0;
}
```

```console
$ ./match
ali    hello alice   match
^ali   hello alice   no
ice$   alice         match
a.*e   alice         match
^b.b$  bob           match
x*y    y             match
^$                   match
a.c    abd           no
```

- `match_here` needs a prototype at the top because `match_star` and `match_here` call each other (*mutual recursion*).
- `tests` is a 2-D array of pointers: 8 rows of 2 `const char *`.
- Each step only moves pointers forward; nothing is copied.

</details>

4. **`const` quiz.** For each declaration, say whether `*p = 0;` and `p = NULL;` compile. Then check with the compiler.
   - `char *const p = buf;`
   - `const char *p = buf;`
   - `char const *p = buf;`
   - `const char *const p = buf;`

<details><summary>Solution</summary>

| declaration | `*p = 0;` | `p = NULL;` |
|---|---|---|
| `char *const p` | yes | no: "assignment of read-only variable ‘p’" |
| `const char *p` | no: "assignment of read-only location ‘*p’" | yes |
| `char const *p` | no (the same type as `const char *`) | yes |
| `const char *const p` | no: "assignment of read-only location ‘*(const char *)p’" | no: "assignment of read-only variable ‘p’" |

`const` applies to what's on its *left*, or, when first, to the type on its right. So `char const *` and `const char *` are the same type, and right-to-left still reads "p is a pointer to a const char".

</details>

Sources for this chapter: [REFERENCES.md](REFERENCES.md#chapter-5--pointers-and-arrays)
