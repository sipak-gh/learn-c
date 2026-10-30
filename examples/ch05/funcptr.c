/* funcptr.c - pointers to functions, callbacks and a dispatch table. */
#include <stdio.h>
#include <string.h>

static int add(int a, int b) { return a + b; }
static int sub(int a, int b) { return a - b; }
static int mul(int a, int b) { return a * b; }

/* A typedef makes the type readable: 'binop' is "function (int, int)
 * returning int", and 'binop *' is a pointer to one. */
typedef int binop(int a, int b);

/* A callback: apply f to every element. */
static void apply(int *v, size_t n, int (*f)(int, int), int arg)
{
    for (size_t i = 0; i < n; i++)
        v[i] = f(v[i], arg);
}

/* A dispatch table: names and functions in parallel arrays. */
static const char *const op_names[] = { "add", "sub", "mul" };
static binop *const op_funcs[] = { add, sub, mul };

static binop *find_op(const char *name)
{
    for (size_t i = 0; i < sizeof op_names / sizeof op_names[0]; i++)
        if (strcmp(name, op_names[i]) == 0)
            return op_funcs[i];
    return NULL;                        /* no such operation */
}

int main(void)
{
    int (*op)(int, int) = add;          /* same as = &add */
    printf("op(2, 3) = %d\n", op(2, 3));    /* same as (*op)(2, 3) */
    op = mul;
    printf("op(2, 3) = %d\n", op(2, 3));

    int v[] = { 1, 2, 3 };
    apply(v, 3, mul, 10);
    printf("after apply: %d %d %d\n", v[0], v[1], v[2]);

    const char *wanted[] = { "sub", "mul", "div" };
    for (int i = 0; i < 3; i++) {
        binop *f = find_op(wanted[i]);
        if (f != NULL)
            printf("%s(12, 4) = %d\n", wanted[i], f(12, 4));
        else
            printf("%s: unknown operation\n", wanted[i]);
    }
    return 0;
}
