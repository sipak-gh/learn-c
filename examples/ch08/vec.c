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
