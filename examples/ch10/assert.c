/* assert.c - assert() checks something that must be true if the code is right. */
#include <assert.h>
#include <stdio.h>
#include <string.h>

/* Copy src into dst of size n. Calling it with n == 0 is a bug in the
 * caller, not a run-time error, so assert() is the right check. */
static void copy_name(char *dst, size_t n, const char *src)
{
    assert(dst != NULL && src != NULL);
    assert(n > 0);
    strncpy(dst, src, n - 1);
    dst[n - 1] = '\0';
}

int main(int argc, char **argv)
{
    char name[10];

    (void)argv;
    copy_name(name, sizeof name, "alice");
    printf("name = %s\n", name);
    copy_name(name, (size_t)(argc - 1), "bob");    /* n == 0 with no arguments */
    printf("name = %s\n", name);
    return 0;
}
