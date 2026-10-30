/* width.c - how many characters are in a UTF-8 string? Two ways. */
#define _POSIX_C_SOURCE 200809L
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Our way: count the bytes that are not 10xxxxxx (continuation bytes). */
static size_t utf8_chars(const char *s)
{
    size_t n = 0;
    for (; *s != '\0'; s++)
        if (((unsigned char)*s & 0xC0) != 0x80)
            n++;
    return n;
}

int main(void)
{
    /* "café €5" in UTF-8. "\xac" "5": a hex escape eats every hex digit
     * after it, so "\xac5" would be one (too big) escape. */
    const char *s = "caf\xc3\xa9 \xe2\x82\xac" "5";
    printf("bytes: %zu, our count: %zu\n", strlen(s), utf8_chars(s));
    /* The C library's way: mbstowcs(NULL, s, 0) counts characters, by the
     * rules of the current locale; (size_t)-1 means "invalid". */
    printf("mbstowcs, \"C\" locale: %ld\n", (long)mbstowcs(NULL, s, 0));
    const char *name = setlocale(LC_CTYPE, "");      /* from LC_ALL/LC_CTYPE/LANG */
    printf("setlocale(LC_CTYPE, \"\") = %s\n", name != NULL ? name : "NULL (failed)");
    printf("mbstowcs, now: %ld\n", (long)mbstowcs(NULL, s, 0));
    return 0;
}
