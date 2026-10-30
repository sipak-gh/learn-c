/* term.c - the terminal: raw mode and window size. */
#include "term.h"

#include <stdbool.h>
#include <sys/ioctl.h>     /* not POSIX, but on every Unix-like system */
#include <termios.h>
#include <unistd.h>

static struct termios saved;       /* the settings to restore */
static bool raw_on = false;

int term_raw(void)
{
    if (tcgetattr(STDIN_FILENO, &saved) < 0)
        return -1;
    struct termios raw = saved;
    /* Input: no CR -> NL translation (Enter arrives as '\r'), no Ctrl-S /
     * Ctrl-Q flow control, no stripping of the 8th bit. */
    raw.c_iflag &= ~(ICRNL | IXON | ISTRIP | INPCK | BRKINT);
    /* Local modes: no line editing (ICANON), no echo, no signals from
     * Ctrl-C / Ctrl-Z (ISIG), no Ctrl-V (IEXTEN). */
    raw.c_lflag &= ~(ICANON | ECHO | ISIG | IEXTEN);
    /* read() returns as soon as 1 byte is there, with no timer. */
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    /* Output processing (OPOST: "\n" -> "\r\n") stays on. */
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) < 0)
        return -1;
    raw_on = true;
    return 0;
}

void term_restore(void)
{
    if (raw_on) {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &saved);
        raw_on = false;
    }
}

void term_size(int *rows, int *cols)
{
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_row > 0 && ws.ws_col > 0) {
        *rows = ws.ws_row;
        *cols = ws.ws_col;
    } else {
        *rows = 24;
        *cols = 80;
    }
}
