/**
 * @file helpviewer.c
 * @brief HelpViewer: shows a directory of HTML help pages, with their
 *        titles in a tree on the left (dirtree.gadget) and the page on the
 *        right (html.gadget or htmlttf.gadget).
 *
 *   HelpViewer [DIR] \<directory or page\> [TTF] [FONTSET Vera|DejaVu|Noto] [SIZE n]
 *              [PORTNAME \<name\>]
 *
 * The same options are tool types of the program icon (DIR=..., TTF,
 * FONTSET=..., SIZE=..., PORTNAME=...), read also from the Shell (arguments win), with
 * WINDOW=left,top,width,height, which Project/Snapshot Window writes into
 * the icon. From the Workbench a drawer or page
 * given as project (shift-click, drop on the icon) is opened. A directory
 * shows its index.html (or index.htm) first. The tree lists only HTML
 * files, by the titles of the pages, in natural order of the file names
 * with directories among the files; a directory shows the title of its
 * index page and is left out if it holds no page. A directory without an
 * index page but with language drawers (named like the Amiga locales)
 * opens the drawer of the user's language. With PORTNAME only one
 * HelpViewer of that name runs (one per application): a later start with
 * the same name passes its location to it through the public port
 * HELPVIEWER.\<NAME\> and ends; without it every start opens a viewer of
 * its own. Back and Forward go
 * through the pages seen (with their scroll positions).
 *
 * Built on HTMLDemo of html.gadget; no stdio (started from the Workbench,
 * libnix would add its console handling for it).
 */
#include <exec/types.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <utility/hooks.h>
#include <intuition/intuition.h>
#include <intuition/icclass.h>
#include <intuition/gadgetclass.h>
#include <libraries/gadtools.h>
#include <libraries/asl.h>
#include <libraries/locale.h>
#include <classes/window.h>
#include <gadgets/layout.h>
#include <gadgets/button.h>
#include <gadgets/scroller.h>
#include <gadgets/speedbar.h>
#include <gadgets/listbrowser.h>
#include <images/label.h>
#include <gadgets/html.h>
#include <gadgets/htmlttf.h>
#include <gadgets/dirtree.h>
#include <workbench/startup.h>
#include <workbench/workbench.h>

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/utility.h>
#include <proto/window.h>
#include <proto/layout.h>
#include <proto/button.h>
#include <proto/scroller.h>
#include <proto/speedbar.h>
#include <proto/label.h>
#include <proto/html.h>
#include <proto/icon.h>
#include <proto/asl.h>
#include <proto/locale.h>
#include <clib/alib_protos.h>

#include <string.h>

#define HELPVIEWER_STRINGS     /**< the built-in strings are defined here */
#include "helpviewer_strings.h"
#include "htmltitle.h"

/** Version string for the Version command; changes only with a release. */
static const char version[] = "$VER: HelpViewer 1.0 (20.10.2026)";
/** The version as the About requester shows it. */
#define HV_VERSION "1.0 (20.10.2026)"

/** libnix swaps to a stack of this size at the start (-Wl,-u,___stkinit). */
unsigned long __stack = 32768;
/** Started from the Workbench: libnix must not open a console window. */
char *__stdiowin = NULL;
/** The Workbench start message (libnix), NULL from the Shell. */
extern struct WBStartup *_WBenchMsg;

/** @name Libraries and classes (initialised, so no auto-open stubs are pulled in) */
/** @{ */
struct Library *IconBase = NULL;        /**< icon.library (tool types) */
struct Library *AslBase = NULL;         /**< asl.library (directory requester) */
struct Library *WindowBase = NULL;      /**< window.class */
struct Library *LayoutBase = NULL;      /**< layout.gadget */
struct Library *ButtonBase = NULL;      /**< button.gadget */
struct Library *ScrollerBase = NULL;    /**< scroller.gadget */
struct Library *SpeedBarBase = NULL;    /**< speedbar.gadget */
struct Library *LabelBase = NULL;       /**< label.image */
struct Library *ListBrowserBase = NULL; /**< listbrowser.gadget (superclass of dirtree.gadget) */
struct Library *DirTreeBase = NULL;     /**< dirtree.gadget */
struct Library *HTMLBase = NULL;        /**< html.gadget or htmlttf.gadget */
struct LocaleBase *LocaleBase = NULL;   /**< locale.library, NULL without it */
/** @} */

/** Gadget IDs. */
enum { GID_HTML = 1, GID_VSCROLL, GID_HSCROLL, GID_TREE, GID_TOOLBAR, GID_STATUS };
/** Menu and toolbar commands (the menu items' user data, the buttons' IDs). */
enum { MID_OPEN = 1, MID_BACK, MID_FORWARD, MID_ABOUT, MID_QUIT, MID_COPY, MID_SELALL, MID_HOME,
       MID_SNAPSHOT };

#define MAXHIST 64              /**< pages kept for Back and Forward */
#define PATHMAX 512             /**< size of path buffers */
#define TITLE_READ 4096         /**< bytes of a page searched for its title */
#define NUMTOOLS 4              /**< toolbar buttons */
#define TOOL_HOME 0             /**< index of Home in tools[] */
#define TOOL_BACK 1             /**< index of Back */
#define TOOL_FORWARD 2          /**< index of Forward */

/** The menus; texts from menu_texts[] (catalog). */
static struct NewMenu menus[] = {
    { NM_TITLE, NULL, 0,             0, 0, 0 },
    { NM_ITEM,  NULL, (STRPTR)"O",   0, 0, (APTR)MID_OPEN },
    { NM_ITEM,  NM_BARLABEL, 0,      0, 0, 0 },
    { NM_ITEM,  NULL, (STRPTR)"H",   0, 0, (APTR)MID_HOME },
    { NM_ITEM,  NULL, (STRPTR)"B",   0, 0, (APTR)MID_BACK },
    { NM_ITEM,  NULL, (STRPTR)"N",   0, 0, (APTR)MID_FORWARD },
    { NM_ITEM,  NM_BARLABEL, 0,      0, 0, 0 },
    { NM_ITEM,  NULL, 0,             0, 0, (APTR)MID_SNAPSHOT },
    { NM_ITEM,  NM_BARLABEL, 0,      0, 0, 0 },
    { NM_ITEM,  NULL, (STRPTR)"?",   0, 0, (APTR)MID_ABOUT },
    { NM_ITEM,  NULL, (STRPTR)"Q",   0, 0, (APTR)MID_QUIT },
    { NM_TITLE, NULL, 0,             0, 0, 0 },
    { NM_ITEM,  NULL, (STRPTR)"C",   0, 0, (APTR)MID_COPY },
    { NM_ITEM,  NULL, (STRPTR)"A",   0, 0, (APTR)MID_SELALL },
    { NM_END,   NULL, 0,             0, 0, 0 }
};

/** Texts of menus[] (MSG_), -1 for bars and the end. */
static const LONG menu_texts[] = {
    MSG_MENU_PROJECT, MSG_MENU_OPEN, -1, MSG_MENU_HOME, MSG_MENU_BACK, MSG_MENU_FORWARD, -1,
    MSG_MENU_SNAPSHOT, -1, MSG_MENU_ABOUT, MSG_MENU_QUIT, MSG_MENU_EDIT, MSG_MENU_COPY, MSG_MENU_SELECTALL, -1
};

/** One toolbar button. */
typedef struct ToolSpec {
    ULONG command;      /**< MID_ of the button, also its speedbar ID */
    LONG label;         /**< its text (MSG_) */
    LONG help;          /**< its help bubble (MSG_) */
} ToolSpec;

/** The toolbar buttons: text only. */
static const ToolSpec tools[NUMTOOLS] = {
    { MID_HOME,    MSG_TB_HOME,    MSG_TBH_HOME },
    { MID_BACK,    MSG_TB_BACK,    MSG_TBH_BACK },
    { MID_FORWARD, MSG_TB_FORWARD, MSG_TBH_FORWARD },
    { MID_OPEN,    MSG_TB_OPEN,    MSG_TBH_OPEN },
};

/** A page seen, for Back and Forward. */
typedef struct Visit {
    char *path;         /**< the page, with "#anchor" if there was one (AllocVec()) */
    ULONG top;          /**< its scroll position when it was left */
} Visit;

/** Prefix of the public port of a named HelpViewer (PORTNAME option). */
#define PORTPREFIX "HELPVIEWER."

/**
 * A later start of a HelpViewer with the same PORTNAME passes its location
 * to the running one through the public port and ends (one viewer per
 * application, e.g. when TinyDev's Help is used again). The running viewer
 * replies once it has taken the location.
 */
typedef struct OpenMessage {
    struct Message msg;     /**< the Exec message */
    char path[PATHMAX];     /**< directory or page (full path), "" to come to the front only */
} OpenMessage;

/** Options from the command line or the tool types. */
static struct {
    char dir[PATHMAX];  /**< directory (or page) to open */
    BOOL ttf;           /**< use htmlttf.gadget */
    char fontset[32];   /**< its font family */
    LONG size;          /**< its text size, 0 = default */
    LONG box[4];        /**< window left, top, width, height; width 0 = automatic */
    char portname[48];  /**< PORTNAME: one viewer per name, "" = no port */
    char icon[PATHMAX]; /**< the program icon (without .info), "" if unknown */
} opt;

/** State of the viewer (there is one window). */
static struct {
    Object *winobj;             /**< window.class object */
    Object *html;               /**< html.gadget or htmlttf.gadget */
    Object *vscroll;            /**< vertical scroller */
    Object *hscroll;            /**< horizontal scroller */
    Object *tree;               /**< dirtree.gadget */
    Object *bar;                /**< speedbar.gadget */
    Object *status;             /**< status line */
    struct Window *win;         /**< the open window */
    struct Screen *screen;      /**< the locked public screen */
    struct DrawInfo *dri;       /**< its DrawInfo */
    struct List buttons;        /**< the speedbar's nodes */
    struct Node *nodes[NUMTOOLS];   /**< the nodes, in tools[] order */
    Object *labels[NUMTOOLS];   /**< their texts */
    Object *ghosts[NUMTOOLS];   /**< their texts, ghosted */
    ULONG disabled;             /**< bit i: button i is ghosted */
    LONG ghost_pen;             /**< pen of ghosted texts, -1 if none */
    struct HintInfo hints[NUMTOOLS + 1];    /**< help bubbles of the toolbar */
    Visit hist[MAXHIST];        /**< pages seen */
    int nhist;                  /**< number of them */
    int cur;                    /**< the page shown, -1 if none */
    char root[PATHMAX];         /**< directory of the tree */
    char home[PATHMAX];         /**< its index page (the home page), "" if none */
    char status_text[PATHMAX + 80]; /**< text of the status line */
    char title[300];            /**< text of the window title */
    struct Hook name_hook;      /**< DIRTREE_NameHook: titles of the pages */
    struct Catalog *catalog;    /**< HelpViewer.catalog, NULL for English */
    struct MsgPort *port;       /**< public port (PORTNAME option), NULL if none */
    char portname[64];          /**< its name: PORTPREFIX and the name in capitals */
} hv = { .cur = -1, .ghost_pen = -1 };

/** Scroller to HTML (ICA_MAP). */
static struct TagItem vscroll_map[] = { { SCROLLER_Top, HTML_Top }, { TAG_DONE, 0 } };
/** Horizontal scroller to HTML (ICA_MAP). */
static struct TagItem hscroll_map[] = { { SCROLLER_Top, HTML_Left }, { TAG_DONE, 0 } };
/** HTML to the vertical scroller (ICA_MAP). */
static struct TagItem html_map[] = {
    { HTML_Top,     SCROLLER_Top },
    { HTML_Total,   SCROLLER_Total },
    { HTML_Visible, SCROLLER_Visible },
    { TAG_DONE, 0 }
};

/**
 * @brief Return a text in the user's language.
 * @param id The MSG_ number.
 * @return The text from the catalog, or the built-in English one.
 */
static const char *S(LONG id)
{
    const char *builtin = id >= 0 && id < MSG_COUNT ? msg_builtin[id] : "";
    if (hv.catalog == NULL) return builtin;
    return (const char *)GetCatalogStr(hv.catalog, id, (STRPTR)builtin);
}

/**
 * @brief Copy a text with its first "%s" replaced, cut to the buffer.
 * @param[out] out The buffer.
 * @param size Its size.
 * @param format The text.
 * @param arg What replaces "%s".
 */
static void format(char *out, long size, const char *format, const char *arg)
{
    long n = 0;
    int done = 0;
    while (*format != '\0' && n < size - 1) {
        if (!done && format[0] == '%' && format[1] == 's') {
            const char *a = arg != NULL ? arg : "";
            while (*a != '\0' && n < size - 1) out[n++] = *a++;
            format += 2;
            done = 1;
        } else out[n++] = *format++;
    }
    out[n] = '\0';
}

/**
 * @brief Show a message: a requester (also from the Shell, as it may come
 *        before or without the window).
 * @param text The message.
 */
static void message(const char *text)
{
    struct EasyStruct es;
    es.es_StructSize = sizeof(es);
    es.es_Flags = 0;
    es.es_Title = (STRPTR)"HelpViewer";
    es.es_TextFormat = (STRPTR)"%s";
    es.es_GadgetFormat = (STRPTR)S(MSG_OK);
    EasyRequest(hv.win, &es, NULL, (ULONG)text);
}

/**
 * @brief Show a text in the status line.
 * @param text The text.
 */
static void set_status(const char *text)
{
    if (text != hv.status_text) {
        strncpy(hv.status_text, text, sizeof(hv.status_text) - 1);
        hv.status_text[sizeof(hv.status_text) - 1] = '\0';
    }
    if (hv.win != NULL)
        SetGadgetAttrs((struct Gadget *)hv.status, hv.win, NULL, GA_Text, (ULONG)hv.status_text, TAG_DONE);
}

/**
 * @brief Tell whether a path is a directory.
 * @param path The path.
 * @return Non-zero for a directory, zero for a file or if it is not there.
 */
static int is_directory(const char *path)
{
    BPTR lock = Lock((STRPTR)path, ACCESS_READ);
    struct FileInfoBlock *fib;
    int dir = 0;
    if (lock == 0) return 0;
    if ((fib = AllocDosObject(DOS_FIB, NULL)) != NULL) {
        if (Examine(lock, fib)) dir = fib->fib_DirEntryType > 0;
        FreeDosObject(DOS_FIB, fib);
    }
    UnLock(lock);
    return dir;
}

/**
 * @brief Find the index page of a directory (index.html, then index.htm).
 * @param dir The directory.
 * @param[out] out Buffer for its path.
 * @param size Its size.
 * @return Non-zero if there is one.
 */
static int find_index(const char *dir, char *out, LONG size)
{
    static const char *const names[] = { "index.html", "index.htm" };
    int i;
    for (i = 0; i < 2; ++i) {
        BPTR lock;
        strncpy(out, dir, (size_t)size - 1);
        out[size - 1] = '\0';
        if (!AddPart((STRPTR)out, (STRPTR)names[i], (ULONG)size)) continue;
        if ((lock = Lock((STRPTR)out, ACCESS_READ)) != 0) {
            UnLock(lock);
            return 1;
        }
    }
    return 0;
}

/**
 * @brief Go into the language drawer of a multilingual help directory.
 *
 * A help directory without an index page may hold one drawer per language,
 * named like the Amiga locales ("deutsch", "english"). The first of the
 * user's preferred languages that has one wins, then "english".
 *
 * @param[in,out] dir The directory; extended by the language drawer.
 * @param size Its size.
 * @return Non-zero if a language drawer was found.
 */
static int language_dir(char *dir, LONG size)
{
    struct Locale *locale = LocaleBase != NULL ? OpenLocale(NULL) : NULL;
    char path[PATHMAX];
    int i, found = 0;
    for (i = 0; i <= 10 && !found; ++i) {
        const char *lang = i < 10 ? (locale != NULL ? (const char *)locale->loc_PrefLanguages[i] : NULL)
                                  : "english";
        if (lang == NULL || *lang == '\0') continue;
        strncpy(path, dir, sizeof(path) - 1);
        path[sizeof(path) - 1] = '\0';
        if (AddPart((STRPTR)path, (STRPTR)lang, sizeof(path)) && is_directory(path) &&
            (LONG)strlen(path) < size) {
            strcpy(dir, path);
            found = 1;
        }
    }
    if (locale != NULL) CloseLocale(locale);
    return found;
}

/**
 * @brief Read the title of a page.
 * @param path The page.
 * @param[out] out Buffer for the title.
 * @param size Its size.
 * @return Non-zero if it has one.
 */
static int read_title(const char *path, char *out, long size)
{
    BPTR fh = Open((STRPTR)path, MODE_OLDFILE);
    char *data;
    LONG got;
    int found = 0;
    if (fh == 0) return 0;
    if ((data = AllocVec(TITLE_READ, MEMF_ANY)) != NULL) {
        if ((got = Read(fh, data, TITLE_READ)) > 0) found = html_title(data, got, out, size);
        FreeVec(data);
    }
    Close(fh);
    return found;
}

/**
 * @brief DIRTREE_NameHook: the title of a page, or of a directory's index
 *        page (runs while the tree reads a directory, on this process).
 * @param hook The hook.
 * @param obj The tree.
 * @param msg The entry.
 * @return TRUE if a title was found.
 */
static ULONG name_hook(struct Hook *hook, Object *obj, struct DirTreeNameMsg *msg)
{
    char index[PATHMAX];
    (void)hook;
    (void)obj;
    if (!msg->dtnm_Directory)
        return (ULONG)read_title((const char *)msg->dtnm_Path, (char *)msg->dtnm_Buffer,
                                 (long)msg->dtnm_BufferSize);
    if (!find_index((const char *)msg->dtnm_Path, index, sizeof(index))) return FALSE;
    return (ULONG)read_title(index, (char *)msg->dtnm_Buffer, (long)msg->dtnm_BufferSize);
}

/* ---- toolbar ------------------------------------------------------------ */

/**
 * @brief Create the text of a toolbar button.
 * @param i The button.
 * @param pen Its pen.
 * @return The label.image, or NULL.
 */
static Object *make_label(int i, LONG pen)
{
    return NewObject(LABEL_GetClass(), NULL,
        LABEL_DrawInfo, (ULONG)hv.dri,
        LABEL_Justification, LJ_CENTRE,
        LABEL_Underscore, 0,
        IA_FGPen, (ULONG)pen,
        LABEL_Text, (ULONG)S(tools[i].label),
        TAG_END);
}

/**
 * @brief Build the speedbar with its text buttons and help bubbles.
 * @return Non-zero on success.
 */
static int make_toolbar(void)
{
    struct ColorMap *cm = hv.screen->ViewPort.ColorMap;
    ULONG t[3], b[3];
    int i;
    NewList(&hv.buttons);
    /* ghosted texts: halfway between text and background */
    GetRGB32(cm, hv.dri->dri_Pens[TEXTPEN], 1, t);
    GetRGB32(cm, hv.dri->dri_Pens[BACKGROUNDPEN], 1, b);
    hv.ghost_pen = ObtainBestPen(cm, (t[0] >> 1) + (b[0] >> 1), (t[1] >> 1) + (b[1] >> 1),
                                 (t[2] >> 1) + (b[2] >> 1), OBP_Precision, PRECISION_IMAGE, TAG_END);
    for (i = 0; i < NUMTOOLS; ++i) {
        hv.labels[i] = make_label(i, (LONG)hv.dri->dri_Pens[TEXTPEN]);
        hv.ghosts[i] = make_label(i, hv.ghost_pen >= 0 ? hv.ghost_pen : (LONG)hv.dri->dri_Pens[TEXTPEN]);
        hv.nodes[i] = AllocSpeedButtonNode(tools[i].command,
            hv.labels[i] != NULL ? SBNA_Image : SBNA_Text,
            hv.labels[i] != NULL ? (ULONG)hv.labels[i] : (ULONG)S(tools[i].label),
            SBNA_Enabled, TRUE,
            SBNA_Spacing, i == TOOL_BACK || i == NUMTOOLS - 1 ? 8 : 0,
            SBNA_Highlight, SBH_RECESS,
            TAG_END);
        if (hv.nodes[i] == NULL) return 0;
        AddTail(&hv.buttons, hv.nodes[i]);
        hv.hints[i].hi_GadgetID = GID_TOOLBAR;
        hv.hints[i].hi_Code = (WORD)tools[i].command;
        hv.hints[i].hi_Text = (STRPTR)S(tools[i].help);
        hv.hints[i].hi_Flags = 0;
    }
    hv.hints[i].hi_GadgetID = -1;
    hv.hints[i].hi_Code = -1;
    hv.bar = NewObject(SPEEDBAR_GetClass(), NULL,
        GA_ID, GID_TOOLBAR,
        GA_RelVerify, TRUE,
        SPEEDBAR_Orientation, SBORIENT_HORIZ,
        SPEEDBAR_Buttons, (ULONG)&hv.buttons,
        SPEEDBAR_BevelStyle, BVS_NONE,
        SPEEDBAR_ButtonBevelStyle, BVS_NONE,
        TAG_END);
    return hv.bar != NULL;
}

/** @brief Ghost Home, Back and Forward when there is no page to go to. */
static void update_toolbar(void)
{
    ULONG mask = 0, changed;
    int i;
    if (hv.home[0] == '\0') mask |= 1UL << TOOL_HOME;
    if (hv.cur <= 0) mask |= 1UL << TOOL_BACK;
    if (hv.cur < 0 || hv.cur >= hv.nhist - 1) mask |= 1UL << TOOL_FORWARD;
    if ((changed = mask ^ hv.disabled) == 0 || hv.bar == NULL) return;
    hv.disabled = mask;
    SetGadgetAttrs((struct Gadget *)hv.bar, hv.win, NULL, SPEEDBAR_Buttons, ~0UL, TAG_END);
    for (i = 0; i < NUMTOOLS; ++i) {
        int off = (mask >> i) & 1;
        Object *img;
        if (((changed >> i) & 1) == 0) continue;
        img = off && hv.ghosts[i] != NULL ? hv.ghosts[i] : hv.labels[i];
        SetSpeedButtonNodeAttrs(hv.nodes[i], SBNA_Disabled, (ULONG)off,
                                img != NULL ? SBNA_Image : TAG_IGNORE, (ULONG)img, TAG_END);
    }
    SetGadgetAttrs((struct Gadget *)hv.bar, hv.win, NULL, SPEEDBAR_Buttons, (ULONG)&hv.buttons, TAG_END);
    if (hv.win != NULL) RefreshGList((struct Gadget *)hv.bar, hv.win, NULL, 1);
}

/* ---- pages -------------------------------------------------------------- */

/** @brief Set the horizontal scroller from the page's width. */
static void sync_hscroll(void)
{
    ULONG left = 0, total = 0, vis = 0;
    GetAttr(HTML_Left, hv.html, &left);
    GetAttr(HTML_TotalWidth, hv.html, &total);
    GetAttr(HTML_VisibleWidth, hv.html, &vis);
    SetGadgetAttrs((struct Gadget *)hv.hscroll, hv.win, NULL,
                   SCROLLER_Total, total, SCROLLER_Visible, vis, SCROLLER_Top, left, TAG_DONE);
}

/** @brief Show the page's title in the window title. */
static void update_title(void)
{
    STRPTR title = NULL;
    GetAttr(HTML_Title, hv.html, (ULONG *)&title);
    if (title != NULL && *title != '\0') {
        strncpy(hv.title, (const char *)title, sizeof(hv.title) - 14);
        hv.title[sizeof(hv.title) - 14] = '\0';
        strcat(hv.title, " - HelpViewer");
    } else strcpy(hv.title, "HelpViewer");
    SetAttrs(hv.winobj, WA_Title, (ULONG)hv.title, TAG_DONE);
}

/**
 * @brief Return a page's path relative to the tree's directory (for the
 *        status line).
 * @param path The page.
 * @return The part after the directory, or the whole path.
 */
static const char *relative(const char *path)
{
    size_t n = strlen(hv.root);
    if (n == 0 || strncmp(path, hv.root, n) != 0) return path;
    path += n;
    if (*path == '/') ++path;
    return path;
}

/**
 * @brief Show a page.
 * @param path The page, may end with "#anchor".
 * @param record Non-zero to note it for Back (dropping the pages after
 *        the current one); zero when going back or forward.
 * @return Non-zero if it was shown.
 */
static int show_page(const char *path, int record)
{
    char file[PATHMAX];
    char *hash;
    BPTR lock;

    strncpy(file, path, sizeof(file) - 1);
    file[sizeof(file) - 1] = '\0';
    if ((hash = strchr(file, '#')) != NULL) *hash++ = '\0';
    if ((lock = Lock((STRPTR)file, ACCESS_READ)) == 0) {
        format(hv.status_text, sizeof(hv.status_text), S(MSG_NOT_FOUND), relative(file));
        set_status(hv.status_text);
        return 0;
    }
    UnLock(lock);
    /* remember where the page that is left was scrolled to */
    if (hv.cur >= 0) GetAttr(HTML_Top, hv.html, &hv.hist[hv.cur].top);
    SetGadgetAttrs((struct Gadget *)hv.html, hv.win, NULL, HTML_File, (ULONG)file, TAG_DONE);
    if (hash != NULL && *hash != '\0')
        SetGadgetAttrs((struct Gadget *)hv.html, hv.win, NULL, HTML_Anchor, (ULONG)hash, TAG_DONE);
    if (record) {
        char *copy = AllocVec((ULONG)strlen(path) + 1, MEMF_ANY);
        while (hv.nhist > hv.cur + 1) FreeVec(hv.hist[--hv.nhist].path);
        if (hv.nhist == MAXHIST) {
            FreeVec(hv.hist[0].path);
            memmove(hv.hist, hv.hist + 1, (MAXHIST - 1) * sizeof(Visit));
            --hv.nhist;
        }
        if (copy != NULL) {
            strcpy(copy, path);
            hv.hist[hv.nhist].path = copy;
            hv.hist[hv.nhist].top = 0;
            hv.cur = hv.nhist++;
        }
    }
    set_status(relative(path));
    update_title();
    sync_hscroll();
    update_toolbar();
    return 1;
}

/**
 * @brief Go back or forward in the pages seen, to where they were
 *        scrolled.
 * @param delta -1 for Back, 1 for Forward.
 */
static void go(int delta)
{
    int to = hv.cur + delta;
    ULONG top;
    if (to < 0 || to >= hv.nhist) return;
    top = hv.hist[to].top;
    if (hv.cur >= 0) GetAttr(HTML_Top, hv.html, &hv.hist[hv.cur].top);
    hv.cur = to;
    if (show_page(hv.hist[to].path, 0) && strchr(hv.hist[to].path, '#') == NULL)
        SetGadgetAttrs((struct Gadget *)hv.html, hv.win, NULL, HTML_Top, top, TAG_DONE);
}

/** @brief Follow the link the user clicked. */
static void follow_link(void)
{
    STRPTR url = NULL, base = NULL;
    char rel[PATHMAX], path[PATHMAX];

    GetAttr(HTML_LinkURL, hv.html, (ULONG *)&url);
    GetAttr(HTML_BaseDir, hv.html, (ULONG *)&base);
    if (url == NULL) return;
    if (url[0] == '#') return;                  /* HTML_AutoAnchors scrolled there */
    if (html_link_is_external((const char *)url)) {
        format(hv.status_text, sizeof(hv.status_text), S(MSG_EXTERNAL), (const char *)url);
        set_status(hv.status_text);
        return;
    }
    if (!html_link_to_path((const char *)url, rel, sizeof(rel))) return;
    strncpy(path, base != NULL ? (const char *)base : "", sizeof(path) - 1);
    path[sizeof(path) - 1] = '\0';
    if (!AddPart((STRPTR)path, (STRPTR)rel, sizeof(path))) return;
    show_page(path, 1);
}

/**
 * @brief Find out what a location shows: the directory that becomes the
 *        tree's root (a help directory's language drawer) and the first
 *        page (the directory's index page, or the page given).
 * @param path The directory or page.
 * @param[out] dir The root directory, a full path (PATHMAX bytes).
 * @param[out] page The first page, "" if none (PATHMAX bytes).
 * @return Non-zero if the location exists.
 */
static int locate(const char *path, char *dir, char *page)
{
    BPTR lock;
    int directory = is_directory(path);

    if ((lock = Lock((STRPTR)path, ACCESS_READ)) == 0) return 0;
    if (!NameFromLock(lock, (STRPTR)dir, PATHMAX)) dir[0] = '\0';
    UnLock(lock);
    if (dir[0] == '\0') return 0;
    if (directory) {
        if (!find_index(dir, page, PATHMAX) &&
            (!language_dir(dir, PATHMAX) || !find_index(dir, page, PATHMAX)))
            page[0] = '\0';
    } else {
        strcpy(page, dir);
        *PathPart((STRPTR)dir) = '\0';
    }
    return 1;
}

/**
 * @brief Open a directory: it becomes the tree's root and its index page
 *        is shown. A page is shown with its directory as root.
 * @param path The directory or page.
 * @return Non-zero on success.
 */
static int open_location(const char *path)
{
    char dir[PATHMAX], page[PATHMAX];

    if (!locate(path, dir, page)) {
        format(hv.status_text, sizeof(hv.status_text), S(MSG_NOT_FOUND), path);
        set_status(hv.status_text);
        return 0;
    }
    strcpy(hv.root, dir);
    /* the home page (the root's index page) has no entry in the tree: its
     * title is the tree's title, Home shows it */
    if (!find_index(hv.root, hv.home, sizeof(hv.home))) hv.home[0] = '\0';
    if (hv.home[0] == '\0' || !read_title(hv.home, hv.title, sizeof(hv.title))) hv.title[0] = '\0';
    SetGadgetAttrs((struct Gadget *)hv.tree, hv.win, NULL, DIRTREE_Root, (ULONG)hv.root,
                   DIRTREE_Title, (ULONG)hv.title, TAG_DONE);
    update_toolbar();
    if (page[0] != '\0') return show_page(page, 1);
    return 1;
}

/** @brief Let the user choose a directory with an ASL requester and open it. */
static void request_directory(void)
{
    struct FileRequester *fr;
    char path[PATHMAX];
    if (AslBase == NULL || (fr = AllocAslRequestTags(ASL_FileRequest,
            ASLFR_TitleText, (ULONG)S(MSG_ASL_DIR), ASLFR_DrawersOnly, TRUE, TAG_END)) == NULL)
        return;
    if (AslRequestTags(fr, ASLFR_Window, (ULONG)hv.win, ASLFR_SleepWindow, TRUE,
                       hv.root[0] != '\0' ? ASLFR_InitialDrawer : TAG_IGNORE, (ULONG)hv.root,
                       TAG_END)) {
        strncpy(path, (const char *)fr->fr_Drawer, sizeof(path) - 1);
        path[sizeof(path) - 1] = '\0';
        if (fr->fr_File[0] == '\0' || AddPart((STRPTR)path, fr->fr_File, sizeof(path)))
            open_location(path);
    }
    FreeAslRequest(fr);
}

/**
 * @brief A click in the tree: open or close a directory (double-click),
 *        show the selected page or the selected directory's index page.
 */
static void tree_event(void)
{
    ULONG event = DIRTREE_EV_NONE;
    STRPTR path = NULL;
    char page[PATHMAX];
    SetGadgetAttrs((struct Gadget *)hv.tree, hv.win, NULL, DIRTREE_HandleEvent, TRUE, TAG_DONE);
    GetAttr(DIRTREE_Event, hv.tree, &event);
    if (event == DIRTREE_EV_ACTIVATE) GetAttr(DIRTREE_ActivePath, hv.tree, (ULONG *)&path);
    else GetAttr(DIRTREE_SelectedPath, hv.tree, (ULONG *)&path);
    if (path == NULL) return;
    if (is_directory((const char *)path)) {
        if (!find_index((const char *)path, page, sizeof(page))) return;
    } else {
        strncpy(page, (const char *)path, sizeof(page) - 1);
        page[sizeof(page) - 1] = '\0';
    }
    if (hv.cur >= 0 && strcmp(hv.hist[hv.cur].path, page) == 0) return;
    show_page(page, 1);
}

/**
 * @brief Scroll the page.
 * @param dy Pixels down (negative: up).
 * @param dx Pixels right (negative: left).
 */
static void scroll_by(LONG dy, LONG dx)
{
    ULONG top = 0, left = 0;
    LONG t, l;
    GetAttr(HTML_Top, hv.html, &top);
    GetAttr(HTML_Left, hv.html, &left);
    t = (LONG)top + dy;
    l = (LONG)left + dx;
    if (t < 0) t = 0;
    if (l < 0) l = 0;
    SetGadgetAttrs((struct Gadget *)hv.html, hv.win, NULL, HTML_Top, (ULONG)t, HTML_Left, (ULONG)l, TAG_DONE);
    if (dx != 0) sync_hscroll();
}

/**
 * @brief Scroll by a page.
 * @param dir 1 down, -1 up.
 */
static void page_by(int dir)
{
    ULONG vis = 0, lh = 8;
    GetAttr(HTML_Visible, hv.html, &vis);
    GetAttr(HTML_LineHeight, hv.html, &lh);
    scroll_by(dir * ((LONG)vis - (LONG)lh), 0);
}

/** @brief Copy the selected text to the clipboard. */
static void copy_selection(void)
{
    ULONG has = FALSE;
    GetAttr(HTML_HasSelection, hv.html, &has);
    if (has) SetGadgetAttrs((struct Gadget *)hv.html, hv.win, NULL, HTML_Copy, TRUE, TAG_DONE);
    set_status(S(has ? MSG_COPIED : MSG_NOTHING_SELECTED));
}

/** @brief Show the About requester. */
static void about(void)
{
    char text[300];
    format(text, sizeof(text), S(MSG_ABOUT), HV_VERSION);
    message(text);
}

/**
 * @brief Append a number to a text.
 * @param[in,out] p Where to write; moved behind the number.
 * @param n The number.
 */
static void put_number(char **p, LONG n)
{
    char digits[12];
    int i = 0;
    ULONG u = n < 0 ? (ULONG)-n : (ULONG)n;
    if (n < 0) *(*p)++ = '-';
    do digits[i++] = (char)('0' + u % 10); while ((u /= 10) != 0);
    while (i > 0) *(*p)++ = digits[--i];
    **p = '\0';
}

/**
 * @brief Write the window's position and size as WINDOW= into the program
 *        icon (Project/Snapshot Window).
 *
 * Replaces an existing WINDOW= (or disabled "(WINDOW=...)") in its place;
 * otherwise the entry goes in front of a NewIcons block. Other tool types
 * stay as they are. Reports the result in the status line.
 */
static void snapshot_window(void)
{
    struct DiskObject *dob;
    STRPTR *old, *tt;
    char entry[64], *p = entry;
    ULONG n = 0, i, at;
    int ok = 0;

    if (hv.win == NULL) return;
    strcpy(p, "WINDOW=");
    p += 7;
    put_number(&p, hv.win->LeftEdge); *p++ = ',';
    put_number(&p, hv.win->TopEdge); *p++ = ',';
    put_number(&p, hv.win->Width); *p++ = ',';
    put_number(&p, hv.win->Height);
    if (IconBase != NULL && opt.icon[0] != '\0' &&
        ((dob = GetDiskObject((STRPTR)opt.icon)) != NULL ||
         (dob = GetDiskObjectNew((STRPTR)opt.icon)) != NULL)) {
        old = (STRPTR *)dob->do_ToolTypes;
        while (old != NULL && old[n] != NULL) ++n;
        for (at = 0; at < n; ++at) {
            const char *t = (const char *)old[at];
            if (*t == '(') ++t;
            if (Strnicmp((STRPTR)t, (STRPTR)"WINDOW=", 7) == 0) break;
        }
        if (at == n)                            /* new: in front of NewIcons */
            for (at = 0; at < n; ++at)
                if (strncmp((const char *)old[at], "*** DON'T EDIT", 14) == 0) {
                    if (at != 0 && strcmp((const char *)old[at - 1], " ") == 0) --at;
                    break;
                }
        if ((tt = AllocVec((n + 2) * sizeof(STRPTR), MEMF_ANY | MEMF_CLEAR)) != NULL) {
            ULONG k = 0;
            for (i = 0; i < at; ++i) tt[k++] = old[i];
            tt[k++] = (STRPTR)entry;
            for (i = at; i < n; ++i) {          /* without the old WINDOW= */
                const char *t = (const char *)old[i];
                if (*t == '(') ++t;
                if (Strnicmp((STRPTR)t, (STRPTR)"WINDOW=", 7) != 0) tt[k++] = old[i];
            }
            dob->do_ToolTypes = tt;
            ok = PutDiskObject((STRPTR)opt.icon, dob) != 0;
            dob->do_ToolTypes = old;
            FreeVec(tt);
        }
        FreeDiskObject(dob);
    }
    set_status(S(ok ? MSG_SNAPSHOT_DONE : MSG_SNAPSHOT_FAILED));
}

/**
 * @brief Carry out a command of the menu or the toolbar.
 * @param id The MID_.
 * @return Non-zero to quit.
 */
static int command(ULONG id)
{
    switch (id) {
        case MID_OPEN: request_directory(); break;
        case MID_HOME:
            if (hv.home[0] != '\0' && (hv.cur < 0 || strcmp(hv.hist[hv.cur].path, hv.home) != 0))
                show_page(hv.home, 1);
            break;
        case MID_BACK: go(-1); break;
        case MID_FORWARD: go(1); break;
        case MID_SNAPSHOT: snapshot_window(); break;
        case MID_ABOUT: about(); break;
        case MID_QUIT: return 1;
        case MID_COPY: copy_selection(); break;
        case MID_SELALL:
            SetGadgetAttrs((struct Gadget *)hv.html, hv.win, NULL, HTML_SelectAll, TRUE, TAG_DONE);
            break;
    }
    return 0;
}

/* ---- start -------------------------------------------------------------- */

/**
 * @brief Read a list of numbers ("10,20,640,480").
 * @param text The text.
 * @param[out] out The numbers.
 * @param count How many there must be.
 * @return Non-zero if all were there.
 */
static int parse_numbers(CONST_STRPTR text, LONG *out, int count)
{
    LONG len;
    int i;
    for (i = 0; i < count; ++i) {
        while (*text == ' ') ++text;
        if ((len = StrToLong(text, &out[i])) <= 0) return 0;
        text += len;
        while (*text == ' ') ++text;
        if (i < count - 1 && *text++ != ',') return 0;
    }
    return 1;
}

/**
 * @brief Read the options from the tool types of the program icon.
 * @param name The icon (without .info), relative to the current directory.
 */
static void icon_options(const char *name)
{
    struct DiskObject *dob;
    CONST_STRPTR *tt;
    STRPTR v;
    LONG box[4];
    if (IconBase == NULL || (dob = GetDiskObject((STRPTR)name)) == NULL) return;
    tt = (CONST_STRPTR *)dob->do_ToolTypes;
    if (FindToolType(tt, (STRPTR)"TTF")) opt.ttf = TRUE;
    if ((v = FindToolType(tt, (STRPTR)"FONTSET")) != NULL)
        strncpy(opt.fontset, (char *)v, sizeof(opt.fontset) - 1);
    if ((v = FindToolType(tt, (STRPTR)"SIZE")) != NULL) StrToLong(v, &opt.size);
    if ((v = FindToolType(tt, (STRPTR)"PORTNAME")) != NULL)
        strncpy(opt.portname, (char *)v, sizeof(opt.portname) - 1);
    if ((v = FindToolType(tt, (STRPTR)"DIR")) != NULL)
        strncpy(opt.dir, (char *)v, sizeof(opt.dir) - 1);
    if ((v = FindToolType(tt, (STRPTR)"WINDOW")) != NULL && parse_numbers(v, box, 4) &&
        box[2] > 0 && box[3] > 0)
        memcpy(opt.box, box, sizeof(box));
    FreeDiskObject(dob);
}

/**
 * @brief Read the options of a Workbench start: the tool types of the
 *        program icon and a drawer or page given as project.
 * @param wbs The Workbench start message.
 */
static void wb_options(struct WBStartup *wbs)
{
    struct WBArg *wa = wbs->sm_ArgList;
    BPTR old;
    if (wbs->sm_NumArgs < 1) return;
    old = CurrentDir(wa[0].wa_Lock);
    icon_options((const char *)wa[0].wa_Name);
    CurrentDir(old);
    if (NameFromLock(wa[0].wa_Lock, (STRPTR)opt.icon, sizeof(opt.icon)) == 0 ||
        AddPart((STRPTR)opt.icon, wa[0].wa_Name, sizeof(opt.icon)) == 0)
        opt.icon[0] = '\0';
    /* a drawer (its name may be empty for a volume) or a page */
    if (wbs->sm_NumArgs > 1 && wa[1].wa_Lock != 0 &&
        NameFromLock(wa[1].wa_Lock, (STRPTR)opt.dir, sizeof(opt.dir)))
        AddPart((STRPTR)opt.dir, wa[1].wa_Name, sizeof(opt.dir));
}

/**
 * @brief Open one of TinyDev's classes: installed, or next to the program.
 * @param name The class file name.
 * @param version_needed The version needed.
 * @return The library, or NULL.
 */
static struct Library *open_own_class(const char *name, ULONG version_needed)
{
    char path[64];
    struct Library *lib;
    strcpy(path, "gadgets/");
    strcat(path, name);
    if ((lib = OpenLibrary((STRPTR)path, version_needed)) != NULL) return lib;
    strcpy(path, "PROGDIR:Classes/Gadgets/");
    strcat(path, name);
    return OpenLibrary((STRPTR)path, version_needed);
}

/**
 * @brief Open html.gadget, or htmlttf.gadget with TTF: installed or next
 *        to the program (as HTMLDemo does).
 * @return The library, or NULL.
 */
static struct Library *open_html_class(void)
{
    static const char *const html[] = {
        "gadgets/html.gadget", "PROGDIR:Classes/Gadgets/html.gadget", "PROGDIR:html.gadget", NULL
    };
    static const char *const ttf[] = {
        "gadgets/htmlttf.gadget", "PROGDIR:Classes/Gadgets/htmlttf.gadget", "PROGDIR:htmlttf.gadget", NULL
    };
    const char *const *names = opt.ttf ? ttf : html;
    struct Library *lib = NULL;
    int i;
    for (i = 0; names[i] != NULL && lib == NULL; ++i) lib = OpenLibrary((STRPTR)names[i], 1);
    return lib;
}

/**
 * @brief Create the gadgets and open the window.
 * @return Non-zero on success.
 */
static int open_window(void)
{
    Class *htmlclass = HTML_GetClass();
    WORD w = (WORD)(hv.screen->Width * 3 / 4), h = (WORD)(hv.screen->Height * 3 / 4);
    WORD l = 0, t = 0;
    int i;

    if (opt.box[2] > 0) {               /* snapshot: pushed into the screen */
        w = (WORD)(opt.box[2] < hv.screen->Width ? opt.box[2] : hv.screen->Width);
        h = (WORD)(opt.box[3] < hv.screen->Height ? opt.box[3] : hv.screen->Height);
        l = (WORD)(opt.box[0] < 0 ? 0 : opt.box[0] > hv.screen->Width - w ? hv.screen->Width - w : opt.box[0]);
        t = (WORD)(opt.box[1] < 0 ? 0 : opt.box[1] > hv.screen->Height - h ? hv.screen->Height - h : opt.box[1]);
    }

    for (i = 0; i < (int)(sizeof(menu_texts) / sizeof(menu_texts[0])); ++i)
        if (menu_texts[i] >= 0) menus[i].nm_Label = (STRPTR)S(menu_texts[i]);
    if (!make_toolbar()) return 0;
    hv.html = NewObject(htmlclass, NULL,
        GA_ID, GID_HTML,
        GA_RelVerify, TRUE,
        HTML_AutoAnchors, TRUE,
        HTML_Text, (ULONG)S(MSG_WELCOME),
        opt.fontset[0] ? HTMLTTF_FontSet : TAG_IGNORE, (ULONG)opt.fontset,
        opt.size > 0 ? HTMLTTF_Size : TAG_IGNORE, (ULONG)opt.size,
        TAG_DONE);
    hv.vscroll = NewObject(SCROLLER_GetClass(), NULL,
        GA_ID, GID_VSCROLL, GA_RelVerify, TRUE,
        SCROLLER_Orientation, SORIENT_VERT, SCROLLER_Arrows, TRUE, SCROLLER_ArrowDelta, 16,
        ICA_TARGET, (ULONG)hv.html, ICA_MAP, (ULONG)vscroll_map,
        TAG_DONE);
    hv.hscroll = NewObject(SCROLLER_GetClass(), NULL,
        GA_ID, GID_HSCROLL, GA_RelVerify, TRUE,
        SCROLLER_Orientation, SORIENT_HORIZ, SCROLLER_Arrows, TRUE, SCROLLER_ArrowDelta, 16,
        ICA_TARGET, (ULONG)hv.html, ICA_MAP, (ULONG)hscroll_map,
        TAG_DONE);
    hv.name_hook.h_Entry = (ULONG (*)())HookEntry;
    hv.name_hook.h_SubEntry = (ULONG (*)())name_hook;
    hv.tree = NewObject(NULL, DIRTREE_CLASSNAME,
        GA_ID, GID_TREE,
        GA_RelVerify, TRUE,
        LISTBROWSER_AutoFit, TRUE,
        LISTBROWSER_HorizontalProp, TRUE,
        LISTBROWSER_AutoWheel, TRUE,
        DIRTREE_Pattern, (ULONG)"#?.(htm|html)",
        /* a directory's index page is shown by the directory's entry */
        DIRTREE_ExcludePattern, (ULONG)"index.(htm|html)",
        DIRTREE_Sort, DIRTREE_SORT_NATURAL,
        DIRTREE_HideEmpty, TRUE,
        DIRTREE_NameHook, (ULONG)&hv.name_hook,
        DIRTREE_LoadingText, (ULONG)S(MSG_TREE_LOADING),
        DIRTREE_EmptyText, (ULONG)S(MSG_TREE_EMPTY),
        DIRTREE_UnavailableText, (ULONG)S(MSG_TREE_UNAVAILABLE),
        TAG_DONE);
    hv.status = NewObject(BUTTON_GetClass(), NULL,
        GA_ID, GID_STATUS, GA_ReadOnly, TRUE,
        GA_Underscore, 0,                   /* file names may contain '_' */
        GA_Text, (ULONG)"",
        BUTTON_BevelStyle, BVS_DISPLAY,
        BUTTON_Justification, BCJ_LEFT,
        TAG_DONE);
    if (hv.html == NULL || hv.vscroll == NULL || hv.hscroll == NULL || hv.tree == NULL ||
        hv.status == NULL)
        return 0;
    SetAttrs(hv.html, ICA_TARGET, (ULONG)hv.vscroll, ICA_MAP, (ULONG)html_map, TAG_DONE);

    hv.winobj = NewObject(WINDOW_GetClass(), NULL,
        WA_Title, (ULONG)"HelpViewer",
        WA_PubScreen, (ULONG)hv.screen,
        WA_Activate, TRUE,
        WA_DepthGadget, TRUE,
        WA_DragBar, TRUE,
        WA_CloseGadget, TRUE,
        WA_SizeGadget, TRUE,
        WA_Width, (ULONG)w,
        WA_Height, (ULONG)h,
        opt.box[2] > 0 ? WA_Left : TAG_IGNORE, (ULONG)l,
        opt.box[2] > 0 ? WA_Top : TAG_IGNORE, (ULONG)t,
        WA_IDCMP, IDCMP_RAWKEY | IDCMP_VANILLAKEY,
        opt.box[2] > 0 ? TAG_IGNORE : WINDOW_Position, WPOS_CENTERSCREEN,
        WINDOW_NewMenu, (ULONG)menus,
        WINDOW_HintInfo, (ULONG)hv.hints,
        WINDOW_GadgetHelp, TRUE,
        WA_NewLookMenus, TRUE,
        WINDOW_ParentGroup, (ULONG)NewObject(LAYOUT_GetClass(), NULL,
            LAYOUT_Orientation, LAYOUT_ORIENT_VERT,
            LAYOUT_DeferLayout, TRUE,
            LAYOUT_AddChild, (ULONG)hv.bar,
            CHILD_WeightedHeight, 0,
            LAYOUT_AddChild, (ULONG)NewObject(LAYOUT_GetClass(), NULL,
                LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ,
                LAYOUT_AddChild, (ULONG)hv.tree,
                CHILD_MinWidth, 120,
                CHILD_WeightedWidth, 25,
                CHILD_WeightBar, TRUE,
                LAYOUT_AddChild, (ULONG)NewObject(LAYOUT_GetClass(), NULL,
                    LAYOUT_Orientation, LAYOUT_ORIENT_VERT,
                    LAYOUT_InnerSpacing, 0,
                    LAYOUT_AddChild, (ULONG)NewObject(LAYOUT_GetClass(), NULL,
                        LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ,
                        LAYOUT_InnerSpacing, 0,
                        LAYOUT_AddChild, (ULONG)hv.html,
                        LAYOUT_AddChild, (ULONG)hv.vscroll,
                        CHILD_WeightedWidth, 0,
                        TAG_DONE),
                    LAYOUT_AddChild, (ULONG)hv.hscroll,
                    CHILD_WeightedHeight, 0,
                    TAG_DONE),
                CHILD_WeightedWidth, 75,
                TAG_DONE),
            LAYOUT_AddChild, (ULONG)hv.status,
            CHILD_WeightedHeight, 0,
            TAG_DONE),
        TAG_DONE);
    if (hv.winobj == NULL || (hv.win = (struct Window *)DoMethod(hv.winobj, WM_OPEN, NULL)) == NULL)
        return 0;
    SetGadgetAttrs((struct Gadget *)hv.bar, hv.win, NULL, SPEEDBAR_Window, (ULONG)hv.win, TAG_DONE);
    update_toolbar();
    update_title();
    sync_hscroll();
    return 1;
}

/**
 * @brief Handle the window's input.
 * @return Non-zero to quit.
 */
static int handle_input(void)
{
    ULONG result;
    WORD code = 0;
    while ((result = DoMethod(hv.winobj, WM_HANDLEINPUT, &code)) != WMHI_LASTMSG) {
        switch (result & WMHI_CLASSMASK) {
        case WMHI_CLOSEWINDOW:
            return 1;
        case WMHI_GADGETUP:
            switch (result & WMHI_GADGETMASK) {
            case GID_HTML: follow_link(); break;
            case GID_TREE: tree_event(); break;
            case GID_TOOLBAR: if (command((ULONG)(UWORD)code)) return 1; break;
            }
            break;
        case WMHI_NEWSIZE:
            sync_hscroll();
            break;
        case WMHI_MENUPICK: {
            UWORD num = (UWORD)(result & WMHI_MENUMASK);
            while (num != MENUNULL) {
                struct MenuItem *item = ItemAddress(hv.win->MenuStrip, num);
                if (item == NULL) break;
                if (command((ULONG)GTMENUITEM_USERDATA(item))) return 1;
                num = item->NextSelect;
            }
            break;
        }
        case WMHI_VANILLAKEY:
            switch (result & WMHI_KEYMASK) {
            case ' ': page_by(1); break;
            case 8:   go(-1); break;                /* Backspace */
            case 27:  return 1;                     /* Esc */
            }
            break;
        case WMHI_RAWKEY:
            switch (result & WMHI_KEYMASK) {
            case 0x4C: scroll_by(-16, 0); break;    /* cursor up */
            case 0x4D: scroll_by(16, 0); break;     /* cursor down */
            case 0x4F: scroll_by(0, -16); break;    /* cursor left */
            case 0x4E: scroll_by(0, 16); break;     /* cursor right */
            case 0x7A: scroll_by(-32, 0); break;    /* mouse wheel up */
            case 0x7B: scroll_by(32, 0); break;     /* mouse wheel down */
            case 0x3F: page_by(-1); break;          /* numpad 9 / PgUp */
            case 0x1F: page_by(1); break;           /* numpad 3 / PgDn */
            }
            break;
        }
    }
    return 0;
}

/**
 * @brief With a PORTNAME: pass the location to the HelpViewer of that name
 *        that is already running, or become it (add the public port).
 *
 * The port is PORTPREFIX and the name in capitals, so names differing in
 * case meet and no other program's port is hit. Without a name nothing
 * happens: every start opens a viewer of its own.
 *
 * @param path The location to show (relative to the current directory),
 *             "" for none.
 * @return Non-zero if a running HelpViewer took it (this one ends).
 */
static int pass_to_running(const char *path)
{
    struct MsgPort *port, *reply;
    OpenMessage *m;
    BPTR lock;
    char *p;

    if (opt.portname[0] == '\0') return 0;
    strcpy(hv.portname, PORTPREFIX);
    strncat(hv.portname, opt.portname, sizeof(hv.portname) - strlen(hv.portname) - 1);
    for (p = hv.portname; *p != '\0'; ++p) *p = (char)ToUpper((ULONG)(UBYTE)*p);
    if ((port = CreateMsgPort()) == NULL) return 0;
    Forbid();
    if (FindPort((STRPTR)hv.portname) == NULL) {
        port->mp_Node.ln_Name = hv.portname;
        port->mp_Node.ln_Pri = 0;
        AddPort(port);
        hv.port = port;
        Permit();
        return 0;
    }
    Permit();
    DeleteMsgPort(port);
    if ((reply = CreateMsgPort()) == NULL) return 0;
    if ((m = AllocVec(sizeof(*m), MEMF_PUBLIC | MEMF_CLEAR)) == NULL) {
        DeleteMsgPort(reply);
        return 0;
    }
    /* the running viewer has another current directory: a full path */
    if (path[0] != '\0' && (lock = Lock((STRPTR)path, ACCESS_READ)) != 0) {
        if (!NameFromLock(lock, (STRPTR)m->path, sizeof(m->path))) m->path[0] = '\0';
        UnLock(lock);
    }
    m->msg.mn_ReplyPort = reply;
    m->msg.mn_Length = (UWORD)sizeof(*m);
    Forbid();                                   /* it may have ended meanwhile */
    if ((port = FindPort((STRPTR)hv.portname)) != NULL) PutMsg(port, &m->msg);
    Permit();
    if (port != NULL) {
        WaitPort(reply);
        GetMsg(reply);
    }
    FreeVec(m);
    DeleteMsgPort(reply);
    return port != NULL;
}

/**
 * @brief Take the locations passed by later starts: show each (unless it
 *        is the root shown already) and come to the front.
 */
static void handle_port(void)
{
    OpenMessage *m;
    char dir[PATHMAX], page[PATHMAX];
    while ((m = (OpenMessage *)GetMsg(hv.port)) != NULL) {
        if (m->path[0] != '\0' &&
            (!locate(m->path, dir, page) || Stricmp((STRPTR)dir, (STRPTR)hv.root) != 0 ||
             !is_directory(m->path)))
            open_location(m->path);
        ReplyMsg(&m->msg);
        if (hv.win != NULL) {
            WindowToFront(hv.win);
            ActivateWindow(hv.win);
        }
    }
}

/**
 * @brief Remove the public port, answering the messages still in it.
 */
static void close_port(void)
{
    struct Message *m;
    if (hv.port == NULL) return;
    Forbid();
    RemPort(hv.port);
    Permit();
    while ((m = GetMsg(hv.port)) != NULL) ReplyMsg(m);
    DeleteMsgPort(hv.port);
    hv.port = NULL;
}

/**
 * @brief The HelpViewer program.
 * @return RETURN_OK, or RETURN_FAIL if it could not start.
 */
int main(void)
{
    struct RDArgs *rda = NULL;
    LONG args[5] = { 0, 0, 0, 0, 0 };   /* DIR, TTF, FONTSET, SIZE, PORTNAME */
    ULONG winsig = 0, treesig = 0, portsig = 0;
    int rc = RETURN_FAIL, done = 0;
    const char *missing = NULL;
    char text[120];

    IconBase = OpenLibrary((STRPTR)"icon.library", 37);
    if (_WBenchMsg != NULL) wb_options(_WBenchMsg);
    else {
        /* the program icon also from the Shell; the arguments win */
        strcpy(opt.icon, "PROGDIR:");
        if (GetProgramName((STRPTR)text, sizeof(text)))
            AddPart((STRPTR)opt.icon, FilePart((STRPTR)text), sizeof(opt.icon));
        else
            AddPart((STRPTR)opt.icon, (STRPTR)"HelpViewer", sizeof(opt.icon));
        icon_options(opt.icon);
        if ((rda = ReadArgs((STRPTR)"DIR,TTF/S,FONTSET/K,SIZE/K/N,PORTNAME/K", args, NULL)) == NULL) {
            PrintFault(IoErr(), (STRPTR)"HelpViewer");
            goto out;
        }
        if (args[0]) strncpy(opt.dir, (char *)args[0], sizeof(opt.dir) - 1);
        if (args[1]) opt.ttf = TRUE;        /* a switch: only adds to the tool type */
        if (args[2]) strncpy(opt.fontset, (char *)args[2], sizeof(opt.fontset) - 1);
        if (args[3]) opt.size = *(LONG *)args[3];
        if (args[4]) strncpy(opt.portname, (char *)args[4], sizeof(opt.portname) - 1);
    }
    if (pass_to_running(opt.dir)) {
        rc = RETURN_OK;
        goto out;
    }
    if ((LocaleBase = (struct LocaleBase *)OpenLibrary((STRPTR)"locale.library", 38)) != NULL)
        hv.catalog = OpenCatalog(NULL, (STRPTR)"HelpViewer.catalog",
                                 OC_BuiltInLanguage, (ULONG)"english", TAG_END);

    AslBase = OpenLibrary((STRPTR)"asl.library", 38);
    if ((WindowBase = OpenLibrary((STRPTR)"window.class", 44)) == NULL) missing = "window.class";
    else if ((LayoutBase = OpenLibrary((STRPTR)"gadgets/layout.gadget", 44)) == NULL) missing = "layout.gadget";
    else if ((ButtonBase = OpenLibrary((STRPTR)"gadgets/button.gadget", 44)) == NULL) missing = "button.gadget";
    else if ((ScrollerBase = OpenLibrary((STRPTR)"gadgets/scroller.gadget", 44)) == NULL) missing = "scroller.gadget";
    else if ((SpeedBarBase = OpenLibrary((STRPTR)"gadgets/speedbar.gadget", 44)) == NULL) missing = "speedbar.gadget";
    else if ((LabelBase = OpenLibrary((STRPTR)"images/label.image", 44)) == NULL) missing = "label.image";
    else if ((ListBrowserBase = OpenLibrary((STRPTR)"gadgets/listbrowser.gadget", 44)) == NULL) missing = "listbrowser.gadget";
    else if ((DirTreeBase = open_own_class("dirtree.gadget", DIRTREE_VERSION)) == NULL) missing = "dirtree.gadget";
    else if ((HTMLBase = open_html_class()) == NULL) missing = opt.ttf ? "htmlttf.gadget" : "html.gadget";
    if (missing != NULL) {
        format(text, sizeof(text), S(MSG_ERR_CLASS), missing);
        message(text);
        goto out;
    }
    if ((hv.screen = LockPubScreen(NULL)) == NULL || (hv.dri = GetScreenDrawInfo(hv.screen)) == NULL)
        goto out;
    if (!open_window()) {
        message(S(MSG_ERR_WINDOW));
        goto out;
    }
    /* without a directory it shows the welcome text (no default) */
    if (opt.dir[0] != '\0') open_location(opt.dir);

    GetAttr(WINDOW_SigMask, hv.winobj, &winsig);
    GetAttr(DIRTREE_SignalMask, hv.tree, &treesig);
    if (hv.port != NULL) portsig = 1UL << hv.port->mp_SigBit;
    while (!done) {
        ULONG sig = Wait(winsig | treesig | portsig | SIGBREAKF_CTRL_C);
        if (sig & portsig) handle_port();
        if (sig & SIGBREAKF_CTRL_C) done = 1;
        if (sig & treesig)
            SetGadgetAttrs((struct Gadget *)hv.tree, hv.win, NULL,
                           DIRTREE_Refresh, DIRTREE_RF_CHANGED, TAG_DONE);
        if (handle_input()) done = 1;
    }
    rc = RETURN_OK;

out:
    close_port();
    if (hv.winobj != NULL) DisposeObject(hv.winobj);    /* disposes all gadgets */
    else {
        if (hv.html != NULL) DisposeObject(hv.html);
        if (hv.vscroll != NULL) DisposeObject(hv.vscroll);
        if (hv.hscroll != NULL) DisposeObject(hv.hscroll);
        if (hv.tree != NULL) DisposeObject(hv.tree);
        if (hv.status != NULL) DisposeObject(hv.status);
        if (hv.bar != NULL) DisposeObject(hv.bar);
    }
    {
        struct Node *n;
        int i;
        if (hv.buttons.lh_Head != NULL)
            while ((n = RemHead(&hv.buttons)) != NULL) FreeSpeedButtonNode(n);
        for (i = 0; i < NUMTOOLS; ++i) {
            if (hv.labels[i] != NULL) DisposeObject(hv.labels[i]);
            if (hv.ghosts[i] != NULL) DisposeObject(hv.ghosts[i]);
        }
    }
    while (hv.nhist > 0) FreeVec(hv.hist[--hv.nhist].path);
    if (hv.screen != NULL) {
        if (hv.ghost_pen >= 0) ReleasePen(hv.screen->ViewPort.ColorMap, (ULONG)hv.ghost_pen);
        if (hv.dri != NULL) FreeScreenDrawInfo(hv.screen, hv.dri);
        UnlockPubScreen(NULL, hv.screen);
    }
    if (HTMLBase != NULL) CloseLibrary(HTMLBase);
    if (DirTreeBase != NULL) CloseLibrary(DirTreeBase);
    if (ListBrowserBase != NULL) CloseLibrary(ListBrowserBase);
    if (LabelBase != NULL) CloseLibrary(LabelBase);
    if (SpeedBarBase != NULL) CloseLibrary(SpeedBarBase);
    if (ScrollerBase != NULL) CloseLibrary(ScrollerBase);
    if (ButtonBase != NULL) CloseLibrary(ButtonBase);
    if (LayoutBase != NULL) CloseLibrary(LayoutBase);
    if (WindowBase != NULL) CloseLibrary(WindowBase);
    if (AslBase != NULL) CloseLibrary(AslBase);
    if (LocaleBase != NULL) {
        CloseCatalog(hv.catalog);
        CloseLibrary((struct Library *)LocaleBase);
    }
    if (IconBase != NULL) CloseLibrary(IconBase);
    if (rda != NULL) FreeArgs(rda);
    (void)version;
    return rc;
}
