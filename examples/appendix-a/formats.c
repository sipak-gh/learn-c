/* formats.c - printf conversions, one line each. */
#include <inttypes.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

int main(int argc, char *argv[])
{
    int i = -42;
    unsigned u = 42;
    long l = LONG_MAX;
    long long ll = LLONG_MIN;
    size_t z = sizeof(long double);
    ptrdiff_t d = -3;
    intmax_t j = INTMAX_MAX;
    uint64_t u64 = UINT64_MAX;
    int32_t i32 = INT32_MIN;
    uint8_t byte = 200;
    double x = 3.14159;
    char buf[8];
    const char *text = argc > 1 ? argv[1] : "a long string";

    printf("[%d] [%i] [%u]\n", i, i, u);
    printf("[%5d] [%-5d] [%05d] [%+d]\n", i, i, i, 42);
    printf("[%o] [%x] [%X] [%#x]\n", u, u, u, u);
    printf("[%ld] [%lld]\n", l, ll);
    printf("[%zu] [%td] [%jd]\n", z, d, j);
    printf("[%" PRIu64 "] [%" PRId32 "] [%" PRIx64 "]\n", u64, i32, u64);
    printf("[%hhu] [%c] [%%]\n", byte, 'A');
    printf("[%f] [%.2f] [%8.3f] [%e] [%g] [%a]\n", x, x, x, x, x, 0.5);
    printf("[%s] [%.3s] [%8s] [%-8s]\n", "hello", "hello", "hi", "hi");
    printf("[%*d] [%.*s]\n", 6, i, 2, "hello");

    int n = snprintf(buf, sizeof buf, "%s", text);
    printf("snprintf wanted %d bytes, buf holds \"%s\"\n", n, buf);

    printf("pointer: %p\n", (void *)&i);       /* the address varies */
    printf("null pointer: %p\n", (void *)NULL);
    return 0;
}
