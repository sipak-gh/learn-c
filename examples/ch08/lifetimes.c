/* lifetimes.c - the three storage durations: static, automatic, allocated. */
#include <stdio.h>
#include <stdlib.h>

int counter_global = 0;              /* static storage: lives for the whole run */

static int next_id(void)
{
    static int id = 100;             /* static too: keeps its value between calls */
    return id++;
}

static int *make_number(int v)
{
    int *p = malloc(sizeof *p);      /* allocated: lives until free() */
    if (p != NULL)
        *p = v;
    return p;                        /* fine: the heap object outlives this call */
}

int main(void)
{
    int local = 7;                   /* automatic: lives until main returns */
    const char *literal = "hello";   /* string literals are static and read-only */
    int *heap = make_number(42);
    if (heap == NULL)
        return 1;

    int a = next_id();
    int b = next_id();
    printf("ids: %d %d\n", a, b);
    printf("*heap = %d\n", *heap);

    /* Where things live. Addresses change from run to run (see below). */
    printf("static  %p  (literal)\n", (const void *)literal);
    printf("static  %p  (counter_global)\n", (void *)&counter_global);
    printf("heap    %p  (*heap)\n", (void *)heap);
    printf("stack   %p  (local)\n", (void *)&local);

    free(heap);                      /* heap is now a dangling pointer */
    heap = NULL;                     /* so make it obviously invalid */
    return 0;
}
