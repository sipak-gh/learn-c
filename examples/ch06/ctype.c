/* ctype.c - classifying characters, and the unsigned char cast. */
#include <ctype.h>
#include <stdio.h>

int main(void)
{
    const char *s = "Bob_9 -!";

    for (const char *p = s; *p != '\0'; p++) {
        unsigned char c = (unsigned char)*p;   /* always cast first */
        printf("'%c' alpha=%d digit=%d alnum=%d space=%d punct=%d upper=%d -> '%c'\n",
               c, isalpha(c) != 0, isdigit(c) != 0, isalnum(c) != 0,
               isspace(c) != 0, ispunct(c) != 0, isupper(c) != 0, tolower(c));
    }

    /* A byte >= 0x80 in a plain char is negative on x86-64. */
    const char *cafe = "caf\xc3\xa9";          /* "café" in UTF-8 */
    for (const char *p = cafe; *p != '\0'; p++)
        printf("byte %4d: toupper((unsigned char)c) = %3d, toupper(c) = %4d\n",
               *p, toupper((unsigned char)*p), toupper(*p));
    return 0;
}
