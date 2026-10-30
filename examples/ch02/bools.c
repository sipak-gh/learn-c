/* bools.c - _Bool, <stdbool.h>, and what counts as true. */
#include <stdbool.h>
#include <stdio.h>

int main(void)
{
    bool ready = false;
    _Bool done = 1;                  /* the keyword behind bool */
    printf("ready %d, done %d, sizeof(bool) %zu\n", ready, done, sizeof(bool));

    int n = 256;
    bool b = n;                      /* any non-zero value becomes 1 */
    unsigned char c = (unsigned char)n;  /* keeps only the low 8 bits: 0 */
    printf("bool from 256: %d, unsigned char from 256: %d\n", b, c);

    printf("5 > 3 is %d, 5 == 3 is %d, !7 is %d, !!7 is %d\n", 5 > 3, 5 == 3, !7, !!7);
    return 0;
}
