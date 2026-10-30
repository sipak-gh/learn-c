/* sort.c - qsort calls our comparison function through a pointer. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* qsort passes pointers to two elements, as const void *. Return <0, 0
 * or >0. (x > y) - (x < y) never overflows, unlike x - y. */
static int cmp_int(const void *a, const void *b)
{
    int x = *(const int *)a;
    int y = *(const int *)b;
    return (x > y) - (x < y);
}

/* The elements are 'const char *', so qsort hands us 'const char **'. */
static int cmp_str(const void *a, const void *b)
{
    const char *s = *(const char *const *)a;
    const char *t = *(const char *const *)b;
    return strcmp(s, t);
}

int main(void)
{
    int nums[] = { 42, -7, 1000, 0, 3 };
    size_t n = sizeof nums / sizeof nums[0];
    qsort(nums, n, sizeof nums[0], cmp_int);
    for (size_t i = 0; i < n; i++)
        printf("%d ", nums[i]);
    printf("\n");

    const char *nicks[] = { "carol", "alice", "dave", "bob" };
    size_t m = sizeof nicks / sizeof nicks[0];
    qsort(nicks, m, sizeof nicks[0], cmp_str);
    for (size_t i = 0; i < m; i++)
        printf("%s ", nicks[i]);
    printf("\n");
    return 0;
}
