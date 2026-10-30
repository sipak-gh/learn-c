/* decay.c - an array is not a pointer, but it turns into one easily. */
#include <stdio.h>

/* 'int a[]' as a parameter means exactly 'int *a': the size is lost,
 * so the caller passes it. */
static int sum(const int *a, size_t n)
{
    int total = 0;
    for (size_t i = 0; i < n; i++)
        total += a[i];
    return total;
}

int main(void)
{
    int arr[5] = { 1, 2, 3, 4, 5 };
    int *p = arr;                       /* arr "decays" to &arr[0] */
    size_t n = sizeof arr / sizeof arr[0];

    printf("sizeof arr = %zu (5 ints), sizeof p = %zu (one pointer)\n",
           sizeof arr, sizeof p);
    printf("elements: %zu\n", n);
    printf("p == &arr[0]: %s\n", p == &arr[0] ? "yes" : "no");
    printf("arr[2] = %d, p[2] = %d\n", arr[2], p[2]);
    printf("sum = %d\n", sum(arr, n));

    int (*whole)[5] = &arr;             /* a pointer to the whole array */
    printf("p + 1 is %td bytes on, whole + 1 is %td bytes on\n",
           (char *)(p + 1) - (char *)p, (char *)(whole + 1) - (char *)whole);
    return 0;
}
