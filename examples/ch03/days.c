/* days.c - switch: the number of days in a month. */
#include <stdio.h>

int main(void)
{
    int month;

    if (scanf("%d", &month) != 1) {
        fprintf(stderr, "days: expected a month number\n");
        return 1;
    }

    int days;
    switch (month) {
    case 2:
        days = 28;                       /* ignoring leap years */
        break;
    case 4: case 6: case 9: case 11:     /* several labels, one action */
        days = 30;
        break;
    case 1: case 3: case 5: case 7: case 8: case 10: case 12:
        days = 31;
        break;
    default:                             /* anything else */
        fprintf(stderr, "days: %d is not a month\n", month);
        return 1;
    }
    printf("month %d has %d days\n", month, days);
    return 0;
}
