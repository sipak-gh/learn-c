/* byvalue.c - arguments are copies; results come back with return. */
#include <stdio.h>

static void reset(int n)
{
    n = 0;                          /* changes the local copy only */
    printf("inside reset: n = %d\n", n);
}

static int doubled(int n)
{
    return n * 2;
}

static void say(const char *what)   /* void: returns nothing */
{
    printf("%s\n", what);
}

int main(void)
{
    int count = 5;
    reset(count);
    printf("after reset: count = %d\n", count);

    count = doubled(count);         /* use the result */
    printf("after doubled: count = %d\n", count);

    doubled(count);                 /* legal: the result is thrown away */
    say("done");
    return 0;
}
