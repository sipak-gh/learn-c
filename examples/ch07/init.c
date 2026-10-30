/* init.c - typedef, designated initializers and compound literals (C99). */
#include <stdio.h>

typedef struct {        /* typedef: "rect" is now a name for this type */
    int x, y;
    int w, h;
    const char *label;
} rect;

enum color { RED, GREEN, BLUE, NCOLORS };

/* Designated initializers work for array indexes too: the table stays
 * right even if someone reorders the enum. */
static const char *const color_names[NCOLORS] = {
    [BLUE] = "blue",
    [RED] = "red",
    [GREEN] = "green",
};

static int area(const rect *r)
{
    return r->w * r->h;
}

static void show(const rect *r)
{
    printf("%-6s at (%d,%d) %dx%d, area %d\n",
           r->label ? r->label : "(none)", r->x, r->y, r->w, r->h, area(r));
}

int main(void)
{
    rect a = { 0, 0, 4, 3, "a" };              /* positional: easy to get wrong */
    rect b = { .w = 5, .h = 2, .label = "b" }; /* designated: x and y are 0 */
    rect c = { 0 };                            /* everything zero / NULL */
    show(&a);
    show(&b);
    show(&c);

    /* A compound literal is an unnamed object: (type){ initializers }.
     * Here it lives until the end of main's block. */
    show(&(rect){ .x = 1, .y = 1, .w = 2, .h = 2, .label = "tmp" });

    b = (rect){ .label = "reset" };            /* assign a whole new value */
    show(&b);

    int *nums = (int[]){ 3, 1, 4 };            /* array compound literal */
    printf("nums[2] = %d\n", nums[2]);

    for (int i = 0; i < NCOLORS; i++)
        printf("color %d is %s\n", i, color_names[i]);
    return 0;
}
