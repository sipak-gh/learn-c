/* posix-hidden.c - POSIX functions need _POSIX_C_SOURCE under -std=c99. */
#define _POSIX_C_SOURCE 200809L     /* must come before every #include */
#include <stdio.h>
#include <string.h>                 /* strdup: POSIX, not C99 */
#include <unistd.h>                 /* getopt: POSIX, not C99 */

int main(int argc, char **argv)
{
    int opt;
    while ((opt = getopt(argc, argv, "v")) != -1)
        printf("option -%c\n", opt);
    char *copy = strdup("a copy of a string");
    printf("%s\n", copy);
    return 0;                       /* (the copy should be freed: Chapter 8) */
}
