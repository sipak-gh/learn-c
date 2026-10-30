/* main.c - uses struct counter only through pointers and functions. */
#include <stdio.h>

#include "counter.h"

int main(void)
{
    struct counter *c = counter_new("lines");
    if (c == NULL)
        return 1;
    counter_add(c, 3);
    counter_add(c, 4);
    printf("%s = %d\n", counter_name(c), counter_value(c));
    /* c->value = 99;   would not compile: the type is incomplete here */
    counter_free(c);
    return 0;
}
