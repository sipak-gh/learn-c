/* nul.c - a NUL byte makes one line look like two different lines. */
#include <stdio.h>
#include <string.h>

int main(void)
{
    static const char line[] = "MSG :hi\0QUIT";   /* 12 bytes from the network */
    size_t len = sizeof line - 1;                 /* what read() said */

    printf("read() length: %zu\n", len);
    printf("strlen():      %zu\n", strlen(line));
    printf("printf(\"%%s\"):  %s\n", line);
    printf("bytes:         ");
    fwrite(line, 1, len, stdout);                 /* writes the NUL too */
    printf("\n");
    return 0;
}
