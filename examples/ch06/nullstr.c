/* nullstr.c - printing a NULL string is undefined behaviour. */
#include <stdio.h>

int main(int argc, char *argv[])
{
    /* NULL, but the compiler cannot know that while compiling. */
    const char *nick = argc > 1 ? argv[1] : NULL;

    printf("nick: %s\n", nick);       /* UB when nick is NULL */
    fflush(stdout);
    printf("%s\n", nick);             /* gcc turns this into puts(nick) */
    printf("survived\n");
    return 0;
}
