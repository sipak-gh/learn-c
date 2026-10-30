# Chapter 7 — Structs, Unions and Enums

Real data has parts: a protocol message has a prefix, a command and parameters; a client has a nickname and a state. This chapter covers C's tools for your own types: **structs** (several values together), **unions** (one of several values), **enums** (named integer constants) and `typedef`. You will also see how a struct sits in memory, which network code depends on. In the project the protocol stops being text glued together with `printf` and becomes data: a `struct msg` with a parser and formatter, a `struct conn` for the client, and a command table of structs. It builds on the string tools of [Chapter 6](06-strings.md). [Chapter 8](08-memory.md) shows how to allocate structs at run time.

## Structs

A struct groups named values, called **members**, into a new type:

`examples/ch07/point.c`
```c
/* point.c - defining a struct, members, copying, and pointers to structs. */
#include <stdio.h>

struct point {          /* a new type: "struct point" */
    int x;
    int y;
};                      /* note the semicolon */

/* Takes a copy: changing p here does not change the caller's point. */
static void move_copy(struct point p, int dx)
{
    p.x += dx;
}

/* Takes a pointer: changes the caller's point. p->x means (*p).x */
static void move(struct point *p, int dx)
{
    p->x += dx;
}

/* Structs can be returned by value, too. */
static struct point midpoint(struct point a, struct point b)
{
    struct point m = { (a.x + b.x) / 2, (a.y + b.y) / 2 };
    return m;
}

int main(void)
{
    struct point a = { 1, 2 };   /* members in declaration order */
    struct point b;
    b.x = 9;
    b.y = 6;

    struct point c = a;          /* assignment copies every member */
    c.x = 100;
    printf("a = (%d, %d), c = (%d, %d)\n", a.x, a.y, c.x, c.y);

    move_copy(a, 10);
    printf("after move_copy: a.x = %d\n", a.x);
    move(&a, 10);
    printf("after move:      a.x = %d\n", a.x);

    struct point m = midpoint(a, b);
    printf("midpoint = (%d, %d)\n", m.x, m.y);

    struct point *p = &b;
    printf("p->x = %d, (*p).y = %d, sizeof(struct point) = %zu\n",
           p->x, (*p).y, sizeof(struct point));
    return 0;
}
```

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o point point.c
$ ./point
a = (1, 2), c = (100, 2)
after move_copy: a.x = 1
after move:      a.x = 11
midpoint = (10, 4)
p->x = 9, (*p).y = 6, sizeof(struct point) = 8
```

| You write | It means |
|---|---|
| `struct point { int x; int y; };` | defines the type `struct point`; `point` is the **tag** |
| `struct point a;` | an object of that type; `struct` is part of the type name |
| `a.x` | member `x` of the struct `a` |
| `p->x` | member `x` of the struct `p` points to; short for `(*p).x` |
| `c = a;` | copies every member, like copying an `int` |
| `f(a)` / `return a;` | passes or returns a full copy |

Structs are **values**, like `int`s: assigning or passing one copies all its bytes. That is simple and safe, but copying a big struct costs time, and the callee cannot change the caller's struct. Usual C style:

- **Pointer** (`struct point *`) when the function must change the struct.
- **Pointer to const** (`const struct point *`) to read a struct bigger than a few words: nothing is copied, and `const` promises no changes.
- **By value** only for small structs such as a point.

You cannot assign an array, but an array *inside* a struct is copied with it. The project uses that: `struct conn` holds `char nick[17]`.

### Gotchas

Forget the semicolon after the closing brace, and the error points at the *next* line:

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -c e1.c
e1.c:2:1: error: expected ‘;’, identifier or ‘(’ before ‘int’
    2 | int main(void) { return 0; }
      | ^~~
```

The type is `struct point`, not `point` (C++ lets you drop `struct`; C does not):

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -c e2.c
e2.c:2:1: error: unknown type name ‘point’; use ‘struct’ keyword to refer to the type
    2 | point origin;
      | ^~~~~
      | struct 
```

`.` on a pointer, or `->` on a struct, is an error, and gcc suggests the right one (two follow-on warnings not shown):

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -c e4.c
e4.c: In function ‘get_x’:
e4.c:4:13: error: ‘p’ is a pointer; did you mean to use ‘->’?
    4 |     return p.x;
      |             ^
      |             ->
```

C has no `==` for structs. Write a comparison function (the layout section shows why `memcmp` is no fix):

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -c e3.c
e3.c: In function ‘main’:
e3.c:5:14: error: invalid operands to binary == (have ‘struct point’ and ‘struct point’)
    5 |     return a == b;
      |              ^~
```

## typedef, designated initializers and compound literals

`typedef` gives an existing type a new name. It does not create a new type.

```c
typedef unsigned long ulong;            /* ulong is another name for unsigned long */
typedef struct { int x, y; } point_t;   /* name an untagged struct */
typedef void command_fn(const char *);  /* a function type (used in the project) */
```

C99 added two clearer ways to write struct values:

- **Designated initializers** name the members: `{ .w = 5, .label = "b" }`. Members you do not name become zero (`0`, `0.0` or `NULL`). They work for array elements too: `[BLUE] = "blue"`.
- **Compound literals** create an unnamed object wherever an expression can go: `(type){ initializers }`, like a cast followed by an initializer.

`examples/ch07/init.c`
```c
/* init.c - typedef, designated initializers and compound literals (C99). */
#include <stdio.h>

typedef struct {        /* typedef: "rect" is now a name for this type */
    int x, y;
    int w, h;
    const char *label;
} rect;

enum color { RED, GREEN, BLUE, NCOLORS };

/* Designated initializers work for array indexes too: the table stays
 * right even if someone reorders the enum. */
static const char *const color_names[NCOLORS] = {
    [BLUE] = "blue",
    [RED] = "red",
    [GREEN] = "green",
};

static int area(const rect *r)
{
    return r->w * r->h;
}

static void show(const rect *r)
{
    printf("%-6s at (%d,%d) %dx%d, area %d\n",
           r->label ? r->label : "(none)", r->x, r->y, r->w, r->h, area(r));
}

int main(void)
{
    rect a = { 0, 0, 4, 3, "a" };              /* positional: easy to get wrong */
    rect b = { .w = 5, .h = 2, .label = "b" }; /* designated: x and y are 0 */
    rect c = { 0 };                            /* everything zero / NULL */
    show(&a);
    show(&b);
    show(&c);

    /* A compound literal is an unnamed object: (type){ initializers }.
     * Here it lives until the end of main's block. */
    show(&(rect){ .x = 1, .y = 1, .w = 2, .h = 2, .label = "tmp" });

    b = (rect){ .label = "reset" };            /* assign a whole new value */
    show(&b);

    int *nums = (int[]){ 3, 1, 4 };            /* array compound literal */
    printf("nums[2] = %d\n", nums[2]);

    for (int i = 0; i < NCOLORS; i++)
        printf("color %d is %s\n", i, color_names[i]);
    return 0;
}
```

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o init init.c
$ ./init
a      at (0,0) 4x3, area 12
b      at (0,0) 5x2, area 10
(none) at (0,0) 0x0, area 0
tmp    at (1,1) 2x2, area 4
reset  at (0,0) 0x0, area 0
nums[2] = 4
color 0 is red
color 1 is green
color 2 is blue
```

Why this matters:

- **Designated initializers survive change.** Add or reorder members of `rect` and `{ .w = 5 }` still means the same; `{ 0, 0, 4, 3, "a" }` silently changes meaning.
- **`{ 0 }` zeroes any struct or array**: the idiomatic "empty" value.
- **`x = (T){ .a = 1 }` resets every other member to zero.** The project clears a whole message with `*m = (struct msg){ .prefix = NULL };`.
- **Compound literals pass a struct without a named variable**, as the project often does: `send_msg(c, &(struct msg){ .command = "PONG", ... })`.

Should you `typedef` your structs? Both styles are common. The Linux kernel style avoids it: `struct conn *c` says "this is a struct", `conn_t *c` hides it. Also, POSIX reserves names ending in `_t`. This course writes `struct name` and keeps `typedef` for function types, where it really helps readability.

### Gotchas

`-Wextra` warns about a too-short positional initializer, but not about designated initializers or `{ 0 }`, where the zeroes are obviously intended:

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -c mf.c
mf.c: In function ‘main’:
mf.c:4:12: warning: missing initializer for field ‘h’ of ‘struct rect’ [-Wmissing-field-initializers]
    4 |     struct rect a = { 0, 0, 4 };
      |            ^~~~
mf.c:1:28: note: ‘h’ declared here
    1 | struct rect { int x, y, w, h; };
      |                            ^
```

A compound literal inside a function is a local (automatic) object: it dies when its block ends, so never return its address. gcc only finds this with optimization on (`-O2`):

```console
$ gcc -O2 -std=c99 -Wall -Wextra -pedantic -c e5.c
e5.c: In function ‘origin’:
e5.c:4:12: warning: function returns address of local variable [-Wreturn-local-addr]
    4 |     return &(struct point){ 0, 0 };
      |            ^~~~~~~~~~~~~~~~~~~~~~~
e5.c:4:27: note: declared here
    4 |     return &(struct point){ 0, 0 };
      |                           ^
```

## Struct layout: padding, alignment and offsetof

The CPU reads an `int` fastest (on some machines, only) at an address that is a multiple of 4; a `double` wants a multiple of 8. That is the type's **alignment**. To meet it, the compiler inserts unused **padding** bytes between members and at the end of a struct. C guarantees:

- Members are stored in declaration order.
- The first member is at offset 0 (no padding before it).
- `sizeof` includes the padding, so every element of an array of structs is aligned.

`offsetof(type, member)` from `<stddef.h>` gives a member's byte offset.

`examples/ch07/layout.c`
```c
/* layout.c - padding, alignment, sizeof and offsetof. */
#include <stddef.h>   /* offsetof */
#include <stdint.h>
#include <stdio.h>

struct loose {        /* members in a careless order */
    char tag;
    double value;
    char flag;
    int count;
};

struct tight {        /* the same members, largest first */
    double value;
    int count;
    char tag;
    char flag;
};

struct header {       /* looks like 7 bytes of data... */
    uint8_t type;
    uint16_t length;
    uint32_t id;
};

#define SHOW(type, member) \
    printf("  %-8s offset %2zu, size %zu\n", #member, \
           offsetof(type, member), sizeof(((type *)0)->member))

int main(void)
{
    printf("struct loose: sizeof %zu\n", sizeof(struct loose));
    SHOW(struct loose, tag);
    SHOW(struct loose, value);
    SHOW(struct loose, flag);
    SHOW(struct loose, count);

    printf("struct tight: sizeof %zu\n", sizeof(struct tight));
    SHOW(struct tight, value);
    SHOW(struct tight, count);
    SHOW(struct tight, tag);
    SHOW(struct tight, flag);

    printf("struct header: sizeof %zu\n", sizeof(struct header));
    SHOW(struct header, type);
    SHOW(struct header, length);
    SHOW(struct header, id);

    struct loose arr[2];
    printf("array of 2 loose: %zu bytes, element 1 starts %td bytes in\n",
           sizeof arr, (char *)&arr[1] - (char *)&arr[0]);
    return 0;
}
```

`SHOW` is a function-like macro that saves typing the same `printf` twelve times; [Chapter 9](09-preprocessor-and-building.md) explains macros, including `#member`, which turns a name into a string. `sizeof(((type *)0)->member)` is safe: `sizeof` only looks at the type and never evaluates its operand.

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o layout layout.c
$ ./layout
struct loose: sizeof 24
  tag      offset  0, size 1
  value    offset  8, size 8
  flag     offset 16, size 1
  count    offset 20, size 4
struct tight: sizeof 16
  value    offset  0, size 8
  count    offset  8, size 4
  tag      offset 12, size 1
  flag     offset 13, size 1
struct header: sizeof 8
  type     offset  0, size 1
  length   offset  2, size 2
  id       offset  4, size 4
array of 2 loose: 48 bytes, element 1 starts 24 bytes in
```

Here is `struct loose` byte by byte (`.` = padding):

```text
offset  0         8                16      20      24
        T . . . . . . . V V V V V V V V F . . . C C C C
        tag  (7 pad)    value           flag    count
```

That is 14 bytes of data in 24. Sorting members from largest to smallest alignment gets it down to 16. Do that for structs you keep thousands of; for a struct you have one of, choose the order that reads best.

### glibc vs musl: struct layout is the same

The musl build prints byte-for-byte the same output:

```console
$ musl-gcc -std=c99 -Wall -Wextra -pedantic -static -o layout-musl layout.c
$ ./layout > glibc.txt; ./layout-musl > musl.txt; diff glibc.txt musl.txt && echo same
same
```

Layout is set by the **ABI** (application binary interface), not the C library. The ABI is the platform's rulebook for type sizes, alignment and calling conventions; on x86-64 Linux it is the System V x86-64 psABI, and gcc follows it whichever libc you link. That is why a library compiled once can be called from any program on the platform.

The libcs *do* differ in what they let you see. glibc's `<stdio.h>` defines `FILE` in full. musl's public header gives `FILE` a one-byte dummy body and keeps the real definition inside the library:

`examples/ch07/file-peek.c`
```c
/* file-peek.c - is FILE an open struct or an opaque one? It depends on the
 * libc: this compiles with glibc and fails, on purpose, with musl. */
#include <stdio.h>

int main(void)
{
    printf("sizeof(FILE) = %zu\n", sizeof(FILE));
    printf("fd of stdout = %d\n", stdout->_fileno);
    return 0;
}
```

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o file-peek file-peek.c && ./file-peek
sizeof(FILE) = 216
fd of stdout = 1
$ musl-gcc -std=c99 -Wall -Wextra -pedantic -static -o file-peek-musl file-peek.c
file-peek.c: In function ‘main’:
file-peek.c:8:41: error: ‘FILE’ has no member named ‘_fileno’
    8 |     printf("fd of stdout = %d\n", stdout->_fileno);
      |                                         ^~
```

musl is better here: code that pokes at `FILE`'s insides works on only one libc, so musl refuses to compile it. The portable way to get the descriptor is `fileno(stdout)` (POSIX, [Chapter 12](12-files-and-descriptors.md)). This is the **opaque type** idea, which you build yourself later in this chapter.

### Gotchas: why you never `memcmp` or `fwrite` a whole struct

Padding bytes hold whatever was in memory before, so two structs with equal members can have different bytes:

`examples/ch07/padcmp.c`
```c
/* padcmp.c - why you compare structs member by member, not with memcmp. */
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

struct item {
    char kind;      /* 1 byte, then 3 bytes of padding */
    int count;
};

static bool item_equal(const struct item *a, const struct item *b)
{
    return a->kind == b->kind && a->count == b->count;
}

int main(void)
{
    struct item a, b;
    memset(&a, 0xAA, sizeof a);     /* fill the padding with different junk */
    memset(&b, 0x00, sizeof b);
    a.kind = b.kind = 'x';
    a.count = b.count = 7;

    printf("item_equal: %s\n", item_equal(&a, &b) ? "equal" : "different");
    printf("memcmp:     %s\n", memcmp(&a, &b, sizeof a) == 0 ? "equal" : "different");

    const unsigned char *p = (const unsigned char *)&a;
    printf("bytes of a:");
    for (size_t i = 0; i < sizeof a; i++)
        printf(" %02x", p[i]);
    putchar('\n');
    return 0;
}
```

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o padcmp padcmp.c
$ ./padcmp
item_equal: equal
memcmp:     different
bytes of a: 78 aa aa aa 07 00 00 00
```

The padding bytes (`aa aa aa`) are unspecified, so the standard does not promise this output; it is what gcc produces on both libcs.

For the same reasons, never write a struct straight to a file or socket with `fwrite(&s, sizeof s, 1, f)`:

- The padding leaks junk bytes, maybe even old secrets.
- Integers are in the CPU's byte order (little-endian on x86-64), not the format's.
- Another compiler, CPU or version of your program may lay the struct out differently.

Network protocols define their bytes exactly, and you read and write them one at a time. parley's protocol is text, so it avoids the problem. Binary formats (DNS, TLS, ELF files, images) need the next section.

## Bytes on the wire: writing integers portably

To store a 32-bit number in a byte format, choose a byte order (**endianness**) and write the bytes with shifts. *Big-endian* puts the most significant byte first; it is "network byte order", used by most Internet protocols. *Little-endian* puts the least significant byte first; x86-64 and most ARM machines use it in memory.

`examples/ch07/byteio.c`
```c
/* byteio.c - turn integers into bytes and back, with a fixed byte order.
 * Never cast a byte pointer to (uint32_t *): the bytes may be misaligned,
 * the byte order depends on the CPU, and it breaks C's aliasing rules. */
#include <stdint.h>
#include <stdio.h>

static void put_u16_be(unsigned char *p, uint16_t v)
{
    p[0] = (unsigned char)(v >> 8);
    p[1] = (unsigned char)v;
}

static uint16_t get_u16_be(const unsigned char *p)
{
    return (uint16_t)(p[0] << 8 | p[1]);
}

static void put_u32_be(unsigned char *p, uint32_t v)
{
    p[0] = (unsigned char)(v >> 24);
    p[1] = (unsigned char)(v >> 16);
    p[2] = (unsigned char)(v >> 8);
    p[3] = (unsigned char)v;
}

static uint32_t get_u32_be(const unsigned char *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 |
           (uint32_t)p[2] << 8 | (uint32_t)p[3];
}

static uint32_t get_u32_le(const unsigned char *p)
{
    return (uint32_t)p[3] << 24 | (uint32_t)p[2] << 16 |
           (uint32_t)p[1] << 8 | (uint32_t)p[0];
}

static void dump(const char *what, const unsigned char *p, size_t n)
{
    printf("%-22s", what);
    for (size_t i = 0; i < n; i++)
        printf(" %02x", p[i]);
    putchar('\n');
}

int main(void)
{
    unsigned char buf[6];
    put_u16_be(buf, 0x1234);
    put_u32_be(buf + 2, 0xdeadbeef);   /* buf + 2 is not 4-byte aligned: fine */
    dump("big-endian on the wire:", buf, sizeof buf);

    printf("get_u16_be: 0x%04x\n", (unsigned)get_u16_be(buf));
    printf("get_u32_be: 0x%08lx\n", (unsigned long)get_u32_be(buf + 2));
    printf("get_u32_le: 0x%08lx (same bytes, other order)\n",
           (unsigned long)get_u32_le(buf + 2));

    uint32_t native = 0xdeadbeef;
    dump("0xdeadbeef in memory:", (const unsigned char *)&native, sizeof native);
    return 0;
}
```

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o byteio byteio.c
$ ./byteio
big-endian on the wire: 12 34 de ad be ef
get_u16_be: 0x1234
get_u32_be: 0xdeadbeef
get_u32_le: 0xefbeadde (same bytes, other order)
0xdeadbeef in memory:  ef be ad de
```

The last line shows x86-64 storing the low byte first. The shift functions do arithmetic on values, never on memory layout, so they work the same on every CPU. About the casts:

- `(uint32_t)p[0] << 24`: without the cast, `p[0]` is promoted to `int`, and shifting a byte of `0x80` or more into an `int`'s sign bit is undefined behaviour. Casting first makes the shift unsigned.
- `*(uint32_t *)(buf + 2)` is the tempting shortcut, and wrong three times over: the address may be misaligned (some CPUs trap), the result depends on the CPU's byte order, and it breaks strict aliasing ([Chapter 5](05-pointers-and-arrays.md)), so the optimizer may miscompile it.

POSIX's `htonl`/`ntohl` ("host to network long") do the same job; you meet them with sockets in [Chapter 14](14-networking-basics.md).

## Flexible array members

A struct's last member may be an array with no size: `char text[];`. This **flexible array member** (C99) lets you allocate a header and variable-length data in one block:

`examples/ch07/fam.c`
```c
/* fam.c - a flexible array member: a struct whose last member is an array
 * with no size. You allocate the header and the array in one block. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct name {
    size_t len;
    char text[];        /* flexible array member: must be last */
};

static struct name *name_new(const char *s)
{
    size_t len = strlen(s);
    struct name *n = malloc(sizeof *n + len + 1);   /* header + chars + '\0' */
    if (n == NULL)
        return NULL;
    n->len = len;
    memcpy(n->text, s, len + 1);
    return n;
}

int main(void)
{
    struct name *n = name_new("alice");
    if (n == NULL) {
        perror("malloc");
        return 1;
    }
    printf("sizeof(struct name) = %zu\n", sizeof(struct name));
    printf("%s has %zu letters\n", n->text, n->len);
    free(n);
    return 0;
}
```

`malloc` reserves memory at run time and `free` gives it back ([Chapter 8](08-memory.md)). Read `malloc(sizeof *n + len + 1)` as "room for the header plus `len + 1` chars".

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o fam fam.c
$ ./fam
sizeof(struct name) = 8
alice has 5 letters
```

`sizeof` counts only the header. One allocation instead of two means one `free`, one cache-friendly block and one pointer fewer. Rules:

- The flexible member must be last, and the struct needs at least one other member.
- You cannot make an array of such structs (or put one inside another struct).
- `=` compiles but copies only the header, not the array. Copy with `memcpy` and the real size.

Old code fakes this with `char text[1]` or the GNU `char text[0]`; in C99, use `[]`.

## Unions

A **union** looks like a struct, but all its members start at offset 0 and share the same bytes. Its size is that of its largest member (plus padding). You store one member at a time. Two uses:

1. **A value that can be one of several types**, plus a separate tag saying which. This *tagged union* is C's version of a variant or sum type.
2. **Reading the bytes of one type as another** ("type punning").

`examples/ch07/union.c`
```c
/* union.c - unions, a tagged union, and reading a float's bits. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* A tagged union: an enum says which union member is valid. */
enum value_kind { V_INT, V_DOUBLE, V_TEXT };

struct value {
    enum value_kind kind;
    union {             /* C99 needs a name for this member: "as" */
        long i;
        double d;
        const char *s;
    } as;
};

static void print_value(const struct value *v)
{
    switch (v->kind) {
    case V_INT:
        printf("int    %ld\n", v->as.i);
        break;
    case V_DOUBLE:
        printf("double %g\n", v->as.d);
        break;
    case V_TEXT:
        printf("text   \"%s\"\n", v->as.s);
        break;
    }
}

int main(void)
{
    struct value vals[] = {
        { V_INT, { .i = 42 } },
        { V_DOUBLE, { .d = 2.5 } },
        { V_TEXT, { .s = "hello" } },
    };
    for (size_t i = 0; i < sizeof vals / sizeof vals[0]; i++)
        print_value(&vals[i]);
    printf("sizeof(struct value) = %zu (enum 4 + pad 4 + biggest member 8)\n",
           sizeof(struct value));

    /* All members share the same bytes. Writing one and reading another
     * reinterprets the bytes ("type punning"). */
    union {
        float f;
        uint32_t u;
    } pun;
    pun.f = 1.0f;
    printf("1.0f as bits: 0x%08lx\n", (unsigned long)pun.u);

    /* memcpy does the same job and also works without a union. */
    float g = -2.0f;
    uint32_t bits;
    memcpy(&bits, &g, sizeof bits);
    printf("-2.0f as bits: 0x%08lx\n", (unsigned long)bits);

    /* Which byte comes first in memory? */
    union {
        uint32_t u;
        unsigned char b[4];
    } order = { 0x01020304 };
    printf("0x01020304 starts with byte %02x: %s-endian\n",
           order.b[0], order.b[0] == 0x04 ? "little" : "big");
    return 0;
}
```

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o union union.c
$ ./union
int    42
double 2.5
text   "hello"
sizeof(struct value) = 16 (enum 4 + pad 4 + biggest member 8)
1.0f as bits: 0x3f800000
-2.0f as bits: 0xc0000000
0x01020304 starts with byte 04: little-endian
```

Notice:

- The `switch` over `v->kind` has no `default`, so `-Wall` warns if you add a `V_BOOL` and forget its `case` (see Enums below). Tag plus `switch` makes the tagged union safe in practice.
- A union initializer sets the *first* member unless you use a designator: `{ .d = 2.5 }`.
- `0x3f800000` is IEEE 754 for 1.0: sign 0, exponent `0x7f` (127, meaning 2⁰), fraction 0.
- C99 (as corrected by TC3) lets you read a different union member than the one last written; the bytes are reinterpreted. `memcpy` is the other legal way, for any two objects of the same size. A pointer cast (`*(uint32_t *)&f`) is *not* legal: it breaks strict aliasing.

### Gotcha: anonymous unions are C11

Many codebases leave the union unnamed so `v.i` works instead of `v.as.i`. That is C11, and `-pedantic` says so:

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -c e7.c
e7.c:6:6: warning: ISO C99 doesn’t support unnamed structs/unions [-Wpedantic]
    6 |     };
      |      ^
```

In C99, name the member. `as` reads well: `v->as.d`.

## Bit-fields, briefly

A struct member can have a width in bits. Neighbouring bit-fields are packed into one unit:

`examples/ch07/bitfields.c`
```c
/* bitfields.c - packing small fields into one unsigned int. */
#include <stdio.h>

struct style {
    unsigned int bold : 1;      /* 1 bit: 0 or 1 */
    unsigned int underline : 1;
    unsigned int color : 4;     /* 4 bits: 0..15 */
};

int main(void)
{
    struct style s = { .bold = 1, .color = 9 };
    printf("bold=%u underline=%u color=%u sizeof=%zu\n",
           s.bold, s.underline, s.color, sizeof s);

    unsigned int big = 17;
    s.color = big;              /* too big: only the low 4 bits are kept */
    printf("after color = %u: color=%u\n", big, s.color);
    return 0;
}
```

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o bitfields bitfields.c
$ ./bitfields
bold=1 underline=0 color=9 sizeof=4
after color = 17: color=1
```

Use bit-fields sparingly, to save memory inside your program. Do **not** use them to match bits in a file or protocol: bit order, packing and whether a plain `int` bit-field is signed are all *implementation-defined* (each compiler and ABI decides). For wire formats, use shifts and masks as in `byteio.c`. Also, a bit-field has no address:

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -c e8.c
e8.c: In function ‘main’:
e8.c:8:23: error: cannot take address of bit-field ‘level’
    8 |     unsigned int *p = &f.level;
      |                       ^
```

When gcc sees an overflow at compile time, it warns:

```text
warning: unsigned conversion from ‘int’ to ‘unsigned char:4’ changes value from ‘17’ to ‘1’ [-Woverflow]
```

## Opaque types: hiding a struct's members

You can declare a struct without defining it: `struct counter;`. That is an **incomplete type**: code can hold `struct counter *` pointers, but cannot see members, create an instance or take `sizeof`. Put the declaration in a header and the definition in one `.c` file. Now only that file touches the members; everyone else goes through functions. This is C's encapsulation. musl's `FILE` uses the same idea (with a dummy body instead of none).

`examples/ch07/opaque/counter.h`
```c
/* counter.h - the public interface. Users see the name, not the members. */
#ifndef COUNTER_H
#define COUNTER_H

struct counter;                 /* incomplete type: members are private */

struct counter *counter_new(const char *name);
void counter_add(struct counter *c, int n);
int counter_value(const struct counter *c);
const char *counter_name(const struct counter *c);
void counter_free(struct counter *c);

#endif
```

`examples/ch07/opaque/counter.c`
```c
/* counter.c - the only file that knows what is inside struct counter. */
#include "counter.h"

#include <stdlib.h>
#include <string.h>

struct counter {
    char name[16];
    int value;
};

struct counter *counter_new(const char *name)
{
    struct counter *c = malloc(sizeof *c);
    if (c == NULL)
        return NULL;
    c->value = 0;
    strncpy(c->name, name, sizeof c->name - 1);
    c->name[sizeof c->name - 1] = '\0';
    return c;
}

void counter_add(struct counter *c, int n)
{
    c->value += n;
}

int counter_value(const struct counter *c)
{
    return c->value;
}

const char *counter_name(const struct counter *c)
{
    return c->name;
}

void counter_free(struct counter *c)
{
    free(c);
}
```

`examples/ch07/opaque/main.c`
```c
/* main.c - uses struct counter only through pointers and functions. */
#include <stdio.h>

#include "counter.h"

int main(void)
{
    struct counter *c = counter_new("lines");
    if (c == NULL)
        return 1;
    counter_add(c, 3);
    counter_add(c, 4);
    printf("%s = %d\n", counter_name(c), counter_value(c));
    /* c->value = 99;   would not compile: the type is incomplete here */
    counter_free(c);
    return 0;
}
```

The `#ifndef COUNTER_H` lines are an *include guard* (stops the header being read twice). `#include "counter.h"` with quotes searches the source file's own directory first. [Chapter 9](09-preprocessor-and-building.md) explains both, and Makefiles. This tiny `Makefile` runs the `gcc` command you would type by hand:

`examples/ch07/opaque/Makefile`
```make
CC = gcc
CFLAGS = -std=c99 -Wall -Wextra -pedantic

counter_demo: main.c counter.c counter.h
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ main.c counter.c

clean:
	rm -f counter_demo
```

```console
$ make
gcc -std=c99 -Wall -Wextra -pedantic  -o counter_demo main.c counter.c
$ ./counter_demo
lines = 7
$ make clean && make CC=musl-gcc LDFLAGS=-static
rm -f counter_demo
musl-gcc -std=c99 -Wall -Wextra -pedantic -static -o counter_demo main.c counter.c
$ ./counter_demo
lines = 7
```

Uncomment the `c->value = 99;` line and `main.c` no longer compiles:

```console
$ make
gcc -std=c99 -Wall -Wextra -pedantic  -o counter_demo main.c counter.c
main.c: In function ‘main’:
main.c:14:6: error: invalid use of undefined type ‘struct counter’
   14 |     c->value = 99;
      |      ^~
make: *** [Makefile:5: counter_demo] Error 1
```

Declaring a `struct counter` variable or taking `sizeof(struct counter)` outside `counter.c` fails too: `storage size of ‘c’ isn’t known` and `invalid application of ‘sizeof’ to incomplete type ‘struct counter’`.

The price: one heap allocation per object and a function call per access. The gain: you can change the members without recompiling or breaking callers, and callers cannot break your invariants. Use opaque types for library-style modules, plain structs for simple data such as `struct msg`.

## Enums

An **enum** defines named `int` constants:

`examples/ch07/enums.c`
```c
/* enums.c - named integer constants. */
#include <stdio.h>

enum level { LOG_ERROR, LOG_WARN, LOG_INFO, LOG_DEBUG, LOG_NLEVELS };

enum numeric {          /* explicit values, like IRC reply numbers */
    RPL_WELCOME = 1,
    ERR_NOSUCHNICK = 401,
    ERR_UNKNOWNCOMMAND = 421,
};

enum { BUF_SIZE = 64 }; /* an anonymous enum: just a compile-time int */

static const char *const level_names[LOG_NLEVELS] = {
    [LOG_ERROR] = "error",
    [LOG_WARN] = "warn",
    [LOG_INFO] = "info",
    [LOG_DEBUG] = "debug",
};

static const char *numeric_text(enum numeric n)
{
    switch (n) {        /* no default: -Wall warns if you forget a value */
    case RPL_WELCOME:
        return "welcome";
    case ERR_NOSUCHNICK:
        return "no such nick";
    case ERR_UNKNOWNCOMMAND:
        return "unknown command";
    }
    return "?";
}

int main(void)
{
    char buf[BUF_SIZE];         /* an enum constant can size an array */
    for (enum level l = LOG_ERROR; l < LOG_NLEVELS; l++)
        printf("%d=%s ", (int)l, level_names[l]);
    putchar('\n');

    snprintf(buf, sizeof buf, "%03d %s", ERR_UNKNOWNCOMMAND,
             numeric_text(ERR_UNKNOWNCOMMAND));
    printf("%s\n", buf);

    /* Enums are just ints. C does not stop this: */
    enum level odd = 42;
    printf("odd = %d, sizeof(enum level) = %zu\n", (int)odd, sizeof odd);
    return 0;
}
```

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o enums enums.c
$ ./enums
0=error 1=warn 2=info 3=debug 
421 unknown command
odd = 42, sizeof(enum level) = 4
```

What to know:

- Values start at 0 and count up by one unless you set one. After `= 401` comes 402.
- A trailing `LOG_NLEVELS` (or `NCOLORS`) counts the values for free. Use it to size tables.
- `enum { BUF_SIZE = 64 };` is the C99 way to make an integer constant that can size an array. A `const int` is not a constant expression in C (it is in C++): `char buf[n]` with it is an error at file scope and a VLA inside a function. `#define` also works ([Chapter 9](09-preprocessor-and-building.md)), but an enum constant is a real, scoped name the debugger knows.
- An enum constant has type `int`; an enum *variable* has an integer type the compiler picks (4 bytes here). C does **no** checking: `enum level odd = 42;` compiles silently. Enums document intent; `-Wswitch` is the one safety net.

`-Wswitch` (part of `-Wall`) warns when a `switch` on an enum misses a value, as long as it has **no `default`**:

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -c e9.c
e9.c: In function ‘name’:
e9.c:4:5: warning: enumeration value ‘BLUE’ not handled in switch [-Wswitch]
    4 |     switch (c) {
      |     ^~~~~~
```

So this course's switches over enums leave out `default` and put `return "?";` after the switch. The compiler then reminds you to update every switch when you add a value.

## Building parley: the protocol as data

So far the server split a line at the first space and glued replies together with `printf` formats. That breaks once a message has more than one parameter. This milestone turns the protocol into data:

- `struct msg`: one parsed line. `msg_parse` fills it in; `msg_format` turns it back into a line.
- `struct conn`: all the server knows about the client. The globals `client_nick` and `client_quit` go away.
- `struct config` in both programs, replacing the `opt_*` globals.
- A **command table**: an array of `struct command { name, handler, needs_nick }`, with one small handler per command.
- New server commands: `ME`, `NAMES` (numerics 353 and 366), `PING`/`PONG` (461 if the token is missing), plus errors 412 (no text) and 400 (malformed line).

### The message format

A parley line looks like this (full grammar in [Appendix C](appendix-c-parley-protocol.md)):

```text
[":" prefix " "] command {" " param} [" :" trailing]

:alice MSG :hello there        prefix "alice", command "MSG", trailing "hello there"
:parley 432 * bad! :Erroneous nickname
PING :123
```

A last parameter that starts with `:` runs to the end of the line and may contain spaces: the *trailing* parameter.

### parleyd.c: configuration

Add `MSG_PARAMS_MAX` to the constants and a `#define` for the server name, then replace the four `opt_*` globals (and their comment) with a struct:

`project/ch07/parleyd.c` (top of file)
```c
enum {
    DEFAULT_PORT = 6667,
    PORT_MAX = 65535,
    DEFAULT_MAX_CLIENTS = 64,
    MAX_CLIENTS_MAX = 1000,
    MSG_LINE_MAX = 512,   /* longest protocol line, including "\r\n" */
    MSG_PARAMS_MAX = 8,   /* most middle parameters in one message */
    NICK_MAX = 16         /* longest nickname */
};

#define SERVER_NAME "parley"   /* the prefix of server messages (#define: Chapter 9) */

enum log_level { LOG_DEBUG, LOG_INFO, LOG_WARN, LOG_ERROR };

/* Settings from the command line, with their defaults. */
struct config {
    const char *addr;
    long port;
    long max_clients;
    bool verbose;
};

/* Set from config.verbose in main; log_msg prints debug lines only if true. */
static bool log_verbose = false;
```

`SERVER_NAME` is a `#define` because it is a string, and an enum holds only integers ([Chapter 9](09-preprocessor-and-building.md) covers `#define`).

`log_msg` now tests `log_verbose` instead of `opt_verbose`:

```c
    if (level == LOG_DEBUG && !log_verbose)
        return;
```

`parse_args` fills in the caller's `struct config` instead of globals:

```c
static void parse_args(int argc, char *argv[], struct config *cfg)
{
    /* ... the loop is unchanged until the switch ... */
        switch (opt) {
        case 'b':
            cfg->addr = value;
            break;
        case 'p':
            if (!str_to_long(value, 1, PORT_MAX, &cfg->port))
                usage_error("invalid port '%s' (expected 1-%d)", value, PORT_MAX);
            break;
        case 'm':
            if (!str_to_long(value, 1, MAX_CLIENTS_MAX, &cfg->max_clients))
                usage_error("invalid client limit '%s' (expected 1-%d)",
                            value, MAX_CLIENTS_MAX);
            break;
        case 'v':
            cfg->verbose = true;
            break;
        default:
            usage_error("unknown option '%s'", arg);
        }
    }
}
```

`&cfg->port` means `&(cfg->port)`, the address of a member, because `->` binds tighter than `&`.

### parleyd.c: struct msg, msg_parse and msg_format

Add this new section after `parse_args`:

`project/ch07/parleyd.c` (new section)
```c
/* ---- protocol messages --------------------------------------------------- */

/* One protocol line, split into its parts. All pointers point into the
 * line that msg_parse() was given, so the line must outlive the msg. */
struct msg {
    const char *prefix;                   /* who sent it; NULL if absent */
    const char *command;                  /* "NICK", "001", ... */
    int nparams;                          /* how many middle parameters */
    const char *params[MSG_PARAMS_MAX];   /* the middle parameters */
    const char *trailing;                 /* text after " :"; NULL if absent */
};

enum msg_status {
    MSG_OK,
    MSG_EMPTY,             /* blank line: not an error, just ignore it */
    MSG_NO_COMMAND,        /* a prefix with nothing after it */
    MSG_TOO_MANY_PARAMS
};

/* End the word at p (overwrite the first space with '\0') and return a
 * pointer to the start of the next word, skipping extra spaces. */
static char *cut_word(char *p)
{
    while (*p != '\0' && *p != ' ')
        p++;
    while (*p == ' ')
        *p++ = '\0';
    return p;
}

/* Parse line (no "\r\n") into *m:
 *     [":" prefix " "] command {" " param} [" :" trailing]
 * The line is modified in place: spaces become '\0' terminators. */
static enum msg_status msg_parse(char *line, struct msg *m)
{
    *m = (struct msg){ .prefix = NULL };   /* compound literal: zero it all */

    char *p = line;
    while (*p == ' ')
        p++;
    if (*p == '\0')
        return MSG_EMPTY;

    if (*p == ':') {
        m->prefix = p + 1;
        p = cut_word(p);
        if (m->prefix[0] == '\0' || *p == '\0')
            return MSG_NO_COMMAND;
    }

    m->command = p;
    p = cut_word(p);

    while (*p != '\0') {
        if (*p == ':') {             /* the trailing parameter: rest of line */
            m->trailing = p + 1;
            break;
        }
        if (m->nparams == MSG_PARAMS_MAX)
            return MSG_TOO_MANY_PARAMS;
        m->params[m->nparams++] = p;
        p = cut_word(p);
    }
    return MSG_OK;
}

/* Parameter i, counting the trailing one as the last: for "NICK bob" and
 * "NICK :bob" msg_param(m, 0) is "bob". NULL if there is no such param. */
static const char *msg_param(const struct msg *m, int i)
{
    if (i < m->nparams)
        return m->params[i];
    if (i == m->nparams)
        return m->trailing;
    return NULL;
}
```

The parser copies nothing. It writes `'\0'` over the spaces, so each word becomes a C string inside the original buffer, and the struct points into it. For `:bob PING x :hi there`:

```text
before:  : b o b ␠ P I N G ␠ x ␠ : h i ␠ t h e r e \0
after:   : b o b \0 P I N G \0 x \0 : h i ␠ t h e r e \0
           ^prefix  ^command   ^params[0] ^trailing
```

A common C design: no allocation, no copying, no out-of-memory failure. The cost, stated in the comment above `struct msg`: the `msg` borrows the line, so the line must live at least as long as the `msg`.

`msg_parse` returns an `enum msg_status`, not `bool`, so the caller knows *why* parsing failed and can ignore blank lines instead of treating them as errors.

The formatter is the reverse. Put it after `msg_param`. Its helper `append` never writes past the buffer:

```c
/* Append text to out (which holds *len bytes, room for size), stopping at
 * size - 1. Returns false if text did not fit completely. */
static bool append(char *out, size_t size, size_t *len, const char *text)
{
    size_t n = strlen(text);
    size_t room = size - 1 - *len;
    bool fits = n <= room;
    if (!fits)
        n = room;
    memcpy(out + *len, text, n);
    *len += n;
    out[*len] = '\0';
    return fits;
}

/* Format *m as a protocol line ending in "\r\n" into out[size], where
 * size >= MSG_LINE_MAX + 1. A trailing parameter that is too long is cut
 * short so the line fits in MSG_LINE_MAX bytes. Returns the line length,
 * or -1 if even the parts before the trailing text do not fit. */
static int msg_format(char *out, size_t size, const struct msg *m)
{
    if (size > MSG_LINE_MAX - 2 + 1)
        size = MSG_LINE_MAX - 2 + 1;     /* leave room for "\r\n" */
    size_t len = 0;
    bool ok = true;

    out[0] = '\0';
    if (m->prefix != NULL) {
        ok = ok && append(out, size, &len, ":");
        ok = ok && append(out, size, &len, m->prefix);
        ok = ok && append(out, size, &len, " ");
    }
    ok = ok && append(out, size, &len, m->command);
    for (int i = 0; i < m->nparams; i++) {
        ok = ok && append(out, size, &len, " ");
        ok = ok && append(out, size, &len, m->params[i]);
    }
    if (m->trailing != NULL) {
        ok = ok && append(out, size, &len, " :");
        if (ok)
            append(out, size, &len, m->trailing);   /* may be cut short */
    }
    if (!ok)
        return -1;
    memcpy(out + len, "\r\n", 3);     /* the 3 copies the '\0' too */
    return (int)len + 2;
}
```

`ok = ok && append(...)` uses `&&` short-circuiting: after one piece fails, later `append` calls are skipped. Too-long trailing text (a huge chat message) is cut to fit; losing the end of a chat message is acceptable. A command or parameter that does not fit makes the whole message invalid (`-1`): a cut-off nickname would be wrong, not just short.

### parleyd.c: struct conn and replies

Replace the "session" section (`client_nick`, `client_quit`, `target()`, `reply()`, `handle_nick()`, `handle_line()`) with what follows. First the per-client state and the two send functions:

`project/ch07/parleyd.c` (replaces the session section)
```c
/* ---- the session with our one client ------------------------------------- */

/* Everything the server knows about a connected client. */
struct conn {
    char nick[NICK_MAX + 1];   /* "" until the client registers */
    bool quitting;             /* sent QUIT: stop reading */
};

/* The name to put in replies: the nick, or "*" before registration. */
static const char *target(const struct conn *c)
{
    return c->nick[0] != '\0' ? c->nick : "*";
}

/* Format a message and send it to the client (for now: to stdout). */
static void send_msg(struct conn *c, const struct msg *m)
{
    char line[MSG_LINE_MAX + 1];
    (void)c;   /* one client on stdout for now; later each conn has a socket */
    if (msg_format(line, sizeof line, m) < 0) {
        log_msg(LOG_WARN, "message too long to send: %s", m->command);
        return;
    }
    fputs(line, stdout);
}

/* Send ":parley CODE <target> [param] :text", the server's reply format. */
static void send_numeric(struct conn *c, const char *code, const char *param,
                         const char *text)
{
    struct msg m = {
        .prefix = SERVER_NAME,
        .command = code,
        .nparams = 1,
        .params = { target(c), param },
        .trailing = text,
    };
    if (param != NULL)
        m.nparams = 2;
    send_msg(c, &m);
}
```

Why a struct for two fields when globals worked? In [Chapter 17](17-threads.md) the server has many clients at once. Each gets its own `struct conn`, and every function already takes the `conn` it works on, so nothing needs rewriting.

`.params = { target(c), param }` is a nested designated initializer: it sets `params[0]` and `params[1]` and zeroes the rest.

### parleyd.c: one handler per command

Every handler has the same signature, `(struct conn *c, const struct msg *m)`, so they all fit in one table:

```c
static void cmd_nick(struct conn *c, const struct msg *m)
{
    const char *nick = msg_param(m, 0);
    if (nick == NULL || nick[0] == '\0') {
        send_numeric(c, "431", NULL, "No nickname given");
        return;
    }
    if (!nick_valid(nick)) {
        send_numeric(c, "432", nick, "Erroneous nickname");
        return;
    }
    if (c->nick[0] == '\0') {                       /* registration */
        char welcome[64];
        snprintf(welcome, sizeof welcome, "Welcome to parley, %s", nick);
        str_copy(c->nick, nick, sizeof c->nick);
        send_numeric(c, "001", NULL, welcome);
        send_msg(c, &(struct msg){ .prefix = c->nick, .command = "JOIN" });
        send_numeric(c, "353", NULL, c->nick);      /* who is here: just you */
        send_numeric(c, "366", NULL, "End of NAMES");
        log_msg(LOG_INFO, "%s joined", c->nick);
    } else if (strcmp(c->nick, nick) != 0) {        /* rename */
        send_msg(c, &(struct msg){
            .prefix = c->nick, .command = "NICK", .nparams = 1, .params = { nick } });
        log_msg(LOG_INFO, "%s is now %s", c->nick, nick);
        str_copy(c->nick, nick, sizeof c->nick);
    }
}

/* MSG and ME: send the text back with the sender as prefix. */
static void say(struct conn *c, const struct msg *m, const char *command)
{
    const char *text = msg_param(m, 0);
    if (text == NULL || text[0] == '\0') {
        send_numeric(c, "412", NULL, "No text to send");
        return;
    }
    send_msg(c, &(struct msg){ .prefix = c->nick, .command = command, .trailing = text });
}

static void cmd_msg(struct conn *c, const struct msg *m)
{
    say(c, m, "MSG");
}

static void cmd_me(struct conn *c, const struct msg *m)
{
    say(c, m, "ME");
}

static void cmd_names(struct conn *c, const struct msg *m)
{
    (void)m;
    send_numeric(c, "353", NULL, c->nick);
    send_numeric(c, "366", NULL, "End of NAMES");
}

static void cmd_ping(struct conn *c, const struct msg *m)
{
    const char *token = msg_param(m, 0);
    if (token == NULL) {
        send_numeric(c, "461", "PING", "Not enough parameters");
        return;
    }
    send_msg(c, &(struct msg){ .prefix = SERVER_NAME, .command = "PONG", .trailing = token });
}

static void cmd_pong(struct conn *c, const struct msg *m)
{
    (void)c;   /* a reply to our PING: nothing to do (yet) */
    (void)m;
}

static void cmd_quit(struct conn *c, const struct msg *m)
{
    const char *reason = msg_param(m, 0);
    log_msg(LOG_INFO, "%s left (%s)", target(c), reason != NULL ? reason : "Quit");
    send_msg(c, &(struct msg){ .command = "ERROR", .trailing = "Goodbye" });
    c->quitting = true;
}
```

`send_msg(c, &(struct msg){ .prefix = c->nick, .command = "JOIN" })` is longer than the old `reply(":%s JOIN", client_nick)`, but you can no longer forget a space or colon, and every reply goes through one formatter that enforces the 512-byte limit. A handler that ignores a parameter writes `(void)m;` ("unused on purpose"), which silences `-Wunused-parameter`.

### parleyd.c: the command table and dispatch

```c
/* The command table. Each entry ties a command name to its handler. */
typedef void command_fn(struct conn *c, const struct msg *m);

struct command {
    const char *name;
    command_fn *handler;
    bool needs_nick;        /* only allowed after registration */
};

static const struct command commands[] = {
    { .name = "NICK",  .handler = cmd_nick,  .needs_nick = false },
    { .name = "MSG",   .handler = cmd_msg,   .needs_nick = true  },
    { .name = "ME",    .handler = cmd_me,    .needs_nick = true  },
    { .name = "NAMES", .handler = cmd_names, .needs_nick = true  },
    { .name = "PING",  .handler = cmd_ping,  .needs_nick = false },
    { .name = "PONG",  .handler = cmd_pong,  .needs_nick = false },
    { .name = "QUIT",  .handler = cmd_quit,  .needs_nick = false },
};
enum { NCOMMANDS = sizeof commands / sizeof commands[0] };

static void handle_line(struct conn *c, char *line)
{
    struct msg m;
    enum msg_status status = msg_parse(line, &m);
    if (status == MSG_EMPTY)
        return;
    if (status != MSG_OK) {
        send_numeric(c, "400", NULL, "Malformed message");
        return;
    }

    for (int i = 0; i < NCOMMANDS; i++) {
        const struct command *cmd = &commands[i];
        if (!str_ieq(m.command, cmd->name))
            continue;
        if (cmd->needs_nick && c->nick[0] == '\0')
            send_numeric(c, "451", NULL, "You have not registered");
        else
            cmd->handler(c, &m);
        return;
    }
    send_numeric(c, "421", m.command, "Unknown command");
}
```

`typedef void command_fn(struct conn *c, const struct msg *m);` names a *function type*. `command_fn *handler` then reads "pointer to a command function", far easier than `void (*handler)(struct conn *, const struct msg *)`, and the handler signature is documented in one place.

Policy lives in the table too: the `needs_nick` column replaces the old `if (client_nick[0] == '\0')` chain. A new command is one function plus one row.

`enum { NCOMMANDS = sizeof commands / sizeof commands[0] };` counts the rows at compile time. Being an enum, it is an `int`, so an `int` loop index gives no signed/unsigned comparison warning.

### parleyd.c: main

```c
int main(int argc, char *argv[])
{
    struct config cfg = {
        .addr = "127.0.0.1",
        .port = DEFAULT_PORT,
        .max_clients = DEFAULT_MAX_CLIENTS,
        .verbose = false,
    };
    parse_args(argc, argv, &cfg);
    log_verbose = cfg.verbose;
    log_msg(LOG_DEBUG, "verbose logging is on");
    log_msg(LOG_INFO, "reading one client session from stdin");

    struct conn client = { .quitting = false };   /* nick[] starts as "" */
    char line[MSG_LINE_MAX + 1];
    while (!client.quitting && fgets(line, sizeof line, stdin) != NULL) {
        if (strchr(line, '\n') == NULL && !feof(stdin)) {
            int c;
            while ((c = getchar()) != EOF && c != '\n')
                ;
            send_numeric(&client, "417", NULL, "Input line too long");
            continue;
        }
        str_chomp(line);
        handle_line(&client, line);
    }
    log_msg(LOG_INFO, "session over");
    return 0;
}
```

The defaults sit in one designated initializer, where they are used. `struct conn client = { .quitting = false };` also zeroes `nick`, making it the empty string.

### parley.c: config and a table of structs

In the client, [Chapter 5](05-pointers-and-arrays.md)'s parallel arrays (`command_names[]` and `command_funcs[]`) become one array of structs that also carries the help text. Replace the `opt_*` globals:

`project/ch07/parley.c`
```c
/* Settings from the command line. */
struct config {
    const char *host;
    long port;
    const char *nick;
};
```

`parse_args` gets a `struct config *cfg` parameter as in the server (`cfg->host = value;`, `cfg->nick = value;`, `&cfg->port`). Replace the old `cmd_help` and the two arrays with:

```c
static void cmd_help(const char *arg);

/* The dispatch table: one struct per slash command. The table also holds
 * the help text, so /help can never get out of date. */
typedef void command_fn(const char *arg);

struct command {
    const char *name;
    const char *args;      /* argument summary for /help */
    const char *help;
    command_fn *run;
};

static const struct command commands[] = {
    { "nick", "NEWNICK",  "change your nickname",    cmd_nick },
    { "me",   "ACTION",   "describe an action",      cmd_me   },
    { "who",  "",         "list who is in the room", cmd_who  },
    { "quit", "[REASON]", "leave",                   cmd_quit },
    { "help", "",         "show this help",          cmd_help },
};
enum { NCOMMANDS = sizeof commands / sizeof commands[0] };

static void cmd_help(const char *arg)
{
    (void)arg;
    /* stdout carries protocol lines, so help goes to stderr. */
    fprintf(stderr, "commands:\n");
    for (int i = 0; i < NCOMMANDS; i++) {
        char usage[32];
        snprintf(usage, sizeof usage, "/%s %s", commands[i].name, commands[i].args);
        fprintf(stderr, "  %-15s %s\n", usage, commands[i].help);
    }
    fprintf(stderr, "anything else is sent as a message\n");
}
```

The table refers to `cmd_help`, and `cmd_help` reads the table. C needs names declared before use, so the first line is a *forward declaration* of `cmd_help`. Without it you get this error (plus two follow-on warnings):

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -c parley.c
parley.c:154:54: error: ‘cmd_help’ undeclared here (not in a function); did you mean ‘cmd_me’?
  154 |     { "help", "",         "show this help",          cmd_help },
      |                                                      ^~~~~~~~
      |                                                      cmd_me
```

In `run_command`, `command_names[i]` becomes `commands[i].name` and `command_funcs[i](arg)` becomes `commands[i].run(arg)`:

```c
    for (int i = 0; i < NCOMMANDS; i++) {
        if (strcmp(name, commands[i].name) == 0) {
            commands[i].run(arg);     /* call through the function pointer */
            return;
        }
    }
```

`main` starts with the defaults:

```c
int main(int argc, char *argv[])
{
    struct config cfg = { .host = "localhost", .port = DEFAULT_PORT, .nick = "guest" };
    parse_args(argc, argv, &cfg);
    printf("NICK %s\r\n", cfg.nick);
    /* ... the input loop is unchanged ... */
```

### Checkpoint

Build both programs with both libcs and feed the server a session. (The server's `\r\n` line endings are invisible here.)

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o parleyd parleyd.c
$ gcc -std=c99 -Wall -Wextra -pedantic -o parley parley.c
$ musl-gcc -std=c99 -Wall -Wextra -pedantic -static -o parleyd-musl parleyd.c
$ musl-gcc -std=c99 -Wall -Wextra -pedantic -static -o parley-musl parley.c
$ printf 'NICK alice\r\nmsg :hi there\r\nME waves\r\nNAMES\r\nPING :123\r\nNICK :Alice\r\nQUIT :bye\r\n' | ./parleyd 2>/dev/null
:parley 001 alice :Welcome to parley, alice
:alice JOIN
:parley 353 alice :alice
:parley 366 alice :End of NAMES
:alice MSG :hi there
:alice ME :waves
:parley 353 alice :alice
:parley 366 alice :End of NAMES
:parley PONG :123
:alice NICK Alice
ERROR :Goodbye
```

The error paths: a command before registration, a missing parameter, missing text, a prefix with no command, nine parameters (one too many) and an ignored line of spaces:

```console
$ printf 'NAMES\r\nPING\r\nNICK bob\r\nMSG\r\n:bob\r\nX 1 2 3 4 5 6 7 8 9\r\n   \r\n:bob MSG :prefix ignored\r\nQUIT\r\n' | ./parleyd 2>/dev/null
:parley 451 * :You have not registered
:parley 461 * PING :Not enough parameters
:parley 001 bob :Welcome to parley, bob
:bob JOIN
:parley 353 bob :bob
:parley 366 bob :End of NAMES
:parley 412 bob :No text to send
:parley 400 bob :Malformed message
:parley 400 bob :Malformed message
:bob MSG :prefix ignored
ERROR :Goodbye
$ printf 'NICK alice\r\nMSG :hi\r\nQUIT :bye\r\n' | ./parleyd >/dev/null
parleyd: info: reading one client session from stdin
parleyd: info: alice joined
parleyd: info: alice left (bye)
parleyd: info: session over
```

Pipe the client into the server, check the generated help, and run the musl pair:

```console
$ printf 'hello\n/me waves\n/who\n/nick bob\n/quit gotta go\n' | ./parley -n alice | ./parleyd 2>/dev/null
:parley 001 alice :Welcome to parley, alice
:alice JOIN
:parley 353 alice :alice
:parley 366 alice :End of NAMES
:alice MSG :hello
:alice ME :waves
:parley 353 alice :alice
:parley 366 alice :End of NAMES
:alice NICK bob
ERROR :Goodbye
$ printf '/help\n' | ./parley 2>&1 >/dev/null
commands:
  /nick NEWNICK   change your nickname
  /me ACTION      describe an action
  /who            list who is in the room
  /quit [REASON]  leave
  /help           show this help
anything else is sent as a message
$ printf 'hello\n/me waves\n/who\n/nick bob\n/quit gotta go\n' | ./parley-musl -n alice | ./parleyd-musl 2>/dev/null
:parley 001 alice :Welcome to parley, alice
:alice JOIN
:parley 353 alice :alice
:parley 366 alice :End of NAMES
:alice MSG :hello
:alice ME :waves
:parley 353 alice :alice
:parley 366 alice :End of NAMES
:alice NICK bob
ERROR :Goodbye
```

`2>&1 >/dev/null` sends stderr to the terminal and discards stdout. Order matters: redirections apply left to right.

The complete project at this point is in `project/ch07/`.

## Summary

- A `struct` groups named members into a new type. `.` reaches a member, `->` a member through a pointer. Assigning and passing copies the struct.
- Pass structs by pointer (`T *` to modify, `const T *` to read); by value only when small.
- Designated initializers (`{ .name = x }`, `[IDX] = y`) and compound literals (`(T){ ... }`) are C99. Unnamed members become zero; `{ 0 }` zeroes anything.
- The compiler pads structs for alignment; `sizeof` and `offsetof` tell the truth. Order members largest-first to save space. Never `memcmp` or `fwrite` whole structs.
- Layout comes from the platform ABI, not the libc: glibc and musl agree byte for byte. musl hides `FILE`'s members; glibc exposes them.
- Write binary formats byte by byte with shifts (`put_u32_be`/`get_u32_be`), never pointer casts.
- A flexible array member (`char text[];`) puts a header and its data in one allocation.
- A union stores one of several members in the same bytes; with an enum tag it is a tagged union. In C99 the union member needs a name.
- Bit-fields save memory, but their layout is implementation-defined: not for wire formats.
- An incomplete `struct name;` in a header makes an opaque type: callers use only pointers and functions.
- Enum constants are `int`s: named numbers that can size arrays (`enum { N = 64 }`) and, with `-Wswitch`, catch forgotten cases. No type safety.
- parley: `struct msg` with an in-place parser and bounds-checked formatter, `struct conn`, `struct config` and a command table of structs.

## Exercises

1. **Minimise padding.** What is `sizeof` of this struct on x86-64? Reorder the members to make it as small as possible; check with a program.

   ```c
   struct before {
       char a;
       double b;
       char c;
       int d;
       short e;
   };
   ```

   <details><summary>Solution</summary>

   `a` at 0, 7 padding bytes, `b` at 8, `c` at 16, 3 padding bytes, `d` at 20, `e` at 24, then 6 bytes of tail padding to reach a multiple of 8: 32 bytes for 16 bytes of data. Largest-first gives 8 + 4 + 2 + 1 + 1 = 16, no padding.

   ```c
   #include <stdio.h>

   struct before {
       char a;
       double b;
       char c;
       int d;
       short e;
   };

   struct after {
       double b;
       int d;
       short e;
       char a;
       char c;
   };

   int main(void)
   {
       printf("before: %zu bytes, after: %zu bytes\n",
              sizeof(struct before), sizeof(struct after));
       return 0;
   }
   ```

   ```console
   $ gcc -std=c99 -Wall -Wextra -pedantic -o pad pad.c && ./pad
   before: 32 bytes, after: 16 bytes
   ```

   musl-gcc prints the same, because layout is ABI.
   </details>

2. **64-bit byte I/O.** Write `put_u64_be` and `get_u64_be` with loops instead of eight lines each. Test a round trip for `0`, `1`, `0x0102030405060708` and `UINT64_MAX`.

   <details><summary>Solution</summary>

   ```c
   #include <stdint.h>
   #include <stdio.h>

   static void put_u64_be(unsigned char *p, uint64_t v)
   {
       for (int i = 7; i >= 0; i--) {
           p[i] = (unsigned char)v;
           v >>= 8;
       }
   }

   static uint64_t get_u64_be(const unsigned char *p)
   {
       uint64_t v = 0;
       for (int i = 0; i < 8; i++)
           v = v << 8 | p[i];
       return v;
   }

   int main(void)
   {
       const uint64_t tests[] = { 0, 1, 0x0102030405060708u, UINT64_MAX };
       unsigned char buf[8];
       for (size_t i = 0; i < sizeof tests / sizeof tests[0]; i++) {
           put_u64_be(buf, tests[i]);
           uint64_t back = get_u64_be(buf);
           printf("%016llx -> %02x %02x ... %02x -> %s\n",
                  (unsigned long long)tests[i], buf[0], buf[1], buf[7],
                  back == tests[i] ? "ok" : "MISMATCH");
       }
       return 0;
   }
   ```

   ```console
   $ gcc -std=c99 -Wall -Wextra -pedantic -o u64 u64.c && ./u64
   0000000000000000 -> 00 00 ... 00 -> ok
   0000000000000001 -> 00 00 ... 01 -> ok
   0102030405060708 -> 01 02 ... 08 -> ok
   ffffffffffffffff -> ff ff ... ff -> ok
   ```

   `%llx` with a cast to `unsigned long long` is a simple portable way to print a `uint64_t`; `<inttypes.h>`'s `PRIx64` is the other ([Chapter 2](02-types-and-operators.md)).
   </details>

3. **Extend the tagged union.** Add a `V_BOOL` kind to `struct value` from `union.c`, then write `bool value_equal(const struct value *a, const struct value *b)`. Values of different kinds are never equal; texts are equal if their characters match. What does gcc say if you forget `V_BOOL` in `value_equal`'s switch?

   <details><summary>Solution</summary>

   ```c
   #include <stdbool.h>
   #include <stdio.h>
   #include <string.h>

   enum value_kind { V_INT, V_DOUBLE, V_TEXT, V_BOOL };

   struct value {
       enum value_kind kind;
       union {
           long i;
           double d;
           const char *s;
           bool b;
       } as;
   };

   static bool value_equal(const struct value *a, const struct value *b)
   {
       if (a->kind != b->kind)
           return false;
       switch (a->kind) {
       case V_INT:    return a->as.i == b->as.i;
       case V_DOUBLE: return a->as.d == b->as.d;
       case V_TEXT:   return strcmp(a->as.s, b->as.s) == 0;
       case V_BOOL:   return a->as.b == b->as.b;
       }
       return false;
   }

   int main(void)
   {
       struct value x = { V_TEXT, { .s = "hi" } };
       char buf[] = "hi";
       struct value y = { V_TEXT, { .s = buf } };
       struct value t = { V_BOOL, { .b = true } };
       struct value n = { V_INT, { .i = 1 } };
       printf("x == y: %d\n", value_equal(&x, &y));
       printf("t == n: %d\n", value_equal(&t, &n));
       printf("t == t: %d\n", value_equal(&t, &t));
       return 0;
   }
   ```

   ```console
   $ gcc -std=c99 -Wall -Wextra -pedantic -o tagged tagged.c && ./tagged
   x == y: 1
   t == n: 0
   t == t: 1
   ```

   Forget the `V_BOOL` case and `-Wall` reports `warning: enumeration value ‘V_BOOL’ not handled in switch [-Wswitch]`. `x` and `y` hold different pointers, so `a->as.s == b->as.s` would be wrong: compare the characters.
   </details>

4. **Read an ELF header.** Every Linux executable starts with a 64-byte ELF header. Bytes 0–3 are `7f 45 4c 46` (`"\x7f" "ELF"`), byte 4 is the class (2 = 64-bit), byte 5 the byte order (1 = little-endian). Then, little-endian: type `u16` at offset 16 (2 = EXEC, 3 = DYN), machine `u16` at 18 (62 = x86-64), entry address `u64` at 24, program header count `u16` at 56, section header count `u16` at 60. Write `elfhdr`: read the header from stdin with `fread` and print these fields. Run it on itself and on a musl build of itself; compare with `readelf -h`. Do **not** `fread` into a struct; use `get_u16_le` and friends.

   <details><summary>Solution</summary>

   ```c
   /* elfhdr.c - print a few fields of a 64-bit little-endian ELF header.
    * Usage: ./elfhdr < some-program */
   #include <stdint.h>
   #include <stdio.h>
   #include <string.h>

   static uint16_t get_u16_le(const unsigned char *p)
   {
       return (uint16_t)(p[1] << 8 | p[0]);
   }

   static uint32_t get_u32_le(const unsigned char *p)
   {
       return (uint32_t)p[3] << 24 | (uint32_t)p[2] << 16 |
              (uint32_t)p[1] << 8 | (uint32_t)p[0];
   }

   static uint64_t get_u64_le(const unsigned char *p)
   {
       return (uint64_t)get_u32_le(p + 4) << 32 | get_u32_le(p);
   }

   int main(void)
   {
       unsigned char h[64];
       if (fread(h, 1, sizeof h, stdin) != sizeof h || memcmp(h, "\x7f" "ELF", 4) != 0) {
           fprintf(stderr, "elfhdr: not an ELF file\n");
           return 1;
       }
       if (h[4] != 2 || h[5] != 1) {
           fprintf(stderr, "elfhdr: only 64-bit little-endian ELF is supported\n");
           return 1;
       }
       unsigned type = get_u16_le(h + 16);
       printf("Type:    %s\n", type == 2 ? "EXEC" : type == 3 ? "DYN" : type == 1 ? "REL" : "other");
       printf("Machine: %u%s\n", (unsigned)get_u16_le(h + 18),
              get_u16_le(h + 18) == 62 ? " (x86-64)" : "");
       printf("Entry:   0x%llx\n", (unsigned long long)get_u64_le(h + 24));
       printf("Program headers: %u, section headers: %u\n",
              (unsigned)get_u16_le(h + 56), (unsigned)get_u16_le(h + 60));
       return 0;
   }
   ```

   `"\x7f" "ELF"` is two string literals pasted together. As one, `"\x7fELF"`, the hex escape would swallow the `E` too (a hex digit), and gcc says `warning: hex escape sequence out of range`.

   ```console
   $ gcc -std=c99 -Wall -Wextra -pedantic -o elfhdr elfhdr.c
   $ musl-gcc -std=c99 -Wall -Wextra -pedantic -static -o elfhdr-musl elfhdr.c
   $ ./elfhdr < elfhdr
   Type:    DYN
   Machine: 62 (x86-64)
   Entry:   0x10e0
   Program headers: 13, section headers: 31
   $ readelf -h elfhdr | grep -E 'Type|Entry'
     Type:                              DYN (Position-Independent Executable file)
     Entry point address:               0x10e0
   $ ./elfhdr < elfhdr-musl
   Type:    EXEC
   Machine: 62 (x86-64)
   Entry:   0x40105a
   Program headers: 6, section headers: 17
   $ ./elfhdr < elfhdr.c
   elfhdr: not an ELF file
   ```

   Entry addresses and counts may differ with another compiler version. The glibc build is a *position-independent executable* (DYN): it loads anywhere in memory and needs the dynamic loader. The `musl-gcc -static` build is a plain EXEC at a fixed address, with far fewer headers.
   </details>

5. **Explain it.** A colleague proposes sending `struct msg` between client and server with `write(fd, &m, sizeof m)`. Give three reasons why that cannot work.

   <details><summary>Solution</summary>

   1. `struct msg` holds **pointers**, and an address means nothing in another process. The receiver gets numbers, not strings.
   2. Even without pointers, a struct has **padding** with unspecified bytes, and integers in the **CPU's byte order**. The other side may be another CPU, or the program built by another compiler.
   3. The layout is an internal detail: add a member and every old peer misreads every message. A protocol must define its own bytes. parley's are text lines, written by `msg_format` and read by `msg_parse`.
   </details>

Sources for this chapter: [REFERENCES.md](REFERENCES.md#chapter-7--structs-unions-and-enums)
