/* args.c - main's parameters and its return value. */
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)     /* same as: char *argv[] */
{
    if (argc < 2) {
        fprintf(stderr, "usage: %s WORD...\n", argv[0]);
        return EXIT_FAILURE;        /* 1 on Linux: "something went wrong" */
    }
    printf("argc = %d\n", argc);
    for (int i = 0; i < argc; i++)
        printf("argv[%d] = \"%s\"\n", i, argv[i]);
    printf("argv[argc] is %s\n", argv[argc] == NULL ? "NULL" : "not NULL");
    return EXIT_SUCCESS;            /* 0 */
}
