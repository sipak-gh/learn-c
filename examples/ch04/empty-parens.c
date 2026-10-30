/* empty-parens.c - why f() is not f(void) in C. This program has a bug
 * that the compiler does not report with -Wall -Wextra -pedantic. */
#include <stdio.h>

int add();                          /* "unknown parameters": no checking */

int main(void)
{
    printf("add(1) = %d\n", add(1));    /* one argument too few */
    return 0;
}

int add(int a, int b)
{
    return a + b;
}
