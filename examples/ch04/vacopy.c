/* vacopy.c - using one va_list twice needs va_copy. */
#include <stdarg.h>
#include <stdio.h>

/* Write the same formatted line to two streams. */
static void vtee(FILE *a, FILE *b, const char *fmt, va_list ap)
{
    va_list ap2;
    va_copy(ap2, ap);               /* a second, independent cursor */
    vfprintf(a, fmt, ap);           /* uses up ap */
    vfprintf(b, fmt, ap2);          /* uses the copy */
    va_end(ap2);                    /* every va_copy needs a va_end */
}

static void tee(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vtee(stdout, stderr, fmt, ap);
    va_end(ap);
}

int main(void)
{
    tee("%s has %d unread messages\n", "alice", 3);
    return 0;
}
