/* layout.c - padding, alignment, sizeof and offsetof. */
#include <stddef.h>   /* offsetof */
#include <stdint.h>
#include <stdio.h>

struct loose {        /* members in a careless order */
    char tag;
    double value;
    char flag;
    int count;
};

struct tight {        /* the same members, largest first */
    double value;
    int count;
    char tag;
    char flag;
};

struct header {       /* looks like 7 bytes of data... */
    uint8_t type;
    uint16_t length;
    uint32_t id;
};

#define SHOW(type, member) \
    printf("  %-8s offset %2zu, size %zu\n", #member, \
           offsetof(type, member), sizeof(((type *)0)->member))

int main(void)
{
    printf("struct loose: sizeof %zu\n", sizeof(struct loose));
    SHOW(struct loose, tag);
    SHOW(struct loose, value);
    SHOW(struct loose, flag);
    SHOW(struct loose, count);

    printf("struct tight: sizeof %zu\n", sizeof(struct tight));
    SHOW(struct tight, value);
    SHOW(struct tight, count);
    SHOW(struct tight, tag);
    SHOW(struct tight, flag);

    printf("struct header: sizeof %zu\n", sizeof(struct header));
    SHOW(struct header, type);
    SHOW(struct header, length);
    SHOW(struct header, id);

    struct loose arr[2];
    printf("array of 2 loose: %zu bytes, element 1 starts %td bytes in\n",
           sizeof arr, (char *)&arr[1] - (char *)&arr[0]);
    return 0;
}
