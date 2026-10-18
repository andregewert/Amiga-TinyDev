/*
 * dirtree_class.c - dirtree.gadget, a lazily loaded directory tree
 *
 * Subclass of listbrowser.gadget. The class owns a hierarchical node list:
 * every node carries a TreeEntry (path, name, kind) as its user data.
 * Directories get a hidden "Loading..." placeholder child so that the
 * listbrowser shows an expand arrow; their real children are read only
 * when the directory is expanded and are freed again when it collapses.
 * Reading is bounded by DIRTREE_MaxDepth and DIRTREE_MaxEntries.
 *
 * DOS is only used from OM_NEW and OM_SET (DIRTREE_Root,
 * DIRTREE_HandleEvent), which run on the application's process, never
 * from input handling. OM_SET also brings the GadgetInfo for redrawing.
 *
 * Copyright (c) 2026 André Gewert <agewert@ubergeek.de>
 * Released under the MIT License, see LICENSE.
 */
#include <exec/memory.h>
#include <graphics/gfxbase.h>
#include <graphics/rastport.h>
#include <graphics/text.h>
#include <libraries/gadtools.h>
#include <proto/graphics.h>
#include <dos/dos.h>
#include <dos/exall.h>
#include <intuition/classes.h>
#include <intuition/classusr.h>
#include <intuition/gadgetclass.h>
#include <gadgets/listbrowser.h>
#include <images/glyph.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/utility.h>
#include <proto/listbrowser.h>
#include <proto/glyph.h>

#include <string.h>

#include <gadgets/dirtree.h>
#include "classlib.h"
#include "dirtree_icons.h"

/* Path buffers are allocated, not put on the stack: the class runs on the
 * application's stack, often only 4 KB, while the listbrowser renders. */
#define DIRTREE_PATH_MAX            1024
#define DIRTREE_DEFAULT_DEPTH       16
#define DIRTREE_DEFAULT_ENTRIES     2048
/* A directory needs room for itself and its placeholder. */
#define DIRTREE_MIN_ENTRIES         3
/* Layout of an entry drawn by the render hook (pixels). */
#define ROW_PADDING                 2   /* above and below the contents */
#define LEFT_PADDING                3   /* left of the symbol */
#define TEXT_GAP                    5   /* between symbol and name */
/* The listbrowser indents the levels itself; an entry without children
 * starts this much further right than a directory of the same level (the
 * room of the expand image), measured with listbrowser.gadget 47 and 1 x 1
 * expand images. Directories are moved by it so that all symbols of a
 * level line up. */
#define LEAF_OFFSET                 11

struct Library *ListBrowserBase = NULL;
/* Optional: without glyph.image the listbrowser has no expand arrows
 * unless the application passes its own images. */
struct Library *GlyphBase = NULL;

/** One tree item; allocated in one block with its path and name. */
typedef struct TreeEntry {
    int directory;          /**< non-zero for a directory */
    int placeholder;        /**< non-zero for a status placeholder */
    int expanded;           /**< directory children are loaded */
    int kind;               /**< DTI_ symbol, -1 for none */
    char *name;             /**< display name, stored after the path */
    char path[1];           /**< full path, NUL terminated */
} TreeEntry;

/** Instance data of a dirtree.gadget object. */
typedef struct DirTreeData {
    struct List nodes;          /**< listbrowser nodes owned by the object */
    char *root;                 /**< full path of the root, or NULL */
    char *active_path;          /**< file activated by the last event */
    const char *loading_text;   /**< placeholder of unread directories */
    const char *empty_text;     /**< placeholder of empty directories */
    const char *unavailable_text; /**< placeholder of unreadable ones */
    Object *show_image;         /**< default expand glyph (owned) or NULL */
    Object *hide_image;         /**< default collapse glyph (owned) or NULL */
    ULONG max_depth;            /**< deepest generation that is read */
    ULONG max_entries;          /**< upper bound of list nodes */
    ULONG status;               /**< DIRTREE_ST_# of the last operation */
    struct ColumnInfo columns[2]; /**< one column, its title is the root's name */
    char *title;                /**< name of the root directory, or NULL */
    Object *icons[DTI_COUNT];   /**< entry symbols (shared by the nodes) or NULL */
    IconPens pens;              /**< their colours on the screen */
    int hide_icons;             /**< leave out .info files */
    struct Hook render_hook;    /**< draws symbol and name of every entry */
    WORD row_height;            /**< LBNCA_HookHeight of new entries */
    ULONG event;                /**< DIRTREE_EV_# of the last HandleEvent */
} DirTreeData;

/* -- libraries ------------------------------------------------------------ */

/**
 * @brief Open listbrowser.gadget (required) and glyph.image (optional).
 * @return TRUE if listbrowser.gadget is available.
 */
BOOL class_open_libraries(void)
{
    ListBrowserBase = OpenLibrary("gadgets/listbrowser.gadget", 44);
    if (ListBrowserBase == NULL) return FALSE;
    if (!dirtree_icons_init()) {
        CloseLibrary(ListBrowserBase);
        ListBrowserBase = NULL;
        return FALSE;
    }
    GlyphBase = OpenLibrary("images/glyph.image", 44);
    return TRUE;
}

/** @brief Close the libraries opened by class_open_libraries(). */
void class_close_libraries(void)
{
    dirtree_icons_exit();
    if (GlyphBase != NULL) CloseLibrary(GlyphBase);
    if (ListBrowserBase != NULL) CloseLibrary(ListBrowserBase);
    GlyphBase = NULL;
    ListBrowserBase = NULL;
}

/* -- nodes ---------------------------------------------------------------- */

/**
 * @brief Duplicate a string with AllocVec().
 * @param value The string to copy.
 * @return The copy (free with FreeVec()), or NULL without memory.
 */
static char *dup_string(const char *value)
{
    size_t length = strlen(value) + 1;
    char *copy = AllocVec((ULONG)length, MEMF_ANY);
    if (copy != NULL) memcpy(copy, value, length);
    return copy;
}

/**
 * @brief Allocate a TreeEntry with its path and name in one block.
 * @param name Display name.
 * @param path Full path ("" for placeholders).
 * @param directory Non-zero for a directory.
 * @param placeholder Non-zero for a placeholder.
 * @return The entry (free with FreeVec()), or NULL without memory.
 */
static TreeEntry *new_entry(const char *name, const char *path,
                            int directory, int placeholder)
{
    size_t path_length = strlen(path) + 1;
    size_t name_length = strlen(name) + 1;
    TreeEntry *entry = AllocVec((ULONG)(sizeof(*entry) + path_length + name_length),
                                MEMF_ANY | MEMF_CLEAR);
    if (entry != NULL) {
        entry->directory = directory;
        entry->placeholder = placeholder;
        memcpy(entry->path, path, path_length);
        entry->name = entry->path + path_length;
        memcpy(entry->name, name, name_length);
    }
    return entry;
}

/**
 * @brief Create a listbrowser node that owns a new TreeEntry.
 * @param d The instance data (symbols).
 * @param name Display name.
 * @param path Full path.
 * @param generation Tree depth of the node (first level = 1).
 * @param directory Non-zero for a directory (gets LBFLG_HASCHILDREN).
 * @param placeholder Non-zero for a placeholder.
 * @param kind DTI_ symbol of the entry, -1 for none.
 * @return The node, or NULL without memory.
 */
static struct Node *new_node(DirTreeData *d, const char *name, const char *path,
                             ULONG generation, int directory, int placeholder, int kind)
{
    TreeEntry *entry = new_entry(name, path, directory, placeholder);
    struct Node *node;
    if (entry == NULL) return NULL;
    entry->kind = kind;
    /* A column holds text or an image, not both; the render hook draws
     * symbol and name. The text stays for the listbrowser's own use. */
    node = AllocListBrowserNode(1,
        LBNA_Generation, generation,
        LBNA_Flags, directory ? LBFLG_HASCHILDREN : 0,
        LBNA_UserData, (ULONG)entry,
        LBNA_Column, 0,
        LBNCA_Text, (ULONG)entry->name,
        LBNCA_RenderHook, (ULONG)&d->render_hook,
        LBNCA_HookHeight, (ULONG)d->row_height,
        TAG_END);
    if (node == NULL) FreeVec(entry);
    return node;
}

/**
 * @brief Return the TreeEntry of a node.
 * @param node The node.
 * @return Its entry, or NULL.
 */
static TreeEntry *node_entry(struct Node *node)
{
    TreeEntry *entry = NULL;
    GetListBrowserNodeAttrs(node, LBNA_UserData, (ULONG)&entry, TAG_END);
    return entry;
}

/**
 * @brief Return the generation (tree depth) of a node.
 * @param node The node.
 * @return Its generation.
 */
static ULONG node_generation(struct Node *node)
{
    ULONG generation = 0;
    GetListBrowserNodeAttrs(node, LBNA_Generation, (ULONG)&generation, TAG_END);
    return generation;
}

/**
 * @brief Unlink a node and free it with its TreeEntry.
 * @param node The node.
 */
static void free_node(struct Node *node)
{
    TreeEntry *entry = node_entry(node);
    Remove(node);
    FreeListBrowserNode(node);
    if (entry != NULL) FreeVec(entry);
}

/**
 * @brief Free every node of the tree.
 * @param d The instance data.
 */
static void free_all(DirTreeData *d)
{
    struct Node *node;
    while ((node = d->nodes.lh_Head)->ln_Succ != NULL) free_node(node);
}

/**
 * @brief Free all descendants of a node (the nodes after it with a
 *        larger generation).
 * @param parent The node whose subtree is pruned.
 */
static void remove_descendants(struct Node *parent)
{
    ULONG generation = node_generation(parent);
    struct Node *node = parent->ln_Succ;
    while (node->ln_Succ != NULL && node_generation(node) > generation) {
        struct Node *next = node->ln_Succ;
        free_node(node);
        node = next;
    }
}

/**
 * @brief Count the nodes of the tree.
 * @param d The instance data.
 * @return The number of nodes.
 */
static ULONG node_count(DirTreeData *d)
{
    ULONG count = 0;
    struct Node *node;
    for (node = d->nodes.lh_Head; node->ln_Succ != NULL; node = node->ln_Succ)
        ++count;
    return count;
}

/**
 * @brief Decide whether a new child sorts before an existing entry:
 *        directories first, then case-insensitive, then case-sensitive.
 * @param name Name of the new child.
 * @param directory Non-zero if the new child is a directory.
 * @param other The existing entry.
 * @return Non-zero if the new child goes before @p other.
 */
static int sorts_before(const char *name, int directory, const TreeEntry *other)
{
    LONG order;
    if (directory != other->directory) return directory > other->directory;
    order = Stricmp((STRPTR)name, (STRPTR)other->name);
    if (order != 0) return order < 0;
    return strcmp(name, other->name) < 0;
}

/**
 * @brief Find the node after which a new child of @p parent is inserted
 *        to keep the children sorted (placeholders are skipped).
 * @param d The instance data.
 * @param parent The parent node, NULL for the first level.
 * @param parent_generation Its generation (0 for the root).
 * @param name Name of the new child.
 * @param directory Non-zero if the new child is a directory.
 * @return The predecessor for Insert().
 */
static struct Node *sorted_predecessor(DirTreeData *d, struct Node *parent,
                                       ULONG parent_generation, const char *name,
                                       int directory)
{
    ULONG child_generation = parent_generation + 1;
    struct Node *node = parent != NULL ? parent->ln_Succ : d->nodes.lh_Head;
    while (node->ln_Succ != NULL && node_generation(node) > parent_generation) {
        if (node_generation(node) == child_generation) {
            TreeEntry *other = node_entry(node);
            if (other != NULL && !other->placeholder &&
                sorts_before(name, directory, other))
                return node->ln_Pred;
        }
        node = node->ln_Succ;
    }
    return node->ln_Pred;
}

/**
 * @brief Insert a placeholder child after @p *after.
 * @param d The instance data.
 * @param parent_generation Generation of its parent (0 for the root).
 * @param after In: insert position; out: the new placeholder.
 * @param label Placeholder text.
 * @param hidden Non-zero to create it hidden (LBFLG_HIDDEN).
 * @return Non-zero on success, 0 without memory.
 */
static int insert_placeholder(DirTreeData *d, ULONG parent_generation,
                              struct Node **after, const char *label, int hidden)
{
    struct Node *node = new_node(d, label, "", parent_generation + 1, 0, 1, -1);
    if (node == NULL) return 0;
    if (hidden) SetListBrowserNodeAttrs(node, LBNA_Flags, LBFLG_HIDDEN, TAG_END);
    Insert(&d->nodes, node, *after);
    *after = node;
    return 1;
}

/**
 * @brief Report whether a file is the icon of another one (*.info).
 * @param name The file name.
 * @return Non-zero for an icon.
 */
static int is_icon(const char *name)
{
    size_t length = strlen(name);
    return length > 5 && Stricmp((STRPTR)(name + length - 5), (STRPTR)".info") == 0;
}

/**
 * @brief Read the direct children of a directory into the list.
 *
 * Every subdirectory gets a hidden placeholder so it can be expanded
 * later. Stops at DIRTREE_MaxEntries; does nothing below MaxDepth. A
 * directory without children gets an "(empty)" or "(unavailable)"
 * placeholder. A parent node is flagged to show its children and gets the
 * open drawer symbol; without a parent the children form the first level
 * below the title.
 *
 * @param d The instance data.
 * @param parent The directory node, NULL for the root.
 * @param path The directory's full path.
 * @param generation The directory's generation (0 for the root).
 * @return Non-zero if the directory was read completely.
 */
static int load_children(DirTreeData *d, struct Node *parent, const char *path,
                         ULONG generation)
{
    ULONG count = node_count(d);
    struct Node *after = parent != NULL ? parent : (struct Node *)&d->nodes;
    struct FileInfoBlock *fib = NULL;
    char *child_path = NULL;
    BPTR lock = 0;
    int ok = 1, children = 0;
    if (generation >= d->max_depth) return 1;
    lock = Lock((STRPTR)path, ACCESS_READ);
    fib = AllocDosObject(DOS_FIB, NULL);
    child_path = AllocVec(DIRTREE_PATH_MAX, MEMF_ANY);
    if (lock == 0 || fib == NULL || child_path == NULL || !Examine(lock, fib) ||
        fib->fib_DirEntryType <= 0) {
        ok = 0;
        goto done;
    }
    while (ExNext(lock, fib)) {
        struct Node *child;
        int directory = fib->fib_DirEntryType > 0;
        if (!directory && d->hide_icons && is_icon(fib->fib_FileName)) continue;
        if (count + (directory ? 2UL : 1UL) > d->max_entries) { ok = 0; break; }
        strncpy(child_path, path, DIRTREE_PATH_MAX - 1);
        child_path[DIRTREE_PATH_MAX - 1] = '\0';
        if (!AddPart(child_path, fib->fib_FileName, DIRTREE_PATH_MAX)) {
            ok = 0;
            continue;
        }
        child = new_node(d, fib->fib_FileName, child_path, generation + 1, directory, 0,
                         directory ? DTI_DRAWER
                                   : dirtree_icon_kind(fib->fib_FileName,
                                                       (ULONG)fib->fib_Protection));
        if (child == NULL) { ok = 0; break; }
        after = sorted_predecessor(d, parent, generation, fib->fib_FileName, directory);
        Insert(&d->nodes, child, after);
        after = child;
        ++count;
        ++children;
        if (directory) {
            if (!insert_placeholder(d, generation + 1, &after, d->loading_text, 1)) {
                ok = 0;
                break;
            }
            HideListBrowserNodeChildren(child);
            ++count;
        }
    }
    /* ExNext() ends with ERROR_NO_MORE_ENTRIES; anything else is a read error. */
    if (ok && IoErr() != ERROR_NO_MORE_ENTRIES) ok = 0;
done:
    if (child_path != NULL) FreeVec(child_path);
    if (fib != NULL) FreeDosObject(DOS_FIB, fib);
    if (lock != 0) UnLock(lock);
    if (children == 0) {
        after = parent != NULL ? parent : (struct Node *)&d->nodes;
        if (!insert_placeholder(d, generation, &after,
                                ok ? d->empty_text : d->unavailable_text, 0)) ok = 0;
    }
    if (parent != NULL) {
        TreeEntry *pe = node_entry(parent);
        if (pe != NULL) pe->kind = DTI_DRAWER_OPEN;
        SetListBrowserNodeAttrs(parent, LBNA_Flags, LBFLG_HASCHILDREN | LBFLG_SHOWCHILDREN,
                                TAG_END);
        ShowListBrowserNodeChildren(parent, 1);
    }
    return ok;
}

/* -- superclass access ---------------------------------------------------- */

/**
 * @brief Set LISTBROWSER_Labels on the superclass (~0 detaches the list).
 * @param cl The class.
 * @param o The object.
 * @param gi GadgetInfo for an immediate refresh, or NULL.
 * @param labels The list, or (struct List *)~0.
 */
static void set_labels(Class *cl, Object *o, struct GadgetInfo *gi,
                       struct List *labels)
{
    struct TagItem tags[2];
    struct opSet set;
    tags[0].ti_Tag = LISTBROWSER_Labels;
    tags[0].ti_Data = (ULONG)labels;
    tags[1].ti_Tag = TAG_END;
    tags[1].ti_Data = 0;
    set.MethodID = OM_SET;
    set.ops_AttrList = tags;
    set.ops_GInfo = gi;
    class_dsm(cl, o, (Msg)&set);
}

/**
 * @brief Give the column the root's name as title (while the list is
 *        detached).
 * @param cl The class.
 * @param o The object.
 * @param d The instance data.
 * @param gi GadgetInfo, or NULL.
 */
static void set_title(Class *cl, Object *o, DirTreeData *d, struct GadgetInfo *gi)
{
    struct TagItem tags[2];
    struct opSet set;
    d->columns[0].ci_Title = d->title != NULL ? (STRPTR)d->title : (STRPTR)"";
    tags[0].ti_Tag = LISTBROWSER_ColumnInfo;
    tags[0].ti_Data = (ULONG)d->columns;
    tags[1].ti_Tag = TAG_END;
    tags[1].ti_Data = 0;
    set.MethodID = OM_SET;
    set.ops_AttrList = tags;
    set.ops_GInfo = gi;
    class_dsm(cl, o, (Msg)&set);
}

/**
 * @brief Report whether a node is (still) part of the tree.
 * @param d The instance data.
 * @param node The node.
 * @return Non-zero if the node is in the list.
 */
static int node_in_tree(DirTreeData *d, struct Node *node)
{
    struct Node *n;
    for (n = d->nodes.lh_Head; n->ln_Succ != NULL; n = n->ln_Succ)
        if (n == node) return 1;
    return 0;
}

/**
 * @brief Select a node in the listbrowser (after the list was re-attached,
 *        which resets the selection).
 * @param cl The class.
 * @param o The object.
 * @param gi GadgetInfo, or NULL.
 * @param node The node to select.
 */
static void set_selected(Class *cl, Object *o, struct GadgetInfo *gi, struct Node *node)
{
    struct TagItem tags[2];
    struct opSet set;
    tags[0].ti_Tag = LISTBROWSER_SelectedNode;
    tags[0].ti_Data = (ULONG)node;
    tags[1].ti_Tag = TAG_END;
    tags[1].ti_Data = 0;
    set.MethodID = OM_SET;
    set.ops_AttrList = tags;
    set.ops_GInfo = gi;
    class_dsm(cl, o, (Msg)&set);
}

/**
 * @brief Read an attribute of the superclass.
 * @param cl The class.
 * @param o The object.
 * @param attr The attribute.
 * @return Its value (0 if unknown).
 */
static ULONG super_get(Class *cl, Object *o, ULONG attr)
{
    ULONG value = 0;
    struct opGet get;
    get.MethodID = OM_GET;
    get.opg_AttrID = attr;
    get.opg_Storage = &value;
    class_dsm(cl, o, (Msg)&get);
    return value;
}

/**
 * @brief Redraw the object after OM_SET/OM_UPDATE.
 *
 * BOOPSI convention: a class redraws itself in OM_SET/OM_UPDATE only when
 * the object is exactly of that class; for a subclass object
 * listbrowser.gadget only returns non-zero and leaves rendering to the
 * subclass. Without this the tree was not redrawn after expanding a
 * directory or scrolling. A further subclass of dirtree.gadget gets the
 * same treatment from us and must render itself.
 *
 * @param cl The class.
 * @param o The object.
 * @param gi GadgetInfo of the message, NULL when not in a window.
 * @param mode GREDRAW_REDRAW or GREDRAW_UPDATE.
 */
static void render_self(Class *cl, Object *o, struct GadgetInfo *gi, ULONG mode)
{
    struct RastPort *rp;
    struct gpRender render;
    if (gi == NULL || OCLASS(o) != cl) return;
    rp = ObtainGIRPort(gi);
    if (rp == NULL) return;
    render.MethodID = GM_RENDER;
    render.gpr_GInfo = gi;
    render.gpr_RPort = rp;
    render.gpr_Redraw = (LONG)mode;
    class_dm(o, (Msg)&render);
    ReleaseGIRPort(rp);
}

/**
 * @brief Render hook of every entry (LBNCA_RenderHook): the background
 *        (FILLPEN when selected), the symbol and the name, cut to the
 *        cell's width.
 *
 * Runs while the listbrowser renders, possibly during input handling:
 * graphics only.
 *
 * @param hook The hook, h_Data is the instance data.
 * @param node The node of the entry.
 * @param msg The draw message.
 * @return LBCB_OK, or LBCB_UNKNOWN for other methods.
 */
static ULONG render_entry(struct Hook *hook __asm("a0"), struct Node *node __asm("a2"),
                          struct LBDrawMsg *msg __asm("a1"))
{
    DirTreeData *d = (DirTreeData *)hook->h_Data;
    struct RastPort *rp = msg->lbdm_RastPort;
    struct DrawInfo *dri = msg->lbdm_DrawInfo;
    struct Rectangle *b = &msg->lbdm_Bounds;
    TreeEntry *entry = node_entry(node);
    int selected = msg->lbdm_State == LBR_SELECTED;
    WORD x, height = (WORD)(b->MaxY - b->MinY + 1);
    UWORD bg, fg;
    struct TextExtent te;
    ULONG length, fit;

    if (msg->lbdm_MethodID != LV_DRAW) return LBCB_UNKNOWN;
    if (dri == NULL || entry == NULL) return LBCB_OK;
    x = (WORD)(b->MinX + LEFT_PADDING);
    if (entry->directory && d->icons[DTI_DRAWER] != NULL) x = (WORD)(x + LEAF_OFFSET);
    bg = dri->dri_Pens[selected ? FILLPEN : BACKGROUNDPEN];
    fg = dri->dri_Pens[selected ? FILLTEXTPEN : entry->placeholder ? SHADOWPEN : TEXTPEN];
    SetAPen(rp, (UBYTE)bg);
    RectFill(rp, b->MinX, b->MinY, b->MaxX, b->MaxY);

    if (entry->kind >= 0 && d->icons[entry->kind] != NULL) {
        DrawImageState(rp, (struct Image *)d->icons[entry->kind], x,
                       (WORD)(b->MinY + (height - dirtree_icon_height()) / 2),
                       selected ? IDS_SELECTED : IDS_NORMAL, dri);
        x = (WORD)(x + dirtree_icon_width() + TEXT_GAP);
    }
    if (x > b->MaxX) return LBCB_OK;
    length = strlen(entry->name);
    fit = TextFit(rp, (STRPTR)entry->name, (UWORD)length, &te, NULL, 1,
                  (UWORD)(b->MaxX - x + 1), (UWORD)height);
    SetAPen(rp, (UBYTE)fg);
    SetDrMd(rp, JAM1);
    Move(rp, x, (WORD)(b->MinY + (height - rp->TxHeight) / 2 + rp->TxBaseline));
    Text(rp, (STRPTR)entry->name, (UWORD)fit);
    return LBCB_OK;
}

/**
 * @brief Height of new rows: the symbol or the screen font, whichever is
 *        higher, plus the padding above and below.
 * @param d The instance data.
 * @param gi GadgetInfo (its DrawInfo font), or NULL to keep the value.
 */
static void update_row_height(DirTreeData *d, struct GadgetInfo *gi)
{
    WORD h;
    if (gi == NULL || gi->gi_DrInfo == NULL || gi->gi_DrInfo->dri_Font == NULL) return;
    h = (WORD)gi->gi_DrInfo->dri_Font->tf_YSize;
    if (d->icons[DTI_DRAWER] != NULL && h < dirtree_icon_height()) h = dirtree_icon_height();
    d->row_height = (WORD)(h + 2 * ROW_PADDING);
}

/* -- operations ----------------------------------------------------------- */

/**
 * @brief Replace the tree with a new root directory.
 *
 * The directory is checked first; if it cannot be read the old tree is
 * kept. Otherwise the list is detached, the column title becomes the
 * directory's name and the list is rebuilt with its first level.
 *
 * @param cl The class.
 * @param o The object.
 * @param d The instance data.
 * @param gi GadgetInfo for the refresh, or NULL.
 * @param path The new root, or NULL to empty the tree.
 * @return The DIRTREE_ST_# result.
 */
static ULONG load_root(Class *cl, Object *o, DirTreeData *d,
                       struct GadgetInfo *gi, const char *path)
{
    BPTR lock;
    struct FileInfoBlock *fib;
    char *canonical;
    ULONG status = DIRTREE_ST_FAILED;
    int is_dir = 0;

    if (path == NULL) {
        set_labels(cl, o, gi, (struct List *)~0UL);
        free_all(d);
        if (d->root != NULL) { FreeVec(d->root); d->root = NULL; }
        if (d->title != NULL) { FreeVec(d->title); d->title = NULL; }
        if (d->active_path != NULL) { FreeVec(d->active_path); d->active_path = NULL; }
        set_title(cl, o, d, gi);
        set_labels(cl, o, gi, &d->nodes);
        return DIRTREE_ST_OK;
    }

    lock = Lock((STRPTR)path, ACCESS_READ);
    fib = AllocDosObject(DOS_FIB, NULL);
    canonical = AllocVec(DIRTREE_PATH_MAX, MEMF_ANY);
    if (lock != 0 && fib != NULL && canonical != NULL && Examine(lock, fib) &&
        fib->fib_DirEntryType > 0 && NameFromLock(lock, canonical, DIRTREE_PATH_MAX))
        is_dir = 1;
    if (is_dir) {
        /* the title: the directory's name, or "Volume:" for a volume */
        const char *name = (const char *)FilePart((STRPTR)canonical);
        char *copy = dup_string(canonical);
        char *title = dup_string(*name != '\0' ? name : canonical);
        if (copy != NULL && title != NULL) {
            set_labels(cl, o, gi, (struct List *)~0UL);
            free_all(d);
            if (d->root != NULL) FreeVec(d->root);
            if (d->title != NULL) FreeVec(d->title);
            d->root = copy;
            d->title = title;
            if (d->active_path != NULL) { FreeVec(d->active_path); d->active_path = NULL; }
            status = load_children(d, NULL, d->root, 0) ? DIRTREE_ST_OK : DIRTREE_ST_INCOMPLETE;
            set_title(cl, o, d, gi);
            set_labels(cl, o, gi, &d->nodes);
        } else {
            if (copy != NULL) FreeVec(copy);
            if (title != NULL) FreeVec(title);
        }
    }
    if (canonical != NULL) FreeVec(canonical);
    if (fib != NULL) FreeDosObject(DOS_FIB, fib);
    if (lock != 0) UnLock(lock);
    return status;
}

/**
 * @brief Expand a directory node: (re)read its children and show them.
 *        The caller detaches and re-attaches the list.
 * @param d The instance data.
 * @param node The directory node.
 * @param entry Its entry.
 * @return The DIRTREE_ST_# result.
 */
static ULONG expand_node(DirTreeData *d, struct Node *node, TreeEntry *entry)
{
    ULONG status;
    remove_descendants(node);
    status = load_children(d, node, entry->path, node_generation(node))
             ? DIRTREE_ST_OK : DIRTREE_ST_INCOMPLETE;
    entry->expanded = 1;
    return status;
}

/**
 * @brief Collapse a directory node: free its children and put the
 *        "Loading..." placeholder back so it stays expandable. The caller
 *        detaches and re-attaches the list.
 * @param d The instance data.
 * @param node The directory node.
 * @param entry Its entry.
 * @return The DIRTREE_ST_# result (FAILED without memory).
 */
static ULONG collapse_node(DirTreeData *d, struct Node *node, TreeEntry *entry)
{
    struct Node *after = node;
    ULONG status = DIRTREE_ST_OK;
    remove_descendants(node);
    if (!insert_placeholder(d, node_generation(node), &after, d->loading_text, 1))
        status = DIRTREE_ST_FAILED;
    SetListBrowserNodeAttrs(node, LBNA_Flags, LBFLG_HASCHILDREN, TAG_END);
    entry->kind = DTI_DRAWER;
    HideListBrowserNodeChildren(node);
    entry->expanded = 0;
    return status;
}

/**
 * @brief Keep the worse of two DIRTREE_ST_# results.
 * @param a First status.
 * @param b Second status.
 * @return The larger (worse) status.
 */
static ULONG worse_status(ULONG a, ULONG b)
{
    return a > b ? a : b;
}

/**
 * @brief DIRTREE_HandleEvent: act on what the listbrowser did.
 *
 * Opening or closing a directory with its arrow only toggles
 * LBFLG_SHOWCHILDREN of that node inside the listbrowser (which then shows
 * the hidden placeholder). Instead of relying on the event and cursor
 * node, every directory whose flag no longer matches its loaded state is
 * expanded (children read) or collapsed (children freed). A double-click
 * toggles a directory or activates a file.
 *
 * @param cl The class.
 * @param o The object.
 * @param d The instance data.
 * @param gi GadgetInfo of the OM_SET, for redrawing.
 * @param event LISTBROWSER_RelEvent, read before the OM_SET reached the
 *        listbrowser.
 * @param cursor The cursor (or selected) node at that time, or NULL.
 * @return A DIRTREE_EV_# value.
 */
static ULONG handle_event(Class *cl, Object *o, DirTreeData *d,
                          struct GadgetInfo *gi, ULONG event, struct Node *cursor)
{
    struct Node *node, *next;
    struct Node *selected = (struct Node *)super_get(cl, o, LISTBROWSER_SelectedNode);
    TreeEntry *entry;
    ULONG result = DIRTREE_EV_NONE;
    int detached = 0;

    if (d->active_path != NULL) { FreeVec(d->active_path); d->active_path = NULL; }
    d->status = DIRTREE_ST_OK;

    for (node = d->nodes.lh_Head; (next = node->ln_Succ) != NULL; node = next) {
        ULONG flags = 0;
        int shown;
        entry = node_entry(node);
        if (entry == NULL || !entry->directory) continue;
        GetListBrowserNodeAttrs(node, LBNA_Flags, (ULONG)&flags, TAG_END);
        shown = (flags & LBFLG_SHOWCHILDREN) != 0;
        if (shown == entry->expanded) continue;
        if (!detached) { set_labels(cl, o, gi, (struct List *)~0UL); detached = 1; }
        if (shown) {
            d->status = worse_status(d->status, expand_node(d, node, entry));
            result = DIRTREE_EV_EXPANDED;
        } else {
            d->status = worse_status(d->status, collapse_node(d, node, entry));
            result = DIRTREE_EV_COLLAPSED;
        }
        /* Continue after the (new) children: freshly read directories are
         * collapsed, a placeholder is not a directory. */
        next = node->ln_Succ;
    }

    if (result == DIRTREE_EV_NONE && (event & LBRE_DOUBLECLICK) != 0 && cursor != NULL &&
        (entry = node_entry(cursor)) != NULL && !entry->placeholder) {
        if (entry->directory) {
            if (!detached) { set_labels(cl, o, gi, (struct List *)~0UL); detached = 1; }
            if (entry->expanded) {
                d->status = collapse_node(d, cursor, entry);
                result = DIRTREE_EV_COLLAPSED;
            } else {
                d->status = expand_node(d, cursor, entry);
                result = DIRTREE_EV_EXPANDED;
            }
        } else {
            d->active_path = dup_string(entry->path);
            if (d->active_path != NULL) result = DIRTREE_EV_ACTIVATE;
            else d->status = DIRTREE_ST_FAILED;
        }
    }
    if (detached) {
        set_labels(cl, o, gi, &d->nodes);
        /* Re-attaching the list clears the selection; keep the selected
         * entry unless it was freed with a collapsed directory. */
        if (selected != NULL && node_in_tree(d, selected))
            set_selected(cl, o, gi, selected);
    }
    return result;
}

/* -- methods -------------------------------------------------------------- */

/**
 * @brief OM_NEW: create the listbrowser, then set up the tree.
 * @param cl The class.
 * @param o The class object.
 * @param ops The creation message.
 * @return The new object, or NULL.
 */
static ULONG om_new(Class *cl, Object *o, struct opSet *ops)
{
    struct TagItem extra[5];
    struct opSet super_ops;
    Object *show = NULL, *hide = NULL, *obj;
    DirTreeData *d;
    STRPTR root;
    int n = 0, images = 0;
    int symbols = (int)GetTagData(DIRTREE_Images, TRUE, ops->ops_AttrList);

    extra[n].ti_Tag = LISTBROWSER_Hierarchical;
    extra[n++].ti_Data = TRUE;
    /* listbrowser.gadget selects a clicked entry but only highlights it
     * with LISTBROWSER_ShowSelected (default FALSE); a tree needs it. */
    if (FindTagItem(LISTBROWSER_ShowSelected, ops->ops_AttrList) == NULL) {
        extra[n].ti_Tag = LISTBROWSER_ShowSelected;
        extra[n++].ti_Data = TRUE;
    }
    if (FindTagItem(LISTBROWSER_ShowImage, ops->ops_AttrList) == NULL &&
        FindTagItem(LISTBROWSER_HideImage, ops->ops_AttrList) == NULL) {
        if (symbols) {
            /* No expand arrows: the drawer symbols show what can be opened
             * (double-click), so the listbrowser gets invisible 1 x 1
             * images. */
            show = dirtree_blank_new(1, 1);
            hide = dirtree_blank_new(1, 1);
        } else if (GlyphBase != NULL) {
            show = NewObject(GLYPH_GetClass(), NULL, GLYPH_Glyph, GLYPH_RIGHTARROW, TAG_END);
            hide = NewObject(GLYPH_GetClass(), NULL, GLYPH_Glyph, GLYPH_DOWNARROW, TAG_END);
        }
        if (show != NULL && hide != NULL) {
            extra[n].ti_Tag = LISTBROWSER_ShowImage;
            extra[n++].ti_Data = (ULONG)show;
            extra[n].ti_Tag = LISTBROWSER_HideImage;
            extra[n++].ti_Data = (ULONG)hide;
            images = 1;
        }
    }
    extra[n].ti_Tag = TAG_MORE;
    extra[n].ti_Data = (ULONG)ops->ops_AttrList;

    super_ops = *ops;
    super_ops.ops_AttrList = extra;
    obj = (Object *)class_dsm(cl, o, (Msg)&super_ops);
    if (obj == NULL || !images) {
        /* Images are disposed here when they are unused or creation failed. */
        if (show != NULL) DisposeObject(show);
        if (hide != NULL) DisposeObject(hide);
        show = hide = NULL;
        if (obj == NULL) return 0;
    }

    d = INST_DATA(cl, obj);
    memset(d, 0, sizeof(*d));
    class_new_list(&d->nodes);
    d->show_image = show;
    d->hide_image = hide;
    d->max_depth = GetTagData(DIRTREE_MaxDepth, DIRTREE_DEFAULT_DEPTH, ops->ops_AttrList);
    d->max_entries = GetTagData(DIRTREE_MaxEntries, DIRTREE_DEFAULT_ENTRIES,
                                ops->ops_AttrList);
    if (d->max_entries < DIRTREE_MIN_ENTRIES) d->max_entries = DIRTREE_MIN_ENTRIES;
    d->loading_text = (const char *)GetTagData(DIRTREE_LoadingText,
                                               (ULONG)"Loading...", ops->ops_AttrList);
    d->empty_text = (const char *)GetTagData(DIRTREE_EmptyText,
                                             (ULONG)"(empty)", ops->ops_AttrList);
    d->unavailable_text = (const char *)GetTagData(DIRTREE_UnavailableText,
                                                   (ULONG)"(unavailable)",
                                                   ops->ops_AttrList);
    d->hide_icons = (int)GetTagData(DIRTREE_HideIcons, TRUE, ops->ops_AttrList);
    d->render_hook.h_Entry = (ULONG (*)())render_entry;
    d->render_hook.h_Data = d;
    if (symbols) {
        int k;
        for (k = 0; k < DTI_COUNT; ++k) d->icons[k] = dirtree_icon_new(k, &d->pens);
    }
    /* until the screen font is known (first OM_SET in a window) */
    d->row_height = (WORD)(GfxBase->DefaultFont != NULL ? GfxBase->DefaultFont->tf_YSize : 8);
    if (d->icons[DTI_DRAWER] != NULL && d->row_height < dirtree_icon_height())
        d->row_height = dirtree_icon_height();
    d->row_height = (WORD)(d->row_height + 2 * ROW_PADDING);
    /* one column; its title shows the root's name above the list */
    d->columns[0].ci_Width = 100;
    d->columns[0].ci_Title = (STRPTR)"";
    d->columns[0].ci_Flags = 0;
    d->columns[1].ci_Width = -1;
    {
        struct TagItem tags[4];
        struct opSet set;
        tags[0].ti_Tag = LISTBROWSER_ColumnInfo;   tags[0].ti_Data = (ULONG)d->columns;
        tags[1].ti_Tag = LISTBROWSER_ColumnTitles; tags[1].ti_Data = TRUE;
        tags[2].ti_Tag = LISTBROWSER_Labels;       tags[2].ti_Data = (ULONG)&d->nodes;
        tags[3].ti_Tag = TAG_END;                  tags[3].ti_Data = 0;
        set.MethodID = OM_SET;
        set.ops_AttrList = tags;
        set.ops_GInfo = NULL;
        class_dsm(cl, obj, (Msg)&set);
    }
    root = (STRPTR)GetTagData(DIRTREE_Root, 0, ops->ops_AttrList);
    d->status = root != NULL ? load_root(cl, obj, d, NULL, root) : DIRTREE_ST_OK;
    return (ULONG)obj;
}

/**
 * @brief OM_SET/OM_UPDATE: pass on to the listbrowser; OM_SET also
 *        handles DIRTREE_Root and DIRTREE_HandleEvent (never OM_UPDATE:
 *        notifications may come from input handling, where DOS is not
 *        allowed).
 * @param cl The class.
 * @param o The object.
 * @param ops The message.
 * @return The superclass result.
 */
static ULONG om_set(Class *cl, Object *o, struct opSet *ops)
{
    DirTreeData *d = INST_DATA(cl, o);
    ULONG result, event = 0;
    struct Node *cursor = NULL;
    struct TagItem *tag = NULL;
    int handle = ops->MethodID == OM_SET &&
                 GetTagData(DIRTREE_HandleEvent, FALSE, ops->ops_AttrList);
    if (ops->MethodID == OM_SET) update_row_height(d, ops->ops_GInfo);
    /* Read the listbrowser's event before the OM_SET reaches it: setting
     * attributes may reset LISTBROWSER_RelEvent. */
    if (handle) {
        event = super_get(cl, o, LISTBROWSER_RelEvent);
        cursor = (struct Node *)super_get(cl, o, LISTBROWSER_CursorNode);
        if (cursor == NULL) cursor = (struct Node *)super_get(cl, o, LISTBROWSER_SelectedNode);
    }
    result = class_dsm(cl, o, (Msg)ops);
    if (ops->MethodID == OM_SET &&
        (tag = FindTagItem(DIRTREE_Root, ops->ops_AttrList)) != NULL)
        d->status = load_root(cl, o, d, ops->ops_GInfo, (const char *)tag->ti_Data);
    if (handle) d->event = handle_event(cl, o, d, ops->ops_GInfo, event, cursor);
    /* A new root or a changed tree needs a full redraw; otherwise follow
     * the listbrowser's request (e.g. scrolling through OM_UPDATE). */
    if ((tag != NULL && ops->MethodID == OM_SET) ||
        (handle && d->event != DIRTREE_EV_NONE && d->event != DIRTREE_EV_ACTIVATE))
        render_self(cl, o, ops->ops_GInfo, GREDRAW_REDRAW);
    else if (result != 0)
        render_self(cl, o, ops->ops_GInfo,
                    ops->MethodID == OM_UPDATE ? GREDRAW_UPDATE : GREDRAW_REDRAW);
    return result;
}

/**
 * @brief OM_GET: the class's attributes, everything else from the listbrowser.
 * @param cl The class.
 * @param o The object.
 * @param opg The message.
 * @return TRUE if the attribute is known.
 */
static ULONG om_get(Class *cl, Object *o, struct opGet *opg)
{
    DirTreeData *d = INST_DATA(cl, o);
    switch (opg->opg_AttrID) {
        case DIRTREE_Root:       *opg->opg_Storage = (ULONG)d->root; return TRUE;
        case DIRTREE_MaxDepth:   *opg->opg_Storage = d->max_depth; return TRUE;
        case DIRTREE_MaxEntries: *opg->opg_Storage = d->max_entries; return TRUE;
        case DIRTREE_Status:     *opg->opg_Storage = d->status; return TRUE;
        case DIRTREE_Event:      *opg->opg_Storage = d->event; return TRUE;
        case DIRTREE_ActivePath: *opg->opg_Storage = (ULONG)d->active_path; return TRUE;
        case DIRTREE_HideIcons:  *opg->opg_Storage = (ULONG)d->hide_icons; return TRUE;
        case DIRTREE_Images:     *opg->opg_Storage = d->icons[DTI_DRAWER] != NULL; return TRUE;
        case DIRTREE_SelectedPath: {
            struct Node *node = (struct Node *)super_get(cl, o, LISTBROWSER_SelectedNode);
            TreeEntry *entry = node != NULL ? node_entry(node) : NULL;
            *opg->opg_Storage = entry != NULL && !entry->placeholder
                ? (ULONG)entry->path : 0;
            return TRUE;
        }
        default:
            return class_dsm(cl, o, (Msg)opg);
    }
}

/**
 * @brief OM_DISPOSE: detach and free the nodes, dispose the listbrowser,
 *        then the images it referenced (arrows, entry symbols) and the
 *        symbols' pens.
 * @param cl The class.
 * @param o The object.
 * @param msg The message.
 * @return The superclass result.
 */
static ULONG om_dispose(Class *cl, Object *o, Msg msg)
{
    DirTreeData *d = INST_DATA(cl, o);
    Object *show = d->show_image, *hide = d->hide_image, *icons[DTI_COUNT];
    IconPens pens = d->pens;
    ULONG result;
    int k;
    set_labels(cl, o, NULL, (struct List *)~0UL);
    free_all(d);
    if (d->root != NULL) FreeVec(d->root);
    if (d->title != NULL) FreeVec(d->title);
    if (d->active_path != NULL) FreeVec(d->active_path);
    for (k = 0; k < DTI_COUNT; ++k) icons[k] = d->icons[k];
    result = class_dsm(cl, o, msg);
    if (show != NULL) DisposeObject(show);
    if (hide != NULL) DisposeObject(hide);
    for (k = 0; k < DTI_COUNT; ++k)
        if (icons[k] != NULL) DisposeObject(icons[k]);
    dirtree_icons_release(&pens);
    return result;
}

/**
 * @brief Class dispatcher.
 * @param cl The class.
 * @param o The object (the class for OM_NEW).
 * @param msg The message.
 * @return The method's result.
 */
static ULONG dirtree_dispatcher(Class *cl __asm("a0"), Object *o __asm("a2"),
                                Msg msg __asm("a1"))
{
    switch (msg->MethodID) {
        case OM_NEW:     return om_new(cl, o, (struct opSet *)msg);
        case OM_SET:
        case OM_UPDATE:  return om_set(cl, o, (struct opSet *)msg);
        case OM_GET:     return om_get(cl, o, (struct opGet *)msg);
        case OM_DISPOSE: return om_dispose(cl, o, msg);
        case GM_RENDER: {
            /* The symbols' colours are obtained on the first rendering,
             * when the screen is known. */
            DirTreeData *d = INST_DATA(cl, o);
            struct GadgetInfo *gi = ((struct gpRender *)msg)->gpr_GInfo;
            if (!d->pens.ok && gi != NULL)
                dirtree_icons_obtain(&d->pens, gi->gi_Screen, gi->gi_DrInfo);
            return class_dsm(cl, o, msg);
        }
        default:         return class_dsm(cl, o, msg);
    }
}

/**
 * @brief Create the dirtree.gadget class as a subclass of listbrowser.gadget.
 * @param name The public class name.
 * @return The class, or NULL.
 */
Class *class_create(const char *name)
{
    Class *cl = MakeClass((STRPTR)name, NULL, LISTBROWSER_GetClass(),
                          sizeof(DirTreeData), 0);
    if (cl != NULL) cl->cl_Dispatcher.h_Entry = (ULONG (*)())dirtree_dispatcher;
    return cl;
}
