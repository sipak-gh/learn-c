/* addresses.c - where different kinds of objects live in memory. */
#include <stdio.h>

int global = 1;                         /* file scope: lives for the whole run */

static void stack_depth(int level)
{
    int local = level;
    printf("  level %d local at %p\n", level, (void *)&local);
    if (level < 3)
        stack_depth(level + 1);
}

int main(void)
{
    static int counter = 0;             /* static local: lives for the whole run */
    int local = 2;                      /* automatic: lives until main returns */
    const char *literal = "hello";      /* the literal's bytes are read-only */

    printf("global   %p\n", (void *)&global);
    printf("static   %p\n", (void *)&counter);
    printf("literal  %p\n", (void *)literal);
    printf("local    %p\n", (void *)&local);
    printf("nested calls (the stack grows down):\n");
    stack_depth(1);
    return 0;
}
