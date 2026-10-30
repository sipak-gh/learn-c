/* stamp.c - from a time_t to text: localtime_r, gmtime_r, strftime. */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static void show(const char *label, const struct tm *tm)
{
    char buf[64];
    /* strftime returns 0 if the result (plus its '\0') does not fit. */
    if (strftime(buf, sizeof buf, "%Y-%m-%d %H:%M:%S %Z", tm) == 0)
        buf[0] = '\0';
    printf("%-6s %s  (%%H:%%M -> ", label, buf);
    strftime(buf, sizeof buf, "[%H:%M]", tm);
    printf("%s)\n", buf);
}

int main(int argc, char *argv[])
{
    /* A time_t counts seconds since 1970-01-01 00:00:00 UTC. With an
     * argument we use that number, otherwise the time right now. */
    time_t t = argc > 1 ? (time_t)strtoll(argv[1], NULL, 10) : time(NULL);
    struct tm tm;

    printf("time_t %lld\n", (long long)t);
    if (gmtime_r(&t, &tm) != NULL)       /* broken down, in UTC */
        show("UTC", &tm);
    if (localtime_r(&t, &tm) != NULL)    /* broken down, in local time (TZ) */
        show("local", &tm);
    printf("fields: tm_year=%d tm_mon=%d tm_mday=%d tm_wday=%d tm_isdst=%d\n",
           tm.tm_year, tm.tm_mon, tm.tm_mday, tm.tm_wday, tm.tm_isdst);
    return 0;
}
