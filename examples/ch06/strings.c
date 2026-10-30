/* strings.c - a C string is an array of char ending in '\0'. */
#include <stdio.h>
#include <string.h>

int main(void)
{
    char nick[8] = "alice";   /* 5 letters + '\0', the rest is zeros too */
    char word[] = { 'b', 'o', 'b', '\0' };   /* the same thing by hand */

    printf("nick = \"%s\", strlen = %zu, sizeof = %zu\n",
           nick, strlen(nick), sizeof nick);
    printf("word = \"%s\", strlen = %zu, sizeof = %zu\n",
           word, strlen(word), sizeof word);

    printf("bytes:");
    for (size_t i = 0; i < sizeof nick; i++)
        printf(" %d", nick[i]);
    printf("\n");

    nick[3] = '\0';           /* cut the string short */
    printf("after nick[3] = '\\0': \"%s\", strlen = %zu\n", nick, strlen(nick));

    char empty[1] = "";       /* the empty string is one byte: '\0' */
    printf("empty: strlen = %zu, sizeof = %zu\n", strlen(empty), sizeof empty);
    return 0;
}
