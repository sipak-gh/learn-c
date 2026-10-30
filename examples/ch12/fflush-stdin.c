/* fflush-stdin.c - fflush() on an input stream. C99 leaves it undefined;
 * POSIX defines it only for files you can seek in. */
#include <stdio.h>

int main(void)
{
    char line[64];

    if (fgets(line, sizeof line, stdin) != NULL)
        printf("first:  %s", line);
    printf("fflush(stdin) returned %d\n", fflush(stdin));
    if (fgets(line, sizeof line, stdin) != NULL)
        printf("second: %s", line);
    else
        printf("second: nothing left\n");
    return 0;
}
