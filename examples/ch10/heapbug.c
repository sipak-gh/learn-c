/* heapbug.c - four classic heap bugs. Run: ./heapbug overflow|uaf|double|leak */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *make_name(const char *s)
{
    char *p = malloc(strlen(s));          /* BUG: no room for the '\0' */
    if (p != NULL)
        strcpy(p, s);
    return p;
}

int main(int argc, char **argv)
{
    const char *what = argc > 1 ? argv[1] : "";

    if (strcmp(what, "overflow") == 0) {
        char *name = make_name("parley");
        printf("name = %s\n", name);
        free(name);
    } else if (strcmp(what, "uaf") == 0) {
        char *name = malloc(16);
        strcpy(name, "alice");
        free(name);
        printf("after free: %s\n", name);       /* BUG: use after free */
    } else if (strcmp(what, "double") == 0) {
        char *name = malloc(16);
        free(name);
        free(name);                             /* BUG: double free */
        printf("freed twice\n");
    } else if (strcmp(what, "leak") == 0) {
        char *name = malloc(100);
        name = malloc(200);                     /* BUG: the first block is lost */
        free(name);
        printf("leaked 100 bytes\n");
    } else {
        fprintf(stderr, "usage: heapbug overflow|uaf|double|leak\n");
        return 2;
    }
    return 0;
}
