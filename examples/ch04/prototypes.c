/* prototypes.c - declare a function before you use it, define it anywhere. */
#include <stdio.h>

int square(int x);                  /* declaration (prototype): name, types */
static void greet(const char *name);

int main(void)
{
    greet("parley");
    printf("square(7) = %d\n", square(7));
    return 0;
}

int square(int x)                   /* definition: the body */
{
    return x * x;
}

static void greet(const char *name)
{
    printf("hello, %s\n", name);
}
