/* statements.c - expressions, statements, blocks and scope. */
#include <stdio.h>

int main(void)
{
    int x = 1;                  /* a declaration */
    x = x + 1;                  /* an expression statement: expression + ; */
    printf("x = %d\n", x);      /* a function call is an expression too */
    ;                           /* the null statement: does nothing */

    {                           /* a block (compound statement) opens a scope */
        int x = 100;            /* a new x that hides the outer one */
        int y = x + 1;
        printf("inner x = %d, y = %d\n", x, y);
    }                           /* the inner x and y end here */

    printf("outer x = %d\n", x);
    return 0;
}
