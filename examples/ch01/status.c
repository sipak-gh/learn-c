/* status.c - main's return value becomes the program's exit status. */
#include <stdio.h>
#include <stdlib.h>   /* EXIT_SUCCESS, EXIT_FAILURE */

int main(void)
{
    printf("something went wrong\n");
    return EXIT_FAILURE;    /* 1 on every Unix system */
}
