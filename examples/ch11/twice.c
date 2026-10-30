/* twice.c - two traps in check macros: arguments evaluated twice, and an
 * "if" inside a macro that steals the caller's "else". */
#include <stdio.h>

#define CHECK_INT_BAD(a, b)                                             \
    if ((a) != (b))                                                     \
        printf("%d: %s == %s: %d != %d\n", __LINE__, #a, #b, (a), (b))

#define CHECK_INT(a, b)                                                 \
    do {                                                                \
        long long a_ = (a), b_ = (b);                                   \
        if (a_ != b_)                                                   \
            printf("%d: %s == %s: %lld != %lld\n", __LINE__, #a, #b,    \
                   a_, b_);                                             \
    } while (0)

static int counter;

static int next_id(void)
{
    return ++counter;
}

int main(void)
{
    puts("bad macro:");
    CHECK_INT_BAD(next_id(), 1);
    CHECK_INT_BAD(next_id(), 1);       /* fails: but which value is shown? */
    printf("next_id() was called %d times\n", counter);

    counter = 0;
    puts("good macro:");
    CHECK_INT(next_id(), 1);
    CHECK_INT(next_id(), 1);
    printf("next_id() was called %d times\n", counter);
    return 0;
}
