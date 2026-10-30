/**
 * @file ui.c
 * @brief The main window: menus, status line, event loop, editor colours and settings that take effect at once.
 *
 * Editor colours: every editor gets a backfill hook and an editor-local
 * DrawInfo with a white background pen, black for normal text and dark
 * screen pens (ObtainBestPen) for keywords, strings, comments and
 * preprocessor lines. The OS 3.2 texteditor.gadget paints its text area
 * with the screen's BACKGROUNDPEN and ignores GA_BackFill and GA_DrawInfo
 * there; GA_TEXTEDITOR_Pen and GA_TEXTEDITOR_ColorMap are not implemented
 * and GA_TEXTEDITOR_Transparent is OS4 only. The text area therefore keeps
 * the Workbench background until a subclass draws it itself.
 */
#include "editor.h"

#include <gadgets/clicktab.h>
#include <gadgets/button.h>
#include <gadgets/layout.h>
#include <gadgets/dirtree.h>
#include <gadgets/minimap.h>
#include <gadgets/listbrowser.h>
#include <gadgets/texteditor.h>
#include <images/bitmap.h>
#include <libraries/gadtools.h>
#include <graphics/gfx.h>
#include <graphics/rastport.h>
#include <graphics/view.h>
#include <intuition/icclass.h>
#include <workbench/workbench.h>
#include <proto/clicktab.h>
#include <proto/button.h>
#include <proto/bitmap.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/icon.h>
#include <proto/intuition.h>
#include <proto/layout.h>
#include <proto/texteditor.h>
#include <proto/utility.h>
#include <proto/window.h>
#include <clib/alib_protos.h>
#include <reaction/reaction.h>

#include <stdio.h>
#include <string.h>

#define UD(id) ((APTR)(ULONG)(id))  /**< a MID_ as a menu item's user data */

/** Message of a layer backfill hook (as layers.library passes it). */
typedef struct EditorBackFillMessage {
    struct Layer *layer;        /**< the layer */
    struct Rectangle bounds;    /**< the rectangle to fill */
    LONG offset_x;              /**< x offset of the pattern */
    LONG offset_y;              /**< y offset of the pattern */
} EditorBackFillMessage;

/**
 * @brief Backfill hook that paints layout backgrounds with the editor color.
 *
 * Invoked by the layout/window classes to fill exposed areas.  It fills the
 * requested rectangle with the editor background pen and preserves the
 * RastPort's previous foreground pen so subsequent rendering is unaffected.
 *
 * @param hook The hook whose h_Data points to the owning EditorApp.
 * @param rast_port The RastPort to render into.
 * @param message The backfill message describing the rectangle to fill.
 * @return Always 0.
 */
static ULONG editor_backfill_entry(struct Hook *hook, struct RastPort *rast_port,
                                   EditorBackFillMessage *message)
{
    EditorApp *app = (EditorApp *)hook->h_Data;
    UBYTE old_pen;
    if (app == NULL || rast_port == NULL || message == NULL) return 0;
    old_pen = (UBYTE)GetAPen(rast_port);
    SetAPen(rast_port,
            (UBYTE)app->editor_pens[EDITOR_COLOR_BACKGROUND]);
    RectFill(rast_port, message->bounds.MinX, message->bounds.MinY,
             message->bounds.MaxX, message->bounds.MaxY);
    SetAPen(rast_port, old_pen);
    return 0;
}

/**
 * @brief window.class IDCMP hook capturing clicktab tab-close notifications.
 *
 * The clicktab.gadget delivers a tab close only as an IDCMP_IDCMPUPDATE
 * notification carrying the closed node in its tag list.  This hook extracts
 * that node and stores it in app->pending_close, deferring the actual
 * document_close() to the event loop because disposing gadgets from within
 * input handling is unsafe.
 *
 * @param hook The hook whose h_Data points to the owning EditorApp.
 * @param win The window object receiving the message (unused).
 * @param msg The IntuiMessage; only IDCMP_IDCMPUPDATE messages are handled.
 * @return The original message pointer cast to ULONG.
 */
static ULONG tab_idcmp_entry(struct Hook *hook, Object *win,
                             struct IntuiMessage *msg)
{
    EditorApp *app = (EditorApp *)hook->h_Data;
    (void)win;
    if (app != NULL && msg != NULL && msg->Class == IDCMP_IDCMPUPDATE) {
        struct TagItem *tags = (struct TagItem *)msg->IAddress;
        struct Node *node;
        /* minimap.gadget reports clicks and drags the same way. */
        if (minimap_handle_notify(app, tags)) return (ULONG)msg;
        node = (struct Node *)GetTagData(CLICKTAB_NodeClosed, 0, tags);
        if (node != NULL) app->pending_close = node;
    }
    return (ULONG)msg;
}

/**
 * @brief One component of an 0xRRGGBB colour as 32 bit value for
 *        ObtainBestPen().
 * @param c The colour.
 * @param shift 16 for red, 8 for green, 0 for blue.
 * @return The component, repeated into all 4 bytes.
 */
static ULONG rgb32(unsigned long c, int shift)
{
    return (ULONG)((c >> shift) & 0xffUL) * 0x01010101UL;
}

/**
 * @brief Reserve and configure the pens used for editor rendering.
 *
 * Obtains the fixed set of editor colors (background, text, and syntax pens)
 * from the public screen's ColorMap, validates that the screen provides the
 * required number of DrawInfo pens, and builds a private DrawInfo whose
 * background and text pens are overridden.  Also obtains an optional minimap
 * viewport-tint pen (non-fatal on failure) and initializes the backfill hook.
 * On any fatal failure all pens obtained so far are released.
 *
 * @param app The application whose screen and pen state are set up.
 * @return 1 on success, 0 on failure.
 */
static int open_editor_colors(EditorApp *app)
{
    struct TagItem tags[] = {
        {OBP_Precision, PRECISION_GUI},
        {OBP_FailIfBad, FALSE},
        {TAG_END, 0}
    };
    struct TagItem white_tags[] = {
        {OBP_Precision, (ULONG)PRECISION_EXACT},
        {OBP_FailIfBad, TRUE},
        {TAG_END, 0}
    };
    int i;
    struct ColorMap *color_map = app->screen->ViewPort.ColorMap;

    app->minimap_view_pen = -1;
    for (i = 0; i < EDITOR_COLOR_COUNT; ++i) app->editor_pens[i] = -1;
    for (i = 0; i < EDITOR_COLOR_COUNT; ++i) {
        /* white background, the other colours from the settings */
        unsigned long c = i == EDITOR_COLOR_BACKGROUND
            ? 0xffffffUL : app->settings.colours[i - EDITOR_COLOR_TEXT];
        app->editor_pens[i] = ObtainBestPenA(color_map, rgb32(c, 16), rgb32(c, 8),
                                             rgb32(c, 0), i == EDITOR_COLOR_BACKGROUND
                                             ? white_tags : tags);
        if (app->editor_pens[i] < 0) {
            while (--i >= 0) ReleasePen(color_map, (ULONG)app->editor_pens[i]);
            return 0;
        }
    }
    app->screen_draw_info = GetScreenDrawInfo(app->screen);
    if (app->screen_draw_info == NULL ||
        app->screen_draw_info->dri_NumPens < NUMDRIPENS) {
        if (app->screen_draw_info != NULL) {
            FreeScreenDrawInfo(app->screen, app->screen_draw_info);
            app->screen_draw_info = NULL;
        }
        for (i = EDITOR_COLOR_COUNT - 1; i >= 0; --i)
            ReleasePen(color_map, (ULONG)app->editor_pens[i]);
        return 0;
    }
    app->editor_draw_info = *app->screen_draw_info;
    memcpy(app->editor_draw_pens, app->screen_draw_info->dri_Pens,
           sizeof(app->editor_draw_pens));
    app->editor_draw_pens[BACKGROUNDPEN] =
        (UWORD)app->editor_pens[EDITOR_COLOR_BACKGROUND];
    app->editor_draw_pens[TEXTPEN] =
        (UWORD)app->editor_pens[EDITOR_COLOR_TEXT];
    app->editor_draw_info.dri_Pens = app->editor_draw_pens;
    app->editor_draw_info.dri_NumPens = NUMDRIPENS;
    /* A grey slightly darker than the editor's BACKGROUNDPEN, used to tint the
     * minimap's visible-area viewport rectangle.  Obtaining it is non-fatal:
     * if it fails the minimap falls back to its plain background. */
    app->minimap_view_pen = ObtainBestPenA(color_map, 0x88888888UL,
                                           0x88888888UL, 0x88888888UL, tags);
    memset(&app->editor_backfill_hook, 0, sizeof(app->editor_backfill_hook));
    app->editor_backfill_hook.h_Entry = (ULONG (*)())HookEntry;
    app->editor_backfill_hook.h_SubEntry = (ULONG (*)())editor_backfill_entry;
    app->editor_backfill_hook.h_Data = app;
    app->editor_colors_open = 1;
    return 1;
}

/**
 * @brief Release the pens and DrawInfo reserved by open_editor_colors().
 *
 * Frees the screen DrawInfo, releases the optional minimap view pen and all
 * editor pens back to the screen ColorMap, and clears the colors-open flag.
 * Does nothing if colors were never opened or the screen is gone.
 *
 * @param app The application whose editor colors are released.
 */
static void close_editor_colors(EditorApp *app)
{
    int i;
    if (!app->editor_colors_open || app->screen == NULL) return;
    if (app->screen_draw_info != NULL) {
        FreeScreenDrawInfo(app->screen, app->screen_draw_info);
        app->screen_draw_info = NULL;
    }
    if (app->minimap_view_pen >= 0) {
        ReleasePen(app->screen->ViewPort.ColorMap, (ULONG)app->minimap_view_pen);
        app->minimap_view_pen = -1;
    }
    for (i = EDITOR_COLOR_COUNT - 1; i >= 0; --i)
        ReleasePen(app->screen->ViewPort.ColorMap, (ULONG)app->editor_pens[i]);
    app->editor_colors_open = 0;
}

/** Items of the File Type menu that exclude each other (bit 2 is its bar). */
#define FT_ALL (((1L << (3 + FILETYPE_SLOTS)) - 1) & ~4L)
/** The mutual exclusion of the File Type menu's item at a position. */
#define FT_EXCLUDE(pos) (FT_ALL & ~(1L << (pos)))

/** The menu; the texts are filled in from menu_texts[] (catalog). The
 *  File Type menu has a slot per language (NM_IGNORE until Scout names
 *  them, ui_update_file_types()). */
static struct NewMenu menus[] = {
    {NM_TITLE, NULL, NULL, 0, 0, NULL},
    {NM_ITEM, NULL, "N", 0, 0, UD(MID_NEW)},
    {NM_ITEM, NULL, "O", 0, 0, UD(MID_OPEN)},
    {NM_ITEM, NULL, NULL, 0, 0, UD(MID_OPEN_DIRECTORY)},
    {NM_ITEM, NULL, NULL, 0, 0, UD(MID_TREE_REFRESH)},
    {NM_ITEM, NULL, "S", 0, 0, UD(MID_SAVE)},
    {NM_ITEM, NULL, NULL, 0, 0, UD(MID_SAVE_AS)},
    {NM_ITEM, NULL, "W", 0, 0, UD(MID_CLOSE)},
    {NM_ITEM, NM_BARLABEL, NULL, 0, 0, NULL},
    {NM_ITEM, NULL, ",", 0, 0, UD(MID_SETTINGS)},
    {NM_ITEM, NULL, NULL, 0, 0, UD(MID_HELP)},
    {NM_ITEM, NULL, "?", 0, 0, UD(MID_ABOUT)},
    {NM_ITEM, NM_BARLABEL, NULL, 0, 0, NULL},
    {NM_ITEM, NULL, "I", 0, 0, UD(MID_ICONIFY)},
    {NM_ITEM, NULL, "Q", 0, 0, UD(MID_QUIT)},
    {NM_TITLE, NULL, NULL, 0, 0, NULL},
    {NM_ITEM, NULL, "Z", 0, 0, UD(MID_UNDO)},
    {NM_ITEM, NULL, "Y", 0, 0, UD(MID_REDO)},
    {NM_ITEM, NM_BARLABEL, NULL, 0, 0, NULL},
    {NM_ITEM, NULL, "X", 0, 0, UD(MID_CUT)},
    {NM_ITEM, NULL, "C", 0, 0, UD(MID_COPY)},
    {NM_ITEM, NULL, "V", 0, 0, UD(MID_PASTE)},
    {NM_ITEM, NULL, "A", 0, 0, UD(MID_SELECT_ALL)},
    {NM_ITEM, NULL, NULL, CHECKIT | MENUTOGGLE, 0, UD(MID_RECT)},
    {NM_ITEM, NM_BARLABEL, NULL, 0, 0, NULL},
    {NM_ITEM, NULL, "F", 0, 0, UD(MID_FIND)},
    {NM_ITEM, NULL, "G", 0, 0, UD(MID_FIND_NEXT)},
    {NM_ITEM, NM_BARLABEL, NULL, 0, 0, NULL},
    {NM_ITEM, NULL, "T", 0, 0, UD(MID_DEFINITION)},
    {NM_ITEM, NULL, "U", 0, 0, UD(MID_GOBACK)},
    {NM_ITEM, NULL, "L", 0, 0, UD(MID_COMPLETE)},
    {NM_TITLE, NULL, NULL, 0, 0, NULL},
    {NM_ITEM, NULL, NULL, CHECKIT | CHECKED, FT_EXCLUDE(0), UD(MID_FT_AUTO)},
    {NM_ITEM, NULL, NULL, CHECKIT, FT_EXCLUDE(1), UD(MID_FT_TEXT)},
    {NM_ITEM, NM_BARLABEL, NULL, 0, 0, NULL},
    {NM_IGNORE, "", NULL, CHECKIT, FT_EXCLUDE(3), UD(MID_FT_FIRST + 0)},
    {NM_IGNORE, "", NULL, CHECKIT, FT_EXCLUDE(4), UD(MID_FT_FIRST + 1)},
    {NM_IGNORE, "", NULL, CHECKIT, FT_EXCLUDE(5), UD(MID_FT_FIRST + 2)},
    {NM_IGNORE, "", NULL, CHECKIT, FT_EXCLUDE(6), UD(MID_FT_FIRST + 3)},
    {NM_IGNORE, "", NULL, CHECKIT, FT_EXCLUDE(7), UD(MID_FT_FIRST + 4)},
    {NM_IGNORE, "", NULL, CHECKIT, FT_EXCLUDE(8), UD(MID_FT_FIRST + 5)},
    {NM_IGNORE, "", NULL, CHECKIT, FT_EXCLUDE(9), UD(MID_FT_FIRST + 6)},
    {NM_IGNORE, "", NULL, CHECKIT, FT_EXCLUDE(10), UD(MID_FT_FIRST + 7)},
    {NM_IGNORE, "", NULL, CHECKIT, FT_EXCLUDE(11), UD(MID_FT_FIRST + 8)},
    {NM_IGNORE, "", NULL, CHECKIT, FT_EXCLUDE(12), UD(MID_FT_FIRST + 9)},
    {NM_IGNORE, "", NULL, CHECKIT, FT_EXCLUDE(13), UD(MID_FT_FIRST + 10)},
    {NM_IGNORE, "", NULL, CHECKIT, FT_EXCLUDE(14), UD(MID_FT_FIRST + 11)},
    {NM_TITLE, NULL, NULL, 0, 0, NULL},
    {NM_ITEM, NULL, "B", 0, 0, UD(MID_BM_TOGGLE)},
    {NM_ITEM, NULL, "J", 0, 0, UD(MID_BM_NEXT)},
    {NM_ITEM, NULL, "K", 0, 0, UD(MID_BM_PREV)},
    {NM_ITEM, NM_BARLABEL, NULL, 0, 0, NULL},
    {NM_ITEM, NULL, NULL, 0, 0, UD(MID_BM_CLEAR)},
    {NM_TITLE, NULL, NULL, 0, 0, NULL},
    {NM_ITEM, NULL, "E", 0, 0, UD(MID_BUILD)},
    {NM_ITEM, NULL, "R", 0, 0, UD(MID_RUN)},
    {NM_ITEM, NULL, "D", 0, 0, UD(MID_DEBUG)},
    {NM_ITEM, NULL, NULL, 0, 0, UD(MID_STOP)},
    {NM_ITEM, NM_BARLABEL, NULL, 0, 0, NULL},
    {NM_ITEM, NULL, NULL, 0, 0, UD(MID_PROJECT_SETTINGS)},
    {NM_TITLE, NULL, NULL, 0, 0, NULL},
    {NM_ITEM, NULL, NULL, CHECKIT | MENUTOGGLE, 0, UD(MID_FOLDER_TREE)},
    {NM_ITEM, NULL, NULL, CHECKIT | MENUTOGGLE, 0, UD(MID_LINE_NUMBERS)},
    {NM_ITEM, NULL, NULL, CHECKIT | MENUTOGGLE, 0, UD(MID_MINIMAP)},
    {NM_ITEM, NM_BARLABEL, NULL, 0, 0, NULL},
    {NM_ITEM, NULL, NULL, CHECKIT | MENUTOGGLE, 0, UD(MID_BM_LIST)},
    {NM_ITEM, NULL, NULL, CHECKIT | MENUTOGGLE, 0, UD(MID_SYMLIST)},
    {NM_ITEM, NULL, NULL, CHECKIT | MENUTOGGLE, 0, UD(MID_DIAGLIST)},
    {NM_ITEM, NULL, NULL, CHECKIT | MENUTOGGLE, 0, UD(MID_OUTPUT)},
    {NM_ITEM, NM_BARLABEL, NULL, 0, 0, NULL},
    {NM_ITEM, NULL, NULL, 0, 0, UD(MID_SNAPSHOT_TOOLS)},
    {NM_END, NULL, NULL, 0, 0, NULL}
};

/** Texts of menus[] (MSG_ numbers), -1 for bars and the end. */
static const LONG menu_texts[] = {
    MSG_MENU_PROJECT,
    MSG_MENU_NEW,
    MSG_MENU_OPEN,
    MSG_MENU_OPENDIR,
    MSG_MENU_TREE_REFRESH,
    MSG_MENU_SAVE,
    MSG_MENU_SAVEAS,
    MSG_MENU_CLOSE,
    -1,
    MSG_MENU_SETTINGS,
    MSG_MENU_HELP,
    MSG_MENU_ABOUT,
    -1,
    MSG_MENU_ICONIFY,
    MSG_MENU_QUIT,
    MSG_MENU_EDIT,
    MSG_MENU_UNDO,
    MSG_MENU_REDO,
    -1,
    MSG_MENU_CUT,
    MSG_MENU_COPY,
    MSG_MENU_PASTE,
    MSG_MENU_SELECTALL,
    MSG_MENU_RECT,
    -1,
    MSG_MENU_FIND,
    MSG_MENU_FINDNEXT,
    -1,
    MSG_MENU_DEFINITION,
    MSG_MENU_GOBACK,
    MSG_MENU_COMPLETE,
    MSG_MENU_FILETYPE,
    MSG_MENU_FT_AUTO,
    MSG_MENU_FT_TEXT,
    -1,
    -1,
    -1,
    -1,
    -1,
    -1,
    -1,
    -1,
    -1,
    -1,
    -1,
    -1,
    -1,
    MSG_MENU_BOOKMARKS,
    MSG_MENU_BM_TOGGLE,
    MSG_MENU_BM_NEXT,
    MSG_MENU_BM_PREV,
    -1,
    MSG_MENU_BM_CLEAR,
    MSG_MENU_BUILD,
    MSG_MENU_DO_BUILD,
    MSG_MENU_DO_RUN,
    MSG_MENU_DO_DEBUG,
    MSG_MENU_STOP,
    -1,
    MSG_MENU_PROJSETTINGS,
    MSG_MENU_VIEW,
    MSG_MENU_TREE,
    MSG_MENU_LINENUMBERS,
    MSG_MENU_MINIMAP,
    -1,
    MSG_MENU_BM_LIST,
    MSG_MENU_SYMLIST,
    MSG_MENU_DIAGLIST,
    MSG_MENU_OUTPUT,
    -1,
    MSG_MENU_SNAPSHOT_TOOLS,
    -1,
};

void ui_error(EditorApp *app, const char *title, const char *message)
{
    struct EasyStruct es = {sizeof(es), 0, (STRPTR)title, (STRPTR)message, (STRPTR)S(MSG_OK)};
    splash_close();                     /* not over the requester */
    EasyRequestArgs(app != NULL ? app->window : NULL, &es, NULL, NULL);
}

void ui_about(EditorApp *app)
{
    char details[200];
    struct EasyStruct es = {sizeof(es), 0, (STRPTR)S(MSG_ABOUT_TITLE), "%s %s\n\n%s", (STRPTR)S(MSG_OK)};
    ULONG args[3];
    snprintf(details, sizeof(details),
             "www.ubergeek.de - agewert@ubergeek.de\n%s\n\n"
             "dirtree.gadget %ld.%ld, minimap.gadget %ld.%ld", S(MSG_ABOUT_LICENSE),
             DirTreeBase != NULL ? (long)DirTreeBase->lib_Version : 0L,
             DirTreeBase != NULL ? (long)DirTreeBase->lib_Revision : 0L,
             MinimapBase != NULL ? (long)MinimapBase->lib_Version : 0L,
             MinimapBase != NULL ? (long)MinimapBase->lib_Revision : 0L);
    if (about_window(app, details)) return;
    /* without the window: a requester */
    args[0] = (ULONG)APP_NAME;
    args[1] = (ULONG)APP_VERSION;
    args[2] = (ULONG)details;
    EasyRequestArgs(app != NULL ? app->window : NULL, &es, NULL, args);
}

void ui_refresh(EditorApp *app)
{
    static char window_title[EDITOR_TITLE_MAX + 24];
    if (app->active == NULL) strcpy(window_title, "TinyDev");
    else snprintf(window_title, sizeof(window_title), "TinyDev - %s%s",
                  app->active->title, app->active->dirty ? "*" : "");
    if (app->window_object != NULL) SetAttrs(app->window_object, WA_Title, (ULONG)window_title, TAG_END);
}

/** The active editor has rectangular selections (as the menu shows it). */
static int rect_shown;

/**
 * @brief Tell whether the active editor has rectangular selections.
 *
 * texteditor.gadget toggles them itself with Ctrl+left Alt (only with a
 * fixed-width font and without word wrap), easily pressed by mistake, so
 * the state is read on every tick and shown in the menu and status line.
 *
 * @param app The application state.
 * @return Non-zero if it has.
 */
static int rect_selections(EditorApp *app)
{
    ULONG on = 0;
    if (app->active != NULL) GetAttr(GA_TEXTEDITOR_RectangularSelections, app->active->editor, &on);
    return on != 0;
}

/** File Type item checked for the active document (FT_SHOWN_), or a
 *  language's index; FT_SHOWN_NONE makes ui_update_status() set it. */
static int ft_shown = -3;
#define FT_SHOWN_AUTO -1    /**< File Type/Automatic */
#define FT_SHOWN_TEXT -2    /**< File Type/Plain Text */
#define FT_SHOWN_NONE -3    /**< not known: to be set */

/**
 * @brief Return the File Type item of the active document.
 * @param app The application state.
 * @return FT_SHOWN_AUTO, FT_SHOWN_TEXT or the index of a language.
 */
static int file_type_of(EditorApp *app)
{
    const Document *doc = app->active;
    int i;
    if (doc == NULL || !doc->lang_chosen) return FT_SHOWN_AUTO;
    if (doc->lang_choice[0] == '\0') return FT_SHOWN_TEXT;
    for (i = 0; i < scoutlink_language_count() && i < FILETYPE_SLOTS; ++i)
        if (strcmp(scoutlink_language_id(i), doc->lang_choice) == 0) return i;
    return FT_SHOWN_AUTO;
}

/**
 * @brief Return the MID_ of a File Type item.
 * @param shown FT_SHOWN_AUTO, FT_SHOWN_TEXT or the index of a language.
 * @return The MID_.
 */
static ULONG file_type_id(int shown)
{
    if (shown == FT_SHOWN_TEXT) return MID_FT_TEXT;
    if (shown >= 0) return (ULONG)(MID_FT_FIRST + shown);
    return MID_FT_AUTO;
}

/**
 * @brief Check the File Type item of the active document (in the window's
 *        menu and in menus[], which window.class builds it from).
 * @param app The application state.
 */
static void sync_file_type(EditorApp *app)
{
    int shown = file_type_of(app), i;
    ULONG id = file_type_id(shown);
    if (shown == ft_shown) return;
    for (i = 0; menus[i].nm_Type != NM_END; ++i) {
        ULONG ud = (ULONG)menus[i].nm_UserData;
        if (ud < MID_FT_AUTO || ud >= MID_FT_FIRST + FILETYPE_SLOTS) continue;
        if (ud == id) menus[i].nm_Flags |= CHECKED;
        else menus[i].nm_Flags &= (UWORD)~CHECKED;
        if (ft_shown == FT_SHOWN_NONE || ud == id || ud == file_type_id(ft_shown))
            ui_set_menu_check(app, ud, ud == id);
    }
    ft_shown = shown;
}

void ui_update_file_types(EditorApp *app)
{
    int i, n = scoutlink_language_count();
    for (i = 0; menus[i].nm_Type != NM_END; ++i) {
        ULONG ud = (ULONG)menus[i].nm_UserData;
        int k = (int)ud - MID_FT_FIRST;
        if (ud < MID_FT_FIRST || k >= FILETYPE_SLOTS || menus[i].nm_Type == NM_TITLE) continue;
        if (k < n) {
            menus[i].nm_Type = NM_ITEM;
            menus[i].nm_Label = (STRPTR)scoutlink_language_name(k);
        } else {
            menus[i].nm_Type = NM_IGNORE;
        }
    }
    ft_shown = FT_SHOWN_NONE;
    sync_file_type(app);
    /* window.class builds the menus anew from the table */
    if (app->window_object != NULL) SetAttrs(app->window_object, WINDOW_NewMenu, (ULONG)menus, TAG_END);
}

/** The message of ui_status_message(). */
static char status_message[200];
/** When the message goes (CurrentTime() seconds), 0 if there is none. */
static ULONG status_message_until;

void ui_status_message(EditorApp *app, const char *text)
{
    ULONG secs = 0, micros = 0;
    if (text == NULL) {                 /* the message goes at once */
        status_message_until = 0;
        ui_update_status(app);
        return;
    }
    CurrentTime(&secs, &micros);
    strncpy(status_message, text, sizeof(status_message) - 1);
    status_message[sizeof(status_message) - 1] = '\0';
    status_message_until = secs + 4;
    ui_update_status(app);
}

void ui_update_status(EditorApp *app)
{
    static char status_text[200], shown_text[200];
    unsigned long doc_count = 0;
    ULONG lines = 0;
    Document *doc;
    for (doc = (Document *)app->documents.lh_Head; doc->node.ln_Succ;
         doc = (Document *)doc->node.ln_Succ)
        ++doc_count;
    if (app->active != NULL)
        GetAttr(GA_TEXTEDITOR_Prop_Entries, app->active->editor, &lines);
    if (app->active != NULL && app->active->scout_symbols >= 0) {
        const DiagItem *d;
        ULONG y = 0;
        GetAttr(GA_TEXTEDITOR_CursorY, app->active->editor, &y);
        snprintf(status_text, sizeof(status_text), S(MSG_STATUS_SCOUT), doc_count,
                 (unsigned long)lines, app->active->scout_symbols, app->active->scout_problems);
        /* the problem of the cursor's line */
        if ((d = diag_at_line(app->active, (long)y + 1)) != NULL) {
            size_t n = strlen(status_text);
            snprintf(status_text + n, sizeof(status_text) - n, " - %s", d->message);
        }
    }
    else
        snprintf(status_text, sizeof(status_text),
                 S(MSG_STATUS), doc_count, (unsigned long)lines);
    if (rect_selections(app) != rect_shown) {
        rect_shown = !rect_shown;
        ui_set_menu_check(app, MID_RECT, rect_shown);
    }
    sync_file_type(app);
    if (rect_shown) {
        char rest[sizeof(status_text)];
        strcpy(rest, status_text);
        snprintf(status_text, sizeof(status_text), S(MSG_STATUS_RECT), rest);
    }
    if (status_message_until != 0) {
        ULONG secs = 0, micros = 0;
        CurrentTime(&secs, &micros);
        if (secs < status_message_until) strcpy(status_text, status_message);
        else status_message_until = 0;
    }
    if (app->statusbar == NULL) return;
    /* Called on every tick: only redraw the status line when it changed. */
    if (strcmp(status_text, shown_text) == 0) return;
    strcpy(shown_text, status_text);
    if (app->window != NULL)
        SetGadgetAttrs((struct Gadget *)app->statusbar, app->window, NULL,
            GA_Text, (ULONG)status_text, TAG_END);
    else SetAttrs(app->statusbar, GA_Text, (ULONG)status_text, TAG_END);
}

void ui_relayout(EditorApp *app)
{
    if (app->defer_depth > 0) { app->pending_layout = 1; return; }
    if (app->window != NULL && app->layout != NULL)
        RethinkLayout((struct Gadget *)app->layout, app->window, NULL, TRUE);
}

void ui_refresh_page(EditorApp *app, Document *doc)
{
    if (doc == NULL || app->window == NULL) return;
    if (app->defer_depth > 0) { app->pending_refresh = 1; return; }
    RefreshPageGadget((struct Gadget *)doc->page, app->pages, app->window, NULL);
}

void ui_gadget_set(EditorApp *app, Object *obj, ULONG tag1, ULONG data1,
                   ULONG tag2, ULONG data2)
{
    if (obj == NULL) return;
    if (app->window != NULL)
        SetGadgetAttrs((struct Gadget *)obj, app->window, NULL,
                       tag1, data1, tag2, data2, TAG_END);
    else SetAttrs(obj, tag1, data1, tag2, data2, TAG_END);
}

void ui_set_menu_check(EditorApp *app, ULONG id, int on)
{
    struct Menu *strip = NULL, *m;
    struct MenuItem *item;
    if (app->window_object == NULL || app->window == NULL) return;
    GetAttr(WINDOW_MenuStrip, app->window_object, (ULONG *)&strip);
    if (strip == NULL) return;
    ClearMenuStrip(app->window);
    for (m = strip; m != NULL; m = m->NextMenu)
        for (item = m->FirstItem; item != NULL; item = item->NextItem)
            if ((ULONG)GTMENUITEM_USERDATA(item) == id) {
                if (on) item->Flags |= CHECKED;
                else item->Flags &= (UWORD)~CHECKED;
            }
    ResetMenuStrip(app->window, strip);
}

void ui_begin_update(EditorApp *app)
{
    ++app->defer_depth;
}

void ui_end_update(EditorApp *app)
{
    if (app->defer_depth == 0 || --app->defer_depth > 0) return;
    if (app->pending_layout) {
        app->pending_layout = app->pending_refresh = 0;
        ui_relayout(app);
    } else if (app->pending_refresh) {
        app->pending_refresh = 0;
        ui_refresh_page(app, app->active);
    }
}

int ui_confirm_close(EditorApp *app, Document *doc)
{
    char text[EDITOR_TITLE_MAX + 80]; int answer;
    if (!doc->dirty) return 1;
    snprintf(text, sizeof(text), S(MSG_CONFIRM_TEXT), doc->title);
    { struct EasyStruct es = {sizeof(es), 0, (STRPTR)S(MSG_CONFIRM_TITLE), text, (STRPTR)S(MSG_CONFIRM_GADGETS)};
      answer = EasyRequestArgs(app->window, &es, NULL, NULL); }
    if (answer == 2) return 1;
    if (answer != 1) return 0;
    return doc->path != NULL ? file_save(app, doc, doc->path) : file_request_save(app, doc);
}



/**
 * @brief Set the check marks of the View menu in menus[] from the state.
 *
 * window.class builds the menu strip anew from menus[] at every WM_OPEN,
 * also after iconifying, so the table must hold the current marks then.
 *
 * @param app The application state.
 * @param bm_open Non-zero if the bookmark list is (going to be) open.
 */
static void set_menu_checks(EditorApp *app, int bm_open)
{
    int i;
    for (i = 0; menus[i].nm_Type != NM_END; ++i) {
        APTR ud = menus[i].nm_UserData;
        int on;
        if (ud == UD(MID_BM_LIST)) on = bm_open;
        else if (ud == UD(MID_FOLDER_TREE)) on = app->tree_visible;
        else if (ud == UD(MID_LINE_NUMBERS)) on = app->line_numbers;
        else if (ud == UD(MID_MINIMAP)) on = app->minimap_visible;
        else if (ud == UD(MID_OUTPUT)) on = app->outwin_was_open;
        else if (ud == UD(MID_SYMLIST)) on = app->symlist_was_open;
        else if (ud == UD(MID_DIAGLIST)) on = app->diaglist_was_open;
        else if (ud == UD(MID_RECT)) on = rect_shown;
        else continue;
        if (on) menus[i].nm_Flags |= CHECKED;
        else menus[i].nm_Flags &= (UWORD)~CHECKED;
    }
}

int ui_create(EditorApp *app)
{
    long win_width, win_height, win_left, win_top;
    struct DiskObject *icon = NULL;
    int i;
    memcpy(app->bm_box, app->settings.bmwin, sizeof(app->bm_box));
    memcpy(app->out_box, app->settings.outwin, sizeof(app->out_box));
    memcpy(app->sym_box, app->settings.symwin, sizeof(app->sym_box));
    memcpy(app->diag_box, app->settings.diagwin, sizeof(app->diag_box));
    app->screen = LockPubScreen(NULL);
    if (app->screen == NULL) return 0;
    diag_open(app);
    if (!open_editor_colors(app)) {
        ui_error(app, APP_NAME, S(MSG_ERR_COLORS));
        return 0;
    }
    if (app->screen_draw_info != NULL)
        bookmarks_set_background(app->screen_draw_info->dri_Pens[BACKGROUNDPEN]);

    app->pages = NewObject(PAGE_GetClass(), NULL, PAGE_NoDispose, TRUE, TAG_END);
    if (app->pages == NULL) return 0;
    /* The tab close gadgets show a simple built-in black cross. */
    app->tab_close_image = close_image_new();
    if (app->tab_close_image == NULL) return 0;
    app->tabs = NewObject(CLICKTAB_GetClass(), NULL,
        GA_ID, GID_TABS, GA_RelVerify, TRUE,
        CLICKTAB_Labels, (ULONG)&app->tab_nodes,
        CLICKTAB_PageGroup, (ULONG)app->pages,
        CLICKTAB_PageGroupBorder, TRUE,
        CLICKTAB_AutoFit, TRUE,
        CLICKTAB_CloseImage, (ULONG)app->tab_close_image,
        CLICKTAB_ClosePlacement, PLACECLOSE_LEFT,
        /* The clicktab.gadget does NOT emit a GADGETUP when a tab close
         * gadget is used (the autodocs are wrong on this point).  The
         * close event is only delivered through the interconnection
         * (icclass) notification chain, so target the window IDCMP to
         * receive it as a WMHI_GADGETUP that tab_event() can react to. */
        ICA_TARGET, (ULONG)ICTARGET_IDCMP,
        TAG_END);
    if (app->tabs == NULL) return 0;
    /* dirtree.gadget owns the hierarchical node list and its expand/collapse
     * arrows; only the listbrowser look is configured here. */
    app->tree = NewObject(NULL, DIRTREE_CLASSNAME,
        GA_ID, GID_TREE,
        GA_RelVerify, TRUE,
        LISTBROWSER_AutoFit, TRUE,
        LISTBROWSER_HorizontalProp, TRUE,
        LISTBROWSER_MinVisible, 4,
        LISTBROWSER_AutoWheel, TRUE,
        DIRTREE_LoadingText, (ULONG)S(MSG_TREE_LOADING),
        DIRTREE_EmptyText, (ULONG)S(MSG_TREE_EMPTY),
        DIRTREE_UnavailableText, (ULONG)S(MSG_TREE_UNAVAILABLE),
        TAG_END);
    if (app->tree == NULL) return 0;
    if (minimap_create_gadget(app) == NULL) return 0;
    if (!toolbar_create(app)) return 0;
    app->content_layout = NewObject(LAYOUT_GetClass(), NULL,
        LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ,
        LAYOUT_AddChild, (ULONG)app->tree,
        CHILD_MinWidth, 140,
        CHILD_WeightedWidth, 25,
        CHILD_WeightBar, TRUE,
        LAYOUT_AddChild, (ULONG)app->tabs,
        CHILD_WeightedWidth, 75,
        /* A WeightBar between the editor and the minimap lets the user resize
         * the minimap column the same way the folder tree is resized.  It is
         * declared statically and never toggled at runtime (layout.gadget does
         * not remove a toggled weight bar, so toggling would accumulate stray
         * bars).  minimap_set_visible() only collapses the minimap child to
         * zero width when hidden, exactly like the folder tree. */
        CHILD_WeightBar, TRUE,
        LAYOUT_AddChild, (ULONG)app->minimap,
        CHILD_MinWidth, 0,
        CHILD_MaxWidth, 0,
        CHILD_WeightedWidth, 0,
        TAG_END);
    if (app->content_layout == NULL) return 0;
    /* The minimap is a permanent child of the content layout; it is only
     * collapsed to zero width when hidden (like the folder tree), never removed
     * at runtime, so it stays attached for the whole lifetime of the layout. */
    app->minimap_attached = 1;
    app->statusbar = NewObject(BUTTON_GetClass(), NULL,
        GA_ReadOnly, TRUE,
        GA_Text, (ULONG)"",
        BUTTON_BevelStyle, BVS_DISPLAY,
        BUTTON_Justification, BCJ_LEFT,
        TAG_END);
    if (app->statusbar == NULL) return 0;
    app->layout = NewObject(LAYOUT_GetClass(), NULL,
        LAYOUT_Orientation, LAYOUT_ORIENT_VERT,
        LAYOUT_AddChild, (ULONG)app->toolbar,
        CHILD_WeightedHeight, 0,
        LAYOUT_AddChild, (ULONG)app->content_layout,
        CHILD_WeightedHeight, 100,
        LAYOUT_AddChild, (ULONG)app->statusbar,
        CHILD_WeightedHeight, 0,
        TAG_END);
    if (app->layout == NULL) return 0;
    memset(&app->tab_idcmp_hook, 0, sizeof(app->tab_idcmp_hook));
    app->tab_idcmp_hook.h_Entry = (ULONG (*)())HookEntry;
    app->tab_idcmp_hook.h_SubEntry = (ULONG (*)())tab_idcmp_entry;
    app->tab_idcmp_hook.h_Data = app;
    {
        /* size and position from the settings: size 0 = from the screen,
         * position -1 = centred; a window that would stick out is moved in */
        long sw = app->screen->Width, sh = app->screen->Height;
        win_width = app->settings.winwidth > 0 ? app->settings.winwidth : sw * 9 / 10;
        win_height = app->settings.winheight > 0 ? app->settings.winheight : sh * 4 / 5;
        if (win_width > sw) win_width = sw;
        if (win_height > sh) win_height = sh;
        win_left = app->settings.winleft;
        win_top = app->settings.wintop;
        if (win_left > sw - win_width) win_left = sw - win_width;
        if (win_top > sh - win_height) win_top = sh - win_height;
    }
    for (i = 0; i < (int)(sizeof(menu_texts) / sizeof(menu_texts[0])); ++i)
        if (menu_texts[i] >= 0) menus[i].nm_Label = (STRPTR)S(menu_texts[i]);
    app->outwin_was_open = app->settings.outlist;      /* for the check marks */
    app->symlist_was_open = app->settings.symlist;
    app->diaglist_was_open = app->settings.diaglist;
    set_menu_checks(app, app->settings.bmlist);
    app->outwin_was_open = app->symlist_was_open = app->diaglist_was_open = 0;
    /* AppPort: the window can be iconified, with the program icon as AppIcon */
    app->app_port = CreateMsgPort();
    if (app->app_port != NULL && IconBase != NULL && app->settings.icon_name[0] != '\0' &&
        (icon = GetDiskObject((STRPTR)app->settings.icon_name)) != NULL)
        icon->do_CurrentX = icon->do_CurrentY = (LONG)NO_ICON_POSITION;
    /* the folder tree as the settings want it at the start, before the
     * window opens (the layout has it shown) */
    if (!app->tree_visible) tree_set_visible(app, 0);
    app->window_object = NewObject(WINDOW_GetClass(), NULL,
        WA_Title, (ULONG)"TinyDev", WA_DragBar, TRUE, WA_DepthGadget, TRUE,
        WA_CloseGadget, TRUE, WA_SizeGadget, TRUE, WA_Activate, TRUE,
        /* The window.class only enables the IDCMP flags its gadgets need.
         * IDCMP_NEWSIZE and IDCMP_MOUSEMOVE (reported while a WeightBar is
         * dragged) wake the event loop after the minimap's size changed, so
         * minimap_poll() can have it re-rendered at the new size.
         * IDCMP_INTUITICKS wakes it regularly while the window is active,
         * so the toolbar follows the selection and undo state. */
        WA_IDCMP, IDCMP_NEWSIZE | IDCMP_MOUSEMOVE | IDCMP_INTUITICKS | IDCMP_ACTIVEWINDOW,
        WA_PubScreen, (ULONG)app->screen,
        WA_Width, (ULONG)win_width, WA_Height, (ULONG)win_height,
        app->settings.winleft >= 0 ? WA_Left : TAG_IGNORE, (ULONG)win_left,
        app->settings.wintop >= 0 ? WA_Top : TAG_IGNORE, (ULONG)win_top,
        app->settings.winleft < 0 || app->settings.wintop < 0 ? WINDOW_Position : TAG_IGNORE,
            WPOS_CENTERSCREEN,
        WINDOW_NewMenu, (ULONG)menus,
        WINDOW_MenuUserData, WGUD_IGNORE,
        WINDOW_ParentGroup, (ULONG)app->layout,
        /* Enable the IDCMPUPDATE class so the clicktab close notification
         * (routed to the window IDCMP via ICA_TARGET) actually reaches us
         * through this hook. */
        app->app_port != NULL ? WINDOW_AppPort : TAG_IGNORE, (ULONG)app->app_port,
        app->app_port != NULL ? WINDOW_IconifyGadget : TAG_IGNORE, TRUE,
        WINDOW_IconTitle, (ULONG)APP_NAME,
        icon != NULL ? WINDOW_Icon : TAG_IGNORE, (ULONG)icon,   /* disposed with the window */
        WINDOW_HintInfo, (ULONG)toolbar_hints(),
        WINDOW_GadgetHelp, TRUE,
        WINDOW_IDCMPHook, (ULONG)&app->tab_idcmp_hook,
        WINDOW_IDCMPHookBits, (ULONG)IDCMP_IDCMPUPDATE,
        TAG_END);
    if (app->window_object == NULL) {
        if (icon != NULL) FreeDiskObject(icon);
        return 0;
    }
    if (IsListEmpty(&app->documents) && document_new(app) == NULL) return 0;
    splash_status(S(MSG_SPLASH_WINDOW));
    app->window = (struct Window *)DoMethod(app->window_object, WM_OPEN, NULL);
    if (app->window == NULL) return 0;
    splash_step();
    splash_close();
    ui_relayout(app);
    toolbar_window_opened(app);
    document_activate(app, app->active);
    ui_update_status(app);
    minimap_set_visible(app, app->minimap_visible);
    if (app->minimap_visible) minimap_request(app);
    if (app->settings.bmlist) {
        bmlist_open(app);
        ui_set_menu_check(app, MID_BM_LIST, bmlist_is_open());
    }
    if (app->settings.outlist) {
        outwin_open(app);
        ui_set_menu_check(app, MID_OUTPUT, outwin_is_open());
    }
    if (app->settings.symlist) {
        symlist_open(app);
        ui_set_menu_check(app, MID_SYMLIST, symlist_is_open());
    }
    if (app->settings.diaglist) {
        diaglist_open(app);
        ui_set_menu_check(app, MID_DIAGLIST, diaglist_is_open());
    }
    return 1;
}

/**
 * @brief Send an ARexx-style command to the active document's editor gadget.
 *
 * Issues the command (e.g. UNDO/REDO/CUT/COPY/PASTE/SELECTALL) via the
 * texteditor gadget.  Because toolbar/menu picks steal input focus, it forces
 * an immediate refresh of the editor gadget and reactivates it so the updated
 * contents and caret appear at once, then resyncs the scrollbars and requests
 * a minimap update.  Does nothing if no document is active.
 *
 * @param app The application whose active document receives the command.
 * @param command The ARexx command string to execute.
 */
static void editor_command(EditorApp *app, const char *command)
{
    if (app->active == NULL) return;
    DoMethod(app->active->editor, GM_TEXTEDITOR_ARexxCmd, NULL, (STRPTR)command);
    /* Toolbar buttons and menu picks steal the input focus from the editor
     * gadget, so a command issued through them (Undo/Redo/Cut/Copy/Paste)
     * changes the buffer without the gadget redrawing right away.  Force an
     * immediate refresh and hand the focus back to the editor so the new
     * contents and caret appear at once. */
    if (app->window != NULL) {
        RefreshGList((struct Gadget *)app->active->editor, app->window, NULL, 1);
        if (app->layout != NULL)
            /* the object, not its ID (layout_gc/ActivateLayoutGadget) */
            ActivateLayoutGadget((struct Gadget *)app->layout, app->window,
                                 NULL, (ULONG)app->active->editor);
    }
    document_sync_scrollers(app, app->active);
    minimap_request(app);
}

/**
 * @brief Save the active document, optionally forcing a Save As prompt.
 *
 * Saves directly to the existing path when save_as is false and a path is
 * known; otherwise prompts the user for a destination path.
 *
 * @param app The application whose active document is saved.
 * @param save_as Nonzero to force a Save As file requester.
 * @return Nonzero on a successful save, 0 on failure or if no document is
 *         active.
 */
static int save_active(EditorApp *app, int save_as)
{
    Document *doc = app->active;
    if (doc == NULL) return 0;
    if (!save_as && doc->path != NULL) return file_save(app, doc, doc->path);
    return file_request_save(app, doc);
}

/**
 * @brief Confirm closing every open document.
 *
 * Iterates over all documents and runs ui_confirm_close() on each; aborts as
 * soon as the user cancels one.
 *
 * @param app The application whose documents are checked.
 * @return 1 if all documents may be closed, 0 if the user cancelled.
 */
static int close_all(EditorApp *app)
{
    Document *doc, *next;
    if (!build_may_quit(app)) return 0;
    for (doc = (Document *)app->documents.lh_Head; doc->node.ln_Succ; doc = next) {
        next = (Document *)doc->node.ln_Succ;
        if (!ui_confirm_close(app, doc)) return 0;
    }
    return 1;
}

/**
 * @brief Dispatch a menu item selection to the corresponding action.
 *
 * Handles the Project, Edit, and View menu commands: creating/opening/saving/
 * closing documents, showing the About requester, quitting (after confirming
 * all closes), editor commands, toggling the folder tree and minimap, and
 * toggling line numbers (updating every document's editor gadget and relaying
 * out).
 *
 * @param app The application acting on the command.
 * @param id The menu command identifier (MID_*).
 */
static void menu_action(EditorApp *app, ULONG id);

/**
 * @brief Show the settings window and take over its result; "Save" also
 *        writes the settings into the program icon.
 * @param app The application state.
 */
static void edit_settings(EditorApp *app)
{
    Settings n = app->settings;
    int r = prefs_dialog(app, &n);
    if (r == PREFS_CANCEL) return;
    ui_apply_settings(app, &n);
    if (r == PREFS_SAVE && !settings_save(&app->settings))
        ui_error(app, APP_NAME, S(MSG_ERR_SAVESETTINGS));
}

void ui_apply_settings(EditorApp *app, const Settings *n)
{
    struct ColorMap *cm;
    long pens[SCOL_COUNT];
    int i, changed;
    Document *doc;

    int scout = app->settings.scout;

    changed = memcmp(app->settings.colours, n->colours, sizeof(n->colours)) != 0;
    app->settings = *n;
    if (scout != n->scout) scoutlink_settings_changed(app);
    if (!changed || !app->editor_colors_open || app->screen == NULL) return;
    cm = app->screen->ViewPort.ColorMap;
    for (i = 0; i < SCOL_COUNT; ++i) {
        unsigned long c = n->colours[i];
        pens[i] = ObtainBestPen(cm, rgb32(c, 16), rgb32(c, 8), rgb32(c, 0),
                                OBP_Precision, PRECISION_GUI, OBP_FailIfBad, FALSE, TAG_END);
        if (pens[i] < 0) {
            while (--i >= 0) ReleasePen(cm, (ULONG)pens[i]);
            return;
        }
    }
    for (i = 0; i < SCOL_COUNT; ++i) {
        long old = app->editor_pens[EDITOR_COLOR_TEXT + i];
        app->editor_pens[EDITOR_COLOR_TEXT + i] = pens[i];
        pens[i] = old;                          /* released below */
    }
    app->editor_draw_pens[TEXTPEN] = (UWORD)app->editor_pens[EDITOR_COLOR_TEXT];
    for (doc = (Document *)app->documents.lh_Head; doc->node.ln_Succ != NULL;
         doc = (Document *)doc->node.ln_Succ) {
        document_set_pens(app, doc);
    }
    if (app->active != NULL && app->window != NULL) {
        /* setting the hook again makes the editor highlight anew */
        SetGadgetAttrs((struct Gadget *)app->active->editor, app->window, NULL,
            GA_TEXTEDITOR_HighlighterHook, 0, TAG_END);
        SetGadgetAttrs((struct Gadget *)app->active->editor, app->window, NULL,
            GA_TEXTEDITOR_HighlighterHook, (ULONG)&app->active->highlight.hook, TAG_END);
        RefreshPageGadget((struct Gadget *)app->active->page, app->pages, app->window, NULL);
    }
    if (app->minimap != NULL) {
        if (app->window != NULL)
            SetGadgetAttrs((struct Gadget *)app->minimap, app->window, NULL,
                MINIMAP_TextPen, (ULONG)app->editor_pens[EDITOR_COLOR_TEXT],
                MINIMAP_TextChanged, TRUE, TAG_END);
        else SetAttrs(app->minimap, MINIMAP_TextPen, (ULONG)app->editor_pens[EDITOR_COLOR_TEXT],
                      MINIMAP_TextChanged, TRUE, TAG_END);
    }
    for (i = 0; i < SCOL_COUNT; ++i) ReleasePen(cm, (ULONG)pens[i]);
}

/**
 * @brief Iconify the main window: the open tool windows close (and are
 *        remembered), the window becomes the program icon on the Workbench.
 * @param app The application state.
 */
static void ui_iconify(EditorApp *app)
{
    struct Window *still = NULL;
    if (app->window == NULL || app->app_port == NULL) return;
    app->find_was_open = find_hide();
    app->bmlist_was_open = bmlist_is_open();
    if (app->bmlist_was_open) bmlist_close(app);   /* remembers its box */
    app->outwin_was_open = outwin_is_open();
    if (app->outwin_was_open) outwin_close(app);   /* likewise */
    app->symlist_was_open = symlist_is_open();
    if (app->symlist_was_open) symlist_close(app);
    app->diaglist_was_open = diaglist_is_open();
    if (app->diaglist_was_open) diaglist_close(app);
    app->main_box[0] = app->window->LeftEdge;
    app->main_box[1] = app->window->TopEdge;
    app->main_box[2] = app->window->Width;
    app->main_box[3] = app->window->Height;
    app->scrollbar_dragging = 0;
    toolbar_window_closed();
    DoMethod(app->window_object, WM_ICONIFY, NULL);
    GetAttr(WINDOW_Window, app->window_object, (ULONG *)&still);
    if (still != NULL) {                /* not iconified: everything as it was */
        toolbar_window_opened(app);
        if (app->bmlist_was_open) bmlist_open(app);
        if (app->outwin_was_open) outwin_open(app);
        if (app->symlist_was_open) symlist_open(app);
        if (app->diaglist_was_open) diaglist_open(app);
        if (app->find_was_open) find_show(app);
        app->outwin_was_open = app->symlist_was_open = app->diaglist_was_open = 0;
        return;
    }
    app->window = NULL;
}

/**
 * @brief Open the main window again after ui_iconify(), where it was, with
 *        the tool windows that were open.
 * @param app The application state.
 * @return Non-zero if the window is open.
 */
static int ui_uniconify(EditorApp *app)
{
    struct Window *w;
    if (app->window != NULL) return 1;
    set_menu_checks(app, app->bmlist_was_open);
    if ((w = (struct Window *)DoMethod(app->window_object, WM_OPEN, NULL)) == NULL) return 0;
    app->window = w;
    if (w->LeftEdge != app->main_box[0] || w->TopEdge != app->main_box[1] ||
        w->Width != app->main_box[2] || w->Height != app->main_box[3])
        ChangeWindowBox(w, app->main_box[0], app->main_box[1], app->main_box[2], app->main_box[3]);
    toolbar_window_opened(app);
    ui_relayout(app);
    /* the tool windows in front of the main window, the main window active */
    if (app->bmlist_was_open) {
        bmlist_open(app);
        ui_set_menu_check(app, MID_BM_LIST, bmlist_is_open());
    }
    if (app->outwin_was_open) {
        outwin_open(app);
        ui_set_menu_check(app, MID_OUTPUT, outwin_is_open());
    }
    if (app->symlist_was_open) {
        symlist_open(app);
        ui_set_menu_check(app, MID_SYMLIST, symlist_is_open());
    }
    if (app->diaglist_was_open) {
        diaglist_open(app);
        ui_set_menu_check(app, MID_DIAGLIST, diaglist_is_open());
    }
    if (app->find_was_open) find_show(app);
    app->bmlist_was_open = app->find_was_open = app->outwin_was_open = 0;
    app->symlist_was_open = app->diaglist_was_open = 0;
    ActivateWindow(w);
    if (app->active != NULL && app->layout != NULL)
        ActivateLayoutGadget((struct Gadget *)app->layout, w, NULL, (ULONG)app->active->editor);
    /* what changed meanwhile: directories on disk, the minimap */
    tree_refresh(app, 0);
    minimap_request(app);
    return 1;
}

static void menu_action(EditorApp *app, ULONG id)
{
    Document *doc;
    if (id >= MID_FT_AUTO && id < MID_FT_FIRST + FILETYPE_SLOTS) {
        int k = (int)id - MID_FT_FIRST;
        if (app->active == NULL) return;
        if (id == MID_FT_AUTO) scoutlink_choose_language(app, app->active, NULL);
        else if (id == MID_FT_TEXT) scoutlink_choose_language(app, app->active, "");
        else if (k < scoutlink_language_count()) scoutlink_choose_language(app, app->active, scoutlink_language_id(k));
        ui_update_status(app);
        return;
    }
    switch (id) {
        case MID_NEW: document_new(app); break;
        case MID_OPEN: file_request_open(app); break;
        case MID_OPEN_DIRECTORY: tree_request_directory(app); break;
        case MID_SAVE:
        case MID_SAVE_AS:
            save_active(app, id == MID_SAVE_AS);
            /* the menu pick (Amiga+S) or the button took the input focus:
             * give it back, or the cursor is gone */
            if (app->window != NULL && app->layout != NULL && app->active != NULL)
                ActivateLayoutGadget((struct Gadget *)app->layout, app->window, NULL,
                                     (ULONG)app->active->editor);
            break;
        case MID_CLOSE: document_close(app, app->active, 1); break;
        case MID_ABOUT: ui_about(app); break;
        case MID_HELP: help_open(app); break;
        case MID_SETTINGS: edit_settings(app); break;
        case MID_PROJECT_SETTINGS: project_settings(app); break;
        case MID_BUILD: case MID_RUN: case MID_DEBUG: case MID_STOP:
            build_action(app, id);
            break;
        case MID_OUTPUT:
            if (outwin_is_open()) outwin_close(app);
            else outwin_open(app);
            ui_set_menu_check(app, MID_OUTPUT, outwin_is_open());
            break;
        case MID_ICONIFY: ui_iconify(app); break;
        case MID_QUIT: if (close_all(app)) app->running = 0; break;
        case MID_UNDO:
            editor_command(app, "UNDO");
            document_check_unchanged(app, app->active);
            break;
        case MID_REDO:
            editor_command(app, "REDO");
            document_check_unchanged(app, app->active);
            break;
        case MID_CUT: editor_command(app, "CUT"); break;
        case MID_COPY: editor_command(app, "COPY"); break;
        case MID_PASTE: editor_command(app, "PASTE"); break;
        case MID_SELECT_ALL: editor_command(app, "SELECTALL"); break;
        case MID_RECT:
            if (app->active != NULL)
                ui_gadget_set(app, app->active->editor, GA_TEXTEDITOR_RectangularSelections,
                              (ULONG)!rect_selections(app), TAG_IGNORE, 0);
            /* the editor may refuse (proportional font, word wrap):
             * marking the shown state as wrong makes ui_update_status()
             * set the menu's check mark from the editor's state */
            rect_shown = !rect_selections(app);
            ui_update_status(app);
            break;
        case MID_FIND: find_open(app); break;
        case MID_FIND_NEXT: find_next(app); break;
        case MID_BM_TOGGLE: case MID_BM_NEXT: case MID_BM_PREV:
        case MID_BM_CLEAR:
            bookmarks_command(app, id);
            break;
        case MID_FOLDER_TREE: tree_set_visible(app, !app->tree_visible); break;
        case MID_TREE_REFRESH: tree_refresh(app, 1); break;
        case MID_BM_LIST:
            if (bmlist_is_open()) bmlist_close(app);
            else bmlist_open(app);
            ui_set_menu_check(app, MID_BM_LIST, bmlist_is_open());
            break;
        case MID_COMPLETE:
        case MID_DEFINITION:
        case MID_GOBACK:
            if (id == MID_DEFINITION) navigate_definition(app);
            else if (id == MID_GOBACK) navigate_back(app);
            else complete_start(app);
            /* the menu pick took the input focus; a jump gives it back
             * later, without one (nothing found) it is given back here */
            if (app->window != NULL && app->layout != NULL && app->active != NULL)
                ActivateLayoutGadget((struct Gadget *)app->layout, app->window, NULL,
                                     (ULONG)app->active->editor);
            break;
        case MID_SYMLIST:
            if (symlist_is_open()) symlist_close(app);
            else symlist_open(app);
            ui_set_menu_check(app, MID_SYMLIST, symlist_is_open());
            break;
        case MID_DIAGLIST:
            if (diaglist_is_open()) diaglist_close(app);
            else diaglist_open(app);
            ui_set_menu_check(app, MID_DIAGLIST, diaglist_is_open());
            break;
        case MID_SNAPSHOT_TOOLS:
            /* the tool windows' boxes and which are open, into the icon */
            bmlist_remember(app);
            memcpy(app->settings.bmwin, app->bm_box, sizeof(app->bm_box));
            app->settings.bmlist = bmlist_is_open();
            outwin_remember(app);
            memcpy(app->settings.outwin, app->out_box, sizeof(app->out_box));
            app->settings.outlist = outwin_is_open();
            symlist_remember(app);
            memcpy(app->settings.symwin, app->sym_box, sizeof(app->sym_box));
            app->settings.symlist = symlist_is_open();
            diaglist_remember(app);
            memcpy(app->settings.diagwin, app->diag_box, sizeof(app->diag_box));
            app->settings.diaglist = diaglist_is_open();
            if (!settings_save_toolwindows(&app->settings))
                ui_error(app, APP_NAME, S(MSG_ERR_SAVESETTINGS));
            break;
        case MID_MINIMAP:
            minimap_set_visible(app, !app->minimap_visible);
            if (app->minimap_visible) minimap_request(app);
            break;
        case MID_LINE_NUMBERS:
            app->line_numbers = !app->line_numbers;
            for (doc = (Document *)app->documents.lh_Head; doc->node.ln_Succ;
                 doc = (Document *)doc->node.ln_Succ) {
                SetAttrs(doc->editor, GA_TEXTEDITOR_ShowLineNumbers,
                         (ULONG)app->line_numbers, TAG_END);
            }
            ui_relayout(app);
            if (app->window != NULL && app->active != NULL)
                RefreshPageGadget((struct Gadget *)app->active->page,
                                  app->pages, app->window, NULL);
            break;
    }
}


/**
 * @brief Close the document whose tab close gadget was used.
 *
 * The closed node is normally captured by tab_idcmp_entry() into
 * app->pending_close; if none is pending it falls back to querying
 * CLICKTAB_NodeClosed.  It resolves the associated Document from the node's
 * user data, resets the gadget's close-node (to avoid closing the same
 * document twice and dereferencing a freed node), and closes the document.
 * Called on every event-loop wake-up and before a tab switch so gadget
 * disposal happens outside raw input handling.
 *
 * @param app The application whose pending tab close is processed.
 */
static void tab_check_closed(EditorApp *app)
{
    ULONG value = 0; struct Node *node; Document *doc = NULL;
    if (app->tabs == NULL) return;
    /* The IDCMP hook records a closed node from the clicktab's IDCMPUPDATE
     * notification; fall back to querying the attribute directly in case a
     * value was set without a delivered notification. */
    node = app->pending_close;
    app->pending_close = NULL;
    if (node == NULL) {
        GetAttr(CLICKTAB_NodeClosed, app->tabs, &value);
        node = (struct Node *)value;
    }
    if (node == NULL) return;
    GetClickTabNodeAttrs(node, TNA_UserData, (ULONG)&doc, TAG_END);
    /* Clear the close-node right away: the gadget keeps returning the last
     * closed node until it is reset, which would otherwise make us close the
     * same document again and dereference a node that document_close() has
     * already freed. */
    if (app->window != NULL)
        SetGadgetAttrs((struct Gadget *)app->tabs, app->window, NULL,
            CLICKTAB_NodeClosed, (ULONG)NULL, TAG_END);
    else SetAttrs(app->tabs, CLICKTAB_NodeClosed, (ULONG)NULL, TAG_END);
    if (doc != NULL) document_close(app, doc, 1);
}

/**
 * @brief Handle a click on the tab bar (switch and/or close).
 *
 * First processes any pending tab close so a tab about to be removed is never
 * switched to, then reads the current tab node and, if it maps to a document,
 * makes it active: refreshes the title, redraws its page, resyncs the
 * scrollbars, and requests a minimap re-render from the new contents.
 *
 * @param app The application whose active tab may change.
 */
static void tab_event(EditorApp *app)
{
    ULONG value = 0; struct Node *node = NULL; Document *doc;
    /* A close gadget click may arrive together with a tab switch; handle a
     * pending close first so we never switch to a tab that is about to go. */
    tab_check_closed(app);
    GetAttr(CLICKTAB_CurrentNode, app->tabs, &value);
    node = (struct Node *)value;
    if (node != NULL) {
        doc = NULL;
        GetClickTabNodeAttrs(node, TNA_UserData, (ULONG)&doc, TAG_END);
        if (doc != NULL) {
            app->active = doc;
            ui_refresh(app);
            if (app->window != NULL)
                RefreshPageGadget((struct Gadget *)doc->page, app->pages,
                                  app->window, NULL);
            document_sync_scrollers(app, doc);
            /* Switching tabs changes the active document, so the minimap must
             * be re-rendered from the newly selected document's contents. */
            minimap_request(app);
        }
    }
}

int ui_run(EditorApp *app)
{
    ULONG signals = 0, result, mask;
    /* WM_HANDLEINPUT stores the gadget/IDCMP code through a WORD* (wmh_Code),
     * so this must be a 16-bit WORD.  Using a ULONG here made the returned
     * value land in the high word on the big-endian 68000, so (UWORD)code was
     * always zero. */
    WORD code = 0;
    ULONG scroll_mask = 1UL << (ULONG)app->scroll_signal;
    ULONG minimap_mask = minimap_signal_mask(app);
    ULONG tree_mask = tree_signal_mask(app);
    app->running = 1;
    while (app->running) {
        /* iconifying changes the window's port: read the mask every time */
        mask = 0;
        GetAttr(WINDOW_SigMask, app->window_object, &mask);
        /* A finished minimap render job only needs to wake the loop: the
         * minimap_poll() at its end shows the new picture. */
        signals = Wait(mask | scroll_mask | minimap_mask | tree_mask | find_sigmask() |
                       bmlist_sigmask() | outwin_sigmask() | build_sigmask() | scoutlink_sigmask() |
                       symlist_sigmask() | diaglist_sigmask() | complete_sigmask() |
                       SIGBREAKF_CTRL_C);
        build_handle(app, signals);
        scoutlink_handle(app);
        /* a shown directory changed (a file was made, deleted, renamed) */
        if ((signals & tree_mask) != 0) tree_refresh(app, 0);
        if ((signals & scroll_mask) != 0) document_scroll_live(app);
        if (signals & SIGBREAKF_CTRL_C) { if (close_all(app)) break; }
        while ((result = DoMethod(app->window_object, WM_HANDLEINPUT, &code)) != WMHI_LASTMSG) {
            ULONG kind = result & WMHI_CLASSMASK;
            if (kind == WMHI_CLOSEWINDOW) { if (close_all(app)) app->running = 0; }
            else if (kind == WMHI_ICONIFY) ui_iconify(app);
            else if (kind == WMHI_UNICONIFY) {
                if (!ui_uniconify(app)) {
                    /* back as an icon rather than lose unsaved documents */
                    ui_error(app, APP_NAME, S(MSG_ERR_REOPEN));
                    DoMethod(app->window_object, WM_ICONIFY, NULL);
                }
            }
            else if (kind == WMHI_GADGETUP && (result & WMHI_GADGETMASK) == GID_TABS) tab_event(app);
            else if (kind == WMHI_GADGETUP && (result & WMHI_GADGETMASK) == GID_TREE) tree_handle_event(app);
            else if (kind == WMHI_GADGETUP && (result & WMHI_GADGETMASK) == GID_VSCROLL) document_scroll_finish(app, 0);
            else if (kind == WMHI_GADGETUP && (result & WMHI_GADGETMASK) == GID_HSCROLL) document_scroll_finish(app, 1);
            else if (kind == WMHI_INTUITICK) { document_scroll_tick(app); document_change_tick(app); }
            else if (kind == WMHI_ACTIVE && app->focus_editor && app->active != NULL) {
                /* activated from a tool window: the editor gets the focus */
                app->focus_editor = 0;
                ActivateLayoutGadget((struct Gadget *)app->layout, app->window, NULL,
                                     (ULONG)app->active->editor);
            }
            /* Clicks and drags in the minimap arrive as IDCMPUPDATE
             * notifications (window IDCMP hook); its GADGETUP needs nothing. */
            else if (kind == WMHI_GADGETUP && (result & WMHI_GADGETMASK) == GID_MINIMAP) ;
            else if (kind == WMHI_GADGETUP && (result & WMHI_GADGETMASK) == GID_TOOLBAR)
                menu_action(app, (ULONG)(UWORD)code);
            else if (kind == WMHI_GADGETUP && (result & WMHI_GADGETMASK) == GID_OVERFLOW) {
                ULONG command = toolbar_overflow_command((ULONG)(UWORD)code);
                if (command != 0) menu_action(app, command);
            }
            else if (kind == WMHI_MENUPICK) {
                struct Menu *strip = NULL; struct MenuItem *item;
                GetAttr(WINDOW_MenuStrip, app->window_object, (ULONG *)&strip);
                item = strip != NULL ? ItemAddress(strip, (UWORD)(result & WMHI_MENUMASK)) : NULL;
                if (item != NULL) menu_action(app, (ULONG)GTMENUITEM_USERDATA(item));
            }
        }
        find_handle(app);
        bookmarks_poll(app);
        bmlist_handle(app);
        symlist_handle(app);
        diaglist_handle(app);
        complete_handle(app);
        outwin_handle(app);
        /* The texteditor gadget does not notify listeners when the user
         * scrolls it from the keyboard or mouse, so re-read its scroll
         * position after every batch of input events (the window wakes us
         * for each key/mouse event) and mirror it onto the scrollbars.  The
         * sync has to happen here instead of inside the WM_HANDLEINPUT loop
         * because that loop body is skipped once WM_HANDLEINPUT returns
         * WMHI_LASTMSG. */
        if (app->active != NULL) {
            ULONG changed = 0;
            GetAttr(GA_TEXTEDITOR_HasChanged, app->active->editor, &changed);
            if (changed) {
                document_changed(app);
                minimap_request(app);
                /* the bookmarks move with the text */
                if (app->active->bm_count > 0) bookmarks_refresh(app, app->active);
            }
        }
        /* The close gadgets on the tabs do not generate a dedicated event, so
         * check for a pending tab close on every wake-up as well. */
        tab_check_closed(app);
        minimap_handle_scroll(app);
        document_sync_scrollers(app, app->active);
        ui_update_status(app);
        toolbar_update(app);
        minimap_poll(app);
        bmlist_update(app);
        symlist_update(app);
        diaglist_update(app);
        document_goto_poll(app);
    }
    return 1;
}

void ui_destroy(EditorApp *app)
{
    build_cleanup(app);
    symlist_cleanup(app);
    diaglist_cleanup(app);
    complete_cleanup(app);
    navigate_cleanup();
    scoutlink_cleanup(app);
    outwin_cleanup(app);
    find_cleanup();
    bmlist_cleanup(app);
    bookmarks_cleanup(app);
    minimap_forget_document(app, NULL);
    document_free_all(app);
    scoutlink_free_rules();       /* the editors that used them are gone */
    if (app->window_object != NULL) {
        if (app->window != NULL) DoMethod(app->window_object, WM_CLOSE, NULL);
        DisposeObject(app->window_object);  /* also the AppIcon and the icon */
    }
    else if (app->layout != NULL) DisposeObject(app->layout);
    else {
        if (app->statusbar != NULL) DisposeObject(app->statusbar);
        if (app->toolbar != NULL) DisposeObject(app->toolbar);
        if (app->content_layout != NULL) DisposeObject(app->content_layout);
        else {
            if (app->minimap != NULL) { DisposeObject(app->minimap); app->minimap = NULL; }
            if (app->tree != NULL) DisposeObject(app->tree);
            if (app->tabs != NULL) DisposeObject(app->tabs);
            else if (app->pages != NULL) DisposeObject(app->pages);
        }
    }
    /* When the minimap column is hidden its gadget has been removed from
     * the content layout, so the layout/window disposal above did not free it.
     * Dispose the still-detached gadget here to avoid leaking it. */
    if (!app->minimap_attached && app->minimap != NULL) {
        DisposeObject(app->minimap);
        app->minimap = NULL;
    }
    toolbar_dispose(app);
    if (app->tab_close_image != NULL) { DisposeObject(app->tab_close_image); app->tab_close_image = NULL; }
    close_image_free_class();
    close_editor_colors(app);
    diag_close(app);
    if (app->screen != NULL) UnlockPubScreen(NULL, app->screen);
    if (app->app_port != NULL) {        /* after the window that used it */
        struct Message *m;
        while ((m = GetMsg(app->app_port)) != NULL) ReplyMsg(m);
        DeleteMsgPort(app->app_port);
        app->app_port = NULL;
    }
    app->window_object = app->layout = app->toolbar = app->content_layout = NULL;
    app->statusbar = NULL;
    app->minimap = NULL;
    app->tree = app->tabs = app->pages = NULL;
    app->window = NULL; app->screen = NULL;
}
