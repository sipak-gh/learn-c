/* variadic.c - functions that take a variable number of arguments. */
#include <stdarg.h>
#include <stdio.h>

/* The first parameter says how many ints follow. */
static int sum_ints(int count, ...)
{
    va_list ap;
    va_start(ap, count);            /* start after the last named parameter */
    int total = 0;
    for (int i = 0; i < count; i++)
        total += va_arg(ap, int);   /* fetch the next argument as an int */
    va_end(ap);
    return total;
}

/* A list of strings that ends with a null pointer (a "sentinel"). */
static void print_words(const char *first, ...)
{
    va_list ap;
    va_start(ap, first);
    for (const char *w = first; w != NULL; w = va_arg(ap, const char *))
        printf("[%s]", w);
    va_end(ap);
    printf("\n");
}

/* A printf-like function: hand the va_list to vfprintf. */
static void warn(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "variadic: warning: ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
}

int main(void)
{
    printf("%d\n", sum_ints(3, 10, 20, 12));
    printf("%d\n", sum_ints(0));
    print_words("alice", "bob", "carol", (const char *)NULL);
    warn("%d of %d users are %s", 2, 3, "idle");
    return 0;
}
