/* counter.h - the public interface. Users see the name, not the members. */
#ifndef COUNTER_H
#define COUNTER_H

struct counter;                 /* incomplete type: members are private */

struct counter *counter_new(const char *name);
void counter_add(struct counter *c, int n);
int counter_value(const struct counter *c);
const char *counter_name(const struct counter *c);
void counter_free(struct counter *c);

#endif
