/**
 * @file gitwin.c
 * @brief The windows of git through am-git: Git/Commit... (the changed
 *        files, staging, the message) and the changes of a file (diff).
 *
 * Both are modal, built like the project settings: the windows below show
 * the busy pointer and their input is dropped. am-git runs synchronously
 * (git_run(), busy pointer).
 *
 * The commit window lists the files of "am-git status --porcelain" with a
 * check box each: checked (shown as "selected") means staged. A click on
 * a check box stages the file at once (am-git add; for a deleted file
 * am-git rm --cached, as add does not stage a deletion) or takes it back
 * (am-git unstage, which puts back the entry of HEAD; rm --cached would
 * stage a deletion), then the status is read again, so the list
 * always shows what am-git made of it. listbrowser.gadget toggles the box
 * itself and need not select the node (see projwin.c): after each
 * GADGETUP the boxes are compared with the status. "Commit" runs
 * am-git commit -m with the message, a single line (am-git takes no line
 * breaks, see TECHNICAL.md). Documents not saved are not part of it.
 *
 * The clone window (Git/Clone Repository...) asks for the remote's
 * address, the drawer the repository goes into (the setting "projectdir",
 * else the drawer above the current project) and the new drawer's name,
 * suggested from the address ("repo" of ".../repo.git") until the name is
 * typed; git_clone() then runs am-git clone in the background.
 *
 * The conflict window (Git/Resolve Conflicts...) lists the files that had
 * a conflict when it opened, each "conflict" or "resolved". "Mark as
 * Resolved" runs am-git add on the file (it clears the conflict stages of
 * the index), after asking if the file still holds conflict markers;
 * "Open" (or a double click) goes to the first marker in the editor and
 * closes the window; "Commit..." goes on to the commit window, whose
 * message is then the first line of .git/MERGE_MSG.
 *
 * The diff window shows a file's changes since the last commit: the
 * file of HEAD from am-git show against the file on disk, compared by
 * linediff.c (am-git's own diff reads every working file as empty on the
 * Amiga, see linediff.h). Staged and not staged changes are not told
 * apart: am-git cannot print a file of the index. One line per row of a
 * listbrowser in the system default text font, coloured with the pens of
 * the git states (added like untracked, removed like conflicts, hunks
 * like staged).
 */
#include "editor.h"
#include "gitstatus.h"
#include "linediff.h"

#include <classes/window.h>
#include <gadgets/button.h>
#include <gadgets/layout.h>
#include <gadgets/listbrowser.h>
#include <gadgets/string.h>
#include <images/label.h>
#include <intuition/gadgetclass.h>
#include <graphics/gfxbase.h>
#include <libraries/asl.h>
#include <proto/asl.h>
#include <proto/button.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/label.h>
#include <proto/layout.h>
#include <proto/listbrowser.h>
#include <proto/string.h>
#include <proto/window.h>
#include <clib/alib_protos.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern struct Library *ListBrowserBase;    /* shared with the settings window */
extern struct Library *StringBase;         /* shared with the find window */

/** Most lines the diff window shows. */
#define DIFF_MAX_LINES 3000
/** Width of a tab in the diff window. */
#define DIFF_TAB 8
/** Longest commit message (characters). */
#define MESSAGE_MAX 200

/** Gadget IDs. */
enum {
    GC_LIST = 100, GC_ADDALL, GC_DIFF, GC_MESSAGE, GC_COMMIT, GC_CLOSE,
    GK_URL, GK_DRAWER, GK_CHOOSE, GK_NAME, GK_CLONE, GK_CANCEL,
    GR_LIST, GR_RESOLVE, GR_OPEN, GR_COMMIT, GR_CLOSE,
    GD_LIST, GD_CLOSE
};

/** @name Spacing, in "virtual pixels", as in the settings window */
/** @{ */
#define AREA_SPACING 6              /**< window border, list, buttons */
#define ROW_SPACING  4              /**< between rows */
/** @} */

/** the child just added keeps its height */
#define FIXED       CHILD_WeightedHeight, 0

/** The open commit window. */
static struct {
    EditorApp *app;
    char root[EDITOR_PATH_MAX];     /* the repository */
    GitStatus status;               /* what the list shows */
    struct List nodes;
    struct ColumnInfo cols[3];
    Object *winobj, *layout, *list, *message;
    struct Window *win;
    int merging;                    /* .git/MERGE_HEAD is there */
} gc;

/* ---- helpers --------------------------------------------------------- */

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

/** @brief An empty group that takes the remaining space. @return It. */
static Object *filler(void)
{
    return NewObject(LAYOUT_GetClass(), NULL, TAG_END);
}

/**
 * @brief Drop the input of a window that waits under a modal one.
 * @param winobj The window (window.class), or NULL.
 */
static void drop_input(Object *winobj)
{
    WORD code;
    if (winobj != NULL)
        while (DoMethod(winobj, WM_HANDLEINPUT, &code) != WMHI_LASTMSG) ;
}

/**
 * @brief Show a message over a window.
 * @param win The window.
 * @param text The message.
 */
static void message(struct Window *win, const char *text)
{
    struct EasyStruct es = {sizeof(es), 0, (STRPTR)S(MSG_GIT_TITLE), (STRPTR)"%s", (STRPTR)S(MSG_OK)};
    ULONG args[1];
    args[0] = (ULONG)text;
    EasyRequestArgs(win, &es, NULL, args);
}

/**
 * @brief Open the classes the windows need.
 * @param app The application state (for the message).
 * @return Non-zero if they are there.
 */
static int open_classes(EditorApp *app)
{
    if (ListBrowserBase == NULL) ListBrowserBase = OpenLibrary((STRPTR)"gadgets/listbrowser.gadget", 44);
    if (StringBase == NULL) StringBase = OpenLibrary((STRPTR)"gadgets/string.gadget", 44);
    if (ListBrowserBase == NULL || StringBase == NULL) {
        char text[160];
        snprintf(text, sizeof(text), S(MSG_PREFS_NEEDCLASS),
                 ListBrowserBase == NULL ? "gadgets/listbrowser.gadget" : "gadgets/string.gadget");
        ui_error(app, S(MSG_GIT_TITLE), text);
        return 0;
    }
    return 1;
}

/**
 * @brief A modal window centred over another.
 * @param app The application state (screen).
 * @param title Its title.
 * @param ref The window below.
 * @param layout Its contents.
 * @return The window object (open), or NULL (the layout is freed then).
 */
static Object *open_window(EditorApp *app, const char *title, struct Window *ref, Object *layout)
{
    Object *winobj = NewObject(WINDOW_GetClass(), NULL,
        WA_Title, (ULONG)title,
        WA_PubScreen, (ULONG)app->screen,
        WA_SmartRefresh, TRUE,     /* scrolled lists: see "Scrolling..." in TECHNICAL.md */
        WA_Activate, TRUE,
        WA_DepthGadget, TRUE,
        WA_DragBar, TRUE,
        WA_CloseGadget, TRUE,
        WA_SizeGadget, TRUE,
        WINDOW_RefWindow, (ULONG)ref,
        WINDOW_Position, WPOS_CENTERWINDOW,
        WINDOW_ParentGroup, (ULONG)layout,
        TAG_END);
    if (winobj == NULL) {
        DisposeObject(layout);
        return NULL;
    }
    if (DoMethod(winobj, WM_OPEN, NULL) == 0) {
        DisposeObject(winobj);
        return NULL;
    }
    return winobj;
}

/** How many of these modal windows are open (the main window stays busy). */
static int modal_open;

/**
 * @brief git_run(), keeping the main window busy while a window of this
 *        module is open (git_run() takes the busy pointer away).
 * @param app The application state.
 * @param dir The drawer it runs in.
 * @param args am-git's arguments.
 * @param[out] output Its output (free() it), or NULL.
 * @return The return code, or -1 if it could not be run.
 */
static LONG run(EditorApp *app, const char *dir, const char *args, char **output)
{
    LONG rc = git_run(app, dir, args, output);
    if (modal_open > 0) SetAttrs(app->window_object, WA_BusyPointer, TRUE, TAG_END);
    return rc;
}

/* ---- the diff window --------------------------------------------------- */

/**
 * @brief Add a line to the diff window's list.
 * @param list The list.
 * @param line The line (need not be terminated after its end).
 * @param length Its length.
 * @param pen Its pen, -1 for the normal one.
 * @return Non-zero if added.
 */
static int add_line(struct List *list, const char *line, size_t length, LONG pen)
{
    char text[512];
    size_t i, n = 0;
    struct Node *node;
    for (i = 0; i < length && n + DIFF_TAB < sizeof(text); ++i) {
        if (line[i] == '\t') {
            do text[n++] = ' '; while (n % DIFF_TAB != 0);
        } else if (line[i] != '\r') {
            text[n++] = line[i];
        }
    }
    text[n] = '\0';
    node = AllocListBrowserNode(1, LBNCA_CopyText, TRUE, LBNCA_Text, (ULONG)text,
                                pen >= 0 ? LBNCA_FGPen : TAG_IGNORE, (ULONG)pen, TAG_END);
    if (node == NULL) return 0;
    AddTail(list, node);
    return 1;
}

/**
 * @brief Add a diff's lines to the list, coloured by their kind.
 * @param list The list.
 * @param text The diff.
 * @param[in,out] count The lines so far (DIFF_MAX_LINES at most).
 */
static void add_diff(struct List *list, const char *text, long *count)
{
    const char *line = text;
    while (*line != '\0' && *count < DIFF_MAX_LINES) {
        const char *nl = strchr(line, '\n');
        size_t len = nl != NULL ? (size_t)(nl - line) : strlen(line);
        LONG pen = -1;
        switch (gitdiff_line_kind(line, len, 0)) {
            case GIT_DIFF_ADDED: pen = git_pen(GIT_ST_UNTRACKED); break;
            case GIT_DIFF_REMOVED: pen = git_pen(GIT_ST_CONFLICT); break;
            case GIT_DIFF_HUNK: pen = git_pen(GIT_ST_STAGED); break;
        }
        if (!add_line(list, line, len, pen)) return;
        ++*count;
        line = nl != NULL ? nl + 1 : line + len;
    }
}

/**
 * @brief A file as it was in the last commit, from am-git.
 *
 * "show --text-blob HEAD:path" (am-git 0.12.3: a first line "text",
 * "binary" or "missing") is tried first; an older am-git answers it with
 * its usage, then "show HEAD:path" (0.12.1) is taken, which prints the
 * contents or nothing for a file not in the commit, and "show: ..." when
 * there is no commit yet.
 *
 * @param app The application state.
 * @param root The repository.
 * @param rel The file relative to the repository.
 * @param ref The window messages are shown over.
 * @param[out] text Its contents (free() it); "" for a file not in the
 *        last commit; NULL unless 1 is returned.
 * @return 1 on success, 0 if a message was shown (failed, binary).
 */
static int head_version(EditorApp *app, const char *root, const char *rel, struct Window *ref, char **text)
{
    char spec[EDITOR_PATH_MAX + 8], command[EDITOR_PATH_MAX + 40], *out = NULL;
    const char *body = NULL;
    LONG rc;
    int pass;
    *text = NULL;
    snprintf(spec, sizeof(spec), "HEAD:%s", rel);
    for (pass = 0; pass < 2 && body == NULL; ++pass) {
        size_t n;
        strcpy(command, pass == 0 ? "show --text-blob " : "show ");
        n = strlen(command);
        if (!git_quote(command + n, sizeof(command) - n, spec)) {
            message(ref, S(MSG_ERR_PATHTOOLONG));
            return 0;
        }
        free(out);
        out = NULL;
        rc = run(app, root, command, &out);
        if (rc == -1) {
            message(ref, S(MSG_GIT_NORUN));
            return 0;
        }
        if (rc != 0) {
            git_report(app, ref, MSG_GIT_FAILED, out);
            free(out);
            return 0;
        }
        if (out == NULL) {
            body = "";
        } else if (pass == 0) {
            if (strncmp(out, "text\n", 5) == 0) body = out + 5;
            else if (strncmp(out, "missing", 7) == 0) body = "";
            else if (strncmp(out, "binary", 6) == 0) {
                free(out);
                message(ref, S(MSG_GITDIFF_BINARY));
                return 0;
            } else if (strncmp(out, "show: cannot resolve", 20) == 0) body = "";    /* no commit yet */
            else if (strncmp(out, "show:", 5) == 0) {
                git_report(app, ref, MSG_GIT_FAILED, out);
                free(out);
                return 0;
            }
            /* otherwise an older am-git: its usage, try plain show */
        } else {
            body = strncmp(out, "show: cannot resolve", 20) == 0 ? "" : out;
        }
    }
    if (body == NULL) body = "";
    *text = malloc(strlen(body) + 1);
    if (*text != NULL) strcpy(*text, body);
    free(out);
    if (*text == NULL) message(ref, S(MSG_PJ_NOMEM));
    return *text != NULL;
}

/**
 * @brief Show a file's changes in a modal window.
 *
 * The changes since the last commit: the file of HEAD (head_version())
 * against the file on disk, compared with linediff_unified() (am-git's
 * diff reads the work tree as empty, see linediff.h).
 *
 * @param app The application state.
 * @param root The repository.
 * @param rel The file relative to the repository.
 * @param ref The window below (the main window or the commit window).
 * @param refobj Its object, if it is not the main window (its input is
 *        dropped too), or NULL.
 */
static void show_diff(EditorApp *app, const char *root, const char *rel, struct Window *ref, Object *refobj)
{
    char *head = NULL, *work = NULL, *hunks, path[EDITOR_PATH_MAX], title[EDITOR_PATH_MAX + 64];
    struct List lines;
    struct TextAttr font;
    struct TextFont *df = GfxBase->DefaultFont, *opened = NULL;
    Object *layout, *list, *winobj;
    ULONG sig = 0, refsig = 0, mainsig = 0, result;
    WORD code;
    long count = 0;
    int done = 0, rc;
    LONG w = TextLength(&app->screen->RastPort, (STRPTR)"n", 1);
    ULONG h = app->screen->RastPort.TxHeight + 1U;

    if (!head_version(app, root, rel, ref, &head)) return;
    strncpy(path, root, sizeof(path) - 1);
    path[sizeof(path) - 1] = '\0';
    if (!AddPart((STRPTR)path, (STRPTR)rel, sizeof(path)) || (rc = datafile_read(path, &work)) < 0) {
        free(head);
        message(ref, S(MSG_GITDIFF_NOREAD));
        return;
    }
    hunks = linediff_unified(head, strlen(head), rc > 0 ? work : "", rc > 0 ? strlen(work) : 0, 3);
    if (hunks == NULL) {
        message(ref, S(MSG_PJ_NOMEM));
    } else if (hunks[0] == '\0') {
        message(ref, S(MSG_GITDIFF_NONE));
    }
    if (hunks == NULL || hunks[0] == '\0') {
        free(head);
        free(work);
        free(hunks);
        return;
    }
    NewList(&lines);
    {
        /* a heading line saying what is compared */
        const char *what = head[0] == '\0' ? S(MSG_GITDIFF_NEW) : rc == 0 ? S(MSG_GITDIFF_DELETED) : S(MSG_GITDIFF_SINCE);
        add_line(&lines, what, strlen(what), -1);
    }
    add_diff(&lines, hunks, &count);
    if (count >= DIFF_MAX_LINES) add_line(&lines, "...", 3, -1);
    free(head);
    free(work);
    free(hunks);

    /* the system default text font, as in the output window: a diff's
       columns line up; opened while the window is */
    memset(&font, 0, sizeof(font));
    if (df != NULL && df->tf_Message.mn_Node.ln_Name != NULL) {
        font.ta_Name = df->tf_Message.mn_Node.ln_Name;
        font.ta_YSize = df->tf_YSize;
        font.ta_Style = df->tf_Style;
        font.ta_Flags = df->tf_Flags;
        opened = OpenFont(&font);
    }
    list = NewObject(LISTBROWSER_GetClass(), NULL,
        GA_ID, GD_LIST, GA_RelVerify, TRUE,
        opened != NULL ? GA_TextAttr : TAG_IGNORE, (ULONG)&font,
        LISTBROWSER_Labels, (ULONG)&lines,
        LISTBROWSER_ShowSelected, FALSE,
        LISTBROWSER_AutoWheel, TRUE,
        LISTBROWSER_HorizontalProp, TRUE,
        LISTBROWSER_AutoFit, TRUE,
        TAG_END);
    layout = NewObject(LAYOUT_GetClass(), NULL,
        LAYOUT_Orientation, LAYOUT_ORIENT_VERT,
        LAYOUT_SpaceOuter, TRUE,
        LAYOUT_LeftSpacing, AREA_SPACING,
        LAYOUT_RightSpacing, AREA_SPACING,
        LAYOUT_TopSpacing, AREA_SPACING,
        LAYOUT_BottomSpacing, AREA_SPACING,
        LAYOUT_InnerSpacing, AREA_SPACING,
        LAYOUT_DeferLayout, TRUE,
        LAYOUT_AddChild, (ULONG)list,
        CHILD_MinWidth, (ULONG)(70 * w),
        CHILD_MinHeight, 18 * h,
        LAYOUT_AddChild, (ULONG)NewObject(LAYOUT_GetClass(), NULL,
            LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ,
            LAYOUT_AddChild, (ULONG)filler(),
            LAYOUT_AddChild, (ULONG)button(GD_CLOSE, S(MSG_GITWIN_CLOSE)),
            CHILD_WeightedWidth, 0,
            LAYOUT_AddChild, (ULONG)filler(),
            TAG_END),
        CHILD_WeightedHeight, 0,
        TAG_END);
    snprintf(title, sizeof(title), S(MSG_GITDIFF_TITLE), rel);
    if (layout == NULL || (winobj = open_window(app, title, ref, layout)) == NULL) {
        FreeListBrowserList(&lines);
        if (opened != NULL) CloseFont(opened);
        return;
    }
    ++modal_open;
    SetAttrs(refobj != NULL ? refobj : app->window_object, WA_BusyPointer, TRUE, TAG_END);
    GetAttr(WINDOW_SigMask, app->window_object, &mainsig);
    if (refobj != NULL) GetAttr(WINDOW_SigMask, refobj, &refsig);
    GetAttr(WINDOW_SigMask, winobj, &sig);
    while (!done) {
        ULONG got = Wait(sig | refsig | mainsig);
        if ((got & mainsig) != 0) drop_input(app->window_object);
        if ((got & refsig) != 0) drop_input(refobj);
        while ((result = DoMethod(winobj, WM_HANDLEINPUT, &code)) != WMHI_LASTMSG) {
            if ((result & WMHI_CLASSMASK) == WMHI_CLOSEWINDOW) done = 1;
            else if ((result & WMHI_CLASSMASK) == WMHI_GADGETUP && (result & WMHI_GADGETMASK) == GD_CLOSE) done = 1;
            else if ((result & WMHI_CLASSMASK) == WMHI_RAWKEY && (code == 0x45 || code == 0x44)) done = 1;  /* Esc, Return */
        }
    }
    --modal_open;
    SetAttrs(refobj != NULL ? refobj : app->window_object, WA_BusyPointer, FALSE, TAG_END);
    DisposeObject(winobj);                      /* the gadgets with it */
    FreeListBrowserList(&lines);
    if (opened != NULL) CloseFont(opened);
}

void gitwin_diff_active(EditorApp *app)
{
    char root[EDITOR_PATH_MAX];
    const char *rel;
    if (app->active == NULL || app->active->path[0] == '\0') {
        ui_error(app, S(MSG_GIT_TITLE), S(MSG_GITDIFF_NOFILE));
        return;
    }
    if (!git_repository(app, root, sizeof(root)) || !open_classes(app)) return;
    if ((rel = gitstatus_relative(root, app->active->path)) == NULL || rel[0] == '\0') {
        ui_error(app, S(MSG_GIT_TITLE), S(MSG_GITDIFF_OUTSIDE));
        return;
    }
    show_diff(app, root, rel, app->window, NULL);
}

/* ---- the commit window --------------------------------------------------- */

/**
 * @brief Whether an entry is staged (its first letter names a change).
 * @param e The entry.
 * @return Non-zero if it is.
 */
static int is_staged(const GitStatusEntry *e)
{
    return e->index != ' ' && e->index != '?' && e->index != '!';
}

/**
 * @brief The text of an entry's state in the list.
 * @param e The entry.
 * @return The text (catalog).
 */
static const char *state_text(const GitStatusEntry *e)
{
    char x = e->index, y = e->worktree;
    if (e->state == GIT_ST_CONFLICT) return S(MSG_GITC_ST_CONFLICT);
    if (x == '?') return S(MSG_GITC_ST_NEW);
    if (is_staged(e) && y != ' ') return S(MSG_GITC_ST_PARTLY);
    switch (is_staged(e) ? x : y) {
        case 'A': return S(MSG_GITC_ST_ADDED);
        case 'D': return S(MSG_GITC_ST_DELETED);
        case 'R': case 'C': return S(MSG_GITC_ST_RENAMED);
        default: return S(MSG_GITC_ST_CHANGED);
    }
}

/**
 * @brief Read the status into gc.status.
 * @return Non-zero on success (otherwise a message was shown).
 */
static int read_status(void)
{
    char *out = NULL;
    LONG rc = run(gc.app, gc.root, "status --porcelain", &out);
    int ok = 0;
    if (rc == -1) message(gc.win != NULL ? gc.win : gc.app->window, S(MSG_GIT_NORUN));
    else if (rc != 0) git_report(gc.app, gc.win, MSG_GIT_FAILED, out);
    else if (!gitstatus_parse(&gc.status, out != NULL ? out : "")) message(gc.win != NULL ? gc.win : gc.app->window, S(MSG_PJ_NOMEM));
    else ok = 1;
    free(out);
    return ok;
}

/**
 * @brief Build the list's nodes from gc.status.
 */
static void make_nodes(void)
{
    long i;
    NewList(&gc.nodes);
    for (i = 0; i < gc.status.count; ++i) {
        const GitStatusEntry *e = &gc.status.entries[i];
        struct Node *n = AllocListBrowserNode(2,
            LBNA_CheckBox, TRUE, LBNA_Checked, (ULONG)is_staged(e), LBNA_UserData, (ULONG)i,
            LBNA_Column, 0, LBNCA_Text, (ULONG)state_text(e),
            LBNA_Column, 1, LBNCA_CopyText, TRUE, LBNCA_Text, (ULONG)e->path,
            TAG_END);
        if (n != NULL) AddTail(&gc.nodes, n);
    }
}

/**
 * @brief Show gc.status anew in the list.
 * @param sel The entry to select, or -1.
 */
static void refill(long sel)
{
    SetGadgetAttrs((struct Gadget *)gc.list, gc.win, NULL, LISTBROWSER_Labels, ~0UL, TAG_END);
    FreeListBrowserList(&gc.nodes);
    make_nodes();
    if (sel >= gc.status.count) sel = gc.status.count - 1;
    SetGadgetAttrs((struct Gadget *)gc.list, gc.win, NULL,
        LISTBROWSER_Labels, (ULONG)&gc.nodes,
        LISTBROWSER_Selected, (ULONG)(LONG)sel,
        sel >= 0 ? LISTBROWSER_MakeVisible : TAG_IGNORE, (ULONG)sel,
        TAG_END);
}

/**
 * @brief Run am-git with a file as its last argument.
 * @param args The arguments before the file, e.g. "add".
 * @param path The file relative to the repository.
 * @return Non-zero on success (otherwise a message was shown).
 */
static int run_on_file(const char *args, const char *path)
{
    char command[EDITOR_PATH_MAX + 32], *out = NULL;
    size_t n;
    LONG rc;
    strcpy(command, args);
    n = strlen(command);
    command[n++] = ' ';
    if (!git_quote(command + n, sizeof(command) - n, path)) {
        message(gc.win, S(MSG_ERR_PATHTOOLONG));
        return 0;
    }
    rc = run(gc.app, gc.root, command, &out);
    if (rc == -1) message(gc.win, S(MSG_GIT_NORUN));
    else if (rc != 0) git_report(gc.app, gc.win, MSG_GIT_FAILED, out);
    free(out);
    return rc == 0;
}

/**
 * @brief After a click in the list: stage or unstage the files whose box
 *        changed, then read the status again.
 */
static void list_clicked(void)
{
    struct Node *n;
    int changed = 0;
    LONG sel = (LONG)get(gc.list, LISTBROWSER_Selected);
    for (n = gc.nodes.lh_Head; n->ln_Succ != NULL; n = n->ln_Succ) {
        ULONG checked = 0, i = 0;
        const GitStatusEntry *e;
        GetListBrowserNodeAttrs(n, LBNA_Checked, (ULONG)&checked, LBNA_UserData, (ULONG)&i, TAG_END);
        if ((long)i >= gc.status.count) continue;
        e = &gc.status.entries[i];
        if ((checked != 0) == is_staged(e)) continue;
        changed = 1;
        /* add does not stage a deletion, rm --cached does (it drops the
           index entry); unstage puts back HEAD's entry (am-git 0.12.1) */
        if (!run_on_file(!checked ? "unstage" : e->worktree == 'D' ? "rm --cached" : "add", e->path)) break;
    }
    if (changed && read_status()) refill(sel);
}

/**
 * @brief The message a merge suggests: the first line of .git/MERGE_MSG
 *        while .git/MERGE_HEAD is there (after a Pull with conflicts).
 * @param root The repository.
 * @param[out] msg The message, "" if there is none.
 * @param size Size of @p msg.
 * @return Non-zero while a merge is going on (.git/MERGE_HEAD is there).
 */
static int merge_message(const char *root, char *msg, size_t size)
{
    char path[EDITOR_PATH_MAX], *text = NULL;
    BPTR lock;
    size_t n = 0;
    msg[0] = '\0';
    strncpy(path, root, sizeof(path) - 1);
    path[sizeof(path) - 1] = '\0';
    if (!AddPart((STRPTR)path, (STRPTR)".git/MERGE_HEAD", sizeof(path)) ||
        (lock = Lock((STRPTR)path, ACCESS_READ)) == 0)
        return 0;
    UnLock(lock);
    *PathPart((STRPTR)path) = '\0';
    if (!AddPart((STRPTR)path, (STRPTR)"MERGE_MSG", sizeof(path)) || datafile_read(path, &text) <= 0) return 1;
    while (text[n] != '\0' && text[n] != '\n' && text[n] != '\r' && n + 1 < size) {
        msg[n] = text[n];
        ++n;
    }
    msg[n] = '\0';
    free(text);
    return 1;
}

/**
 * @brief Commit with the message entered.
 * @return Non-zero if committed (the window closes).
 */
static int commit(void)
{
    const char *msg = (const char *)get(gc.message, STRINGA_TextVal);
    char command[MESSAGE_MAX * 2 + 32], *out = NULL;
    long i;
    LONG rc;
    if (msg == NULL || msg[0] == '\0') {
        message(gc.win, S(MSG_GITC_NOMESSAGE));
        ActivateLayoutGadget((struct Gadget *)gc.layout, gc.win, NULL, (ULONG)gc.message);
        return 0;
    }
    /* a merge may be committed with a tree equal to HEAD (a conflict
       resolved with one's own version): am-git takes MERGE_HEAD as the
       second parent and refuses only an empty index or a conflict left */
    for (i = 0; i < gc.status.count && !is_staged(&gc.status.entries[i]); ++i) ;
    if (i >= gc.status.count && !gc.merging) {
        message(gc.win, S(MSG_GITC_NOTHING));
        return 0;
    }
    strcpy(command, "commit -m ");
    if (!git_quote(command + 10, sizeof(command) - 10, msg)) return 0;
    rc = run(gc.app, gc.root, command, &out);
    if (rc == -1) message(gc.win, S(MSG_GIT_NORUN));
    else if (rc != 0) git_report(gc.app, gc.win, MSG_GITC_FAILED, out);
    free(out);
    if (rc != 0) return 0;
    ui_status_message(gc.app, S(MSG_GITC_DONE));
    return 1;
}

void gitwin_commit(EditorApp *app)
{
    char merge_msg[MESSAGE_MAX + 1];
    ULONG sig = 0, mainsig = 0, result;
    WORD code;
    int done = 0;
    LONG w = TextLength(&app->screen->RastPort, (STRPTR)"n", 1);
    ULONG h = app->screen->RastPort.TxHeight + 1U;

    memset(&gc, 0, sizeof(gc));
    gc.app = app;
    gitstatus_init(&gc.status);
    if (!git_repository(app, gc.root, sizeof(gc.root)) || !open_classes(app) || !read_status()) goto out;
    gc.merging = merge_message(gc.root, merge_msg, sizeof(merge_msg));
    if (gc.status.count == 0 && !gc.merging) {
        message(app->window, S(MSG_GITC_CLEAN));
        goto out;
    }
    make_nodes();
    gc.cols[0].ci_Width = 30;
    gc.cols[0].ci_Title = (STRPTR)S(MSG_GITC_COL_STATE);
    gc.cols[1].ci_Width = 70;
    gc.cols[1].ci_Title = (STRPTR)S(MSG_GITC_COL_FILE);
    gc.cols[2].ci_Width = -1;
    gc.list = NewObject(LISTBROWSER_GetClass(), NULL,
        GA_ID, GC_LIST, GA_RelVerify, TRUE,
        LISTBROWSER_Labels, (ULONG)&gc.nodes,
        LISTBROWSER_ColumnInfo, (ULONG)gc.cols,
        LISTBROWSER_ColumnTitles, TRUE,
        LISTBROWSER_ShowSelected, TRUE,
        LISTBROWSER_AutoFit, TRUE,
        LISTBROWSER_HorizontalProp, TRUE,
        TAG_END);
    gc.message = NewObject(STRING_GetClass(), NULL,
        GA_ID, GC_MESSAGE, GA_RelVerify, TRUE, GA_TabCycle, TRUE,
        STRINGA_MaxChars, MESSAGE_MAX, STRINGA_TextVal, (ULONG)merge_msg, TAG_END);
    gc.layout = NewObject(LAYOUT_GetClass(), NULL,
        LAYOUT_Orientation, LAYOUT_ORIENT_VERT,
        LAYOUT_SpaceOuter, TRUE,
        LAYOUT_LeftSpacing, AREA_SPACING,
        LAYOUT_RightSpacing, AREA_SPACING,
        LAYOUT_TopSpacing, AREA_SPACING,
        LAYOUT_BottomSpacing, AREA_SPACING,
        LAYOUT_InnerSpacing, AREA_SPACING,
        LAYOUT_DeferLayout, TRUE,
        LAYOUT_AddImage, (ULONG)label(S(MSG_GITC_LISTNOTE)), FIXED,
        LAYOUT_AddChild, (ULONG)NewObject(LAYOUT_GetClass(), NULL,
            LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ,
            LAYOUT_InnerSpacing, AREA_SPACING,
            LAYOUT_AddChild, (ULONG)gc.list,
            CHILD_MinWidth, (ULONG)(50 * w),
            CHILD_MinHeight, 10 * h,
            /* the buttons beside the list, at its top */
            LAYOUT_AddChild, (ULONG)NewObject(LAYOUT_GetClass(), NULL,
                LAYOUT_Orientation, LAYOUT_ORIENT_VERT,
                LAYOUT_InnerSpacing, ROW_SPACING,
                LAYOUT_AddChild, (ULONG)button(GC_ADDALL, S(MSG_GITC_ADDALL)), FIXED,
                LAYOUT_AddChild, (ULONG)button(GC_DIFF, S(MSG_GITC_DIFF)), FIXED,
                LAYOUT_AddChild, (ULONG)filler(),
                TAG_END),
            CHILD_WeightedWidth, 0,
            TAG_END),
        LAYOUT_AddChild, (ULONG)gc.message, FIXED,
        CHILD_Label, (ULONG)label(S(MSG_GITC_MESSAGE)),
        LAYOUT_AddImage, (ULONG)label(S(gc.merging ? MSG_GITC_MERGENOTE : MSG_GITC_NOTE)), FIXED,
        LAYOUT_AddChild, (ULONG)NewObject(LAYOUT_GetClass(), NULL,
            LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ,
            LAYOUT_AddChild, (ULONG)button(GC_COMMIT, S(MSG_GITC_COMMIT)),
            CHILD_WeightedWidth, 0,
            LAYOUT_AddChild, (ULONG)filler(),
            LAYOUT_AddChild, (ULONG)button(GC_CLOSE, S(MSG_GITWIN_CLOSE)),
            CHILD_WeightedWidth, 0,
            TAG_END),
        FIXED,
        TAG_END);
    if (gc.layout == NULL || (gc.winobj = open_window(app, S(MSG_GITC_TITLE), app->window, gc.layout)) == NULL) {
        gc.winobj = NULL;
        FreeListBrowserList(&gc.nodes);
        goto out;
    }
    GetAttr(WINDOW_Window, gc.winobj, (ULONG *)&gc.win);
    ++modal_open;
    ActivateLayoutGadget((struct Gadget *)gc.layout, gc.win, NULL, (ULONG)gc.message);

    SetAttrs(app->window_object, WA_BusyPointer, TRUE, TAG_END);
    GetAttr(WINDOW_SigMask, app->window_object, &mainsig);
    GetAttr(WINDOW_SigMask, gc.winobj, &sig);
    while (!done) {
        ULONG got = Wait(sig | mainsig);
        if ((got & mainsig) != 0) drop_input(app->window_object);
        while ((result = DoMethod(gc.winobj, WM_HANDLEINPUT, &code)) != WMHI_LASTMSG) {
            if ((result & WMHI_CLASSMASK) == WMHI_CLOSEWINDOW) { done = 1; continue; }
            if ((result & WMHI_CLASSMASK) != WMHI_GADGETUP) continue;
            switch (result & WMHI_GADGETMASK) {
                case GC_LIST:
                    if ((get(gc.list, LISTBROWSER_RelEvent) & LBRE_DOUBLECLICK) != 0) {
                        LONG sel = (LONG)get(gc.list, LISTBROWSER_Selected);
                        if (sel >= 0 && sel < gc.status.count)
                            show_diff(app, gc.root, gc.status.entries[sel].path, gc.win, gc.winobj);
                    } else {
                        list_clicked();
                    }
                    break;
                case GC_DIFF: {
                    LONG sel = (LONG)get(gc.list, LISTBROWSER_Selected);
                    if (sel >= 0 && sel < gc.status.count)
                        show_diff(app, gc.root, gc.status.entries[sel].path, gc.win, gc.winobj);
                    else message(gc.win, S(MSG_GITC_NOSELECTION));
                    break;
                }
                case GC_ADDALL:
                    if (run_on_file("add", ".") && read_status())
                        refill((LONG)get(gc.list, LISTBROWSER_Selected));
                    break;
                case GC_COMMIT:
                    if (commit()) done = 1;
                    break;
                case GC_CLOSE:
                    done = 1;
                    break;
            }
        }
    }
    --modal_open;
    SetAttrs(app->window_object, WA_BusyPointer, FALSE, TAG_END);

out:
    if (gc.winobj != NULL) DisposeObject(gc.winobj);       /* the gadgets with it */
    if (gc.nodes.lh_Head != NULL) FreeListBrowserList(&gc.nodes);
    gitstatus_clear(&gc.status);
    memset(&gc, 0, sizeof(gc));
    /* staged or committed: the tree's colours change */
    git_status_request(app);
}

/* ---- the clone window ---------------------------------------------------- */

/**
 * @brief The name a repository's address suggests: its last part without
 *        ".git" ("repo" of "https://host/user/repo.git").
 * @param url The address.
 * @param[out] name The name, "" if there is none.
 * @param size Size of @p name.
 */
static void name_from_url(const char *url, char *name, size_t size)
{
    size_t len = strlen(url), start, n;
    while (len > 0 && (url[len - 1] == '/' || url[len - 1] == ' ')) --len;
    start = len;
    while (start > 0 && url[start - 1] != '/' && url[start - 1] != ':') --start;
    n = len - start;
    if (n > 4 && strncmp(url + len - 4, ".git", 4) == 0) n -= 4;
    if (n >= size) n = size - 1;
    memcpy(name, url + start, n);
    name[n] = '\0';
}

/**
 * @brief Check the values of the clone window (a message for the first
 *        that is wrong).
 * @param win The clone window (messages).
 * @param url The address.
 * @param drawer The drawer.
 * @param name The new drawer's name.
 * @return Non-zero if they are right (the window closes, the clone starts).
 */
static int start_clone_check(struct Window *win, const char *url, const char *drawer, const char *name)
{
    char target[EDITOR_PATH_MAX], text[EDITOR_PATH_MAX + 120];
    BPTR lock;
    if (url[0] == '\0') {
        message(win, S(MSG_GITCL_NOURL));
        return 0;
    }
    if (name[0] == '\0' || strpbrk(name, "/:") != NULL) {
        message(win, S(MSG_GITCL_BADNAME));
        return 0;
    }
    if (drawer[0] == '\0' || (lock = Lock((STRPTR)drawer, ACCESS_READ)) == 0) {
        message(win, S(MSG_GITCL_NODRAWER));
        return 0;
    }
    UnLock(lock);
    strncpy(target, drawer, sizeof(target) - 1);
    target[sizeof(target) - 1] = '\0';
    if (!AddPart((STRPTR)target, (STRPTR)name, sizeof(target))) {
        message(win, S(MSG_ERR_PATHTOOLONG));
        return 0;
    }
    if ((lock = Lock((STRPTR)target, ACCESS_READ)) != 0) {
        UnLock(lock);
        snprintf(text, sizeof(text), S(MSG_GITCL_EXISTS), target);
        message(win, text);
        return 0;
    }
    return 1;
}

void gitwin_clone(EditorApp *app)
{
    char drawer[EDITOR_PATH_MAX], suggested[128] = "", root[EDITOR_PATH_MAX];
    Object *winobj, *layout, *url, *dir, *name;
    struct Window *win = NULL;
    ULONG sig = 0, mainsig = 0, result;
    WORD code;
    int done = 0, start = 0;
    LONG w = TextLength(&app->screen->RastPort, (STRPTR)"n", 1);
    char url_v[512], drawer_v[EDITOR_PATH_MAX], name_v[128];

    if (!open_classes(app)) return;
    /* where projects go: the setting, else the drawer above the project */
    drawer[0] = '\0';
    if (app->settings.projectdir[0] != '\0') {
        strncpy(drawer, app->settings.projectdir, sizeof(drawer) - 1);
        drawer[sizeof(drawer) - 1] = '\0';
    } else if (tree_project_root(app, root, sizeof(root))) {
        *PathPart((STRPTR)root) = '\0';
        strcpy(drawer, root);
    }
    url = NewObject(STRING_GetClass(), NULL,
        GA_ID, GK_URL, GA_RelVerify, TRUE, GA_TabCycle, TRUE,
        STRINGA_MaxChars, sizeof(url_v) - 1, STRINGA_TextVal, (ULONG)"", TAG_END);
    dir = NewObject(STRING_GetClass(), NULL,
        GA_ID, GK_DRAWER, GA_RelVerify, TRUE, GA_TabCycle, TRUE,
        STRINGA_MaxChars, sizeof(drawer_v) - 1, STRINGA_TextVal, (ULONG)drawer, TAG_END);
    name = NewObject(STRING_GetClass(), NULL,
        GA_ID, GK_NAME, GA_RelVerify, TRUE, GA_TabCycle, TRUE,
        STRINGA_MaxChars, sizeof(name_v) - 1, STRINGA_TextVal, (ULONG)"", TAG_END);
    layout = NewObject(LAYOUT_GetClass(), NULL,
        LAYOUT_Orientation, LAYOUT_ORIENT_VERT,
        LAYOUT_SpaceOuter, TRUE,
        LAYOUT_LeftSpacing, AREA_SPACING,
        LAYOUT_RightSpacing, AREA_SPACING,
        LAYOUT_TopSpacing, AREA_SPACING,
        LAYOUT_BottomSpacing, AREA_SPACING,
        LAYOUT_InnerSpacing, ROW_SPACING,
        LAYOUT_DeferLayout, TRUE,
        LAYOUT_AddChild, (ULONG)url, FIXED,
        CHILD_Label, (ULONG)label(S(MSG_GITCL_URL)),
        CHILD_MinWidth, (ULONG)(50 * w),
        LAYOUT_AddChild, (ULONG)NewObject(LAYOUT_GetClass(), NULL,
            LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ,
            LAYOUT_InnerSpacing, ROW_SPACING,
            LAYOUT_AddChild, (ULONG)dir,
            LAYOUT_AddChild, (ULONG)button(GK_CHOOSE, S(MSG_GITCL_CHOOSE)),
            CHILD_WeightedWidth, 0,
            TAG_END),
        FIXED,
        CHILD_Label, (ULONG)label(S(MSG_GITCL_DRAWER)),
        LAYOUT_AddChild, (ULONG)name, FIXED,
        CHILD_Label, (ULONG)label(S(MSG_GITCL_NAME)),
        LAYOUT_AddImage, (ULONG)label(S(MSG_GITCL_NOTE)), FIXED,
        LAYOUT_AddChild, (ULONG)filler(),
        LAYOUT_AddChild, (ULONG)NewObject(LAYOUT_GetClass(), NULL,
            LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ,
            LAYOUT_AddChild, (ULONG)button(GK_CLONE, S(MSG_GITCL_CLONE)),
            CHILD_WeightedWidth, 0,
            LAYOUT_AddChild, (ULONG)filler(),
            LAYOUT_AddChild, (ULONG)button(GK_CANCEL, S(MSG_PREFS_CANCEL)),
            CHILD_WeightedWidth, 0,
            TAG_END),
        FIXED,
        TAG_END);
    if (layout == NULL || (winobj = open_window(app, S(MSG_GITCL_TITLE), app->window, layout)) == NULL) return;
    GetAttr(WINDOW_Window, winobj, (ULONG *)&win);
    ActivateLayoutGadget((struct Gadget *)layout, win, NULL, (ULONG)url);

    SetAttrs(app->window_object, WA_BusyPointer, TRUE, TAG_END);
    GetAttr(WINDOW_SigMask, app->window_object, &mainsig);
    GetAttr(WINDOW_SigMask, winobj, &sig);
    while (!done) {
        ULONG got = Wait(sig | mainsig);
        if ((got & mainsig) != 0) drop_input(app->window_object);
        while ((result = DoMethod(winobj, WM_HANDLEINPUT, &code)) != WMHI_LASTMSG) {
            if ((result & WMHI_CLASSMASK) == WMHI_CLOSEWINDOW) { done = 1; continue; }
            if ((result & WMHI_CLASSMASK) != WMHI_GADGETUP) continue;
            switch (result & WMHI_GADGETMASK) {
                case GK_URL: {
                    /* suggest the name while it is empty or still the suggestion */
                    const char *u = (const char *)get(url, STRINGA_TextVal);
                    const char *n = (const char *)get(name, STRINGA_TextVal);
                    if (u != NULL && (n == NULL || n[0] == '\0' || strcmp(n, suggested) == 0)) {
                        name_from_url(u, suggested, sizeof(suggested));
                        SetGadgetAttrs((struct Gadget *)name, win, NULL, STRINGA_TextVal, (ULONG)suggested, TAG_END);
                    }
                    break;
                }
                case GK_CHOOSE: {
                    const char *cur = (const char *)get(dir, STRINGA_TextVal);
                    struct FileRequester *fr = AllocAslRequestTags(ASL_FileRequest,
                        ASLFR_TitleText, (ULONG)S(MSG_GITCL_ASL), ASLFR_DrawersOnly, TRUE, TAG_END);
                    if (fr == NULL) break;
                    if (AslRequestTags(fr, ASLFR_Window, (ULONG)win, ASLFR_SleepWindow, TRUE,
                                       cur != NULL && cur[0] != '\0' ? ASLFR_InitialDrawer : TAG_IGNORE, (ULONG)cur,
                                       TAG_END) && fr->fr_Drawer[0] != '\0')
                        SetGadgetAttrs((struct Gadget *)dir, win, NULL, STRINGA_TextVal, (ULONG)fr->fr_Drawer, TAG_END);
                    FreeAslRequest(fr);
                    break;
                }
                case GK_CLONE: {
                    const char *u = (const char *)get(url, STRINGA_TextVal);
                    const char *d = (const char *)get(dir, STRINGA_TextVal);
                    const char *n = (const char *)get(name, STRINGA_TextVal);
                    /* copies: the clone starts after the window is gone */
                    while (u != NULL && *u == ' ') ++u;
                    strncpy(url_v, u != NULL ? u : "", sizeof(url_v) - 1);
                    url_v[sizeof(url_v) - 1] = '\0';
                    strncpy(drawer_v, d != NULL ? d : "", sizeof(drawer_v) - 1);
                    drawer_v[sizeof(drawer_v) - 1] = '\0';
                    strncpy(name_v, n != NULL ? n : "", sizeof(name_v) - 1);
                    name_v[sizeof(name_v) - 1] = '\0';
                    {
                        size_t k = strlen(url_v);
                        while (k > 0 && url_v[k - 1] == ' ') url_v[--k] = '\0';
                    }
                    if (start_clone_check(win, url_v, drawer_v, name_v)) done = start = 1;
                    break;
                }
                case GK_CANCEL:
                    done = 1;
                    break;
            }
        }
    }
    SetAttrs(app->window_object, WA_BusyPointer, FALSE, TAG_END);
    DisposeObject(winobj);                      /* the gadgets with it */
    if (start) git_clone(app, url_v, drawer_v, name_v);
}

/* ---- the conflict window ----------------------------------------------- */

/** The files that had a conflict when the conflict window opened. */
static struct {
    char **path;                /* relative to the repository */
    int *resolved;              /* no longer a conflict in the status */
    long count;
} gk;

/**
 * @brief The first line of a text holding a conflict marker.
 * @param text The text.
 * @return The line (1-based), or 0 if there is none.
 */
static long marker_line(const char *text)
{
    const char *line = text;
    long n = 1;
    while (*line != '\0') {
        if (strncmp(line, "<<<<<<< ", 8) == 0 || strncmp(line, ">>>>>>> ", 8) == 0 ||
            (strncmp(line, "=======", 7) == 0 && (line[7] == '\n' || line[7] == '\r' || line[7] == '\0')))
            return n;
        while (*line != '\0' && *line != '\n') ++line;
        if (*line == '\n') ++line;
        ++n;
    }
    return 0;
}

/**
 * @brief The first marker of a file of the repository.
 * @param rel The file.
 * @return Its line (1-based), 0 if none or it cannot be read.
 */
static long file_marker(const char *rel)
{
    char path[EDITOR_PATH_MAX], *text = NULL;
    long line = 0;
    strncpy(path, gc.root, sizeof(path) - 1);
    path[sizeof(path) - 1] = '\0';
    if (AddPart((STRPTR)path, (STRPTR)rel, sizeof(path)) && datafile_read(path, &text) > 0)
        line = marker_line(text);
    free(text);
    return line;
}

/**
 * @brief Note which of the files are no longer conflicts (gc.status read).
 */
static void update_resolved(void)
{
    long i;
    for (i = 0; i < gk.count; ++i)
        gk.resolved[i] = gitstatus_state(&gc.status, gk.path[i], 0) != GIT_ST_CONFLICT;
}

/**
 * @brief Build the conflict list's nodes.
 */
static void make_conflict_nodes(void)
{
    long i;
    NewList(&gc.nodes);
    for (i = 0; i < gk.count; ++i) {
        struct Node *n = AllocListBrowserNode(2,
            LBNA_Column, 0, LBNCA_Text, (ULONG)S(gk.resolved[i] ? MSG_GITR_RESOLVED : MSG_GITC_ST_CONFLICT),
            LBNA_Column, 1, LBNCA_CopyText, TRUE, LBNCA_Text, (ULONG)gk.path[i],
            TAG_END);
        if (n != NULL) AddTail(&gc.nodes, n);
    }
}

/**
 * @brief Mark a file as resolved (am-git add), after the checks.
 * @param i The file.
 * @return Non-zero if the list changed.
 */
static int resolve(long i)
{
    char full[EDITOR_PATH_MAX];
    Document *doc;
    strncpy(full, gc.root, sizeof(full) - 1);
    full[sizeof(full) - 1] = '\0';
    if (gk.resolved[i] || !AddPart((STRPTR)full, (STRPTR)gk.path[i], sizeof(full))) return 0;
    /* the file on disk counts: a changed document must be saved first */
    if ((doc = document_find_path(gc.app, full)) != NULL && doc->dirty) {
        message(gc.win, S(MSG_GITR_UNSAVED));
        return 0;
    }
    if (file_marker(gk.path[i]) > 0) {
        struct EasyStruct es = {sizeof(es), 0, NULL, NULL, NULL};
        es.es_Title = (STRPTR)S(MSG_GIT_TITLE);
        es.es_TextFormat = (STRPTR)S(MSG_GITR_MARKERS);
        es.es_GadgetFormat = (STRPTR)S(MSG_GITR_MARKERSGADGETS);
        if (EasyRequestArgs(gc.win, &es, NULL, NULL) != 1) return 0;
    }
    if (!run_on_file("add", gk.path[i]) || !read_status()) return 0;
    update_resolved();
    return 1;
}

/**
 * @brief Free the list of the files.
 */
static void free_conflicts(void)
{
    long i;
    for (i = 0; i < gk.count; ++i) free(gk.path[i]);
    free(gk.path);
    free(gk.resolved);
    memset(&gk, 0, sizeof(gk));
}

void gitwin_conflicts(EditorApp *app)
{
    ULONG sig = 0, mainsig = 0, result;
    WORD code;
    int done = 0, then_commit = 0;
    long i, open_line = 0, open_index = -1;
    char open_path[EDITOR_PATH_MAX];
    LONG w = TextLength(&app->screen->RastPort, (STRPTR)"n", 1);
    ULONG h = app->screen->RastPort.TxHeight + 1U;

    memset(&gc, 0, sizeof(gc));
    memset(&gk, 0, sizeof(gk));
    gc.app = app;
    gitstatus_init(&gc.status);
    if (!git_repository(app, gc.root, sizeof(gc.root)) || !open_classes(app)) goto out;
    git_fix_dot_drawer(app, gc.root);       /* a Pull's files left in "." */
    if (!read_status()) goto out;
    for (i = 0; i < gc.status.count; ++i)
        if (gc.status.entries[i].state == GIT_ST_CONFLICT) ++gk.count;
    if (gk.count == 0) {
        message(app->window, S(MSG_GITR_NONE));
        goto out;
    }
    gk.path = calloc((size_t)gk.count, sizeof(char *));
    gk.resolved = calloc((size_t)gk.count, sizeof(int));
    if (gk.path == NULL || gk.resolved == NULL) {
        gk.count = 0;
        message(app->window, S(MSG_PJ_NOMEM));
        goto out;
    }
    gk.count = 0;
    for (i = 0; i < gc.status.count; ++i)
        if (gc.status.entries[i].state == GIT_ST_CONFLICT &&
            (gk.path[gk.count] = malloc(strlen(gc.status.entries[i].path) + 1)) != NULL)
            strcpy(gk.path[gk.count++], gc.status.entries[i].path);
    make_conflict_nodes();
    gc.cols[0].ci_Width = 25;
    gc.cols[0].ci_Title = (STRPTR)S(MSG_GITC_COL_STATE);
    gc.cols[1].ci_Width = 75;
    gc.cols[1].ci_Title = (STRPTR)S(MSG_GITC_COL_FILE);
    gc.cols[2].ci_Width = -1;
    gc.list = NewObject(LISTBROWSER_GetClass(), NULL,
        GA_ID, GR_LIST, GA_RelVerify, TRUE,
        LISTBROWSER_Labels, (ULONG)&gc.nodes,
        LISTBROWSER_ColumnInfo, (ULONG)gc.cols,
        LISTBROWSER_ColumnTitles, TRUE,
        LISTBROWSER_ShowSelected, TRUE,
        LISTBROWSER_Selected, 0,
        LISTBROWSER_AutoFit, TRUE,
        TAG_END);
    gc.layout = NewObject(LAYOUT_GetClass(), NULL,
        LAYOUT_Orientation, LAYOUT_ORIENT_VERT,
        LAYOUT_SpaceOuter, TRUE,
        LAYOUT_LeftSpacing, AREA_SPACING,
        LAYOUT_RightSpacing, AREA_SPACING,
        LAYOUT_TopSpacing, AREA_SPACING,
        LAYOUT_BottomSpacing, AREA_SPACING,
        LAYOUT_InnerSpacing, AREA_SPACING,
        LAYOUT_DeferLayout, TRUE,
        LAYOUT_AddImage, (ULONG)label(S(MSG_GITR_LISTNOTE)), FIXED,
        LAYOUT_AddChild, (ULONG)NewObject(LAYOUT_GetClass(), NULL,
            LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ,
            LAYOUT_InnerSpacing, AREA_SPACING,
            LAYOUT_AddChild, (ULONG)gc.list,
            CHILD_MinWidth, (ULONG)(46 * w),
            CHILD_MinHeight, 8 * h,
            LAYOUT_AddChild, (ULONG)NewObject(LAYOUT_GetClass(), NULL,
                LAYOUT_Orientation, LAYOUT_ORIENT_VERT,
                LAYOUT_InnerSpacing, ROW_SPACING,
                LAYOUT_AddChild, (ULONG)button(GR_RESOLVE, S(MSG_GITR_RESOLVE)), FIXED,
                LAYOUT_AddChild, (ULONG)button(GR_OPEN, S(MSG_GITR_OPEN)), FIXED,
                LAYOUT_AddChild, (ULONG)filler(),
                TAG_END),
            CHILD_WeightedWidth, 0,
            TAG_END),
        LAYOUT_AddImage, (ULONG)label(S(MSG_GITR_NOTE)), FIXED,
        LAYOUT_AddChild, (ULONG)NewObject(LAYOUT_GetClass(), NULL,
            LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ,
            LAYOUT_AddChild, (ULONG)button(GR_COMMIT, S(MSG_GITR_COMMIT)),
            CHILD_WeightedWidth, 0,
            LAYOUT_AddChild, (ULONG)filler(),
            LAYOUT_AddChild, (ULONG)button(GR_CLOSE, S(MSG_GITWIN_CLOSE)),
            CHILD_WeightedWidth, 0,
            TAG_END),
        FIXED,
        TAG_END);
    if (gc.layout == NULL || (gc.winobj = open_window(app, S(MSG_GITR_TITLE), app->window, gc.layout)) == NULL) {
        gc.winobj = NULL;
        FreeListBrowserList(&gc.nodes);
        goto out;
    }
    GetAttr(WINDOW_Window, gc.winobj, (ULONG *)&gc.win);
    ++modal_open;

    SetAttrs(app->window_object, WA_BusyPointer, TRUE, TAG_END);
    GetAttr(WINDOW_SigMask, app->window_object, &mainsig);
    GetAttr(WINDOW_SigMask, gc.winobj, &sig);
    while (!done) {
        ULONG got = Wait(sig | mainsig);
        if ((got & mainsig) != 0) drop_input(app->window_object);
        while ((result = DoMethod(gc.winobj, WM_HANDLEINPUT, &code)) != WMHI_LASTMSG) {
            LONG sel;
            if ((result & WMHI_CLASSMASK) == WMHI_CLOSEWINDOW) { done = 1; continue; }
            if ((result & WMHI_CLASSMASK) != WMHI_GADGETUP) continue;
            sel = (LONG)get(gc.list, LISTBROWSER_Selected);
            switch (result & WMHI_GADGETMASK) {
                case GR_LIST:
                    if ((get(gc.list, LISTBROWSER_RelEvent) & LBRE_DOUBLECLICK) == 0) break;
                    /* fall through: a double click opens the file */
                case GR_OPEN:
                    if (sel < 0 || sel >= gk.count) { message(gc.win, S(MSG_GITC_NOSELECTION)); break; }
                    open_index = sel;
                    done = 1;
                    break;
                case GR_RESOLVE:
                    if (sel < 0 || sel >= gk.count) { message(gc.win, S(MSG_GITC_NOSELECTION)); break; }
                    if (resolve(sel)) {
                        SetGadgetAttrs((struct Gadget *)gc.list, gc.win, NULL, LISTBROWSER_Labels, ~0UL, TAG_END);
                        FreeListBrowserList(&gc.nodes);
                        make_conflict_nodes();
                        /* on to the next conflict */
                        for (i = 0; i < gk.count && gk.resolved[(sel + i) % gk.count]; ++i) ;
                        if (i < gk.count) sel = (sel + i) % gk.count;
                        SetGadgetAttrs((struct Gadget *)gc.list, gc.win, NULL,
                            LISTBROWSER_Labels, (ULONG)&gc.nodes, LISTBROWSER_Selected, (ULONG)sel, TAG_END);
                    }
                    break;
                case GR_COMMIT:
                    for (i = 0; i < gk.count && gk.resolved[i]; ++i) ;
                    if (i < gk.count) message(gc.win, S(MSG_GITR_LEFT));
                    else done = then_commit = 1;
                    break;
                case GR_CLOSE:
                    done = 1;
                    break;
            }
        }
    }
    --modal_open;
    SetAttrs(app->window_object, WA_BusyPointer, FALSE, TAG_END);
    if (open_index >= 0) {
        strncpy(open_path, gc.root, sizeof(open_path) - 1);
        open_path[sizeof(open_path) - 1] = '\0';
        if (!AddPart((STRPTR)open_path, (STRPTR)gk.path[open_index], sizeof(open_path))) open_index = -1;
        else open_line = file_marker(gk.path[open_index]);
    }

out:
    if (gc.winobj != NULL) DisposeObject(gc.winobj);       /* the gadgets with it */
    if (gc.nodes.lh_Head != NULL) FreeListBrowserList(&gc.nodes);
    gitstatus_clear(&gc.status);
    memset(&gc, 0, sizeof(gc));
    free_conflicts();
    git_status_request(app);
    if (open_index >= 0) document_go_to(app, open_path, open_line > 0 ? open_line : 1, 1);
    else if (then_commit) gitwin_commit(app);
}
