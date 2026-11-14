/**
 * @file buildmsg.h
 * @brief Portable reader of the file and line in a compiler's or
 *        assembler's message, for the jump from the output window; no
 *        Amiga headers.
 *
 * It knows the formats of the tools the templates use:
 *
 * - gcc and GNU make: @c file:line:col: or @c file:line: (the file name
 *   may hold the colon of an Amiga volume, "Work:src/x.c:12:3:");
 * - vbcc and vasm: <tt>error 18 in line 12 of "file":</tt> (also
 *   warning, message);
 * - BAsm (Barfly): <tt>Error in Line 35 in 'file'.</tt> (also Warning);
 * - SAS/C: <tt>file 12 Error 3: ...</tt> (also Warning);
 * - Free Pascal: <tt>file(12,5) Error: ...</tt> (also Warning, Note,
 *   Hint, Fatal);
 * - E-VO and ec: <tt>ERROR: LINE 12: ...</tt>, <tt>LINE 12: ...</tt> and
 *   <tt>WARNING: ... (line 12)</tt>, without a file.
 */
#ifndef BUILDMSG_H
#define BUILDMSG_H

#include <stddef.h>

/** Longest file name kept (with its terminator); longer ones are not taken. */
#define BUILDMSG_FILE_MAX 256

/** The place a message names. */
typedef struct BuildMessage {
    char file[BUILDMSG_FILE_MAX];   /**< the file as written, "" if the message names none */
    long line;                      /**< the line (1-based) */
    long col;                       /**< the column (1-based), 0 if not given */
} BuildMessage;

/**
 * @brief Find the file and line a line of output names.
 * @param text The line (need not be terminated, no line end).
 * @param length Its length.
 * @param[out] m The place, if one was found.
 * @return Non-zero if the line names a place (a line number above 0).
 */
int buildmsg_parse(const char *text, size_t length, BuildMessage *m);

#endif
