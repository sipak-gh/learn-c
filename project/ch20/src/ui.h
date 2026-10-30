/* ui.h - the split-screen terminal user interface of the client.
 *
 *   +-------------------------------------+
 *   |[12:00] --> alice joined             |  rows 1 .. rows-2: the chat, a
 *   |[12:01] <alice> hi                   |  scroll region: each new line
 *   |[12:01] <bob> hello                  |  pushes the older ones up
 *   | parley | alice @ localhost:6667     |  row rows-1: the status bar
 *   |> hello wor_                         |  row rows: what you are typing
 *   +-------------------------------------+
 *
 * Drawing only collects escape sequences and text in a buffer; ui_flush()
 * sends it all to the terminal with one write(), so the screen never shows
 * a half-drawn state. */
#ifndef PARLEY_UI_H
#define PARLEY_UI_H

#include <stdbool.h>
#include <stddef.h>

/* Longest chat line we keep (a protocol line plus decoration). */
#define UI_LINE_MAX 600

/* Switch the terminal to raw mode and the alternate screen, and draw the
 * layout. input_max: the longest line the user may type. Returns 0, or -1
 * with errno set if stdin is not a terminal. */
int ui_start(const char *status, size_t input_max);

/* Put the terminal back: the user's screen and settings return. Safe to
 * call more than once. */
void ui_stop(void);

/* The window changed size: redraw everything. */
void ui_resize(void);

/* Add a line at the bottom of the chat area. */
void ui_print(const char *line);

/* Change the text of the status bar. */
void ui_set_status(const char *status);

/* Handle n bytes typed by the user. Each finished line (Enter) is passed
 * to on_line. Returns false if the user wants to leave (Ctrl-C, or
 * Ctrl-D on an empty line). */
bool ui_keys(const char *keys, size_t n, void (*on_line)(char *line));

/* Send everything drawn so far to the terminal. */
void ui_flush(void);

#endif
