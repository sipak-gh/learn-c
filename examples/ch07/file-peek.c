/* file-peek.c - is FILE an open struct or an opaque one? It depends on the
 * libc: this compiles with glibc and fails, on purpose, with musl. */
#include <stdio.h>

int main(void)
{
    printf("sizeof(FILE) = %zu\n", sizeof(FILE));
    printf("fd of stdout = %d\n", stdout->_fileno);
    return 0;
}
