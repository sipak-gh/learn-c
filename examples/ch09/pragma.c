/* pragma.c - #undef, #pragma and the C99 _Pragma operator. */
#include <stdio.h>

/* A GCC pragma: using these names anywhere below is now a compile error. */
#pragma GCC poison gets sprintf

/* _Pragma("text") does what #pragma text does, but it is an operator, so a
 * macro can produce one. Here: a TODO that shows up as a compiler warning. */
#define DO_PRAGMA(x) _Pragma(#x)
#define TODO(text) DO_PRAGMA(GCC warning text)

TODO("check the port range")

#define LIMIT 10
static const int old_limit = LIMIT;
#undef LIMIT                    /* forget the old value ... */
#define LIMIT 20                /* ... before giving a new one */

int main(void)
{
    printf("old_limit = %d, LIMIT = %d\n", old_limit, LIMIT);
    return 0;
}
