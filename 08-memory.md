# Chapter 8 — Memory

Every C object has a **lifetime**, and for heap memory you manage it yourself. Get it wrong and you get leaks, crashes or silent corruption, often much later and somewhere else. This chapter covers where objects live, `malloc` and friends, the classic heap bugs (and what glibc and musl do about them), and three designs that keep memory manageable: the growable buffer, error paths tested with fault injection, and the arena. In the project the server stops reading one fixed-size line at a time. It collects bytes in a growable `strbuf` and cuts out complete lines, as a network server must, because a socket hands you bytes in arbitrary pieces. It builds on the structs of [Chapter 7](07-structs-unions-enums.md); [Chapter 9](09-preprocessor-and-building.md) splits the program into modules.

## Object lifetimes: static, automatic, allocated

C has three **storage durations**:

| Duration | How you get one | Lives | Starts as |
|---|---|---|---|
| **static** | file-scope variables, `static` locals, string literals | the whole run | zero (or your initializer) |
| **automatic** | ordinary local variables and parameters | until its block ends | garbage, unless initialized |
| **allocated** | `malloc`, `calloc`, `realloc` | until you `free` it | garbage (`calloc`: zero) |

`examples/ch08/lifetimes.c`
```c
/* lifetimes.c - the three storage durations: static, automatic, allocated. */
#include <stdio.h>
#include <stdlib.h>

int counter_global = 0;              /* static storage: lives for the whole run */

static int next_id(void)
{
    static int id = 100;             /* static too: keeps its value between calls */
    return id++;
}

static int *make_number(int v)
{
    int *p = malloc(sizeof *p);      /* allocated: lives until free() */
    if (p != NULL)
        *p = v;
    return p;                        /* fine: the heap object outlives this call */
}

int main(void)
{
    int local = 7;                   /* automatic: lives until main returns */
    const char *literal = "hello";   /* string literals are static and read-only */
    int *heap = make_number(42);
    if (heap == NULL)
        return 1;

    int a = next_id();
    int b = next_id();
    printf("ids: %d %d\n", a, b);
    printf("*heap = %d\n", *heap);

    /* Where things live. Addresses change from run to run (see below). */
    printf("static  %p  (literal)\n", (const void *)literal);
    printf("static  %p  (counter_global)\n", (void *)&counter_global);
    printf("heap    %p  (*heap)\n", (void *)heap);
    printf("stack   %p  (local)\n", (void *)&local);

    free(heap);                      /* heap is now a dangling pointer */
    heap = NULL;                     /* so make it obviously invalid */
    return 0;
}
```

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o lifetimes lifetimes.c
$ ./lifetimes
ids: 100 101
*heap = 42
static  0x6272b28c2004  (literal)
static  0x6272b28c4018  (counter_global)
heap    0x6272ccfd72a0  (*heap)
stack   0x7ffff629209c  (local)
$ musl-gcc -std=c99 -Wall -Wextra -pedantic -static -o lifetimes-musl lifetimes.c
$ ./lifetimes-musl
ids: 100 101
*heap = 42
static  0x407000  (literal)
static  0x409144  (counter_global)
heap    0x7bfbbc035040  (*heap)
stack   0x7ffeb690336c  (local)
```

⚠ Your addresses will differ on every run: Linux places memory at random (*address space layout randomization*, ASLR) to make attacks harder. The pattern is what matters:

- **Static** objects sit next to the program's code, at fixed offsets from each other.
- The **stack** is at the top of the address space (`0x7ff...`). Each call pushes a *frame* for its automatic objects; returning pops it.
- The **heap** is where `malloc` gets memory from the kernel. glibc's heap starts just past the program; musl's `malloc` got this block from a separate `mmap` region.
- The glibc build is position-independent, loaded at a random base. The static musl build loads at the fixed address `0x400000` (DYN vs EXEC, as in [Chapter 7](07-structs-unions-enums.md)'s ELF exercise).

**Stack vs heap.** Automatic objects are free to create and vanish by themselves, but cannot outlive their function, and the stack is small (usually 8 MiB on Linux; see `ulimit -s`). Use the heap when the size is known only at run time, when the object must outlive its creator, or when it is big.

### Gotcha: returning a pointer to a local

`buf` dies when `greeting` returns, so the pointer dangles:

```c
static char *greeting(const char *name)
{
    char buf[32];
    snprintf(buf, sizeof buf, "hello, %s", name);
    return buf;
}

int main(void)
{
    printf("%s\n", greeting("alice"));
    return 0;
}
```

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o dangle dangle.c
dangle.c: In function ‘greeting’:
dangle.c:7:12: warning: function returns address of local variable [-Wreturn-local-addr]
    7 |     return buf;
      |            ^~~
$ ./dangle
Segmentation fault (core dumped)
```

gcc knows the pointer is useless, so it returns `NULL` instead, and `printf` crashes on it (the musl build too). Three fixes:

- The caller passes in a buffer (`void greeting(char *out, size_t size, ...)`): the most common C style.
- Return heap memory that the caller must `free`.
- Use a `static` buffer: simple, but the next call overwrites it, and it is not thread-safe.

## malloc, calloc, realloc and free

All four are in `<stdlib.h>`:

| Call | Does | On failure |
|---|---|---|
| `malloc(n)` | `n` bytes, contents undefined | `NULL` |
| `calloc(count, size)` | `count * size` bytes, all zero; checks the multiplication for overflow | `NULL` |
| `realloc(p, n)` | resizes the block to `n` bytes; may move it (copying the contents) | `NULL`, **and `p` is still valid** |
| `free(p)` | gives the block back; `free(NULL)` does nothing | — |

`examples/ch08/alloc.c`
```c
/* alloc.c - malloc, calloc, realloc and free, with the checks they need. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* malloc(n * size) without the overflow: returns NULL if n * size does
 * not fit in a size_t, instead of silently allocating too little. */
static void *alloc_array(size_t n, size_t size)
{
    if (size != 0 && n > SIZE_MAX / size)
        return NULL;
    return malloc(n * size);
}

int main(void)
{
    /* malloc: uninitialized memory. sizeof *nums follows nums's type. */
    size_t n = 5;
    int *nums = malloc(n * sizeof *nums);
    if (nums == NULL) {
        perror("malloc");
        return 1;
    }
    for (size_t i = 0; i < n; i++)
        nums[i] = (int)(i * i);

    /* realloc: grow the block. It may move, so use a temporary. */
    int *bigger = realloc(nums, 2 * n * sizeof *nums);
    if (bigger == NULL) {
        free(nums);                  /* the old block is still ours */
        perror("realloc");
        return 1;
    }
    nums = bigger;
    for (size_t i = n; i < 2 * n; i++)
        nums[i] = -1;
    n *= 2;
    for (size_t i = 0; i < n; i++)
        printf("%d ", nums[i]);
    putchar('\n');
    free(nums);

    /* calloc: zeroed memory, and it checks n * size for overflow itself. */
    double *zeros = calloc(4, sizeof *zeros);
    if (zeros == NULL) {
        perror("calloc");
        return 1;
    }
    printf("calloc: %g %g %g %g\n", zeros[0], zeros[1], zeros[2], zeros[3]);
    free(zeros);

    /* An attacker-controlled count: n * size would wrap around to 8. */
    size_t evil = SIZE_MAX / 8 + 2;
    printf("evil * 8 wraps to %zu\n", evil * 8);
    printf("alloc_array(evil, 8) -> %s\n", alloc_array(evil, 8) ? "memory" : "NULL");

    free(NULL);                      /* allowed, does nothing */
    return 0;
}
```

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o alloc alloc.c
$ ./alloc
0 1 4 9 16 -1 -1 -1 -1 -1 
calloc: 0 0 0 0
evil * 8 wraps to 8
alloc_array(evil, 8) -> NULL
```

The habits, and why:

- **`malloc(n * sizeof *p)`, not `malloc(n * sizeof(int))`.** If `p`'s type changes, the size follows.
- **No cast on `malloc`'s result.** In C, `void *` converts to any object pointer type; a cast only hides mistakes. (C++ needs the cast.)
- **Check every result for `NULL`.** On Linux `malloc` rarely fails for small sizes, but does for huge ones, under `ulimit -v`, and in memory-limited containers.
- **Guard `n * size` against overflow.** `size_t` arithmetic wraps. If `n` comes from outside (say, a length field in a network message), `n * size` can wrap to a tiny number; `malloc` succeeds and your loop writes far past the end. A classic security hole. Test `n > SIZE_MAX / size` before multiplying; `calloc` does this for you.
- **The `realloc` temporary.** Never write `p = realloc(p, n);`. On failure `realloc` returns `NULL` and leaves the old block alone, but you just overwrote your only pointer to it: a leak, and usually a crash later.

### Ownership: who frees it?

C has no garbage collector or destructors, so every heap block needs exactly one **owner**, the code that frees it. Types cannot express that, so write it in comments and keep to it:

- *"Returns a new X that the caller owns; free it with x_free()."*
- *"The strbuf owns `data`."*
- *"`msg` borrows `line`: the line must outlive the msg."* ([Chapter 7](07-structs-unions-enums.md)'s `msg_parse`)

Pair every constructor with a destructor (`x_new`/`x_free`, `x_init`/`x_free`), and free in reverse order of creation.

## A growable array

Most "I don't know how many" problems are solved by an array that grows. Keep a pointer, a length and a capacity; when full, `realloc` to **twice** the size:

`examples/ch08/vec.c`
```c
/* vec.c - a growable array of any element type (a "vector"). */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* A vec owns its items block: only vec_free releases it.
 * A zeroed vec is valid and empty: struct vec v = { .elem_size = 4 }; */
struct vec {
    void *items;        /* cap * elem_size bytes on the heap, or NULL */
    size_t len;         /* elements in use */
    size_t cap;         /* elements allocated */
    size_t elem_size;   /* bytes per element */
};

/* Append one element, copied from *elem. Returns 0, or -1 if out of memory
 * (the vec is unchanged then). */
static int vec_push(struct vec *v, const void *elem)
{
    if (v->len == v->cap) {
        size_t cap = v->cap > 0 ? v->cap * 2 : 4;
        if (cap < v->cap || cap > SIZE_MAX / v->elem_size)
            return -1;                          /* the byte count would overflow */
        void *items = realloc(v->items, cap * v->elem_size);
        if (items == NULL)
            return -1;
        v->items = items;
        v->cap = cap;
    }
    /* void * has no arithmetic in standard C: step through it as bytes. */
    memcpy((char *)v->items + v->len * v->elem_size, elem, v->elem_size);
    v->len++;
    return 0;
}

static void *vec_at(const struct vec *v, size_t i)
{
    return (char *)v->items + i * v->elem_size;
}

static void vec_free(struct vec *v)
{
    free(v->items);
    v->items = NULL;
    v->len = v->cap = 0;
}

struct user {
    char nick[17];
    int id;
};

int main(void)
{
    struct vec ints = { .elem_size = sizeof(int) };
    for (int i = 1; i <= 10; i++) {
        size_t old_cap = ints.cap;
        int sq = i * i;
        if (vec_push(&ints, &sq) < 0) {
            fprintf(stderr, "out of memory\n");
            return 1;
        }
        if (ints.cap != old_cap)
            printf("[grew to %zu]\n", ints.cap);
    }
    for (size_t i = 0; i < ints.len; i++)
        printf("%d ", *(int *)vec_at(&ints, i));
    putchar('\n');
    vec_free(&ints);

    struct vec users = { .elem_size = sizeof(struct user) };
    struct user u = { "alice", 1 };
    if (vec_push(&users, &u) < 0)
        return 1;
    u = (struct user){ "bob", 2 };
    if (vec_push(&users, &u) < 0)
        return 1;
    for (size_t i = 0; i < users.len; i++) {
        const struct user *p = vec_at(&users, i);
        printf("user %d: %s\n", p->id, p->nick);
    }
    vec_free(&users);
    return 0;
}
```

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o vec vec.c
$ ./vec
[grew to 4]
[grew to 8]
[grew to 16]
1 4 9 16 25 36 49 64 81 100 
user 1: alice
user 2: bob
```

Why double? Growing by a constant (say +1) makes `n` pushes cost about n²/2 element copies. Doubling copies each element about once on average, so a push takes constant "amortized" time (Exercise 3 measures it). The price, up to 50% unused capacity, is almost always worth it.

Design points:

- **A zeroed struct is a valid empty vec.** `realloc(NULL, n)` acts like `malloc(n)`, so the first push needs no special case. The project's `strbuf` does the same.
- **On failure, nothing changes.** `vec_push` updates `items` and `cap` only after `realloc` succeeds, so the caller can still use or free the vec.
- **`void *` makes it generic, at a cost.** Element types are no longer checked: `*(int *)vec_at(&users, 0)` compiles and reads garbage. Many C programmers write a small array struct per element type instead, as the server does in [Chapter 17](17-threads.md) for its client array (`struct room`).
- **Pointers into a vec go stale.** A growing push may move the array, and every pointer from `vec_at` dangles. Keep indexes instead.

## Heap bugs, and what glibc and musl do about them

The heap rules: free each block exactly once, never touch it after freeing, never write outside it. Break one and you get **undefined behaviour**: the standard places no requirements on what happens next. In practice the outcome depends on your libc's `malloc`, so glibc and musl visibly differ.

`examples/ch08/heap_bugs.c`
```c
/* heap_bugs.c - break the heap rules on purpose (all undefined behaviour).
 * Usage: ./heap_bugs double-free | overflow | use-after-free | leak */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char *argv[])
{
    const char *bug = argc > 1 ? argv[1] : "";
    char *p = malloc(16);
    char *q = malloc(16);
    if (p == NULL || q == NULL)
        return 1;
    printf("bug: %s\n", bug);
    fflush(stdout);                  /* print it before a possible crash */

    if (strcmp(bug, "double-free") == 0) {
        free(p);
        free(p);                     /* freeing the same block twice */
    } else if (strcmp(bug, "overflow") == 0) {
        memset(p, 'A', 64);          /* writes 48 bytes past the end of p */
        free(p);
        free(q);
    } else if (strcmp(bug, "use-after-free") == 0) {
        free(p);
        strcpy(p, "still here?");    /* writing to memory we gave back */
        printf("p says: %s\n", p);
        free(q);
    } else if (strcmp(bug, "leak") == 0) {
        p = NULL;                    /* the only pointer to the block is gone */
        free(q);
    } else {
        free(p);
        free(q);
    }
    printf("survived\n");
    return 0;
}
```

### glibc vs musl: heap corruption

Both builds compile without a warning. At run time:

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o heap_bugs heap_bugs.c
$ musl-gcc -std=c99 -Wall -Wextra -pedantic -static -o heap_bugs-musl heap_bugs.c
$ ./heap_bugs double-free; echo "exit $?"
bug: double-free
free(): double free detected in tcache 2
Aborted (core dumped)
exit 134
$ ./heap_bugs-musl double-free; echo "exit $?"
bug: double-free
Segmentation fault (core dumped)
exit 139
$ ./heap_bugs overflow; echo "exit $?"
bug: overflow
double free or corruption (out)
Aborted (core dumped)
exit 134
$ ./heap_bugs-musl overflow; echo "exit $?"
bug: overflow
Segmentation fault (core dumped)
exit 139
$ ./heap_bugs use-after-free; ./heap_bugs-musl use-after-free
bug: use-after-free
p says: still here?
survived
bug: use-after-free
p says: still here?
survived
$ ./heap_bugs leak; ./heap_bugs-musl leak
bug: leak
survived
bug: leak
survived
```

(Exit status 134 = 128 + 6, SIGABRT; 139 = 128 + 11, SIGSEGV. Signals: [Chapter 13](13-processes-and-signals.md).)

| Bug | glibc 2.35 | musl 1.2.6 |
|---|---|---|
| double free | message + `abort()` | crash, no message |
| write past the end, then `free` | message + `abort()` | crash, no message |
| use after free | not detected | not detected |
| leak | not detected | not detected |

- Both libcs **check their own bookkeeping** in `free` and stop rather than run on with a corrupted heap (that is how heap bugs become exploits).
- **glibc says what it found**: more helpful while learning.
- **musl crashes deliberately and silently.** When its `mallocng` checks fail it executes a trapping instruction (`a_crash()`), seen as SIGSEGV; a debugger shows the stop inside `free` ([Chapter 10](10-errors-and-debugging.md)). musl keeps allocator metadata *out of line*, away from your data, which makes the heap harder to exploit. Its job is to be safe, not chatty.
- **Neither catches use-after-free or leaks.** Those need a tool.
- Either way, the crash is at the `free`, often far from the buggy line.

**Better for learning: glibc**, because it explains the problem. Neither is a debugging tool, though; for that, use AddressSanitizer.

### AddressSanitizer: a first look

`-fsanitize=address` (ASan) makes gcc **instrument** your program so every memory access is checked. Add `-g` so reports show file names and line numbers. The program runs about 2× slower and uses more memory: a development build, not a release.

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -g -fsanitize=address -o heap_bugs-asan heap_bugs.c
$ ./heap_bugs-asan use-after-free
bug: use-after-free
=================================================================
==243382==ERROR: AddressSanitizer: heap-use-after-free on address 0x502000000010 at pc 0x71d563e3a2c3 bp 0x7ffcc7f5b730 sp 0x7ffcc7f5aed8
WRITE of size 12 at 0x502000000010 thread T0
    #0 0x71d563e3a2c2 in __interceptor_memcpy ../../../../src/libsanitizer/sanitizer_common/sanitizer_common_interceptors.inc:827
    #1 0x64ca74a39462 in main /home/you/learn-c/examples/ch08/heap_bugs.c:26
    ...

0x502000000010 is located 0 bytes inside of 16-byte region [0x502000000010,0x502000000020)
freed by thread T0 here:
    #0 0x71d563eb4537 in __interceptor_free ../../../../src/libsanitizer/asan/asan_malloc_linux.cpp:127
    #1 0x64ca74a39447 in main /home/you/learn-c/examples/ch08/heap_bugs.c:25
    ...

previously allocated by thread T0 here:
    #0 0x71d563eb4887 in __interceptor_malloc ../../../../src/libsanitizer/asan/asan_malloc_linux.cpp:145
    #1 0x64ca74a39325 in main /home/you/learn-c/examples/ch08/heap_bugs.c:10
    ...
SUMMARY: AddressSanitizer: heap-use-after-free ...
```

(Shortened: `...` marks omitted lines. Addresses, process IDs and paths will differ.)

Read from the top: *what* happened (a 12-byte write to freed memory at line 26, the `strcpy`), *where the block was freed* (line 25) and *where it was allocated* (line 10). For the other bugs ASan reports `attempting double-free` (line 19) and `heap-buffer-overflow ... WRITE of size 64` (line 21). At exit, its LeakSanitizer reports blocks nothing points to any more:

```console
$ ./heap_bugs-asan leak
bug: leak
survived

=================================================================
==242871==ERROR: LeakSanitizer: detected memory leaks

Direct leak of 16 byte(s) in 1 object(s) allocated from:
    #0 0x788b592b4887 in __interceptor_malloc ../../../../src/libsanitizer/asan/asan_malloc_linux.cpp:145
    #1 0x5678237fb325 in main /home/you/learn-c/examples/ch08/heap_bugs.c:10
    ...

SUMMARY: AddressSanitizer: 16 byte(s) leaked in 1 allocation(s).
```

A program stopped by ASan exits with status 1.

### glibc vs musl: no ASan on musl

ASan needs a runtime library (`libasan`) built for glibc. With musl it does not work:

```console
$ musl-gcc -std=c99 -g -static -fsanitize=address -o heap_bugs-asan heap_bugs.c
gcc: error: cannot specify -static with -fsanitize=address
$ musl-gcc -std=c99 -g -fsanitize=address -o heap_bugs-asan heap_bugs.c
$ ./heap_bugs-asan use-after-free
Error loading shared library libasan.so.6: No such file or directory (needed by ./heap_bugs-asan)
Error relocating ./heap_bugs-asan: __asan_init: symbol not found
...
```

This course's workflow: **develop and test with glibc + ASan, and also build and test with musl**, which finds different bugs (one is coming up). [Chapter 10](10-errors-and-debugging.md) adds UndefinedBehaviorSanitizer, gdb and a `make SANITIZE=1` switch.

## glibc vs musl: what C99 leaves to the library

Some `malloc` behaviour is *implementation-defined*, not a bug: each libc chooses and documents its choice.

`examples/ch08/edge_alloc.c`
```c
/* edge_alloc.c - malloc(0), realloc(p, 0) and a failed malloc.
 * C99 lets each libc choose what the first two do. */
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void)
{
    void *a = malloc(0);
    printf("malloc(0)        -> %s\n", a != NULL ? "a pointer (must be freed)" : "NULL");
    free(a);

    void *p = malloc(16);
    void *r = realloc(p, 0);         /* p is invalid after this, either way */
    printf("realloc(p, 0)    -> %s\n", r != NULL ? "a new pointer (must be freed)" : "NULL");
    free(r);

    volatile size_t huge = SIZE_MAX; /* volatile: stop gcc warning at compile time */
    errno = 0;
    void *big = malloc(huge);
    printf("malloc(SIZE_MAX) -> %s, errno = %s\n",
           big != NULL ? "memory?!" : "NULL", strerror(errno));
    free(big);
    return 0;
}
```

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o edge_alloc edge_alloc.c && ./edge_alloc
malloc(0)        -> a pointer (must be freed)
realloc(p, 0)    -> NULL
malloc(SIZE_MAX) -> NULL, errno = Cannot allocate memory
$ musl-gcc -std=c99 -Wall -Wextra -pedantic -static -o edge_alloc-musl edge_alloc.c && ./edge_alloc-musl
malloc(0)        -> a pointer (must be freed)
realloc(p, 0)    -> a new pointer (must be freed)
malloc(SIZE_MAX) -> NULL, errno = Out of memory
```

- **`realloc(p, 0)` differs.** glibc frees `p` and returns `NULL`; musl returns a new minimum-size block. Code written for one breaks on the other: treat `NULL` as "failed, `p` still valid" and you double-free on glibc; assume `NULL` and you leak on musl. Neither is "better". The portable rule: **never call `realloc` with size 0.** For empty, `free` and set the pointer to `NULL`.
- **`malloc(0)`** returns a unique pointer on both, which you must `free`. Other systems may return `NULL`, so don't treat that as an error.
- **`errno` is `ENOMEM` on both; only the text differs** ("Cannot allocate memory" vs "Out of memory"). Compare `errno` values, never error *strings*.

## memcpy vs memmove

`memcpy(dst, src, n)` requires non-overlapping ranges. `memmove` allows overlap: it copies as if through a temporary buffer. Buffers that drop data from the front, like this chapter's project, move bytes within one array, so they must use `memmove`:

`examples/ch08/overlap.c`
```c
/* overlap.c - memcpy must not be used when source and destination overlap.
 * memmove handles overlap. Here: shift a string right to make room. */
#include <stdio.h>
#include <string.h>

int main(int argc, char *argv[])
{
    char a[64] = "0123456789abcdefghijklmnopqrstuvwxyz";
    char b[64] = "0123456789abcdefghijklmnopqrstuvwxyz";
    size_t n = (size_t)argc + 29;    /* 30, computed at run time so gcc
                                        cannot see the overlap and warn */
    (void)argv;

    memmove(a + 1, a, n);            /* correct */
    memcpy(b + 1, b, n);             /* undefined behaviour: the ranges overlap */
    printf("memmove: %s\n", a);
    printf("memcpy:  %s\n", b);
    return 0;
}
```

### glibc vs musl: overlapping memcpy

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o overlap overlap.c && ./overlap
memmove: 00123456789abcdefghijklmnopqrstvwxyz
memcpy:  00123456789abcdefghijklmnopqrstvwxyz
$ musl-gcc -std=c99 -Wall -Wextra -pedantic -static -o overlap-musl overlap.c && ./overlap-musl
memmove: 00123456789abcdefghijklmnopqrstvwxyz
memcpy:  00000000089abcdeeghijklmmmmmmmmvwxyz
```

glibc's `memcpy` happened to get it right. musl's copies forward in 8-byte words, so it reads bytes it already overwrote and smears them along the string. musl is **better** here: it exposes the bug instead of hiding it. glibc's luck is no promise: glibc 2.13 (2011) brought a faster `memcpy` for newer Intel CPUs that copied in a different order, and programs relying on overlapping `memcpy` broke, Adobe's Flash player among them (glibc bug 12518). Use `memmove` whenever ranges might overlap.

## Error paths, xmalloc and fault injection

Handling `NULL` from `malloc` well is hard. A function with three allocations must free the first two if the third fails, and that code almost never runs, so it is almost never tested. Two strategies:

- **Recover:** return an error and free everything allocated so far. Libraries must, because only the caller can decide what to do. A server should too: one huge request should not kill everyone's session.
- **Give up:** wrap `malloc` in `xmalloc`, which prints a message and exits on failure. Small command-line tools often do this, git included. It is honest: on Linux, a small `malloc` failing usually means the system is already in trouble.

To *test* recovery code, use **fault injection**: route allocations through a wrapper that fails the Nth call on purpose, and run with N = 1, 2, 3, ...

`examples/ch08/faults.c`
```c
/* faults.c - testing out-of-memory paths with fault injection, and xmalloc. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- fault injection: make the Nth allocation fail ---- */
static int alloc_calls = 0;
static int fail_at = 0;              /* 0: never fail */

static void *test_malloc(size_t n)
{
    if (++alloc_calls == fail_at)
        return NULL;
    return malloc(n);
}

/* ---- xmalloc: for programs that cannot go on without memory ---- */
static void *xmalloc(size_t n)
{
    void *p = malloc(n);
    if (p == NULL && n > 0) {
        fprintf(stderr, "faults: out of memory (%zu bytes)\n", n);
        exit(1);
    }
    return p;
}

/* ---- the code under test: three allocations, any of which may fail ---- */
struct user {
    char *nick;
    char *realname;
};

static char *dup_str(const char *s)
{
    size_t n = strlen(s) + 1;
    char *copy = test_malloc(n);
    if (copy != NULL)
        memcpy(copy, s, n);
    return copy;
}

/* Returns a new user that the caller owns (free with user_free), or NULL
 * if out of memory. Never leaks, whichever allocation fails. */
static struct user *user_new(const char *nick, const char *realname)
{
    struct user *u = test_malloc(sizeof *u);
    if (u == NULL)
        return NULL;
    u->nick = dup_str(nick);
    if (u->nick == NULL)
        goto fail_user;
    u->realname = dup_str(realname);
    if (u->realname == NULL)
        goto fail_nick;
    return u;

fail_nick:
    free(u->nick);
fail_user:
    free(u);
    return NULL;
}

static void user_free(struct user *u)
{
    if (u == NULL)
        return;
    free(u->nick);
    free(u->realname);
    free(u);
}

int main(void)
{
    for (fail_at = 1; fail_at <= 4; fail_at++) {
        alloc_calls = 0;
        struct user *u = user_new("alice", "Alice Liddell");
        printf("fail allocation #%d: %s\n", fail_at,
               u != NULL ? "user created" : "NULL, nothing leaked");
        user_free(u);
    }

    char *big = xmalloc(64);         /* never NULL: xmalloc exits instead */
    strcpy(big, "xmalloc worked");
    puts(big);
    free(big);
    return 0;
}
```

The `goto` cleanup chain from [Chapter 3](03-control-flow.md) undoes the work in reverse order: each label frees one more thing and falls through to the next.

Run it under ASan, so "nothing leaked" is checked, not just printed:

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -g -fsanitize=address -o faults faults.c
$ ./faults
fail allocation #1: NULL, nothing leaked
fail allocation #2: NULL, nothing leaked
fail allocation #3: NULL, nothing leaked
fail allocation #4: user created
xmalloc worked
```

Delete the `free(u->nick);` line under `fail_nick:` and run again. Failure #3 now leaks the nick, and ASan says so:

```console
$ ./faults
...
=================================================================
==246668==ERROR: LeakSanitizer: detected memory leaks

Direct leak of 6 byte(s) in 1 object(s) allocated from:
    #0 0x7c4f102b4887 in __interceptor_malloc ../../../../src/libsanitizer/asan/asan_malloc_linux.cpp:145
    #1 0x5aad67c8b34a in test_malloc /home/you/learn-c/examples/ch08/faults.c:14
    #2 0x5aad67c8b405 in dup_str /home/you/learn-c/examples/ch08/faults.c:37
    #3 0x5aad67c8b46c in user_new /home/you/learn-c/examples/ch08/faults.c:50
    ...

SUMMARY: AddressSanitizer: 6 byte(s) leaked in 1 allocation(s).
```

Six bytes: `"alice"` plus its `'\0'`. In a real program the counter and `fail_at` would come from a test harness ([Chapter 11](11-testing.md)), not `main`.

## Arenas: free everything at once

Sometimes many objects share one lifetime: the pieces of one parsed request, or the nodes of a tree you build and throw away. An **arena** (or *region*) allocator takes one big block and hands out pieces by bumping an offset. There is no per-object `free`; you free the whole arena at once:

`examples/ch08/arena.c`
```c
/* arena.c - a bump allocator: many small allocations, one free. */
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* C99 has no max_align_t (that is C11), so build our own: a union is
 * aligned for the strictest of its members. */
union align_max {
    long double ld;
    long long ll;
    void *p;
    void (*fp)(void);
};
enum { ALIGN = sizeof(union align_max) };   /* a safe alignment for anything */

/* An arena owns one heap block and hands out pieces of it in order.
 * Pieces are never freed one by one: arena_free releases them all. */
struct arena {
    char *base;
    size_t used;
    size_t cap;
};

static int arena_init(struct arena *a, size_t cap)
{
    a->base = malloc(cap);
    a->used = 0;
    a->cap = a->base != NULL ? cap : 0;
    return a->base != NULL ? 0 : -1;
}

/* Returns n bytes aligned for any type, or NULL if the arena is full. */
static void *arena_alloc(struct arena *a, size_t n)
{
    size_t start = (a->used + ALIGN - 1) / ALIGN * ALIGN;   /* round up */
    if (start > a->cap || n > a->cap - start)
        return NULL;
    a->used = start + n;
    return a->base + start;
}

static char *arena_strdup(struct arena *a, const char *s)
{
    size_t n = strlen(s) + 1;
    char *copy = arena_alloc(a, n);
    if (copy != NULL)
        memcpy(copy, s, n);
    return copy;
}

static void arena_free(struct arena *a)
{
    free(a->base);
    a->base = NULL;
    a->used = a->cap = 0;
}

int main(void)
{
    struct arena a;
    if (arena_init(&a, 256) < 0)
        return 1;

    printf("ALIGN = %d\n", (int)ALIGN);
    const char *words[] = { "parse", "one", "message", "then", "forget", "it" };
    char *copies[6];
    for (int i = 0; i < 6; i++) {
        copies[i] = arena_strdup(&a, words[i]);
        if (copies[i] == NULL)
            return 1;
        printf("%-8s at offset %3td\n", copies[i], copies[i] - a.base);
    }
    double *d = arena_alloc(&a, sizeof *d);
    if (d == NULL)
        return 1;
    *d = 3.5;
    printf("double   at offset %3td, used %zu of %zu\n",
           (char *)d - a.base, a.used, a.cap);
    printf("arena_alloc(&a, 1000) -> %s\n", arena_alloc(&a, 1000) ? "memory" : "NULL");
    arena_free(&a);                  /* everything above is gone at once */
    return 0;
}
```

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o arena arena.c
$ ./arena
ALIGN = 16
parse    at offset   0
one      at offset  16
message  at offset  32
then     at offset  48
forget   at offset  64
it       at offset  80
double   at offset  96, used 104 of 256
arena_alloc(&a, 1000) -> NULL
```

The musl build prints the same. Why arenas are worth knowing:

- **Speed:** an allocation is an addition and a comparison.
- **No leaks or double frees inside the arena:** there is exactly one `free`.
- **Alignment:** every piece starts at a multiple of `ALIGN`, so it can hold any type, like `malloc` memory. C11 has `max_align_t` and `alignof`; in C99 the union trick gives a safe value. (On x86-64 `long double` is 16 bytes with 16-byte alignment, which is also what `malloc` guarantees.)
- **Limits:** you cannot free or grow one piece. A real arena chains more blocks when one fills up.

parley needs no arena: it parses messages in place with no allocation at all, which is cheaper still. Arenas show up in compilers, game engines and web servers that allocate per request.

## Building parley: a growable input buffer

The server reads with `fgets(line, 513, stdin)`. That works for terminal input, but has two problems that get worse with a network:

1. A **socket delivers bytes, not lines**. One `read` may return half a line, or three and a half. The server must collect bytes and cut out complete lines itself: **framing**.
2. **Line endings.** The protocol says `\r\n`, but `nc` and many test tools send plain `\n`. Be liberal in what you accept.

This milestone adds:

- `struct strbuf`, a growable heap byte buffer, with `sb_reserve`, `sb_append`, `sb_consume` and `sb_free`;
- `take_line`, which cuts the next complete line (LF or CRLF) out of a strbuf and throws away lines over 512 bytes with a `417` reply (the next line still works);
- a `struct conn` that owns its input buffer.

Until [Chapter 12](12-files-and-descriptors.md), `fgets` into a small 256-byte `chunk` stands in for `read`; `take_line` works however the input is split. `str_chomp` is no longer needed.

### The strbuf

Add `#include <stdint.h>` (for `SIZE_MAX`), delete `str_chomp`, and add this section after `parse_args`:

`project/ch08/parleyd.c` (new section)
```c
/* ---- strbuf: a growable byte buffer ------------------------------------- */

/* A strbuf owns data, a heap block of cap bytes holding len bytes of
 * content. data[len] is always '\0' (once data is allocated), so the
 * content can be printed as a string. A zeroed strbuf is a valid, empty
 * buffer: struct strbuf sb = { 0 }; ... sb_free(&sb); */
struct strbuf {
    char *data;
    size_t len;
    size_t cap;
};

/* Make room for extra more bytes (plus the '\0'). Returns 0, or -1 if
 * the size would overflow or memory is exhausted (buffer unchanged). */
static int sb_reserve(struct strbuf *sb, size_t extra)
{
    if (extra > SIZE_MAX - 1 - sb->len)
        return -1;                      /* len + extra + 1 would overflow */
    size_t need = sb->len + extra + 1;
    if (need <= sb->cap)
        return 0;

    size_t cap = sb->cap > 0 ? sb->cap : 64;
    while (cap < need)
        cap = cap <= SIZE_MAX / 2 ? cap * 2 : need;   /* double, or give up doubling */

    /* Never write p = realloc(p, n): if realloc fails it returns NULL and
     * the old block would leak. Keep the old pointer until we know. */
    char *data = realloc(sb->data, cap);
    if (data == NULL)
        return -1;
    sb->data = data;
    sb->cap = cap;
    return 0;
}

/* Append n bytes (which may include '\0' bytes). Returns 0 or -1. */
static int sb_append(struct strbuf *sb, const void *bytes, size_t n)
{
    if (sb_reserve(sb, n) < 0)
        return -1;
    memcpy(sb->data + sb->len, bytes, n);
    sb->len += n;
    sb->data[sb->len] = '\0';
    return 0;
}

/* Drop the first n bytes (all of them if n >= len); the rest moves to the
 * front. This is how we remove a line once it has been handled. */
static void sb_consume(struct strbuf *sb, size_t n)
{
    if (n >= sb->len) {
        sb->len = 0;
    } else {
        memmove(sb->data, sb->data + n, sb->len - n);   /* regions overlap */
        sb->len -= n;
    }
    if (sb->data != NULL)
        sb->data[sb->len] = '\0';
}

/* Release the memory; the strbuf is empty and reusable afterwards. */
static void sb_free(struct strbuf *sb)
{
    free(sb->data);
    *sb = (struct strbuf){ .data = NULL };
}
```

Each piece uses something from this chapter:

- **The ownership comment** says who frees `data` and states the *invariant* every function keeps: `data[len] == '\0'`.
- **The overflow check** is `extra > SIZE_MAX - 1 - sb->len`, not `sb->len + extra + 1 > SIZE_MAX`, which can never be true because the addition wraps first. Rearranging so only safe subtractions happen is the standard trick.
- **Doubling, with a ceiling.** Start at 64 bytes and double until big enough; if doubling would overflow, ask for exactly `need`.
- **The `realloc` temporary,** with a comment saying why.
- **`memmove` in `sb_consume`**: the remaining bytes move to the front of the same block. The overlap test above shows what `memcpy` could do here on musl.
- **`sb_free` leaves a valid empty buffer.** Resetting with a compound literal makes a second `sb_free` harmless (`free(NULL)`) and a later `sb_append` safe.

### Framing: take_line

Add this after `msg_format`:

```c
enum frame_status {
    FRAME_NONE,       /* no complete line yet: read more input */
    FRAME_LINE,       /* a line was copied out */
    FRAME_TOO_LONG    /* a line longer than MSG_LINE_MAX was thrown away */
};

/* Take the next complete line out of the input buffer in. On FRAME_LINE
 * the line (without "\r\n" or "\n") is in line[MSG_LINE_MAX + 1].
 * *skipping remembers, between calls, that we are throwing away the rest
 * of a too-long line. Works no matter how the input was split up. */
static enum frame_status take_line(struct strbuf *in, bool *skipping,
                                   char line[MSG_LINE_MAX + 1])
{
    for (;;) {
        char *nl = in->len > 0 ? memchr(in->data, '\n', in->len) : NULL;
        if (nl == NULL) {
            if (in->len < MSG_LINE_MAX)
                return FRAME_NONE;      /* the line may still be completed */
            sb_consume(in, in->len);    /* too long already: drop it */
            if (*skipping)
                return FRAME_NONE;
            *skipping = true;
            return FRAME_TOO_LONG;
        }

        size_t n = (size_t)(nl - in->data) + 1;    /* bytes including '\n' */
        if (*skipping) {                /* the end of a too-long line */
            sb_consume(in, n);
            *skipping = false;
            continue;
        }
        if (n > MSG_LINE_MAX) {
            sb_consume(in, n);
            return FRAME_TOO_LONG;
        }
        memcpy(line, in->data, n - 1);
        line[n - 1] = '\0';
        if (n >= 2 && line[n - 2] == '\r')
            line[n - 2] = '\0';
        sb_consume(in, n);
        return FRAME_LINE;
    }
}
```

How it works:

- **`memchr`** finds the `'\n'` in the first `len` bytes. Unlike `strchr`, it does not stop at a `'\0'` byte, which a client might send.
- **No newline yet:** with fewer than 512 bytes waiting, the next chunk may complete the line, so return `FRAME_NONE` ("read more"). With 512 bytes and no newline, the line is too long whatever comes next: drop what we have, report it once, and set `*skipping`.
- **`*skipping`** carries state *between calls*: the rest of a too-long line may arrive many chunks later, and everything up to its `'\n'` must be dropped. The flag lives in `struct conn` because each client needs its own.
- **Memory never grows past about 512 bytes plus one chunk**, however long a line an attacker sends. A buffer that grows without limit is a denial-of-service hole.
- The line is **copied out** to `line[]`, because `msg_parse` writes into it and `sb_consume` moves the buffer's bytes.

The `char line[MSG_LINE_MAX + 1]` parameter is really a `char *` (arrays decay, [Chapter 5](05-pointers-and-arrays.md)); the size documents what the caller must pass.

### The connection owns its buffer

Add two members to `struct conn`:

`project/ch08/parleyd.c`
```c
/* Everything the server knows about a connected client. */
struct conn {
    char nick[NICK_MAX + 1];   /* "" until the client registers */
    bool quitting;             /* sent QUIT: stop reading */
    struct strbuf in;          /* bytes received but not yet handled */
    bool skipping;             /* throwing away a too-long line */
};
```

### main: read chunks, take lines

```c
    struct conn client = { .quitting = false };   /* zeroed: nick "", in empty */
    char chunk[256];
    char line[MSG_LINE_MAX + 1];
    /* fgets gives us the input in pieces of at most 255 bytes. take_line
     * puts the pieces back together into whole lines. */
    while (!client.quitting && fgets(chunk, sizeof chunk, stdin) != NULL) {
        if (sb_append(&client.in, chunk, strlen(chunk)) < 0) {
            log_msg(LOG_ERROR, "out of memory");
            break;
        }
        enum frame_status st;
        while (!client.quitting
               && (st = take_line(&client.in, &client.skipping, line)) != FRAME_NONE) {
            if (st == FRAME_TOO_LONG)
                send_numeric(&client, "417", NULL, "Input line too long");
            else
                handle_line(&client, line);
        }
    }
    sb_free(&client.in);
    log_msg(LOG_INFO, "session over");
    return 0;
}
```

This replaces the `fgets` loop at the end of `main`. Two loops: the outer one *reads* a chunk; the inner one *handles every complete line* that chunk finished (zero or several). Every network server has this shape, and the server keeps it through [Chapter 18](18-event-loop.md)'s event loop.

Lifetimes in `main`: `client` is automatic and lives until `main` returns. `client.in.data` is allocated and owned by `client`, so `main` frees it with `sb_free` before returning. In [Chapter 17](17-threads.md) each `struct conn` itself lives on the heap.

### Checkpoint

Build as before (`\r\n` line endings not shown):

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o parleyd parleyd.c
$ gcc -std=c99 -Wall -Wextra -pedantic -o parley parley.c
$ musl-gcc -std=c99 -Wall -Wextra -pedantic -static -o parleyd-musl parleyd.c
$ musl-gcc -std=c99 -Wall -Wextra -pedantic -static -o parley-musl parley.c
$ printf 'NICK alice\nMSG :unix newline\r\nMSG :dos newline\r\nQUIT\r\n' | ./parleyd 2>/dev/null
:parley 001 alice :Welcome to parley, alice
:alice JOIN
:parley 353 alice :alice
:parley 366 alice :End of NAMES
:alice MSG :unix newline
:alice MSG :dos newline
ERROR :Goodbye
```

A 600-byte line arrives in three chunks. It gets a 417, and the next line still works:

```console
$ { printf 'NICK alice\r\nMSG :'; head -c 600 /dev/zero | tr '\0' x; printf '\r\nMSG :still here\r\nQUIT\r\n'; } | ./parleyd 2>/dev/null
:parley 001 alice :Welcome to parley, alice
:alice JOIN
:parley 353 alice :alice
:parley 366 alice :End of NAMES
:parley 417 alice :Input line too long
:alice MSG :still here
ERROR :Goodbye
```

Exactly 512 bytes (`MSG :` + 505 bytes + CRLF) is allowed. The echo adds the `:alice ` prefix, so `msg_format` cuts the trailing text to keep the reply at 512 bytes. `awk` prints each reply's length (the `\r` it keeps, plus one for the `\n`):

```console
$ { printf 'NICK alice\r\nMSG :'; head -c 505 /dev/zero | tr '\0' z; printf '\r\nQUIT\r\n'; } | ./parleyd 2>/dev/null | awk '{ print length($0) + 1, substr($0, 1, 30) }'
45 :parley 001 alice :Welcome to 
13 :alice JOIN
26 :parley 353 alice :alice
33 :parley 366 alice :End of NAME
512 :alice MSG :zzzzzzzzzzzzzzzzzz
16 ERROR :Goodbye
$ printf 'hello\n/quit\n' | ./parley -n alice | ./parleyd 2>/dev/null
:parley 001 alice :Welcome to parley, alice
:alice JOIN
:parley 353 alice :alice
:parley 366 alice :End of NAMES
:alice MSG :hello
ERROR :Goodbye
$ { printf 'NICK alice\r\nMSG :'; head -c 600 /dev/zero | tr '\0' x; printf '\r\nMSG :still here\r\nQUIT\r\n'; } | ./parleyd-musl 2>/dev/null
:parley 001 alice :Welcome to parley, alice
:alice JOIN
:parley 353 alice :alice
:parley 366 alice :End of NAMES
:parley 417 alice :Input line too long
:alice MSG :still here
ERROR :Goodbye
```

Finally, the server's first sanitizer build; `,undefined` adds the undefined-behaviour checker ([Chapter 10](10-errors-and-debugging.md)). A 3000-byte line makes the buffer grow several times and runs the skipping path. stderr shows only the normal log lines: no report means no memory error and no leak on this input.

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -g -fsanitize=address,undefined -o parleyd-asan parleyd.c
$ { printf 'NICK alice\r\nMSG :'; head -c 3000 /dev/zero | tr '\0' x; printf '\r\nMSG :ok\r\nQUIT\r\n'; } | ./parleyd-asan >/dev/null
parleyd: info: reading one client session from stdin
parleyd: info: alice joined
parleyd: info: alice left (Quit)
parleyd: info: session over
$ echo $?
0
```

The complete project at this point is in `project/ch08/`.

## Summary

- Objects have **static** (whole run), **automatic** (until the block ends) or **allocated** (until `free`) storage duration. Never return the address of an automatic object.
- `malloc` gives uninitialized memory, `calloc` zeroed memory with an overflow check, and `realloc` resizes and may move the block. Check every result for `NULL`.
- Write `p = malloc(n * sizeof *p)`. Test `n > SIZE_MAX / size` before multiplying sizes that come from outside.
- Never `p = realloc(p, n)`: use a temporary, because on failure the old block is still yours. Never call `realloc(p, 0)`: glibc returns `NULL`, musl a new block.
- Every heap block has one owner. Document it, and pair `x_new` with `x_free`.
- Grow arrays by doubling for amortized constant-time appends. Pointers into a growable array go stale when it moves.
- Double free and heap overflow: glibc prints a diagnosis and aborts (134), musl crashes silently on purpose (139). Neither detects use-after-free or leaks.
- ASan (`-g -fsanitize=address`) finds all four, with stack traces. It works with glibc only.
- Use `memmove` when ranges may overlap. Overlapping `memcpy` happened to work on glibc and corrupted data on musl.
- Test error paths with fault injection. Use `xmalloc` (exit on failure) only where giving up is acceptable.
- An arena allocates by bumping an offset and frees everything at once. In C99, align with a union of the strictest types (no `max_align_t`).
- parley: the server reads chunks into a `strbuf` and cuts out LF or CRLF lines with `take_line`. Too-long lines get 417 without unbounded memory.

## Exercises

1. **A linked list.** Write a singly linked list of nicknames. Each node owns a heap copy of its nick. Implement `list_push` (at the front), `list_remove` (first match; return whether one was found) and `list_free`, then test them with ASan. Hint: walking a `struct node **` lets you unlink the head and middle nodes with the same code.

   <details><summary>Solution</summary>

   ```c
   #include <stdbool.h>
   #include <stdio.h>
   #include <stdlib.h>
   #include <string.h>

   /* A node owns its nick (a copy). The list owns all its nodes. */
   struct node {
       char *nick;
       struct node *next;
   };

   /* Add a copy of nick at the front. Returns 0, or -1 if out of memory. */
   static int list_push(struct node **head, const char *nick)
   {
       struct node *n = malloc(sizeof *n);
       if (n == NULL)
           return -1;
       size_t len = strlen(nick) + 1;
       n->nick = malloc(len);
       if (n->nick == NULL) {
           free(n);
           return -1;
       }
       memcpy(n->nick, nick, len);
       n->next = *head;
       *head = n;
       return 0;
   }

   /* Remove the first node with this nick. Returns true if one was found. */
   static bool list_remove(struct node **head, const char *nick)
   {
       for (struct node **pp = head; *pp != NULL; pp = &(*pp)->next) {
           struct node *n = *pp;
           if (strcmp(n->nick, nick) == 0) {
               *pp = n->next;          /* unlink: works for the head too */
               free(n->nick);
               free(n);
               return true;
           }
       }
       return false;
   }

   static void list_free(struct node **head)
   {
       struct node *n = *head;
       while (n != NULL) {
           struct node *next = n->next;   /* read next before freeing n */
           free(n->nick);
           free(n);
           n = next;
       }
       *head = NULL;
   }

   static void list_print(const struct node *n)
   {
       for (; n != NULL; n = n->next)
           printf("%s ", n->nick);
       putchar('\n');
   }

   int main(void)
   {
       struct node *users = NULL;
       const char *nicks[] = { "alice", "bob", "carol", "dave" };
       for (int i = 0; i < 4; i++)
           if (list_push(&users, nicks[i]) < 0)
               return 1;
       list_print(users);
       list_remove(&users, "dave");      /* the head */
       list_remove(&users, "bob");       /* the middle */
       printf("remove zed: %s\n", list_remove(&users, "zed") ? "found" : "not found");
       list_print(users);
       list_free(&users);
       return 0;
   }
   ```

   ```console
   $ gcc -std=c99 -Wall -Wextra -pedantic -g -fsanitize=address -o list list.c && ./list
   dave carol bob alice 
   remove zed: not found
   carol alice 
   ```

   No ASan report: nothing leaked. A common bug is `free(n); n = n->next;` in `list_free`, which reads freed memory: ASan reports `heap-use-after-free`.
   </details>

2. **Answer without notes.** (a) What does `realloc` return on failure, and what happened to the old block? (b) Why does `sb_consume` use `memmove`? (c) Why is `if (len + extra + 1 > SIZE_MAX)` useless as an overflow check? (d) Your program dies with `free(): double free detected in tcache 2` on glibc. What would the musl build most likely do, and which tool shows you the line that freed the block first?

   <details><summary>Solution</summary>

   (a) `NULL`; the old block is untouched and still yours to free. Hence the temporary.
   (b) The remaining bytes move to the front of the *same* block, so the ranges overlap. `memcpy` is undefined then, and musl's really corrupts the data.
   (c) `size_t` arithmetic wraps, so the sum can never exceed `SIZE_MAX`. Rearrange so nothing can overflow: `extra > SIZE_MAX - 1 - len`.
   (d) Crash with SIGSEGV (exit status 139), silently. Build with `-g -fsanitize=address` on glibc: the `attempting double-free` report shows both `free` calls and the `malloc`.
   </details>

3. **Why double?** Write a program that pushes 100000 `int`s into an array twice: once growing the capacity by +1, once by ×2. Count the `realloc` calls and the worst-case bytes copied, assuming `realloc` copies the whole old block every time.

   <details><summary>Solution</summary>

   ```c
   #include <stdio.h>
   #include <stdlib.h>

   /* Push n ints, growing the array with grow(cap). Count the reallocs and
    * the worst-case bytes copied (realloc may copy the whole old block). */
   static void run(const char *name, size_t (*grow)(size_t), size_t n)
   {
       int *items = NULL;
       size_t len = 0, cap = 0, reallocs = 0, copied = 0;
       for (size_t i = 0; i < n; i++) {
           if (len == cap) {
               size_t new_cap = grow(cap);
               int *tmp = realloc(items, new_cap * sizeof *items);
               if (tmp == NULL) {
                   free(items);
                   fprintf(stderr, "out of memory\n");
                   exit(1);
               }
               copied += len * sizeof *items;
               reallocs++;
               items = tmp;
               cap = new_cap;
           }
           items[len++] = (int)i;
       }
       printf("%-6s %9zu reallocs, up to %15zu bytes copied\n", name, reallocs, copied);
       free(items);
   }

   static size_t plus_one(size_t cap) { return cap + 1; }
   static size_t times_two(size_t cap) { return cap > 0 ? cap * 2 : 1; }

   int main(void)
   {
       run("+1", plus_one, 100000);
       run("x2", times_two, 100000);
       return 0;
   }
   ```

   ```console
   $ gcc -std=c99 -Wall -Wextra -pedantic -O2 -o growth growth.c && ./growth
   +1        100000 reallocs, up to     19999800000 bytes copied
   x2            18 reallocs, up to          524284 bytes copied
   ```

   About 20 GB against half a megabyte: n²/2 element copies against fewer than 2n. The +1 version still runs fast here, because both libcs can often grow the last heap block in place. You cannot count on that once other allocations sit in the way, the normal case in a real program.
   </details>

4. **Word frequencies (challenge).** Write `wordfreq`: read stdin, count each word (a run of ASCII letters, folded to lower case) in a hash map, and print the 20 most frequent words as `%7ld %s`, by count descending and then alphabetically. Use the FNV-1a hash (start with `2166136261`; for each byte, XOR it in, then multiply by `16777619`, all in `uint32_t`), chaining for collisions, and double the bucket array when the number of entries reaches the number of buckets. Sort with `qsort`. Check your output against a shell pipeline, and run it under ASan.

   <details><summary>Solution</summary>

   ```c
   /* wordfreq.c - count words on stdin with a hash map, print the top 20.
    * A word is a run of ASCII letters, folded to lower case. */
   #include <ctype.h>
   #include <stdint.h>
   #include <stdio.h>
   #include <stdlib.h>
   #include <string.h>

   struct entry {
       char *word;                 /* owned */
       long count;
       struct entry *next;         /* the next entry in the same bucket */
   };

   struct map {
       struct entry **buckets;     /* nbuckets chains */
       size_t nbuckets;
       size_t len;                 /* entries in the map */
   };

   static uint32_t fnv1a(const char *s)
   {
       uint32_t h = 2166136261u;
       for (; *s != '\0'; s++) {
           h ^= (unsigned char)*s;
           h *= 16777619u;
       }
       return h;
   }

   /* Double the bucket array and move every entry. Returns 0 or -1. */
   static int map_grow(struct map *m)
   {
       size_t n = m->nbuckets > 0 ? m->nbuckets * 2 : 64;
       struct entry **b = calloc(n, sizeof *b);
       if (b == NULL)
           return -1;
       for (size_t i = 0; i < m->nbuckets; i++) {
           struct entry *e = m->buckets[i];
           while (e != NULL) {
               struct entry *next = e->next;
               size_t j = fnv1a(e->word) % n;
               e->next = b[j];
               b[j] = e;
               e = next;
           }
       }
       free(m->buckets);
       m->buckets = b;
       m->nbuckets = n;
       return 0;
   }

   /* Add 1 to word's count. Returns 0 or -1 (out of memory). */
   static int map_add(struct map *m, const char *word)
   {
       if (m->len >= m->nbuckets && map_grow(m) < 0)   /* keep chains short */
           return -1;
       size_t i = fnv1a(word) % m->nbuckets;
       for (struct entry *e = m->buckets[i]; e != NULL; e = e->next) {
           if (strcmp(e->word, word) == 0) {
               e->count++;
               return 0;
           }
       }
       struct entry *e = malloc(sizeof *e);
       size_t len = strlen(word) + 1;
       char *copy = malloc(len);
       if (e == NULL || copy == NULL) {
           free(e);
           free(copy);
           return -1;
       }
       memcpy(copy, word, len);
       *e = (struct entry){ .word = copy, .count = 1, .next = m->buckets[i] };
       m->buckets[i] = e;
       m->len++;
       return 0;
   }

   static void map_free(struct map *m)
   {
       for (size_t i = 0; i < m->nbuckets; i++) {
           struct entry *e = m->buckets[i];
           while (e != NULL) {
               struct entry *next = e->next;
               free(e->word);
               free(e);
               e = next;
           }
       }
       free(m->buckets);
       *m = (struct map){ .buckets = NULL };
   }

   /* qsort comparator: higher count first, then alphabetical. */
   static int by_count(const void *a, const void *b)
   {
       const struct entry *x = *(const struct entry *const *)a;
       const struct entry *y = *(const struct entry *const *)b;
       if (x->count != y->count)
           return x->count < y->count ? 1 : -1;
       return strcmp(x->word, y->word);
   }

   int main(void)
   {
       struct map m = { .buckets = NULL };
       char word[64];
       size_t len = 0;
       int c;
       do {
           c = getchar();
           if (c != EOF && isalpha(c)) {
               if (len < sizeof word - 1)
                   word[len++] = (char)tolower(c);
           } else if (len > 0) {
               word[len] = '\0';
               len = 0;
               if (map_add(&m, word) < 0) {
                   fprintf(stderr, "wordfreq: out of memory\n");
                   return 1;
               }
           }
       } while (c != EOF);

       struct entry **all = calloc(m.len + 1, sizeof *all);   /* + 1: never 0 */
       if (all == NULL)
           return 1;
       size_t k = 0;
       for (size_t i = 0; i < m.nbuckets; i++)
           for (struct entry *e = m.buckets[i]; e != NULL; e = e->next)
               all[k++] = e;
       qsort(all, k, sizeof *all, by_count);
       for (size_t i = 0; i < k && i < 20; i++)
           printf("%7ld %s\n", all[i]->count, all[i]->word);
       free(all);
       map_free(&m);
       return 0;
   }
   ```

   `qsort` sorts an array of *pointers* to entries, so the comparator gets pointers to those pointers: hence `*(const struct entry *const *)a`. Words over 63 letters are cut short, fine for normal text. Compare with `tr`, `sort` and `uniq` on any text, for example this course's chapters:

   ```console
   $ gcc -std=c99 -Wall -Wextra -pedantic -g -fsanitize=address,undefined -o wordfreq wordfreq.c
   $ cat *.md > input.txt
   $ ./wordfreq < input.txt > mine.txt
   $ tr -cs 'A-Za-z' '\n' < input.txt | tr 'A-Z' 'a-z' | grep -v '^$' | LC_ALL=C sort | uniq -c | LC_ALL=C sort -k1,1nr -k2,2 | head -20 > theirs.txt
   $ diff mine.txt theirs.txt && echo identical
   identical
   ```

   `LC_ALL=C` makes `sort` compare bytes like `strcmp`. Your locale might sort differently. The musl build gives identical output.
   </details>

Sources for this chapter: [REFERENCES.md](REFERENCES.md#chapter-8--memory)
