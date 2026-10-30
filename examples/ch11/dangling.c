/* dangling.c - why check macros are wrapped in do { ... } while (0). */
#include <stdio.h>

#define CHECK_BAD(cond) \
    if (!(cond)) printf("%d: CHECK(%s)\n", __LINE__, #cond)

int main(void)
{
    int verbose = 0;
    if (verbose)
        CHECK_BAD(1 + 1 == 2);
    else
        puts("quiet mode");
    return 0;
}
