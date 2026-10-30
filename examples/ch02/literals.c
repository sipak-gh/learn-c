/* literals.c - how to write numbers and characters in C source. */
#include <stdio.h>

int main(void)
{
    printf("decimal 10 = %d, octal 010 = %d, hex 0x10 = %d\n", 10, 010, 0x10);
    printf("0xFF = %d, 0777 = %d\n", 0xFF, 0777);
    printf("sizes: 1 is %zu, 1L is %zu, 1LL is %zu, 1.0f is %zu, 1.0 is %zu\n",
           sizeof 1, sizeof 1L, sizeof 1LL, sizeof 1.0f, sizeof 1.0);
    printf("4000000000U = %u, 1e3 = %f, 2.5e-3 = %f\n", 4000000000U, 1e3, 2.5e-3);
    printf("'A' = %d, '0' = %d, '\\n' = %d, '\\0' = %d\n", 'A', '0', '\n', '\0');
    printf("'\\x41' = %c, '\\101' = %c, sizeof 'A' = %zu\n", '\x41', '\101', sizeof 'A');
    return 0;
}
