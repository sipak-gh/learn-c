/* fallthrough.c - a forgotten break, and an intended fall-through. */
#include <stdio.h>

int main(void)
{
    int level = 1;

    switch (level) {
    case 0:
        printf("debug\n");
    case 1:
        printf("info\n");                /* oops: no break */
    case 2:
        printf("warning\n");
        break;
    }

    int stage = 1;
    switch (stage) {                     /* the countdown falls through on purpose */
    case 1:
        printf("connect\n");
        /* fall through */
    case 2:
        printf("register nick\n");
        /* fall through */
    case 3:
        printf("join channel\n");
        break;
    }
    return 0;
}
