/**
 * @file helpviewer.h
 * @brief What the HelpViewer's modules share: texts, messages, the
 *        status line, the program icon's tool types and printing.
 */
#ifndef HELPVIEWER_H
#define HELPVIEWER_H

#include <exec/types.h>
#include <intuition/classusr.h>
#include <intuition/screens.h>

#define PATHMAX 512             /**< size of path buffers */
#define TOOLTYPE_LEN 64         /**< size of a tool type written by write_tooltypes() */

/** A tool type to write into the program icon with write_tooltypes(). */
typedef struct ToolTypeEntry {
    const char *key;            /**< its name ("PAPER"), compared without case */
    BOOL active;                /**< FALSE: the default value; an entry in the icon is disabled ("(KEY=...)") */
    BOOL done;                  /**< used by write_tooltypes() */
    char text[TOOLTYPE_LEN];    /**< "KEY" or "KEY=value" */
} ToolTypeEntry;

/**
 * @brief Return a text in the user's language.
 * @param id The MSG_ number.
 * @return The text from the catalog, or the built-in English one.
 */
const char *hv_text(LONG id);

/** A text in the user's language (hv_text(); TinyDev's own S() is another). */
#define S(id) hv_text(id)

/**
 * @brief Show a message: a requester (also from the Shell, as it may come
 *        before or without the window).
 * @param text The message.
 */
void message(const char *text);

/**
 * @brief Show a text in the status line.
 * @param text The text.
 */
void set_status(const char *text);

/**
 * @brief Write tool types into the program icon.
 *
 * An entry already in the icon (also as a disabled "(KEY=...)") is
 * replaced in its place; an entry that is the default now (not active)
 * is disabled, so the icon still shows what can be set. New entries go
 * in front of a NewIcons block. Other tool types stay as they are.
 *
 * @param e The entries.
 * @param count Their number.
 * @return Non-zero if the icon was written.
 */
int write_tooltypes(ToolTypeEntry *e, int count);

/**
 * @brief Take the print options from the tool types of the program icon
 *        (PRINT_MODE, PRINT_TO, PAPER, MARGINS ..., as in Kickdown).
 * @param tt The tool types.
 */
void print_options(CONST_STRPTR *tt);

/**
 * @brief Project/Print: the print window, then the output of the page
 *        shown, to the printer, as PostScript or as PDF.
 *
 * The main window waits meanwhile (busy pointer, its input is dropped).
 * Changed options are written into the program icon.
 *
 * @param screen The screen of the main window.
 * @param winobj The main window (window.class).
 * @param html The page's html.gadget or htmlttf.gadget.
 * @param page The page shown (for the name of the output file).
 */
void print_page(struct Screen *screen, Object *winobj, Object *html, const char *page);

/** @brief Close the classes that printing opened. */
void print_cleanup(void);

#endif /* HELPVIEWER_H */
