/* features.c - which feature-test macros are in effect, and which libc? */
#include <stdio.h>   /* any libc header sets up the libc's feature macros */

int main(void)
{
#ifdef __STRICT_ANSI__
    puts("__STRICT_ANSI__   defined: strict ISO C, no extensions");
#endif
#ifdef _POSIX_C_SOURCE
    printf("_POSIX_C_SOURCE   %ldL\n", (long)_POSIX_C_SOURCE);
#else
    puts("_POSIX_C_SOURCE   not defined");
#endif
#ifdef _XOPEN_SOURCE
    printf("_XOPEN_SOURCE     %d\n", (int)_XOPEN_SOURCE);
#endif
#ifdef _DEFAULT_SOURCE
    puts("_DEFAULT_SOURCE   defined");
#endif
#ifdef _BSD_SOURCE
    puts("_BSD_SOURCE       defined");
#endif
#ifdef __GLIBC__
    printf("C library         glibc %d.%d\n", __GLIBC__, __GLIBC_MINOR__);
#else
    puts("C library         not glibc (musl defines no macro to say so)");
#endif
    return 0;
}
