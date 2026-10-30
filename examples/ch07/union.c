/* union.c - unions, a tagged union, and reading a float's bits. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* A tagged union: an enum says which union member is valid. */
enum value_kind { V_INT, V_DOUBLE, V_TEXT };

struct value {
    enum value_kind kind;
    union {             /* C99 needs a name for this member: "as" */
        long i;
        double d;
        const char *s;
    } as;
};

static void print_value(const struct value *v)
{
    switch (v->kind) {
    case V_INT:
        printf("int    %ld\n", v->as.i);
        break;
    case V_DOUBLE:
        printf("double %g\n", v->as.d);
        break;
    case V_TEXT:
        printf("text   \"%s\"\n", v->as.s);
        break;
    }
}

int main(void)
{
    struct value vals[] = {
        { V_INT, { .i = 42 } },
        { V_DOUBLE, { .d = 2.5 } },
        { V_TEXT, { .s = "hello" } },
    };
    for (size_t i = 0; i < sizeof vals / sizeof vals[0]; i++)
        print_value(&vals[i]);
    printf("sizeof(struct value) = %zu (enum 4 + pad 4 + biggest member 8)\n",
           sizeof(struct value));

    /* All members share the same bytes. Writing one and reading another
     * reinterprets the bytes ("type punning"). */
    union {
        float f;
        uint32_t u;
    } pun;
    pun.f = 1.0f;
    printf("1.0f as bits: 0x%08lx\n", (unsigned long)pun.u);

    /* memcpy does the same job and also works without a union. */
    float g = -2.0f;
    uint32_t bits;
    memcpy(&bits, &g, sizeof bits);
    printf("-2.0f as bits: 0x%08lx\n", (unsigned long)bits);

    /* Which byte comes first in memory? */
    union {
        uint32_t u;
        unsigned char b[4];
    } order = { 0x01020304 };
    printf("0x01020304 starts with byte %02x: %s-endian\n",
           order.b[0], order.b[0] == 0x04 ? "little" : "big");
    return 0;
}
