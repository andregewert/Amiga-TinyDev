/**
 * @file symlist.c
 * @brief The symbol list, a tool window: the symbols TinyDev-Scout found
 *        in the active document or in the project.
 *
 * "Show" chooses the active document (its symbols in the order of their
 * lines) or the project (the symbols of the files below the project
 * drawer, sorted by name, with their file and line). Locals (parameters,
 * local variables, fields) are left out. "Filter" keeps the symbols whose
 * names contain its text (without regard to case; taken with Return, the
 * field keeps the input focus), "Kind" those of a kind. A click on a
 * column's title sorts by it (again: the other way round). The first
 * column shows the kind as a coloured letter; as listbrowser.gadget has
 * no help bubbles for rows on OS 3.2 (LISTBROWSER_HintInfoHook is OS 4
 * only), the line below the list shows the kind and the declaration of
 * the selected symbol. A double-click or Go to puts the cursor on the
 * symbol, opening its file if needed. The columns' separators can be
 * dragged. The mouse wheel scrolls the list: its rawkey codes are handled
 * here, as listbrowser.gadget only gets them while it is the active
 * gadget. While Scout indexes the project, the line below the list says
 * how many jobs wait.
 *
 * The symbols come from Scout through scoutlink.c: the list asks again
 * when the active document changes or has been analysed again
 * (Document::scout_generation), and for the project when
 * scoutlink_project_generation() changes. The line below the list says
 * how many symbols there are, or why there are none.
 *
 * The window stays open next to the main window (toolwin.c places it and
 * remembers its box, like the bookmark list's).
 */
#include "editor.h"
#include "json.h"
#include "scout_protocol.h"

#include <intuition/gadgetclass.h>
#include <classes/window.h>
#include <gadgets/layout.h>
#include <gadgets/button.h>
#include <gadgets/chooser.h>
#include <gadgets/listbrowser.h>
#include <gadgets/string.h>
#include <images/label.h>
#include <exec/memory.h>
#include <libraries/gadtools.h>
#include <libraries/keymap.h>

#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/window.h>
#include <proto/layout.h>
#include <proto/button.h>
#include <proto/chooser.h>
#include <proto/listbrowser.h>
#include <proto/string.h>
#include <proto/label.h>
#include <proto/utility.h>
#include <clib/alib_protos.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern struct Library *ListBrowserBase;     /* shared with the settings window */
extern struct Library *StringBase;          /* shared with the find window */

enum { SG_SCOPE = 500, SG_FILTER, SG_KIND, SG_LIST, SG_GOTO, SG_STATUS };

/** Smallest width of the window. */
#define MIN_WIDTH 200
/** Smallest height of the window. */
#define MIN_HEIGHT 120
/** Rows the list moves per step of the mouse wheel. */
#define WHEEL_ROWS 3
/** Longest filter text. */
#define FILTER_MAX 64

/** What the list shows. */
enum {
    SCOPE_FILE = 0,     /**< the active document */
    SCOPE_PROJECT,      /**< the project */
    SCOPE_COUNT         /**< number of scopes */
};

/** Groups of symbol kinds: the Kind chooser and the letters. */
enum {
    KG_ALL = 0,         /**< all kinds (chooser only) */
    KG_FUNCTION,        /**< functions, procedures, methods */
    KG_TYPE,            /**< structs, classes, enums, type aliases */
    KG_MACRO,           /**< macros */
    KG_VARIABLE,        /**< variables, fields */
    KG_CONSTANT,        /**< constants, enum members */
    KG_OTHER,           /**< labels, modules and the rest */
    KG_COUNT            /**< number of groups */
};

/** Texts of the groups in the Kind chooser (MSG_). */
static const LONG group_texts[KG_COUNT] = {
    MSG_SYM_KG_ALL, MSG_SYM_KG_FUNCTION, MSG_SYM_KG_TYPE, MSG_SYM_KG_MACRO,
    MSG_SYM_KG_VARIABLE, MSG_SYM_KG_CONSTANT, MSG_SYM_KG_OTHER
};

/** Letters of the groups (the first column). */
static const char group_letters[KG_COUNT] = { ' ', 'F', 'T', 'M', 'V', 'C', '?' };

/** Colours of the groups' letters (0xRRGGBB, after the style guide's accents). */
static const unsigned long group_colours[KG_COUNT] = {
    0x000000, 0x0A521E, 0x0A2A8A, 0x7A1F6E, 0x8C1810, 0x005266, 0x3A4A66
};

/** Columns, also what the list is sorted by. */
enum { COL_KIND = 0, COL_NAME, COL_WHERE, COL_COUNT };

/** A symbol of the list. */
typedef struct SymItem {
    char *name;         /**< its name */
    char *detail;       /**< its declaration (may be NULL) */
    char *file;         /**< its file (project), NULL for the document */
    int kind;           /**< SCOUT_SYMBOL_ value */
    long line;          /**< line of its name (1-based) */
    long col;           /**< column of its name (1-based) */
} SymItem;

/** Texts of the scopes (MSG_). */
static const LONG scope_texts[SCOPE_COUNT] = { MSG_SYM_SCOPE_FILE, MSG_SYM_SCOPE_PROJECT };

/** State of the window and its symbols. */
static struct {
    Object *winobj, *root, *scope, *filter, *kind, *list, *status;
    struct Window *win;
    struct List nodes;              /* LBNA_UserData: index in items */
    struct ColumnInfo *columns;     /* the list's columns */
    SymItem *items;                 /* the symbols (all, before filtering) */
    long count;                     /* number of them */
    int scope_on;                   /* SCOPE_ value */
    char filter_text[FILTER_MAX];   /* the filter */
    unsigned long doc;              /* document of the symbols (number), 0 for none */
    unsigned long generation;       /* its scout_generation / the project generation */
    int waiting;                    /* a request is on its way */
    int asked;                      /* something was asked since the scope was chosen */
    char where_text[EDITOR_PATH_MAX];   /* buffer of a row's line or file (copied by the node) */
    int kind_on;                    /* KG_ value of the Kind chooser */
    int sort_col;                   /* COL_ value the list is sorted by */
    int sort_rev;                   /* the other way round */
    struct Hook render_hook;        /* draws the letter of the kind */
    WORD row_height;                /* height of a row */
    struct ColorMap *cm;            /* where the pens are from */
    LONG pens[KG_COUNT];            /* pens of the groups' colours, -1 if none */
    STRPTR scope_labels[SCOPE_COUNT + 1];
    STRPTR kind_labels[KG_COUNT + 1];
} sy = { .pens = { -1, -1, -1, -1, -1, -1, -1 } };

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
 * @brief Return the text of a symbol kind.
 * @param kind SCOUT_SYMBOL_ value.
 * @return The text, in the user's language.
 */
static const char *kind_text(int kind)
{
    switch (kind) {
    case SCOUT_SYMBOL_FUNCTION:     return S(MSG_SYM_K_FUNCTION);
    case SCOUT_SYMBOL_PROCEDURE:    return S(MSG_SYM_K_PROCEDURE);
    case SCOUT_SYMBOL_METHOD:
    case SCOUT_SYMBOL_CONSTRUCTOR:  return S(MSG_SYM_K_METHOD);
    case SCOUT_SYMBOL_STRUCT:       return S(MSG_SYM_K_STRUCT);
    case SCOUT_SYMBOL_CLASS:
    case SCOUT_SYMBOL_INTERFACE:    return S(MSG_SYM_K_CLASS);
    case SCOUT_SYMBOL_ENUM:         return S(MSG_SYM_K_ENUM);
    case SCOUT_SYMBOL_ENUM_MEMBER:
    case SCOUT_SYMBOL_CONSTANT:     return S(MSG_SYM_K_CONSTANT);
    case SCOUT_SYMBOL_TYPE_ALIAS:   return S(MSG_SYM_K_TYPE);
    case SCOUT_SYMBOL_MACRO:        return S(MSG_SYM_K_MACRO);
    case SCOUT_SYMBOL_VARIABLE:     return S(MSG_SYM_K_VARIABLE);
    case SCOUT_SYMBOL_FIELD:
    case SCOUT_SYMBOL_PROPERTY:     return S(MSG_SYM_K_FIELD);
    case SCOUT_SYMBOL_LABEL:        return S(MSG_SYM_K_LABEL);
    case SCOUT_SYMBOL_MODULE:
    case SCOUT_SYMBOL_NAMESPACE:
    case SCOUT_SYMBOL_PACKAGE:      return S(MSG_SYM_K_MODULE);
    default:                        return S(MSG_SYM_K_OTHER);
    }
}

/**
 * @brief Return the group of a symbol kind.
 * @param kind SCOUT_SYMBOL_ value.
 * @return KG_ value (not KG_ALL).
 */
static int kind_group(int kind)
{
    switch (kind) {
    case SCOUT_SYMBOL_FUNCTION: case SCOUT_SYMBOL_PROCEDURE:
    case SCOUT_SYMBOL_METHOD: case SCOUT_SYMBOL_CONSTRUCTOR:   return KG_FUNCTION;
    case SCOUT_SYMBOL_STRUCT: case SCOUT_SYMBOL_CLASS: case SCOUT_SYMBOL_INTERFACE:
    case SCOUT_SYMBOL_ENUM: case SCOUT_SYMBOL_TYPE_ALIAS:       return KG_TYPE;
    case SCOUT_SYMBOL_MACRO:                                    return KG_MACRO;
    case SCOUT_SYMBOL_VARIABLE: case SCOUT_SYMBOL_FIELD:
    case SCOUT_SYMBOL_PROPERTY:                                 return KG_VARIABLE;
    case SCOUT_SYMBOL_CONSTANT: case SCOUT_SYMBOL_ENUM_MEMBER:  return KG_CONSTANT;
    default:                                                    return KG_OTHER;
    }
}

/**
 * @brief Render hook of the first column: the kind's letter in white on
 *        its colour. Graphics only, it may run during input handling.
 * @param hook The hook.
 * @param node The row's node.
 * @param msg The draw message.
 * @return LBCB_OK, or LBCB_UNKNOWN for other methods.
 */
static ULONG render_kind(struct Hook *hook, struct Node *node, struct LBDrawMsg *msg)
{
    struct RastPort *rp = msg->lbdm_RastPort;
    struct DrawInfo *dri = msg->lbdm_DrawInfo;
    struct Rectangle *b = &msg->lbdm_Bounds;
    ULONG index = 0;
    WORD size, x, y, w;
    int group;
    char letter;
    (void)hook;
    if (msg->lbdm_MethodID != LV_DRAW) return LBCB_UNKNOWN;
    if (dri == NULL) return LBCB_OK;
    SetAPen(rp, (UBYTE)dri->dri_Pens[msg->lbdm_State == LBR_SELECTED ? FILLPEN : BACKGROUNDPEN]);
    RectFill(rp, b->MinX, b->MinY, b->MaxX, b->MaxY);
    GetListBrowserNodeAttrs(node, LBNA_UserData, (ULONG)&index, TAG_END);
    if ((long)index >= sy.count) return LBCB_OK;
    group = kind_group(sy.items[index].kind);
    letter = group_letters[group];
    size = (WORD)(b->MaxY - b->MinY - 1);
    if (size > b->MaxX - b->MinX - 1) size = (WORD)(b->MaxX - b->MinX - 1);
    if (size < 4) return LBCB_OK;
    x = (WORD)(b->MinX + (b->MaxX - b->MinX + 1 - size) / 2);
    y = (WORD)(b->MinY + (b->MaxY - b->MinY + 1 - size) / 2);
    SetAPen(rp, (UBYTE)(sy.pens[group] >= 0 ? sy.pens[group] : dri->dri_Pens[FILLPEN]));
    RectFill(rp, x, y, (WORD)(x + size - 1), (WORD)(y + size - 1));
    w = (WORD)TextLength(rp, (STRPTR)&letter, 1);
    SetAPen(rp, (UBYTE)dri->dri_Pens[SHINEPEN]);
    SetDrMd(rp, JAM1);
    Move(rp, (WORD)(x + (size - w) / 2), (WORD)(y + (size - rp->TxHeight) / 2 + rp->TxBaseline));
    Text(rp, (STRPTR)&letter, 1);
    return LBCB_OK;
}

/**
 * @brief Obtain the pens of the groups' colours (once per screen).
 * @param app The application state.
 */
static void obtain_pens(EditorApp *app)
{
    int i;
    if (sy.cm != NULL || app->screen == NULL) return;
    sy.cm = app->screen->ViewPort.ColorMap;
    for (i = 1; i < KG_COUNT; ++i) {
        unsigned long c = group_colours[i];
        sy.pens[i] = ObtainBestPen(sy.cm, ((c >> 16) & 0xFF) * 0x01010101UL, ((c >> 8) & 0xFF) * 0x01010101UL,
                                   (c & 0xFF) * 0x01010101UL, OBP_Precision, PRECISION_GUI,
                                   OBP_FailIfBad, FALSE, TAG_END);
    }
}

/**
 * @brief Give the list its nodes (~0 detaches them for changes).
 * @param list The list.
 */
static void set_labels(struct List *list)
{
    if (sy.list == NULL) return;
    if (sy.win != NULL)
        SetGadgetAttrs((struct Gadget *)sy.list, sy.win, NULL, LISTBROWSER_Labels, (ULONG)list, TAG_END);
    else SetAttrs(sy.list, LISTBROWSER_Labels, (ULONG)list, TAG_END);
}

/**
 * @brief Show a text in the line below the list.
 * @param text The text.
 */
static void set_status(const char *text)
{
    static char shown[80];
    if (sy.status == NULL || strncmp(shown, text, sizeof(shown) - 1) == 0) return;
    strncpy(shown, text, sizeof(shown) - 1);
    shown[sizeof(shown) - 1] = '\0';
    if (sy.win != NULL)
        SetGadgetAttrs((struct Gadget *)sy.status, sy.win, NULL, GA_Text, (ULONG)shown, TAG_END);
    else SetAttrs(sy.status, GA_Text, (ULONG)shown, TAG_END);
}

/** @brief Free the rows (the list is detached). */
static void free_rows(void)
{
    struct Node *n;
    if (sy.nodes.lh_Head == NULL) NewList(&sy.nodes);
    while ((n = RemHead(&sy.nodes)) != NULL) FreeListBrowserNode(n);
}

/** @brief Free the symbols. */
static void free_items(void)
{
    long i;
    for (i = 0; i < sy.count; ++i) {
        free(sy.items[i].name);
        free(sy.items[i].detail);
        free(sy.items[i].file);
    }
    free(sy.items);
    sy.items = NULL;
    sy.count = 0;
}

/**
 * @brief Tell whether a symbol passes the filters: its name contains the
 *        filter text (without regard to case), its kind is the chosen one.
 * @param it The symbol.
 * @return Non-zero if it does.
 */
static int passes(const SymItem *it)
{
    const char *name = it->name;
    size_t n = strlen(sy.filter_text), len = strlen(name), i;
    if (sy.kind_on != KG_ALL && kind_group(it->kind) != sy.kind_on) return 0;
    if (n == 0) return 1;
    for (i = 0; i + n <= len; ++i)
        if (Strnicmp((STRPTR)name + i, (STRPTR)sy.filter_text, (LONG)n) == 0) return 1;
    return 0;
}

/**
 * @brief Build the rows from the symbols and the filter.
 */
static void rebuild(void)
{
    char text[80];
    long i, shown = 0;
    set_labels((struct List *)~0);
    free_rows();
    for (i = 0; i < sy.count; ++i) {
        const SymItem *it = &sy.items[i];
        struct Node *n;
        if (!passes(it)) continue;
        if (it->file != NULL)
            snprintf(sy.where_text, sizeof(sy.where_text), "%s:%ld", (const char *)FilePart((STRPTR)it->file),
                     it->line);
        else
            snprintf(sy.where_text, sizeof(sy.where_text), "%ld", it->line);
        n = AllocListBrowserNode(COL_COUNT, LBNA_UserData, (ULONG)i,
            LBNA_Column, COL_KIND, LBNCA_RenderHook, (ULONG)&sy.render_hook,
            LBNCA_HookHeight, (ULONG)sy.row_height,
            LBNA_Column, COL_NAME, LBNCA_CopyText, TRUE, LBNCA_Text, (ULONG)it->name,
            LBNA_Column, COL_WHERE, LBNCA_CopyText, TRUE, LBNCA_Text, (ULONG)sy.where_text,
            TAG_END);
        if (n == NULL) continue;
        AddTail(&sy.nodes, n);
        ++shown;
    }
    set_labels(&sy.nodes);
    snprintf(text, sizeof(text), S(MSG_SYM_COUNT), shown);
    set_status(text);
}

/**
 * @brief qsort() order of the symbols: by the column the list is sorted by
 *        (kind, name, or file and line), then by name and position; the
 *        other way round with sort_rev.
 * @param a A symbol.
 * @param b Another symbol.
 * @return Less than, equal to or greater than 0.
 */
static int order(const void *a, const void *b)
{
    const SymItem *x = (const SymItem *)a, *y = (const SymItem *)b;
    int c = 0;
    if (sy.sort_col == COL_KIND) c = kind_group(x->kind) - kind_group(y->kind);
    if (c == 0 && sy.sort_col == COL_WHERE) {
        if (x->file != NULL && y->file != NULL) c = Stricmp((STRPTR)x->file, (STRPTR)y->file);
        if (c == 0) c = x->line < y->line ? -1 : x->line > y->line;
        if (c == 0) c = x->col < y->col ? -1 : x->col > y->col;
    }
    if (c == 0) c = Stricmp((STRPTR)x->name, (STRPTR)y->name);
    if (c == 0) c = strcmp(x->name, y->name);
    if (c == 0 && x->file != NULL && y->file != NULL) c = Stricmp((STRPTR)x->file, (STRPTR)y->file);
    if (c == 0) c = x->line < y->line ? -1 : x->line > y->line;
    return sy.sort_rev ? -c : c;
}

/** @brief Sort the symbols by the chosen column. */
static void sort_items(void)
{
    if (sy.count > 1) qsort(sy.items, (size_t)sy.count, sizeof(SymItem), order);
}

/**
 * @brief Show the sort arrow at the column the list is sorted by, in its
 *        direction. Set while the rows are detached, so the gadget does
 *        not sort them itself (by text, which is wrong for the lines).
 */
static void show_sort_arrow(void)
{
    if (sy.list == NULL || sy.columns == NULL) return;
    SetLBColumnInfoAttrs(sy.columns, LBCIA_Column, (ULONG)sy.sort_col,
                         LBCIA_SortDirection, (ULONG)(sy.sort_rev ? LBMSORT_REVERSE : LBMSORT_FORWARD), TAG_END);
    if (sy.win != NULL)
        SetGadgetAttrs((struct Gadget *)sy.list, sy.win, NULL, LISTBROWSER_Labels, (ULONG)~0,
                       LISTBROWSER_ColumnInfo, (ULONG)sy.columns, LISTBROWSER_SortColumn, (ULONG)sy.sort_col, TAG_END);
}

/**
 * @brief Copy a string with malloc().
 * @param s The string.
 * @return The copy, or NULL.
 */
static char *dup_str(const char *s)
{
    char *copy = malloc(strlen(s) + 1);
    if (copy != NULL) strcpy(copy, s);
    return copy;
}

void symlist_symbols(EditorApp *app, unsigned long doc_number, const char *json)
{
    JsonValue *root;
    const JsonValue *list;
    size_t i, n;
    (void)app;
    /* one request at a time: this is its answer; it is used only if it is
     * for what is shown now (not for an earlier choice) */
    sy.waiting = 0;
    if ((sy.scope_on == SCOPE_FILE) != (doc_number != 0) || doc_number != sy.doc || sy.win == NULL) {
        sy.asked = 0;
        return;
    }
    set_labels((struct List *)~0);
    free_rows();
    free_items();
    root = json != NULL ? json_parse(json, NULL) : NULL;
    list = root != NULL ? json_obj_get_array(root, "symbols") : NULL;
    n = list != NULL ? json_array_length(list) : 0;
    if (n > 0) sy.items = calloc(n, sizeof(SymItem));
    for (i = 0; sy.items != NULL && i < n; ++i) {
        const JsonValue *v = json_array_get(list, i);
        const char *name = json_obj_get_string(v, "name", NULL);
        const char *file = json_obj_get_string(v, "file", NULL);
        const char *detail = json_obj_get_string(v, "detail", NULL);
        const JsonValue *sel = json_obj_get_obj(v, "selection");
        const JsonValue *start = sel != NULL ? json_obj_get_array(sel, "start") : NULL;
        SymItem *it = &sy.items[sy.count];
        if (name == NULL || strcmp(json_obj_get_string(v, "scope", "global"), "local") == 0) continue;
        it->name = dup_str(name);
        it->file = file != NULL ? dup_str(file) : NULL;
        it->detail = detail != NULL ? dup_str(detail) : NULL;
        if (it->name == NULL || (file != NULL && it->file == NULL)) {
            free(it->name);
            free(it->file);
            free(it->detail);
            continue;
        }
        it->kind = (int)json_obj_get_int(v, "kind", 0);
        it->line = start != NULL && json_array_length(start) > 0 ? json_array_get(start, 0)->u.num_val.int_val : 1;
        it->col = start != NULL && json_array_length(start) > 1 ? json_array_get(start, 1)->u.num_val.int_val : 1;
        ++sy.count;
    }
    json_value_free(root);
    sort_items();
    if (json == NULL) {
        set_labels(&sy.nodes);
        set_status(S(MSG_SYM_FAILED));
        return;
    }
    rebuild();
}

/**
 * @brief Ask for the symbols the list shows now, when something changed.
 * @param app The application state.
 * @param force Ask even if nothing changed (scope chosen, window opened).
 */
static void ask(EditorApp *app, int force)
{
    Document *doc = app->active;
    int why;
    if (sy.win == NULL) return;
    if (sy.scope_on == SCOPE_FILE) {
        unsigned long number = doc != NULL ? doc->number : 0;
        unsigned long gen = doc != NULL ? doc->scout_generation : 0;
        if (!force && sy.asked && number == sy.doc && gen == sy.generation) return;
        if (number != sy.doc) {
            /* another document: its symbols are not known yet */
            set_labels((struct List *)~0);
            free_rows();
            free_items();
            set_labels(&sy.nodes);
        }
        sy.doc = number;
        sy.generation = gen;
    } else {
        unsigned long gen = scoutlink_project_generation();
        if (!force && sy.asked && gen == sy.generation) return;
        sy.doc = 0;
        sy.generation = gen;
    }
    if ((why = scoutlink_unavailable(app)) != 0) {
        set_status(S(why == 1 ? MSG_SYM_OFF : MSG_SYM_WAIT));
        sy.asked = 0;           /* again when Scout is there */
        return;
    }
    if (sy.scope_on == SCOPE_FILE && (doc == NULL || doc->path == NULL)) {
        set_status(S(MSG_SYM_NOFILE));
        sy.asked = 1;
        return;
    }
    if (sy.scope_on == SCOPE_FILE && doc->scout_generation == 0) {
        set_status(S(MSG_SYM_WAIT));    /* not analysed yet: wait for it */
        sy.asked = 1;
        return;
    }
    if (!scoutlink_request_symbols(app, sy.scope_on == SCOPE_FILE ? doc : NULL)) {
        set_status(S(sy.scope_on == SCOPE_PROJECT ? MSG_SYM_NOPROJECT : MSG_SYM_WAIT));
        sy.asked = 0;
        return;
    }
    sy.waiting = 1;
    sy.asked = 1;
}

/**
 * @brief Open the classes the window needs (once).
 * @param app The application state (for the requester).
 * @return Non-zero if all are available.
 */
static int open_classes(EditorApp *app)
{
    if (ListBrowserBase == NULL) ListBrowserBase = OpenLibrary((STRPTR)"gadgets/listbrowser.gadget", 44);
    if (ChooserBase == NULL) ChooserBase = OpenLibrary((STRPTR)"gadgets/chooser.gadget", 44);
    if (StringBase == NULL) StringBase = OpenLibrary((STRPTR)"gadgets/string.gadget", 44);
    if (ListBrowserBase != NULL && ChooserBase != NULL && StringBase != NULL) return 1;
    ui_error(app, APP_NAME, S(MSG_SYM_NEEDCLASSES));
    return 0;
}

int symlist_is_open(void)
{
    return sy.win != NULL;
}

void symlist_remember(EditorApp *app)
{
    if (sy.win != NULL) toolwin_remember(sy.win, app->sym_box);
}

void symlist_open(EditorApp *app)
{
    WORD box[4];
    int i;
    if (sy.win != NULL) {
        WindowToFront(sy.win);
        return;
    }
    if (app->window == NULL || !open_classes(app)) return;
    if (sy.nodes.lh_Head == NULL) NewList(&sy.nodes);
    if (sy.columns == NULL &&
        (sy.columns = AllocLBColumnInfo(COL_COUNT,
                                        LBCIA_Column, COL_KIND, LBCIA_Title, (ULONG)S(MSG_SYM_COL_KIND),
                                        LBCIA_Weight, 8, LBCIA_Sortable, TRUE, LBCIA_DraggableSeparator, TRUE,
                                        LBCIA_Column, COL_NAME, LBCIA_Title, (ULONG)S(MSG_SYM_COL_NAME),
                                        LBCIA_Weight, 62, LBCIA_Sortable, TRUE, LBCIA_DraggableSeparator, TRUE,
                                        LBCIA_Column, COL_WHERE, LBCIA_Title, (ULONG)S(MSG_SYM_COL_WHERE),
                                        LBCIA_Weight, 30, LBCIA_Sortable, TRUE, TAG_END)) == NULL)
        return;
    obtain_pens(app);
    sy.render_hook.h_Entry = (ULONG (*)())HookEntry;
    sy.render_hook.h_SubEntry = (ULONG (*)())render_kind;
    sy.row_height = (WORD)((app->screen != NULL && app->screen->Font != NULL ?
                            app->screen->Font->ta_YSize : 8) + 2);
    if (!sy.asked && sy.count == 0) sy.sort_col = sy.scope_on == SCOPE_FILE ? COL_WHERE : COL_NAME;
    for (i = 0; i < SCOPE_COUNT; ++i) sy.scope_labels[i] = (STRPTR)S(scope_texts[i]);
    sy.scope_labels[SCOPE_COUNT] = NULL;
    for (i = 0; i < KG_COUNT; ++i) sy.kind_labels[i] = (STRPTR)S(group_texts[i]);
    sy.kind_labels[KG_COUNT] = NULL;
    toolwin_box(app, app->sym_box, MIN_WIDTH, MIN_HEIGHT, 0, box);

    sy.scope = NewObject(CHOOSER_GetClass(), NULL, GA_ID, SG_SCOPE, GA_RelVerify, TRUE,
        CHOOSER_PopUp, TRUE, CHOOSER_LabelArray, (ULONG)sy.scope_labels,
        CHOOSER_Selected, (ULONG)sy.scope_on, TAG_END);
    sy.filter = NewObject(STRING_GetClass(), NULL, GA_ID, SG_FILTER, GA_RelVerify, TRUE,
        GA_TabCycle, TRUE, STRINGA_MaxChars, FILTER_MAX, STRINGA_TextVal, (ULONG)sy.filter_text,
        TAG_END);
    sy.kind = NewObject(CHOOSER_GetClass(), NULL, GA_ID, SG_KIND, GA_RelVerify, TRUE,
        CHOOSER_PopUp, TRUE, CHOOSER_LabelArray, (ULONG)sy.kind_labels,
        CHOOSER_Selected, (ULONG)sy.kind_on, TAG_END);
    sy.list = NewObject(LISTBROWSER_GetClass(), NULL, GA_ID, SG_LIST, GA_RelVerify, TRUE,
        LISTBROWSER_Labels, (ULONG)&sy.nodes, LISTBROWSER_ColumnInfo, (ULONG)sy.columns,
        LISTBROWSER_ColumnTitles, TRUE, LISTBROWSER_TitleClickable, TRUE, LISTBROWSER_ShowSelected, TRUE,
        LISTBROWSER_AutoWheel, TRUE, TAG_END);
    sy.status = NewObject(BUTTON_GetClass(), NULL, GA_ID, SG_STATUS, GA_ReadOnly, TRUE,
        GA_Underscore, 0, GA_Text, (ULONG)"", BUTTON_BevelStyle, BVS_NONE, BUTTON_Transparent, TRUE,
        BUTTON_Justification, BCJ_LEFT, TAG_END);
    sy.winobj = NewObject(WINDOW_GetClass(), NULL,
        WA_Title, (ULONG)S(MSG_SYM_TITLE),
        WA_PubScreen, (ULONG)app->screen,
        WA_Left, (ULONG)box[0], WA_Top, (ULONG)box[1],
        WA_Width, (ULONG)box[2], WA_Height, (ULONG)box[3],
        WA_DepthGadget, TRUE, WA_DragBar, TRUE, WA_CloseGadget, TRUE, WA_SizeGadget, TRUE,
        WA_Activate, FALSE,
        WA_IDCMP, IDCMP_RAWKEY,
        WINDOW_ParentGroup, (ULONG)(sy.root = NewObject(LAYOUT_GetClass(), NULL,
            LAYOUT_Orientation, LAYOUT_ORIENT_VERT,
            LAYOUT_SpaceOuter, TRUE,
            LAYOUT_LeftSpacing, 6, LAYOUT_RightSpacing, 6,
            LAYOUT_TopSpacing, 6, LAYOUT_BottomSpacing, 6,
            LAYOUT_InnerSpacing, 4,
            LAYOUT_DeferLayout, TRUE,
            LAYOUT_AddChild, (ULONG)sy.scope,
            CHILD_Label, (ULONG)NewObject(LABEL_GetClass(), NULL, LABEL_Text, (ULONG)S(MSG_SYM_SHOW), TAG_END),
            CHILD_WeightedHeight, 0,
            LAYOUT_AddChild, (ULONG)sy.filter,
            CHILD_Label, (ULONG)NewObject(LABEL_GetClass(), NULL, LABEL_Text, (ULONG)S(MSG_SYM_FILTER), TAG_END),
            CHILD_WeightedHeight, 0,
            LAYOUT_AddChild, (ULONG)sy.kind,
            CHILD_Label, (ULONG)NewObject(LABEL_GetClass(), NULL, LABEL_Text, (ULONG)S(MSG_SYM_KIND), TAG_END),
            CHILD_WeightedHeight, 0,
            LAYOUT_AddChild, (ULONG)sy.list,
            LAYOUT_AddChild, (ULONG)sy.status,
            CHILD_WeightedHeight, 0,
            LAYOUT_AddChild, (ULONG)NewObject(LAYOUT_GetClass(), NULL,
                LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ,
                LAYOUT_AddChild, (ULONG)NewObject(BUTTON_GetClass(), NULL, GA_ID, SG_GOTO,
                    GA_RelVerify, TRUE, GA_Text, (ULONG)S(MSG_SYM_GOTO), BUTTON_TextPadding, TRUE, TAG_END),
                CHILD_WeightedWidth, 0,
                LAYOUT_AddChild, (ULONG)NewObject(LAYOUT_GetClass(), NULL, TAG_END),
                TAG_END),
            CHILD_WeightedHeight, 0,
            TAG_END)),
        TAG_END);
    if (sy.winobj == NULL || (sy.win = (struct Window *)DoMethod(sy.winobj, WM_OPEN, NULL)) == NULL) {
        if (sy.winobj != NULL) DisposeObject(sy.winobj);
        sy.winobj = NULL;
        sy.list = sy.scope = sy.filter = sy.kind = sy.status = sy.root = NULL;
        return;
    }
    if (sy.win->LeftEdge != box[0] || sy.win->TopEdge != box[1] ||
        sy.win->Width != box[2] || sy.win->Height != box[3])
        ChangeWindowBox(sy.win, box[0], box[1], box[2], box[3]);
    sy.asked = 0;
    sy.doc = 0;
    show_sort_arrow();
    set_labels(&sy.nodes);
    ask(app, 1);
}

void symlist_close(EditorApp *app)
{
    if (sy.winobj == NULL) return;
    symlist_remember(app);
    set_labels((struct List *)~0);
    DoMethod(sy.winobj, WM_CLOSE, NULL);
    DisposeObject(sy.winobj);
    sy.winobj = NULL;
    sy.win = NULL;
    sy.list = sy.scope = sy.filter = sy.kind = sy.status = sy.root = NULL;
    free_rows();
    free_items();
    sy.waiting = 0;
}

ULONG symlist_sigmask(void)
{
    ULONG sig = 0;
    if (sy.win != NULL) GetAttr(WINDOW_SigMask, sy.winobj, &sig);
    return sig;
}

/**
 * @brief Show the kind and declaration of the selected symbol in the line
 *        below the list (instead of help bubbles).
 */
static void show_selected(void)
{
    char text[200];
    struct Node *node = (struct Node *)get(sy.list, LISTBROWSER_SelectedNode);
    ULONG index = 0;
    const SymItem *it;
    if (node == NULL) return;
    GetListBrowserNodeAttrs(node, LBNA_UserData, (ULONG)&index, TAG_END);
    if ((long)index >= sy.count) return;
    it = &sy.items[index];
    snprintf(text, sizeof(text), "%s: %s", kind_text(it->kind), it->detail != NULL ? it->detail : it->name);
    set_status(text);
}

/**
 * @brief Go to the selected symbol and bring the main window to the
 *        front, with the input focus.
 * @param app The application state.
 */
static void go_to_selected(EditorApp *app)
{
    struct Node *node = (struct Node *)get(sy.list, LISTBROWSER_SelectedNode);
    ULONG index = 0;
    const SymItem *it;
    Document *doc;
    if (node == NULL) return;
    GetListBrowserNodeAttrs(node, LBNA_UserData, (ULONG)&index, TAG_END);
    if ((long)index >= sy.count) return;
    it = &sy.items[index];
    if (it->file != NULL) {
        document_go_to(app, it->file, it->line, it->col);
    } else {
        /* the document the symbols are of, if it is still open */
        for (doc = (Document *)app->documents.lh_Head; doc->node.ln_Succ;
             doc = (Document *)doc->node.ln_Succ)
            if (doc->number == sy.doc) break;
        if (doc->node.ln_Succ == NULL) return;
        if (doc != app->active) document_activate(app, doc);
        document_go_to(app, NULL, it->line, it->col);
    }
    if (app->window != NULL) {
        ActivateWindow(app->window);
        app->focus_editor = 1;          /* when the window is active */
    }
}

void symlist_handle(EditorApp *app)
{
    ULONG result;
    WORD code;
    if (sy.win == NULL) return;
    while ((result = DoMethod(sy.winobj, WM_HANDLEINPUT, &code)) != WMHI_LASTMSG) {
        switch (result & WMHI_CLASSMASK) {
        case WMHI_CLOSEWINDOW:
            symlist_close(app);
            ui_set_menu_check(app, MID_SYMLIST, 0);
            return;
        case WMHI_RAWKEY:
            if ((result & WMHI_KEYMASK) == RAWKEY_WHEEL_UP || (result & WMHI_KEYMASK) == RAWKEY_WHEEL_DOWN) {
                LONG top = (LONG)get(sy.list, LISTBROWSER_Top);
                top += (result & WMHI_KEYMASK) == RAWKEY_WHEEL_UP ? -WHEEL_ROWS : WHEEL_ROWS;
                if (top < 0) top = 0;
                SetGadgetAttrs((struct Gadget *)sy.list, sy.win, NULL, LISTBROWSER_Top, (ULONG)top, TAG_END);
            }
            break;
        case WMHI_GADGETUP:
            switch (result & WMHI_GADGETMASK) {
            case SG_SCOPE:
                sy.scope_on = (int)get(sy.scope, CHOOSER_Selected);
                sy.sort_col = sy.scope_on == SCOPE_FILE ? COL_WHERE : COL_NAME;
                sy.sort_rev = 0;
                show_sort_arrow();
                set_labels((struct List *)~0);
                free_rows();
                free_items();
                set_labels(&sy.nodes);
                sy.doc = 0;
                ask(app, 1);
                break;
            case SG_FILTER: {
                const char *t = (const char *)get(sy.filter, STRINGA_TextVal);
                strncpy(sy.filter_text, t != NULL ? t : "", sizeof(sy.filter_text) - 1);
                sy.filter_text[sizeof(sy.filter_text) - 1] = '\0';
                rebuild();
                /* Return ended the input: the field keeps the focus */
                ActivateLayoutGadget((struct Gadget *)sy.root, sy.win, NULL, (ULONG)sy.filter);
                break;
            }
            case SG_KIND:
                sy.kind_on = (int)get(sy.kind, CHOOSER_Selected);
                rebuild();
                break;
            case SG_LIST: {
                ULONG event = get(sy.list, LISTBROWSER_RelEvent);
                if (event & LBRE_TITLECLICK) {
                    int col = (int)get(sy.list, LISTBROWSER_RelColumn);
                    if (col == sy.sort_col) sy.sort_rev = !sy.sort_rev;
                    else { sy.sort_col = col; sy.sort_rev = 0; }
                    set_labels((struct List *)~0);
                    free_rows();
                    sort_items();
                    show_sort_arrow();
                    rebuild();
                } else if (event & LBRE_DOUBLECLICK) {
                    go_to_selected(app);
                } else {
                    show_selected();
                }
                break;
            }
            case SG_GOTO:
                go_to_selected(app);
                break;
            }
            break;
        }
    }
}

void symlist_update(EditorApp *app)
{
    long waiting;
    if (sy.win == NULL || sy.waiting) return;
    ask(app, 0);
    if (sy.scope_on == SCOPE_PROJECT && !sy.waiting && (waiting = scoutlink_indexing()) >= 0) {
        char text[80];
        snprintf(text, sizeof(text), S(MSG_SYM_INDEXING), waiting);
        set_status(text);
    }
}

void symlist_cleanup(EditorApp *app)
{
    int i;
    symlist_close(app);
    if (sy.columns != NULL) {
        FreeLBColumnInfo(sy.columns);
        sy.columns = NULL;
    }
    if (sy.cm != NULL) {
        for (i = 0; i < KG_COUNT; ++i)
            if (sy.pens[i] >= 0) ReleasePen(sy.cm, (ULONG)sy.pens[i]);
        for (i = 0; i < KG_COUNT; ++i) sy.pens[i] = -1;
        sy.cm = NULL;
    }
}
