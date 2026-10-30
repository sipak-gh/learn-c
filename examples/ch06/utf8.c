/* utf8.c - C strings are bytes; UTF-8 text passes through untouched. */
#include <stdio.h>
#include <string.h>

/* Count code points: every byte except continuation bytes 10xxxxxx. */
static size_t utf8_count(const char *s)
{
    size_t n = 0;
    for (; *s != '\0'; s++)
        if (((unsigned char)*s & 0xC0) != 0x80)
            n++;
    return n;
}

int main(void)
{
    const char *words[] = { "cafe", "caf\xc3\xa9", "\xe2\x82\xac" "5", "\xf0\x9f\x91\x8b" };

    for (size_t i = 0; i < sizeof words / sizeof words[0]; i++) {
        printf("%-6s strlen = %zu, code points = %zu, bytes:",
               words[i], strlen(words[i]), utf8_count(words[i]));
        for (const char *p = words[i]; *p != '\0'; p++)
            printf(" %02x", (unsigned char)*p);
        printf("\n");
    }
    return 0;
}
