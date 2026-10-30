/* mystring.c - strlen, strcmp and strchr written by hand. */
#include <stdio.h>
#include <string.h>

static size_t my_strlen(const char *s)
{
    const char *p = s;
    while (*p != '\0')
        p++;
    return (size_t)(p - s);
}

/* Compare as unsigned char, like the real strcmp. */
static int my_strcmp(const char *a, const char *b)
{
    while (*a != '\0' && *a == *b) {
        a++;
        b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

/* Pointer to the first c in s, or NULL. c == '\0' finds the terminator. */
static char *my_strchr(const char *s, int c)
{
    for (;; s++) {
        if (*s == (char)c)
            return (char *)s;
        if (*s == '\0')
            return NULL;
    }
}

static int sign(int n)
{
    return (n > 0) - (n < 0);
}

int main(void)
{
    const char *words[] = { "", "bob", "alice", "alicia", "\xc3\xa9" };
    size_t n = sizeof words / sizeof words[0];

    for (size_t i = 0; i < n; i++)
        printf("len(\"%s\") = %zu %s\n", words[i], my_strlen(words[i]),
               my_strlen(words[i]) == strlen(words[i]) ? "ok" : "WRONG");
    for (size_t i = 0; i + 1 < n; i++) {
        int mine = sign(my_strcmp(words[i], words[i + 1]));
        int real = sign(strcmp(words[i], words[i + 1]));
        printf("cmp(\"%s\", \"%s\") = %2d %s\n", words[i], words[i + 1], mine,
               mine == real ? "ok" : "WRONG");
    }
    const char *s = "NICK bob";
    printf("chr ' ' -> \"%s\", chr '\\0' at index %td, chr 'z' -> %s\n",
           my_strchr(s, ' '), my_strchr(s, '\0') - s,
           my_strchr(s, 'z') == NULL ? "NULL" : "?");
    return 0;
}
