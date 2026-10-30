/* minied.c - a tiny line editor in raw mode. Type, Backspace, Ctrl-U
 * (clear), Enter. Ctrl-C, or Ctrl-D on an empty line, quits. ASCII only. */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

static struct termios saved;

static void restore(void)
{
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &saved);
}

static void raw_mode(void)
{
    if (tcgetattr(STDIN_FILENO, &saved) < 0) {
        perror("minied: tcgetattr");
        exit(1);
    }
    atexit(restore);
    struct termios t = saved;
    t.c_iflag &= ~(ICRNL | IXON);
    t.c_lflag &= ~(ICANON | ECHO | ISIG | IEXTEN);
    t.c_cc[VMIN] = 1;
    t.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &t) < 0) {
        perror("minied: tcsetattr");
        exit(1);
    }
}

/* Draw the line again: column 1, prompt, text, erase the rest. */
static void refresh(const char *line, size_t len)
{
    printf("\r> %.*s\x1b[K", (int)len, line);
    fflush(stdout);
}

int main(void)
{
    char line[60];
    size_t len = 0;
    char c;

    raw_mode();
    refresh(line, len);
    while (read(STDIN_FILENO, &c, 1) == 1) {
        if (c == '\r') {                            /* Enter */
            printf("\ngot \"%.*s\"\n", (int)len, line);
            len = 0;
        } else if (c == 0x7f || c == '\b') {        /* Backspace */
            if (len > 0)
                len--;
        } else if (c == 0x15) {                     /* Ctrl-U */
            len = 0;
        } else if (c == 0x03 || (c == 0x04 && len == 0)) {
            break;                                  /* Ctrl-C, Ctrl-D */
        } else if (c >= 0x20 && c < 0x7f && len < sizeof line) {
            line[len++] = c;
        }
        refresh(line, len);
    }
    printf("\nbye\n");
    return 0;                   /* exit runs restore() */
}
