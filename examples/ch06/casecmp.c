/* casecmp.c - case-insensitive comparison: POSIX strcasecmp vs our own. */
#define _POSIX_C_SOURCE 200809L
#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <strings.h>                  /* strcasecmp: POSIX, not C99 */

/* ASCII-only equality, portable C99. */
static bool str_ieq(const char *a, const char *b)
{
    while (*a != '\0' && tolower((unsigned char)*a) == tolower((unsigned char)*b)) {
        a++;
        b++;
    }
    return tolower((unsigned char)*a) == tolower((unsigned char)*b);
}

int main(void)
{
    const char *pairs[][2] = {
        { "NICK", "nick" }, { "Nick", "NICKNAME" }, { "msg", "MSG" }, { "quit", "QUIZ" },
    };
    for (size_t i = 0; i < sizeof pairs / sizeof pairs[0]; i++) {
        const char *a = pairs[i][0], *b = pairs[i][1];
        int r = strcasecmp(a, b);
        printf("%-5s %-9s strcasecmp %-2s 0, str_ieq %s\n", a, b,
               r < 0 ? "<" : r > 0 ? ">" : "==", str_ieq(a, b) ? "true" : "false");
    }
    printf("strncasecmp(\"NICKNAME\", \"nick\", 4) = %d\n",
           strncasecmp("NICKNAME", "nick", 4));
    return 0;
}
