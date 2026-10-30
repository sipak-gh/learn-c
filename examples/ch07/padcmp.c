/* padcmp.c - why you compare structs member by member, not with memcmp. */
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

struct item {
    char kind;      /* 1 byte, then 3 bytes of padding */
    int count;
};

static bool item_equal(const struct item *a, const struct item *b)
{
    return a->kind == b->kind && a->count == b->count;
}

int main(void)
{
    struct item a, b;
    memset(&a, 0xAA, sizeof a);     /* fill the padding with different junk */
    memset(&b, 0x00, sizeof b);
    a.kind = b.kind = 'x';
    a.count = b.count = 7;

    printf("item_equal: %s\n", item_equal(&a, &b) ? "equal" : "different");
    printf("memcmp:     %s\n", memcmp(&a, &b, sizeof a) == 0 ? "equal" : "different");

    const unsigned char *p = (const unsigned char *)&a;
    printf("bytes of a:");
    for (size_t i = 0; i < sizeof a; i++)
        printf(" %02x", p[i]);
    putchar('\n');
    return 0;
}
