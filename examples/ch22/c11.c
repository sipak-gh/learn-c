/* c11.c - two things C11 added: compile-time checks and type-generic code. */
#include <stdio.h>

#define TYPE_NAME(x) _Generic((x), int: "int", double: "double", default: "other")

struct header {
    char tag[4];
    unsigned int length;
};

/* Checked by the compiler; a failing check stops the build. */
_Static_assert(sizeof(struct header) == 8, "struct header must be 8 bytes");

int main(void)
{
    printf("%s %s %s\n", TYPE_NAME(42), TYPE_NAME(4.2), TYPE_NAME("x"));
    return 0;
}
