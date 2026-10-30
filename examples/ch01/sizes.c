/* sizes.c - how many bytes each basic type takes on this machine. */
#include <stdio.h>

int main(void)
{
    /* sizeof gives a size in bytes; %zu is the printf format for it. */
    printf("char        %zu\n", sizeof(char));
    printf("short       %zu\n", sizeof(short));
    printf("int         %zu\n", sizeof(int));
    printf("long        %zu\n", sizeof(long));
    printf("long long   %zu\n", sizeof(long long));
    printf("float       %zu\n", sizeof(float));
    printf("double      %zu\n", sizeof(double));
    printf("long double %zu\n", sizeof(long double));
    printf("pointer     %zu\n", sizeof(void *));  /* pointers: Chapter 5 */
    return 0;
}
