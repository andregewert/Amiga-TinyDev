/**
 * @file toolbar.c
 * @brief The toolbar: a speedbar with images, images and text or text, ghosted buttons, help bubbles and an overflow chooser.
 *
 * The toolbar, built like Kickdown's (see the ReAction style guide in
 * amiga-devkit, section 6):
 * - a flat speedbar.gadget: no frame around the bar, buttons without frames
 *   (TOOLBARFRAMES adds them) that look recessed while pressed;
 * - AISS images from TBIMAGES:, texts as label.image objects passed as
 *   SBNA_Image (speedbar.gadget 47.x stores SBNA_Text but never draws it);
 *   modes IMAGES, BOTH (image with the text below in the smallest real size
 *   of the screen font) and TEXT (no image is loaded at all); a missing
 *   image gives the text;
 * - buttons that would do nothing are ghosted with their \<name\>_g image (or
 *   a text in a pen halfway between text and background). The ghosted image
 *   is only loaded when the button is ghosted for the first time;
 * - a drop-down chooser right of the bar lists the buttons that do not fit;
 * - help bubbles through WINDOW_HintInfo.
 */
#include "editor.h"

#include <classes/window.h>
#include <diskfont/diskfont.h>
#include <exec/memory.h>
#include <gadgets/chooser.h>
#include <gadgets/layout.h>
#include <gadgets/speedbar.h>
#include <gadgets/texteditor.h>
#include <graphics/text.h>
#include <images/bevel.h>
#include <images/bitmap.h>
#include <images/label.h>
#include <intuition/screens.h>
#include <proto/bitmap.h>
#include <proto/chooser.h>
#include <proto/diskfont.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/label.h>
#include <proto/layout.h>
#include <proto/speedbar.h>
#include <clib/alib_protos.h>

#include <stdio.h>
#include <string.h>

/** One toolbar button. */
typedef struct ToolSpec {
    ULONG command;          /**< MID_ of the button, also its speedbar id */
    WORD spacing;           /**< gap before the button (groups) */
    const char *image;      /**< AISS name in TBIMAGES: */
    LONG label;             /**< short text for the bar and the overflow list (MSG_) */
    LONG help;              /**< help bubble (MSG_) */
} ToolSpec;

/**
 * The toolbar buttons, in their order; spacing separates the groups.
 */
static const ToolSpec tools[] = {
    { MID_NEW,   0, "new",   MSG_TB_NEW, MSG_TBH_NEW },
    { MID_OPEN,  0, "open",  MSG_TB_OPEN, MSG_TBH_OPEN },
    { MID_SAVE,  0, "save",  MSG_TB_SAVE, MSG_TBH_SAVE },
    { MID_UNDO,  8, "undo",  MSG_TB_UNDO, MSG_TBH_UNDO },
    { MID_REDO,  0, "redo",  MSG_TB_REDO, MSG_TBH_REDO },
    { MID_CUT,   8, "cut",   MSG_TB_CUT, MSG_TBH_CUT },
    { MID_COPY,  0, "copy",  MSG_TB_COPY, MSG_TBH_COPY },
    { MID_PASTE, 0, "paste", MSG_TB_PASTE, MSG_TBH_PASTE },
    { MID_FIND,  8, "find",  MSG_TB_FIND, MSG_TBH_FIND },
    { MID_HELP,  8, "help",  MSG_TB_HELP, MSG_TBH_HELP },
};
/**
 * Number of toolbar buttons.
 */
#define NUMTOOLS (sizeof(tools) / sizeof(tools[0]))
/**
 * @brief The bit of a button in a mask.
 * @param i The button's index in tools[].
 */
#define TOOLBIT(i) (1UL << (i))
/**
 * Size of a button's label buffer.
 */
#define LABEL_SIZE 32

/** State of the toolbar (there is one window). */
static struct {
    struct DrawInfo *dri;
    int mode;                       /**< ToolbarMode */
    Object *bar;                    /**< speedbar.gadget */
    Object *overflow;               /**< chooser.gadget or NULL */
    struct List buttons;            /**< speedbar nodes */
    struct List oflist;             /**< chooser nodes */
    struct Node *nodes[NUMTOOLS];
    Object *images[NUMTOOLS];       /**< normal image or label */
    Object *ghosts[NUMTOOLS];       /**< ghosted image or label, loaded lazily */
    UBYTE has_image[NUMTOOLS];      /**< the AISS image was found */
    UBYTE ghost_tried[NUMTOOLS];    /**< ghost loading was attempted */
    char labels[NUMTOOLS][LABEL_SIZE];
    struct TextAttr small_attr;
    char small_name[64];
    struct TextFont *small_font;
    LONG ghost_pen;
    ULONG disabled;                 /**< TOOLBIT() of the ghosted buttons */
    ULONG of_visible;               /**< SPEEDBAR_Visible of the overflow list */
    ULONG of_disabled;              /**< ghosted buttons of the overflow list */
    struct HintInfo hints[NUMTOOLS + 2];
} tb;

/**
 * @brief Compare two font names case-insensitively.
 * @param a First name.
 * @param b Second name.
 * @return Non-zero if they are equal.
 */
static int same_name(const char *a, const char *b)
{
    while (*a != '\0' && (*a | 0x20) == (*b | 0x20)) ++a, ++b;
    return *a == *b;
}

/**
 * @brief Open the screen font in its smallest real (designed) size of at
 *        least 6 pixels for the texts below the images.
 * @param app The application state.
 * @return The TextAttr of the font, or NULL to use the screen font.
 */
static struct TextAttr *small_font(EditorApp *app)
{
    struct TextAttr *screen_font = app->screen->Font;
    struct AvailFontsHeader *afh = NULL;
    struct AvailFonts *af;
    LONG size = 2048, more;
    UWORD best = 0, n;

    if (screen_font == NULL || screen_font->ta_Name == NULL) return NULL;
    if (DiskfontBase == NULL &&
        (DiskfontBase = OpenLibrary((STRPTR)"diskfont.library", 39)) == NULL) return NULL;
    for (;;) {
        if ((afh = AllocVec((ULONG)size, MEMF_ANY)) == NULL) return NULL;
        if ((more = AvailFonts(afh, size, AFF_MEMORY | AFF_DISK)) == 0) break;
        FreeVec(afh);
        size += more;
    }
    af = (struct AvailFonts *)(afh + 1);
    for (n = 0; n < afh->afh_NumEntries; ++n, ++af)
        if (af->af_Attr.ta_YSize >= 6 && (best == 0 || af->af_Attr.ta_YSize < best) &&
            same_name((const char *)af->af_Attr.ta_Name, (const char *)screen_font->ta_Name))
            best = af->af_Attr.ta_YSize;
    FreeVec(afh);
    if (best == 0) return NULL;
    strncpy(tb.small_name, (const char *)screen_font->ta_Name, sizeof(tb.small_name) - 1);
    tb.small_attr.ta_Name = (STRPTR)tb.small_name;
    tb.small_attr.ta_YSize = best;
    tb.small_attr.ta_Style = FS_NORMAL;
    tb.small_attr.ta_Flags = 0;
    if ((tb.small_font = OpenDiskFont(&tb.small_attr)) == NULL) return NULL;
    return &tb.small_attr;
}

/**
 * @brief Obtain the pen for ghosted texts: halfway between text and
 *        background (falls back to the text pen).
 * @param app The application state.
 */
static void obtain_ghost_pen(EditorApp *app)
{
    struct ColorMap *cm = app->screen->ViewPort.ColorMap;
    ULONG t[3], b[3];
    GetRGB32(cm, tb.dri->dri_Pens[TEXTPEN], 1, t);
    GetRGB32(cm, tb.dri->dri_Pens[BACKGROUNDPEN], 1, b);
    tb.ghost_pen = ObtainBestPen(cm, (t[0] >> 1) + (b[0] >> 1), (t[1] >> 1) + (b[1] >> 1),
                                 (t[2] >> 1) + (b[2] >> 1), OBP_Precision, PRECISION_IMAGE,
                                 TAG_END);
}

/**
 * @brief Return the pen for ghosted texts.
 * @return The pen number.
 */
static LONG ghost_pen(void)
{
    return tb.ghost_pen >= 0 ? tb.ghost_pen : (LONG)tb.dri->dri_Pens[TEXTPEN];
}

/**
 * @brief Load an AISS image from TBIMAGES:, remapped for the screen.
 * @param app The application state.
 * @param name The image name.
 * @param suffix "" or "_g".
 * @return The bitmap.image, or NULL if there is none.
 */
static Object *load_image(EditorApp *app, const char *name, const char *suffix)
{
    char file[64];
    snprintf(file, sizeof(file), "TBIMAGES:%s%s", name, suffix);
    return NewObject(BITMAP_GetClass(), NULL,
        BITMAP_SourceFile, (ULONG)file,
        BITMAP_Screen, (ULONG)app->screen,
        BITMAP_Masking, TRUE,
        TAG_END);
}

/**
 * @brief Create a label.image with the text of button i, below @p image if
 *        given (the label then disposes the image).
 * @param i The button index.
 * @param image The image above the text, or NULL.
 * @param pen The text pen.
 * @return The label, or NULL (the image is disposed then).
 */
static Object *make_label(ULONG i, Object *image, LONG pen)
{
    Object *o;
    snprintf(tb.labels[i], sizeof(tb.labels[i]), "%s%s", image != NULL ? "\n" : "",
             S(tools[i].label));
    o = NewObject(LABEL_GetClass(), NULL,
        LABEL_DrawInfo, (ULONG)tb.dri,
        LABEL_Justification, LJ_CENTRE,
        LABEL_Underscore, 0,
        IA_FGPen, (ULONG)pen,
        image != NULL && tb.small_font != NULL ? IA_Font : TAG_IGNORE, (ULONG)&tb.small_attr,
        image != NULL ? LABEL_DisposeImage : TAG_IGNORE, TRUE,
        image != NULL ? LABEL_Image : TAG_IGNORE, (ULONG)image,
        LABEL_Text, (ULONG)tb.labels[i],
        TAG_END);
    if (o == NULL && image != NULL) DisposeObject(image);
    return o;
}

/**
 * @brief Create the speedbar node of button i (normal state).
 * @param app The application state.
 * @param i The button index.
 * @return Non-zero on success.
 */
static int make_button(EditorApp *app, ULONG i)
{
    Object *bm = NULL, *img;
    struct Node *node;
    if (tb.mode != TBMODE_TEXT) bm = load_image(app, tools[i].image, "");
    tb.has_image[i] = bm != NULL;
    if (tb.mode == TBMODE_IMAGES && bm != NULL) {
        img = bm;
    } else {
        /* The text, below the image in mode BOTH; a missing image in mode
         * IMAGES gives the text as well. */
        if (tb.mode != TBMODE_BOTH && bm != NULL) { DisposeObject(bm); bm = NULL; }
        img = make_label(i, bm, (LONG)tb.dri->dri_Pens[TEXTPEN]);
    }
    tb.images[i] = img;
    node = AllocSpeedButtonNode(tools[i].command,
        img != NULL ? SBNA_Image : SBNA_Text,
        img != NULL ? (ULONG)img : (ULONG)S(tools[i].label),
        SBNA_Enabled, TRUE,
        SBNA_Spacing, (ULONG)tools[i].spacing,
        SBNA_Highlight, SBH_RECESS,
        TAG_END);
    if (node == NULL) return 0;
    tb.nodes[i] = node;
    AddTail(&tb.buttons, node);
    return 1;
}

/**
 * @brief Load the ghosted look of button i the first time it is needed.
 *
 * Mode IMAGES: \<name\>_g. Mode BOTH: \<name\>_g with the text in the ghost
 * pen. Text (mode TEXT or no image): the text in the ghost pen. Without a
 * ghosted image the button keeps its normal image (and cannot be pressed).
 *
 * @param app The application state.
 * @param i The button index.
 */
static void load_ghost(EditorApp *app, ULONG i)
{
    Object *g;
    if (tb.ghost_tried[i]) return;
    tb.ghost_tried[i] = 1;
    if (tb.has_image[i]) {
        g = load_image(app, tools[i].image, "_g");
        if (g != NULL && tb.mode == TBMODE_BOTH) g = make_label(i, g, ghost_pen());
    } else {
        g = make_label(i, NULL, ghost_pen());
    }
    tb.ghosts[i] = g;
}

ULONG toolbar_tool_count(void)
{
    return NUMTOOLS;
}

int toolbar_create(EditorApp *app)
{
    Object *group;
    ULONG i;

    memset(&tb, 0, sizeof(tb));
    NewList(&tb.buttons);
    NewList(&tb.oflist);
    tb.ghost_pen = -1;
    tb.mode = app->settings.tbmode;
    tb.dri = app->screen_draw_info;
    if (tb.dri == NULL) return 0;
    obtain_ghost_pen(app);
    if (tb.mode == TBMODE_BOTH) small_font(app);
    splash_status(S(tb.mode == TBMODE_TEXT ? MSG_SPLASH_TOOLBAR : MSG_SPLASH_IMAGES));
    for (i = 0; i < NUMTOOLS; ++i) {
        if (!make_button(app, i)) return 0;
        splash_step();
        tb.hints[i].hi_GadgetID = GID_TOOLBAR;
        tb.hints[i].hi_Code = (WORD)tools[i].command;
        tb.hints[i].hi_Text = (STRPTR)S(tools[i].help);
        tb.hints[i].hi_Flags = 0;
    }
    tb.hints[i].hi_GadgetID = GID_OVERFLOW;
    tb.hints[i].hi_Code = -1;
    tb.hints[i].hi_Text = (STRPTR)S(MSG_TB_MORE);
    tb.hints[i].hi_Flags = 0;
    ++i;
    tb.hints[i].hi_GadgetID = -1;
    tb.hints[i].hi_Code = -1;

    tb.bar = NewObject(SPEEDBAR_GetClass(), NULL,
        GA_ID, GID_TOOLBAR,
        GA_RelVerify, TRUE,
        SPEEDBAR_Orientation, SBORIENT_HORIZ,
        SPEEDBAR_Buttons, (ULONG)&tb.buttons,
        /* No frame around the bar; flat buttons get one only while pressed
         * (ButtonBevelStyle is V47). With BVS_NONE bevel.image draws no
         * ghost pattern, ghosted buttons show their ghosted image. */
        SPEEDBAR_BevelStyle, BVS_NONE,
        SPEEDBAR_ButtonBevelStyle, app->settings.tbframes ? BVS_BUTTON : BVS_NONE,
        TAG_END);
    if (tb.bar == NULL) return 0;
    /* Right of the bar: a drop-down chooser (only its arrow) with the
     * buttons that do not fit (toolbar_update()). */
    if (ChooserBase != NULL)
        tb.overflow = NewObject(CHOOSER_GetClass(), NULL,
            GA_ID, GID_OVERFLOW,
            GA_RelVerify, TRUE,
            GA_Disabled, TRUE,
            CHOOSER_DropDown, TRUE,
            CHOOSER_Labels, (ULONG)&tb.oflist,
            CHOOSER_MaxLabels, NUMTOOLS,
            CHOOSER_AutoFit, TRUE,
            TAG_END);
    group = NewObject(LAYOUT_GetClass(), NULL,
        LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ,
        LAYOUT_VertAlignment, LALIGN_CENTER,
        LAYOUT_AddChild, (ULONG)tb.bar,
        CHILD_CacheDomain, FALSE,
        tb.overflow != NULL ? LAYOUT_AddChild : TAG_IGNORE, (ULONG)tb.overflow,
        tb.overflow != NULL ? CHILD_WeightedWidth : TAG_IGNORE, 0,
        /* just wide enough for the arrow, as high as a text line */
        tb.overflow != NULL ? CHILD_MinWidth : TAG_IGNORE, 20,
        tb.overflow != NULL ? CHILD_MaxWidth : TAG_IGNORE, 20,
        tb.overflow != NULL ? CHILD_MaxHeight : TAG_IGNORE,
            (ULONG)(app->screen->Font->ta_YSize + 6),
        TAG_END);
    if (group == NULL) {
        DisposeObject(tb.bar);
        tb.bar = NULL;
        if (tb.overflow != NULL) DisposeObject(tb.overflow);
        tb.overflow = NULL;
        return 0;
    }
    app->toolbar = group;
    return 1;
}

struct HintInfo *toolbar_hints(void)
{
    return tb.hints;
}

void toolbar_window_opened(EditorApp *app)
{
    if (tb.bar != NULL && app->window != NULL)
        SetGadgetAttrs((struct Gadget *)tb.bar, app->window, NULL,
                       SPEEDBAR_Window, (ULONG)app->window, TAG_END);
}

/**
 * @brief Ghost the buttons whose bit is set in @p mask.
 *
 * Ghosted looks are loaded first (outside the detached list). The node
 * list may only be changed while it is detached from the speedbar, which
 * does not redraw itself when it is attached again.
 *
 * @param app The application state.
 * @param mask TOOLBIT() of the buttons to ghost.
 */
static void set_disabled(EditorApp *app, ULONG mask)
{
    ULONG changed = mask ^ tb.disabled, i;
    int detached = 0;
    if (tb.bar == NULL || changed == 0) return;
    for (i = 0; i < NUMTOOLS; ++i)
        if ((changed & mask & TOOLBIT(i)) != 0) load_ghost(app, i);
    tb.disabled = mask;
    for (i = 0; i < NUMTOOLS; ++i) {
        int off;
        Object *img;
        if ((changed & TOOLBIT(i)) == 0) continue;
        off = (mask & TOOLBIT(i)) != 0;
        img = off && tb.ghosts[i] != NULL ? tb.ghosts[i] : tb.images[i];
        if (!detached) {
            SetGadgetAttrs((struct Gadget *)tb.bar, app->window, NULL,
                           SPEEDBAR_Buttons, ~0UL, TAG_END);
            detached = 1;
        }
        SetSpeedButtonNodeAttrs(tb.nodes[i],
            SBNA_Disabled, (ULONG)off,
            img != NULL ? SBNA_Image : TAG_IGNORE, (ULONG)img,
            TAG_END);
    }
    if (!detached) return;
    SetGadgetAttrs((struct Gadget *)tb.bar, app->window, NULL,
                   SPEEDBAR_Buttons, (ULONG)&tb.buttons, TAG_END);
    if (app->window != NULL) RefreshGList((struct Gadget *)tb.bar, app->window, NULL, 1);
}

/**
 * @brief Rebuild the overflow list when the visible or the ghosted buttons
 *        changed. The chooser is ghosted when every button fits.
 * @param app The application state.
 */
static void update_overflow(EditorApp *app)
{
    ULONG visible = 0, n = 0, i;
    struct Node *node, *next;
    if (tb.overflow == NULL || app->window == NULL) return;
    GetAttr(SPEEDBAR_Visible, tb.bar, &visible);
    if (visible == tb.of_visible && tb.disabled == tb.of_disabled) return;
    tb.of_visible = visible;
    tb.of_disabled = tb.disabled;
    SetGadgetAttrs((struct Gadget *)tb.overflow, app->window, NULL,
                   CHOOSER_Labels, ~0UL, TAG_END);
    for (node = tb.oflist.lh_Head; (next = node->ln_Succ) != NULL; node = next) {
        Remove(node);
        FreeChooserNode(node);
    }
    /* The bar is never scrolled: the buttons after the visible ones are
     * the hidden ones. */
    for (i = 0; i < NUMTOOLS; ++i, ++n) {
        struct Node *c;
        if (n < visible) continue;
        c = AllocChooserNode(
            CNA_Text, (ULONG)S(tools[i].label),
            CNA_UserData, tools[i].command,
            CNA_Disabled, (tb.disabled & TOOLBIT(i)) != 0,
            TAG_END);
        if (c != NULL) AddTail(&tb.oflist, c);
    }
    SetGadgetAttrs((struct Gadget *)tb.overflow, app->window, NULL,
                   CHOOSER_Labels, (ULONG)&tb.oflist,
                   GA_Disabled, IsListEmpty(&tb.oflist),
                   TAG_END);
}

void toolbar_update(EditorApp *app)
{
    ULONG mask = 0, undo = TRUE, redo = TRUE, marked = TRUE, i;
    Document *doc = app->active;
    if (tb.bar == NULL || app->window == NULL) return;
    if (doc != NULL) {
        GetAttr(GA_TEXTEDITOR_UndoAvailable, doc->editor, &undo);
        GetAttr(GA_TEXTEDITOR_RedoAvailable, doc->editor, &redo);
        GetAttr(GA_TEXTEDITOR_AreaMarked, doc->editor, &marked);
    }
    for (i = 0; i < NUMTOOLS; ++i) {
        switch (tools[i].command) {
            case MID_SAVE: if (doc == NULL || !doc->dirty) mask |= TOOLBIT(i); break;
            case MID_UNDO: if (!undo) mask |= TOOLBIT(i); break;
            case MID_REDO: if (!redo) mask |= TOOLBIT(i); break;
            case MID_CUT:
            case MID_COPY: if (!marked) mask |= TOOLBIT(i); break;
            default: break;
        }
    }
    set_disabled(app, mask);
    update_overflow(app);
}

ULONG toolbar_overflow_command(ULONG index)
{
    struct Node *node;
    ULONG command = 0;
    for (node = tb.oflist.lh_Head; node->ln_Succ != NULL && index != 0; node = node->ln_Succ)
        --index;
    if (node->ln_Succ == NULL) return 0;
    GetChooserNodeAttrs(node, CNA_UserData, (ULONG)&command, TAG_END);
    return command;
}

void toolbar_dispose(EditorApp *app)
{
    struct Node *node, *next;
    ULONG i;
    if (tb.buttons.lh_Head != NULL)
        for (node = tb.buttons.lh_Head; (next = node->ln_Succ) != NULL; node = next)
            FreeSpeedButtonNode(node);
    if (tb.oflist.lh_Head != NULL)
        for (node = tb.oflist.lh_Head; (next = node->ln_Succ) != NULL; node = next)
            FreeChooserNode(node);
    for (i = 0; i < NUMTOOLS; ++i) {
        if (tb.images[i] != NULL) DisposeObject(tb.images[i]);
        if (tb.ghosts[i] != NULL) DisposeObject(tb.ghosts[i]);
    }
    if (tb.small_font != NULL) CloseFont(tb.small_font);   /* after the labels */
    if (tb.ghost_pen >= 0 && app->screen != NULL)
        ReleasePen(app->screen->ViewPort.ColorMap, (ULONG)tb.ghost_pen);
    memset(&tb, 0, sizeof(tb));
}
