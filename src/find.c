/*
 * find.c - the find and replace window
 *
 * A window that stays open next to the main window, built like Kickdown's:
 * search text, replacement, case sensitive, whole words, regular
 * expression, backwards, wrap around; Find, Replace, Replace all, Close. Edit/Find next repeats the
 * last search without the window. It works on the active document.
 *
 * Unlike Kickdown the search does not use GM_TEXTEDITOR_Search: the text
 * is exported from the editor and searched by search.c (plain text or
 * POSIX extended regular expressions; \0 to \9 in the replacement stand
 * for the groups). A hit is
 * marked with GM_TEXTEDITOR_MarkText and replaced with
 * GM_TEXTEDITOR_Replace. Replace only replaces the last hit (its block is
 * remembered), not any other selection. Replace all is one undo step
 * (BEGINMULTICHANGE/ENDMULTICHANGE).
 */
#include "editor.h"
#include "search.h"

#include <intuition/gadgetclass.h>
#include <classes/window.h>
#include <gadgets/layout.h>
#include <gadgets/button.h>
#include <gadgets/checkbox.h>
#include <gadgets/string.h>
#include <gadgets/texteditor.h>
#include <images/label.h>
#include <images/bevel.h>

#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/window.h>
#include <proto/layout.h>
#include <proto/button.h>
#include <proto/checkbox.h>
#include <proto/string.h>
#include <proto/label.h>
#include <clib/alib_protos.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct Library *StringBase = NULL;
extern struct Library *CheckBoxBase;    /* shared with the settings window */

enum {
    FG_FIND = 300, FG_REPLACE, FG_CASE, FG_WORDS, FG_REGEX, FG_BACKWARDS, FG_WRAP,
    FG_DOFIND, FG_DOREPLACE, FG_REPLACEALL, FG_CLOSE, FG_STATUS
};

#define MAXSEARCH 200                   /* length of the text fields */

/** State of the find window (there is one). */
static struct {
    Object *winobj, *root, *find, *replace, *casesens, *words, *regex, *backwards, *wrap, *status;
    struct Window *win;
    int focus;                          /* activate the search field when active */
    char text[MAXSEARCH + 1];           /* last search, also for Find next */
    char with[MAXSEARCH + 1];           /* last replacement */
    int casesens_on, words_on, regex_on, backwards_on, wrap_on;
    Document *hit_doc;                  /* the last hit was marked in this document ... */
    SearchHit hit;                      /* ... here (as found) ... */
    ULONG hx0, hy0, hx1, hy1;           /* ... and the editor's block of it */
    char message[MAXSEARCH + 40];       /* the status text */
} fw = { .wrap_on = 1 };

/**
 * @brief Open the classes the window needs (once).
 * @param app The application state (for the requester).
 * @return Non-zero if all are available.
 */
static int open_classes(EditorApp *app)
{
    if (CheckBoxBase == NULL) CheckBoxBase = OpenLibrary((STRPTR)"gadgets/checkbox.gadget", 44);
    if (StringBase == NULL) StringBase = OpenLibrary((STRPTR)"gadgets/string.gadget", 44);
    if (CheckBoxBase != NULL && StringBase != NULL) return 1;
    ui_error(app, APP_NAME, "Find and replace needs checkbox.gadget and string.gadget.");
    return 0;
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
 * @return The gadget.
 */
static Object *string(ULONG id, const char *text)
{
    return NewObject(STRING_GetClass(), NULL,
        GA_ID, id, GA_RelVerify, TRUE, GA_TabCycle, TRUE,
        STRINGA_MaxChars, MAXSEARCH, STRINGA_TextVal, (ULONG)text, TAG_END);
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
 * @brief Show a text in the window's status line (nothing if it is closed).
 * @param text The text.
 */
static void status(const char *text)
{
    if (text != fw.message) {
        strncpy(fw.message, text, sizeof(fw.message) - 1);
        fw.message[sizeof(fw.message) - 1] = '\0';
    }
    if (fw.win != NULL)
        SetGadgetAttrs((struct Gadget *)fw.status, fw.win, NULL, GA_Text, (ULONG)fw.message, TAG_END);
}

/**
 * @brief Build the window object.
 * @param app The application state.
 * @return Non-zero on success.
 */
static int build(EditorApp *app)
{
    fw.find = string(FG_FIND, fw.text);
    fw.replace = string(FG_REPLACE, fw.with);
    fw.casesens = checkbox(FG_CASE, "_Case sensitive", fw.casesens_on);
    fw.words = checkbox(FG_WORDS, "Whole wor_ds", fw.words_on);
    fw.regex = checkbox(FG_REGEX, "Regular e_xpression", fw.regex_on);
    fw.backwards = checkbox(FG_BACKWARDS, "_Backwards", fw.backwards_on);
    fw.wrap = checkbox(FG_WRAP, "Wrap _around", fw.wrap_on);
    fw.status = NewObject(BUTTON_GetClass(), NULL,
        GA_ID, FG_STATUS, GA_ReadOnly, TRUE,
        GA_Underscore, 0,                   /* the search text may contain '_' */
        GA_Text, (ULONG)"",
        BUTTON_Justification, BCJ_LEFT, BUTTON_BevelStyle, BVS_NONE,
        BUTTON_Transparent, TRUE,           /* the window's backfill shows through */
        TAG_END);

    fw.winobj = NewObject(WINDOW_GetClass(), NULL,
        WA_Title,          (ULONG)"Find and replace",
        WA_PubScreen,      (ULONG)app->screen,
        WA_Activate,       TRUE,
        WA_DepthGadget,    TRUE,
        WA_DragBar,        TRUE,
        WA_CloseGadget,    TRUE,
        WA_SizeGadget,     TRUE,
        WINDOW_RefWindow,  (ULONG)app->window,
        WINDOW_Position,   WPOS_CENTERWINDOW,
        WA_IDCMP,          IDCMP_ACTIVEWINDOW,
        WINDOW_ParentGroup, (ULONG)(fw.root = NewObject(LAYOUT_GetClass(), NULL,
            LAYOUT_Orientation,   LAYOUT_ORIENT_VERT,
            LAYOUT_SpaceOuter,    TRUE,
            LAYOUT_LeftSpacing,   6,
            LAYOUT_RightSpacing,  6,
            LAYOUT_TopSpacing,    6,
            LAYOUT_BottomSpacing, 6,
            LAYOUT_InnerSpacing,  4,
            LAYOUT_DeferLayout,   TRUE,
            /* the rows keep their height and stay together at the top,
             * an empty group takes the rest (like the settings window) */
            LAYOUT_AddChild, (ULONG)NewObject(LAYOUT_GetClass(), NULL,
                LAYOUT_Orientation,  LAYOUT_ORIENT_VERT,
                LAYOUT_InnerSpacing, 4,
                LAYOUT_AddChild, (ULONG)fw.find,
                CHILD_Label,     (ULONG)label("_Find "),
                CHILD_WeightedHeight, 0,
                LAYOUT_AddChild, (ULONG)fw.replace,
                CHILD_Label,     (ULONG)label("Replace _with "),
                CHILD_WeightedHeight, 0,
                LAYOUT_AddChild, (ULONG)NewObject(LAYOUT_GetClass(), NULL,
                    LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ,
                    LAYOUT_AddChild,    (ULONG)fw.casesens,
                    LAYOUT_AddChild,    (ULONG)fw.words,
                    LAYOUT_AddChild,    (ULONG)fw.regex,
                    TAG_END),
                CHILD_WeightedHeight, 0,
                LAYOUT_AddChild, (ULONG)NewObject(LAYOUT_GetClass(), NULL,
                    LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ,
                    LAYOUT_AddChild,    (ULONG)fw.backwards,
                    LAYOUT_AddChild,    (ULONG)fw.wrap,
                    TAG_END),
                CHILD_WeightedHeight, 0,
                LAYOUT_AddChild, (ULONG)fw.status,
                CHILD_WeightedHeight, 0,
                TAG_END),
            CHILD_WeightedHeight, 0,
            LAYOUT_AddChild, (ULONG)NewObject(LAYOUT_GetClass(), NULL, TAG_END),
            /* the actions left, Close right, each as wide as its text */
            LAYOUT_AddChild, (ULONG)NewObject(LAYOUT_GetClass(), NULL,
                LAYOUT_Orientation,  LAYOUT_ORIENT_HORIZ,
                LAYOUT_AddChild,     (ULONG)button(FG_DOFIND, "Fi_nd"),
                CHILD_WeightedWidth, 0,
                LAYOUT_AddChild,     (ULONG)button(FG_DOREPLACE, "_Replace"),
                CHILD_WeightedWidth, 0,
                LAYOUT_AddChild,     (ULONG)button(FG_REPLACEALL, "Replace a_ll"),
                CHILD_WeightedWidth, 0,
                LAYOUT_AddChild,     (ULONG)NewObject(LAYOUT_GetClass(), NULL, TAG_END),
                LAYOUT_AddChild,     (ULONG)button(FG_CLOSE, "Cl_ose"),
                CHILD_WeightedWidth, 0,
                TAG_END),
            CHILD_WeightedHeight, 0,
            TAG_END)),
        TAG_END);
    return fw.winobj != NULL;
}

/** @brief Take the texts and options from the gadgets (if the window exists). */
static void read_gadgets(void)
{
    STRPTR s;
    if (fw.winobj == NULL) return;
    fw.text[0] = fw.with[0] = '\0';
    if ((s = (STRPTR)get(fw.find, STRINGA_TextVal)) != NULL) strncat(fw.text, (const char *)s, MAXSEARCH);
    if ((s = (STRPTR)get(fw.replace, STRINGA_TextVal)) != NULL) strncat(fw.with, (const char *)s, MAXSEARCH);
    fw.casesens_on = get(fw.casesens, CHECKBOX_Checked) != 0;
    fw.words_on = get(fw.words, CHECKBOX_Checked) != 0;
    fw.regex_on = get(fw.regex, CHECKBOX_Checked) != 0;
    fw.backwards_on = get(fw.backwards, CHECKBOX_Checked) != 0;
    fw.wrap_on = get(fw.wrap, CHECKBOX_Checked) != 0;
}

/**
 * @brief Prepare the search from the current texts and options; shows an
 *        error in the status line.
 * @param p The pattern; free it with search_free().
 * @return Non-zero on success.
 */
static int compile(SearchPattern *p)
{
    SearchOptions o;
    int error;
    memset(&o, 0, sizeof(o));
    o.case_sensitive = fw.casesens_on;
    o.whole_word = fw.words_on;
    o.regex = fw.regex_on;
    error = search_compile(p, fw.text, &o);
    if (error == SEARCH_OK) return 1;
    if (error != SEARCH_EMPTY) status(p->error);
    search_free(p);
    return 0;
}

/**
 * @brief Export the text of a document's editor.
 * @param doc The document.
 * @param size Set to the text's length.
 * @return The text (FreeVec() it), or NULL.
 */
static char *export_text(Document *doc, long *size)
{
    char *text = (char *)DoMethod(doc->editor, GM_TEXTEDITOR_ExportText, NULL);
    *size = text != NULL ? (long)strlen(text) : 0;
    return text;
}

/**
 * @brief Read the editor's marked block.
 * @param app The application state.
 * @param doc The document.
 * @param x0 Start column.
 * @param y0 Start line.
 * @param x1 End column.
 * @param y1 End line.
 */
static void block(EditorApp *app, Document *doc, ULONG *x0, ULONG *y0, ULONG *x1, ULONG *y1)
{
    *x0 = *y0 = *x1 = *y1 = ~0UL;
    DoGadgetMethod((struct Gadget *)doc->editor, app->window, NULL, GM_TEXTEDITOR_BlockInfo, 0,
                   (ULONG)x0, (ULONG)y0, (ULONG)x1, (ULONG)y1);
}

/**
 * @brief Tell whether the last hit is still marked as it was found.
 * @param app The application state.
 * @return Non-zero if it is.
 */
static int hit_marked(EditorApp *app)
{
    ULONG x0, y0, x1, y1;
    Document *doc = app->active;
    if (doc == NULL || fw.hit_doc != doc || !get(doc->editor, GA_TEXTEDITOR_AreaMarked)) return 0;
    block(app, doc, &x0, &y0, &x1, &y1);
    return x0 == fw.hx0 && y0 == fw.hy0 && x1 == fw.hx1 && y1 == fw.hy1;
}

/**
 * @brief Mark a hit in the active document (the cursor goes there first,
 *        so the editor shows it) and remember it.
 * @param app The application state.
 * @param hit The hit.
 */
static void mark_hit(EditorApp *app, const SearchHit *hit)
{
    Document *doc = app->active;
    SetGadgetAttrs((struct Gadget *)doc->editor, app->window, NULL,
        GA_TEXTEDITOR_CursorY, (ULONG)hit->line,
        GA_TEXTEDITOR_CursorX, (ULONG)hit->col, TAG_END);
    DoGadgetMethod((struct Gadget *)doc->editor, app->window, NULL, GM_TEXTEDITOR_MarkText, 0,
                   (ULONG)hit->col, (ULONG)hit->line,
                   (ULONG)(hit->col + hit->length), (ULONG)hit->line);
    fw.hit_doc = doc;
    fw.hit = *hit;
    block(app, doc, &fw.hx0, &fw.hy0, &fw.hx1, &fw.hy1);
}

/**
 * @brief Where the next search starts: after (or before) the last hit
 *        while it is marked, otherwise at the cursor.
 * @param app The application state.
 * @return The position.
 */
static SearchPos start_position(EditorApp *app)
{
    SearchPos pos;
    if (hit_marked(app)) {
        pos.line = fw.hit.line;
        pos.col = fw.hit.col;
        if (!fw.backwards_on) pos.col += fw.hit.length > 0 ? fw.hit.length : 1;
    } else {
        pos.line = (long)get(app->active->editor, GA_TEXTEDITOR_CursorY);
        pos.col = (long)get(app->active->editor, GA_TEXTEDITOR_CursorX);
    }
    return pos;
}

/**
 * @brief Search the active document from a position and mark the hit.
 * @param app The application state.
 * @param from The start position.
 * @return Non-zero if found.
 */
static int find_from(EditorApp *app, SearchPos from)
{
    SearchPattern p;
    SearchHit hit;
    char *text;
    long size;
    int found, wrapped = 0;

    fw.hit_doc = NULL;
    if (app->active == NULL || app->window == NULL || !compile(&p)) return 0;
    text = export_text(app->active, &size);
    if (text == NULL) {
        search_free(&p);
        status("The text could not be read");
        return 0;
    }
    found = search_text(&p, text, size, from, fw.backwards_on, fw.wrap_on, &hit, &wrapped);
    FreeVec(text);
    search_free(&p);
    if (found) {
        mark_hit(app, &hit);
        status(wrapped ? "Wrapped around" : "");
    } else {
        snprintf(fw.message, sizeof(fw.message), "\"%s\" not found", fw.text);
        status(fw.message);
    }
    return found;
}

/**
 * @brief Find: the next hit from the last one or the cursor.
 * @param app The application state.
 */
static void do_find(EditorApp *app)
{
    read_gadgets();
    if (fw.text[0] == '\0' || app->active == NULL) return;
    find_from(app, start_position(app));
}

/**
 * @brief Replace: replace the last hit if it is still marked, then find
 *        the next one.
 * @param app The application state.
 */
static void do_replace(EditorApp *app)
{
    SearchPattern p;
    SearchHit hit;
    SearchMatch m;
    SearchPos pos;
    char *text, *with;
    long size, length;
    int found = 0;

    read_gadgets();
    if (fw.text[0] == '\0' || app->active == NULL) return;
    if (!hit_marked(app)) {
        do_find(app);
        return;
    }
    /* the hit again, for the groups of the replacement */
    if (!compile(&p)) return;
    text = export_text(app->active, &size);
    pos.line = fw.hit.line;
    pos.col = fw.hit.col;
    if (text != NULL)
        found = search_text(&p, text, size, pos, 0, 0, &hit, NULL) &&
                hit.line == fw.hit.line && hit.col == fw.hit.col &&
                search_line_next(&p, text + hit.line_offset, hit.line_length, hit.col, &m);
    if (!found) {
        if (text != NULL) FreeVec(text);
        search_free(&p);
        do_find(app);
        return;
    }
    length = search_expand(&p, fw.with, text + hit.line_offset, &m, NULL, 0);
    with = malloc((size_t)length + 1);
    if (with != NULL) {
        search_expand(&p, fw.with, text + hit.line_offset, &m, with, length + 1);
        DoGadgetMethod((struct Gadget *)app->active->editor, app->window, NULL,
                       GM_TEXTEDITOR_Replace, 0, (ULONG)with, 0);
        free(with);
    }
    FreeVec(text);
    search_free(&p);
    if (with == NULL) {
        status("Not enough memory");
        return;
    }
    /* on to the next one, behind the replacement */
    if (!fw.backwards_on) pos.col += length > 0 ? length : 1;
    find_from(app, pos);
}

/**
 * @brief Replace all: every hit of the active document, as one undo step.
 * @param app The application state.
 */
static void do_replace_all(EditorApp *app)
{
    SearchPattern p;
    SearchHit *hits = NULL;
    SearchMatch m;
    Document *doc = app->active;
    Object *ed;
    char *text, *with = NULL;
    long size, n, i, length, capacity = 0, done = 0;

    read_gadgets();
    if (fw.text[0] == '\0' || doc == NULL || app->window == NULL || !compile(&p)) return;
    ed = doc->editor;
    text = export_text(doc, &size);
    n = text != NULL ? search_all(&p, text, size, &hits) : -1;
    if (n < 0) {
        if (text != NULL) FreeVec(text);
        search_free(&p);
        status("Not enough memory");
        return;
    }
    fw.hit_doc = NULL;
    if (n > 0) {
        DoGadgetMethod((struct Gadget *)ed, app->window, NULL, GM_TEXTEDITOR_ARexxCmd, 0,
                       (ULONG)"BEGINMULTICHANGE");
        /* no redraw for every single replacement */
        SetGadgetAttrs((struct Gadget *)ed, app->window, NULL, GA_TEXTEDITOR_Quiet, TRUE, TAG_END);
        /* from the end, so the positions of the hits before stay valid */
        for (i = n - 1; i >= 0; --i) {
            const SearchHit *h = &hits[i];
            const char *line = text + h->line_offset;
            if (!search_line_next(&p, line, h->line_length, h->col, &m)) continue;
            length = search_expand(&p, fw.with, line, &m, NULL, 0);
            if (length + 1 > capacity) {
                free(with);
                capacity = length + 64;
                if ((with = malloc((size_t)capacity)) == NULL) break;
            }
            search_expand(&p, fw.with, line, &m, with, capacity);
            DoGadgetMethod((struct Gadget *)ed, app->window, NULL, GM_TEXTEDITOR_MarkText, 0,
                           (ULONG)h->col, (ULONG)h->line,
                           (ULONG)(h->col + h->length), (ULONG)h->line);
            DoGadgetMethod((struct Gadget *)ed, app->window, NULL, GM_TEXTEDITOR_Replace, 0,
                           (ULONG)with, 0);
            ++done;
        }
        SetGadgetAttrs((struct Gadget *)ed, app->window, NULL, GA_TEXTEDITOR_Quiet, FALSE, TAG_END);
        DoGadgetMethod((struct Gadget *)ed, app->window, NULL, GM_TEXTEDITOR_ARexxCmd, 0,
                       (ULONG)"ENDMULTICHANGE");
        RefreshGList((struct Gadget *)ed, app->window, NULL, 1);
    }
    free(with);
    free(hits);
    FreeVec(text);
    search_free(&p);
    if (done < n) status("Not enough memory");
    else if (done == 1) status("1 replacement");
    else {
        snprintf(fw.message, sizeof(fw.message), "%ld replacements", done);
        status(fw.message);
    }
}

/**
 * @brief Put the cursor into the search field. Works only while the
 *        window is active; ActivateWindow() is asynchronous, so it is
 *        tried again when the window reports WMHI_ACTIVE.
 */
static void focus_field(void)
{
    if (fw.win != NULL &&
        ActivateLayoutGadget((struct Gadget *)fw.root, fw.win, NULL, (ULONG)fw.find))
        fw.focus = 0;
}

void find_open(EditorApp *app)
{
    if (app->window == NULL || !open_classes(app)) return;
    if (fw.winobj == NULL && !build(app)) {
        ui_error(app, APP_NAME, "Could not create the find window.");
        return;
    }
    if (fw.win == NULL) {
        SetAttrs(fw.winobj, WINDOW_RefWindow, (ULONG)app->window, TAG_END);
        if ((fw.win = (struct Window *)DoMethod(fw.winobj, WM_OPEN, NULL)) == NULL) return;
        status(fw.message);
    } else {
        WindowToFront(fw.win);
        ActivateWindow(fw.win);
    }
    fw.focus = 1;
    focus_field();
}

void find_next(EditorApp *app)
{
    read_gadgets();
    if (fw.text[0] == '\0') {
        find_open(app);
        return;
    }
    if (app->active == NULL) return;
    if (!find_from(app, start_position(app)) && app->screen != NULL) DisplayBeep(app->screen);
}

/** @brief Close the window (the object stays for the next time). */
static void find_close(void)
{
    if (fw.win != NULL) {
        read_gadgets();                 /* for Find next */
        DoMethod(fw.winobj, WM_CLOSE, NULL);
        fw.win = NULL;
    }
}

ULONG find_sigmask(void)
{
    ULONG sig = 0;
    if (fw.win != NULL) GetAttr(WINDOW_SigMask, fw.winobj, &sig);
    return sig;
}

void find_handle(EditorApp *app)
{
    ULONG result;
    WORD code;

    if (fw.win == NULL) return;
    while ((result = DoMethod(fw.winobj, WM_HANDLEINPUT, &code)) != WMHI_LASTMSG) {
        switch (result & WMHI_CLASSMASK) {
        case WMHI_CLOSEWINDOW:
            find_close();
            return;
        case WMHI_ACTIVE:
            if (fw.focus) focus_field();
            break;
        case WMHI_GADGETUP:
            switch (result & WMHI_GADGETMASK) {
            case FG_FIND:                       /* Return in the search field */
            case FG_DOFIND:     do_find(app); break;
            case FG_DOREPLACE:  do_replace(app); break;
            case FG_REPLACEALL: do_replace_all(app); break;
            case FG_CLOSE:      find_close(); return;
            }
            break;
        }
    }
}

void find_forget_document(Document *doc)
{
    if (fw.hit_doc == doc) fw.hit_doc = NULL;
}

void find_cleanup(void)
{
    find_close();
    if (fw.winobj != NULL) {
        DisposeObject(fw.winobj);
        fw.winobj = NULL;
    }
    if (StringBase != NULL) {
        CloseLibrary(StringBase);
        StringBase = NULL;
    }
}
