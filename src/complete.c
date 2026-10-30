/**
 * @file complete.c
 * @brief Edit/Complete: a list of names that could complete the word at
 *        the cursor, from TinyDev-Scout, in a small window at the cursor.
 *
 * The part of the word before the cursor is the prefix; Scout answers
 * COMPLETE with the locals of the function there, the file's symbols, the
 * project's and the language's keywords. The list opens below the cursor
 * (its position is worked out from the editor's box, the first line shown
 * and the width of the text before the cursor: texteditor.gadget does not
 * tell where its cursor is drawn, so it may be a little off) and takes
 * the keyboard:
 * - Up and Down choose, Return or Tab put the chosen name in place of the
 *   prefix (GM_TEXTEDITOR_MarkText and Replace), Esc closes;
 * - characters of a name go into the editor and narrow the list, Backspace
 *   takes the last one back; any other character goes into the editor and
 *   closes the list, as does a click elsewhere.
 * The first column shows the kind as a coloured letter, like the symbol
 * list (K for a keyword). After Scout's names come the other words of the
 * document that start with the prefix (W; not those in comments and
 * strings, found with the document's highlighter): so a name Scout cannot know,
 * such as a function of a system header the project does not index
 * (printf), is still offered once it is used in the file.
 */
#include "editor.h"
#include "json.h"
#include "scout_protocol.h"

#include <intuition/gadgetclass.h>
#include <classes/window.h>
#include <gadgets/layout.h>
#include <gadgets/listbrowser.h>
#include <gadgets/texteditor.h>
#include <libraries/gadtools.h>
#include <exec/memory.h>

#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/window.h>
#include <proto/layout.h>
#include <proto/listbrowser.h>
#include <proto/utility.h>
#include <clib/alib_protos.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern struct Library *ListBrowserBase;     /* shared with the settings window */

/** Most items shown. */
#define MAX_ITEMS 50
/** Longest prefix. */
#define PREFIX_MAX 64
/** Rows the list shows. */
#define ROWS 8
/** Width of the window (pixels). */
#define WIN_WIDTH 280
/** Width of the left bar of the editor (bookmarks.c). */
#define LEFTBAR_WIDTH 20
/** Rawkey codes of the cursor keys. */
#define KEY_UP 0x4C
#define KEY_DOWN 0x4D           /**< Down */

/** A name of the list. */
typedef struct CompItem {
    char *label;        /**< the name (malloc()) */
    char *detail;       /**< its declaration (malloc(), may be NULL) */
    int kind;           /**< SCOUT_SYMBOL_ value, 0 for a keyword, WORD_KIND for a word of the document */
} CompItem;

/** Kind of a word of the document (not from Scout). */
#define WORD_KIND (-2)

/** The state of the list. */
static struct {
    Object *winobj, *list;
    struct Window *win;
    struct List nodes;              /* LBNA_UserData: index in items */
    struct ColumnInfo *columns;
    struct Hook render_hook;
    CompItem *items;                /* all names of the answer */
    long count;
    char prefix[PREFIX_MAX];        /* the prefix typed so far */
    unsigned long doc;              /* the document (number) */
    long line;                      /* the cursor's line (from 0) */
    long start;                     /* column where the prefix starts (from 0) */
    int waiting;                    /* the answer is on its way */
    WORD row_height;
} cp;

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
 * @brief Tell whether a character may be part of a name.
 * @param c The character.
 * @return Non-zero if it may.
 */
static int name_char(unsigned char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

/**
 * @brief Letter and severity of a kind (the colour comes from diag_pen()
 *        of the kind's group, as in the symbol list).
 * @param kind SCOUT_SYMBOL_ value, 0 for a keyword.
 * @param[out] severity The colour to use, as a SCOUT_SEVERITY_ value.
 * @return The letter.
 */
static char kind_letter(int kind, int *severity)
{
    switch (kind) {
    case 0: *severity = SCOUT_SEVERITY_HINT; return 'K';
    case WORD_KIND: *severity = SCOUT_SEVERITY_HINT; return 'W';
    case SCOUT_SYMBOL_FUNCTION: case SCOUT_SYMBOL_PROCEDURE:
    case SCOUT_SYMBOL_METHOD: case SCOUT_SYMBOL_CONSTRUCTOR: *severity = SCOUT_SEVERITY_INFO; return 'F';
    case SCOUT_SYMBOL_STRUCT: case SCOUT_SYMBOL_CLASS: case SCOUT_SYMBOL_ENUM:
    case SCOUT_SYMBOL_TYPE_ALIAS: *severity = SCOUT_SEVERITY_INFO; return 'T';
    case SCOUT_SYMBOL_MACRO: *severity = SCOUT_SEVERITY_WARNING; return 'M';
    case SCOUT_SYMBOL_VARIABLE: case SCOUT_SYMBOL_FIELD: *severity = SCOUT_SEVERITY_ERROR; return 'V';
    case SCOUT_SYMBOL_CONSTANT: case SCOUT_SYMBOL_ENUM_MEMBER: *severity = SCOUT_SEVERITY_ERROR; return 'C';
    default: *severity = SCOUT_SEVERITY_HINT; return '?';
    }
}

/**
 * @brief Render hook of the first column: the kind's letter on a colour.
 *        Graphics only, it may run during input handling.
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
    int severity;
    char letter;
    (void)hook;
    if (msg->lbdm_MethodID != LV_DRAW) return LBCB_UNKNOWN;
    if (dri == NULL) return LBCB_OK;
    SetAPen(rp, (UBYTE)dri->dri_Pens[msg->lbdm_State == LBR_SELECTED ? FILLPEN : BACKGROUNDPEN]);
    RectFill(rp, b->MinX, b->MinY, b->MaxX, b->MaxY);
    GetListBrowserNodeAttrs(node, LBNA_UserData, (ULONG)&index, TAG_END);
    if ((long)index >= cp.count) return LBCB_OK;
    letter = kind_letter(cp.items[index].kind, &severity);
    size = (WORD)(b->MaxY - b->MinY - 1);
    if (size > b->MaxX - b->MinX - 1) size = (WORD)(b->MaxX - b->MinX - 1);
    if (size < 4) return LBCB_OK;
    x = (WORD)(b->MinX + (b->MaxX - b->MinX + 1 - size) / 2);
    y = (WORD)(b->MinY + (b->MaxY - b->MinY + 1 - size) / 2);
    SetAPen(rp, (UBYTE)(cp.items[index].kind == 0 || cp.items[index].kind == WORD_KIND ?
                        dri->dri_Pens[SHADOWPEN] : diag_pen(severity, dri->dri_Pens[FILLPEN])));
    RectFill(rp, x, y, (WORD)(x + size - 1), (WORD)(y + size - 1));
    w = (WORD)TextLength(rp, (STRPTR)&letter, 1);
    SetAPen(rp, (UBYTE)dri->dri_Pens[SHINEPEN]);
    SetDrMd(rp, JAM1);
    Move(rp, (WORD)(x + (size - w) / 2), (WORD)(y + (size - rp->TxHeight) / 2 + rp->TxBaseline));
    Text(rp, (STRPTR)&letter, 1);
    return LBCB_OK;
}

/** @brief Free the rows and the names. */
static void free_all(void)
{
    struct Node *n;
    long i;
    if (cp.nodes.lh_Head == NULL) NewList(&cp.nodes);
    while ((n = RemHead(&cp.nodes)) != NULL) FreeListBrowserNode(n);
    for (i = 0; i < cp.count; ++i) {
        free(cp.items[i].label);
        free(cp.items[i].detail);
    }
    free(cp.items);
    cp.items = NULL;
    cp.count = 0;
}

/**
 * @brief Find the document of the list.
 * @param app The application state.
 * @return The document if it is still the active one, otherwise NULL.
 */
static Document *the_document(EditorApp *app)
{
    return app->active != NULL && app->active->number == cp.doc ? app->active : NULL;
}

/**
 * @brief Close the list and give the editor the input focus back.
 * @param app The application state.
 */
static void close_list(EditorApp *app)
{
    if (cp.winobj != NULL) {
        if (cp.list != NULL && cp.win != NULL)
            SetGadgetAttrs((struct Gadget *)cp.list, cp.win, NULL, LISTBROWSER_Labels, (ULONG)~0, TAG_END);
        DoMethod(cp.winobj, WM_CLOSE, NULL);
        DisposeObject(cp.winobj);
        cp.winobj = NULL;
        cp.win = NULL;
        cp.list = NULL;
    }
    free_all();
    if (app->window != NULL) {
        ActivateWindow(app->window);
        app->focus_editor = 1;          /* when the window is active */
    }
}

/**
 * @brief Build the rows: the names that start with the prefix (without
 *        regard to case); the first is chosen.
 * @return The number of rows.
 */
static long rebuild(void)
{
    long i, shown = 0;
    size_t n = strlen(cp.prefix);
    struct Node *node;
    if (cp.list != NULL && cp.win != NULL)
        SetGadgetAttrs((struct Gadget *)cp.list, cp.win, NULL, LISTBROWSER_Labels, (ULONG)~0, TAG_END);
    while ((node = RemHead(&cp.nodes)) != NULL) FreeListBrowserNode(node);
    for (i = 0; i < cp.count; ++i) {
        if (Strnicmp((STRPTR)cp.items[i].label, (STRPTR)cp.prefix, (LONG)n) != 0) continue;
        node = AllocListBrowserNode(3, LBNA_UserData, (ULONG)i,
            LBNA_Column, 0, LBNCA_RenderHook, (ULONG)&cp.render_hook, LBNCA_HookHeight, (ULONG)cp.row_height,
            LBNA_Column, 1, LBNCA_CopyText, TRUE, LBNCA_Text, (ULONG)cp.items[i].label,
            LBNA_Column, 2, LBNCA_CopyText, TRUE,
            LBNCA_Text, (ULONG)(cp.items[i].detail != NULL ? cp.items[i].detail : ""), TAG_END);
        if (node != NULL) {
            AddTail(&cp.nodes, node);
            ++shown;
        }
    }
    if (cp.list != NULL && cp.win != NULL)
        SetGadgetAttrs((struct Gadget *)cp.list, cp.win, NULL, LISTBROWSER_Labels, (ULONG)&cp.nodes,
                       LISTBROWSER_Selected, 0, LISTBROWSER_MakeVisible, 0, TAG_END);
    return shown;
}

/**
 * @brief Work out where the cursor is drawn on the screen.
 * @param app The application state.
 * @param doc The document.
 * @param[out] x Left edge for the list.
 * @param[out] y Top edge for the list (below the cursor's line).
 */
static void cursor_box(EditorApp *app, Document *doc, WORD *x, WORD *y)
{
    struct Gadget *g = (struct Gadget *)doc->editor;
    struct RastPort *rp = &app->screen->RastPort;
    ULONG first = 0, hfirst = 0;
    WORD h = app->screen->Font != NULL ? (WORD)app->screen->Font->ta_YSize : 8;
    WORD left = (WORD)(g->LeftEdge + LEFTBAR_WIDTH + 4);
    char *text = (char *)DoMethod(doc->editor, GM_TEXTEDITOR_ExportText, NULL);
    GetAttr(GA_TEXTEDITOR_Prop_First, doc->editor, &first);
    GetAttr(GA_TEXTEDITOR_HProp_First, doc->editor, &hfirst);
    if (app->line_numbers) left = (WORD)(left + TextLength(rp, (STRPTR)"00000 ", 6));
    if (text != NULL) {
        const char *p = text;
        long n = 0;
        while (*p != '\0' && n < cp.line) if (*p++ == '\n') ++n;
        left = (WORD)(left + TextLength(rp, (STRPTR)p, (UWORD)cp.start));
        FreeVec(text);
    }
    *x = (WORD)(app->window->LeftEdge + left - (WORD)hfirst);
    *y = (WORD)(app->window->TopEdge + g->TopEdge + ((WORD)(cp.line - (long)first) + 1) * h + 2);
}

/**
 * @brief Open the list window at the cursor.
 * @param app The application state.
 * @param doc The document.
 * @return Non-zero if it is open.
 */
static int open_list(EditorApp *app, Document *doc)
{
    WORD x, y, height;
    if (ListBrowserBase == NULL &&
        (ListBrowserBase = OpenLibrary((STRPTR)"gadgets/listbrowser.gadget", 44)) == NULL)
        return 0;
    if (cp.columns == NULL &&
        (cp.columns = AllocLBColumnInfo(3, LBCIA_Column, 0, LBCIA_Weight, 7,
                                        LBCIA_Column, 1, LBCIA_Weight, 43,
                                        LBCIA_Column, 2, LBCIA_Weight, 50, TAG_END)) == NULL)
        return 0;
    cp.render_hook.h_Entry = (ULONG (*)())HookEntry;
    cp.render_hook.h_SubEntry = (ULONG (*)())render_kind;
    cp.row_height = (WORD)((app->screen->Font != NULL ? app->screen->Font->ta_YSize : 8) + 2);
    height = (WORD)(ROWS * cp.row_height + 8);
    cursor_box(app, doc, &x, &y);
    /* in the screen; above the line if there is no room below */
    if (x + WIN_WIDTH > app->screen->Width) x = (WORD)(app->screen->Width - WIN_WIDTH);
    if (x < 0) x = 0;
    if (y + height > app->screen->Height) y = (WORD)(y - height - cp.row_height - 4);
    if (y < 0) y = 0;
    cp.list = NewObject(LISTBROWSER_GetClass(), NULL, GA_RelVerify, TRUE,
        LISTBROWSER_Labels, (ULONG)&cp.nodes, LISTBROWSER_ColumnInfo, (ULONG)cp.columns,
        LISTBROWSER_ShowSelected, TRUE, LISTBROWSER_Selected, 0, TAG_END);
    cp.winobj = NewObject(WINDOW_GetClass(), NULL,
        WA_PubScreen, (ULONG)app->screen,
        WA_Left, (ULONG)x, WA_Top, (ULONG)y, WA_Width, WIN_WIDTH, WA_Height, (ULONG)height,
        WA_Borderless, TRUE, WA_Activate, TRUE, WA_RMBTrap, TRUE,
        WA_IDCMP, IDCMP_VANILLAKEY | IDCMP_RAWKEY | IDCMP_INACTIVEWINDOW,
        WINDOW_ParentGroup, (ULONG)NewObject(LAYOUT_GetClass(), NULL,
            LAYOUT_Orientation, LAYOUT_ORIENT_VERT,
            LAYOUT_BevelStyle, BVS_BUTTON,
            LAYOUT_AddChild, (ULONG)cp.list,
            TAG_END),
        TAG_END);
    if (cp.winobj == NULL || (cp.win = (struct Window *)DoMethod(cp.winobj, WM_OPEN, NULL)) == NULL) {
        if (cp.winobj != NULL) DisposeObject(cp.winobj);
        cp.winobj = NULL;
        cp.list = NULL;
        return 0;
    }
    rebuild();
    return 1;
}

void complete_start(EditorApp *app)
{
    Document *doc = app->active;
    char *text, prefix[PREFIX_MAX];
    ULONG x = 0, y = 0;
    const char *p, *end;
    long n = 0, start;
    int why;
    if (doc == NULL || cp.waiting || cp.win != NULL) return;
    if (doc->path == NULL) {
        ui_status_message(app, S(MSG_SYM_NOFILE));
        return;
    }
    if ((why = scoutlink_unavailable(app)) != 0) {
        ui_status_message(app, S(why == 1 ? MSG_SYM_OFF : MSG_SYM_WAIT));
        return;
    }
    GetAttr(GA_TEXTEDITOR_CursorX, doc->editor, &x);
    GetAttr(GA_TEXTEDITOR_CursorY, doc->editor, &y);
    /* the part of the word before the cursor */
    if ((text = (char *)DoMethod(doc->editor, GM_TEXTEDITOR_ExportText, NULL)) == NULL) return;
    for (p = text; *p != '\0' && n < (long)y; ++p)
        if (*p == '\n') ++n;
    for (end = p; *end != '\0' && *end != '\n'; ++end) ;
    start = (long)x <= (long)(end - p) ? (long)x : (long)(end - p);
    while (start > 0 && name_char((unsigned char)p[start - 1]) && (long)x - start < PREFIX_MAX - 1) --start;
    memcpy(prefix, p + start, (size_t)((long)x - start));
    prefix[(long)x - start] = '\0';
    FreeVec(text);
    if (!scoutlink_request_complete(app, doc, (long)y + 1, (long)x + 1, prefix)) {
        ui_status_message(app, S(MSG_SYM_WAIT));
        return;
    }
    strcpy(cp.prefix, prefix);
    cp.doc = doc->number;
    cp.line = (long)y;
    cp.start = start;
    cp.waiting = 1;
}

/**
 * @brief qsort() order of names: alphabetical, without regard to case.
 * @param a A name (CompItem).
 * @param b Another name.
 * @return Less than, equal to or greater than 0.
 */
static int by_label(const void *a, const void *b)
{
    return Stricmp((STRPTR)((const CompItem *)a)->label, (STRPTR)((const CompItem *)b)->label);
}

/** Comments and strings of a line, where no words are taken. */
typedef struct Skip {
    size_t start[32];   /**< first column of each */
    size_t end[32];     /**< column after it */
    int count;          /**< number of them */
} Skip;

/**
 * @brief SyntaxEmit callback that notes the comments and strings of a line.
 * @param context The Skip.
 * @param span A span of the highlighter.
 */
static void note_skip(void *context, const SyntaxSpan *span)
{
    Skip *sk = (Skip *)context;
    if ((span->style == SYNTAX_COMMENT || span->style == SYNTAX_STRING) && sk->count < 32) {
        sk->start[sk->count] = span->start;
        sk->end[sk->count++] = span->end;
    }
}

/**
 * @brief Add a word as a name, unless it is listed already.
 * @param w The word.
 * @param len Its length.
 */
static void add_word(const char *w, size_t len)
{
    long i;
    for (i = 0; i < cp.count; ++i)
        if (strlen(cp.items[i].label) == len && strncmp(cp.items[i].label, w, len) == 0) return;
    if ((cp.items[cp.count].label = malloc(len + 1)) == NULL) return;
    memcpy(cp.items[cp.count].label, w, len);
    cp.items[cp.count].label[len] = '\0';
    cp.items[cp.count].detail = NULL;
    cp.items[cp.count++].kind = WORD_KIND;
}

/**
 * @brief Add the words of the document that start with the prefix and are
 *        longer, outside comments and strings, unless they are listed
 *        already (up to MAX_ITEMS names).
 * @param doc The document.
 */
static void add_words(Document *doc)
{
    char *text = (char *)DoMethod(doc->editor, GM_TEXTEDITOR_ExportText, NULL);
    char *line, *nl;
    size_t n = strlen(cp.prefix);
    unsigned long state = 0;
    long first = cp.count;
    CompItem *bigger;
    if (text == NULL) return;
    if ((bigger = realloc(cp.items, MAX_ITEMS * sizeof(CompItem))) == NULL) {
        FreeVec(text);
        return;
    }
    cp.items = bigger;
    for (line = text; line != NULL && cp.count < MAX_ITEMS; line = nl != NULL ? nl + 1 : NULL) {
        Skip sk;
        size_t i = 0, len;
        if ((nl = strchr(line, '\n')) != NULL) *nl = '\0';
        sk.count = 0;
        state = syntax_scan(doc->highlight.rules, NULL, line, state, note_skip, &sk);
        len = strlen(line);
        while (i < len && cp.count < MAX_ITEMS) {
            size_t w = i, wl;
            int k, skip = 0;
            if (!name_char((unsigned char)line[i])) { ++i; continue; }
            while (i < len && name_char((unsigned char)line[i])) ++i;
            wl = i - w;
            if (line[w] >= '0' && line[w] <= '9') continue;     /* a number */
            if (wl <= n || wl > 127 || Strnicmp((STRPTR)line + w, (STRPTR)cp.prefix, (LONG)n) != 0) continue;
            for (k = 0; k < sk.count && !skip; ++k)
                if (w >= sk.start[k] && w < sk.end[k]) skip = 1;
            if (!skip) add_word(line + w, wl);
        }
    }
    FreeVec(text);
    if (cp.count - first > 1) qsort(cp.items + first, (size_t)(cp.count - first), sizeof(CompItem), by_label);
}

void complete_answer(EditorApp *app, const char *json)
{
    JsonValue *root;
    const JsonValue *list;
    size_t i, n;
    Document *doc = the_document(app);
    char text[120];
    cp.waiting = 0;
    free_all();
    if (doc == NULL || json == NULL) return;
    root = json_parse(json, NULL);
    list = root != NULL ? json_obj_get_array(root, "items") : NULL;
    n = list != NULL ? json_array_length(list) : 0;
    if (n > MAX_ITEMS) n = MAX_ITEMS;
    if (n > 0) cp.items = calloc(n, sizeof(CompItem));
    for (i = 0; cp.items != NULL && i < n; ++i) {
        const JsonValue *v = json_array_get(list, i);
        const char *label = json_obj_get_string(v, "label", NULL);
        const char *detail = json_obj_get_string(v, "detail", NULL);
        CompItem *it = &cp.items[cp.count];
        if (label == NULL || (it->label = malloc(strlen(label) + 1)) == NULL) continue;
        strcpy(it->label, label);
        if (detail != NULL && (it->detail = malloc(strlen(detail) + 1)) != NULL) strcpy(it->detail, detail);
        it->kind = json_obj_get_bool(v, "keyword", 0) ? 0 : (int)json_obj_get_int(v, "kind", -1);
        ++cp.count;
    }
    json_value_free(root);
    add_words(doc);
    if (cp.count == 0) {
        DisplayBeep(app->screen);
        snprintf(text, sizeof(text), S(MSG_COMP_NONE), cp.prefix);
        ui_status_message(app, text);
        return;
    }
    if (!open_list(app, doc)) free_all();
}

/**
 * @brief Put the chosen name in place of the prefix and close the list.
 * @param app The application state.
 */
static void insert_selected(EditorApp *app)
{
    Document *doc = the_document(app);
    struct Node *node = cp.list != NULL ? (struct Node *)get(cp.list, LISTBROWSER_SelectedNode) : NULL;
    ULONG index = 0;
    char label[128];
    long len = (long)strlen(cp.prefix);
    if (doc == NULL || node == NULL || app->window == NULL) {
        close_list(app);
        return;
    }
    GetListBrowserNodeAttrs(node, LBNA_UserData, (ULONG)&index, TAG_END);
    if ((long)index >= cp.count) {
        close_list(app);
        return;
    }
    strncpy(label, cp.items[index].label, sizeof(label) - 1);
    label[sizeof(label) - 1] = '\0';
    close_list(app);
    if (len > 0) {
        DoGadgetMethod((struct Gadget *)doc->editor, app->window, NULL, GM_TEXTEDITOR_MarkText, 0,
                       (ULONG)cp.start, (ULONG)cp.line, (ULONG)(cp.start + len), (ULONG)cp.line);
        DoGadgetMethod((struct Gadget *)doc->editor, app->window, NULL, GM_TEXTEDITOR_Replace, 0,
                       (ULONG)label, 0);
    } else {
        DoGadgetMethod((struct Gadget *)doc->editor, app->window, NULL, GM_TEXTEDITOR_InsertText, 0,
                       (ULONG)label, GV_TEXTEDITOR_InsertText_Cursor);
    }
}

/**
 * @brief Put a character into the editor (typed while the list is open).
 * @param app The application state.
 * @param doc The document.
 * @param c The character.
 */
static void type_char(EditorApp *app, Document *doc, char c)
{
    char s[2];
    s[0] = c;
    s[1] = '\0';
    DoGadgetMethod((struct Gadget *)doc->editor, app->window, NULL, GM_TEXTEDITOR_InsertText, 0,
                   (ULONG)s, GV_TEXTEDITOR_InsertText_Cursor);
}

/**
 * @brief Move the chosen row up or down.
 * @param delta -1 or 1.
 */
static void move_selection(int delta)
{
    LONG sel = (LONG)get(cp.list, LISTBROWSER_Selected), rows = 0;
    struct Node *n;
    for (n = cp.nodes.lh_Head; n->ln_Succ != NULL; n = n->ln_Succ) ++rows;
    sel += delta;
    if (sel < 0) sel = 0;
    if (sel >= rows) sel = rows - 1;
    if (sel < 0) return;
    SetGadgetAttrs((struct Gadget *)cp.list, cp.win, NULL, LISTBROWSER_Selected, (ULONG)sel,
                   LISTBROWSER_MakeVisible, (ULONG)sel, TAG_END);
}

void complete_handle(EditorApp *app)
{
    ULONG result;
    WORD code;
    Document *doc;
    if (cp.win == NULL) return;
    while (cp.winobj != NULL && (result = DoMethod(cp.winobj, WM_HANDLEINPUT, &code)) != WMHI_LASTMSG) {
        switch (result & WMHI_CLASSMASK) {
        case WMHI_INACTIVE:
            close_list(app);
            return;
        case WMHI_GADGETUP:
            if (get(cp.list, LISTBROWSER_RelEvent) & LBRE_DOUBLECLICK) {
                insert_selected(app);
                return;
            }
            break;
        case WMHI_RAWKEY:
            if ((result & WMHI_KEYMASK) == KEY_UP) move_selection(-1);
            else if ((result & WMHI_KEYMASK) == KEY_DOWN) move_selection(1);
            break;
        case WMHI_VANILLAKEY: {
            unsigned char c = (unsigned char)(result & WMHI_KEYMASK);
            size_t n = strlen(cp.prefix);
            if ((doc = the_document(app)) == NULL || c == 27) {      /* Esc */
                close_list(app);
                return;
            }
            if (c == 13 || c == 9) {                                /* Return, Tab */
                insert_selected(app);
                return;
            }
            if (c == 8) {                                           /* Backspace */
                if (n == 0) {
                    close_list(app);
                    return;
                }
                cp.prefix[n - 1] = '\0';
                DoGadgetMethod((struct Gadget *)doc->editor, app->window, NULL, GM_TEXTEDITOR_ARexxCmd, 0,
                               (ULONG)"BACKSPACE");
                rebuild();
                break;
            }
            type_char(app, doc, (char)c);
            if (!name_char(c) || n + 1 >= sizeof(cp.prefix)) {
                close_list(app);
                return;
            }
            cp.prefix[n] = (char)c;
            cp.prefix[n + 1] = '\0';
            if (rebuild() == 0) {
                close_list(app);
                return;
            }
            break;
        }
        }
    }
}

ULONG complete_sigmask(void)
{
    ULONG sig = 0;
    if (cp.win != NULL) GetAttr(WINDOW_SigMask, cp.winobj, &sig);
    return sig;
}

void complete_cleanup(EditorApp *app)
{
    if (cp.win != NULL) close_list(app);
    free_all();
    if (cp.columns != NULL) {
        FreeLBColumnInfo(cp.columns);
        cp.columns = NULL;
    }
}
