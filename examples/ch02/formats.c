/* formats.c - printf conversions, flags, width and precision. */
#include <stdio.h>

int main(void)
{
    printf("[%d] [%5d] [%-5d] [%05d] [%+d] [% d]\n", 42, 42, 42, 42, 42, 42);
    printf("[%u] [%x] [%X] [%#x] [%o] [%#o]\n", 255u, 255u, 255u, 255u, 255u, 255u);
    printf("[%f] [%.2f] [%8.3f] [%e] [%g] [%g]\n", 3.14159, 3.14159, 3.14159,
           3.14159, 0.0001, 1e20);
    printf("[%s] [%8s] [%-8s] [%.3s]\n", "parley", "parley", "parley", "parley");
    printf("[%c] [%*d] [%%]\n", 'P', 6, 42);
    printf("[%hhd] [%ld] [%lld] [%zu]\n", (signed char)-5, 123456789L, 1LL << 40, sizeof(long));
    return 0;
}
