/* point.c - defining a struct, members, copying, and pointers to structs. */
#include <stdio.h>

struct point {          /* a new type: "struct point" */
    int x;
    int y;
};                      /* note the semicolon */

/* Takes a copy: changing p here does not change the caller's point. */
static void move_copy(struct point p, int dx)
{
    p.x += dx;
}

/* Takes a pointer: changes the caller's point. p->x means (*p).x */
static void move(struct point *p, int dx)
{
    p->x += dx;
}

/* Structs can be returned by value, too. */
static struct point midpoint(struct point a, struct point b)
{
    struct point m = { (a.x + b.x) / 2, (a.y + b.y) / 2 };
    return m;
}

int main(void)
{
    struct point a = { 1, 2 };   /* members in declaration order */
    struct point b;
    b.x = 9;
    b.y = 6;

    struct point c = a;          /* assignment copies every member */
    c.x = 100;
    printf("a = (%d, %d), c = (%d, %d)\n", a.x, a.y, c.x, c.y);

    move_copy(a, 10);
    printf("after move_copy: a.x = %d\n", a.x);
    move(&a, 10);
    printf("after move:      a.x = %d\n", a.x);

    struct point m = midpoint(a, b);
    printf("midpoint = (%d, %d)\n", m.x, m.y);

    struct point *p = &b;
    printf("p->x = %d, (*p).y = %d, sizeof(struct point) = %zu\n",
           p->x, (*p).y, sizeof(struct point));
    return 0;
}
