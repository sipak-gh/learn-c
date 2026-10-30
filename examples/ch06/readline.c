/* readline.c - read lines with fgets and notice lines that don't fit. */
#include <stdio.h>
#include <string.h>

int main(void)
{
    char line[8];                       /* room for 6 chars + '\n' + '\0' */
    int n = 0;

    while (fgets(line, sizeof line, stdin) != NULL) {
        n++;
        char *nl = strchr(line, '\n');
        if (nl != NULL) {
            *nl = '\0';                 /* a whole line: drop the '\n' */
            printf("line %d: \"%s\"\n", n, line);
        } else if (feof(stdin)) {
            printf("line %d: \"%s\" (no newline at the end)\n", n, line);
        } else {
            int c;                      /* too long: skip the rest of it */
            while ((c = getchar()) != EOF && c != '\n')
                ;
            printf("line %d: too long, starts \"%s\"\n", n, line);
        }
    }
    return 0;
}
