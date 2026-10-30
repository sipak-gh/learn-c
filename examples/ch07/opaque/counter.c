/* counter.c - the only file that knows what is inside struct counter. */
#include "counter.h"

#include <stdlib.h>
#include <string.h>

struct counter {
    char name[16];
    int value;
};

struct counter *counter_new(const char *name)
{
    struct counter *c = malloc(sizeof *c);
    if (c == NULL)
        return NULL;
    c->value = 0;
    strncpy(c->name, name, sizeof c->name - 1);
    c->name[sizeof c->name - 1] = '\0';
    return c;
}

void counter_add(struct counter *c, int n)
{
    c->value += n;
}

int counter_value(const struct counter *c)
{
    return c->value;
}

const char *counter_name(const struct counter *c)
{
    return c->name;
}

void counter_free(struct counter *c)
{
    free(c);
}
