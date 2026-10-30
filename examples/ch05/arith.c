/* arith.c - pointer arithmetic counts elements, not bytes. */
#include <stddef.h>
#include <stdio.h>

int main(void)
{
    int nums[] = { 10, 20, 30, 40 };
    int *first = nums;
    int *end = nums + 4;                /* one past the last element: valid to
                                           compute and compare, not to read */

    printf("*(first + 2) = %d, first[2] = %d\n", *(first + 2), first[2]);

    for (int *p = first; p < end; p++)  /* walk with a pointer */
        printf("%d ", *p);
    printf("\n");

    ptrdiff_t count = end - first;      /* difference = number of elements */
    printf("end - first = %td elements\n", count);

    int *third = &nums[2];
    printf("third[-1] = %d\n", third[-1]);  /* fine: still inside nums */

    char text[] = "parley";
    char *c = text;
    while (*c != '\0')                  /* walk to the terminating '\0' */
        c++;
    printf("\"%s\" has %td characters\n", text, c - text);
    return 0;
}
