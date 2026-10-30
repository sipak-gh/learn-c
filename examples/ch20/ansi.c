/* ansi.c - a few ANSI escape sequences. ESC is byte 27: "\x1b" in C. */
#include <stdio.h>

#define ESC "\x1b"

int main(void)
{
    /* SGR (Select Graphic Rendition): ESC [ n m */
    printf(ESC "[1m" "bold" ESC "[0m "
           ESC "[7m" "reverse" ESC "[0m "
           ESC "[31m" "red" ESC "[0m "
           ESC "[36m" "cyan" ESC "[0m\n");
    /* Cursor back 3 columns, then overwrite: "abcdef" becomes "abcXYf". */
    printf("abcdef" ESC "[3D" "XY\n");
    /* '\r' goes to column 1; ESC [ K erases to the end of the line. */
    printf("this text goes away\r" ESC "[K" "replaced\n");
    /* Cursor up 3 lines and right 25 columns: write into an earlier line.
     * Then down 3 and back to column 1, below everything. */
    printf(ESC "[3A" ESC "[25C" "<-- up here" ESC "[3B" "\r");
    return 0;
}
