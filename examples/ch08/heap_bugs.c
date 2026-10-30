/* heap_bugs.c - break the heap rules on purpose (all undefined behaviour).
 * Usage: ./heap_bugs double-free | overflow | use-after-free | leak */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char *argv[])
{
    const char *bug = argc > 1 ? argv[1] : "";
    char *p = malloc(16);
    char *q = malloc(16);
    if (p == NULL || q == NULL)
        return 1;
    printf("bug: %s\n", bug);
    fflush(stdout);                  /* print it before a possible crash */

    if (strcmp(bug, "double-free") == 0) {
        free(p);
        free(p);                     /* freeing the same block twice */
    } else if (strcmp(bug, "overflow") == 0) {
        memset(p, 'A', 64);          /* writes 48 bytes past the end of p */
        free(p);
        free(q);
    } else if (strcmp(bug, "use-after-free") == 0) {
        free(p);
        strcpy(p, "still here?");    /* writing to memory we gave back */
        printf("p says: %s\n", p);
        free(q);
    } else if (strcmp(bug, "leak") == 0) {
        p = NULL;                    /* the only pointer to the block is gone */
        free(q);
    } else {
        free(p);
        free(q);
    }
    printf("survived\n");
    return 0;
}
