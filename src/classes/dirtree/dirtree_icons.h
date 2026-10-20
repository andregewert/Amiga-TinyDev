/**
 * @file dirtree_icons.h
 * @brief The small entry symbols of dirtree.gadget (private).
 *
 * Copyright (c) 2026 André Gewert <agewert@ubergeek.de>
 * Released under the MIT License, see LICENSE.
 */
#ifndef DIRTREE_ICONS_H
#define DIRTREE_ICONS_H

#include <exec/types.h>
#include <intuition/classusr.h>
#include <intuition/screens.h>
#include <graphics/view.h>

/** Kinds of entry symbols. */
enum {
    DTI_DRAWER = 0,     /**< closed directory */
    DTI_DRAWER_OPEN,    /**< expanded directory */
    DTI_FILE,           /**< any other file */
    DTI_SOURCE,         /**< source code (.c, .h, .s, .rexx ...) */
    DTI_SCRIPT,         /**< AmigaDOS script (S bit, .script) */
    DTI_PICTURE,        /**< picture (.iff, .png, .jpg ...) */
    DTI_COUNT
};

/** Number of colours of the symbols. */
#define DTI_COLOURS 10

/** Pens of the symbols on one screen, shared by all symbols of a tree. */
typedef struct IconPens {
    struct ColorMap *cm;            /**< colour map the pens belong to */
    LONG pens[DTI_COLOURS];         /**< obtained pens, -1 if none */
    int ok;                         /**< pens were obtained */
} IconPens;

/**
 * @brief Create the private image class of the symbols.
 * @return TRUE on success.
 */
BOOL dirtree_icons_init(void);

/** @brief Free the private image class (no symbol may exist any more). */
void dirtree_icons_exit(void);

/**
 * @brief Create a symbol image.
 * @param kind A DTI_ kind.
 * @param pens The shared pens (used once they are obtained).
 * @return The image object, or NULL.
 */
Object *dirtree_icon_new(int kind, IconPens *pens);

/**
 * @brief Create an invisible image (kind -1, draws nothing).
 * @param width Its width.
 * @param height Its height.
 * @return The image object, or NULL.
 */
Object *dirtree_blank_new(WORD width, WORD height);

/**
 * @brief Obtain the symbols' colours on a screen (once).
 * @param pens The shared pens.
 * @param screen The screen.
 * @param dri The screen's DrawInfo (unused, for later colours), may be NULL.
 */
void dirtree_icons_obtain(IconPens *pens, struct Screen *screen, struct DrawInfo *dri);

/**
 * @brief Width of the symbols.
 * @return The width in pixels.
 */
WORD dirtree_icon_width(void);

/**
 * @brief Height of the symbols.
 * @return The height in pixels.
 */
WORD dirtree_icon_height(void);

/**
 * @brief Release pens obtained by dirtree_icons_obtain().
 * @param pens The shared pens.
 */
void dirtree_icons_release(IconPens *pens);

/**
 * @brief Choose the symbol of a file by its name and protection bits.
 * @param name The file name.
 * @param protection fib_Protection of the file.
 * @return A DTI_ kind (never a drawer).
 */
int dirtree_icon_kind(const char *name, ULONG protection);

#endif
