/* matrix.c - two-dimensional arrays are arrays of arrays. */
#include <stdio.h>

enum { ROWS = 3, COLS = 4 };

/* The column count must be known; the row count is passed. */
static void print_grid(int rows, int grid[][COLS])
{
    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < COLS; c++)
            printf("%3d", grid[r][c]);
        printf("\n");
    }
}

int main(void)
{
    int grid[ROWS][COLS];
    for (int r = 0; r < ROWS; r++)
        for (int c = 0; c < COLS; c++)
            grid[r][c] = r * 10 + c;

    print_grid(ROWS, grid);
    printf("sizeof grid = %zu, sizeof grid[0] = %zu\n", sizeof grid, sizeof grid[0]);

    /* Row-major: the rows are stored one after another. */
    const int *flat = &grid[0][0];
    printf("element 5 in memory order = %d (grid[1][1])\n", flat[5]);

    /* An array of pointers can have rows of different lengths. */
    const char *menu[] = { "nick", "me", "quit" };
    printf("menu[2][0] = %c\n", menu[2][0]);
    return 0;
}
