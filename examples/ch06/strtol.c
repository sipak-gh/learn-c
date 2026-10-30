/* strtol.c - converting text to numbers, with full error checking. */
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

static void try(const char *s)
{
    char *end;
    errno = 0;                        /* strtol only sets errno on error */
    long n = strtol(s, &end, 10);

    printf("%-22s -> %20ld  ", s, n);
    if (end == s)
        printf("no digits at all\n");
    else if (errno == ERANGE)
        printf("out of range (clamped to LONG_%s)\n", n == LONG_MAX ? "MAX" : "MIN");
    else if (*end != '\0')
        printf("trailing junk \"%s\"\n", end);
    else
        printf("ok\n");
}

int main(void)
{
    const char *inputs[] = {
        "6667", "  -42", "+7", "", "port", "66x", "12 ",
        "99999999999999999999", "-99999999999999999999",
    };
    for (size_t i = 0; i < sizeof inputs / sizeof inputs[0]; i++)
        try(inputs[i]);

    /* Base 0 accepts C-style prefixes. */
    printf("base 0: \"0x1f\" = %ld, \"017\" = %ld\n",
           strtol("0x1f", NULL, 0), strtol("017", NULL, 0));

    /* strtoul accepts a minus sign and negates the result! */
    errno = 0;
    unsigned long u = strtoul("-1", NULL, 10);
    printf("strtoul(\"-1\") = %lu, errno = %d\n", u, errno);
    printf("atoi(\"port\") = %d (no way to tell this from \"0\")\n", atoi("port"));
    return 0;
}
