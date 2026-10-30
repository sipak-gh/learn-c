/* macros.c - object-like and function-like macros, and their traps. */
#include <stdio.h>

#define NICK_MAX 16                     /* object-like: a named constant */
#define BAD_SQUARE(x) x * x             /* no parentheses: wrong */
#define SQUARE(x) ((x) * (x))           /* every use of x, and the whole body */
#define MAX(a, b) ((a) > (b) ? (a) : (b))

/* Several statements that act as one: wrap them in do { ... } while (0). */
#define SWAP_INT(a, b) do { int swap_tmp_ = (a); (a) = (b); (b) = swap_tmp_; } while (0)

static int calls = 0;
static int next(void)
{
    return ++calls;
}

int main(void)
{
    char nick[NICK_MAX + 1] = "alice";
    printf("sizeof nick = %zu\n", sizeof nick);

    printf("BAD_SQUARE(1 + 2) = %d\n", BAD_SQUARE(1 + 2));   /* 1 + 2 * 1 + 2 */
    printf("SQUARE(1 + 2)     = %d\n", SQUARE(1 + 2));

    /* A macro argument is pasted in as text, so it runs once per use. */
    int i = 5;
    int m = MAX(i++, 0);                 /* ((i++) > (0) ? (i++) : (0)) */
    printf("MAX(i++, 0) = %d, and i is now %d (not 6)\n", m, i);
    int n = MAX(next(), 0);
    printf("MAX(next(), 0) = %d, next() was called %d times\n", n, calls);

    int a = 1, b = 2;
    if (a < b)
        SWAP_INT(a, b);                  /* one statement, so the else still fits */
    else
        printf("never printed\n");
    printf("after SWAP_INT: a = %d, b = %d\n", a, b);
    return 0;
}
