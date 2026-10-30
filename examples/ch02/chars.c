/* chars.c - a char is a small integer; whether it is signed depends on the platform. */
#include <limits.h>
#include <stdio.h>

int main(void)
{
    char letter = 'A';
    printf("%c is %d; next is %c\n", letter, letter, letter + 1);
    printf("'a' - 'A' = %d, '7' - '0' = %d\n", 'a' - 'A', '7' - '0');

    int big = 200;
    char c = (char)big;                     /* doesn't fit if char is signed */
    unsigned char uc = (unsigned char)big;
    signed char sc = (signed char)big;
    printf("char %d, unsigned char %d, signed char %d\n", c, uc, sc);
    printf("CHAR_MIN = %d, CHAR_MAX = %d\n", CHAR_MIN, CHAR_MAX);
    return 0;
}
