/**
 * @file treeops.c
 * @brief The file operations of the folder tree: the buttons above it
 *        (New File, New Drawer, Rename, Delete), the same items in
 *        Project/Folder Tree, the small window asking for a name, and the
 *        operations themselves.
 *
 * The operations work on the entry selected in dirtree.gadget
 * (DIRTREE_SelectedPath); new files and drawers go into the selected
 * drawer, into the drawer of a selected file, or into the project's.
 * dirtree.gadget watches the shown drawers itself, so the tree follows
 * every change without being told. The tree does not show icons: Rename
 * and Delete take an entry's icon (name.info) along.
 *
 * The buttons show symbols of tmplsym.c (12 x 12 pixels in the screen's
 * pens) instead of AISS images, which would cost start-up time and be
 * larger than the tree's rows. Without a project all buttons are ghosted,
 * without a selected entry Rename and Delete; treeops_update() follows
 * that after every batch of input. button.gadget does not dispose its
 * BUTTON_RenderImage, so the images are disposed after the window
 * (treeops_dispose()). The column lies on the first page of a
 * page.gadget; the hidden tree shows the empty second one.
 *
 * Delete asks first, naming how many files and drawers a drawer holds,
 * and deletes nothing if anything in it is protected from deletion (it
 * does not lift the protection). Open documents of deleted files stay
 * open, marked as changed, so their text is not lost; those of renamed
 * files and drawers follow the new names.
 */
#include "editor.h"

#include <classes/window.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <gadgets/button.h>
#include <gadgets/dirtree.h>
#include <gadgets/layout.h>
#include <images/bevel.h>
#include <gadgets/string.h>
#include <images/label.h>
#include <intuition/gadgetclass.h>
#include <intuition/intuition.h>
#include <proto/button.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/label.h>
#include <proto/layout.h>
#include <proto/string.h>
#include <proto/utility.h>
#include <proto/window.h>
#include <clib/alib_protos.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern struct Library *StringBase;         /* shared with the find window */

/** Longest name the window takes (FFS allows 107 characters). */
#define NAME_MAX_CHARS 108
/** Deepest drawer level below the deleted drawer that is walked. */
#define WALK_DEPTH 32
/** Raw key code of Esc. */
#define RAWKEY_ESC 0x45

/** Bits of the buttons' state (treeops_update()). */
enum {
    TS_PROJECT = 1,     /**< a project is open */
    TS_SELECTED = 2     /**< an entry is selected */
};

/** The buttons above the tree. */
static struct {
    Object *buttons[TREEOPS_BUTTONS];   /**< button.gadget, owned by the column */
    Object *images[TREEOPS_BUTTONS];    /**< their symbols (disposed by treeops_dispose()) */
    int state;                          /**< TS_ bits shown */
} to;

/** Distance of the buttons from the frame of their row. */
#define ROW_INSET 3

/** Symbols of the buttons, in the order of their gadget IDs. */
static const TmplSymType symbols[TREEOPS_BUTTONS] = {
    TMPLSYM_NEW_FILE, TMPLSYM_NEW_DRAWER, TMPLSYM_RENAME, TMPLSYM_DELETE
};

/** Help bubbles of the buttons, in the order of their gadget IDs. */
static const LONG hints[TREEOPS_BUTTONS] = {
    MSG_TREEOPS_HINT_NEWFILE, MSG_TREEOPS_HINT_NEWDRAWER,
    MSG_TREEOPS_HINT_RENAME, MSG_TREEOPS_HINT_DELETE
};

/* ---- messages ------------------------------------------------------- */

/**
 * @brief Show a message about the folder tree.
 *
 * The text is passed as an argument of "%s", so a '%' in a file name is
 * shown as it is.
 *
 * @param app The application state.
 * @param text The message.
 */
static void message(EditorApp *app, const char *text)
{
    struct EasyStruct es = {sizeof(es), 0, (STRPTR)S(MSG_TREE_TITLE), (STRPTR)"%s", (STRPTR)S(MSG_OK)};
    EasyRequest(app->window, &es, NULL, (ULONG)text);
}

/**
 * @brief Show a message with a path.
 * @param app The application state.
 * @param id The message (MSG_, with %s for the path).
 * @param path The path.
 */
static void path_message(EditorApp *app, LONG id, const char *path)
{
    char text[EDITOR_PATH_MAX + 200];
    snprintf(text, sizeof(text), S(id), path);
    message(app, text);
}

/**
 * @brief Show a failed DOS operation with AmigaDOS's text of the error.
 * @param app The application state.
 * @param id The message (MSG_, with %s for the path, then %s for the reason).
 * @param path The path.
 * @param error The DOS error (IoErr()).
 */
static void dos_message(EditorApp *app, LONG id, const char *path, LONG error)
{
    char reason[100], text[EDITOR_PATH_MAX + 300];
    if (Fault(error, NULL, (STRPTR)reason, sizeof(reason)) <= 0)
        snprintf(reason, sizeof(reason), "%ld", (long)error);
    snprintf(text, sizeof(text), S(id), path, reason);
    message(app, text);
}

/* ---- paths ---------------------------------------------------------- */

/**
 * @brief Copy the path of the entry selected in the tree.
 *
 * The project's drawer itself does not count as an entry.
 *
 * @param app The application state.
 * @param[out] out The path.
 * @param size Size of @p out.
 * @return Non-zero if an entry is selected.
 */
static int selected_path(EditorApp *app, char *out, size_t size)
{
    STRPTR path = NULL;
    char root[EDITOR_PATH_MAX];
    if (app->tree == NULL) return 0;
    GetAttr(DIRTREE_SelectedPath, app->tree, (ULONG *)&path);
    if (path == NULL || path[0] == '\0' || strlen((const char *)path) >= size) return 0;
    if (tree_project_root(app, root, sizeof(root)) && Stricmp((STRPTR)root, path) == 0) return 0;
    strcpy(out, (const char *)path);
    return 1;
}

/**
 * @brief Examine a file or drawer.
 * @param path Its path.
 * @param[out] type Its fib_DirEntryType (> 0 for drawers and links to them).
 * @param[out] protection Its fib_Protection.
 * @return Non-zero if it exists and could be examined (IoErr() otherwise).
 */
static int examine(const char *path, LONG *type, LONG *protection)
{
    struct FileInfoBlock *fib;
    BPTR lock;
    int ok = 0;
    if ((lock = Lock((STRPTR)path, ACCESS_READ)) == 0) return 0;
    if ((fib = AllocDosObject(DOS_FIB, NULL)) != NULL) {
        if (Examine(lock, fib)) {
            *type = fib->fib_DirEntryType;
            *protection = fib->fib_Protection;
            ok = 1;
        }
        FreeDosObject(DOS_FIB, fib);
    }
    UnLock(lock);
    return ok;
}

/**
 * @brief Tell whether something of a name exists.
 * @param path The path.
 * @return Non-zero if it can be locked.
 */
static int exists(const char *path)
{
    BPTR lock = Lock((STRPTR)path, ACCESS_READ);
    if (lock == 0) return 0;
    UnLock(lock);
    return 1;
}

/**
 * @brief Find the drawer new entries go into: the selected drawer, the
 *        drawer of the selected file, or the project's.
 * @param app The application state.
 * @param[out] out The drawer.
 * @param size Size of @p out.
 * @return Non-zero if there is one (a project is open).
 */
static int target_drawer(EditorApp *app, char *out, size_t size)
{
    LONG type = 0, protection = 0;
    if (selected_path(app, out, size)) {
        if (!examine(out, &type, &protection) || type <= 0) *PathPart((STRPTR)out) = '\0';
        return 1;
    }
    return tree_project_root(app, out, (LONG)size);
}

/**
 * @brief Check a name typed for a file or drawer and join it to a drawer.
 * @param app The application state (for the message).
 * @param drawer The drawer.
 * @param name The name.
 * @param[out] out The path.
 * @param size Size of @p out.
 * @return Non-zero if the name is usable; a message was shown otherwise.
 */
static int join_name(EditorApp *app, const char *drawer, const char *name, char *out, size_t size)
{
    if (name[0] == '\0' || strchr(name, ':') != NULL || strchr(name, '/') != NULL) {
        message(app, S(MSG_TREEOPS_BADNAME));
        return 0;
    }
    if (strlen(drawer) >= size) goto too_long;
    strcpy(out, drawer);
    if (AddPart((STRPTR)out, (STRPTR)name, (ULONG)size)) return 1;
too_long:
    message(app, S(MSG_ERR_PATHTOOLONG));
    return 0;
}

/**
 * @brief Tell whether a path is another one or lies inside it.
 * @param path The path tested.
 * @param base The other path (a file or drawer, not ending in '/').
 * @return The length of @p base if @p path is it or lies inside it, else 0.
 */
static size_t inside(const char *path, const char *base)
{
    size_t n = strlen(base);
    if (n == 0 || Strnicmp((STRPTR)path, (STRPTR)base, (LONG)n) != 0) return 0;
    if (path[n] != '\0' && path[n] != '/' && base[n - 1] != ':') return 0;
    return n;
}

/* ---- the name window ------------------------------------------------ */

/** Gadget IDs of the name window. */
enum {
    NG_NAME = 1,        /**< the name */
    NG_OK,              /**< OK */
    NG_CANCEL           /**< Cancel */
};

/**
 * @brief Drop the input of a window that waits under a modal one.
 * @param winobj The window (window.class).
 */
static void drop_input(Object *winobj)
{
    WORD code;
    while (DoMethod(winobj, WM_HANDLEINPUT, &code) != WMHI_LASTMSG) ;
}

/**
 * @brief A label.image showing a text as it is (a '_' in a path is no
 *        shortcut).
 * @param text The text.
 * @return The label.
 */
static Object *plain_label(const char *text)
{
    return NewObject(LABEL_GetClass(), NULL, LABEL_Underscore, 0, LABEL_Text, (ULONG)text, TAG_END);
}

/**
 * @brief A button as wide as its text.
 * @param id Gadget ID.
 * @param text Its text.
 * @return The gadget.
 */
static Object *text_button(ULONG id, const char *text)
{
    return NewObject(BUTTON_GetClass(), NULL,
        GA_ID, id, GA_RelVerify, TRUE, GA_Text, (ULONG)text,
        BUTTON_TextPadding, TRUE, TAG_END);
}

/**
 * @brief Ask for a name in a small modal window over the main window.
 *
 * Return in the field or OK takes the name; Cancel, Esc and the close
 * gadget do not.
 *
 * @param app The application state.
 * @param title The window's title.
 * @param where A line above the field (where the entry is made).
 * @param[in,out] name The name shown at first; the name typed.
 * @param size Size of @p name.
 * @return Non-zero if a name was taken.
 */
static int ask_name(EditorApp *app, const char *title, const char *where, char *name, size_t size)
{
    Object *winobj, *layout, *field;
    struct Window *win;
    ULONG sig = 0, mainsig = 0, result;
    WORD code;
    LONG chars;
    int done = 0, ok = 0, opened = 0;

    if (StringBase == NULL) {
        StringBase = OpenLibrary((STRPTR)"gadgets/string.gadget", 44);
        opened = StringBase != NULL;
    }
    if (StringBase == NULL) {
        path_message(app, MSG_TREEOPS_NEEDCLASS, "gadgets/string.gadget");
        return 0;
    }
    chars = TextLength(&app->screen->RastPort, (STRPTR)"n", 1);
    field = NewObject(STRING_GetClass(), NULL,
        GA_ID, NG_NAME, GA_RelVerify, TRUE, GA_TabCycle, TRUE,
        STRINGA_MaxChars, (ULONG)(size < NAME_MAX_CHARS ? size : NAME_MAX_CHARS),
        STRINGA_TextVal, (ULONG)name,
        STRINGA_BufferPos, (ULONG)strlen(name),
        TAG_END);
    layout = NewObject(LAYOUT_GetClass(), NULL,
        LAYOUT_Orientation, LAYOUT_ORIENT_VERT,
        LAYOUT_SpaceOuter, TRUE,
        LAYOUT_LeftSpacing, 6, LAYOUT_RightSpacing, 6,
        LAYOUT_TopSpacing, 6, LAYOUT_BottomSpacing, 6,
        LAYOUT_InnerSpacing, 6,
        LAYOUT_DeferLayout, TRUE,
        LAYOUT_AddChild, (ULONG)NewObject(LAYOUT_GetClass(), NULL,
            LAYOUT_Orientation, LAYOUT_ORIENT_VERT,
            LAYOUT_InnerSpacing, 4,
            LAYOUT_AddImage, (ULONG)plain_label(where),
            CHILD_WeightedHeight, 0,
            LAYOUT_AddChild, (ULONG)field,
            CHILD_WeightedHeight, 0,
            CHILD_MinWidth, (ULONG)(chars * 36),
            CHILD_Label, (ULONG)NewObject(LABEL_GetClass(), NULL,
                LABEL_Text, (ULONG)S(MSG_TREEOPS_NAME), TAG_END),
            TAG_END),
        LAYOUT_AddChild, (ULONG)NewObject(LAYOUT_GetClass(), NULL,
            LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ,
            LAYOUT_AddChild, (ULONG)text_button(NG_OK, S(MSG_PJE_OK)),
            CHILD_WeightedWidth, 0,
            LAYOUT_AddChild, (ULONG)NewObject(LAYOUT_GetClass(), NULL, TAG_END),
            LAYOUT_AddChild, (ULONG)text_button(NG_CANCEL, S(MSG_PJE_CANCEL)),
            CHILD_WeightedWidth, 0,
            TAG_END),
        CHILD_WeightedHeight, 0,
        TAG_END);
    winobj = layout == NULL ? NULL : NewObject(WINDOW_GetClass(), NULL,
        WA_Title, (ULONG)title,
        WA_PubScreen, (ULONG)app->screen,
        WA_Activate, TRUE,
        WA_DepthGadget, TRUE,
        WA_DragBar, TRUE,
        WA_CloseGadget, TRUE,
        WA_SizeGadget, TRUE,
        WA_IDCMP, IDCMP_RAWKEY,
        WINDOW_RefWindow, (ULONG)app->window,
        WINDOW_Position, WPOS_CENTERWINDOW,
        WINDOW_ParentGroup, (ULONG)layout,
        TAG_END);
    if (winobj == NULL) {
        if (layout != NULL) DisposeObject(layout);
        else if (field != NULL) DisposeObject(field);
        message(app, S(MSG_FIND_NOMEM));
        goto out;
    }
    if ((win = (struct Window *)DoMethod(winobj, WM_OPEN, NULL)) == NULL) {
        DisposeObject(winobj);
        message(app, S(MSG_FIND_NOMEM));
        goto out;
    }
    ActivateLayoutGadget((struct Gadget *)layout, win, NULL, (ULONG)field);

    SetAttrs(app->window_object, WA_BusyPointer, TRUE, TAG_END);
    GetAttr(WINDOW_SigMask, app->window_object, &mainsig);
    GetAttr(WINDOW_SigMask, winobj, &sig);
    while (!done) {
        ULONG got = Wait(sig | mainsig);
        if ((got & mainsig) != 0) drop_input(app->window_object);
        while ((result = DoMethod(winobj, WM_HANDLEINPUT, &code)) != WMHI_LASTMSG) {
            ULONG kind = result & WMHI_CLASSMASK, id = result & WMHI_GADGETMASK;
            if (kind == WMHI_CLOSEWINDOW) done = 1;
            else if (kind == WMHI_RAWKEY && (result & WMHI_KEYMASK) == RAWKEY_ESC) done = 1;
            else if (kind == WMHI_GADGETUP && id == NG_CANCEL) done = 1;
            /* Return in the field or OK; Tab (code 9) only moves on */
            else if (kind == WMHI_GADGETUP && (id == NG_OK || (id == NG_NAME && code != 9))) {
                STRPTR text = NULL;
                GetAttr(STRINGA_TextVal, field, (ULONG *)&text);
                strncpy(name, text != NULL ? (const char *)text : "", size - 1);
                name[size - 1] = '\0';
                done = ok = 1;
            }
        }
    }
    SetAttrs(app->window_object, WA_BusyPointer, FALSE, TAG_END);
    DisposeObject(winobj);                      /* the gadgets with it */
out:
    if (opened) {
        CloseLibrary(StringBase);
        StringBase = NULL;
    }
    return ok;
}

/* ---- open documents ------------------------------------------------- */

/**
 * @brief Let the open documents of a renamed file or drawer follow its new
 *        name, keeping whether they are changed.
 * @param app The application state.
 * @param old The old path.
 * @param new_path The new path.
 */
static void documents_renamed(EditorApp *app, const char *old, const char *new_path)
{
    Document *doc;
    char path[EDITOR_PATH_MAX];
    int moved = 0;
    for (doc = (Document *)app->documents.lh_Head; doc->node.ln_Succ;
         doc = (Document *)doc->node.ln_Succ) {
        size_t n;
        int dirty;
        if (doc->path == NULL || (n = inside(doc->path, old)) == 0) continue;
        if (snprintf(path, sizeof(path), "%s%s", new_path, doc->path + n) >= (int)sizeof(path)) continue;
        dirty = doc->dirty;
        document_set_path(app, doc, path);      /* shows it unchanged */
        if (dirty) document_set_dirty(app, doc, 1);
        else scoutlink_document_saved(doc);     /* the file under its new name */
        moved = 1;
    }
    if (moved) bookmarks_save(app);
}

/**
 * @brief Mark the open documents of deleted files as changed, so closing
 *        them asks about saving.
 * @param app The application state.
 * @param path The deleted file or drawer.
 */
static void documents_deleted(EditorApp *app, const char *path)
{
    Document *doc;
    for (doc = (Document *)app->documents.lh_Head; doc->node.ln_Succ;
         doc = (Document *)doc->node.ln_Succ)
        if (doc->path != NULL && inside(doc->path, path) != 0) document_set_dirty(app, doc, 1);
}

/* ---- walking a drawer ----------------------------------------------- */

/** Why walking a drawer stopped. */
enum {
    WALK_OK = 0,        /**< it did not */
    WALK_DOS,           /**< a DOS call failed (WalkState::error) */
    WALK_PROTECTED,     /**< an entry is protected from deletion */
    WALK_DEEP,          /**< drawers nested deeper than WALK_DEPTH */
    WALK_LONG,          /**< a path got too long */
    WALK_NOMEM          /**< out of memory */
};

/** State of counting or deleting what a drawer holds. */
typedef struct WalkState {
    char path[EDITOR_PATH_MAX];     /**< the entry at hand */
    char failed[EDITOR_PATH_MAX];   /**< where it stopped */
    long files;                     /**< files counted (without icons) */
    long drawers;                   /**< drawers counted */
    int remove;                     /**< delete instead of counting */
    int problem;                    /**< WALK_ value */
    LONG error;                     /**< the DOS error of WALK_DOS */
} WalkState;

/**
 * @brief Note why the walk stops, and where.
 * @param w The walk.
 * @param problem The WALK_ value.
 * @param error The DOS error, or 0.
 * @return 0, to be returned by the walk.
 */
static int walk_fail(WalkState *w, int problem, LONG error)
{
    strcpy(w->failed, w->path);
    w->problem = problem;
    w->error = error;
    return 0;
}

/**
 * @brief Tell whether a name is that of an icon.
 * @param name The name.
 * @return Non-zero if it ends with ".info".
 */
static int is_icon(const char *name)
{
    size_t n = strlen(name);
    return n > 5 && Stricmp((STRPTR)(name + n - 5), (STRPTR)".info") == 0;
}

/**
 * @brief Count or delete what the drawer WalkState::path holds, drawers
 *        below it first.
 *
 * The names of a drawer are read completely before anything in it is
 * deleted (deleting while ExNext() goes on is not safe). Links are not
 * followed: a link to a drawer is deleted as a link. Counting stops at an
 * entry protected from deletion.
 *
 * @param w The walk (path: the drawer; restored on return).
 * @param depth The drawer's level below the deleted one.
 * @return Non-zero on success, 0 if it stopped (WalkState::problem).
 */
static int walk(WalkState *w, int depth)
{
    struct FileInfoBlock *fib;
    BPTR lock;
    char *names = NULL, *p;
    size_t used = 0, room = 0, len = strlen(w->path);
    LONG error;
    int ok = 1;

    if (depth >= WALK_DEPTH) return walk_fail(w, WALK_DEEP, 0);
    if ((lock = Lock((STRPTR)w->path, ACCESS_READ)) == 0) return walk_fail(w, WALK_DOS, IoErr());
    if ((fib = AllocDosObject(DOS_FIB, NULL)) == NULL) {
        UnLock(lock);
        return walk_fail(w, WALK_NOMEM, 0);
    }
    /* the names, each as a type byte ('d' drawer, 'f' anything else;
     * upper case if protected from deletion) and the name with its 0 */
    if (!Examine(lock, fib)) ok = walk_fail(w, WALK_DOS, IoErr());
    while (ok && ExNext(lock, fib)) {
        size_t n = strlen((const char *)fib->fib_FileName) + 2;
        char type = fib->fib_DirEntryType == ST_USERDIR ? 'd' : 'f';
        if ((fib->fib_Protection & FIBF_DELETE) != 0) type = (char)(type - 'a' + 'A');
        if (used + n > room) {
            size_t grown = room == 0 ? 1024 : room * 2;
            char *more;
            while (grown < used + n) grown *= 2;
            if ((more = realloc(names, grown)) == NULL) { ok = walk_fail(w, WALK_NOMEM, 0); break; }
            names = more;
            room = grown;
        }
        names[used] = type;
        strcpy(names + used + 1, (const char *)fib->fib_FileName);
        used += n;
    }
    if (ok && (error = IoErr()) != ERROR_NO_MORE_ENTRIES) ok = walk_fail(w, WALK_DOS, error);
    FreeDosObject(DOS_FIB, fib);
    UnLock(lock);

    for (p = names; ok && p < names + used; p += strlen(p + 1) + 2) {
        char type = p[0];
        if (!AddPart((STRPTR)w->path, (STRPTR)(p + 1), sizeof(w->path))) { ok = walk_fail(w, WALK_LONG, 0); break; }
        if (!w->remove && (type == 'D' || type == 'F')) ok = walk_fail(w, WALK_PROTECTED, 0);
        else if (type == 'd' || type == 'D') {
            ok = walk(w, depth + 1);
            if (ok && w->remove && !DeleteFile((STRPTR)w->path)) ok = walk_fail(w, WALK_DOS, IoErr());
            if (ok && !w->remove) ++w->drawers;
        }
        else if (w->remove) {
            if (!DeleteFile((STRPTR)w->path)) ok = walk_fail(w, WALK_DOS, IoErr());
        }
        else if (!is_icon(p + 1)) ++w->files;
        w->path[len] = '\0';
    }
    free(names);
    return ok;
}

/**
 * @brief Report why walking a drawer stopped.
 * @param app The application state.
 * @param w The walk.
 */
static void walk_message(EditorApp *app, const WalkState *w)
{
    switch (w->problem) {
        case WALK_DOS:
            dos_message(app, w->remove ? MSG_TREEOPS_ERR_DELETE : MSG_TREEOPS_ERR_READ, w->failed, w->error);
            break;
        case WALK_PROTECTED: path_message(app, MSG_TREEOPS_PROTECTED, w->failed); break;
        case WALK_DEEP: path_message(app, MSG_TREEOPS_TOODEEP, w->failed); break;
        case WALK_LONG: message(app, S(MSG_ERR_PATHTOOLONG)); break;
        default: message(app, S(MSG_FIND_NOMEM)); break;
    }
}

/* ---- the operations ------------------------------------------------- */

/**
 * @brief Ask for the name of a new entry in the target drawer and make
 *        the path.
 * @param app The application state.
 * @param title The window's title (MSG_).
 * @param[out] path The path of the new entry.
 * @param size Size of @p path.
 * @return Non-zero if a usable name for an entry that does not exist yet
 *         was given.
 */
static int ask_new(EditorApp *app, LONG title, char *path, size_t size)
{
    char drawer[EDITOR_PATH_MAX], where[EDITOR_PATH_MAX + 64], name[NAME_MAX_CHARS] = "";
    if (!target_drawer(app, drawer, sizeof(drawer))) {
        message(app, S(MSG_TREEOPS_NOPROJECT));
        return 0;
    }
    snprintf(where, sizeof(where), S(MSG_TREEOPS_IN), drawer);
    if (!ask_name(app, S(title), where, name, sizeof(name))) return 0;
    if (!join_name(app, drawer, name, path, size)) return 0;
    if (exists(path)) {
        path_message(app, MSG_TREEOPS_EXISTS, path);
        return 0;
    }
    return 1;
}

void treeops_new_file(EditorApp *app)
{
    char path[EDITOR_PATH_MAX];
    BPTR fh;
    if (!ask_new(app, MSG_TREEOPS_NEWFILE_TITLE, path, sizeof(path))) return;
    if ((fh = Open((STRPTR)path, MODE_NEWFILE)) == 0) {
        dos_message(app, MSG_TREEOPS_ERR_CREATE, path, IoErr());
        return;
    }
    Close(fh);
    document_open(app, path);
}

void treeops_new_drawer(EditorApp *app)
{
    char path[EDITOR_PATH_MAX];
    BPTR lock;
    if (!ask_new(app, MSG_TREEOPS_NEWDRAWER_TITLE, path, sizeof(path))) return;
    if ((lock = CreateDir((STRPTR)path)) == 0) {
        dos_message(app, MSG_TREEOPS_ERR_CREATE, path, IoErr());
        return;
    }
    UnLock(lock);
}

void treeops_rename(EditorApp *app)
{
    char old[EDITOR_PATH_MAX], drawer[EDITOR_PATH_MAX], path[EDITOR_PATH_MAX];
    char where[EDITOR_PATH_MAX + 64], name[NAME_MAX_CHARS];
    char old_icon[EDITOR_PATH_MAX + 8], new_icon[EDITOR_PATH_MAX + 8];
    const char *old_name;
    if (!selected_path(app, old, sizeof(old))) {
        message(app, S(MSG_TREEOPS_NOSELECTION));
        return;
    }
    old_name = (const char *)FilePart((STRPTR)old);
    strncpy(name, old_name, sizeof(name) - 1);
    name[sizeof(name) - 1] = '\0';
    strcpy(drawer, old);
    *PathPart((STRPTR)drawer) = '\0';
    snprintf(where, sizeof(where), S(MSG_TREEOPS_RENAME_WHAT), old);
    if (!ask_name(app, S(MSG_TREEOPS_RENAME_TITLE), where, name, sizeof(name))) return;
    if (strcmp(name, old_name) == 0) return;
    if (!join_name(app, drawer, name, path, sizeof(path))) return;
    /* only the case changes: the "existing" entry is the one renamed */
    if (Stricmp((STRPTR)name, (STRPTR)old_name) != 0 && exists(path)) {
        path_message(app, MSG_TREEOPS_EXISTS, path);
        return;
    }
    if (!Rename((STRPTR)old, (STRPTR)path)) {
        dos_message(app, MSG_TREEOPS_ERR_RENAME, old, IoErr());
        return;
    }
    /* the icon goes along; one that cannot be renamed stays behind */
    snprintf(old_icon, sizeof(old_icon), "%s.info", old);
    snprintf(new_icon, sizeof(new_icon), "%s.info", path);
    if (exists(old_icon) && !exists(new_icon)) Rename((STRPTR)old_icon, (STRPTR)new_icon);
    documents_renamed(app, old, path);
}

void treeops_delete(EditorApp *app)
{
    char path[EDITOR_PATH_MAX], text[EDITOR_PATH_MAX + 200], icon[EDITOR_PATH_MAX + 8];
    WalkState *w;
    LONG type = 0, protection = 0;
    int drawer;
    struct EasyStruct es = {sizeof(es), 0, NULL, (STRPTR)"%s", NULL};

    if (!selected_path(app, path, sizeof(path))) {
        message(app, S(MSG_TREEOPS_NOSELECTION));
        return;
    }
    if (!examine(path, &type, &protection)) {
        dos_message(app, MSG_TREEOPS_ERR_READ, path, IoErr());
        return;
    }
    if ((protection & FIBF_DELETE) != 0) {
        path_message(app, MSG_TREEOPS_PROTECTED, path);
        return;
    }
    if ((w = AllocVec(sizeof(*w), MEMF_CLEAR)) == NULL) {
        message(app, S(MSG_FIND_NOMEM));
        return;
    }
    drawer = type == ST_USERDIR;        /* a link to a drawer is deleted as a link */
    strcpy(w->path, path);
    SetAttrs(app->window_object, WA_BusyPointer, TRUE, TAG_END);
    if (drawer && !walk(w, 0)) {
        SetAttrs(app->window_object, WA_BusyPointer, FALSE, TAG_END);
        walk_message(app, w);
        FreeVec(w);
        return;
    }
    SetAttrs(app->window_object, WA_BusyPointer, FALSE, TAG_END);

    if (!drawer) snprintf(text, sizeof(text), S(MSG_TREEOPS_DEL_FILE), path);
    else if (w->files == 0 && w->drawers == 0) snprintf(text, sizeof(text), S(MSG_TREEOPS_DEL_DRAWER), path);
    else snprintf(text, sizeof(text), S(MSG_TREEOPS_DEL_FULL), path, w->files, w->drawers);
    es.es_Title = (STRPTR)S(MSG_TREEOPS_DEL_TITLE);
    es.es_GadgetFormat = (STRPTR)S(MSG_TREEOPS_DEL_GADGETS);
    if (EasyRequest(app->window, &es, NULL, (ULONG)text) != 1) {
        FreeVec(w);
        return;
    }

    SetAttrs(app->window_object, WA_BusyPointer, TRUE, TAG_END);
    w->remove = 1;
    if (drawer && !walk(w, 0)) {
        SetAttrs(app->window_object, WA_BusyPointer, FALSE, TAG_END);
        walk_message(app, w);
        documents_deleted(app, path);           /* some of it may be gone */
        FreeVec(w);
        return;
    }
    if (!DeleteFile((STRPTR)path)) {
        LONG error = IoErr();
        SetAttrs(app->window_object, WA_BusyPointer, FALSE, TAG_END);
        dos_message(app, MSG_TREEOPS_ERR_DELETE, path, error);
        if (drawer) documents_deleted(app, path);
        FreeVec(w);
        return;
    }
    snprintf(icon, sizeof(icon), "%s.info", path);
    DeleteFile((STRPTR)icon);                   /* if it has one */
    SetAttrs(app->window_object, WA_BusyPointer, FALSE, TAG_END);
    documents_deleted(app, path);
    FreeVec(w);
}

/* ---- the buttons ---------------------------------------------------- */

/**
 * @brief Tell whether a button does something in a state.
 * @param i The button's number.
 * @param state TS_ bits.
 * @return Non-zero if it is not ghosted.
 */
static int button_on(int i, int state)
{
    return i < 2 ? (state & TS_PROJECT) != 0 : (state & TS_SELECTED) != 0;
}

Object *treeops_create(EditorApp *app)
{
    Object *row, *column, *page;
    int i;
    memset(&to, 0, sizeof(to));
    for (i = 0; i < TREEOPS_BUTTONS; ++i) {
        if ((to.images[i] = tmplsym_image_new(1, TMPLSYM_SIZE, TMPLSYM_SIZE)) == NULL) goto failed;
        SetAttrs(to.images[i], TMPLSYM_Type, (ULONG)symbols[i], TAG_END);
        to.buttons[i] = NewObject(BUTTON_GetClass(), NULL,
            GA_ID, (ULONG)(GID_TREE_NEW_FILE + i),
            GA_RelVerify, TRUE,
            GA_Disabled, TRUE,              /* no project yet (to.state 0) */
            BUTTON_RenderImage, (ULONG)to.images[i],
            /* frames or none like the toolbar's (TOOLBARFRAMES) */
            BUTTON_BevelStyle, (ULONG)(app->settings.tbframes ? BVS_BUTTON : BVS_NONE),
            TAG_END);
        if (to.buttons[i] == NULL) goto failed;
    }
    row = NewObject(LAYOUT_GetClass(), NULL,
        LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ,
        LAYOUT_BevelStyle, BVS_THIN,
        LAYOUT_LeftSpacing, ROW_INSET,
        LAYOUT_RightSpacing, ROW_INSET,
        LAYOUT_TopSpacing, ROW_INSET,
        LAYOUT_BottomSpacing, ROW_INSET,
        LAYOUT_InnerSpacing, 2,
        LAYOUT_AddChild, (ULONG)to.buttons[0], CHILD_WeightedWidth, 0,
        LAYOUT_AddChild, (ULONG)to.buttons[1], CHILD_WeightedWidth, 0,
        LAYOUT_AddChild, (ULONG)to.buttons[2], CHILD_WeightedWidth, 0,
        LAYOUT_AddChild, (ULONG)to.buttons[3], CHILD_WeightedWidth, 0,
        /* takes the rest, so the buttons stay on the left */
        LAYOUT_AddChild, (ULONG)NewObject(LAYOUT_GetClass(), NULL, TAG_END),
        TAG_END);
    if (row == NULL) goto failed;
    /* no space between them: the row's frame adjoins the tree's */
    column = NewObject(LAYOUT_GetClass(), NULL,
        LAYOUT_Orientation, LAYOUT_ORIENT_VERT,
        LAYOUT_InnerSpacing, 0,
        LAYOUT_AddChild, (ULONG)row,
        CHILD_WeightedHeight, 0,
        CHILD_MinWidth, 0,
        LAYOUT_AddChild, (ULONG)app->tree,
        /* the column's least width comes from the content layout; the
         * children's own would keep the hidden column from collapsing */
        CHILD_MinWidth, 0,
        TAG_END);
    if (column == NULL) {
        DisposeObject(row);                     /* the buttons with it */
        memset(to.buttons, 0, sizeof(to.buttons));
        goto failed;
    }
    /* Hiding the tree shows the empty second page and collapses the page
     * to no width (tree_set_visible()). Collapsed alone, the column kept
     * drawing its buttons over the tabs, and taking them out of the layout
     * and adding them again at run time left them misplaced; page.gadget
     * draws only the page shown. */
    page = NewObject(PAGE_GetClass(), NULL,
        PAGE_Add, (ULONG)column,
        PAGE_Add, (ULONG)NewObject(LAYOUT_GetClass(), NULL, TAG_END),
        PAGE_Current, 0,
        TAG_END);
    if (page == NULL) {
        SetAttrs(column, LAYOUT_RemoveChild, (ULONG)app->tree, TAG_END);  /* not the tree */
        DisposeObject(column);
        memset(to.buttons, 0, sizeof(to.buttons));
        treeops_dispose();
        return NULL;
    }
    app->tree_column = page;
    return page;
failed:
    for (i = 0; i < TREEOPS_BUTTONS; ++i)
        if (to.buttons[i] != NULL) DisposeObject(to.buttons[i]);
    memset(to.buttons, 0, sizeof(to.buttons));
    treeops_dispose();
    return NULL;
}

void treeops_hints(struct HintInfo *h)
{
    int i;
    for (i = 0; i < TREEOPS_BUTTONS; ++i) {
        h[i].hi_GadgetID = (WORD)(GID_TREE_NEW_FILE + i);
        h[i].hi_Code = -1;
        h[i].hi_Text = (STRPTR)S(hints[i]);
        h[i].hi_Flags = 0;
    }
}

void treeops_update(EditorApp *app)
{
    STRPTR root = NULL, selected = NULL;
    int state = 0, i;
    if (app->tree_column == NULL) return;
    /* runs after every batch of input: only the gadget's attributes, no
     * DOS calls (the operations check the selection again) */
    GetAttr(DIRTREE_Root, app->tree, (ULONG *)&root);
    GetAttr(DIRTREE_SelectedPath, app->tree, (ULONG *)&selected);
    if (root != NULL && root[0] != '\0') {
        state = TS_PROJECT;
        if (selected != NULL && selected[0] != '\0') state |= TS_SELECTED;
    }
    if (state == to.state) return;
    to.state = state;
    /* through the page: on the hidden one the buttons must not draw */
    for (i = 0; i < TREEOPS_BUTTONS; ++i)
        if (app->window != NULL)
            SetPageGadgetAttrs((struct Gadget *)to.buttons[i], app->tree_column, app->window, NULL,
                               GA_Disabled, (ULONG)!button_on(i, state), TAG_END);
        else SetAttrs(to.buttons[i], GA_Disabled, (ULONG)!button_on(i, state), TAG_END);
}

void treeops_dispose(void)
{
    int i;
    for (i = 0; i < TREEOPS_BUTTONS; ++i)
        if (to.images[i] != NULL) {
            DisposeObject(to.images[i]);
            to.images[i] = NULL;
        }
    memset(to.buttons, 0, sizeof(to.buttons));
}
