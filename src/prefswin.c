/**
 * @file prefswin.c
 * @brief The settings window (categories General, Colours, Toolbar, Project Defaults, Advanced and the file types), built like Kickdown's.
 *
 * Built after the ReAction style guide of amiga-devkit (section 4.2): a
 * list of categories on the left, the options of the selected one on the
 * right (page.gadget), Save/Use/Cancel below. The window is modal: the
 * main window shows the busy pointer and its input is dropped. The page
 * General is divided into two framed groups, Window (size and position)
 * and View (what is shown at the start); the other pages are one framed
 * group each.
 *
 * Below the categories, the list holds File Types with Standard (plain
 * text and every language without settings of its own) and the languages
 * TinyDev-Scout knows (a hierarchical listbrowser; the nodes carry their
 * meaning as LBNA_UserData). All file types share one page: selecting
 * another one first takes the page's values into the copy of the settings
 * being edited, then shows those of the new one. A language may use the
 * standard settings ("Use the standard settings" ghosts the rest); it
 * then has no entry of its own.
 *
 * The groups are built with complete tag lists (CHILD_Label applies to the
 * child of the same tag list). The gadgets of a page keep their height; an
 * empty group at the end takes the rest. Gadgets on a hidden page are
 * changed with SetPageGadgetAttrs(). Keyboard shortcuts are unique in the
 * whole window, in every language. The classes the window needs are
 * opened when it is first used and closed by prefs_cleanup().
 *
 * The page Advanced chooses the output window's font with getfont.gadget;
 * its GETFONT_TextAttr is copied by the class (name included), so the
 * settings only take the name and size when the window is left.
 */
#include "editor.h"

#include <classes/window.h>
#include <gadgets/button.h>
#include <gadgets/checkbox.h>
#include <gadgets/chooser.h>
#include <gadgets/getcolor.h>
#include <gadgets/getfont.h>
#include <gadgets/integer.h>
#include <gadgets/layout.h>
#include <gadgets/listbrowser.h>
#include <gadgets/string.h>
#include <libraries/asl.h>
#include <images/bevel.h>
#include <images/label.h>
#include <intuition/gadgetclass.h>
#include <proto/asl.h>
#include <proto/button.h>
#include <proto/checkbox.h>
#include <proto/chooser.h>
#include <proto/exec.h>
#include <proto/getcolor.h>
#include <proto/getfont.h>
#include <proto/graphics.h>
#include <graphics/gfxbase.h>
#include <proto/integer.h>
#include <proto/intuition.h>
#include <proto/label.h>
#include <proto/layout.h>
#include <proto/listbrowser.h>
#include <proto/string.h>
#include <proto/window.h>
#include <clib/alib_protos.h>

#include <stdio.h>
#include <string.h>

/**
 * listbrowser.gadget, opened with the window (also used by others).
 */
struct Library *ListBrowserBase = NULL;
/**
 * checkbox.gadget, opened with the window (also used by others).
 */
struct Library *CheckBoxBase = NULL;
/**
 * integer.gadget, opened with the window.
 */
struct Library *IntegerBase = NULL;
/**
 * getcolor.gadget, opened with the window.
 */
struct Library *GetColorBase = NULL;
/**
 * getfont.gadget, opened with the window.
 */
struct Library *GetFontBase = NULL;
extern struct Library *StringBase;         /* shared with the find window */

enum {
    PG_LIST = 100,
    PG_WIDTH, PG_HEIGHT, PG_LEFT, PG_TOP, PG_CURSIZE, PG_AUTOSIZE, PG_SPLASH,
    PG_FOLDERTREE, PG_MINIMAP, PG_HIDEINFO,
    PG_COLOUR, PG_DEFCOLOURS = PG_COLOUR + SCOL_COUNT,
    PG_TBMODE, PG_TBFRAMES,
    PG_PROJECTDIR, PG_CHOOSEPROJECT, PG_AUTHOR, PG_AUTHORMAIL,
    PG_SCOUT, PG_TEMPDIR, PG_CHOOSETEMP, PG_SHELLWINDOW, PG_OUTFONTOWN, PG_OUTFONT,
    PG_FT_NAME, PG_FT_STANDARD, PG_FT_TABSIZE, PG_FT_INDENTWITH, PG_FT_INDENT, PG_FT_AUTO,
    PG_FT_TABKEY, PG_FT_NUMBERS, PG_FT_CUTCOPY,
    PG_SAVE, PG_USE, PG_CANCEL
};

/**
 * Names of the categories (MSG_), in the order of the pages.
 */
static const LONG categories[] = {
    MSG_CAT_GENERAL, MSG_CAT_COLOURS, MSG_CAT_TOOLBAR, MSG_CAT_PROJECT, MSG_CAT_ADVANCED
};
/**
 * Number of categories (pages) before the file types.
 */
#define NUMCATEGORIES 5
/** The page of the file types (after the categories). */
#define PAGE_FILETYPES NUMCATEGORIES
/** LBNA_UserData of the list's nodes: the categories are their page numbers. */
#define UD_FILETYPES 100        /**< "File Types" (shows Standard) */
#define UD_STANDARD 101         /**< Standard */
#define UD_LANGUAGE 102         /**< the first of Scout's languages (in their order) */

/** Labels of the chooser "Indent with" (indent with spaces, with tabs). */
static const LONG indentwith_texts[2] = { MSG_PREFS_FT_SPACES, MSG_PREFS_FT_TABS };
static STRPTR indentwith[3];        /**< its labels, filled from indentwith_texts[] */
/** Labels of the chooser "Tab inside the text" (indents the line, the text after the cursor). */
static const LONG tabkey_texts[2] = { MSG_PREFS_FT_TABLINE, MSG_PREFS_FT_TABAFTER };
static STRPTR tabkey[3];            /**< its labels, filled from tabkey_texts[] */

/** Chooser labels, in the order of enum ToolbarMode. */
static const LONG tbmode_texts[TBMODE_COUNT] = {
    MSG_PREFS_TBMODE_IMAGES, MSG_PREFS_TBMODE_BOTH, MSG_PREFS_TBMODE_TEXT
};
static STRPTR tbmodes[TBMODE_COUNT + 1];      /**< the chooser's labels, filled from tbmode_texts[] */

/** Labels and requester titles of the colours, SettingsColour order. */
static const LONG colour_labels[SCOL_COUNT] = {
    MSG_PREFS_COL_TEXT, MSG_PREFS_COL_KEYWORD, MSG_PREFS_COL_STRING, MSG_PREFS_COL_COMMENT,
    MSG_PREFS_COL_PREPROC, MSG_PREFS_COL_TYPE, MSG_PREFS_COL_FUNCTION, MSG_PREFS_COL_MACRO,
    MSG_PREFS_COL_CONSTANT, MSG_PREFS_COL_VARIABLE
};
/**
 * Titles of the colour requesters (MSG_), SettingsColour order.
 */
static const LONG colour_titles[SCOL_COUNT] = {
    MSG_PREFS_TTL_TEXT, MSG_PREFS_TTL_KEYWORD, MSG_PREFS_TTL_STRING, MSG_PREFS_TTL_COMMENT,
    MSG_PREFS_TTL_PREPROC, MSG_PREFS_TTL_TYPE, MSG_PREFS_TTL_FUNCTION, MSG_PREFS_TTL_MACRO,
    MSG_PREFS_TTL_CONSTANT, MSG_PREFS_TTL_VARIABLE
};

/**
 * @name Spacing
 * In "virtual pixels" (layout.gadget scales them). The labels end with a
 * space: label.image has no margin of its own.
 */
/** @{ */
#define FRAME_SIDE   8              /**< inside the frame of a page, left and right */
#define FRAME_TOPBOT 6              /**< inside the frame of a page, above and below */
#define ROW_SPACING  4              /**< between the rows of a page */
#define AREA_SPACING 6              /**< window border, list, frame, buttons */
/** @} */

/** tags of a page or a group of one: vertical group with a frame and a title */
#define PAGE_GROUP(title) \
    LAYOUT_Orientation, LAYOUT_ORIENT_VERT, LAYOUT_BevelStyle, BVS_GROUP, \
    LAYOUT_Label, (ULONG)(title), LAYOUT_SpaceOuter, TRUE, \
    LAYOUT_LeftSpacing, FRAME_SIDE, LAYOUT_RightSpacing, FRAME_SIDE, \
    LAYOUT_TopSpacing, FRAME_TOPBOT, LAYOUT_BottomSpacing, FRAME_TOPBOT, \
    LAYOUT_InnerSpacing, ROW_SPACING

/** the child just added keeps its height */
#define FIXED       CHILD_WeightedHeight, 0

/** Objects of the open settings window. */
static struct {
    Object *winobj, *root, *list, *page;
    Object *width, *height, *left, *top, *splash, *foldertree, *minimap, *hideinfo;
    Object *colours[SCOL_COUNT];
    Object *tbmode, *tbframes;
    Object *projectdir, *author, *authormail;
    Object *scout, *tempdir, *shellwindow, *outfontown, *outfont;
    Object *ft_name, *ft_standard, *ft_tabsize, *ft_indentwith, *ft_indent, *ft_auto,
           *ft_tabkey, *ft_numbers, *ft_cutcopy;
    FileTypes *ft;          /**< the settings per file type being edited */
    int ft_current;         /**< the file type on the page: -1 Standard, else a language's index */
    struct Window *win;
    struct List catlist;
} pw;

/**
 * @brief Open the classes the window needs (once).
 * @param app The application state (for the requester).
 * @return Non-zero if all are available.
 */
static int open_classes(EditorApp *app)
{
    static const struct { struct Library **base; const char *name; } cl[] = {
        { &ListBrowserBase, "gadgets/listbrowser.gadget" },
        { &ChooserBase,     "gadgets/chooser.gadget" },
        { &CheckBoxBase,    "gadgets/checkbox.gadget" },
        { &IntegerBase,     "gadgets/integer.gadget" },
        { &GetColorBase,    "gadgets/getcolor.gadget" },
        { &GetFontBase,     "gadgets/getfont.gadget" },
        { &LabelBase,       "images/label.image" },
        { &StringBase,      "gadgets/string.gadget" },
    };
    ULONG i;
    for (i = 0; i < sizeof(cl) / sizeof(cl[0]); ++i)
        if (*cl[i].base == NULL &&
            (*cl[i].base = OpenLibrary((STRPTR)cl[i].name, 44)) == NULL) {
            char message[160];
            snprintf(message, sizeof(message), S(MSG_PREFS_NEEDCLASS), cl[i].name);
            ui_error(app, APP_NAME, message);
            return 0;
        }
    return 1;
}

void prefs_cleanup(void)
{
    struct Library **b[] = { &ListBrowserBase, &CheckBoxBase, &IntegerBase, &GetColorBase, &GetFontBase };
    ULONG i;
    for (i = 0; i < sizeof(b) / sizeof(b[0]); ++i)
        if (*b[i] != NULL) {
            CloseLibrary(*b[i]);
            *b[i] = NULL;
        }
}

/**
 * @brief A label.image with a text.
 * @param text The text ('_' marks the shortcut).
 * @return The label.
 */
static Object *label(const char *text)
{
    return NewObject(LABEL_GetClass(), NULL, LABEL_Text, (ULONG)text, TAG_END);
}

/**
 * @brief A checkbox.
 * @param id Gadget ID.
 * @param text Its text.
 * @param on Checked.
 * @return The gadget.
 */
static Object *checkbox(ULONG id, const char *text, int on)
{
    return NewObject(CHECKBOX_GetClass(), NULL,
        GA_ID, id, GA_RelVerify, TRUE, GA_Text, (ULONG)text,
        CHECKBOX_Checked, (ULONG)on, TAG_END);
}

/**
 * @brief An integer gadget.
 * @param id Gadget ID.
 * @param value Its value.
 * @param min Smallest value.
 * @return The gadget.
 */
static Object *integer(ULONG id, long value, long min)
{
    return NewObject(INTEGER_GetClass(), NULL,
        GA_ID, id, GA_RelVerify, TRUE, INTEGER_Number, (ULONG)value,
        INTEGER_Minimum, (ULONG)min, INTEGER_Maximum, 9999, INTEGER_MaxChars, 4, TAG_END);
}

/**
 * @brief A button as wide as its text.
 * @param id Gadget ID.
 * @param text Its text.
 * @return The gadget.
 */
static Object *button(ULONG id, const char *text)
{
    return NewObject(BUTTON_GetClass(), NULL,
        GA_ID, id, GA_RelVerify, TRUE, GA_Text, (ULONG)text,
        BUTTON_TextPadding, TRUE, TAG_END);
}

/**
 * @brief Make a string gadget.
 * @param id Its ID.
 * @param text Its text.
 * @param max The size of the setting it edits (with the terminator).
 * @return The gadget.
 */
static Object *string(ULONG id, const char *text, ULONG max)
{
    return NewObject(STRING_GetClass(), NULL,
        GA_ID, id, GA_RelVerify, TRUE, GA_TabCycle, TRUE,
        STRINGA_MaxChars, max, STRINGA_TextVal, (ULONG)text, TAG_END);
}

/** @brief An empty group that takes the remaining space. @return It. */
static Object *filler(void)
{
    return NewObject(LAYOUT_GetClass(), NULL, TAG_END);
}

/**
 * @brief Width of the category list: its longest entry plus margins.
 * @param app The application state (screen font).
 * @return The width in pixels.
 */
static LONG list_width(EditorApp *app)
{
    LONG w = 0, t;
    struct Node *n;
    for (n = pw.catlist.lh_Head; n->ln_Succ != NULL; n = n->ln_Succ) {
        STRPTR text = NULL;
        ULONG gen = 1;
        GetListBrowserNodeAttrs(n, LBNCA_Text, (ULONG)&text, LBNA_Generation, (ULONG)&gen, TAG_END);
        if (text == NULL) continue;
        /* a generation is indented about as much as a tree's knob */
        t = TextLength(&app->screen->RastPort, text, (UWORD)strlen((const char *)text)) + (LONG)gen * 16;
        if (t > w) w = t;
    }
    return w + 24;
}

/**
 * @brief Add a node to the category list.
 * @param text Its text.
 * @param generation 1 for a category, 2 for a file type.
 * @param flags LBNA_Flags.
 * @param ud Its meaning (a page number or UD_...).
 */
static void add_category(const char *text, ULONG generation, ULONG flags, ULONG ud)
{
    struct Node *n = AllocListBrowserNode(1, LBNCA_Text, (ULONG)text, LBNA_Generation, generation,
                                          LBNA_Flags, flags, LBNA_UserData, ud, TAG_END);
    if (n != NULL) AddTail(&pw.catlist, n);
}

/**
 * @brief An integer gadget for a width of the file type page (1 to
 *        FILETYPES_MAX_WIDTH, with arrows).
 * @param id Gadget ID.
 * @return The gadget.
 */
static Object *width_gadget(ULONG id)
{
    return NewObject(INTEGER_GetClass(), NULL,
        GA_ID, id, GA_RelVerify, TRUE, INTEGER_Number, 1, INTEGER_Arrows, TRUE,
        INTEGER_Minimum, 1, INTEGER_Maximum, FILETYPES_MAX_WIDTH, INTEGER_MaxChars, 2, TAG_END);
}

/**
 * @brief A pop-up chooser.
 * @param id Gadget ID.
 * @param labels Its labels (NULL-terminated).
 * @return The gadget.
 */
static Object *popup(ULONG id, STRPTR *labels)
{
    return NewObject(CHOOSER_GetClass(), NULL,
        GA_ID, id, GA_RelVerify, TRUE, CHOOSER_PopUp, TRUE,
        CHOOSER_LabelArray, (ULONG)labels, CHOOSER_Selected, 0, TAG_END);
}

/**
 * @brief The font the output window uses without a font of its own: the
 *        system default text font.
 * @param[out] ta Its attributes; ta_Name points into the font, which stays
 *        in memory.
 */
static void default_font(struct TextAttr *ta)
{
    struct TextFont *f = GfxBase->DefaultFont;
    memset(ta, 0, sizeof(*ta));
    ta->ta_Name = f->tf_Message.mn_Node.ln_Name;
    ta->ta_YSize = f->tf_YSize;
}

/**
 * @brief Build the window's contents from the settings.
 * @param app The application state.
 * @param s The settings shown.
 * @return The root layout, or NULL.
 */
static Object *build(EditorApp *app, const Settings *s)
{
    Object *general, *colours, *toolbar, *project, *advanced, *filetypes;
    struct TextAttr font;
    ULONG i;
    int k, own_font;

    NewList(&pw.catlist);
    for (i = 0; i < NUMCATEGORIES; ++i) add_category(S(categories[i]), 1, 0, i);
    add_category(S(MSG_CAT_FILETYPES), 1, LBFLG_HASCHILDREN | LBFLG_SHOWCHILDREN, UD_FILETYPES);
    add_category(S(MSG_PREFS_FT_STANDARDNAME), 2, 0, UD_STANDARD);
    for (k = 0; k < scoutlink_language_count(); ++k)
        add_category(scoutlink_language_name(k), 2, 0, (ULONG)(UD_LANGUAGE + k));
    pw.list = NewObject(LISTBROWSER_GetClass(), NULL,
        GA_ID, PG_LIST,
        GA_RelVerify, TRUE,
        LISTBROWSER_Labels, (ULONG)&pw.catlist,
        LISTBROWSER_Hierarchical, TRUE,
        LISTBROWSER_ShowSelected, TRUE,
        LISTBROWSER_Selected, 0,
        LISTBROWSER_AutoFit, TRUE,
        LISTBROWSER_VerticalProp, FALSE,
        TAG_END);

    /* General: the groups Window and View, without a frame of their own */
    pw.width = integer(PG_WIDTH, s->winwidth, 0);
    pw.height = integer(PG_HEIGHT, s->winheight, 0);
    pw.left = integer(PG_LEFT, s->winleft, -1);
    pw.top = integer(PG_TOP, s->wintop, -1);
    pw.splash = checkbox(PG_SPLASH, S(MSG_PREFS_SPLASH), s->splash);
    pw.foldertree = checkbox(PG_FOLDERTREE, S(MSG_PREFS_FOLDERTREE), s->foldertree);
    pw.minimap = checkbox(PG_MINIMAP, S(MSG_PREFS_MINIMAP), s->minimap);
    pw.hideinfo = checkbox(PG_HIDEINFO, S(MSG_PREFS_HIDEINFO), !s->showinfo);
    general = NewObject(LAYOUT_GetClass(), NULL,
        LAYOUT_Orientation, LAYOUT_ORIENT_VERT,
        LAYOUT_InnerSpacing, AREA_SPACING,
        LAYOUT_AddChild, (ULONG)NewObject(LAYOUT_GetClass(), NULL, PAGE_GROUP(S(MSG_PREFS_GRP_WINDOW)),
            LAYOUT_AddChild, (ULONG)pw.width, FIXED,
            CHILD_Label, (ULONG)label(S(MSG_PREFS_WIDTH)),
            LAYOUT_AddChild, (ULONG)pw.height, FIXED,
            CHILD_Label, (ULONG)label(S(MSG_PREFS_HEIGHT)),
            LAYOUT_AddChild, (ULONG)pw.left, FIXED,
            CHILD_Label, (ULONG)label(S(MSG_PREFS_LEFT)),
            LAYOUT_AddChild, (ULONG)pw.top, FIXED,
            CHILD_Label, (ULONG)label(S(MSG_PREFS_TOP)),
            LAYOUT_AddImage, (ULONG)label(S(MSG_PREFS_WINDOWNOTE)),
            FIXED,
            LAYOUT_AddChild, (ULONG)NewObject(LAYOUT_GetClass(), NULL,
                LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ,
                LAYOUT_EvenSize, TRUE,
                LAYOUT_AddChild, (ULONG)filler(),
                LAYOUT_AddChild, (ULONG)button(PG_CURSIZE, S(MSG_PREFS_CURRENT)),
                CHILD_WeightedWidth, 0,
                LAYOUT_AddChild, (ULONG)button(PG_AUTOSIZE, S(MSG_PREFS_AUTOMATIC)),
                CHILD_WeightedWidth, 0,
                TAG_END),
            FIXED,
            TAG_END),
        FIXED,
        LAYOUT_AddChild, (ULONG)NewObject(LAYOUT_GetClass(), NULL, PAGE_GROUP(S(MSG_PREFS_GRP_VIEW)),
            LAYOUT_AddImage, (ULONG)label(S(MSG_PREFS_ATSTART)), FIXED,
            LAYOUT_AddChild, (ULONG)pw.splash, FIXED,
            LAYOUT_AddChild, (ULONG)pw.foldertree, FIXED,
            LAYOUT_AddChild, (ULONG)pw.minimap, FIXED,
            LAYOUT_AddImage, (ULONG)label(S(MSG_PREFS_VIEWNOTE)), FIXED,
            LAYOUT_AddChild, (ULONG)pw.hideinfo, FIXED,
            TAG_END),
        FIXED,
        LAYOUT_AddChild, (ULONG)filler(),
        TAG_END);

    /* Colours */
    for (i = 0; i < SCOL_COUNT; ++i)
        pw.colours[i] = NewObject(GETCOLOR_GetClass(), NULL,
            GA_ID, PG_COLOUR + i, GA_RelVerify, TRUE,
            GETCOLOR_Screen, (ULONG)app->screen, GETCOLOR_Color, s->colours[i],
            GETCOLOR_TitleText, (ULONG)S(colour_titles[i]), TAG_END);
    colours = NewObject(LAYOUT_GetClass(), NULL, PAGE_GROUP(S(categories[1])),
        LAYOUT_AddChild, (ULONG)pw.colours[0], FIXED, CHILD_Label, (ULONG)label(S(colour_labels[0])),
        LAYOUT_AddChild, (ULONG)pw.colours[1], FIXED, CHILD_Label, (ULONG)label(S(colour_labels[1])),
        LAYOUT_AddChild, (ULONG)pw.colours[2], FIXED, CHILD_Label, (ULONG)label(S(colour_labels[2])),
        LAYOUT_AddChild, (ULONG)pw.colours[3], FIXED, CHILD_Label, (ULONG)label(S(colour_labels[3])),
        LAYOUT_AddChild, (ULONG)pw.colours[4], FIXED, CHILD_Label, (ULONG)label(S(colour_labels[4])),
        LAYOUT_AddChild, (ULONG)pw.colours[5], FIXED, CHILD_Label, (ULONG)label(S(colour_labels[5])),
        LAYOUT_AddChild, (ULONG)pw.colours[6], FIXED, CHILD_Label, (ULONG)label(S(colour_labels[6])),
        LAYOUT_AddChild, (ULONG)pw.colours[7], FIXED, CHILD_Label, (ULONG)label(S(colour_labels[7])),
        LAYOUT_AddChild, (ULONG)pw.colours[8], FIXED, CHILD_Label, (ULONG)label(S(colour_labels[8])),
        LAYOUT_AddChild, (ULONG)pw.colours[9], FIXED, CHILD_Label, (ULONG)label(S(colour_labels[9])),
        LAYOUT_AddImage, (ULONG)label(S(MSG_PREFS_COLOURNOTE)),
        FIXED,
        /* the button on the right, not across the page */
        LAYOUT_AddChild, (ULONG)NewObject(LAYOUT_GetClass(), NULL,
            LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ,
            LAYOUT_AddChild, (ULONG)filler(),
            LAYOUT_AddChild, (ULONG)button(PG_DEFCOLOURS, S(MSG_PREFS_DEFCOLOURS)),
            CHILD_WeightedWidth, 0,
            TAG_END),
        FIXED,
        LAYOUT_AddChild, (ULONG)filler(),
        TAG_END);

    /* Toolbar */
    for (i = 0; i < TBMODE_COUNT; ++i) tbmodes[i] = (STRPTR)S(tbmode_texts[i]);
    pw.tbmode = NewObject(CHOOSER_GetClass(), NULL,
        GA_ID, PG_TBMODE, GA_RelVerify, TRUE, CHOOSER_PopUp, TRUE,
        CHOOSER_LabelArray, (ULONG)tbmodes,
        CHOOSER_Selected, (ULONG)(s->tbmode >= 0 && s->tbmode < TBMODE_COUNT ? s->tbmode : 0),
        TAG_END);
    pw.tbframes = checkbox(PG_TBFRAMES, S(MSG_PREFS_FRAMES), s->tbframes);
    toolbar = NewObject(LAYOUT_GetClass(), NULL, PAGE_GROUP(S(categories[2])),
        LAYOUT_AddChild, (ULONG)pw.tbmode, FIXED,
        CHILD_Label, (ULONG)label(S(MSG_PREFS_DISPLAY)),
        LAYOUT_AddChild, (ULONG)pw.tbframes, FIXED,
        LAYOUT_AddImage, (ULONG)label(S(MSG_PREFS_TOOLBARNOTE)),
        FIXED,
        LAYOUT_AddChild, (ULONG)filler(),
        TAG_END);

    /* Project Defaults: the drawer of new projects, author, e-mail address */
    pw.projectdir = string(PG_PROJECTDIR, s->projectdir, sizeof(s->projectdir));
    pw.author = string(PG_AUTHOR, s->author, sizeof(s->author));
    pw.authormail = string(PG_AUTHORMAIL, s->authormail, sizeof(s->authormail));
    project = NewObject(LAYOUT_GetClass(), NULL, PAGE_GROUP(S(categories[3])),
        LAYOUT_AddChild, (ULONG)NewObject(LAYOUT_GetClass(), NULL,
            LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ,
            LAYOUT_AddChild, (ULONG)pw.projectdir,
            LAYOUT_AddChild, (ULONG)button(PG_CHOOSEPROJECT, S(MSG_PREFS_CHOOSE)),
            CHILD_WeightedWidth, 0,
            TAG_END),
        FIXED,
        CHILD_Label, (ULONG)label(S(MSG_PREFS_PROJECTDIR)),
        LAYOUT_AddChild, (ULONG)pw.author, FIXED,
        CHILD_Label, (ULONG)label(S(MSG_PREFS_AUTHOR)),
        LAYOUT_AddChild, (ULONG)pw.authormail, FIXED,
        CHILD_Label, (ULONG)label(S(MSG_PREFS_AUTHORMAIL)),
        LAYOUT_AddImage, (ULONG)label(S(MSG_PREFS_PROJNOTE)),
        FIXED,
        LAYOUT_AddChild, (ULONG)filler(),
        TAG_END);

    /* Advanced: Scout, the drawer of the temporary files, the Shell window */
    pw.scout = checkbox(PG_SCOUT, S(MSG_PREFS_SCOUT), s->scout);
    pw.tempdir = string(PG_TEMPDIR, s->tempdir, sizeof(s->tempdir));
    pw.shellwindow = string(PG_SHELLWINDOW, s->shellwindow, sizeof(s->shellwindow));
    /* the output window's font: without one of its own the system's is
     * shown, ghosted; any font may be chosen, not only monospaced ones */
    own_font = s->outfont[0] != '\0' && s->outfontsize > 0;
    default_font(&font);
    if (own_font) {
        font.ta_Name = (STRPTR)s->outfont;
        font.ta_YSize = (UWORD)s->outfontsize;
    }
    pw.outfontown = checkbox(PG_OUTFONTOWN, S(MSG_PREFS_OUTFONTOWN), own_font);
    pw.outfont = NewObject(GETFONT_GetClass(), NULL,
        GA_ID, PG_OUTFONT, GA_RelVerify, TRUE, GA_Disabled, (ULONG)!own_font,
        GETFONT_TextAttr, (ULONG)&font,          /* copied */
        GETFONT_TitleText, (ULONG)S(MSG_PREFS_ASL_OUTFONT),
        GETFONT_MaxHeight, 64,
        TAG_END);
    advanced = NewObject(LAYOUT_GetClass(), NULL, PAGE_GROUP(S(categories[4])),
        LAYOUT_AddChild, (ULONG)pw.scout, FIXED,
        LAYOUT_AddChild, (ULONG)NewObject(LAYOUT_GetClass(), NULL,
            LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ,
            LAYOUT_AddChild, (ULONG)pw.tempdir,
            LAYOUT_AddChild, (ULONG)button(PG_CHOOSETEMP, S(MSG_PREFS_CHOOSE)),
            CHILD_WeightedWidth, 0,
            TAG_END),
        FIXED,
        CHILD_Label, (ULONG)label(S(MSG_PREFS_TEMPDIR)),
        LAYOUT_AddChild, (ULONG)pw.shellwindow, FIXED,
        CHILD_Label, (ULONG)label(S(MSG_PREFS_SHELLWINDOW)),
        LAYOUT_AddChild, (ULONG)pw.outfontown, FIXED,
        LAYOUT_AddChild, (ULONG)pw.outfont, FIXED,
        CHILD_Label, (ULONG)label(S(MSG_PREFS_OUTFONT)),
        LAYOUT_AddImage, (ULONG)label(S(MSG_PREFS_OUTFONTNOTE)),
        FIXED,
        LAYOUT_AddImage, (ULONG)label(S(MSG_PREFS_ADVNOTE)),
        FIXED,
        LAYOUT_AddChild, (ULONG)filler(),
        TAG_END);

    /* File types: one page for all, filled by ft_show() */
    for (i = 0; i < 2; ++i) {
        indentwith[i] = (STRPTR)S(indentwith_texts[i]);
        tabkey[i] = (STRPTR)S(tabkey_texts[i]);
    }
    /* the name of the file type shown; it may hold a '_' */
    pw.ft_name = NewObject(BUTTON_GetClass(), NULL,
        GA_ID, PG_FT_NAME, GA_ReadOnly, TRUE, GA_Underscore, 0, GA_Text, (ULONG)"",
        BUTTON_BevelStyle, BVS_NONE, BUTTON_Transparent, TRUE, BUTTON_Justification, BCJ_LEFT,
        TAG_END);
    pw.ft_standard = checkbox(PG_FT_STANDARD, S(MSG_PREFS_FT_USESTANDARD), 0);
    pw.ft_tabsize = width_gadget(PG_FT_TABSIZE);
    pw.ft_indentwith = popup(PG_FT_INDENTWITH, indentwith);
    pw.ft_indent = width_gadget(PG_FT_INDENT);
    pw.ft_auto = checkbox(PG_FT_AUTO, S(MSG_PREFS_FT_AUTOINDENT), 0);
    pw.ft_tabkey = popup(PG_FT_TABKEY, tabkey);
    pw.ft_numbers = checkbox(PG_FT_NUMBERS, S(MSG_PREFS_FT_LINENUMBERS), 0);
    pw.ft_cutcopy = checkbox(PG_FT_CUTCOPY, S(MSG_PREFS_FT_CUTCOPY), 0);
    filetypes = NewObject(LAYOUT_GetClass(), NULL, PAGE_GROUP(S(MSG_CAT_FILETYPES)),
        LAYOUT_AddChild, (ULONG)pw.ft_name, FIXED,
        CHILD_Label, (ULONG)label(S(MSG_PREFS_FT_NAME)),
        LAYOUT_AddChild, (ULONG)pw.ft_standard, FIXED,
        LAYOUT_AddChild, (ULONG)pw.ft_tabsize, FIXED, CHILD_WeightedWidth, 0,
        CHILD_Label, (ULONG)label(S(MSG_PREFS_FT_TABSIZE)),
        LAYOUT_AddChild, (ULONG)pw.ft_indentwith, FIXED,
        CHILD_Label, (ULONG)label(S(MSG_PREFS_FT_INDENTWITH)),
        LAYOUT_AddChild, (ULONG)pw.ft_indent, FIXED, CHILD_WeightedWidth, 0,
        CHILD_Label, (ULONG)label(S(MSG_PREFS_FT_INDENT)),
        LAYOUT_AddChild, (ULONG)pw.ft_auto, FIXED,
        LAYOUT_AddChild, (ULONG)pw.ft_tabkey, FIXED,
        CHILD_Label, (ULONG)label(S(MSG_PREFS_FT_TABKEY)),
        LAYOUT_AddChild, (ULONG)pw.ft_numbers, FIXED,
        LAYOUT_AddChild, (ULONG)pw.ft_cutcopy, FIXED,
        LAYOUT_AddImage, (ULONG)label(S(MSG_PREFS_FT_NOTE)),
        FIXED,
        LAYOUT_AddChild, (ULONG)filler(),
        TAG_END);

    pw.page = NewObject(PAGE_GetClass(), NULL,
        PAGE_Add, (ULONG)general,
        PAGE_Add, (ULONG)colours,
        PAGE_Add, (ULONG)toolbar,
        PAGE_Add, (ULONG)project,
        PAGE_Add, (ULONG)advanced,
        PAGE_Add, (ULONG)filetypes,
        PAGE_Current, 0,
        TAG_END);

    return NewObject(LAYOUT_GetClass(), NULL,
        LAYOUT_Orientation, LAYOUT_ORIENT_VERT,
        LAYOUT_SpaceOuter, TRUE,
        LAYOUT_LeftSpacing, AREA_SPACING,
        LAYOUT_RightSpacing, AREA_SPACING,
        LAYOUT_TopSpacing, AREA_SPACING,
        LAYOUT_BottomSpacing, AREA_SPACING,
        LAYOUT_InnerSpacing, AREA_SPACING,
        LAYOUT_DeferLayout, TRUE,
        LAYOUT_AddChild, (ULONG)NewObject(LAYOUT_GetClass(), NULL,
            LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ,
            /* the list as wide as its longest entry; the bar lets the user
             * move the border between list and page */
            LAYOUT_AddChild, (ULONG)pw.list,
            CHILD_WeightedWidth, 20,
            CHILD_MinWidth, (ULONG)list_width(app),
            LAYOUT_WeightBar, TRUE,
            LAYOUT_AddChild, (ULONG)pw.page,
            CHILD_WeightedWidth, 80,
            TAG_END),
        /* positive actions left, negative right, each as wide as its text */
        LAYOUT_AddChild, (ULONG)NewObject(LAYOUT_GetClass(), NULL,
            LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ,
            LAYOUT_AddChild, (ULONG)button(PG_SAVE, S(MSG_PREFS_SAVE)),
            CHILD_WeightedWidth, 0,
            LAYOUT_AddChild, (ULONG)button(PG_USE, S(MSG_PREFS_USE)),
            CHILD_WeightedWidth, 0,
            LAYOUT_AddChild, (ULONG)filler(),
            LAYOUT_AddChild, (ULONG)button(PG_CANCEL, S(MSG_PREFS_CANCEL)),
            CHILD_WeightedWidth, 0,
            TAG_END),
        CHILD_WeightedHeight, 0,
        TAG_END);
}

/**
 * @brief Read an attribute.
 * @param o The object.
 * @param attr The attribute.
 * @return Its value.
 */
static ULONG get(Object *o, ULONG attr)
{
    ULONG v = 0;
    GetAttr(attr, o, &v);
    return v;
}

/**
 * @brief Copy the text of a string gadget into a setting.
 * @param o The gadget.
 * @param[out] out The setting.
 * @param size Its size.
 */
static void copy_text(Object *o, char *out, size_t size)
{
    const char *text = (const char *)get(o, STRINGA_TextVal);
    strncpy(out, text != NULL ? text : "", size - 1);
    out[size - 1] = '\0';
}

/**
 * @brief Take the gadgets' values into the settings.
 * @param s The settings.
 */
static void read_gadgets(Settings *s)
{
    ULONG i;
    copy_text(pw.projectdir, s->projectdir, sizeof(s->projectdir));
    copy_text(pw.author, s->author, sizeof(s->author));
    copy_text(pw.authormail, s->authormail, sizeof(s->authormail));
    s->winwidth = (long)(LONG)get(pw.width, INTEGER_Number);
    s->winheight = (long)(LONG)get(pw.height, INTEGER_Number);
    s->winleft = (long)(LONG)get(pw.left, INTEGER_Number);
    s->wintop = (long)(LONG)get(pw.top, INTEGER_Number);
    s->splash = get(pw.splash, CHECKBOX_Checked) != 0;
    s->foldertree = get(pw.foldertree, CHECKBOX_Checked) != 0;
    s->showinfo = get(pw.hideinfo, CHECKBOX_Checked) == 0;
    s->minimap = get(pw.minimap, CHECKBOX_Checked) != 0;
    for (i = 0; i < SCOL_COUNT; ++i)
        s->colours[i] = get(pw.colours[i], GETCOLOR_Color) & 0xFFFFFFUL;
    s->tbmode = (int)get(pw.tbmode, CHOOSER_Selected);
    s->tbframes = get(pw.tbframes, CHECKBOX_Checked) != 0;
    s->scout = get(pw.scout, CHECKBOX_Checked) != 0;
    {
        const char *dir = (const char *)get(pw.tempdir, STRINGA_TextVal);
        strncpy(s->tempdir, dir != NULL && dir[0] != '\0' ? dir : "T:", sizeof(s->tempdir) - 1);
        s->tempdir[sizeof(s->tempdir) - 1] = '\0';
    }
    {
        const char *con = (const char *)get(pw.shellwindow, STRINGA_TextVal);
        strncpy(s->shellwindow, con != NULL && con[0] != '\0' ? con : "CON:", sizeof(s->shellwindow) - 1);
        s->shellwindow[sizeof(s->shellwindow) - 1] = '\0';
    }
    s->outfont[0] = '\0';
    s->outfontsize = 0;
    if (get(pw.outfontown, CHECKBOX_Checked)) {
        const struct TextAttr *ta = (const struct TextAttr *)get(pw.outfont, GETFONT_TextAttr);
        if (ta != NULL && ta->ta_Name != NULL && ta->ta_YSize > 0) {
            strncpy(s->outfont, (const char *)ta->ta_Name, sizeof(s->outfont) - 1);
            s->outfont[sizeof(s->outfont) - 1] = '\0';
            s->outfontsize = ta->ta_YSize;
        }
    }
}

/**
 * @brief Let the user choose a drawer with the ASL requester and put it
 *        into a string gadget.
 * @param o The string gadget.
 * @param title The requester's title.
 * @param initial The drawer to show if the gadget is empty.
 */
static void choose_drawer(Object *o, const char *title, const char *initial)
{
    const char *dir = (const char *)get(o, STRINGA_TextVal);
    struct FileRequester *fr = AllocAslRequestTags(ASL_FileRequest,
        ASLFR_TitleText, (ULONG)title, ASLFR_DrawersOnly, TRUE, TAG_END);
    if (fr == NULL) return;
    if (AslRequestTags(fr, ASLFR_Window, (ULONG)pw.win, ASLFR_SleepWindow, TRUE,
                       ASLFR_InitialDrawer, (ULONG)(dir != NULL && dir[0] != '\0' ? dir : initial),
                       TAG_END) &&
        fr->fr_Drawer != NULL && fr->fr_Drawer[0] != '\0')
        SetPageGadgetAttrs((struct Gadget *)o, pw.page, pw.win, NULL,
                           STRINGA_TextVal, (ULONG)fr->fr_Drawer, TAG_END);
    FreeAslRequest(fr);
}

/**
 * @brief Set an attribute of a gadget that may be on a hidden page.
 * @param o The gadget.
 * @param tag The attribute.
 * @param value Its value.
 */
static void set_page_gadget(Object *o, ULONG tag, ULONG value)
{
    SetPageGadgetAttrs((struct Gadget *)o, pw.page, pw.win, NULL, tag, value, TAG_END);
}

/**
 * @brief Ghost the gadgets of the file type page that do not apply: all
 *        for a language that uses the standard settings, the number of
 *        spaces when indenting with tabs.
 *
 * checkbox.gadget does not draw itself again when only GA_Disabled
 * changes (seen on OS 3.2: a checkbox stayed ghosted), so every gadget of
 * the page is refreshed while the window is open.
 */
static void ft_ghost(void)
{
    int standard = pw.ft_current >= 0 && get(pw.ft_standard, CHECKBOX_Checked) != 0;
    Object *const all[] = { pw.ft_tabsize, pw.ft_indentwith, pw.ft_indent, pw.ft_auto, pw.ft_tabkey,
                            pw.ft_numbers, pw.ft_cutcopy };
    ULONG i;
    for (i = 0; i < sizeof(all) / sizeof(all[0]); ++i) {
        int off = standard || (all[i] == pw.ft_indent && get(pw.ft_indentwith, CHOOSER_Selected) == 1);
        set_page_gadget(all[i], GA_Disabled, (ULONG)off);
        if (pw.win != NULL) RefreshPageGadget((struct Gadget *)all[i], pw.page, pw.win, NULL);
    }
}

/**
 * @brief Show settings on the file type page.
 * @param o The settings.
 */
static void ft_values(const FileTypeOptions *o)
{
    set_page_gadget(pw.ft_tabsize, INTEGER_Number, (ULONG)o->tab_size);
    set_page_gadget(pw.ft_indentwith, CHOOSER_Selected, o->indent > 0 ? 0 : 1);
    /* indenting with tabs keeps a number for switching back */
    set_page_gadget(pw.ft_indent, INTEGER_Number, (ULONG)(o->indent > 0 ? o->indent : 4));
    set_page_gadget(pw.ft_auto, CHECKBOX_Checked, (ULONG)o->auto_indent);
    set_page_gadget(pw.ft_tabkey, CHOOSER_Selected, (ULONG)o->tab_after);
    set_page_gadget(pw.ft_numbers, CHECKBOX_Checked, (ULONG)o->line_numbers);
    set_page_gadget(pw.ft_cutcopy, CHECKBOX_Checked, (ULONG)o->cut_copy_line);
}

/**
 * @brief Show the settings of the current file type (pw.ft_current) on
 *        the file type page.
 */
static void ft_show(void)
{
    const FileTypeOptions *o = NULL;
    if (pw.ft_current >= 0) o = filetypes_find(pw.ft, scoutlink_language_id(pw.ft_current));
    set_page_gadget(pw.ft_name, GA_Text, (ULONG)(pw.ft_current >= 0 ? scoutlink_language_name(pw.ft_current)
                                                                    : S(MSG_PREFS_FT_STANDARDNAME)));
    /* Standard is always its own */
    set_page_gadget(pw.ft_standard, GA_Disabled, (ULONG)(pw.ft_current < 0));
    set_page_gadget(pw.ft_standard, CHECKBOX_Checked, (ULONG)(pw.ft_current >= 0 && o == NULL));
    ft_values(o != NULL ? o : &pw.ft->standard);
    ft_ghost();
}

/**
 * @brief Take the values of the file type page into the settings being
 *        edited.
 * @return 1 on success, 0 if out of memory.
 */
static int ft_store(void)
{
    const char *id = pw.ft_current >= 0 ? scoutlink_language_id(pw.ft_current) : NULL;
    FileTypeOptions o;
    if (id != NULL && get(pw.ft_standard, CHECKBOX_Checked)) return filetypes_set(pw.ft, id, NULL) > 0;
    o.tab_size = (int)(LONG)get(pw.ft_tabsize, INTEGER_Number);
    o.indent = get(pw.ft_indentwith, CHOOSER_Selected) == 1 ? 0 : (int)(LONG)get(pw.ft_indent, INTEGER_Number);
    o.auto_indent = get(pw.ft_auto, CHECKBOX_Checked) != 0;
    o.tab_after = get(pw.ft_tabkey, CHOOSER_Selected) == 1;
    o.line_numbers = get(pw.ft_numbers, CHECKBOX_Checked) != 0;
    o.cut_copy_line = get(pw.ft_cutcopy, CHECKBOX_Checked) != 0;
    return filetypes_set(pw.ft, id, &o) > 0;
}

/**
 * @brief Show the page of the category selected in the list.
 */
static void select_category(void)
{
    struct Node *n = (struct Node *)get(pw.list, LISTBROWSER_SelectedNode);
    ULONG ud = 0, page;
    if (n == NULL) return;
    GetListBrowserNodeAttrs(n, LBNA_UserData, (ULONG)&ud, TAG_END);
    if (ud < NUMCATEGORIES) {
        page = ud;
    } else {
        int current = ud >= UD_LANGUAGE ? (int)(ud - UD_LANGUAGE) : -1;
        page = PAGE_FILETYPES;
        if (current != pw.ft_current) {
            ft_store();
            pw.ft_current = current;
            ft_show();
        }
    }
    SetGadgetAttrs((struct Gadget *)pw.page, pw.win, NULL, PAGE_Current, page, TAG_END);
    RethinkLayout((struct Gadget *)pw.root, pw.win, NULL, TRUE);
}

int prefs_dialog(EditorApp *app, Settings *s, FileTypes *ft)
{
    ULONG sig = 0, mainsig = 0, result, i;
    WORD code = 0;
    int rc = PREFS_CANCEL, done = 0;

    if (app->window == NULL || !open_classes(app)) return PREFS_CANCEL;
    memset(&pw, 0, sizeof(pw));
    pw.ft = ft;
    pw.ft_current = -1;
    if ((pw.root = build(app, s)) == NULL) goto out;
    ft_show();

    pw.winobj = NewObject(WINDOW_GetClass(), NULL,
        WA_Title, (ULONG)S(MSG_PREFS_TITLE),
        WA_PubScreen, (ULONG)app->screen,
        WA_SmartRefresh, TRUE,     /* scrolled lists: see "Scrolling..." in TECHNICAL.md */
        WA_Activate, TRUE,
        WA_DepthGadget, TRUE,
        WA_DragBar, TRUE,
        WA_CloseGadget, TRUE,
        WA_SizeGadget, TRUE,
        WINDOW_RefWindow, (ULONG)app->window,
        WINDOW_Position, WPOS_CENTERWINDOW,
        WINDOW_ParentGroup, (ULONG)pw.root,
        TAG_END);
    if (pw.winobj == NULL) {
        DisposeObject(pw.root);
        goto out;
    }
    if ((pw.win = (struct Window *)DoMethod(pw.winobj, WM_OPEN, NULL)) == NULL) goto out;

    SetAttrs(app->window_object, WA_BusyPointer, TRUE, TAG_END);
    GetAttr(WINDOW_SigMask, app->window_object, &mainsig);
    GetAttr(WINDOW_SigMask, pw.winobj, &sig);
    while (!done) {
        ULONG got = Wait(sig | mainsig);
        /* the main window waits: its input is dropped */
        if ((got & mainsig) != 0)
            while (DoMethod(app->window_object, WM_HANDLEINPUT, &code) != WMHI_LASTMSG) ;
        while ((result = DoMethod(pw.winobj, WM_HANDLEINPUT, &code)) != WMHI_LASTMSG) {
            ULONG id = result & WMHI_GADGETMASK;
            if ((result & WMHI_CLASSMASK) == WMHI_CLOSEWINDOW) { done = 1; continue; }
            if ((result & WMHI_CLASSMASK) != WMHI_GADGETUP) continue;
            switch (id) {
                case PG_LIST:
                    select_category();
                    break;
                case PG_FT_STANDARD:
                    /* back to the standard settings: show them */
                    if (get(pw.ft_standard, CHECKBOX_Checked)) ft_values(&pw.ft->standard);
                    ft_ghost();
                    break;
                case PG_FT_INDENTWITH:
                    ft_ghost();
                    break;
                case PG_DEFCOLOURS:
                    for (i = 0; i < SCOL_COUNT; ++i)
                        set_page_gadget(pw.colours[i], GETCOLOR_Color, settings_default_colours[i]);
                    break;
                case PG_CURSIZE:
                    set_page_gadget(pw.width, INTEGER_Number, (ULONG)app->window->Width);
                    set_page_gadget(pw.height, INTEGER_Number, (ULONG)app->window->Height);
                    set_page_gadget(pw.left, INTEGER_Number, (ULONG)app->window->LeftEdge);
                    set_page_gadget(pw.top, INTEGER_Number, (ULONG)app->window->TopEdge);
                    break;
                case PG_AUTOSIZE:
                    set_page_gadget(pw.width, INTEGER_Number, 0);
                    set_page_gadget(pw.height, INTEGER_Number, 0);
                    set_page_gadget(pw.left, INTEGER_Number, (ULONG)-1);
                    set_page_gadget(pw.top, INTEGER_Number, (ULONG)-1);
                    break;
                case PG_OUTFONTOWN:
                    /* like ft_ghost(): the gadget is drawn anew */
                    set_page_gadget(pw.outfont, GA_Disabled, (ULONG)!get(pw.outfontown, CHECKBOX_Checked));
                    RefreshPageGadget((struct Gadget *)pw.outfont, pw.page, pw.win, NULL);
                    break;
                case PG_OUTFONT: {
                    struct gfRequest msg;
                    msg.MethodID = GFONT_REQUEST;
                    msg.gfr_Window = pw.win;
                    DoMethodA(pw.outfont, (Msg)&msg);
                    break;
                }
                case PG_CHOOSETEMP:
                    choose_drawer(pw.tempdir, S(MSG_PREFS_ASL_TEMPDIR), "T:");
                    break;
                case PG_CHOOSEPROJECT:
                    choose_drawer(pw.projectdir, S(MSG_PREFS_ASL_PROJECTDIR), "SYS:");
                    break;
                case PG_SAVE:
                case PG_USE:
                    read_gadgets(s);
                    ft_store();
                    rc = id == PG_SAVE ? PREFS_SAVE : PREFS_USE;
                    done = 1;
                    break;
                case PG_CANCEL:
                    done = 1;
                    break;
                default:
                    if (id >= PG_COLOUR && id < PG_COLOUR + SCOL_COUNT) {
                        struct gcRequest msg;
                        msg.MethodID = GCOLOR_REQUEST;
                        msg.gcr_Window = pw.win;
                        DoMethodA(pw.colours[id - PG_COLOUR], (Msg)&msg);
                    }
                    break;
            }
        }
    }
    SetAttrs(app->window_object, WA_BusyPointer, FALSE, TAG_END);

out:
    if (pw.winobj != NULL) DisposeObject(pw.winobj);         /* the gadgets with it */
    if (pw.catlist.lh_Head != NULL) FreeListBrowserList(&pw.catlist);
    memset(&pw, 0, sizeof(pw));
    return rc;
}
