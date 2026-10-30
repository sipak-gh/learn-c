/* roots.c - a function from the math library: sqrt. */
#include <math.h>
#include <stdio.h>

int main(void)
{
    double x;

    if (scanf("%lf", &x) != 1) {                 /* %lf reads a double */
        fprintf(stderr, "roots: expected a number\n");
        return 1;
    }
    printf("sqrt(%g) = %g\n", x, sqrt(x));       /* printf uses %g/%f for double */
    return 0;
}
