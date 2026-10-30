/* keys.c - show the bytes each read() from the terminal returns.
 * Usage: keys [-r]     -r: raw mode. Type q (as the first key) to quit. */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

static struct termios saved;            /* the settings we found */

static void restore(void)
{
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &saved);
}

static int raw_mode(void)
{
    if (tcgetattr(STDIN_FILENO, &saved) < 0)
        return -1;
    atexit(restore);                    /* runs at exit() and return from main */
    struct termios t = saved;           /* a copy: change only what we need */
    t.c_iflag &= ~(ICRNL | IXON);       /* Enter stays '\r'; Ctrl-S/Ctrl-Q are keys */
    t.c_lflag &= ~(ICANON | ECHO | ISIG | IEXTEN);
    t.c_cc[VMIN] = 1;                   /* read() returns after 1 byte... */
    t.c_cc[VTIME] = 0;                  /* ...with no timeout */
    return tcsetattr(STDIN_FILENO, TCSAFLUSH, &t);
}

int main(int argc, char *argv[])
{
    int raw = argc > 1 && strcmp(argv[1], "-r") == 0;
    if (raw && raw_mode() < 0) {
        perror("keys: raw mode");
        return 1;
    }
    printf("%s mode, q quits\n", raw ? "raw" : "cooked");
    char buf[64];
    ssize_t n;
    while ((n = read(STDIN_FILENO, buf, sizeof buf)) > 0) {
        printf("%zd byte%s:", n, n == 1 ? "" : "s");
        for (ssize_t i = 0; i < n; i++) {
            unsigned char c = (unsigned char)buf[i];
            if (c >= 0x21 && c < 0x7f)
                printf(" %c", c);
            else
                printf(" %02x", c);       /* control or non-ASCII: in hex */
        }
        printf("\n");
        fflush(stdout);
        if (buf[0] == 'q')
            break;
    }
    return 0;
}
