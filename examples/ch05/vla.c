/* vla.c - variable-length arrays (C99): the size is known only at run time. */
#include <stdio.h>
#include <stdlib.h>

/* A VLA parameter: the column count comes from an earlier parameter. */
static void fill(int rows, int cols, int t[rows][cols])
{
    for (int r = 0; r < rows; r++)
        for (int c = 0; c < cols; c++)
            t[r][c] = (r + 1) * (c + 1);
}

int main(int argc, char **argv)
{
    long n = argc > 1 ? strtol(argv[1], NULL, 10) : 4;
    if (n < 1)
        return 1;

    double samples[n];                  /* n * 8 bytes on the stack */
    for (long i = 0; i < n; i++)
        samples[i] = i * 0.5;
    printf("n = %ld, sizeof samples = %zu, last = %.1f\n",
           n, sizeof samples, samples[n - 1]);

    int table[3][4];
    fill(3, 4, table);
    printf("table[2][3] = %d\n", table[2][3]);
    return 0;
}
