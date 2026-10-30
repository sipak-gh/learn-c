/* whoami.c - which C standard and which C library was this built with? */
#include <stdio.h>

int main(void)
{
    /* __STDC_VERSION__ is set by the compiler: 199901L means C99. */
    printf("C standard: %ld\n", __STDC_VERSION__);

    /* #ifdef keeps or drops lines before compiling (Chapter 9).
     * glibc's headers define __GLIBC__; musl defines no such macro. */
#ifdef __GLIBC__
    printf("C library:  glibc %d.%d\n", __GLIBC__, __GLIBC_MINOR__);
#else
    printf("C library:  not glibc (musl has no version macro, on purpose)\n");
#endif
    return 0;
}
