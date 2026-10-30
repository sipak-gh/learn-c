# Chapter 6 — Strings

C has no string type. A string is a `char` array ending in a zero byte, and every string function relies on that. This chapter covers storage, read-only literals, everyday `<string.h>` functions, and why `strcpy`, `strcat` and `gets` cause so many security holes. You'll write safe replacements, convert text to numbers with full error checks, classify characters correctly, and meet UTF-8; several functions differ between glibc and musl. In the project, the server finally speaks the protocol: it reads one client's lines on stdin, checks nicknames, and answers on stdout. [Chapter 5](05-pointers-and-arrays.md) gave you the pointers; [Chapter 7](07-structs-unions-enums.md) turns protocol lines into structs.

## A string is a `char` array ending in `'\0'`

A C string is bytes followed by a **NUL terminator**, the byte with value 0, written `'\0'`, which is how functions find the end. Its length counts the bytes *before* the `'\0'`, so length 5 needs 6 bytes.

`examples/ch06/strings.c`
```c
/* strings.c - a C string is an array of char ending in '\0'. */
#include <stdio.h>
#include <string.h>

int main(void)
{
    char nick[8] = "alice";   /* 5 letters + '\0', the rest is zeros too */
    char word[] = { 'b', 'o', 'b', '\0' };   /* the same thing by hand */

    printf("nick = \"%s\", strlen = %zu, sizeof = %zu\n",
           nick, strlen(nick), sizeof nick);
    printf("word = \"%s\", strlen = %zu, sizeof = %zu\n",
           word, strlen(word), sizeof word);

    printf("bytes:");
    for (size_t i = 0; i < sizeof nick; i++)
        printf(" %d", nick[i]);
    printf("\n");

    nick[3] = '\0';           /* cut the string short */
    printf("after nick[3] = '\\0': \"%s\", strlen = %zu\n", nick, strlen(nick));

    char empty[1] = "";       /* the empty string is one byte: '\0' */
    printf("empty: strlen = %zu, sizeof = %zu\n", strlen(empty), sizeof empty);
    return 0;
}
```

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o strings strings.c
$ ./strings
nick = "alice", strlen = 5, sizeof = 8
word = "bob", strlen = 3, sizeof = 4
bytes: 97 108 105 99 101 0 0 0
after nick[3] = '\0': "ali", strlen = 3
empty: strlen = 0, sizeof = 1
```

The musl build prints the same. Note:

- **`strlen` counts bytes up to the `'\0'`; `sizeof` gives the array size.** Different questions. `strlen` walks the string on every call, so its cost grows with the length.
- **An initializer zero-fills the rest of the array.** `char nick[8] = "alice"` is `a l i c e \0 \0 \0`.
- **A `'\0'` written mid-string shortens it.** The later bytes remain, but no string function looks past the first `'\0'`.
- **`char word[] = "bob"` sizes the array for you**, terminator included: 4 bytes.

`'\0'` (NUL, value 0) is neither `NULL` (the null pointer) nor `'0'` (the digit, value 48).

The terminator is C strings' weak point. Without it, `strlen`, `printf("%s")` and every other string function read past the array until they hit a zero byte: undefined behaviour, and a common way to leak memory contents.

## String literals are read-only

A string literal like `"hello"` is a `char` array that lasts the whole run, in read-only memory ([Chapter 5](05-pointers-and-arrays.md#memory-and-addresses) showed where). Two ways to use one:

`examples/ch06/literal.c`
```c
/* literal.c - string literals are read-only; arrays are copies you own. */
#include <stdio.h>

int main(void)
{
    char copy[] = "hello";        /* an array, initialised from the literal */
    const char *lit = "hello";    /* a pointer to the literal itself */
    char *bad = "hello";          /* legal C, but the target is read-only */

    copy[0] = 'J';
    printf("copy = %s, lit = %s\n", copy, lit);
    printf("sizeof copy = %zu, sizeof lit = %zu\n", sizeof copy, sizeof lit);
    fflush(stdout);

    bad[0] = 'J';                 /* undefined behaviour: crashes on Linux */
    printf("bad = %s\n", bad);
    return 0;
}
```

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o literal literal.c
$ ./literal
copy = Jello, lit = hello
sizeof copy = 6, sizeof lit = 8
Segmentation fault (core dumped)
$ musl-gcc -std=c99 -Wall -Wextra -pedantic -static -o literal-musl literal.c
$ ./literal-musl
copy = Jello, lit = hello
sizeof copy = 6, sizeof lit = 8
Segmentation fault (core dumped)
```

- **`char copy[] = "hello"`** creates your own 6-byte array, a changeable copy of the literal.
- **`const char *lit = "hello"`** points at the literal itself. `sizeof lit` is the pointer's size (8), not the string's.
- **`char *bad = "hello"`** compiles silently because, by historical accident, a C literal (unlike C++) has type `char[N]`, not `const char[N]`. Writing through `bad` is undefined; on Linux the literal is in a read-only page, so it crashes on both libcs.

Always point at literals with `const char *`. gcc's `-Wwrite-strings` makes literals `const` so the compiler catches the mistake:

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -Wwrite-strings -c literal.c
literal.c: In function ‘main’:
literal.c:8:17: warning: initialization discards ‘const’ qualifier from pointer target type [-Wdiscarded-qualifiers]
    8 |     char *bad = "hello";          /* legal C, but the target is read-only */
      |                 ^~~~~~~
```

It's not in `-Wall` or `-Wextra`, since much old code would warn, but worth adding to new projects.

Two more facts about literals:

- **Adjacent literals join at compile time**: `"par" "ley"` is `"parley"`. Use it to split long strings across lines.
- **Identical literals may share storage**, so `"hello" == "hello"` may be true or false. Compare strings with `strcmp`, never `==`, which compares addresses.

## A tour of `<string.h>`

The functions you'll use most. All trust you: none checks that a pointer is valid or an array big enough.

| function | what it does |
|---|---|
| `strlen(s)` | length of `s`, not counting the `'\0'` |
| `strcmp(a, b)` | compare: `< 0`, `0` or `> 0`, like a dictionary |
| `strncmp(a, b, n)` | compare at most `n` bytes |
| `strchr(s, c)` | pointer to the first `c` in `s`, or `NULL` |
| `strrchr(s, c)` | pointer to the last `c` in `s`, or `NULL` |
| `strstr(s, sub)` | pointer to the first `sub` in `s`, or `NULL` |
| `strspn(s, set)` | how many leading bytes of `s` are in `set` |
| `strcspn(s, set)` | how many leading bytes of `s` are *not* in `set` |
| `memcpy(d, s, n)` | copy `n` bytes; the areas must not overlap |
| `memmove(d, s, n)` | copy `n` bytes; the areas may overlap |
| `memset(d, c, n)` | set `n` bytes to `c` |
| `memcmp(a, b, n)` | compare `n` bytes; `'\0'` is not special |
| `memchr(s, c, n)` | find a byte in the first `n` bytes |

`examples/ch06/tour.c`
```c
/* tour.c - the most used functions in <string.h>. */
#include <stdio.h>
#include <string.h>

int main(void)
{
    const char *line = "MSG #chat :hello, world";

    /* length and comparison */
    printf("strlen            = %zu\n", strlen(line));
    printf("strcmp(a, b)      = %d\n", strcmp("alice", "bob") < 0 ? -1 : 1);
    printf("strncmp 3 bytes   = %d\n", strncmp(line, "MSGX", 3));

    /* searching: each returns a pointer into line, or NULL */
    const char *colon = strchr(line, ':');
    printf("strchr ':'        -> \"%s\" (index %td)\n", colon, colon - line);
    printf("strrchr 'o'       -> \"%s\"\n", strrchr(line, 'o'));
    printf("strstr \"world\"    -> \"%s\"\n", strstr(line, "world"));
    printf("strstr \"bob\"      -> %s\n", strstr(line, "bob") ? "found" : "NULL");

    /* spans: how many leading bytes are (not) in a set */
    printf("strspn  upper     = %zu\n", strspn(line, "ABCDEFGHIJKLMNOPQRSTUVWXYZ"));
    printf("strcspn \" \"       = %zu\n", strcspn(line, " "));

    /* raw memory: works on bytes, '\0' is not special */
    char buf[16];
    memset(buf, '-', sizeof buf - 1);
    buf[sizeof buf - 1] = '\0';
    memcpy(buf, "abc", 3);              /* no '\0' copied */
    printf("memset+memcpy     = %s\n", buf);
    memmove(buf + 2, buf, 5);           /* overlapping: must be memmove */
    printf("memmove           = %s\n", buf);
    printf("memcmp            = %d\n", memcmp("ab\0x", "ab\0y", 4) < 0 ? -1 : 1);
    return 0;
}
```

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o tour tour.c
$ ./tour
strlen            = 23
strcmp(a, b)      = -1
strncmp 3 bytes   = 0
strchr ':'        -> ":hello, world" (index 10)
strrchr 'o'       -> "orld"
strstr "world"    -> "world"
strstr "bob"      -> NULL
strspn  upper     = 3
strcspn " "       = 3
memset+memcpy     = abc------------
memmove           = ababc----------
memcmp            = -1
```

The musl build prints the same. Notice:

- **`strcmp` returns a sign, not a fixed value.** Only negative, zero or positive is promised, and the libcs may return different numbers for the same pair. Test `< 0`, `== 0` or `> 0`, never `== -1`; hence the program prints the sign.
- **The search functions return pointers into your string**, not copies; subtracting the start gives the index (`%td` prints a `ptrdiff_t`). They take `const char *` but return plain `char *`, a known hole in C's `const` that lets you write through the result of a `const` string.
- **`strspn` and `strcspn` count, not search.** `strcspn(line, " ")` is the first word's length. Together they make a simple tokenizer (Exercise 3).
- **`memcpy` with overlapping areas is undefined.** Use `memmove` when source and destination might overlap, as in the shift above.
- **`memcmp` compares all `n` bytes**, even past a `'\0'`: it's for raw data, not strings.

### Gotcha: `strtok`

`<string.h>` also has `strtok` for splitting into tokens. Avoid it: it writes `'\0'`s into your string (failing on literals) and keeps its position in a hidden `static`, so you can't tokenize two strings at once or call anything else that uses it, and it isn't thread-safe ([Chapter 17](17-threads.md)). POSIX `strtok_r` takes the position as an argument. parley splits lines by hand with `strchr`, which is clearer.

## Writing your own

Most string functions are short loops. Writing a few shows how they use pointers and the terminator:

`examples/ch06/mystring.c`
```c
/* mystring.c - strlen, strcmp and strchr written by hand. */
#include <stdio.h>
#include <string.h>

static size_t my_strlen(const char *s)
{
    const char *p = s;
    while (*p != '\0')
        p++;
    return (size_t)(p - s);
}

/* Compare as unsigned char, like the real strcmp. */
static int my_strcmp(const char *a, const char *b)
{
    while (*a != '\0' && *a == *b) {
        a++;
        b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

/* Pointer to the first c in s, or NULL. c == '\0' finds the terminator. */
static char *my_strchr(const char *s, int c)
{
    for (;; s++) {
        if (*s == (char)c)
            return (char *)s;
        if (*s == '\0')
            return NULL;
    }
}

static int sign(int n)
{
    return (n > 0) - (n < 0);
}

int main(void)
{
    const char *words[] = { "", "bob", "alice", "alicia", "\xc3\xa9" };
    size_t n = sizeof words / sizeof words[0];

    for (size_t i = 0; i < n; i++)
        printf("len(\"%s\") = %zu %s\n", words[i], my_strlen(words[i]),
               my_strlen(words[i]) == strlen(words[i]) ? "ok" : "WRONG");
    for (size_t i = 0; i + 1 < n; i++) {
        int mine = sign(my_strcmp(words[i], words[i + 1]));
        int real = sign(strcmp(words[i], words[i + 1]));
        printf("cmp(\"%s\", \"%s\") = %2d %s\n", words[i], words[i + 1], mine,
               mine == real ? "ok" : "WRONG");
    }
    const char *s = "NICK bob";
    printf("chr ' ' -> \"%s\", chr '\\0' at index %td, chr 'z' -> %s\n",
           my_strchr(s, ' '), my_strchr(s, '\0') - s,
           my_strchr(s, 'z') == NULL ? "NULL" : "?");
    return 0;
}
```

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o mystring mystring.c
$ ./mystring
len("") = 0 ok
len("bob") = 3 ok
len("alice") = 5 ok
len("alicia") = 6 ok
len("é") = 2 ok
cmp("", "bob") = -1 ok
cmp("bob", "alice") =  1 ok
cmp("alice", "alicia") = -1 ok
cmp("alicia", "é") = -1 ok
chr ' ' -> " bob", chr '\0' at index 8, chr 'z' -> NULL
```

The musl build agrees on every line. Three details:

- **`my_strlen` subtracts two pointers** instead of counting, a common idiom.
- **`my_strcmp` compares bytes as `unsigned char`**, as `strcmp` must. x86-64 plain `char` is *signed*, so `é`'s first byte (`0xc3`) is -61 and would sort before `a`; as `unsigned char` it's 195, after every ASCII letter (last line). (ARM Linux's plain `char` is unsigned; code depending on either isn't portable.)
- **`my_strchr` can find the terminator**: searching for `'\0'` returns the end of the string. Like the real one, it casts away `const` on return.

## Why `strcpy`, `strcat` and `gets` are dangerous

`strcpy(dst, src)` copies `src` and its `'\0'` into `dst`; `strcat(dst, src)` appends. Neither knows `dst`'s size, since an array parameter is just a pointer ([Chapter 5](05-pointers-and-arrays.md#arrays-and-pointers)). A too-long source overwrites whatever follows the array, on the stack including the return address: the classic **buffer overflow**, used to take over programs for decades.

`examples/ch06/overrun.c`
```c
/* overrun.c - strcpy does not know how big the destination is. */
#include <stdio.h>
#include <string.h>

int main(int argc, char *argv[])
{
    char nick[8];
    const char *input = argc > 1 ? argv[1] : "bob";

    strcpy(nick, input);          /* overflows nick if input is 8+ bytes */
    printf("nick = %s\n", nick);
    return 0;
}
```

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o overrun overrun.c
$ ./overrun
nick = bob
$ ./overrun alice-in-wonderland
nick = alice-in-wonderland
*** stack smashing detected ***: terminated
Aborted (core dumped)
```

No warning, and the short name works, so the bug is easy to miss. The long name wrote 20 bytes into an 8-byte array, was even printed, and the program died only when `main` returned.

### glibc vs musl: catching overflows

The same overflow, built four ways:

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -O2 -o overrun-O2 overrun.c
$ ./overrun-O2 alice-in-wonderland
*** buffer overflow detected ***: terminated
Aborted (core dumped)
$ musl-gcc -std=c99 -Wall -Wextra -pedantic -static -o overrun-musl overrun.c
$ ./overrun-musl alice-in-wonderland
nick = alice-in-wonderland
Segmentation fault (core dumped)
```

| build | what happens |
|---|---|
| glibc, `-O0` | the copy happens, then "stack smashing detected", abort (exit 134) |
| glibc, `-O2` | stopped *before* the copy: "buffer overflow detected", abort (exit 134) |
| musl, `-O0` or `-O2` | the copy happens, then a plain segmentation fault (exit 139) |

Two guards are at work, neither part of C:

- **The stack protector.** Ubuntu's gcc enables `-fstack-protector-strong` by default: a secret *canary* value sits between local arrays and the return address and is checked before returning. If it changed, the program calls `__stack_chk_fail`. glibc's prints "stack smashing detected" and aborts. musl's just crashes on purpose: it executes `hlt`, a privileged instruction, and the kernel answers with `SIGSEGV`, so you see a segmentation fault and no message.
- **Fortify.** At `-O1` and above, Ubuntu's gcc also defines `_FORTIFY_SOURCE=2`. glibc's headers then swap `strcpy` for `__strcpy_chk`, which knows `nick`'s size and aborts *before* writing. musl has no fortify support, so `-O2` changes nothing there.

glibc diagnoses better: a message naming the problem, and with fortify no overflow at all. But these nets have holes: they don't check every array, and other distributions use other defaults. The only fix is not writing past the end, as the rest of this section shows. [Chapter 10](10-errors-and-debugging.md) adds AddressSanitizer, which finds such bugs reliably in testing.

### `gets`: the function that was removed

`gets(buf)` reads a line into `buf` with no way to give its size, so any longer line overflows it, and the user controls the input. It's the only standard function that can't be used safely; C11 removed it. C99 still has it, so you may meet it in old code:

`examples/ch06/gets.c`
```c
/* gets.c - never use gets: it cannot know how big the array is. */
#include <stdio.h>

int main(void)
{
    char name[8];
    gets(name);                   /* removed from C in C11 */
    printf("hello, %s\n", name);
    return 0;
}
```

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o gets gets.c
gets.c: In function ‘main’:
gets.c:7:5: warning: ‘gets’ is deprecated [-Wdeprecated-declarations]
    7 |     gets(name);                   /* removed from C in C11 */
      |     ^~~~
In file included from gets.c:2:
/usr/include/stdio.h:605:14: note: declared here
  605 | extern char *gets (char *__s) __wur __attribute_deprecated__;
      |              ^~~~
/usr/bin/ld: /tmp/ccLMcIi2.o: in function `main':
gets.c:(.text+0x23): warning: the `gets' function is dangerous and should not be used.
$ ./gets
alice-and-a-very-long-name
hello, alice-and-a-very-long-name
*** stack smashing detected ***: terminated
Aborted (core dumped)
$ musl-gcc -std=c99 -Wall -Wextra -pedantic -static -o gets-musl gets.c
$ ./gets-musl
alice-and-a-very-long-name
hello, alice-and-a-very-long-name
Segmentation fault (core dumped)
```

(The `/tmp/cc….o` name changes every run.) glibc warns twice: the compiler, because its header marks `gets` deprecated, and the linker. musl compiles it silently. With `-std=c11`, glibc's header no longer declares `gets`. Use `fgets`, which takes the size ([Reading lines with `fgets`](#reading-lines-with-fgets)).

### Gotcha: `strncpy` is not a safe `strcpy`

`strncpy(dst, src, n)` looks like a bounded `strcpy` but was designed for fixed-width fields in old Unix directory entries. If `src` has `n` or more bytes, it copies `n` and **adds no `'\0'`**, so `dst` isn't a string. If `src` is short, it zero-fills the rest of `dst`, wasting time. `strncat(dst, src, n)` differs again: `n` limits the bytes appended, not `dst`'s size. Use `snprintf` or `str_copy` below instead.

## `snprintf`: bounded formatting

`snprintf(buf, size, fmt, ...)` is `printf` into `buf`, never writing more than `size` bytes including the `'\0'`. A result that doesn't fit is cut short, still terminated. The key feature is the return value: the length the *whole* result would have had, so truncation is easy to detect:

`examples/ch06/snprintf.c`
```c
/* snprintf.c - bounded formatting, and how to detect truncation. */
#include <stdio.h>

int main(void)
{
    char buf[8];
    const char *names[] = { "bob", "parley-server" };

    for (int i = 0; i < 2; i++) {
        int n = snprintf(buf, sizeof buf, "%s", names[i]);
        if (n < 0) {
            fprintf(stderr, "snprintf: encoding error\n");
            return 1;
        }
        printf("wanted %2d bytes, got \"%s\"%s\n", n, buf,
               (size_t)n >= sizeof buf ? " (truncated)" : "");
    }

    /* Size 0 writes nothing: a way to measure first. */
    int need = snprintf(NULL, 0, ":%s MSG :%s", "alice", "hello there");
    printf("the reply needs %d bytes + 1 for '\\0'\n", need);
    return 0;
}
```

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o snprintf snprintf.c
$ ./snprintf
wanted  3 bytes, got "bob"
wanted 13 bytes, got "parley-" (truncated)
the reply needs 23 bytes + 1 for '\0'
```

Same output with musl. The rules:

- **Truncated means `n >= size`.** `n == size` is already truncated: the `'\0'` needs a byte too.
- **Negative means error.** Check before casting `n` to `size_t`, or -1 becomes huge.
- **`snprintf(NULL, 0, ...)` only measures** (allowed by C99): measure, allocate exactly enough ([Chapter 8](08-memory.md)), format again.
- **`vsnprintf`** takes a `va_list`, so you can build printf-style functions on it ([Chapter 4](04-functions.md#variadic-functions-stdargh)), as the project's `reply` does.

`snprintf(dst, size, "%s", src)` is also a correct bounded copy, slightly slower since it parses the format.

`sprintf` is unbounded, like `strcpy`; prefer `snprintf` everywhere.

## Safe copy and append: `str_copy` and `str_append`

Copying strings is common enough to deserve a function with a clear contract. OpenBSD designed the widely copied `strlcpy` and `strlcat` for this; we write our own with the same contract:

- Take the destination array's **full size**.
- **Always** terminate the result (if the size is at least 1).
- Return the **length of the string they tried to create**: `>= size` means truncated, as with `snprintf`.

`examples/ch06/strcopy.c`
```c
/* strcopy.c - safe copy and append that always terminate. */
#include <stdio.h>
#include <string.h>

/* Like BSD strlcpy: copy at most size - 1 bytes, always add '\0'.
 * Returns strlen(src); a result >= size means "truncated". */
static size_t str_copy(char *dst, const char *src, size_t size)
{
    size_t len = strlen(src);
    if (size > 0) {
        size_t n = len < size - 1 ? len : size - 1;
        memcpy(dst, src, n);
        dst[n] = '\0';
    }
    return len;
}

/* Like BSD strlcat: append src to the string in dst.
 * Returns the length the result wanted; >= size means "truncated". */
static size_t str_append(char *dst, const char *src, size_t size)
{
    const char *end = memchr(dst, '\0', size);
    if (end == NULL)                  /* dst is not terminated: give up */
        return size + strlen(src);
    size_t used = (size_t)(end - dst);
    return used + str_copy(dst + used, src, size - used);
}

int main(void)
{
    char nick[8];
    size_t n = str_copy(nick, "bob", sizeof nick);
    printf("\"%s\" (%zu, %s)\n", nick, n, n >= sizeof nick ? "truncated" : "fits");
    n = str_copy(nick, "a-very-long-nick", sizeof nick);
    printf("\"%s\" (%zu, %s)\n", nick, n, n >= sizeof nick ? "truncated" : "fits");

    char msg[16] = "hello";
    n = str_append(msg, ", ", sizeof msg);
    n = str_append(msg, "world", sizeof msg);
    printf("\"%s\" (%zu, %s)\n", msg, n, n >= sizeof msg ? "truncated" : "fits");
    n = str_append(msg, " and everyone", sizeof msg);
    printf("\"%s\" (%zu, %s)\n", msg, n, n >= sizeof msg ? "truncated" : "fits");
    return 0;
}
```

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o strcopy strcopy.c
$ ./strcopy
"bob" (3, fits)
"a-very-" (16, truncated)
"hello, world" (12, fits)
"hello, world an" (25, truncated)
```

The musl build prints the same. `str_append` finds `dst`'s end with `memchr`, limited to `size` bytes, so it never reads past the array even if `dst` is unterminated. (`strnlen` would be neater, but it's POSIX, not C99.)

Whether truncation is acceptable is the caller's call: a log line losing its end is fine; a file name losing its end is a bug, maybe a security bug. The return value lets each caller decide; that's why the contract matters.

### glibc vs musl: `strlcpy`

Why write our own? Here is the real `strlcpy`:

`examples/ch06/strlcpy.c`
```c
/* strlcpy.c - BSD strlcpy: in musl, and in glibc only since 2.38. */
#define _DEFAULT_SOURCE               /* musl declares strlcpy under this */
#include <stdio.h>
#include <string.h>

int main(void)
{
    char nick[8];
    size_t n = strlcpy(nick, "a-very-long-nick", sizeof nick);
    printf("nick = \"%s\", strlcpy returned %zu (%s)\n", nick, n,
           n >= sizeof nick ? "truncated" : "fits");
    return 0;
}
```

```console
$ musl-gcc -std=c99 -Wall -Wextra -pedantic -static -o strlcpy-musl strlcpy.c
$ ./strlcpy-musl
nick = "a-very-", strlcpy returned 16 (truncated)
$ gcc -std=c99 -Wall -Wextra -pedantic -o strlcpy strlcpy.c
strlcpy.c: In function ‘main’:
strlcpy.c:9:16: warning: implicit declaration of function ‘strlcpy’; did you mean ‘strncpy’? [-Wimplicit-function-declaration]
    9 |     size_t n = strlcpy(nick, "a-very-long-nick", sizeof nick);
      |                ^~~~~~~
      |                strncpy
/usr/bin/ld: /tmp/ccyikD0x.o: in function `main':
strlcpy.c:(.text+0x37): undefined reference to `strlcpy'
collect2: error: ld returned 1 exit status
```

musl has had `strlcpy` and `strlcat` for years; glibc long refused them and added them only in 2.38 (2023). Ubuntu 22.04's glibc 2.35 doesn't declare it, and the linker can't find it. ([Chapter 4](04-functions.md#gotcha-calling-a-function-the-compiler-hasnt-seen) explained the implicit-declaration warning.)

musl is better here: the function is just there. But code that must build on both, and on older systems, can't count on it. Our `str_copy` is ten lines of C99 that work everywhere, so parley uses it. POSIX 2024 added `strlcpy` too, so this difference will fade.

## Text to numbers: `strtol` and `strtoul`

Options and protocol fields arrive as text. `strtol(s, &end, base)` converts the start of `s` to a `long` and reports problems three separate ways; a careful caller checks all three:

1. **`end`** is set to the first *unused* character. `end == s` means no digits; `*end != '\0'` means junk after the number.
2. **`errno`** becomes `ERANGE` if the value doesn't fit in a `long`, and the result is clamped to `LONG_MAX` or `LONG_MIN`. `strtol` never zeroes `errno` on success, so set it to 0 yourself first.
3. **The range** your program accepts, such as 1–65535 for a port, is your own check.

`examples/ch06/strtol.c`
```c
/* strtol.c - converting text to numbers, with full error checking. */
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

static void try(const char *s)
{
    char *end;
    errno = 0;                        /* strtol only sets errno on error */
    long n = strtol(s, &end, 10);

    printf("%-22s -> %20ld  ", s, n);
    if (end == s)
        printf("no digits at all\n");
    else if (errno == ERANGE)
        printf("out of range (clamped to LONG_%s)\n", n == LONG_MAX ? "MAX" : "MIN");
    else if (*end != '\0')
        printf("trailing junk \"%s\"\n", end);
    else
        printf("ok\n");
}

int main(void)
{
    const char *inputs[] = {
        "6667", "  -42", "+7", "", "port", "66x", "12 ",
        "99999999999999999999", "-99999999999999999999",
    };
    for (size_t i = 0; i < sizeof inputs / sizeof inputs[0]; i++)
        try(inputs[i]);

    /* Base 0 accepts C-style prefixes. */
    printf("base 0: \"0x1f\" = %ld, \"017\" = %ld\n",
           strtol("0x1f", NULL, 0), strtol("017", NULL, 0));

    /* strtoul accepts a minus sign and negates the result! */
    errno = 0;
    unsigned long u = strtoul("-1", NULL, 10);
    printf("strtoul(\"-1\") = %lu, errno = %d\n", u, errno);
    printf("atoi(\"port\") = %d (no way to tell this from \"0\")\n", atoi("port"));
    return 0;
}
```

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o strtol strtol.c
$ ./strtol
6667                   ->                 6667  ok
  -42                  ->                  -42  ok
+7                     ->                    7  ok
                       ->                    0  no digits at all
port                   ->                    0  no digits at all
66x                    ->                   66  trailing junk "x"
12                     ->                   12  trailing junk " "
99999999999999999999   ->  9223372036854775807  out of range (clamped to LONG_MAX)
-99999999999999999999  -> -9223372036854775808  out of range (clamped to LONG_MIN)
base 0: "0x1f" = 31, "017" = 15
strtoul("-1") = 18446744073709551615, errno = 0
atoi("port") = 0 (no way to tell this from "0")
```

The musl build prints the same. Note:

- **Leading whitespace and a sign are accepted**, trailing whitespace isn't: `"12 "` has junk. Decide if that's what you want.
- **Base 0** reads `0x` as hex and a leading `0` as octal, like C source, which surprises users typing `010` and getting 8. Use base 10 for user input.
- **`strtoul` accepts a minus sign**: it converts `"1"` and negates it as unsigned, so `"-1"` becomes `ULONG_MAX` with no error. For an unsigned value, use `strtol` (or `strtoll`) and reject negatives, as parley does.
- **`atoi` can't report errors.** It returns 0 for `"port"`, and overflow is undefined. Don't use it on input you didn't write.

`parse_number` ([Chapter 4](04-functions.md)) already used this pattern; the project turns it into `str_to_long` below.

## Classifying characters: `<ctype.h>`

`<ctype.h>` answers questions about single characters (`isalpha`, `isdigit`, `isalnum`, `isspace`, `ispunct`, `isupper`, `islower`, `isprint` and more) and converts with `tolower` and `toupper`. They follow the current *locale*; a program that never calls `setlocale` runs in the "C" locale, where only ASCII letters are letters.

The trap: the `int` argument must be `EOF` or fit in an `unsigned char` (0–255); anything else is undefined. On x86-64 plain `char` is signed, so every byte 0x80–0xff, including every non-ASCII UTF-8 byte, is a *negative* `char`, and passing it straight to `isalpha` is a bug. Always cast to `unsigned char` first:

`examples/ch06/ctype.c`
```c
/* ctype.c - classifying characters, and the unsigned char cast. */
#include <ctype.h>
#include <stdio.h>

int main(void)
{
    const char *s = "Bob_9 -!";

    for (const char *p = s; *p != '\0'; p++) {
        unsigned char c = (unsigned char)*p;   /* always cast first */
        printf("'%c' alpha=%d digit=%d alnum=%d space=%d punct=%d upper=%d -> '%c'\n",
               c, isalpha(c) != 0, isdigit(c) != 0, isalnum(c) != 0,
               isspace(c) != 0, ispunct(c) != 0, isupper(c) != 0, tolower(c));
    }

    /* A byte >= 0x80 in a plain char is negative on x86-64. */
    const char *cafe = "caf\xc3\xa9";          /* "café" in UTF-8 */
    for (const char *p = cafe; *p != '\0'; p++)
        printf("byte %4d: toupper((unsigned char)c) = %3d, toupper(c) = %4d\n",
               *p, toupper((unsigned char)*p), toupper(*p));
    return 0;
}
```

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o ctype ctype.c
$ ./ctype
'B' alpha=1 digit=0 alnum=1 space=0 punct=0 upper=1 -> 'b'
'o' alpha=1 digit=0 alnum=1 space=0 punct=0 upper=0 -> 'o'
'b' alpha=1 digit=0 alnum=1 space=0 punct=0 upper=0 -> 'b'
'_' alpha=0 digit=0 alnum=0 space=0 punct=1 upper=0 -> '_'
'9' alpha=0 digit=1 alnum=1 space=0 punct=0 upper=0 -> '9'
' ' alpha=0 digit=0 alnum=0 space=1 punct=0 upper=0 -> ' '
'-' alpha=0 digit=0 alnum=0 space=0 punct=1 upper=0 -> '-'
'!' alpha=0 digit=0 alnum=0 space=0 punct=1 upper=0 -> '!'
byte   99: toupper((unsigned char)c) =  67, toupper(c) =   67
byte   97: toupper((unsigned char)c) =  65, toupper(c) =   65
byte  102: toupper((unsigned char)c) =  70, toupper(c) =   70
byte  -61: toupper((unsigned char)c) = 195, toupper(c) =  195
byte  -87: toupper((unsigned char)c) = 169, toupper(c) =  169
$ musl-gcc -std=c99 -Wall -Wextra -pedantic -static -o ctype-musl ctype.c
$ ./ctype-musl | tail -2
byte  -61: toupper((unsigned char)c) = 195, toupper(c) =  -61
byte  -87: toupper((unsigned char)c) = 169, toupper(c) =  -87
```

The `is...` functions return "non-zero" for true, not necessarily 1, hence `!= 0`. `'_'` and `'-'` are punctuation, not letters; the project's nickname check allows them by name.

### glibc vs musl: `toupper` of a negative `char`

With the cast, both libcs agree. Without it, `toupper(-61)` is undefined, and they differ:

- **glibc** looks it up in a table that also covers -128 to -1, a courtesy to old code that forgot the cast. -61 is treated like 195, and 195 is returned.
- **musl** computes it: between `'a'` and `'z'` it clears the lowercase bit, otherwise returns the value unchanged. -61 comes back as -61.

Neither makes the call correct. glibc's courtesy hides the bug on your machine and fails for byte 0xff, which as a signed `char` is -1, i.e. `EOF`; musl is simpler and doesn't pretend. gcc doesn't warn about the missing cast, so: **cast every `char` to `unsigned char` before passing it to a `<ctype.h>` function.**

## Comparing without case

IRC commands are case-insensitive: `NICK`, `nick` and `Nick` are the same. C99 has no function for this; POSIX has `strcasecmp` and `strncasecmp` in `<strings.h>` (with an `s`, not `<string.h>`). As [Chapter 4](04-functions.md#functions-outside-c99-feature-test-macros) explained, `-std=c99` needs a feature-test macro for POSIX:

`examples/ch06/casecmp.c`
```c
/* casecmp.c - case-insensitive comparison: POSIX strcasecmp vs our own. */
#define _POSIX_C_SOURCE 200809L
#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <strings.h>                  /* strcasecmp: POSIX, not C99 */

/* ASCII-only equality, portable C99. */
static bool str_ieq(const char *a, const char *b)
{
    while (*a != '\0' && tolower((unsigned char)*a) == tolower((unsigned char)*b)) {
        a++;
        b++;
    }
    return tolower((unsigned char)*a) == tolower((unsigned char)*b);
}

int main(void)
{
    const char *pairs[][2] = {
        { "NICK", "nick" }, { "Nick", "NICKNAME" }, { "msg", "MSG" }, { "quit", "QUIZ" },
    };
    for (size_t i = 0; i < sizeof pairs / sizeof pairs[0]; i++) {
        const char *a = pairs[i][0], *b = pairs[i][1];
        int r = strcasecmp(a, b);
        printf("%-5s %-9s strcasecmp %-2s 0, str_ieq %s\n", a, b,
               r < 0 ? "<" : r > 0 ? ">" : "==", str_ieq(a, b) ? "true" : "false");
    }
    printf("strncasecmp(\"NICKNAME\", \"nick\", 4) = %d\n",
           strncasecmp("NICKNAME", "nick", 4));
    return 0;
}
```

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o casecmp casecmp.c
$ ./casecmp
NICK  nick      strcasecmp == 0, str_ieq true
Nick  NICKNAME  strcasecmp <  0, str_ieq false
msg   MSG       strcasecmp == 0, str_ieq true
quit  QUIZ      strcasecmp <  0, str_ieq false
strncasecmp("NICKNAME", "nick", 4) = 0
```

The musl build prints the same; both libcs have `strcasecmp`. The project still uses its own `str_ieq`: it needs only equality, it's plain C99, and it's deliberately ASCII-only. `strcasecmp` follows the locale, and protocol keywords shouldn't change meaning with the user's language settings.

Case-folding is only simple for ASCII. Elsewhere a lowercase letter may have no single uppercase form, or change byte length when converted. Real Unicode case-folding needs a library; chat servers usually sidestep it by allowing only ASCII names.

## `printf("%s", NULL)`

A null pointer for `%s` is undefined: `%s` needs a string, and `NULL` points to nothing. It happens easily, for example when an optional argument is missing:

`examples/ch06/nullstr.c`
```c
/* nullstr.c - printing a NULL string is undefined behaviour. */
#include <stdio.h>

int main(int argc, char *argv[])
{
    /* NULL, but the compiler cannot know that while compiling. */
    const char *nick = argc > 1 ? argv[1] : NULL;

    printf("nick: %s\n", nick);       /* UB when nick is NULL */
    fflush(stdout);
    printf("%s\n", nick);             /* gcc turns this into puts(nick) */
    printf("survived\n");
    return 0;
}
```

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o nullstr nullstr.c
$ ./nullstr bob
nick: bob
bob
survived
$ ./nullstr
nick: (null)
Segmentation fault (core dumped)
```

### glibc vs musl: printing a null string

| build | `./nullstr` (no argument) |
|---|---|
| glibc, `-O0` | `nick: (null)`, then a segmentation fault |
| glibc, `-O2` | `nick: (null)`, `(null)`, `survived` |
| musl, `-O0` or `-O2` | `nick: (null)`, then a segmentation fault |

Both libcs print `(null)` when `printf` itself meets a null `%s`, a kindness, not a promise. The crash is gcc's doing: it rewrites `printf("%s\n", x)` as the faster `puts(x)`, which reads through the pointer unchecked. With glibc at `-O2`, fortify ([above](#glibc-vs-musl-catching-overflows)) makes `printf` into `__printf_chk` instead, so there's no rewrite and the program "survives".

Neither libc is better: the same line works or crashes depending on libc, optimisation level and format string. Check for `NULL` yourself, e.g. `nick ? nick : "(none)"`.

## Bytes, characters and UTF-8

A C string is bytes; C doesn't care what text they encode. Today that's nearly always **UTF-8**, which stores each Unicode character (*code point*) in 1 to 4 bytes:

- ASCII characters (0–127) are one unchanged byte, so every ASCII string is valid UTF-8.
- Other characters use 2–4 bytes, all 0x80 or above. The first byte says how many follow; each following byte has the form `10xxxxxx` (0x80–0xbf).
- No byte of a multi-byte character is ever `'\0'` or ASCII. So `strlen`, `strchr(s, ':')`, `strcmp` for equality, and splitting at spaces all work on UTF-8 unchanged.

`examples/ch06/utf8.c`
```c
/* utf8.c - C strings are bytes; UTF-8 text passes through untouched. */
#include <stdio.h>
#include <string.h>

/* Count code points: every byte except continuation bytes 10xxxxxx. */
static size_t utf8_count(const char *s)
{
    size_t n = 0;
    for (; *s != '\0'; s++)
        if (((unsigned char)*s & 0xC0) != 0x80)
            n++;
    return n;
}

int main(void)
{
    const char *words[] = { "cafe", "caf\xc3\xa9", "\xe2\x82\xac" "5", "\xf0\x9f\x91\x8b" };

    for (size_t i = 0; i < sizeof words / sizeof words[0]; i++) {
        printf("%-6s strlen = %zu, code points = %zu, bytes:",
               words[i], strlen(words[i]), utf8_count(words[i]));
        for (const char *p = words[i]; *p != '\0'; p++)
            printf(" %02x", (unsigned char)*p);
        printf("\n");
    }
    return 0;
}
```

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o utf8 utf8.c
$ ./utf8
cafe   strlen = 4, code points = 4, bytes: 63 61 66 65
café  strlen = 5, code points = 4, bytes: 63 61 66 c3 a9
€5   strlen = 4, code points = 2, bytes: e2 82 ac 35
👋   strlen = 4, code points = 1, bytes: f0 9f 91 8b
```

The musl build prints the same bytes. The source writes non-ASCII characters as `\x` escapes, so the file is pure ASCII and compiles the same everywhere. (Note `"\xe2\x82\xac" "5"`: a `\x` escape takes as many hex digits as it can, so `"\xac5"` would be one wrong byte; splitting the literal ends the escape.)

The columns are misaligned: `%-6s` pads to 6 *bytes*, not screen characters, so `café` is one space short, `€5` two, and the emoji is also two columns wide on most terminals. Screen columns are much harder to count than bytes. The client's line editor ([Chapter 20](20-terminal-ui.md)) moves its cursor by code points, using `utf8_count`'s `10xxxxxx` rule.

For parley this means:

- **Messages are bytes.** The server passes them through unexamined, so any UTF-8 works.
- **Nicknames are ASCII**, so validating, comparing and case-folding them is simple and safe, and nobody can mimic another's name with look-alike letters.
- **Lengths are bytes.** The 512-byte line limit is a byte limit, as in IRC. Cutting at a byte limit can split a multi-byte character, leaving the receiver an invalid byte.

## Reading lines with `fgets`

`fgets(buf, size, stream)` is the safe line reader. It stops after storing a `'\n'`, after `size - 1` bytes, or at end of input, whichever comes first, and always adds a `'\0'`. It returns `NULL` at end of input (or on error) when nothing was read.

After a successful call, the `'\n'` tells three cases apart:

1. **A `'\n'` in the buffer:** a whole line, newline included.
2. **No `'\n'`, input ended:** the last line had no newline.
3. **No `'\n'`, input not ended:** the line was too long. The rest still waits in the stream, and the next `fgets` would return it as if it were a new line.

`examples/ch06/readline.c`
```c
/* readline.c - read lines with fgets and notice lines that don't fit. */
#include <stdio.h>
#include <string.h>

int main(void)
{
    char line[8];                       /* room for 6 chars + '\n' + '\0' */
    int n = 0;

    while (fgets(line, sizeof line, stdin) != NULL) {
        n++;
        char *nl = strchr(line, '\n');
        if (nl != NULL) {
            *nl = '\0';                 /* a whole line: drop the '\n' */
            printf("line %d: \"%s\"\n", n, line);
        } else if (feof(stdin)) {
            printf("line %d: \"%s\" (no newline at the end)\n", n, line);
        } else {
            int c;                      /* too long: skip the rest of it */
            while ((c = getchar()) != EOF && c != '\n')
                ;
            printf("line %d: too long, starts \"%s\"\n", n, line);
        }
    }
    return 0;
}
```

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -o readline readline.c
$ printf 'NICK a\nhello, world\n\nbye' | ./readline
line 1: "NICK a"
line 2: too long, starts "hello, "
line 3: ""
line 4: "bye" (no newline at the end)
```

The musl build prints the same. `feof(stdin)` turns true once a read hits end of input, separating case 2 from 3. To skip the rest of a long line, discard characters up to the next `'\n'`. `getchar` returns an `int` so negative `EOF` can't be confused with a byte.

The tiny buffer just makes the cases visible. Real programs size the buffer for the longest accepted line and either reject longer ones (as parley does) or grow the buffer ([Chapter 8](08-memory.md)).

Edge case: a line of exactly 7 characters counts as too long here, since its `'\n'` doesn't fit; the skipping loop then reads just that `'\n'`. Try `printf 'abcdefg\n' | ./readline`.

## Building parley: the server speaks the protocol

So far the server only parsed options; now it handles a real one-client session. With no network yet ([Chapter 15](15-a-first-server.md)), it reads protocol lines from stdin and replies on stdout, enough to test the whole protocol with `printf` and pipes, or to connect the client with `|`.

The rules ([Appendix C](appendix-c-parley-protocol.md) has the full protocol):

| the client sends | the server replies |
|---|---|
| anything except `NICK`/`QUIT` before registering | `:parley 451 * :You have not registered` |
| `NICK` with no name | `:parley 431 <nick> :No nickname given` |
| `NICK <bad name>` | `:parley 432 <nick> <bad name> :Erroneous nickname` |
| the first valid `NICK alice` | `:parley 001 alice :Welcome to parley, alice` and `:alice JOIN` |
| a later `NICK bob` | `:alice NICK bob` |
| `MSG :text` | `:alice MSG :text` |
| `QUIT` | `ERROR :Goodbye`, then the server stops reading |
| an unknown command | `:parley 421 <nick> <command> :Unknown command` |
| a line longer than 512 bytes | `:parley 417 <nick> :Input line too long` |

`<nick>` is the client's nickname, or `*` before it has one. Commands are case-insensitive; every line ends with CR LF. A valid nickname is 1 to 16 characters: a letter, then letters, digits, `_` or `-`.

Everything stays in `parleyd.c` for now; [Chapter 9](09-preprocessor-and-building.md) splits it into several files.

### parleyd.c

At the top, replace the comment, include `<ctype.h>`, and add two limits to the enum:

`project/ch06/parleyd.c` (top)
```c
/* parleyd.c - the parley chat server.
 * Chapter 6: no network yet. The server talks to one client on
 * stdin/stdout: read protocol lines from stdin, write replies to stdout. */
#include <ctype.h>
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
    MAX_CLIENTS_MAX = 1000,
    MSG_LINE_MAX = 512,   /* longest protocol line, including "\r\n" */
    NICK_MAX = 16         /* longest nickname */
};
```

`level_name`, `log_msg`, `usage_error` and `print_help` stay as they were. Replace `parse_number` with a block of string helpers. `str_copy` and `str_ieq` come from this chapter; `str_chomp` strips the line ending, `"\n"` or `"\r\n"`; `str_to_long` is `parse_number` with a better interface, returning success as a `bool` and the number through a pointer, so no value is reserved for "error":

`project/ch06/parleyd.c` (string helpers)
```c
/* ---- string helpers ---------------------------------------------------- */

/* Copy src into dst, which has room for size bytes. Always NUL-terminates
 * (if size > 0) and never writes past dst[size - 1], like BSD strlcpy.
 * Returns strlen(src): if that is >= size, the copy was truncated. */
static size_t str_copy(char *dst, const char *src, size_t size)
{
    size_t len = strlen(src);
    if (size > 0) {
        size_t n = len < size - 1 ? len : size - 1;
        memcpy(dst, src, n);
        dst[n] = '\0';
    }
    return len;
}

/* Remove any trailing '\r' and '\n' characters. */
static void str_chomp(char *s)
{
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r'))
        s[--n] = '\0';
}

/* ASCII case-insensitive equality: str_ieq("Bob", "bOB") is true. */
static bool str_ieq(const char *a, const char *b)
{
    /* <ctype.h> functions need an unsigned char value (or EOF): a plain
     * char may be negative, which is undefined behaviour for tolower. */
    while (*a != '\0' && tolower((unsigned char)*a) == tolower((unsigned char)*b)) {
        a++;
        b++;
    }
    return tolower((unsigned char)*a) == tolower((unsigned char)*b);
}

/* Parse a whole decimal number in [min, max] into *out.
 * Returns false (and leaves *out alone) if s is not such a number. */
static bool str_to_long(const char *s, long min, long max, long *out)
{
    char *end;
    errno = 0;
    long n = strtol(s, &end, 10);
    if (errno != 0 || end == s || *end != '\0' || n < min || n > max)
        return false;
    *out = n;
    return true;
}
```

Right after the string helpers, before `parse_args`, add the nickname rule. The `unsigned char` cast comes first, so a UTF-8 byte is a large positive number that `isalnum` rejects, not a negative one that is undefined:

`project/ch06/parleyd.c` (nicknames)
```c
/* ---- nicknames ----------------------------------------------------------- */

/* A nickname is 1-16 characters: a letter, then letters, digits, '_' or '-'. */
static bool nick_valid(const char *nick)
{
    size_t len = strlen(nick);
    if (len == 0 || len > NICK_MAX || !isalpha((unsigned char)nick[0]))
        return false;
    for (size_t i = 1; i < len; i++) {
        unsigned char c = (unsigned char)nick[i];
        if (!isalnum(c) && c != '_' && c != '-')
            return false;
    }
    return true;
}
```

In `parse_args`, the `-p` and `-m` cases now call `str_to_long`:

`project/ch06/parleyd.c` (in `parse_args`)
```c
        case 'p':
            if (!str_to_long(value, 1, PORT_MAX, &opt_port))
                usage_error("invalid port '%s' (expected 1-%d)", value, PORT_MAX);
            break;
        case 'm':
            if (!str_to_long(value, 1, MAX_CLIENTS_MAX, &opt_max_clients))
                usage_error("invalid client limit '%s' (expected 1-%d)",
                            value, MAX_CLIENTS_MAX);
            break;
```

Now the session, after `parse_args` and before `main`. One client's state is two variables: its nickname (empty until registered) and whether it quit. `reply` is printf-style on `vsnprintf`, like [Chapter 4](04-functions.md)'s logger: it formats into a buffer sized for the longest protocol line, warns if the reply didn't fit, and adds CR LF:

`project/ch06/parleyd.c` (the session)
```c
/* ---- the session with our one client ------------------------------------- */

static char client_nick[NICK_MAX + 1];  /* "" until the client registers */
static bool client_quit = false;

/* The name to put in replies: the nick, or "*" before registration. */
static const char *target(void)
{
    return client_nick[0] != '\0' ? client_nick : "*";
}

/* Send one protocol line built printf-style. The line (without "\r\n")
 * must fit in MSG_LINE_MAX - 2 bytes; vsnprintf tells us if it didn't. */
static void reply(const char *fmt, ...)
{
    char line[MSG_LINE_MAX - 2 + 1];   /* + 1 for the NUL terminator */
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    if (n < 0)
        return;
    if ((size_t)n >= sizeof line)
        log_msg(LOG_WARN, "reply truncated from %d to %zu bytes", n, sizeof line - 1);
    printf("%s\r\n", line);
}

static void handle_nick(const char *arg)
{
    if (*arg == '\0') {
        reply(":parley 431 %s :No nickname given", target());
        return;
    }
    if (!nick_valid(arg)) {
        reply(":parley 432 %s %s :Erroneous nickname", target(), arg);
        return;
    }
    if (client_nick[0] == '\0') {          /* first NICK: registration */
        str_copy(client_nick, arg, sizeof client_nick);
        reply(":parley 001 %s :Welcome to parley, %s", client_nick, client_nick);
        reply(":%s JOIN", client_nick);
        log_msg(LOG_INFO, "%s joined", client_nick);
    } else if (strcmp(client_nick, arg) != 0) {   /* a rename */
        reply(":%s NICK %s", client_nick, arg);
        log_msg(LOG_INFO, "%s is now %s", client_nick, arg);
        str_copy(client_nick, arg, sizeof client_nick);
    }
}

static void handle_line(char *line)
{
    /* Split "COMMAND rest of line" at the first space. */
    char *arg = strchr(line, ' ');
    if (arg != NULL)
        *arg++ = '\0';
    else
        arg = line + strlen(line);   /* no argument: point at the "" */

    if (line[0] == '\0')
        return;                       /* empty line: ignore it */
    if (str_ieq(line, "NICK")) {
        handle_nick(arg);
    } else if (str_ieq(line, "QUIT")) {
        reply("ERROR :Goodbye");
        client_quit = true;
    } else if (client_nick[0] == '\0') {
        reply(":parley 451 * :You have not registered");
    } else if (str_ieq(line, "MSG")) {
        if (*arg == ':')
            arg++;                    /* skip the ':' before the text */
        reply(":%s MSG :%s", client_nick, arg);
    } else {
        reply(":parley 421 %s %s :Unknown command", target(), line);
    }
}
```

Points to notice:

- **`client_nick` holds a copy**, not a pointer into the input line, whose buffer is reused for the next line and would soon hold different text. `str_copy` can't truncate here because `nick_valid` has checked the length, but it stays safe even if a later change breaks that.
- **`handle_line` splits in place**, like the client's `run_command` in [Chapter 5](05-pointers-and-arrays.md): `strchr` finds the first space, a `'\0'` ends the command there, and `arg` points just after. With no space, `arg` points at the line's own terminator, an empty string, so `arg` is never `NULL` and handlers never check for it.
- **`*arg++ = '\0'`** writes the `'\0'`, then moves `arg` on one byte. Postfix `++` binds tighter than `*` but yields the old pointer.
- **Renaming to the same name does nothing**, and the case-sensitive `strcmp` makes `alice` → `Alice` a rename. (Uniqueness differs: from [Chapter 17](17-threads.md), with several clients, `Alice` and `alice` count as the same name and can't both be online, as in IRC.)
- **`reply` checks for truncation** instead of assuming every reply fits. A `MSG` line that just fit coming from the client returns 7 bytes longer, with `:alice ` in front; the Checkpoint shows the warning.

Finally `main` runs the session. The buffer holds the longest allowed line, 512 bytes with CR LF, plus the `'\0'`. The too-long check is the one from [Reading lines with `fgets`](#reading-lines-with-fgets):

`project/ch06/parleyd.c` (`main`)
```c
int main(int argc, char *argv[])
{
    parse_args(argc, argv);
    log_msg(LOG_DEBUG, "verbose logging is on");
    log_msg(LOG_INFO, "reading one client session from stdin");

    char line[MSG_LINE_MAX + 1];
    while (!client_quit && fgets(line, sizeof line, stdin) != NULL) {
        /* fgets stops at '\n' or when the array is full. No '\n' (and not
         * at end of input) means the line was too long: drop the rest. */
        if (strchr(line, '\n') == NULL && !feof(stdin)) {
            int c;
            while ((c = getchar()) != EOF && c != '\n')
                ;
            reply(":parley 417 %s :Input line too long", target());
            continue;
        }
        str_chomp(line);
        handle_line(line);
    }
    log_msg(LOG_INFO, "session over");
    return 0;
}
```

### parley.c

The client gets the same `str_to_long` and `str_chomp` (copied for now; [Chapter 9](09-preprocessor-and-building.md) moves shared code into its own file). Update the top comment:

`project/ch06/parley.c` (top comment)
```c
/* parley.c - the parley chat client.
 * No network yet (Chapters 5-15). The client reads what you type on stdin and
 * prints the protocol lines it would send to the server on stdout. */
```

Replace `parse_number` with these two functions:

`project/ch06/parley.c` (helpers)
```c
/* Parse a whole decimal number in [min, max] into *out (see parleyd.c). */
static bool str_to_long(const char *s, long min, long max, long *out)
{
    char *end;
    errno = 0;
    long n = strtol(s, &end, 10);
    if (errno != 0 || end == s || *end != '\0' || n < min || n > max)
        return false;
    *out = n;
    return true;
}

/* Remove any trailing '\r' and '\n' characters. */
static void str_chomp(char *s)
{
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r'))
        s[--n] = '\0';
}
```

In `parse_args`, the `-p` case becomes:

`project/ch06/parley.c` (in `parse_args`)
```c
        case 'p':
            if (!str_to_long(value, 1, PORT_MAX, &opt_port))
                usage_error("invalid port '%s' (expected 1-%d)", value, PORT_MAX);
            break;
```

Delete `strip_newline`, which `str_chomp` replaces. In `main`, reject too-long lines before using them; until now, `fgets` split a long line into pieces sent as separate messages:

`project/ch06/parley.c` (`main`)
```c
int main(int argc, char *argv[])
{
    parse_args(argc, argv);
    printf("NICK %s\r\n", opt_nick);

    char line[INPUT_MAX];
    while (!quitting && fgets(line, sizeof line, stdin) != NULL) {
        if (strchr(line, '\n') == NULL && !feof(stdin)) {
            int c;
            while ((c = getchar()) != EOF && c != '\n')
                ;   /* throw away the rest of the long line */
            fprintf(stderr, "parley: line too long (max %zu bytes), not sent\n",
                    sizeof line - 2);
            continue;
        }
        str_chomp(line);
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

`sizeof line - 2` is 510: the buffer's 512 bytes minus room for the `'\n'` and the `'\0'`.

### Checkpoint

Build both programs with both libcs, then play a whole session into the server. `2>/dev/null` hides the stderr log, leaving only the replies:

```console
$ cd project/ch06
$ gcc -std=c99 -Wall -Wextra -pedantic -o parleyd parleyd.c
$ gcc -std=c99 -Wall -Wextra -pedantic -o parley parley.c
$ musl-gcc -std=c99 -Wall -Wextra -pedantic -static -o parleyd-musl parleyd.c
$ musl-gcc -std=c99 -Wall -Wextra -pedantic -static -o parley-musl parley.c
$ printf 'MSG :too early\r\nNICK 9lives\r\nNICK alice\r\nMSG :hello\r\nnick Alice\r\nDANCE\r\nQUIT\r\nMSG :after quit\r\n' | ./parleyd 2>/dev/null
:parley 451 * :You have not registered
:parley 432 * 9lives :Erroneous nickname
:parley 001 alice :Welcome to parley, alice
:alice JOIN
:alice MSG :hello
:alice NICK Alice
:parley 421 Alice DANCE :Unknown command
ERROR :Goodbye
```

Every input line got one answer except the last: after `QUIT` the server stops reading. Lowercase `nick` worked. Now only the log, with `>/dev/null`:

```console
$ printf 'MSG :too early\r\nNICK 9lives\r\nNICK alice\r\nMSG :hello\r\nnick Alice\r\nDANCE\r\nQUIT\r\nMSG :after quit\r\n' | ./parleyd >/dev/null
parleyd: info: reading one client session from stdin
parleyd: info: alice joined
parleyd: info: alice is now Alice
parleyd: info: session over
```

Missing and bad nicknames (17 characters is one too many), with plain `\n` line endings, which `str_chomp` handles too:

```console
$ printf 'NICK\nNICK abcdefghijklmnopq\nNICK bob\n' | ./parleyd 2>/dev/null
:parley 431 * :No nickname given
:parley 432 * abcdefghijklmnopq :Erroneous nickname
:parley 001 bob :Welcome to parley, bob
:bob JOIN
```

A 600-byte message is rejected, and the server carries on with the next line:

```console
$ { printf 'NICK alice\r\nMSG :'; head -c 600 /dev/zero | tr '\0' x; printf '\r\nMSG :still here\r\n'; } | ./parleyd 2>/dev/null
:parley 001 alice :Welcome to parley, alice
:alice JOIN
:parley 417 alice :Input line too long
:alice MSG :still here
```

The client's output is the protocol, so it can talk straight to the server through a pipe. The client turns `/me` into `ME`, which this server doesn't know yet:

```console
$ printf 'hi there\n/me grins\n/quit\n' | ./parley -n alice | ./parleyd 2>/dev/null
:parley 001 alice :Welcome to parley, alice
:alice JOIN
:alice MSG :hi there
:parley 421 alice ME :Unknown command
ERROR :Goodbye
```

The client refuses a line that is too long, and sends the next one:

```console
$ { head -c 600 /dev/zero | tr '\0' y; printf '\nok\n'; } | ./parley -n alice
NICK alice
parley: line too long (max 510 bytes), not sent
MSG :ok
QUIT
```

The client's limit counts only what you type, but the server echoes the text with `:alice ` in front. A 505-byte message makes a `MSG` line of exactly 512 bytes, which the server accepts, but the reply doesn't fit, and `reply` says so:

```console
$ { head -c 505 /dev/zero | tr '\0' y; printf '\n'; } | ./parley -n alice | ./parleyd >/dev/null
parleyd: info: reading one client session from stdin
parleyd: info: alice joined
parleyd: warning: reply truncated from 517 to 510 bytes
parleyd: info: session over
```

The truncated reply is still a valid, terminated line, so nothing worse happens; that's what checking `snprintf`'s result buys. (For `alice`, typed messages of 499–505 bytes get a truncated echo; 506–510 bytes pass the client but exceed 512 on the wire, so the server answers 417; the client refuses 511 and up.)

The musl builds give the same replies:

```console
$ printf 'MSG :too early\r\nNICK 9lives\r\nNICK alice\r\nMSG :hello\r\nnick Alice\r\nDANCE\r\nQUIT\r\n' | ./parleyd-musl 2>/dev/null
:parley 451 * :You have not registered
:parley 432 * 9lives :Erroneous nickname
:parley 001 alice :Welcome to parley, alice
:alice JOIN
:alice MSG :hello
:alice NICK Alice
:parley 421 Alice DANCE :Unknown command
ERROR :Goodbye
$ printf 'hi there\n/me grins\n/quit\n' | ./parley-musl -n alice | ./parleyd-musl 2>/dev/null
:parley 001 alice :Welcome to parley, alice
:alice JOIN
:alice MSG :hi there
:parley 421 alice ME :Unknown command
ERROR :Goodbye
```

### glibc vs musl: output order

So far we've looked at stdout and stderr separately. On a terminal you see both, in the natural order, with either libc:

```console
$ printf 'NICK alice\r\nQUIT\r\n' | ./parleyd
parleyd: info: reading one client session from stdin
:parley 001 alice :Welcome to parley, alice
:alice JOIN
parleyd: info: alice joined
ERROR :Goodbye
parleyd: info: session over
```

Now send both streams into a pipe, as a log collector or test script would. `2>&1` joins stderr to stdout, and `| cat` makes stdout a pipe instead of the terminal:

```console
$ printf 'NICK alice\r\nQUIT\r\n' | ./parleyd 2>&1 | cat
parleyd: info: reading one client session from stdin
parleyd: info: alice joined
parleyd: info: session over
:parley 001 alice :Welcome to parley, alice
:alice JOIN
ERROR :Goodbye
$ printf 'NICK alice\r\nQUIT\r\n' | ./parleyd-musl 2>&1 | cat
parleyd: info: reading one client session from stdin
:parley 001 alice :Welcome to parley, alice
parleyd: info: alice joined
parleyd: info: session over
:alice JOIN
ERROR :Goodbye
```

Three orders from one program. Each stream's bytes are identical; only *when* they're written differs. stderr is unbuffered, so log lines go out at once. stdout is **line-buffered** on a terminal (written at each `'\n'`) and **fully buffered** otherwise (written when the buffer fills or the program ends). C99 requires full buffering only when the stream "can be determined not to refer to an interactive device"; the details are the libc's.

- **glibc** decides before the first write: a pipe means fully buffered from the start, so all replies wait until `main` returns, and every log line comes first.
- **musl** starts line-buffered and checks for a terminal during its first write, so the first line, `:parley 001`, is already out; after that, stdout is fully buffered.

glibc's way is conventional; musl's is simpler code (one check inside the first write) at the cost of one early line. Neither is a bug, and that's the lesson: **never rely on the order between two streams.** If order matters, `fflush(stdout)` at the right moment, or use one stream. [Chapter 12](12-files-and-descriptors.md) covers buffering; the server stops using stdout for the protocol once it has sockets ([Chapter 15](15-a-first-server.md)).

The complete project at this point is in `project/ch06/`.

## Summary

- A C string is a `char` array ending in `'\0'`. `strlen` counts the bytes before it; `sizeof` gives the array size. Without a terminator, every string function reads past the end.
- String literals are read-only though typed `char[N]`. Point at them with `const char *`; `-Wwrite-strings` catches mistakes.
- `<string.h>` functions trust you. Compare with `strcmp`, not `==`, and test its sign, not its value. Use `memmove` for overlapping copies. Avoid `strtok`.
- `strcpy`, `strcat`, `sprintf` and `gets` can't know the destination's size; `strncpy` may leave it unterminated. glibc's stack protector and fortify stop some overflows with a message; musl just crashes. Neither replaces correct code.
- `snprintf` never overflows and returns the length it wanted: `n >= size` means truncated, `n < 0` an error.
- `str_copy`/`str_append` (our `strlcpy`/`strlcat`) always terminate and report truncation. musl has `strlcpy`; glibc only since 2.38.
- Check all of `strtol`'s results: `end == s`, `*end != '\0'`, `errno == ERANGE`, and your range. Avoid `atoi`; don't trust `strtoul` with negative input.
- Cast to `unsigned char` before `<ctype.h>` calls. glibc hides the bug for negative values; musl doesn't.
- `strcasecmp` is POSIX (`<strings.h>`). A small ASCII-only `str_ieq` is portable and locale-proof.
- `printf("%s", NULL)` is undefined: it prints `(null)` or crashes when gcc makes it `puts`, depending on libc and `-O` level.
- UTF-8 passes through byte-based string code untouched, but lengths are bytes, not characters or screen columns.
- `fgets` is the safe line reader. No `'\n'` before end of input means the line was too long; skip the rest.
- stdout is line-buffered on a terminal, fully buffered otherwise, so its order relative to stderr varies, and differs between glibc and musl.

## Exercises

1. **Your own `strstr`.** Write `char *my_strstr(const char *hay, const char *needle)` returning a pointer to the first `needle` in `hay`, or `NULL`. An empty `needle` is found at the start of `hay`, as with `strstr`. Compare with `strstr` on tricky cases, including `"aab"` in `"aaab"`, where the first partial match fails.

<details><summary>Solution</summary>

```c
#include <stdio.h>
#include <string.h>

/* First occurrence of needle in hay, or NULL. "" is found at hay. */
static char *my_strstr(const char *hay, const char *needle)
{
    size_t n = strlen(needle);
    for (; *hay != '\0'; hay++)
        if (strncmp(hay, needle, n) == 0)
            return (char *)hay;
    return n == 0 ? (char *)hay : NULL;
}

int main(void)
{
    const char *tests[][2] = {
        { "hello alice", "alice" }, { "hello alice", "bob" },
        { "aaab", "aab" }, { "abc", "" }, { "", "" }, { "ab", "abc" },
    };
    for (size_t i = 0; i < sizeof tests / sizeof tests[0]; i++) {
        const char *h = tests[i][0], *nd = tests[i][1];
        char *mine = my_strstr(h, nd), *real = strstr(h, nd);
        printf("\"%s\" in \"%s\": %s %s\n", nd, h,
               mine ? mine : "NULL", mine == real ? "ok" : "WRONG");
    }
    return 0;
}
```

```console
$ ./strstr
"alice" in "hello alice": alice ok
"bob" in "hello alice": NULL ok
"aab" in "aaab": aab ok
"" in "abc": abc ok
"" in "":  ok
"abc" in "ab": NULL ok
```

`strncmp(hay, needle, n)` stops at the first difference or a `'\0'` in either string, so it never reads past `hay`'s end. The final `return` handles `"" in ""`, where the loop never runs. This simple method can take about `strlen(hay) * strlen(needle)` steps at worst; the C libraries use cleverer algorithms.

</details>

2. **Reverse in place.** Write `void str_reverse(char *s)` that reverses a string in place with two pointers. It must work for `""` and one-character strings.

<details><summary>Solution</summary>

```c
#include <stdio.h>
#include <string.h>

static void str_reverse(char *s)
{
    size_t n = strlen(s);
    if (n < 2)
        return;
    for (char *lo = s, *hi = s + n - 1; lo < hi; lo++, hi--) {
        char tmp = *lo;
        *lo = *hi;
        *hi = tmp;
    }
}

int main(void)
{
    char a[] = "parley", b[] = "ab", c[] = "x", d[] = "";
    str_reverse(a);
    str_reverse(b);
    str_reverse(c);
    str_reverse(d);
    printf("\"%s\" \"%s\" \"%s\" \"%s\"\n", a, b, c, d);
    return 0;
}
```

```console
$ ./reverse
"yelrap" "ba" "x" ""
```

The early `return` matters: for `""`, `s + n - 1` would point *before* the array, undefined even without a dereference. Reversing bytes garbles multi-byte UTF-8 characters.

</details>

3. **Words without `strtok`.** Write `size_t count_words(const char *s)` that prints each word of `s` in brackets and returns the word count. Words are separated by runs of spaces, tabs, `'\r'` or `'\n'`. Use `strspn` and `strcspn`; don't modify the string. (Hint: `printf("%.*s", len, p)` prints at most `len` bytes from `p`.)

<details><summary>Solution</summary>

```c
#include <stdio.h>
#include <string.h>

static const char SPACES[] = " \t\r\n";

static size_t count_words(const char *s)
{
    size_t n = 0;
    for (;;) {
        s += strspn(s, SPACES);         /* skip blanks */
        if (*s == '\0')
            return n;
        size_t len = strcspn(s, SPACES);  /* length of this word */
        printf("[%.*s]", (int)len, s);
        n++;
        s += len;
    }
}

int main(void)
{
    const char *lines[] = { "NICK alice", "  MSG   :hi  there\r\n", "", "   " };
    for (size_t i = 0; i < sizeof lines / sizeof lines[0]; i++)
        printf(" -> %zu words\n", count_words(lines[i]));
    return 0;
}
```

```console
$ ./words
[NICK][alice] -> 2 words
[MSG][:hi][there] -> 3 words
 -> 0 words
 -> 0 words
```

`strspn` skips blanks and `strcspn` measures non-blanks, so the loop alternates between them. Unlike `strtok`, it works on `const` strings with no hidden state. The `*` precision takes an `int`, hence the cast.

</details>

4. **`mygrep`.** Write `mygrep [-i] [-n] PATTERN`, printing the stdin lines that contain `PATTERN`. `-i` ignores (ASCII) case, `-n` prefixes line numbers. Like `grep`, exit 0 if a line matched, 1 if none did, and 2 with a usage message on bad arguments. Print lines exactly as read.

<details><summary>Solution</summary>

```c
/* mygrep.c - print lines of stdin that contain PATTERN. */
#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

enum { LINE_MAX_LEN = 1024 };

static void to_lower(char *s)
{
    for (; *s != '\0'; s++)
        *s = (char)tolower((unsigned char)*s);
}

int main(int argc, char *argv[])
{
    bool ignore_case = false, number = false;
    int i = 1;
    for (; i < argc && argv[i][0] == '-'; i++) {
        if (strcmp(argv[i], "-i") == 0)
            ignore_case = true;
        else if (strcmp(argv[i], "-n") == 0)
            number = true;
        else
            break;
    }
    if (argc - i != 1) {
        fprintf(stderr, "usage: %s [-i] [-n] PATTERN\n", argv[0]);
        return 2;
    }

    char pattern[LINE_MAX_LEN];
    if ((size_t)snprintf(pattern, sizeof pattern, "%s", argv[i]) >= sizeof pattern) {
        fprintf(stderr, "%s: pattern too long\n", argv[0]);
        return 2;
    }
    if (ignore_case)
        to_lower(pattern);

    char line[LINE_MAX_LEN], copy[LINE_MAX_LEN];
    long lineno = 0;
    bool found = false;
    while (fgets(line, sizeof line, stdin) != NULL) {
        lineno++;
        memcpy(copy, line, strlen(line) + 1);   /* search a copy, print the original */
        if (ignore_case)
            to_lower(copy);
        if (strstr(copy, pattern) != NULL) {
            found = true;
            if (number)
                printf("%ld:", lineno);
            fputs(line, stdout);
        }
    }
    return found ? 0 : 1;
}
```

```console
$ printf 'NICK alice\nMSG :Hello\nmsg :hello again\nQUIT\n' > in.txt
$ ./mygrep -i -n hello < in.txt
2:MSG :Hello
3:msg :hello again
$ ./mygrep Hello < in.txt
MSG :Hello
$ ./mygrep bob < in.txt; echo "exit $?"
exit 1
$ ./mygrep; echo "exit $?"
usage: ./mygrep [-i] [-n] PATTERN
exit 2
```

To ignore case, the program lowers a *copy* of each line, keeping the original for printing; `memcpy` with `strlen(line) + 1` copies the terminator too. The pattern goes through `snprintf` so a huge argument can't overflow `pattern`. `fgets` splits lines over 1023 bytes, missing matches across the split; growing the buffer fixes that ([Chapter 8](08-memory.md)).

</details>

5. **Host and port.** The client will need to split a server address like `127.0.0.1:6667`. Write `bool parse_hostport(const char *s, char *host, size_t hsize, long *port)`, splitting at the *last* `':'`. The host must be non-empty and fit in `hsize` bytes with its terminator; the port must be a whole number from 1 to 65535. On failure, leave `host` and `*port` unchanged.

<details><summary>Solution</summary>

```c
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Split "HOST:PORT" into host (copied into a buffer of size hsize) and port. */
static bool parse_hostport(const char *s, char *host, size_t hsize, long *port)
{
    const char *colon = strrchr(s, ':');
    if (colon == NULL || colon == s)
        return false;
    size_t hlen = (size_t)(colon - s);
    if (hlen >= hsize)
        return false;                    /* host too long */

    char *end;
    errno = 0;
    long p = strtol(colon + 1, &end, 10);
    if (errno != 0 || end == colon + 1 || *end != '\0' || p < 1 || p > 65535)
        return false;

    memcpy(host, s, hlen);
    host[hlen] = '\0';
    *port = p;
    return true;
}

int main(void)
{
    const char *inputs[] = { "127.0.0.1:6667", "localhost:80", "localhost", ":6667",
                             "host:", "host:99999", "host:12ab" };
    for (size_t i = 0; i < sizeof inputs / sizeof inputs[0]; i++) {
        char host[64];
        long port;
        if (parse_hostport(inputs[i], host, sizeof host, &port))
            printf("%-15s host \"%s\" port %ld\n", inputs[i], host, port);
        else
            printf("%-15s invalid\n", inputs[i]);
    }
    return 0;
}
```

```console
$ ./hostport
127.0.0.1:6667  host "127.0.0.1" port 6667
localhost:80    host "localhost" port 80
localhost       invalid
:6667           invalid
host:           invalid
host:99999      invalid
host:12ab       invalid
```

`strrchr` finds the last `':'`, so an IPv6-style host containing colons still splits at the port. The host isn't a separate string inside `s`, so it's copied with `memcpy` and terminated by hand. All checks come before any write, so failure leaves the outputs alone. Every solution gives the same output with musl.

</details>

Sources for this chapter: [REFERENCES.md](REFERENCES.md#chapter-6--strings)
