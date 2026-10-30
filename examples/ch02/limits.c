/* limits.c - the size and range of each integer type on this machine. */
#include <limits.h>
#include <stdio.h>

int main(void)
{
    printf("CHAR_BIT  %d\n", CHAR_BIT);
    printf("char      %zu byte   %d .. %d\n", sizeof(char), CHAR_MIN, CHAR_MAX);
    printf("short     %zu bytes  %d .. %d\n", sizeof(short), SHRT_MIN, SHRT_MAX);
    printf("int       %zu bytes  %d .. %d\n", sizeof(int), INT_MIN, INT_MAX);
    printf("unsigned  %zu bytes  0 .. %u\n", sizeof(unsigned), UINT_MAX);
    printf("long      %zu bytes  %ld .. %ld\n", sizeof(long), LONG_MIN, LONG_MAX);
    printf("unsigned long       0 .. %lu\n", ULONG_MAX);
    printf("long long %zu bytes  %lld .. %lld\n", sizeof(long long), LLONG_MIN, LLONG_MAX);
    printf("unsigned long long  0 .. %llu\n", ULLONG_MAX);
    return 0;
}
