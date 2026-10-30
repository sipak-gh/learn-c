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
