/* qsort-test.c - a test that passes with one C library and fails with
 * another. qsort() need not keep equal elements in their original order. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct user {
    int idle;            /* minutes since the user last spoke */
    int joined;          /* order of joining: 1, 2, 3, ... */
    const char *nick;
};

/* Sort by idle time only. Users with the same idle time are "equal". */
static int by_idle(const void *a, const void *b)
{
    const struct user *x = a, *y = b;
    return (x->idle > y->idle) - (x->idle < y->idle);
}

/* Sort by idle time, then by join order: no two users are ever equal. */
static int by_idle_then_joined(const void *a, const void *b)
{
    const struct user *x = a, *y = b;
    int c = by_idle(a, b);
    if (c != 0)
        return c;
    return (x->joined > y->joined) - (x->joined < y->joined);
}

int main(int argc, char *argv[])
{
    struct user users[] = {
        { 5, 1, "alice" }, { 0, 2, "bob" }, { 5, 3, "carol" }, { 0, 4, "dave" },
        { 5, 5, "erin" },  { 0, 6, "frank" }, { 5, 7, "grace" }, { 0, 8, "heidi" },
    };
    /* What the test expects: idle 0 first, each group in join order. */
    const char *want[] = { "bob", "dave", "frank", "heidi",
                           "alice", "carol", "erin", "grace" };
    size_t n = sizeof users / sizeof users[0];
    int fixed = argc > 1 && strcmp(argv[1], "fixed") == 0;
    int failed = 0;

    qsort(users, n, sizeof users[0], fixed ? by_idle_then_joined : by_idle);
    for (size_t i = 0; i < n; i++) {
        if (strcmp(users[i].nick, want[i]) != 0) {
            printf("position %zu: got %s, want %s\n", i, users[i].nick, want[i]);
            failed = 1;
        }
    }
    puts(failed ? "FAIL" : "ok");
    return failed;
}
