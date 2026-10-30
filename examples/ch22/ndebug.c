/* ndebug.c - what -DNDEBUG does to assert(). */
#include <assert.h>
#include <stdio.h>

static int calls;

static int check_something(void)
{
    calls++;                   /* a side effect */
    return 0;                  /* "the check failed" */
}

int main(void)
{
    assert(check_something() == 1);   /* wrong: the call vanishes with NDEBUG */
    printf("check_something() ran %d time(s)\n", calls);
    return 0;
}
