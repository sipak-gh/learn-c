/* main.c - calls a function defined in another file. */
#include <stdio.h>

int percent(int part, int whole);       /* declared here, defined in percent.c */

/* A second private clamp: no clash with the one in percent.c. */
static int clamp(int v, int lo, int hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

int main(void)
{
    int users = 7, max = 5;
    printf("room is %d%% full\n", percent(users, max));
    printf("showing %d of %d users\n", clamp(users, 0, max), users);
    return 0;
}
