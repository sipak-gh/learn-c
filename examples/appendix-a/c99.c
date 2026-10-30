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
