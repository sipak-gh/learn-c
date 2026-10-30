/* term.h - the terminal: raw mode and window size. */
#ifndef PARLEY_TERM_H
#define PARLEY_TERM_H

/* Put the terminal on stdin in raw mode: every key press is delivered at
 * once (no line editing by the terminal), nothing is echoed, and Ctrl-C
 * is just a byte. Returns 0, or -1 with errno set (e.g. ENOTTY). */
int term_raw(void);

/* Put the terminal back the way term_raw() found it. Safe to call when
 * it is not in raw mode. */
void term_restore(void);

/* The window size in character cells. If the terminal does not know
 * (some report 0x0), assume the classic 24 rows of 80 columns. */
void term_size(int *rows, int *cols);

#endif
