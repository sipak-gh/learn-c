/* decls.c - C99 declarations: where you need them, with // comments. */
#include <stdio.h>

int main(void)
{
    int nicks = 3;                    // a // comment runs to the end of the line
    printf("nicks: %d\n", nicks);

    int channels = nicks * 2;         // declared after a statement: fine in C99
    printf("channels: %d\n", channels);

    long long bytes = 6000000000LL;   // long long is new in C99 too
    printf("bytes: %lld\n", bytes);

    int a = 7, b = 2;                 /* several variables in one declaration */
    double ratio = (double)a / b;
    printf("ratio: %f\n", ratio);
    return 0;
}
