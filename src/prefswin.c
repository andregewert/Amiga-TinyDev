/**
 * @file prefswin.c
 * @brief The settings window (categories Window, Colours, Toolbar), built like Kickdown's.
 *
 * Built after the ReAction style guide of amiga-devkit (section 4.2): a
 * list of categories on the left, the options of the selected one on the
 * right (page.gadget), Save/Use/Cancel below. The window is modal: the
 * main window shows the busy pointer and its input is dropped.
 *
 * The groups are built with complete tag lists (CHILD_Label applies to the
 * child of the same tag list). The gadgets of a page keep their height; an
 * empty group at the end takes the rest. Gadgets on a hidden page are
 * changed with SetPageGadgetAttrs(). Keyboard shortcuts are unique in the
 * whole window, in every language. The classes the window needs are
 * opened when it is first used and closed by prefs_cleanup().
 */
#include "editor.h"

#include <classes/window.h>
#include <gadgets/button.h>
#include <gadgets/checkbox.h>
#include <gadgets/chooser.h>
#include <gadgets/getcolor.h>
#include <gadgets/integer.h>
#include <gadgets/layout.h>
#include <gadgets/listbrowser.h>
#include <images/bevel.h>
#include <images/label.h>
#include <intuition/gadgetclass.h>
#include <proto/button.h>
#include <proto/checkbox.h>
#include <proto/chooser.h>
#include <proto/exec.h>
#include <proto/getcolor.h>
#include <proto/graphics.h>
#include <proto/integer.h>
#include <proto/intuition.h>
#include <proto/label.h>
#include <proto/layout.h>
#include <proto/listbrowser.h>
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

enum {
    PG_LIST = 100,
    PG_WIDTH, PG_HEIGHT, PG_LEFT, PG_TOP, PG_CURSIZE, PG_AUTOSIZE, PG_SPLASH,
    PG_COLOUR, PG_DEFCOLOURS = PG_COLOUR + SCOL_COUNT,
    PG_TBMODE, PG_TBFRAMES,
    PG_SAVE, PG_USE, PG_CANCEL
};

/**
 * Names of the categories (MSG_), in the order of the pages.
 */
static const LONG categories[] = { MSG_CAT_WINDOW, MSG_CAT_COLOURS, MSG_CAT_TOOLBAR };
/**
 * Number of categories (pages).
 */
#define NUMCATEGORIES 3

/** Chooser labels, in the order of enum ToolbarMode. */
static const LONG tbmode_texts[TBMODE_COUNT] = {
    MSG_PREFS_TBMODE_IMAGES, MSG_PREFS_TBMODE_BOTH, MSG_PREFS_TBMODE_TEXT
};
static STRPTR tbmodes[TBMODE_COUNT + 1];      /**< the chooser's labels, filled from tbmode_texts[] */

/** Labels and requester titles of the colours, SettingsColour order. */
static const LONG colour_labels[SCOL_COUNT] = {
    MSG_PREFS_COL_TEXT, MSG_PREFS_COL_KEYWORD, MSG_PREFS_COL_STRING, MSG_PREFS_COL_COMMENT,
    MSG_PREFS_COL_PREPROC
};
/**
 * Titles of the colour requesters (MSG_), SettingsColour order.
 */
static const LONG colour_titles[SCOL_COUNT] = {
    MSG_PREFS_TTL_TEXT, MSG_PREFS_TTL_KEYWORD, MSG_PREFS_TTL_STRING, MSG_PREFS_TTL_COMMENT,
    MSG_PREFS_TTL_PREPROC
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

/** tags of a page: vertical group with a frame and the category as title */
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
    Object *width, *height, *left, *top, *splash;
    Object *colours[SCOL_COUNT];
    Object *tbmode, *tbframes;
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
        { &LabelBase,       "images/label.image" },
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
    struct Library **b[] = { &ListBrowserBase, &CheckBoxBase, &IntegerBase, &GetColorBase };
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
    ULONG i;
    for (i = 0; i < NUMCATEGORIES; ++i) {
        t = TextLength(&app->screen->RastPort, (STRPTR)S(categories[i]),
                       (UWORD)strlen(S(categories[i])));
        if (t > w) w = t;
    }
    return w + 24;
}

/**
 * @brief Build the window's contents from the settings.
 * @param app The application state.
 * @param s The settings shown.
 * @return The root layout, or NULL.
 */
static Object *build(EditorApp *app, const Settings *s)
{
    Object *window, *colours, *toolbar;
    ULONG i;

    NewList(&pw.catlist);
    for (i = 0; i < NUMCATEGORIES; ++i) {
        struct Node *n = AllocListBrowserNode(1, LBNCA_Text, (ULONG)S(categories[i]), TAG_END);
        if (n != NULL) AddTail(&pw.catlist, n);
    }
    pw.list = NewObject(LISTBROWSER_GetClass(), NULL,
        GA_ID, PG_LIST,
        GA_RelVerify, TRUE,
        LISTBROWSER_Labels, (ULONG)&pw.catlist,
        LISTBROWSER_ShowSelected, TRUE,
        LISTBROWSER_Selected, 0,
        LISTBROWSER_AutoFit, TRUE,
        LISTBROWSER_VerticalProp, FALSE,
        TAG_END);

    /* Window */
    pw.width = integer(PG_WIDTH, s->winwidth, 0);
    pw.height = integer(PG_HEIGHT, s->winheight, 0);
    pw.left = integer(PG_LEFT, s->winleft, -1);
    pw.top = integer(PG_TOP, s->wintop, -1);
    pw.splash = checkbox(PG_SPLASH, S(MSG_PREFS_SPLASH), s->splash);
    window = NewObject(LAYOUT_GetClass(), NULL, PAGE_GROUP(S(categories[0])),
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
        LAYOUT_AddChild, (ULONG)pw.splash, FIXED,
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

    pw.page = NewObject(PAGE_GetClass(), NULL,
        PAGE_Add, (ULONG)window,
        PAGE_Add, (ULONG)colours,
        PAGE_Add, (ULONG)toolbar,
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
 * @brief Take the gadgets' values into the settings.
 * @param s The settings.
 */
static void read_gadgets(Settings *s)
{
    ULONG i;
    s->winwidth = (long)(LONG)get(pw.width, INTEGER_Number);
    s->winheight = (long)(LONG)get(pw.height, INTEGER_Number);
    s->winleft = (long)(LONG)get(pw.left, INTEGER_Number);
    s->wintop = (long)(LONG)get(pw.top, INTEGER_Number);
    s->splash = get(pw.splash, CHECKBOX_Checked) != 0;
    for (i = 0; i < SCOL_COUNT; ++i)
        s->colours[i] = get(pw.colours[i], GETCOLOR_Color) & 0xFFFFFFUL;
    s->tbmode = (int)get(pw.tbmode, CHOOSER_Selected);
    s->tbframes = get(pw.tbframes, CHECKBOX_Checked) != 0;
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

int prefs_dialog(EditorApp *app, Settings *s)
{
    ULONG sig = 0, mainsig = 0, result, i;
    WORD code = 0;
    int rc = PREFS_CANCEL, done = 0;

    if (app->window == NULL || !open_classes(app)) return PREFS_CANCEL;
    memset(&pw, 0, sizeof(pw));
    if ((pw.root = build(app, s)) == NULL) goto out;

    pw.winobj = NewObject(WINDOW_GetClass(), NULL,
        WA_Title, (ULONG)S(MSG_PREFS_TITLE),
        WA_PubScreen, (ULONG)app->screen,
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
                    SetGadgetAttrs((struct Gadget *)pw.page, pw.win, NULL,
                                   PAGE_Current, get(pw.list, LISTBROWSER_Selected), TAG_END);
                    RethinkLayout((struct Gadget *)pw.root, pw.win, NULL, TRUE);
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
                case PG_SAVE:
                case PG_USE:
                    read_gadgets(s);
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
