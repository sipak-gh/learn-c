/* noreturn.c - main without a return statement. */
#include <stdio.h>

int main(void)
{
    printf("hello, world\n");
    /* No return: in C99, reaching the end of main returns 0. */
}
