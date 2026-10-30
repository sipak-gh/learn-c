/* ptrptr.c - pointers to pointers, and arrays of pointers. */
#include <stdio.h>

/* Skip spaces, then return the length of the word at *cursor and move
 * *cursor past it. The caller's pointer changes, so we get its address. */
static int next_word(const char **cursor)
{
    const char *p = *cursor;
    while (*p == ' ')
        p++;
    const char *start = p;
    while (*p != '\0' && *p != ' ')
        p++;
    *cursor = p;                        /* update the caller's pointer */
    return (int)(p - start);
}

int main(int argc, char **argv)
{
    const char *text = "  NICK   alice ";
    const char *cur = text;
    int len;
    while ((len = next_word(&cur)) > 0)
        printf("word: %.*s\n", len, cur - len);

    const char *names[] = { "alice", "bob", "carol" };  /* array of pointers */
    size_t n = sizeof names / sizeof names[0];
    for (size_t i = 0; i < n; i++)
        printf("names[%zu] = %s (first letter %c)\n", i, names[i], names[i][0]);

    /* argv is a char ** pointing at an array of pointers ending in NULL. */
    for (char **a = argv; *a != NULL; a++)
        printf("arg: %s\n", *a);
    (void)argc;
    return 0;
}
