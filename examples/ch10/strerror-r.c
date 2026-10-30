/* strerror-r.c - the thread-safe strerror_r(), POSIX (XSI) version. */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    char buf[64], tiny[8];

    /* Returns 0 and fills buf: no shared static buffer, so threads are safe. */
    int rc = strerror_r(EADDRINUSE, buf, sizeof buf);
    printf("rc=%d buf=\"%s\"\n", rc, buf);

    /* Too small: the text is cut and the result is ERANGE. */
    rc = strerror_r(EADDRINUSE, tiny, sizeof tiny);
    printf("tiny: rc=%d%s buf=\"%s\"\n", rc, rc == ERANGE ? " (ERANGE)" : "", tiny);

    /* Not an errno value: the libcs disagree. */
    rc = strerror_r(9999, buf, sizeof buf);
    printf("9999: rc=%d%s buf=\"%s\"\n", rc, rc == EINVAL ? " (EINVAL)" : "", buf);
    return 0;
}
