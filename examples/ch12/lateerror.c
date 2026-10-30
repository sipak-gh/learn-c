/* lateerror.c - stdio reports a write error late: when the buffer is
 * written, not when you call printf. */
#include <errno.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    int n = printf("hello\n");
    fprintf(stderr, "printf returned %d\n", n);
    n = printf("world\n");
    fprintf(stderr, "printf returned %d\n", n);
    if (fflush(stdout) == EOF) {
        fprintf(stderr, "fflush failed: %s\n", strerror(errno));
        return 1;
    }
    fprintf(stderr, "fflush succeeded\n");
    return 0;
}
