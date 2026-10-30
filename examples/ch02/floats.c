/* floats.c - floating point is binary and approximate. */
#include <float.h>
#include <stdio.h>

int main(void)
{
    double sum = 0.1 + 0.2;
    printf("0.1 + 0.2 = %f (%%f)\n", sum);
    printf("0.1 + 0.2 = %.17g (%%.17g)\n", sum);
    printf("0.1 + 0.2 == 0.3 is %d\n", sum == 0.3);
    printf("0.1 in hex: %a\n", 0.1);

    float f = 16777217;                     /* 2^24 + 1: too many digits for float */
    printf("float 16777217 -> %.1f\n", f);
    printf("FLT_DIG %d, DBL_DIG %d, DBL_EPSILON %g, DBL_MAX %g\n",
           FLT_DIG, DBL_DIG, DBL_EPSILON, DBL_MAX);

    double zero = 0.0;
    printf("1/0.0 = %f, -1/0.0 = %f, 0/0.0 = %f\n", 1 / zero, -1 / zero, zero / zero);
    printf("%e, %g, %g\n", 6.02e23, 0.0001, 1e20);
    return 0;
}
