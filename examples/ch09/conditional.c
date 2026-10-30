/* conditional.c - #if, #ifdef, macros set with -D, and #error. */
#include <stdio.h>

#if !defined(__STDC_VERSION__) || __STDC_VERSION__ < 199901L
#error "this program needs C99 or later: compile with -std=c99"
#endif

#ifndef GREETING_LEVEL
#define GREETING_LEVEL 1        /* the default; override with -DGREETING_LEVEL=2 */
#endif

#ifdef DEBUG
#define DPRINT(...) fprintf(stderr, __VA_ARGS__)
#else
#define DPRINT(...) ((void)0)   /* compiles to nothing at all */
#endif

int main(void)
{
    int users = 3;
    DPRINT("debug: users = %d\n", users);

#if GREETING_LEVEL >= 2
    printf("Hello, and welcome! %d users are here.\n", users);
#elif GREETING_LEVEL == 1
    printf("hello (%d users)\n", users);
#else
    (void)users;
#endif

#ifdef __linux__
    puts("built for Linux");
#endif
    return 0;
}
