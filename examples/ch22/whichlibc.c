/* whichlibc.c - can a program tell which C library it was built with? */
#include <stdio.h>
#include <stdlib.h>         /* glibc defines __GLIBC__ in every header */

int main(void)
{
#ifdef __GLIBC__
    printf("glibc %d.%d\n", __GLIBC__, __GLIBC_MINOR__);
#else
    printf("not glibc: musl, a BSD libc, ... (no way to tell)\n");
#endif
#ifdef __linux__
    printf("compiled for Linux\n");
#endif
    return 0;
}
