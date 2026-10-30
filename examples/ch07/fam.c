/* fam.c - a flexible array member: a struct whose last member is an array
 * with no size. You allocate the header and the array in one block. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct name {
    size_t len;
    char text[];        /* flexible array member: must be last */
};

static struct name *name_new(const char *s)
{
    size_t len = strlen(s);
    struct name *n = malloc(sizeof *n + len + 1);   /* header + chars + '\0' */
    if (n == NULL)
        return NULL;
    n->len = len;
    memcpy(n->text, s, len + 1);
    return n;
}

int main(void)
{
    struct name *n = name_new("alice");
    if (n == NULL) {
        perror("malloc");
        return 1;
    }
    printf("sizeof(struct name) = %zu\n", sizeof(struct name));
    printf("%s has %zu letters\n", n->text, n->len);
    free(n);
    return 0;
}
