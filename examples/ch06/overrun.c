/* overrun.c - strcpy does not know how big the destination is. */
#include <stdio.h>
#include <string.h>

int main(int argc, char *argv[])
{
    char nick[8];
    const char *input = argc > 1 ? argv[1] : "bob";

    strcpy(nick, input);          /* overflows nick if input is 8+ bytes */
    printf("nick = %s\n", nick);
    return 0;
}
