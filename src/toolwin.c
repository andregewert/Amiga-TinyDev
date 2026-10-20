/**
 * @file toolwin.c
 * @brief Placement of TinyDev's tool windows.
 *
 * Tool windows (the bookmark list, later others) stay open next to the
 * main window and can be shown and hidden at any time. The first time a
 * window opens next to the main window: right of it if the screen has
 * room, otherwise over its right edge, as high as the main window. Once
 * moved or sized, its box is remembered (in the settings, saved with them
 * in the program icon) and used again, pushed into the screen if it no
 * longer fits.
 */
#include "editor.h"

#include <intuition/intuition.h>
#include <intuition/screens.h>

void toolwin_box(EditorApp *app, const long saved[4], WORD min_width, WORD min_height,
                 WORD box[4])
{
    struct Window *main_win = app->window;
    WORD sw = app->screen != NULL ? app->screen->Width : 640;
    WORD sh = app->screen != NULL ? app->screen->Height : 256;
    WORD top_border = app->screen != NULL ? (WORD)(app->screen->BarHeight + 1) : 0;
    WORD l, t, w, h;

    if (saved[2] > 0 && saved[3] > 0) {
        l = (WORD)saved[0];
        t = (WORD)saved[1];
        w = (WORD)saved[2];
        h = (WORD)saved[3];
    } else {
        /* a quarter of the screen wide, as high as the main window */
        w = (WORD)(sw / 4);
        if (main_win != NULL) {
            h = main_win->Height;
            t = main_win->TopEdge;
            l = (WORD)(main_win->LeftEdge + main_win->Width);
            if (l + w > sw) l = (WORD)(main_win->LeftEdge + main_win->Width - w);
        } else {
            h = (WORD)(sh - top_border);
            t = top_border;
            l = (WORD)(sw - w);
        }
    }
    if (w < min_width) w = min_width;
    if (h < min_height) h = min_height;
    if (w > sw) w = sw;
    if (h > sh - top_border) h = (WORD)(sh - top_border);
    if (l + w > sw) l = (WORD)(sw - w);
    if (t + h > sh) t = (WORD)(sh - h);
    if (l < 0) l = 0;
    if (t < top_border) t = top_border;
    box[0] = l;
    box[1] = t;
    box[2] = w;
    box[3] = h;
}

void toolwin_remember(struct Window *win, long saved[4])
{
    if (win == NULL) return;
    saved[0] = win->LeftEdge;
    saved[1] = win->TopEdge;
    saved[2] = win->Width;
    saved[3] = win->Height;
}
