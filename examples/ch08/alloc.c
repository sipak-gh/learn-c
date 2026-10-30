/* alloc.c - malloc, calloc, realloc and free, with the checks they need. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* malloc(n * size) without the overflow: returns NULL if n * size does
 * not fit in a size_t, instead of silently allocating too little. */
static void *alloc_array(size_t n, size_t size)
{
    if (size != 0 && n > SIZE_MAX / size)
        return NULL;
    return malloc(n * size);
}

int main(void)
{
    /* malloc: uninitialized memory. sizeof *nums follows nums's type. */
    size_t n = 5;
    int *nums = malloc(n * sizeof *nums);
    if (nums == NULL) {
        perror("malloc");
        return 1;
    }
    for (size_t i = 0; i < n; i++)
        nums[i] = (int)(i * i);

    /* realloc: grow the block. It may move, so use a temporary. */
    int *bigger = realloc(nums, 2 * n * sizeof *nums);
    if (bigger == NULL) {
        free(nums);                  /* the old block is still ours */
        perror("realloc");
        return 1;
    }
    nums = bigger;
    for (size_t i = n; i < 2 * n; i++)
        nums[i] = -1;
    n *= 2;
    for (size_t i = 0; i < n; i++)
        printf("%d ", nums[i]);
    putchar('\n');
    free(nums);

    /* calloc: zeroed memory, and it checks n * size for overflow itself. */
    double *zeros = calloc(4, sizeof *zeros);
    if (zeros == NULL) {
        perror("calloc");
        return 1;
    }
    printf("calloc: %g %g %g %g\n", zeros[0], zeros[1], zeros[2], zeros[3]);
    free(zeros);

    /* An attacker-controlled count: n * size would wrap around to 8. */
    size_t evil = SIZE_MAX / 8 + 2;
    printf("evil * 8 wraps to %zu\n", evil * 8);
    printf("alloc_array(evil, 8) -> %s\n", alloc_array(evil, 8) ? "memory" : "NULL");

    free(NULL);                      /* allowed, does nothing */
    return 0;
}
