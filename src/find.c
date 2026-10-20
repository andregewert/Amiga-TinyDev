/**
 * @file find.c
 * @brief The find and replace window.
 *
 * A window that stays open next to the main window, built like Kickdown's:
 * search text, replacement, case sensitive, whole words, regular
 * expression, backwards, wrap around; Find, Replace, Replace all, Close. Edit/Find next repeats the
 * last search without the window. It searches the active document, all
 * open documents (in tab order, going on with the next tab) or the
 * project files.
 *
 * Project files are the files below the folder tree's directory (the
 * project) whose names match the file pattern. They are searched step by
 * step in the event loop (filescan.c walks the directories; the window
 * signals itself to go on), so the window stays usable and Stop ends the
 * search. Files that are open are searched in their editor. The hits fill
 * a list; Find (Next) and a click in the list open the file and mark the
 * hit. Replace all opens every file with hits in a tab and replaces there
 * (one undo step per file, nothing is saved).
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
#include <gadgets/chooser.h>
#include <gadgets/listbrowser.h>
#include <gadgets/dirtree.h>
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
#include <proto/chooser.h>
#include <proto/listbrowser.h>
#include <proto/dos.h>
#include <proto/string.h>
#include <proto/label.h>
#include <clib/alib_protos.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct Library *StringBase = NULL;      /**< string.gadget, opened with the window */
extern struct Library *CheckBoxBase;    /**< checkbox.gadget, shared with the settings window */
extern struct Library *ListBrowserBase; /* likewise */

enum {
    FG_FIND = 300, FG_REPLACE, FG_SCOPE, FG_FILES, FG_CASE, FG_WORDS, FG_REGEX, FG_BACKWARDS,
    FG_WRAP, FG_LIST, FG_DOFIND, FG_DOREPLACE, FG_REPLACEALL, FG_STOP, FG_CLOSE, FG_STATUS
};

#define MAXSEARCH 200                   /**< length of the text fields */

/** Where the search looks (the "Search in" chooser). */
enum { SCOPE_CURRENT = 0, SCOPE_OPEN, SCOPE_PROJECT };

/** Texts of the "Search in" choices (MSG_), in the order of the scopes. */
static const LONG scope_texts[] = {
    MSG_FIND_SCOPE_CURRENT, MSG_FIND_SCOPE_OPEN, MSG_FIND_SCOPE_PROJECT
};
static STRPTR scope_labels[4];              /**< the chooser's labels, filled from scope_texts[] */

#define MAXRESULTS 500                  /**< hits listed for the project files */
#define MAXFILESIZE (1024L * 1024L)     /**< larger files are not searched */
#define FLUSH_FILES 16                  /**< show new hits after so many files */

/** A hit in the result list (LBNA_UserData of its node). */
typedef struct Result {
    char *path;                         /**< full name of the file */
    long line;                          /**< line, from 0 */
    long col;                           /**< column, from 0 */
    long length;                        /**< length of the hit */
} Result;

/** Columns of the result list (titles from column_texts[]). */
static struct ColumnInfo result_columns[] = {
    { 40, NULL, 0 },
    { 8,  NULL, 0 },
    { 52, NULL, 0 },
    { -1, (STRPTR)~0, (ULONG)-1 }
};
/** Titles of the result list's columns (MSG_). */
static const LONG column_texts[] = { MSG_FIND_COL_FILE, MSG_FIND_COL_LINE, MSG_FIND_COL_TEXT };

/** State of the find window (there is one). */
static struct {
    Object *winobj, *root, *find, *replace, *scope, *files, *casesens, *words, *regex, *backwards,
           *wrap, *list, *stop, *status;
    struct Window *win;
    int focus;                          /* activate the search field when active */
    char text[MAXSEARCH + 1];           /* last search, also for Find next */
    char with[MAXSEARCH + 1];           /* last replacement */
    char pattern[MAXSEARCH + 1];        /* file pattern of the project search */
    int scope_on, casesens_on, words_on, regex_on, backwards_on, wrap_on;
    Document *hit_doc;                  /* the last hit was marked in this document ... */
    SearchHit hit;                      /* ... here (as found) ... */
    ULONG hx0, hy0, hx1, hy1;           /* ... and the editor's block of it */
    char message[MAXSEARCH + 40];       /* the status text */
    /* the project search */
    FileScan *scan;                     /* running walk, or NULL */
    SearchPattern sp;                   /* its pattern */
    BYTE signal;                        /* to go on with it, -1 if none */
    struct List results;                /* nodes of the result list */
    struct List pending;                /* new nodes not shown yet */
    long result_count, file_count, hit_files, unflushed;
    struct Node *current;               /* the result shown last */
    int complete;                       /* the results are complete */
    int jump_first;                     /* show the first hit when it is found */
    int replace_after;                  /* replace all instead of listing the hits */
    int walked;                         /* all files were searched */
    struct List queue;                  /* files to open and replace in (ln_Name) */
    Document *replace_doc;              /* opened, to be replaced in next */
    Document *show_doc;                 /* opened, a hit to be marked in next ... */
    SearchPos show_pos;                 /* ... from here */
    char project[EDITOR_PATH_MAX];      /* the project directory searched */
    char key[3 * MAXSEARCH + EDITOR_PATH_MAX + 16]; /* the search the results belong to */
    char file[EDITOR_PATH_MAX];         /* buffer for file names */
} fw = { .wrap_on = 1, .pattern = "#?", .signal = -1 };

/**
 * @brief Open the classes the window needs (once).
 * @param app The application state (for the requester).
 * @return Non-zero if all are available.
 */
static int open_classes(EditorApp *app)
{
    if (CheckBoxBase == NULL) CheckBoxBase = OpenLibrary((STRPTR)"gadgets/checkbox.gadget", 44);
    if (StringBase == NULL) StringBase = OpenLibrary((STRPTR)"gadgets/string.gadget", 44);
    if (ChooserBase == NULL) ChooserBase = OpenLibrary((STRPTR)"gadgets/chooser.gadget", 44);
    if (ListBrowserBase == NULL) ListBrowserBase = OpenLibrary((STRPTR)"gadgets/listbrowser.gadget", 44);
    if (CheckBoxBase != NULL && StringBase != NULL && ChooserBase != NULL && ListBrowserBase != NULL) {
        if (fw.results.lh_Head == NULL) {
            NewList(&fw.results);
            NewList(&fw.pending);
            NewList(&fw.queue);
        }
        return 1;
    }
    ui_error(app, APP_NAME, S(MSG_FIND_NEEDCLASSES));
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
 * @brief Append a text to the status text (fw.message), as far as it fits.
 * @param text The text.
 */
static void append(const char *text)
{
    size_t n = strlen(fw.message);
    strncpy(fw.message + n, text, sizeof(fw.message) - n - 1);
    fw.message[sizeof(fw.message) - 1] = '\0';
}

/**
 * @brief Build the window object.
 * @param app The application state.
 * @return Non-zero on success.
 */
static int build(EditorApp *app)
{
    int i;
    for (i = 0; i < 3; ++i) {
        scope_labels[i] = (STRPTR)S(scope_texts[i]);
        result_columns[i].ci_Title = (STRPTR)S(column_texts[i]);
    }
    fw.find = string(FG_FIND, fw.text);
    fw.replace = string(FG_REPLACE, fw.with);
    fw.scope = NewObject(CHOOSER_GetClass(), NULL,
        GA_ID, FG_SCOPE, GA_RelVerify, TRUE, CHOOSER_PopUp, TRUE,
        CHOOSER_LabelArray, (ULONG)scope_labels,
        CHOOSER_Selected, (ULONG)fw.scope_on, TAG_END);
    fw.files = NewObject(STRING_GetClass(), NULL,
        GA_ID, FG_FILES, GA_RelVerify, TRUE, GA_TabCycle, TRUE,
        GA_Disabled, (ULONG)(fw.scope_on != SCOPE_PROJECT),
        STRINGA_MaxChars, MAXSEARCH, STRINGA_TextVal, (ULONG)fw.pattern, TAG_END);
    fw.list = NewObject(LISTBROWSER_GetClass(), NULL,
        GA_ID, FG_LIST, GA_RelVerify, TRUE,
        LISTBROWSER_ColumnInfo, (ULONG)result_columns,
        LISTBROWSER_ColumnTitles, TRUE,
        LISTBROWSER_Labels, (ULONG)&fw.results,
        LISTBROWSER_ShowSelected, TRUE,
        LISTBROWSER_HorizontalProp, TRUE,
        TAG_END);
    fw.stop = NewObject(BUTTON_GetClass(), NULL,
        GA_ID, FG_STOP, GA_RelVerify, TRUE, GA_Text, (ULONG)S(MSG_FIND_STOP),
        GA_Disabled, (ULONG)(fw.scan == NULL), BUTTON_TextPadding, TRUE, TAG_END);
    fw.casesens = checkbox(FG_CASE, S(MSG_FIND_CASE), fw.casesens_on);
    fw.words = checkbox(FG_WORDS, S(MSG_FIND_WORDS), fw.words_on);
    fw.regex = checkbox(FG_REGEX, S(MSG_FIND_REGEX), fw.regex_on);
    fw.backwards = checkbox(FG_BACKWARDS, S(MSG_FIND_BACKWARDS), fw.backwards_on);
    fw.wrap = checkbox(FG_WRAP, S(MSG_FIND_WRAP), fw.wrap_on);
    fw.status = NewObject(BUTTON_GetClass(), NULL,
        GA_ID, FG_STATUS, GA_ReadOnly, TRUE,
        GA_Underscore, 0,                   /* the search text may contain '_' */
        GA_Text, (ULONG)"",
        BUTTON_Justification, BCJ_LEFT, BUTTON_BevelStyle, BVS_NONE,
        BUTTON_Transparent, TRUE,           /* the window's backfill shows through */
        TAG_END);

    fw.winobj = NewObject(WINDOW_GetClass(), NULL,
        WA_Title,          (ULONG)S(MSG_FIND_TITLE),
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
                CHILD_Label,     (ULONG)label(S(MSG_FIND_FIND)),
                CHILD_WeightedHeight, 0,
                LAYOUT_AddChild, (ULONG)fw.replace,
                CHILD_Label,     (ULONG)label(S(MSG_FIND_WITH)),
                CHILD_WeightedHeight, 0,
                LAYOUT_AddChild, (ULONG)fw.scope,
                CHILD_Label,     (ULONG)label(S(MSG_FIND_SCOPE)),
                CHILD_WeightedHeight, 0,
                LAYOUT_AddChild, (ULONG)fw.files,
                CHILD_Label,     (ULONG)label(S(MSG_FIND_PATTERN)),
                CHILD_WeightedHeight, 0,
                /* the options in columns below the fields: what matches
                 * left, the direction right */
                LAYOUT_AddChild, (ULONG)NewObject(LAYOUT_GetClass(), NULL,
                    LAYOUT_Orientation,  LAYOUT_ORIENT_HORIZ,
                    LAYOUT_InnerSpacing, 16,
                    LAYOUT_AddChild, (ULONG)NewObject(LAYOUT_GetClass(), NULL,
                        LAYOUT_Orientation,  LAYOUT_ORIENT_VERT,
                        LAYOUT_InnerSpacing, 2,
                        LAYOUT_AddChild,     (ULONG)fw.casesens,
                        LAYOUT_AddChild,     (ULONG)fw.words,
                        LAYOUT_AddChild,     (ULONG)fw.regex,
                        TAG_END),
                    CHILD_WeightedWidth, 0,
                    LAYOUT_AddChild, (ULONG)NewObject(LAYOUT_GetClass(), NULL,
                        LAYOUT_Orientation,  LAYOUT_ORIENT_VERT,
                        LAYOUT_InnerSpacing, 2,
                        LAYOUT_AddChild,     (ULONG)fw.backwards,
                        LAYOUT_AddChild,     (ULONG)fw.wrap,
                        /* the column stays at the top */
                        LAYOUT_AddChild,     (ULONG)NewObject(LAYOUT_GetClass(), NULL, TAG_END),
                        TAG_END),
                    CHILD_WeightedWidth, 0,
                    LAYOUT_AddChild, (ULONG)NewObject(LAYOUT_GetClass(), NULL, TAG_END),
                    TAG_END),
                CHILD_Label,     (ULONG)label(""),
                CHILD_WeightedHeight, 0,
                LAYOUT_AddChild, (ULONG)fw.status,
                CHILD_WeightedHeight, 0,
                TAG_END),
            CHILD_WeightedHeight, 0,
            /* the hits in the project files; takes the remaining height */
            LAYOUT_AddChild, (ULONG)fw.list,
            CHILD_MinHeight, 80,
            /* the actions left, Close right, each as wide as its text */
            LAYOUT_AddChild, (ULONG)NewObject(LAYOUT_GetClass(), NULL,
                LAYOUT_Orientation,  LAYOUT_ORIENT_HORIZ,
                LAYOUT_AddChild,     (ULONG)button(FG_DOFIND, S(MSG_FIND_DOFIND)),
                CHILD_WeightedWidth, 0,
                LAYOUT_AddChild,     (ULONG)button(FG_DOREPLACE, S(MSG_FIND_DOREPLACE)),
                CHILD_WeightedWidth, 0,
                LAYOUT_AddChild,     (ULONG)button(FG_REPLACEALL, S(MSG_FIND_REPLACEALL)),
                CHILD_WeightedWidth, 0,
                LAYOUT_AddChild,     (ULONG)fw.stop,
                CHILD_WeightedWidth, 0,
                LAYOUT_AddChild,     (ULONG)NewObject(LAYOUT_GetClass(), NULL, TAG_END),
                LAYOUT_AddChild,     (ULONG)button(FG_CLOSE, S(MSG_FIND_CLOSE)),
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
    fw.pattern[0] = '\0';
    if ((s = (STRPTR)get(fw.files, STRINGA_TextVal)) != NULL) strncat(fw.pattern, (const char *)s, MAXSEARCH);
    fw.scope_on = (int)get(fw.scope, CHOOSER_Selected);
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
    if (error == SEARCH_NOMEM) status(S(MSG_FIND_NOMEM));
    else if (error == SEARCH_BADREGEX) {
        snprintf(fw.message, sizeof(fw.message), S(MSG_FIND_BADREGEX), p->error);
        status(fw.message);
    }
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

/** Start position "at the end" for a backward search through a document. */
#define FAR_END 0x7fffffffL

/**
 * @brief Tell whether a document is the one shown in the window. Editor
 *        methods for it get the window (and draw); documents on hidden
 *        pages get no GadgetInfo, so they change without drawing.
 * @param app The application state.
 * @param doc The document.
 * @return Non-zero if it is visible.
 */
static int visible(EditorApp *app, Document *doc)
{
    return doc == app->active && app->window != NULL;
}

/**
 * @brief Mark a range of one line in a document's editor.
 * @param app The application state.
 * @param doc The document.
 * @param line The line.
 * @param col First column.
 * @param length Length of the range.
 */
static void ed_mark(EditorApp *app, Document *doc, long line, long col, long length)
{
    if (visible(app, doc))
        DoGadgetMethod((struct Gadget *)doc->editor, app->window, NULL, GM_TEXTEDITOR_MarkText, 0,
                       (ULONG)col, (ULONG)line, (ULONG)(col + length), (ULONG)line);
    else DoMethod(doc->editor, GM_TEXTEDITOR_MarkText, NULL,
                  (ULONG)col, (ULONG)line, (ULONG)(col + length), (ULONG)line);
}

/**
 * @brief Replace the marked text of a document's editor.
 * @param app The application state.
 * @param doc The document.
 * @param with The new text.
 */
static void ed_replace(EditorApp *app, Document *doc, const char *with)
{
    if (visible(app, doc))
        DoGadgetMethod((struct Gadget *)doc->editor, app->window, NULL, GM_TEXTEDITOR_Replace, 0,
                       (ULONG)with, 0);
    else DoMethod(doc->editor, GM_TEXTEDITOR_Replace, NULL, (ULONG)with, 0);
}

/**
 * @brief Send an ARexx command to a document's editor.
 * @param app The application state.
 * @param doc The document.
 * @param command The command.
 */
static void ed_command(EditorApp *app, Document *doc, const char *command)
{
    if (visible(app, doc))
        DoGadgetMethod((struct Gadget *)doc->editor, app->window, NULL, GM_TEXTEDITOR_ARexxCmd, 0,
                       (ULONG)command);
    else DoMethod(doc->editor, GM_TEXTEDITOR_ARexxCmd, NULL, (ULONG)command);
}

/**
 * @brief Switch the drawing of a document's editor off or on.
 * @param app The application state.
 * @param doc The document.
 * @param quiet Non-zero for no drawing.
 */
static void ed_quiet(EditorApp *app, Document *doc, int quiet)
{
    if (visible(app, doc))
        SetGadgetAttrs((struct Gadget *)doc->editor, app->window, NULL,
                       GA_TEXTEDITOR_Quiet, (ULONG)quiet, TAG_END);
    else SetAttrs(doc->editor, GA_TEXTEDITOR_Quiet, (ULONG)quiet, TAG_END);
}

/**
 * @brief Search one document.
 * @param doc The document.
 * @param p The pattern.
 * @param from The start position (see search_text()).
 * @param backwards Non-zero to search towards the start.
 * @param wrap Non-zero to go on at the other end of the document.
 * @param hit The hit.
 * @param wrapped Set if the hit was found after wrapping (may be NULL).
 * @return Non-zero if found.
 */
static int search_doc(Document *doc, const SearchPattern *p, SearchPos from, int backwards,
                      int wrap, SearchHit *hit, int *wrapped)
{
    long size;
    int found;
    char *text = export_text(doc, &size);
    if (text == NULL) return 0;
    found = search_text(p, text, size, from, backwards, wrap, hit, wrapped);
    FreeVec(text);
    return found;
}

/**
 * @brief The next document in the search direction.
 * @param doc The document.
 * @return The next (or previous) one, or NULL at the end of the list.
 */
static Document *step(Document *doc)
{
    struct Node *n = fw.backwards_on ? doc->node.ln_Pred : doc->node.ln_Succ;
    return (fw.backwards_on ? n->ln_Pred : n->ln_Succ) != NULL ? (Document *)n : NULL;
}

/**
 * @brief Search from a position in the active document, then (in the
 *        scope "open documents") through the other documents in tab
 *        order; activate the document with the hit and mark it.
 * @param app The application state.
 * @param from The start position in the active document.
 * @return Non-zero if found.
 */
static int find_from(EditorApp *app, SearchPos from)
{
    SearchPattern p;
    SearchHit hit;
    SearchPos top;
    Document *doc = app->active, *d, *target;
    int found, wrapped = 0, all = fw.scope_on == SCOPE_OPEN;

    fw.hit_doc = NULL;
    if (doc == NULL || app->window == NULL || !compile(&p)) return 0;
    target = doc;
    found = search_doc(doc, &p, from, fw.backwards_on, fw.wrap_on && !all, &hit, &wrapped);
    if (!found && all) {
        top.line = top.col = fw.backwards_on ? FAR_END : 0;
        for (d = step(doc); ; d = step(d)) {
            if (d == NULL) {
                if (!fw.wrap_on) break;
                wrapped = 1;
                d = (Document *)(fw.backwards_on ? app->documents.lh_TailPred : app->documents.lh_Head);
            }
            if (d == doc) {
                /* round again: the part of the first document before the start */
                found = search_doc(doc, &p, from, fw.backwards_on, 1, &hit, NULL);
                break;
            }
            if (search_doc(d, &p, top, fw.backwards_on, 0, &hit, NULL)) {
                found = 1;
                target = d;
                break;
            }
        }
    }
    search_free(&p);
    if (found) {
        if (target != app->active) document_activate(app, target);
        mark_hit(app, &hit);
        status(wrapped ? S(MSG_FIND_WRAPPED) : "");
    } else {
        snprintf(fw.message, sizeof(fw.message), S(MSG_FIND_NOTFOUND), fw.text);
        status(fw.message);
    }
    return found;
}

/* ---- project files ------------------------------------------------- */

static long replace_all_in(EditorApp *app, Document *doc, const SearchPattern *p);

/**
 * @brief Ghost a gadget of the window or make it usable.
 * @param o The gadget.
 * @param off Non-zero to ghost it.
 */
static void set_disabled(Object *o, int off)
{
    if (o == NULL) return;
    if (fw.win != NULL) SetGadgetAttrs((struct Gadget *)o, fw.win, NULL, GA_Disabled, (ULONG)off, TAG_END);
    else SetAttrs(o, GA_Disabled, (ULONG)off, TAG_END);
}

/**
 * @brief Describe the current search for the project files, to tell
 *        whether the result list belongs to it.
 * @param key The buffer (sizeof(fw.key)).
 * @param root The project directory.
 */
static void make_key(char *key, const char *root)
{
    snprintf(key, sizeof(fw.key), "%d%d%d|%s|%s|%s", fw.casesens_on, fw.words_on, fw.regex_on,
             root, fw.pattern, fw.text);
}

/**
 * @brief Give the result list its nodes (~0 detaches them for changes).
 * @param list The list.
 */
static void set_labels(struct List *list)
{
    if (fw.list == NULL) return;
    if (fw.win != NULL)
        SetGadgetAttrs((struct Gadget *)fw.list, fw.win, NULL, LISTBROWSER_Labels, (ULONG)list, TAG_END);
    else SetAttrs(fw.list, LISTBROWSER_Labels, (ULONG)list, TAG_END);
}

/**
 * @brief Free a node of the result list with its result.
 * @param n The node (removed from its list).
 */
static void free_result(struct Node *n)
{
    Result *r = NULL;
    GetListBrowserNodeAttrs(n, LBNA_UserData, (ULONG)&r, TAG_END);
    if (r != NULL) {
        free(r->path);
        free(r);
    }
    FreeListBrowserNode(n);
}

/** @brief Empty the result list; the results no longer belong to a search. */
static void results_clear(void)
{
    struct Node *n;
    if (fw.results.lh_Head == NULL) return;
    set_labels((struct List *)~0);
    while ((n = RemHead(&fw.results)) != NULL) free_result(n);
    while ((n = RemHead(&fw.pending)) != NULL) free_result(n);
    set_labels(&fw.results);
    fw.result_count = fw.file_count = fw.hit_files = fw.unflushed = 0;
    fw.current = NULL;
    fw.complete = 0;
    fw.key[0] = '\0';
}

/** @brief Show the hits found since the last call in the list. */
static void results_flush(void)
{
    struct Node *n;
    fw.unflushed = 0;
    if (IsListEmpty(&fw.pending)) return;
    set_labels((struct List *)~0);
    while ((n = RemHead(&fw.pending)) != NULL) AddTail(&fw.results, n);
    set_labels(&fw.results);
}

/**
 * @brief Add a hit to the result list (shown with the next flush).
 * @param path Full name of the file.
 * @param text The file's text.
 * @param h The hit.
 * @return Non-zero on success.
 */
static int results_add(const char *path, const char *text, const SearchHit *h)
{
    Result *r;
    struct Node *n;
    char number[16], shown[72];
    const char *t = text + h->line_offset, *relative = path;
    size_t root_length = strlen(fw.project);
    long i = 0, k;

    if ((r = malloc(sizeof(*r))) == NULL) return 0;
    if ((r->path = malloc(strlen(path) + 1)) == NULL) {
        free(r);
        return 0;
    }
    strcpy(r->path, path);
    r->line = h->line;
    r->col = h->col;
    r->length = h->length;
    /* the line without its indentation, tabs as spaces */
    while (i < h->line_length && (t[i] == ' ' || t[i] == '\t')) ++i;
    for (k = 0; i < h->line_length && k < (long)sizeof(shown) - 1; ++i, ++k)
        shown[k] = t[i] == '\t' ? ' ' : t[i];
    shown[k] = '\0';
    snprintf(number, sizeof(number), "%ld", h->line + 1);
    if (strncmp(path, fw.project, root_length) == 0) {
        relative = path + root_length;
        if (*relative == '/') ++relative;
    }
    n = AllocListBrowserNode(3, LBNA_UserData, (ULONG)r,
        LBNA_Column, 0, LBNCA_CopyText, TRUE, LBNCA_Text, (ULONG)relative,
        LBNA_Column, 1, LBNCA_CopyText, TRUE, LBNCA_Text, (ULONG)number,
            LBNCA_Justification, LCJ_RIGHT,
        LBNA_Column, 2, LBNCA_CopyText, TRUE, LBNCA_Text, (ULONG)shown,
        TAG_END);
    if (n == NULL) {
        free(r->path);
        free(r);
        return 0;
    }
    AddTail(&fw.pending, n);
    ++fw.result_count;
    return 1;
}

/**
 * @brief Read a text file for the search.
 * @param path The file.
 * @param size Set to its size.
 * @return The text (free() it), or NULL if it cannot be read, is larger
 *         than MAXFILESIZE or contains NUL bytes (not a text).
 */
static char *read_file(const char *path, long *size)
{
    BPTR fh = Open((STRPTR)path, MODE_OLDFILE);
    struct FileInfoBlock *fib;
    char *data = NULL;
    if (fh == 0) return NULL;
    fib = AllocDosObject(DOS_FIB, NULL);
    if (fib != NULL && ExamineFH(fh, fib) && fib->fib_Size >= 0 && fib->fib_Size <= MAXFILESIZE) {
        long n = fib->fib_Size;
        data = malloc((size_t)n + 1);
        if (data != NULL && Read(fh, data, n) == n && memchr(data, 0, (size_t)n) == NULL) {
            data[n] = '\0';
            *size = n;
        } else {
            free(data);
            data = NULL;
        }
    }
    if (fib != NULL) FreeDosObject(DOS_FIB, fib);
    Close(fh);
    return data;
}

/**
 * @brief Search one project file: list its hits or, when replacing,
 *        open it in a tab and replace them there. Open files are searched
 *        in their editor.
 * @param app The application state.
 * @param path The file.
 */
static void search_file(EditorApp *app, const char *path)
{
    char canonical[EDITOR_PATH_MAX];
    Document *doc;
    SearchHit *hits = NULL;
    char *text;
    long size = 0, n, i;
    int from_editor;
    BPTR lock = Lock((STRPTR)path, ACCESS_READ);

    if (lock == 0) return;
    i = NameFromLock(lock, (STRPTR)canonical, sizeof(canonical));
    UnLock(lock);
    if (!i) return;
    ++fw.file_count;
    doc = document_find_path(app, canonical);
    from_editor = doc != NULL;
    text = from_editor ? export_text(doc, &size) : read_file(canonical, &size);
    if (text == NULL) return;
    n = search_all(&fw.sp, text, size, &hits);
    if (n > 0) {
        ++fw.hit_files;
        if (fw.replace_after) {
            /* A file that is not open yet is opened after the walk and
             * replaced in only one turn of the event loop later, once
             * its new tab is laid out and drawn. */
            if (doc != NULL) {
                if ((i = replace_all_in(app, doc, &fw.sp)) > 0) fw.result_count += i;
            } else {
                struct Node *q = malloc(sizeof(*q) + strlen(canonical) + 1);
                if (q != NULL) {
                    q->ln_Name = (char *)(q + 1);
                    strcpy(q->ln_Name, canonical);
                    AddTail(&fw.queue, q);
                }
            }
        } else {
            for (i = 0; i < n && fw.result_count < MAXRESULTS; ++i)
                if (!results_add(canonical, text, &hits[i])) break;
        }
    }
    free(hits);
    if (from_editor) FreeVec(text);
    else free(text);
}

/**
 * @brief Draw the editor of a just opened document once before it is
 *        marked or changed (a tab reused for the file may still show the
 *        old, empty contents).
 * @param app The application state.
 * @param doc The document.
 */
static void redraw(EditorApp *app, Document *doc)
{
    if (visible(app, doc)) RefreshGList((struct Gadget *)doc->editor, app->window, NULL, 1);
}

/**
 * @brief Let the window's input be handled again soon (the event loop
 *        wakes up at once).
 */
static void wake(void)
{
    if (fw.signal < 0) fw.signal = AllocSignal(-1);
    if (fw.signal >= 0) Signal(FindTask(NULL), 1UL << fw.signal);
}

/**
 * @brief Mark a hit of the result list in the active document: it is
 *        searched again from its position, as the text may have changed;
 *        if it is not there, the cursor goes to its line.
 * @param app The application state.
 * @param doc The document (the active one).
 * @param pos Line and column of the hit.
 */
static void mark_result(EditorApp *app, Document *doc, SearchPos pos)
{
    SearchPattern p;
    SearchHit hit;
    if (!compile(&p)) return;
    if (search_doc(doc, &p, pos, 0, 0, &hit, NULL) && hit.line == pos.line) mark_hit(app, &hit);
    else SetGadgetAttrs((struct Gadget *)doc->editor, app->window, NULL,
                        GA_TEXTEDITOR_CursorY, (ULONG)pos.line, GA_TEXTEDITOR_CursorX, 0, TAG_END);
    search_free(&p);
}

/**
 * @brief Show a hit of the result list: open its file and mark the hit. In
 *        a file that has just been opened it is marked one turn of the
 *        event loop later, once the new tab is laid out and the editor has
 *        taken the text (marking at once left the editor empty, and a
 *        Replace then lost the text).
 * @param app The application state.
 * @param node The node of the hit.
 */
static void show_result(EditorApp *app, struct Node *node)
{
    Result *r = NULL;
    Document *doc;
    SearchPos pos;
    struct Node *n;
    ULONG index = 0;
    int was_open;

    GetListBrowserNodeAttrs(node, LBNA_UserData, (ULONG)&r, TAG_END);
    if (r == NULL) return;
    fw.current = node;
    for (n = fw.results.lh_Head; n != node && n->ln_Succ != NULL; n = n->ln_Succ) ++index;
    if (fw.win != NULL)
        SetGadgetAttrs((struct Gadget *)fw.list, fw.win, NULL,
            LISTBROWSER_Selected, index, LISTBROWSER_MakeVisible, index, TAG_END);
    was_open = document_find_path(app, r->path) != NULL;
    fw.hit_doc = fw.show_doc = NULL;
    if ((doc = document_open(app, r->path)) == NULL || app->active != doc) return;
    pos.line = r->line;
    pos.col = r->col;
    if (was_open) mark_result(app, doc, pos);
    else {
        fw.show_doc = doc;
        fw.show_pos = pos;
        wake();
    }
}

/**
 * @brief End the project search.
 * @param stopped Non-zero if it was stopped before the end.
 */
static void scan_finish(int stopped)
{
    int incomplete;
    struct Node *q;
    if (fw.scan == NULL) return;
    while ((q = RemHead(&fw.queue)) != NULL) free(q);
    fw.replace_doc = NULL;
    fw.walked = 0;
    incomplete = filescan_incomplete(fw.scan);
    filescan_end(fw.scan);
    fw.scan = NULL;
    search_free(&fw.sp);
    results_flush();
    set_disabled(fw.stop, 1);
    if (fw.replace_after) {
        snprintf(fw.message, sizeof(fw.message), S(MSG_FIND_REPLACEDFILES),
                 fw.result_count, fw.hit_files);
        if (stopped) append(S(MSG_FIND_STOPPED));
        fw.replace_after = 0;
        fw.result_count = 0;
        fw.key[0] = '\0';
    } else if (fw.result_count == 0) {
        snprintf(fw.message, sizeof(fw.message), S(MSG_FIND_NOTFOUNDFILES), fw.text, fw.file_count);
        if (stopped) append(S(MSG_FIND_STOPPED));
    } else {
        snprintf(fw.message, sizeof(fw.message), S(MSG_FIND_HITS),
                 fw.result_count, fw.hit_files, fw.file_count);
        if (stopped) append(S(MSG_FIND_STOPPED));
        else if (fw.result_count >= MAXRESULTS) append(S(MSG_FIND_LIMIT));
        fw.complete = !stopped && fw.result_count < MAXRESULTS;
    }
    if (incomplete) append(S(MSG_FIND_SKIPPED));
    status(fw.message);
}

/**
 * @brief Start searching the project files (the result list is emptied).
 * @param app The application state.
 * @param replace Non-zero to replace all hits (opening the files in tabs)
 *                instead of listing them.
 */
static void scan_start(EditorApp *app, int replace)
{
    scan_finish(1);
    results_clear();
    if (!tree_project_root(app, fw.project, sizeof(fw.project))) {
        status(S(MSG_FIND_NOPROJECT));
        return;
    }
    if (fw.signal < 0 && (fw.signal = AllocSignal(-1)) < 0) {
        status(S(MSG_FIND_NOSIGNAL));
        return;
    }
    if (!compile(&fw.sp)) return;
    if ((fw.scan = filescan_start(fw.project, fw.pattern)) == NULL) {
        search_free(&fw.sp);
        status(S(MSG_FIND_BADPROJECT));
        return;
    }
    if (!replace) make_key(fw.key, fw.project);
    fw.replace_after = replace;
    fw.jump_first = !replace;
    set_disabled(fw.stop, 0);
    status(S(replace ? MSG_FIND_REPLACING : MSG_FIND_SEARCHING));
    Signal(FindTask(NULL), 1UL << fw.signal);
}

/**
 * @brief Go on with the project search (a few directory entries, at most
 *        one file); the window signals itself to be called again.
 * @param app The application state.
 */
static void scan_step(EditorApp *app)
{
    int r;
    struct Node *q;
    if (fw.walked) {
        /* replacing: open the next file, replace in it in the next step */
        if (fw.replace_doc != NULL) {
            long n;
            redraw(app, fw.replace_doc);
            n = replace_all_in(app, fw.replace_doc, &fw.sp);
            if (n > 0) fw.result_count += n;
            fw.replace_doc = NULL;
        } else if ((q = RemHead(&fw.queue)) != NULL) {
            fw.replace_doc = document_open(app, q->ln_Name);
            free(q);
        } else {
            scan_finish(0);
            return;
        }
        if (fw.scan != NULL) Signal(FindTask(NULL), 1UL << fw.signal);
        return;
    }
    r = filescan_next(fw.scan, fw.file, sizeof(fw.file));
    if (r == 1) {
        search_file(app, fw.file);
        if (++fw.unflushed >= FLUSH_FILES) {
            results_flush();
            snprintf(fw.message, sizeof(fw.message),
                     S(fw.replace_after ? MSG_FIND_RPROGRESS : MSG_FIND_PROGRESS),
                     fw.file_count, fw.result_count);
            status(fw.message);
        }
        if (fw.jump_first && fw.result_count > 0) {
            fw.jump_first = 0;
            results_flush();
            show_result(app, fw.results.lh_Head);
        }
    }
    if (fw.scan == NULL) return;        /* ended meanwhile (window closed) */
    if (r < 0 && fw.replace_after) fw.walked = 1;
    else if (r < 0 || (!fw.replace_after && fw.result_count >= MAXRESULTS)) {
        scan_finish(0);
        return;
    }
    Signal(FindTask(NULL), 1UL << fw.signal);
}

/**
 * @brief Show the next (or previous) hit of the result list.
 * @param app The application state.
 */
static void results_next(EditorApp *app)
{
    struct Node *n;
    int wrapped = 0;
    results_flush();
    if (IsListEmpty(&fw.results)) {
        if (fw.scan == NULL) status(S(MSG_FIND_NOHITS));
        return;
    }
    if (fw.backwards_on) n = fw.current != NULL ? fw.current->ln_Pred : fw.results.lh_TailPred;
    else n = fw.current != NULL ? fw.current->ln_Succ : fw.results.lh_Head;
    if ((fw.backwards_on ? n->ln_Pred : n->ln_Succ) == NULL) {
        if (!fw.wrap_on) {
            status(S(MSG_FIND_NOMORE));
            return;
        }
        n = fw.backwards_on ? fw.results.lh_TailPred : fw.results.lh_Head;
        wrapped = 1;
    }
    show_result(app, n);
    status(wrapped ? S(MSG_FIND_WRAPPED) : "");
}

/**
 * @brief Find in the project files: search them if the result list does
 *        not belong to this search, otherwise show the next hit.
 * @param app The application state.
 */
static void project_find(EditorApp *app)
{
    char key[sizeof(fw.key)], root[EDITOR_PATH_MAX];
    if (!tree_project_root(app, root, sizeof(root))) {
        status(S(MSG_FIND_NOPROJECT));
        return;
    }
    make_key(key, root);
    if (strcmp(key, fw.key) != 0 || (fw.scan == NULL && fw.result_count == 0)) scan_start(app, 0);
    else results_next(app);
}

/**
 * @brief After a replacement in a project file, move the later hits of
 *        the same line in the result list by the change in length.
 * @param app The application state.
 * @param delta Length of the replacement minus that of the hit.
 */
static void results_shift(EditorApp *app, long delta)
{
    struct Node *n;
    Result *r;
    const char *path = app->active != NULL ? app->active->path : NULL;
    if (path == NULL || delta == 0) return;
    for (n = fw.results.lh_Head; n->ln_Succ != NULL; n = n->ln_Succ) {
        r = NULL;
        GetListBrowserNodeAttrs(n, LBNA_UserData, (ULONG)&r, TAG_END);
        if (r != NULL && r->line == fw.hit.line && r->col > fw.hit.col && strcmp(r->path, path) == 0)
            r->col += delta;
    }
}

/**
 * @brief Find: the next hit from the last one or the cursor.
 * @param app The application state.
 */
static void do_find(EditorApp *app)
{
    read_gadgets();
    if (fw.text[0] == '\0') return;
    if (fw.scope_on == SCOPE_PROJECT) project_find(app);
    else if (app->active != NULL) find_from(app, start_position(app));
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
        ed_replace(app, app->active, with);
        free(with);
    }
    FreeVec(text);
    search_free(&p);
    if (with == NULL) {
        status(S(MSG_FIND_NOMEM));
        return;
    }
    /* on to the next one, behind the replacement */
    if (fw.scope_on == SCOPE_PROJECT) {
        results_shift(app, length - fw.hit.length);
        fw.hit_doc = NULL;
        results_next(app);
        return;
    }
    if (!fw.backwards_on) pos.col += length > 0 ? length : 1;
    find_from(app, pos);
}

/**
 * @brief Replace every hit in one document, as one undo step.
 * @param app The application state.
 * @param doc The document.
 * @param p The pattern.
 * @return The number of replacements, or -1 if out of memory (some may
 *         have been made).
 */
static long replace_all_in(EditorApp *app, Document *doc, const SearchPattern *p)
{
    SearchHit *hits = NULL;
    SearchMatch m;
    char *text, *with = NULL;
    long size, n, i, length, capacity = 0, done = 0;
    int nomem = 0;

    text = export_text(doc, &size);
    n = text != NULL ? search_all(p, text, size, &hits) : -1;
    if (n <= 0) {
        if (text != NULL) FreeVec(text);
        return n;
    }
    ed_command(app, doc, "BEGINMULTICHANGE");
    ed_quiet(app, doc, 1);                  /* no redraw for every single replacement */
    /* from the end, so the positions of the hits before stay valid */
    for (i = n - 1; i >= 0; --i) {
        const SearchHit *h = &hits[i];
        const char *line = text + h->line_offset;
        if (!search_line_next(p, line, h->line_length, h->col, &m)) continue;
        length = search_expand(p, fw.with, line, &m, NULL, 0);
        if (length + 1 > capacity) {
            free(with);
            capacity = length + 64;
            if ((with = malloc((size_t)capacity)) == NULL) {
                nomem = 1;
                break;
            }
        }
        search_expand(p, fw.with, line, &m, with, capacity);
        ed_mark(app, doc, h->line, h->col, h->length);
        ed_replace(app, doc, with);
        ++done;
    }
    ed_quiet(app, doc, 0);
    ed_command(app, doc, "ENDMULTICHANGE");
    if (visible(app, doc)) RefreshGList((struct Gadget *)doc->editor, app->window, NULL, 1);
    if (done > 0) document_set_dirty(app, doc, 1);
    free(with);
    free(hits);
    FreeVec(text);
    return nomem ? -1 : done;
}

/**
 * @brief Replace all: every hit of the active document or (in the scope
 *        "open documents") of all open documents, one undo step each.
 * @param app The application state.
 */
static void do_replace_all(EditorApp *app)
{
    SearchPattern p;
    Document *doc;
    long n, total = 0, docs = 0;
    int nomem = 0;

    read_gadgets();
    if (fw.text[0] == '\0' || app->window == NULL) return;
    if (fw.scope_on == SCOPE_PROJECT) {
        fw.hit_doc = NULL;
        scan_start(app, 1);
        return;
    }
    if (app->active == NULL || !compile(&p)) return;
    fw.hit_doc = NULL;
    if (fw.scope_on == SCOPE_OPEN) {
        for (doc = (Document *)app->documents.lh_Head; doc->node.ln_Succ != NULL;
             doc = (Document *)doc->node.ln_Succ) {
            n = replace_all_in(app, doc, &p);
            if (n < 0) nomem = 1;
            else if (n > 0) {
                total += n;
                ++docs;
            }
        }
    } else {
        n = replace_all_in(app, app->active, &p);
        if (n < 0) nomem = 1;
        else total = n;
    }
    search_free(&p);
    if (nomem) status(S(MSG_FIND_NOMEM));
    else if (fw.scope_on == SCOPE_OPEN) {
        snprintf(fw.message, sizeof(fw.message), S(MSG_FIND_REPLACEDDOCS), total, docs);
        status(fw.message);
    } else {
        snprintf(fw.message, sizeof(fw.message), S(MSG_FIND_REPLACED), total);
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
        ui_error(app, APP_NAME, S(MSG_FIND_NOWINDOW));
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
    if (fw.scope_on == SCOPE_PROJECT) {
        if (fw.win == NULL) find_open(app);    /* the search runs while the window is open */
        project_find(app);
        return;
    }
    if (app->active == NULL) return;
    if (!find_from(app, start_position(app)) && app->screen != NULL) DisplayBeep(app->screen);
}

/** @brief Close the window (the object stays for the next time). */
static void find_close(void)
{
    scan_finish(1);
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
    if ((fw.scan != NULL || fw.show_doc != NULL) && fw.signal >= 0) sig |= 1UL << fw.signal;
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
            case FG_FILES:                      /* ... or in the file pattern */
            case FG_DOFIND:     do_find(app); break;
            case FG_SCOPE:
                fw.scope_on = (int)get(fw.scope, CHOOSER_Selected);
                set_disabled(fw.files, fw.scope_on != SCOPE_PROJECT);
                break;
            case FG_LIST: {
                struct Node *n = (struct Node *)get(fw.list, LISTBROWSER_SelectedNode);
                read_gadgets();
                if (n != NULL) show_result(app, n);
                break;
            }
            case FG_STOP:       scan_finish(1); break;
            case FG_DOREPLACE:  do_replace(app); break;
            case FG_REPLACEALL: do_replace_all(app); break;
            case FG_CLOSE:      find_close(); return;
            }
            break;
        }
    }
    if (fw.show_doc != NULL) {
        Document *doc = fw.show_doc;
        fw.show_doc = NULL;
        if (doc == app->active) {
            redraw(app, doc);
            mark_result(app, doc, fw.show_pos);
        }
    }
    if (fw.scan != NULL) scan_step(app);
}

void find_forget_document(Document *doc)
{
    if (fw.hit_doc == doc) fw.hit_doc = NULL;
    if (fw.replace_doc == doc) fw.replace_doc = NULL;
    if (fw.show_doc == doc) fw.show_doc = NULL;
}

void find_cleanup(void)
{
    struct Node *n;
    find_close();
    if (fw.winobj != NULL) {
        DisposeObject(fw.winobj);
        fw.winobj = NULL;
        fw.list = NULL;
    }
    if (fw.results.lh_Head != NULL) {
        while ((n = RemHead(&fw.results)) != NULL) free_result(n);
        while ((n = RemHead(&fw.pending)) != NULL) free_result(n);
    }
    if (fw.signal >= 0) {
        FreeSignal(fw.signal);
        fw.signal = -1;
    }
    if (StringBase != NULL) {
        CloseLibrary(StringBase);
        StringBase = NULL;
    }
}
