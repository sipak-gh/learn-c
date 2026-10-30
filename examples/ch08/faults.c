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
