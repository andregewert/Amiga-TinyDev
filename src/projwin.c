/**
 * @file projwin.c
 * @brief The project settings window: the project's name, the commands of
 *        the actions Build, Run and Debug and the include drawers, kept in
 *        \<project\>/.tinydev/project.json.
 *
 * The project's name is a field above the rest (TinyDev's AppIcon shows
 * it). Built like the settings window (prefswin.c): the categories on the left,
 * one page per action on the right with a listbrowser of its commands
 * (name, first line of the command; the checkbox marks the default). The
 * buttons Add, Edit, Remove, Up and Down sit next to the page and act on
 * the list shown, so their shortcuts exist once in the window. A double
 * click edits a command, as does Edit, in a second modal window (name,
 * command lines in a texteditor.gadget, drawer, default). The last page
 * lists the include drawers (for TinyDev-Scout); Add and Edit choose a
 * drawer with the ASL requester (one in the project goes in relative to
 * it). After saving, Scout's session of the project is opened anew, so
 * the include drawers count.
 *
 * The window edits a copy of the settings read when it opens. "Save"
 * reads the file again and replaces only the lists of the actions
 * (projconf_format()), so everything else in it stays; a file that cannot
 * be read or is not a project file is reported and never overwritten.
 * Both windows are modal: the windows below show the busy pointer and
 * their input is dropped.
 *
 * listbrowser.gadget toggles a node's checkbox (LBNA_CheckBox) on a click
 * itself and reports it as LBRE_CHECKED or LBRE_UNCHECKED in
 * LISTBROWSER_RelEvent; the window then compares the nodes' LBNA_Checked
 * with the settings (the click need not select the node).
 */
#include "editor.h"
#include "projconf.h"

#include <classes/window.h>
#include <dos/dos.h>
#include <gadgets/button.h>
#include <gadgets/checkbox.h>
#include <gadgets/layout.h>
#include <gadgets/listbrowser.h>
#include <gadgets/string.h>
#include <gadgets/texteditor.h>
#include <images/bevel.h>
#include <images/label.h>
#include <intuition/gadgetclass.h>
#include <libraries/asl.h>
#include <proto/asl.h>
#include <proto/button.h>
#include <proto/checkbox.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/label.h>
#include <proto/layout.h>
#include <proto/listbrowser.h>
#include <proto/string.h>
#include <proto/texteditor.h>
#include <proto/utility.h>
#include <proto/window.h>
#include <clib/alib_protos.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern struct Library *ListBrowserBase;    /* shared with the settings window */
extern struct Library *CheckBoxBase;       /* likewise */
extern struct Library *StringBase;         /* shared with the find window */

/** The project file in \<project\>/.tinydev/. */
#define PROJECT_FILE "project.json"

/** Gadget IDs of the project window. */
enum {
    PJ_CATS = 100,
    PJ_LIST,                    /* + page */
    PJ_ADD = PJ_LIST + PROJ_ACTIONS + 1,
    PJ_EDIT, PJ_REMOVE, PJ_UP, PJ_DOWN, PJ_SAVE, PJ_CANCEL, PJ_NAME
};

/** Gadget IDs of the command window. */
enum { PE_NAME = 200, PE_COMMAND, PE_DIR, PE_CHOOSE, PE_DEFAULT, PE_OK, PE_CANCEL };

/** Number of pages: one per action, then the include drawers. */
#define PAGES (PROJ_ACTIONS + 1)
/** The page of the include drawers. */
#define PAGE_INCLUDE PROJ_ACTIONS

/** Names of the categories (MSG_), in the order of ProjAction, then the include drawers. */
static const LONG categories[PAGES] = { MSG_PJ_CAT_BUILD, MSG_PJ_CAT_RUN, MSG_PJ_CAT_DEBUG, MSG_PJ_CAT_INCLUDE };

/**
 * @name Spacing
 * As in the settings window, in "virtual pixels".
 */
/** @{ */
#define FRAME_SIDE   8              /**< inside the frame of a page, left and right */
#define FRAME_TOPBOT 6              /**< inside the frame of a page, above and below */
#define ROW_SPACING  4              /**< between rows */
#define AREA_SPACING 6              /**< window border, list, frame, buttons */
/** @} */

/** the child just added keeps its height */
#define FIXED CHILD_WeightedHeight, 0

/** State of the open project window (there is one at a time). */
static struct {
    EditorApp *app;
    char root[EDITOR_PATH_MAX];         /* the project directory */
    char title[EDITOR_PATH_MAX + 64];
    ProjConf conf;                      /* the settings edited */
    Object *winobj, *layout, *cats, *page, *lists[PAGES];
    Object *add, *edit, *remove, *up, *down, *name;
    struct Window *win;
    struct List catlist, nodes[PAGES];
    struct ColumnInfo columns[PAGES][3];
    int action;                         /* the page shown (an action or PAGE_INCLUDE) */
} pj;

/**
 * @brief Open the classes the windows need (once; closed by the windows
 *        that also use them).
 * @param app The application state (for the requester).
 * @return Non-zero if all are available.
 */
static int open_classes(EditorApp *app)
{
    static const struct { struct Library **base; const char *name; } cl[] = {
        { &ListBrowserBase, "gadgets/listbrowser.gadget" },
        { &CheckBoxBase,    "gadgets/checkbox.gadget" },
        { &StringBase,      "gadgets/string.gadget" },
    };
    ULONG i;
    for (i = 0; i < sizeof(cl) / sizeof(cl[0]); ++i)
        if (*cl[i].base == NULL &&
            (*cl[i].base = OpenLibrary((STRPTR)cl[i].name, 44)) == NULL) {
            char text[160];
            snprintf(text, sizeof(text), S(MSG_PREFS_NEEDCLASS), cl[i].name);
            ui_error(app, APP_NAME, text);
            return 0;
        }
    return 1;
}

/**
 * @brief Show a message over a window.
 * @param win The window.
 * @param text The message.
 */
static void message(struct Window *win, const char *text)
{
    struct EasyStruct es = {sizeof(es), 0, (STRPTR)S(MSG_PJ_WINDOW), (STRPTR)text, (STRPTR)S(MSG_OK)};
    EasyRequestArgs(win, &es, NULL, NULL);
}

/**
 * @brief Show a message about the project file over a window.
 * @param win The window (NULL: the main window).
 * @param id The message (MSG_, with %s for the file).
 */
static void file_message(struct Window *win, LONG id)
{
    char path[EDITOR_PATH_MAX], text[EDITOR_PATH_MAX + 200];
    if (!projdata_path(pj.root, PROJECT_FILE, path, sizeof(path))) strcpy(path, PROJECT_FILE);
    snprintf(text, sizeof(text), S(id), path);
    if (win != NULL) message(win, text);
    else ui_error(pj.app, S(MSG_PJ_WINDOW), text);
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
 * @brief A label.image with a text.
 * @param text The text.
 * @return The label.
 */
static Object *label(const char *text)
{
    return NewObject(LABEL_GetClass(), NULL, LABEL_Text, (ULONG)text, TAG_END);
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
 * @brief A text field.
 * @param id Gadget ID.
 * @param text Its contents.
 * @param max Its size (characters plus one).
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
 * @brief Width of a number of average characters of the screen font.
 * @param app The application state.
 * @param n The number of characters.
 * @return The width in pixels.
 */
static ULONG chars(EditorApp *app, ULONG n)
{
    LONG w = TextLength(&app->screen->RastPort, (STRPTR)"n", 1);
    return n * (ULONG)w;
}

/**
 * @brief Height of a number of text lines of the screen font.
 * @param app The application state.
 * @param n The number of lines.
 * @return The height in pixels.
 */
static ULONG lines(EditorApp *app, ULONG n)
{
    return n * (ULONG)(app->screen->RastPort.TxHeight + 1U);
}

/**
 * @brief Drop the input of a window that waits under a modal one.
 * @param winobj The window (window.class).
 */
static void drop_input(Object *winobj)
{
    WORD code;
    while (DoMethod(winobj, WM_HANDLEINPUT, &code) != WMHI_LASTMSG) ;
}

/* ---- the command window -------------------------------------------- */

/** Objects of the open command window. */
static struct {
    Object *winobj, *name, *command, *dir, *isdefault;
    struct Window *win;
} pe;

/**
 * @brief Let the user choose the drawer of a command with the ASL
 *        requester; a drawer in the project goes in relative to it.
 */
static void choose_drawer(void)
{
    char start[EDITOR_PATH_MAX], chosen[EDITOR_PATH_MAX];
    const char *dir = (const char *)get(pe.dir, STRINGA_TextVal);
    struct FileRequester *fr;
    size_t n;

    strcpy(start, pj.root);
    if (dir != NULL && dir[0] != '\0' && !AddPart((STRPTR)start, (STRPTR)dir, sizeof(start)))
        strcpy(start, pj.root);
    fr = AllocAslRequestTags(ASL_FileRequest, ASLFR_TitleText, (ULONG)S(MSG_PJE_ASL),
                             ASLFR_DrawersOnly, TRUE, TAG_END);
    if (fr == NULL) return;
    if (AslRequestTags(fr, ASLFR_Window, (ULONG)pe.win, ASLFR_SleepWindow, TRUE,
                       ASLFR_InitialDrawer, (ULONG)start, TAG_END)) {
        strncpy(chosen, fr->fr_Drawer, sizeof(chosen) - 1);
        chosen[sizeof(chosen) - 1] = '\0';
        n = strlen(pj.root);
        if (n > 0 && Strnicmp((STRPTR)chosen, (STRPTR)pj.root, (LONG)n) == 0) {
            if (chosen[n] == '\0') chosen[0] = '\0';
            else if (pj.root[n - 1] == ':') memmove(chosen, chosen + n, strlen(chosen + n) + 1);
            else if (chosen[n] == '/') memmove(chosen, chosen + n + 1, strlen(chosen + n + 1) + 1);
        }
        SetGadgetAttrs((struct Gadget *)pe.dir, pe.win, NULL, STRINGA_TextVal, (ULONG)chosen, TAG_END);
    }
    FreeAslRequest(fr);
}

/**
 * @brief Take the command window's values into the settings.
 * @param index The command changed, or -1 for a new one.
 * @return 1 if taken over, 0 if the window stays open (a message was
 *         shown).
 */
static int take_command(long index)
{
    char *text = (char *)DoMethod(pe.command, GM_TEXTEDITOR_ExportText, NULL);
    int r = projconf_set(&pj.conf, (ProjAction)pj.action, index,
                         (const char *)get(pe.name, STRINGA_TextVal),
                         text != NULL ? text : "",
                         (const char *)get(pe.dir, STRINGA_TextVal),
                         get(pe.isdefault, CHECKBOX_Checked) != 0);
    if (text != NULL) FreeVec(text);
    if (r > 0) return 1;
    message(pe.win, S(r == 0 ? MSG_PJE_NOCOMMAND : MSG_PJ_NOMEM));
    return 0;
}

/**
 * @brief The command window: add or change a command of the action shown.
 * @param index The command, or -1 to add one.
 * @return Non-zero if the settings were changed.
 */
static int edit_command(long index)
{
    EditorApp *app = pj.app;
    const ProjCommand *cmd = index >= 0 ? &pj.conf.commands[pj.action][index] : NULL;
    char *text = cmd != NULL ? projconf_command_text(cmd) : NULL;
    ULONG sig = 0, pjsig = 0, mainsig = 0, result;
    WORD code;
    int done = 0, changed = 0;
    Object *layout;

    memset(&pe, 0, sizeof(pe));
    if (cmd != NULL && text == NULL) {
        message(pj.win, S(MSG_PJ_NOMEM));
        return 0;
    }
    pe.name = string(PE_NAME, cmd != NULL && strcmp(cmd->name, cmd->lines[0]) != 0 ? cmd->name : "", 128);
    pe.command = NewObject(TEXTEDITOR_GetClass(), NULL,
        GA_ID, PE_COMMAND,
        GA_TEXTEDITOR_Contents, (ULONG)(text != NULL ? text : ""),
        GA_TEXTEDITOR_FixedFont, TRUE,
        GA_TEXTEDITOR_WrapBorder, 0,
        GA_TEXTEDITOR_HorizontalScroll, TRUE,
        TAG_END);
    free(text);
    pe.dir = string(PE_DIR, cmd != NULL && cmd->dir != NULL ? cmd->dir : "", EDITOR_PATH_MAX);
    pe.isdefault = NewObject(CHECKBOX_GetClass(), NULL,
        GA_ID, PE_DEFAULT, GA_RelVerify, TRUE, GA_Text, (ULONG)S(MSG_PJE_DEFAULT),
        CHECKBOX_Checked, (ULONG)(cmd != NULL && cmd->is_default), TAG_END);

    layout = NewObject(LAYOUT_GetClass(), NULL,
        LAYOUT_Orientation, LAYOUT_ORIENT_VERT,
        LAYOUT_SpaceOuter, TRUE,
        LAYOUT_LeftSpacing, AREA_SPACING,
        LAYOUT_RightSpacing, AREA_SPACING,
        LAYOUT_TopSpacing, AREA_SPACING,
        LAYOUT_BottomSpacing, AREA_SPACING,
        LAYOUT_InnerSpacing, AREA_SPACING,
        LAYOUT_DeferLayout, TRUE,
        LAYOUT_AddChild, (ULONG)NewObject(LAYOUT_GetClass(), NULL,
            LAYOUT_Orientation, LAYOUT_ORIENT_VERT,
            LAYOUT_InnerSpacing, ROW_SPACING,
            LAYOUT_AddChild, (ULONG)pe.name, FIXED,
            CHILD_Label, (ULONG)label(S(MSG_PJE_NAME)),
            LAYOUT_AddChild, (ULONG)pe.command,
            CHILD_MinWidth, chars(app, 50),
            CHILD_MinHeight, lines(app, 5) + 8,
            CHILD_Label, (ULONG)label(S(MSG_PJE_COMMAND)),
            LAYOUT_AddChild, (ULONG)NewObject(LAYOUT_GetClass(), NULL,
                LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ,
                LAYOUT_AddChild, (ULONG)pe.dir,
                LAYOUT_AddChild, (ULONG)button(PE_CHOOSE, S(MSG_PJE_CHOOSE)),
                CHILD_WeightedWidth, 0,
                TAG_END),
            FIXED,
            CHILD_Label, (ULONG)label(S(MSG_PJE_DIR)),
            LAYOUT_AddChild, (ULONG)pe.isdefault, FIXED,
            LAYOUT_AddImage, (ULONG)label(S(MSG_PJE_NOTE_LINES)), FIXED,
            LAYOUT_AddImage, (ULONG)label(S(MSG_PJE_NOTE_PLACEHOLDERS)), FIXED,
            LAYOUT_AddImage, (ULONG)label(S(MSG_PJE_NOTE_DIR)), FIXED,
            TAG_END),
        LAYOUT_AddChild, (ULONG)NewObject(LAYOUT_GetClass(), NULL,
            LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ,
            LAYOUT_AddChild, (ULONG)button(PE_OK, S(MSG_PJE_OK)),
            CHILD_WeightedWidth, 0,
            LAYOUT_AddChild, (ULONG)filler(),
            LAYOUT_AddChild, (ULONG)button(PE_CANCEL, S(MSG_PJE_CANCEL)),
            CHILD_WeightedWidth, 0,
            TAG_END),
        CHILD_WeightedHeight, 0,
        TAG_END);
    if (layout == NULL) return 0;

    pe.winobj = NewObject(WINDOW_GetClass(), NULL,
        WA_Title, (ULONG)S(index >= 0 ? MSG_PJE_TITLE_EDIT : MSG_PJE_TITLE_ADD),
        WA_PubScreen, (ULONG)app->screen,
        WA_Activate, TRUE,
        WA_DepthGadget, TRUE,
        WA_DragBar, TRUE,
        WA_CloseGadget, TRUE,
        WA_SizeGadget, TRUE,
        WINDOW_RefWindow, (ULONG)pj.win,
        WINDOW_Position, WPOS_CENTERWINDOW,
        WINDOW_ParentGroup, (ULONG)layout,
        TAG_END);
    if (pe.winobj == NULL) {
        DisposeObject(layout);
        return 0;
    }
    if ((pe.win = (struct Window *)DoMethod(pe.winobj, WM_OPEN, NULL)) == NULL) {
        DisposeObject(pe.winobj);
        return 0;
    }
    ActivateLayoutGadget((struct Gadget *)layout, pe.win, NULL, (ULONG)(cmd != NULL ? pe.command : pe.name));

    SetAttrs(pj.winobj, WA_BusyPointer, TRUE, TAG_END);
    GetAttr(WINDOW_SigMask, app->window_object, &mainsig);
    GetAttr(WINDOW_SigMask, pj.winobj, &pjsig);
    GetAttr(WINDOW_SigMask, pe.winobj, &sig);
    while (!done) {
        ULONG got = Wait(sig | pjsig | mainsig);
        if ((got & mainsig) != 0) drop_input(app->window_object);
        if ((got & pjsig) != 0) drop_input(pj.winobj);
        while ((result = DoMethod(pe.winobj, WM_HANDLEINPUT, &code)) != WMHI_LASTMSG) {
            if ((result & WMHI_CLASSMASK) == WMHI_CLOSEWINDOW) { done = 1; continue; }
            if ((result & WMHI_CLASSMASK) != WMHI_GADGETUP) continue;
            switch (result & WMHI_GADGETMASK) {
                case PE_CHOOSE: choose_drawer(); break;
                case PE_OK: if (take_command(index)) done = changed = 1; break;
                case PE_CANCEL: done = 1; break;
            }
        }
    }
    SetAttrs(pj.winobj, WA_BusyPointer, FALSE, TAG_END);
    DisposeObject(pe.winobj);                   /* the gadgets with it */
    memset(&pe, 0, sizeof(pe));
    return changed;
}

/* ---- the project window ------------------------------------------------ */

/**
 * @brief Return the command selected in the list shown.
 * @return Its index, or -1 if none is selected.
 */
static long selected(void)
{
    LONG sel = (LONG)get(pj.lists[pj.action], LISTBROWSER_Selected);
    long count = pj.action == PAGE_INCLUDE ? pj.conf.include_count : pj.conf.count[pj.action];
    return sel >= 0 && sel < count ? (long)sel : -1;
}

/**
 * @brief Ghost the buttons that would do nothing for the selection.
 */
static void update_buttons(void)
{
    long sel = selected();
    long count = pj.action == PAGE_INCLUDE ? pj.conf.include_count : pj.conf.count[pj.action];
    struct { Object *o; int off; } b[] = {
        { pj.edit, sel < 0 }, { pj.remove, sel < 0 },
        { pj.up, sel <= 0 }, { pj.down, sel < 0 || sel >= count - 1 },
    };
    ULONG i;
    for (i = 0; i < sizeof(b) / sizeof(b[0]); ++i)
        if ((get(b[i].o, GA_Disabled) != 0) != b[i].off)
            SetGadgetAttrs((struct Gadget *)b[i].o, pj.win, NULL, GA_Disabled, (ULONG)b[i].off, TAG_END);
}

/**
 * @brief Build the nodes of a page's list from the settings.
 * @param a The action, or PAGE_INCLUDE.
 */
static void make_nodes(int a)
{
    long i;
    NewList(&pj.nodes[a]);
    if (a == PAGE_INCLUDE) {
        for (i = 0; i < pj.conf.include_count; ++i) {
            struct Node *n = AllocListBrowserNode(1, LBNCA_CopyText, TRUE,
                                                  LBNCA_Text, (ULONG)pj.conf.include[i], TAG_END);
            if (n != NULL) AddTail(&pj.nodes[a], n);
        }
        return;
    }
    for (i = 0; i < pj.conf.count[a]; ++i) {
        const ProjCommand *cmd = &pj.conf.commands[a][i];
        char shown[160];
        struct Node *n;
        /* the first line; "..." if there are more */
        snprintf(shown, sizeof(shown), cmd->line_count > 1 ? "%s ..." : "%s", cmd->lines[0]);
        n = AllocListBrowserNode(2,
            LBNA_CheckBox, TRUE, LBNA_Checked, (ULONG)cmd->is_default,
            LBNA_Column, 0, LBNCA_CopyText, TRUE, LBNCA_Text, (ULONG)cmd->name,
            LBNA_Column, 1, LBNCA_CopyText, TRUE, LBNCA_Text, (ULONG)shown,
            TAG_END);
        if (n != NULL) AddTail(&pj.nodes[a], n);
    }
}

/**
 * @brief Show a page's entries anew in its list.
 * @param a The action, or PAGE_INCLUDE.
 * @param sel The entry to select, or -1.
 */
static void refill(int a, long sel)
{
    SetPageGadgetAttrs((struct Gadget *)pj.lists[a], pj.page, pj.win, NULL,
                       LISTBROWSER_Labels, ~0UL, TAG_END);
    FreeListBrowserList(&pj.nodes[a]);
    make_nodes(a);
    SetPageGadgetAttrs((struct Gadget *)pj.lists[a], pj.page, pj.win, NULL,
        LISTBROWSER_Labels, (ULONG)&pj.nodes[a],
        LISTBROWSER_Selected, (ULONG)(LONG)sel,
        sel >= 0 ? LISTBROWSER_MakeVisible : TAG_IGNORE, (ULONG)sel,
        TAG_END);
    if (a == pj.action) update_buttons();
}

/**
 * @brief Let the user choose an include drawer with the ASL requester; a
 *        drawer in the project goes in relative to it.
 * @param index The drawer to change, or -1 to add one.
 * @return Non-zero if the settings changed.
 */
static int choose_include(long index)
{
    char start[EDITOR_PATH_MAX], chosen[EDITOR_PATH_MAX];
    struct FileRequester *fr;
    size_t n;
    int r = 0;
    char *full = index >= 0 ? projconf_include_full(&pj.conf, index, pj.root) : NULL;
    strcpy(start, full != NULL && strlen(full) < sizeof(start) ? full : pj.root);
    free(full);
    fr = AllocAslRequestTags(ASL_FileRequest, ASLFR_TitleText, (ULONG)S(MSG_PJ_ASL_INCLUDE),
                             ASLFR_DrawersOnly, TRUE, TAG_END);
    if (fr == NULL) return 0;
    if (AslRequestTags(fr, ASLFR_Window, (ULONG)pj.win, ASLFR_SleepWindow, TRUE,
                       ASLFR_InitialDrawer, (ULONG)start, TAG_END) && fr->fr_Drawer[0] != '\0') {
        strncpy(chosen, fr->fr_Drawer, sizeof(chosen) - 1);
        chosen[sizeof(chosen) - 1] = '\0';
        n = strlen(pj.root);
        if (n > 0 && Strnicmp((STRPTR)chosen, (STRPTR)pj.root, (LONG)n) == 0 &&
            (pj.root[n - 1] == ':' || chosen[n] == '/') && chosen[n] != '\0') {
            memmove(chosen, chosen + n + (pj.root[n - 1] == ':' ? 0 : 1),
                    strlen(chosen + n + (pj.root[n - 1] == ':' ? 0 : 1)) + 1);
        }
        r = projconf_include_set(&pj.conf, index, chosen);
        if (r < 0) message(pj.win, S(MSG_PJ_NOMEM));
    }
    FreeAslRequest(fr);
    return r > 0;
}

/**
 * @brief Width of the category list: its longest entry plus margins.
 * @param app The application state.
 * @return The width in pixels.
 */
static ULONG list_width(EditorApp *app)
{
    ULONG w = 0, t;
    int a;
    for (a = 0; a < PAGES; ++a) {
        t = (ULONG)TextLength(&app->screen->RastPort, (STRPTR)S(categories[a]),
                              (UWORD)strlen(S(categories[a])));
        if (t > w) w = t;
    }
    return w + 24;
}

/**
 * @brief Build the project window's contents.
 * @param app The application state.
 * @return The root layout, or NULL.
 */
static Object *build(EditorApp *app)
{
    Object *pages[PAGES];
    int a;

    NewList(&pj.catlist);
    for (a = 0; a < PAGES; ++a) {
        struct Node *n = AllocListBrowserNode(1, LBNCA_Text, (ULONG)S(categories[a]), TAG_END);
        if (n != NULL) AddTail(&pj.catlist, n);
    }
    pj.cats = NewObject(LISTBROWSER_GetClass(), NULL,
        GA_ID, PJ_CATS, GA_RelVerify, TRUE,
        LISTBROWSER_Labels, (ULONG)&pj.catlist,
        LISTBROWSER_ShowSelected, TRUE,
        LISTBROWSER_Selected, 0,
        LISTBROWSER_AutoFit, TRUE,
        LISTBROWSER_VerticalProp, FALSE,
        TAG_END);

    for (a = 0; a < PAGES; ++a) {
        struct ColumnInfo *ci = pj.columns[a];
        if (a == PAGE_INCLUDE) {
            ci[0].ci_Width = 100;
            ci[0].ci_Title = (STRPTR)S(MSG_PJ_COL_DRAWER);
            ci[1].ci_Width = -1;
            ci[1].ci_Title = (STRPTR)~0;
            ci[1].ci_Flags = (ULONG)-1;
        } else {
            ci[0].ci_Width = 35;
            ci[0].ci_Title = (STRPTR)S(MSG_PJ_COL_NAME);
            ci[1].ci_Width = 65;
            ci[1].ci_Title = (STRPTR)S(MSG_PJ_COL_COMMAND);
            ci[2].ci_Width = -1;
            ci[2].ci_Title = (STRPTR)~0;
            ci[2].ci_Flags = (ULONG)-1;
        }
        make_nodes(a);
        pj.lists[a] = NewObject(LISTBROWSER_GetClass(), NULL,
            GA_ID, (ULONG)(PJ_LIST + a), GA_RelVerify, TRUE,
            LISTBROWSER_ColumnInfo, (ULONG)ci,
            LISTBROWSER_ColumnTitles, TRUE,
            LISTBROWSER_Labels, (ULONG)&pj.nodes[a],
            LISTBROWSER_ShowSelected, TRUE,
            LISTBROWSER_Selected, (ULONG)-1,
            TAG_END);
        pages[a] = NewObject(LAYOUT_GetClass(), NULL,
            LAYOUT_Orientation, LAYOUT_ORIENT_VERT,
            LAYOUT_BevelStyle, BVS_GROUP,
            LAYOUT_Label, (ULONG)S(categories[a]),
            LAYOUT_SpaceOuter, TRUE,
            LAYOUT_LeftSpacing, FRAME_SIDE, LAYOUT_RightSpacing, FRAME_SIDE,
            LAYOUT_TopSpacing, FRAME_TOPBOT, LAYOUT_BottomSpacing, FRAME_TOPBOT,
            LAYOUT_AddChild, (ULONG)pj.lists[a],
            CHILD_MinWidth, chars(app, 46),
            CHILD_MinHeight, lines(app, 8),
            TAG_END);
    }
    pj.page = NewObject(PAGE_GetClass(), NULL,
        PAGE_Add, (ULONG)pages[0],
        PAGE_Add, (ULONG)pages[1],
        PAGE_Add, (ULONG)pages[2],
        PAGE_Add, (ULONG)pages[3],
        PAGE_Current, 0,
        TAG_END);

    pj.name = string(PJ_NAME, pj.conf.name != NULL ? pj.conf.name : "", 80);
    pj.add = button(PJ_ADD, S(MSG_PJ_ADD));
    pj.edit = button(PJ_EDIT, S(MSG_PJ_EDIT));
    pj.remove = button(PJ_REMOVE, S(MSG_PJ_REMOVE));
    pj.up = button(PJ_UP, S(MSG_PJ_UP));
    pj.down = button(PJ_DOWN, S(MSG_PJ_DOWN));

    return NewObject(LAYOUT_GetClass(), NULL,
        LAYOUT_Orientation, LAYOUT_ORIENT_VERT,
        LAYOUT_SpaceOuter, TRUE,
        LAYOUT_LeftSpacing, AREA_SPACING,
        LAYOUT_RightSpacing, AREA_SPACING,
        LAYOUT_TopSpacing, AREA_SPACING,
        LAYOUT_BottomSpacing, AREA_SPACING,
        LAYOUT_InnerSpacing, AREA_SPACING,
        LAYOUT_DeferLayout, TRUE,
        LAYOUT_AddChild, (ULONG)pj.name, FIXED,
        CHILD_Label, (ULONG)label(S(MSG_PJ_NAME)),
        LAYOUT_AddChild, (ULONG)NewObject(LAYOUT_GetClass(), NULL,
            LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ,
            LAYOUT_InnerSpacing, AREA_SPACING,
            LAYOUT_AddChild, (ULONG)pj.cats,
            CHILD_WeightedWidth, 20,
            CHILD_MinWidth, list_width(app),
            LAYOUT_AddChild, (ULONG)pj.page,
            CHILD_WeightedWidth, 80,
            /* the buttons act on the page shown; a column of equal width */
            LAYOUT_AddChild, (ULONG)NewObject(LAYOUT_GetClass(), NULL,
                LAYOUT_Orientation, LAYOUT_ORIENT_VERT,
                LAYOUT_InnerSpacing, ROW_SPACING,
                LAYOUT_TopSpacing, FRAME_TOPBOT,
                LAYOUT_AddChild, (ULONG)pj.add, FIXED,
                LAYOUT_AddChild, (ULONG)pj.edit, FIXED,
                LAYOUT_AddChild, (ULONG)pj.remove, FIXED,
                LAYOUT_AddChild, (ULONG)pj.up, FIXED,
                LAYOUT_AddChild, (ULONG)pj.down, FIXED,
                LAYOUT_AddChild, (ULONG)filler(),
                TAG_END),
            CHILD_WeightedWidth, 0,
            TAG_END),
        LAYOUT_AddImage, (ULONG)label(S(MSG_PJ_DEFAULTNOTE)),
        FIXED,
        LAYOUT_AddChild, (ULONG)NewObject(LAYOUT_GetClass(), NULL,
            LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ,
            LAYOUT_AddChild, (ULONG)button(PJ_SAVE, S(MSG_PREFS_SAVE)),
            CHILD_WeightedWidth, 0,
            LAYOUT_AddChild, (ULONG)filler(),
            LAYOUT_AddChild, (ULONG)button(PJ_CANCEL, S(MSG_PREFS_CANCEL)),
            CHILD_WeightedWidth, 0,
            TAG_END),
        FIXED,
        TAG_END);
}

/**
 * @brief Write the settings into the project file: read it again and
 *        replace the lists of the actions in it.
 * @return Non-zero if written (otherwise a message was shown).
 */
static int save(void)
{
    char *old = NULL, *json;
    int r = projdata_read(pj.root, PROJECT_FILE, &old), ok;
    if (r > 0) {
        ProjConf test;
        projconf_init(&test);
        r = projconf_parse(&test, old);
        projconf_clear(&test);
        if (r < 0) {
            free(old);
            message(pj.win, S(MSG_PJ_NOMEM));
            return 0;
        }
    }
    if (r < 0 || (old != NULL && r == 0)) {
        free(old);
        file_message(pj.win, MSG_PJ_READ);
        return 0;
    }
    json = projconf_format(&pj.conf, old);
    free(old);
    ok = json != NULL && projdata_write(pj.root, PROJECT_FILE, json);
    free(json);
    if (!ok) file_message(pj.win, MSG_PJ_WRITE);
    return ok;
}

/**
 * @brief Handle a click into an action's list: the checkbox sets or
 *        clears the default, a double click edits the command.
 */
static void list_event(void)
{
    ULONG event = get(pj.lists[pj.action], LISTBROWSER_RelEvent);
    long sel = selected(), i = 0;
    ProjAction a = (ProjAction)pj.action;
    struct Node *n;

    if (pj.action == PAGE_INCLUDE) {
        if (sel >= 0 && (event & LBRE_DOUBLECLICK) != 0 && choose_include(sel)) refill(PAGE_INCLUDE, sel);
        else update_buttons();
        return;
    }
    if ((event & (LBRE_CHECKED | LBRE_UNCHECKED)) != 0) {
        /* compare the checkboxes with the settings rather than rely on the
         * selection: the newly checked one becomes the default */
        long checked = -1;
        for (n = pj.nodes[a].lh_Head; n->ln_Succ != NULL && i < pj.conf.count[a]; n = n->ln_Succ, ++i) {
            ULONG on = 0;
            GetListBrowserNodeAttrs(n, LBNA_Checked, (ULONG)&on, TAG_END);
            if (on && !pj.conf.commands[a][i].is_default) checked = i;
            else if (!on) pj.conf.commands[a][i].is_default = 0;
        }
        if (checked >= 0) {
            projconf_set_default(&pj.conf, a, checked);
            refill(a, checked);
        } else update_buttons();
    } else if (sel >= 0 && (event & LBRE_DOUBLECLICK) != 0) {
        if (edit_command(sel)) refill(a, sel);
    } else update_buttons();
}

void project_settings(EditorApp *app)
{
    ULONG sig = 0, mainsig = 0, result;
    WORD code = 0;
    char *text = NULL;
    const char *name;
    int done = 0, r, a;
    long sel;

    if (app->window == NULL) return;
    memset(&pj, 0, sizeof(pj));
    pj.app = app;
    projconf_init(&pj.conf);
    if (!tree_project_root(app, pj.root, sizeof(pj.root))) {
        ui_error(app, S(MSG_PJ_WINDOW), S(MSG_PJ_NOPROJECT));
        return;
    }
    /* no file yet: empty settings; one that cannot be read stays as it is */
    r = projdata_read(pj.root, PROJECT_FILE, &text);
    if (r > 0) {
        r = projconf_parse(&pj.conf, text);
        free(text);
        if (r < 0) {
            ui_error(app, S(MSG_PJ_WINDOW), S(MSG_PJ_NOMEM));
            return;
        }
        if (r == 0) r = -1;
    }
    if (r < 0) {
        file_message(NULL, MSG_PJ_READ);
        return;
    }
    if (!open_classes(app) || (pj.layout = build(app)) == NULL) goto out;

    name = (const char *)FilePart((STRPTR)pj.root);
    snprintf(pj.title, sizeof(pj.title), S(MSG_PJ_TITLE), name[0] != '\0' ? name : pj.root);
    pj.winobj = NewObject(WINDOW_GetClass(), NULL,
        WA_Title, (ULONG)pj.title,
        WA_PubScreen, (ULONG)app->screen,
        WA_Activate, TRUE,
        WA_DepthGadget, TRUE,
        WA_DragBar, TRUE,
        WA_CloseGadget, TRUE,
        WA_SizeGadget, TRUE,
        WINDOW_RefWindow, (ULONG)app->window,
        WINDOW_Position, WPOS_CENTERWINDOW,
        WINDOW_ParentGroup, (ULONG)pj.layout,
        TAG_END);
    if (pj.winobj == NULL) {
        DisposeObject(pj.layout);
        goto out;
    }
    if ((pj.win = (struct Window *)DoMethod(pj.winobj, WM_OPEN, NULL)) == NULL) goto out;
    update_buttons();

    SetAttrs(app->window_object, WA_BusyPointer, TRUE, TAG_END);
    GetAttr(WINDOW_SigMask, app->window_object, &mainsig);
    GetAttr(WINDOW_SigMask, pj.winobj, &sig);
    while (!done) {
        ULONG got = Wait(sig | mainsig);
        if ((got & mainsig) != 0) drop_input(app->window_object);
        while ((result = DoMethod(pj.winobj, WM_HANDLEINPUT, &code)) != WMHI_LASTMSG) {
            ULONG id = result & WMHI_GADGETMASK;
            ProjAction act = (ProjAction)pj.action;
            if ((result & WMHI_CLASSMASK) == WMHI_CLOSEWINDOW) { done = 1; continue; }
            if ((result & WMHI_CLASSMASK) != WMHI_GADGETUP) continue;
            sel = selected();
            switch (id) {
                case PJ_CATS:
                    a = (int)(LONG)get(pj.cats, LISTBROWSER_Selected);
                    if (a < 0 || a >= PAGES) break;
                    pj.action = a;
                    SetGadgetAttrs((struct Gadget *)pj.page, pj.win, NULL, PAGE_Current, (ULONG)a, TAG_END);
                    RethinkLayout((struct Gadget *)pj.layout, pj.win, NULL, TRUE);
                    update_buttons();
                    break;
                case PJ_ADD:
                    if (pj.action == PAGE_INCLUDE) {
                        if (choose_include(-1)) refill(PAGE_INCLUDE, pj.conf.include_count - 1);
                    } else if (edit_command(-1)) refill(act, pj.conf.count[act] - 1);
                    break;
                case PJ_EDIT:
                    if (sel < 0) break;
                    if (pj.action == PAGE_INCLUDE) {
                        if (choose_include(sel)) refill(PAGE_INCLUDE, sel);
                    } else if (edit_command(sel)) refill(act, sel);
                    break;
                case PJ_REMOVE:
                    if (sel < 0) break;
                    if (pj.action == PAGE_INCLUDE) {
                        projconf_include_remove(&pj.conf, sel);
                        refill(PAGE_INCLUDE, sel < pj.conf.include_count ? sel : pj.conf.include_count - 1);
                        break;
                    }
                    projconf_remove(&pj.conf, act, sel);
                    refill(act, sel < pj.conf.count[act] ? sel : pj.conf.count[act] - 1);
                    break;
                case PJ_UP:
                case PJ_DOWN:
                    if (sel < 0) break;
                    if (pj.action == PAGE_INCLUDE)
                        refill(PAGE_INCLUDE, projconf_include_move(&pj.conf, sel, id == PJ_UP ? -1 : 1));
                    else refill(act, projconf_move(&pj.conf, act, sel, id == PJ_UP ? -1 : 1));
                    break;
                case PJ_SAVE:
                    if (projconf_set_name(&pj.conf, (const char *)get(pj.name, STRINGA_TextVal)) < 0) {
                        message(pj.win, S(MSG_PJ_NOMEM));
                        break;
                    }
                    if (save()) {
                        done = 1;
                        scoutlink_project_opened(app);     /* the include drawers count */
                    }
                    break;
                case PJ_CANCEL:
                    done = 1;
                    break;
                default:
                    if (id == PJ_LIST + (ULONG)pj.action) list_event();
                    break;
            }
        }
    }
    SetAttrs(app->window_object, WA_BusyPointer, FALSE, TAG_END);

out:
    if (pj.winobj != NULL) DisposeObject(pj.winobj);         /* the gadgets with it */
    if (pj.catlist.lh_Head != NULL) FreeListBrowserList(&pj.catlist);
    for (a = 0; a < PAGES; ++a)
        if (pj.nodes[a].lh_Head != NULL) FreeListBrowserList(&pj.nodes[a]);
    projconf_clear(&pj.conf);
    memset(&pj, 0, sizeof(pj));
}
