/* literal.c - string literals are read-only; arrays are copies you own. */
#include <stdio.h>

int main(void)
{
    char copy[] = "hello";        /* an array, initialised from the literal */
    const char *lit = "hello";    /* a pointer to the literal itself */
    char *bad = "hello";          /* legal C, but the target is read-only */

    copy[0] = 'J';
    printf("copy = %s, lit = %s\n", copy, lit);
    printf("sizeof copy = %zu, sizeof lit = %zu\n", sizeof copy, sizeof lit);
    fflush(stdout);

    bad[0] = 'J';                 /* undefined behaviour: crashes on Linux */
    printf("bad = %s\n", bad);
    return 0;
}
