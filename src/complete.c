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
 * - Up and Down choose, the mouse wheel scrolls (its rawkey codes are
 *   handled here, as listbrowser.gadget only gets them while it is the
 *   active gadget), Return or Tab put the chosen name in place of the
 *   prefix (GM_TEXTEDITOR_MarkText and Replace), Esc closes;
 * - characters of a name go into the editor and narrow the list, Backspace
 *   takes the last one back; any other character goes into the editor and
 *   closes the list, as does a click elsewhere.
 * The first column shows the kind as a coloured letter, like the symbol
 * list (K for a keyword); the name column is as wide as the longest name
 * shown, at most half the list. As the window stays small, the declarations
 * are cut off in the list: the field below it shows the chosen one whole,
 * wrapped by the width of the font into DETAIL_LINES lines (the last one
 * ends in "..." if that is not enough). After Scout's names come the other words of the
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
#include <gadgets/space.h>
#include <images/bevel.h>
#include <gadgets/texteditor.h>
#include <libraries/gadtools.h>
#include <libraries/keymap.h>
#include <graphics/gfxbase.h>
#include <exec/memory.h>

#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/window.h>
#include <proto/layout.h>
#include <proto/listbrowser.h>
#include <proto/space.h>
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
/** Lines of the field below the list that shows the chosen declaration. */
#define DETAIL_LINES 3
/** Longest line of that field (bytes). */
#define DETAIL_MAX 160
/** Indentation of its continued lines. */
#define DETAIL_INDENT "    "
/** Share of the list (percent) the name column takes at least and at most. */
#define NAME_MIN_PCT 20
#define NAME_MAX_PCT 50         /**< at most */
/** Width of the left bar of the editor (bookmarks.c). */
#define LEFTBAR_WIDTH 20
/** What texteditor.gadget adds to its left bar (pixels; see cursor_box()). */
#define LEFTBAR_MARGIN 5
/** Width of the bookmark marks in that bar, drawn once there is a bookmark. */
#define LEFTBAR_BOOKMARKS 8
/** From the editor's left edge to its text area: bevel and padding. */
#define EDITOR_INSET 4
/** Rawkey codes of the cursor keys. */
#define KEY_UP 0x4C
#define KEY_DOWN 0x4D           /**< Down */
/** Rows the list moves per step of the mouse wheel. */
#define WHEEL_ROWS 3

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
    Object *detail;                 /* space.gadget drawing the chosen declaration */
    struct Hook detail_hook;        /* its render hook */
    char detail_text[DETAIL_LINES][DETAIL_MAX];
    WORD line_height;               /* of the field's lines */
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
        cp.detail = NULL;
    }
    free_all();
    if (app->window != NULL) {
        ActivateWindow(app->window);
        app->focus_editor = 1;          /* when the window is active */
    }
}

/**
 * @brief Take as many bytes of a text as fit into a width, breaking after
 *        a space where one is.
 * @param rp The rastport (its font measures).
 * @param s The text.
 * @param len Its length.
 * @param width The width in pixels.
 * @param[out] next Where the rest starts (spaces skipped).
 * @return The number of bytes of the line.
 */
static long fit_line(struct RastPort *rp, const char *s, long len, WORD width, long *next)
{
    struct TextExtent te;
    long n, k;
    if (width <= 0) { *next = len; return 0; }
    n = (long)TextFit(rp, (STRPTR)s, (UWORD)(len > 0xFFFF ? 0xFFFF : len), &te, NULL, 1,
                      (UWORD)width, (UWORD)(rp->TxHeight + 1));
    if (n >= len) { *next = len; return len; }
    for (k = n; k > 0 && s[k] != ' '; --k) ;
    if (k > 0) n = k;                   /* after the last word that fits */
    else if (n == 0) n = 1;             /* not even one character: go on anyway */
    *next = n;
    while (*next < len && s[*next] == ' ') ++*next;
    return n;
}

/**
 * @brief Show the chosen name's declaration (or the name) whole in the
 *        field below the list, wrapped into its lines.
 */
static void show_detail(void)
{
    struct Node *node;
    struct RastPort *rp;
    char text[512];
    const char *src = "";
    ULONG index = 0;
    long len, pos = 0, i, n, next;
    int line;
    WORD width, indent, dots;
    if (cp.win == NULL || cp.list == NULL || cp.detail == NULL) return;
    node = (struct Node *)get(cp.list, LISTBROWSER_SelectedNode);
    rp = cp.win->RPort;
    width = (WORD)(((struct Gadget *)cp.detail)->Width - 4);
    indent = (WORD)TextLength(rp, (STRPTR)DETAIL_INDENT, sizeof(DETAIL_INDENT) - 1);
    dots = (WORD)TextLength(rp, (STRPTR)"...", 3);
    if (node != NULL) {
        GetListBrowserNodeAttrs(node, LBNA_UserData, (ULONG)&index, TAG_END);
        if ((long)index < cp.count)
            src = cp.items[index].detail != NULL && cp.items[index].detail[0] != '\0' ?
                  cp.items[index].detail : cp.items[index].label;
    }
    /* one line of single spaces */
    for (len = 0; *src != '\0' && len < (long)sizeof(text) - 1; ++src) {
        char c = *src;
        if ((unsigned char)c < ' ') c = ' ';
        if (c == ' ' && (len == 0 || text[len - 1] == ' ')) continue;
        text[len++] = c;
    }
    while (len > 0 && text[len - 1] == ' ') --len;
    for (line = 0; line < DETAIL_LINES; ++line) {
        char *out = cp.detail_text[line];
        long o = 0;
        WORD w = width;
        if (line > 0 && pos < len) {
            strcpy(out, DETAIL_INDENT);
            o = (long)sizeof(DETAIL_INDENT) - 1;
            w = (WORD)(w - indent);
        }
        n = fit_line(rp, text + pos, len - pos, w, &next);
        if (line == DETAIL_LINES - 1 && pos + next < len) {
            /* the last line and still more: cut it, with "..." */
            n = fit_line(rp, text + pos, len - pos, (WORD)(w - dots), &next);
            while (n > 0 && text[pos + n - 1] == ' ') --n;
            if (n > DETAIL_MAX - o - 4) n = DETAIL_MAX - o - 4;
            for (i = 0; i < n; ++i) out[o++] = text[pos + i];
            strcpy(out + o, "...");
        } else {
            if (n > DETAIL_MAX - o - 1) n = DETAIL_MAX - o - 1;
            for (i = 0; i < n; ++i) out[o++] = text[pos + i];
            out[o] = '\0';
        }
        pos += next;
    }
    RefreshGList((struct Gadget *)cp.detail, cp.win, NULL, 1);
}

/**
 * @brief Render hook of the field below the list: its lines, as close as
 *        the font allows. Graphics only, it may run during input handling.
 *
 * It clears the field first: space.gadget 47 leaves the old lines when
 * called again (despite SPACE_Transparent defaulting to FALSE).
 * @param hook The hook.
 * @param obj The space.gadget.
 * @param msg The render message.
 * @return 0.
 */
static ULONG render_detail(struct Hook *hook, Object *obj, struct gpRender *msg)
{
    struct RastPort *rp = msg->gpr_RPort;
    struct IBox *box = NULL;
    UWORD pen = 1;
    WORD y;
    int line;
    (void)hook;
    if (rp == NULL) return 0;
    GetAttr(SPACE_AreaBox, obj, (ULONG *)&box);
    if (box == NULL || box->Width <= 0 || box->Height <= 0) return 0;
    /* the window's background (its backfill hook), as around the field */
    EraseRect(rp, box->Left, box->Top, (WORD)(box->Left + box->Width - 1),
              (WORD)(box->Top + box->Height - 1));
    if (msg->gpr_GInfo != NULL && msg->gpr_GInfo->gi_DrInfo != NULL)
        pen = msg->gpr_GInfo->gi_DrInfo->dri_Pens[TEXTPEN];
    SetAPen(rp, (UBYTE)pen);
    SetDrMd(rp, JAM1);
    y = (WORD)(box->Top + 1);
    for (line = 0; line < DETAIL_LINES && y + rp->TxHeight <= box->Top + box->Height; ++line) {
        const char *t = cp.detail_text[line];
        Move(rp, (WORD)(box->Left + 2), (WORD)(y + rp->TxBaseline));
        Text(rp, (STRPTR)t, (UWORD)strlen(t));
        y = (WORD)(y + cp.line_height);
    }
    return 0;
}

/**
 * @brief Make the name column as wide as the longest name shown (between
 *        NAME_MIN_PCT and NAME_MAX_PCT of the list); the declarations get
 *        the rest. Only while the list is detached from its labels.
 */
static void size_columns(void)
{
    struct RastPort *rp = cp.win->RPort;
    struct Node *node;
    WORD list_width = (WORD)(((struct Gadget *)cp.list)->Width - 20);     /* without the scroller */
    long widest = 0, w, kind_pct, name_pct;
    ULONG index;
    if (list_width < 40) return;
    for (node = cp.nodes.lh_Head; node->ln_Succ != NULL; node = node->ln_Succ) {
        index = 0;
        GetListBrowserNodeAttrs(node, LBNA_UserData, (ULONG)&index, TAG_END);
        if ((long)index >= cp.count) continue;
        w = TextLength(rp, (STRPTR)cp.items[index].label, (UWORD)strlen(cp.items[index].label));
        if (w > widest) widest = w;
    }
    kind_pct = (cp.row_height + 6) * 100L / list_width;
    name_pct = (widest + 8) * 100L / list_width + 1;
    if (name_pct < NAME_MIN_PCT) name_pct = NAME_MIN_PCT;
    if (name_pct > NAME_MAX_PCT) name_pct = NAME_MAX_PCT;
    SetLBColumnInfoAttrs(cp.columns, LBCIA_Column, 0, LBCIA_Weight, (ULONG)kind_pct,
                         LBCIA_Column, 1, LBCIA_Weight, (ULONG)name_pct,
                         LBCIA_Column, 2, LBCIA_Weight, (ULONG)(100 - kind_pct - name_pct), TAG_END);
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
    if (cp.list != NULL && cp.win != NULL) {
        /* the column info set again, so the list takes the new widths */
        size_columns();
        SetGadgetAttrs((struct Gadget *)cp.list, cp.win, NULL, LISTBROWSER_ColumnInfo, (ULONG)cp.columns,
                       LISTBROWSER_Labels, (ULONG)&cp.nodes,
                       LISTBROWSER_Selected, 0, LISTBROWSER_MakeVisible, 0, TAG_END);
        show_detail();
    }
    return shown;
}

/**
 * @brief Measure the start of a line as the editor draws it.
 * @param rp A rastport with the editor's font.
 * @param p The line's text.
 * @param n The bytes to measure.
 * @param tab The distance of the tab stops (pixels, at least 1).
 * @return The width in pixels.
 */
static WORD text_width(struct RastPort *rp, const char *p, long n, WORD tab)
{
    WORD width = 0;
    long start = 0, i;
    for (i = 0; i < n; ++i) {
        if (p[i] != '\t') continue;
        width = (WORD)(width + TextLength(rp, (STRPTR)p + start, (UWORD)(i - start)));
        width = (WORD)((width / tab + 1) * tab);
        start = i + 1;
    }
    return (WORD)(width + TextLength(rp, (STRPTR)p + start, (UWORD)(n - start)));
}

/**
 * @brief Work out where the cursor is drawn on the screen.
 *
 * The editor uses a fixed font (GA_TEXTEDITOR_FixedFont), which is the
 * system's default text font and not the screen's. Its left bar, as found
 * in the code of texteditor.gadget 47.51: the hook's 20 pixels and a margin
 * of 5, 8 more once there are bookmarks, and with line numbers the digits
 * of the line count (at least 2, at most 6) in the width of a space.
 *
 * @param app The application state.
 * @param doc The document.
 * @param[out] x Left edge for the list.
 * @param[out] y Top edge for the list (below the cursor's line).
 * @param[out] line_height Height of the editor's lines.
 */
static void cursor_box(EditorApp *app, Document *doc, WORD *x, WORD *y, WORD *line_height)
{
    struct Gadget *g = (struct Gadget *)doc->editor;
    struct TextFont *font = GfxBase->DefaultFont;
    struct RastPort rp;
    ULONG first = 0, hfirst = 0, lines = 0;
    WORD h = font != NULL ? (WORD)font->tf_YSize : 8;
    WORD space, tab, left = (WORD)(g->LeftEdge + EDITOR_INSET + LEFTBAR_WIDTH + LEFTBAR_MARGIN);
    char *text = (char *)DoMethod(doc->editor, GM_TEXTEDITOR_ExportText, NULL);
    InitRastPort(&rp);
    if (font != NULL) SetFont(&rp, font);
    space = (WORD)TextLength(&rp, (STRPTR)" ", 1);
    tab = (WORD)(space * filetypes_get(&app->filetypes, doc->ft_language)->tab_size);
    if (tab < 1) tab = 1;
    GetAttr(GA_TEXTEDITOR_Prop_First, doc->editor, &first);
    GetAttr(GA_TEXTEDITOR_HProp_First, doc->editor, &hfirst);
    GetAttr(GA_TEXTEDITOR_Prop_Entries, doc->editor, &lines);
    if (doc->bm_count > 0) left = (WORD)(left + LEFTBAR_BOOKMARKS);
    if (doc->line_numbers) {
        WORD digits = 2;
        ULONG limit = 99;
        while (digits < 6 && lines > limit) {
            ++digits;
            limit = limit * 10 + 9;
        }
        left = (WORD)(left + digits * space);
    }
    if (text != NULL) {
        const char *p = text;
        long n = 0;
        while (*p != '\0' && n < cp.line) if (*p++ == '\n') ++n;
        left = (WORD)(left + text_width(&rp, p, cp.start, tab));
        FreeVec(text);
    }
    *x = (WORD)(app->window->LeftEdge + left - (WORD)hfirst);
    *y = (WORD)(app->window->TopEdge + g->TopEdge + ((WORD)(cp.line - (long)first) + 1) * h + 2);
    *line_height = h;
}

/**
 * @brief Open the list window at the cursor.
 * @param app The application state.
 * @param doc The document.
 * @return Non-zero if it is open.
 */
static int open_list(EditorApp *app, Document *doc)
{
    WORD x, y, height, editor_line;
    int i;
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
    cp.line_height = (WORD)(cp.row_height - 1);
    height = (WORD)(ROWS * cp.row_height + 8 + 2 + DETAIL_LINES * cp.line_height + 4);
    cursor_box(app, doc, &x, &y, &editor_line);
    /* in the screen; above the line if there is no room below */
    if (x + WIN_WIDTH > app->screen->Width) x = (WORD)(app->screen->Width - WIN_WIDTH);
    if (x < 0) x = 0;
    if (y + height > app->screen->Height) y = (WORD)(y - height - editor_line - 6);
    if (y < 0) y = 0;
    cp.list = NewObject(LISTBROWSER_GetClass(), NULL, GA_RelVerify, TRUE,
        LISTBROWSER_Labels, (ULONG)&cp.nodes, LISTBROWSER_ColumnInfo, (ULONG)cp.columns,
        LISTBROWSER_ShowSelected, TRUE, LISTBROWSER_Selected, 0, TAG_END);
    /* the field below the list: space.gadget, drawn by render_detail()
     * (button.gadget lines would be taller than the font: they keep a
     * margin above and below their text, whatever the layout allows) */
    for (i = 0; i < DETAIL_LINES; ++i) cp.detail_text[i][0] = '\0';
    cp.detail_hook.h_Entry = (ULONG (*)())HookEntry;
    cp.detail_hook.h_SubEntry = (ULONG (*)())render_detail;
    cp.detail = NewObject(SPACE_GetClass(), NULL,
        SPACE_MinHeight, (ULONG)(DETAIL_LINES * cp.line_height + 2),
        SPACE_RenderHook, (ULONG)&cp.detail_hook, TAG_END);
    if (cp.list == NULL || cp.detail == NULL) {
        if (cp.list != NULL) DisposeObject(cp.list);
        if (cp.detail != NULL) DisposeObject(cp.detail);
        cp.list = cp.detail = NULL;
        return 0;
    }
    cp.winobj = NewObject(WINDOW_GetClass(), NULL,
        WA_PubScreen, (ULONG)app->screen,
        WA_Left, (ULONG)x, WA_Top, (ULONG)y, WA_Width, WIN_WIDTH, WA_Height, (ULONG)height,
        WA_Borderless, TRUE, WA_Activate, TRUE, WA_RMBTrap, TRUE,
        WA_IDCMP, IDCMP_VANILLAKEY | IDCMP_RAWKEY | IDCMP_INACTIVEWINDOW,
        WINDOW_ParentGroup, (ULONG)NewObject(LAYOUT_GetClass(), NULL,
            LAYOUT_Orientation, LAYOUT_ORIENT_VERT,
            LAYOUT_BevelStyle, BVS_BUTTON,
            LAYOUT_InnerSpacing, 2,
            LAYOUT_AddChild, (ULONG)cp.list,
            LAYOUT_AddChild, (ULONG)cp.detail,
            CHILD_WeightedHeight, 0,
            TAG_END),
        TAG_END);
    /* the window disposes the layout with its children */
    if (cp.winobj == NULL || (cp.win = (struct Window *)DoMethod(cp.winobj, WM_OPEN, NULL)) == NULL) {
        if (cp.winobj != NULL) DisposeObject(cp.winobj);
        cp.winobj = NULL;
        cp.list = cp.detail = NULL;
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
    show_detail();
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
            show_detail();              /* a click chose another row */
            break;
        case WMHI_RAWKEY:
            if ((result & WMHI_KEYMASK) == KEY_UP) move_selection(-1);
            else if ((result & WMHI_KEYMASK) == KEY_DOWN) move_selection(1);
            else if ((result & WMHI_KEYMASK) == RAWKEY_WHEEL_UP || (result & WMHI_KEYMASK) == RAWKEY_WHEEL_DOWN) {
                ULONG top = 0;
                LONG t;
                GetAttr(LISTBROWSER_Top, cp.list, &top);
                t = (LONG)top + ((result & WMHI_KEYMASK) == RAWKEY_WHEEL_UP ? -WHEEL_ROWS : WHEEL_ROWS);
                if (t < 0) t = 0;
                SetGadgetAttrs((struct Gadget *)cp.list, cp.win, NULL, LISTBROWSER_Top, (ULONG)t, TAG_END);
            }
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
