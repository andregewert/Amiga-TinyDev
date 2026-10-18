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

/* Path buffers are allocated, not put on the stack: the class runs on the
 * application's stack, often only 4 KB, while the listbrowser renders. */
#define DIRTREE_PATH_MAX            1024
#define DIRTREE_DEFAULT_DEPTH       16
#define DIRTREE_DEFAULT_ENTRIES     2048
/* A directory needs room for itself and its placeholder. */
#define DIRTREE_MIN_ENTRIES         3

struct Library *ListBrowserBase = NULL;
/* Optional: without glyph.image the listbrowser has no expand arrows
 * unless the application passes its own images. */
struct Library *GlyphBase = NULL;

/** One tree item; allocated in one block with its path and name. */
typedef struct TreeEntry {
    int directory;          /**< non-zero for a directory */
    int placeholder;        /**< non-zero for a status placeholder */
    int expanded;           /**< directory children are loaded */
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
    GlyphBase = OpenLibrary("images/glyph.image", 44);
    return TRUE;
}

/** @brief Close the libraries opened by class_open_libraries(). */
void class_close_libraries(void)
{
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
 * @param name Display name.
 * @param path Full path.
 * @param generation Tree depth of the node (root = 0).
 * @param directory Non-zero for a directory (gets LBFLG_HASCHILDREN).
 * @param placeholder Non-zero for a placeholder.
 * @return The node, or NULL without memory.
 */
static struct Node *new_node(const char *name, const char *path,
                             ULONG generation, int directory, int placeholder)
{
    TreeEntry *entry = new_entry(name, path, directory, placeholder);
    struct Node *node;
    if (entry == NULL) return NULL;
    node = AllocListBrowserNode(1,
        LBNA_Generation, generation,
        LBNA_Flags, directory ? LBFLG_HASCHILDREN : 0,
        LBNA_UserData, (ULONG)entry,
        LBNA_Column, 0,
        LBNCA_Text, (ULONG)entry->name,
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
 * @param parent The parent node.
 * @param name Name of the new child.
 * @param directory Non-zero if the new child is a directory.
 * @return The predecessor for Insert().
 */
static struct Node *sorted_predecessor(struct Node *parent, const char *name,
                                       int directory)
{
    ULONG parent_generation = node_generation(parent);
    ULONG child_generation = parent_generation + 1;
    struct Node *node = parent->ln_Succ;
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
 * @brief Insert a placeholder child of @p parent after @p *after.
 * @param d The instance data.
 * @param parent The parent node.
 * @param after In: insert position; out: the new placeholder.
 * @param label Placeholder text.
 * @param hidden Non-zero to create it hidden (LBFLG_HIDDEN).
 * @return Non-zero on success, 0 without memory.
 */
static int insert_placeholder(DirTreeData *d, struct Node *parent,
                              struct Node **after, const char *label, int hidden)
{
    struct Node *node = new_node(label, "", node_generation(parent) + 1, 0, 1);
    if (node == NULL) return 0;
    if (hidden) SetListBrowserNodeAttrs(node, LBNA_Flags, LBFLG_HIDDEN, TAG_END);
    Insert(&d->nodes, node, *after);
    *after = node;
    return 1;
}

/**
 * @brief Read the direct children of a directory node into the list.
 *
 * Every subdirectory gets a hidden placeholder so it can be expanded
 * later. Stops at DIRTREE_MaxEntries; does nothing below MaxDepth. A
 * directory without children gets an "(empty)" or "(unavailable)"
 * placeholder. The parent is flagged to show its children.
 *
 * @param d The instance data.
 * @param parent The directory node.
 * @return Non-zero if the directory was read completely.
 */
static int load_children(DirTreeData *d, struct Node *parent)
{
    TreeEntry *entry = node_entry(parent);
    ULONG generation = node_generation(parent), count = node_count(d);
    struct Node *after = parent;
    struct FileInfoBlock *fib = NULL;
    char *child_path = NULL;
    BPTR lock = 0;
    int ok = 1, children = 0;
    if (entry == NULL || !entry->directory || generation >= d->max_depth) return 1;
    lock = Lock(entry->path, ACCESS_READ);
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
        if (count + (directory ? 2UL : 1UL) > d->max_entries) { ok = 0; break; }
        strncpy(child_path, entry->path, DIRTREE_PATH_MAX - 1);
        child_path[DIRTREE_PATH_MAX - 1] = '\0';
        if (!AddPart(child_path, fib->fib_FileName, DIRTREE_PATH_MAX)) {
            ok = 0;
            continue;
        }
        child = new_node(fib->fib_FileName, child_path, generation + 1, directory, 0);
        if (child == NULL) { ok = 0; break; }
        after = sorted_predecessor(parent, fib->fib_FileName, directory);
        Insert(&d->nodes, child, after);
        after = child;
        ++count;
        ++children;
        if (directory) {
            if (!insert_placeholder(d, child, &after, d->loading_text, 1)) {
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
        after = parent;
        if (!insert_placeholder(d, parent, &after,
                                ok ? d->empty_text : d->unavailable_text, 0)) ok = 0;
    }
    SetListBrowserNodeAttrs(parent, LBNA_Flags,
        LBFLG_HASCHILDREN | LBFLG_SHOWCHILDREN, TAG_END);
    ShowListBrowserNodeChildren(parent, 1);
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

/* -- operations ----------------------------------------------------------- */

/**
 * @brief Replace the tree with a new root directory.
 *
 * The directory is checked first; if it cannot be read the old tree is
 * kept. Otherwise the list is detached, rebuilt with the root and its
 * first level, and attached again.
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
    struct Node *root;
    ULONG status = DIRTREE_ST_FAILED;
    int is_dir = 0;

    if (path == NULL) {
        set_labels(cl, o, gi, (struct List *)~0UL);
        free_all(d);
        if (d->root != NULL) { FreeVec(d->root); d->root = NULL; }
        if (d->active_path != NULL) { FreeVec(d->active_path); d->active_path = NULL; }
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
        char *copy = dup_string(canonical);
        root = new_node(fib->fib_FileName, canonical, 0, 1, 0);
        if (copy != NULL && root != NULL) {
            TreeEntry *entry = node_entry(root);
            set_labels(cl, o, gi, (struct List *)~0UL);
            free_all(d);
            if (d->root != NULL) FreeVec(d->root);
            d->root = copy;
            if (d->active_path != NULL) { FreeVec(d->active_path); d->active_path = NULL; }
            AddTail(&d->nodes, root);
            status = load_children(d, root) ? DIRTREE_ST_OK : DIRTREE_ST_INCOMPLETE;
            if (entry != NULL) entry->expanded = 1;
            set_labels(cl, o, gi, &d->nodes);
        } else {
            if (copy != NULL) FreeVec(copy);
            if (root != NULL) {
                TreeEntry *entry = node_entry(root);
                FreeListBrowserNode(root);
                if (entry != NULL) FreeVec(entry);
            }
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
    status = load_children(d, node) ? DIRTREE_ST_OK : DIRTREE_ST_INCOMPLETE;
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
    if (!insert_placeholder(d, node, &after, d->loading_text, 1))
        status = DIRTREE_ST_FAILED;
    SetListBrowserNodeAttrs(node, LBNA_Flags, LBFLG_HASCHILDREN, TAG_END);
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
    if (detached) set_labels(cl, o, gi, &d->nodes);
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
    struct TagItem extra[4];
    struct opSet super_ops;
    Object *show = NULL, *hide = NULL, *obj;
    DirTreeData *d;
    STRPTR root;
    int n = 0;

    extra[n].ti_Tag = LISTBROWSER_Hierarchical;
    extra[n++].ti_Data = TRUE;
    if (GlyphBase != NULL &&
        FindTagItem(LISTBROWSER_ShowImage, ops->ops_AttrList) == NULL &&
        FindTagItem(LISTBROWSER_HideImage, ops->ops_AttrList) == NULL) {
        show = NewObject(GLYPH_GetClass(), NULL, GLYPH_Glyph, GLYPH_RIGHTARROW, TAG_END);
        hide = NewObject(GLYPH_GetClass(), NULL, GLYPH_Glyph, GLYPH_DOWNARROW, TAG_END);
        if (show != NULL && hide != NULL) {
            extra[n].ti_Tag = LISTBROWSER_ShowImage;
            extra[n++].ti_Data = (ULONG)show;
            extra[n].ti_Tag = LISTBROWSER_HideImage;
            extra[n++].ti_Data = (ULONG)hide;
        }
    }
    extra[n].ti_Tag = TAG_MORE;
    extra[n].ti_Data = (ULONG)ops->ops_AttrList;

    super_ops = *ops;
    super_ops.ops_AttrList = extra;
    obj = (Object *)class_dsm(cl, o, (Msg)&super_ops);
    if (obj == NULL || n < 3) {
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
    set_labels(cl, obj, NULL, &d->nodes);
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
 *        then the default images it referenced.
 * @param cl The class.
 * @param o The object.
 * @param msg The message.
 * @return The superclass result.
 */
static ULONG om_dispose(Class *cl, Object *o, Msg msg)
{
    DirTreeData *d = INST_DATA(cl, o);
    Object *show = d->show_image, *hide = d->hide_image;
    ULONG result;
    set_labels(cl, o, NULL, (struct List *)~0UL);
    free_all(d);
    if (d->root != NULL) FreeVec(d->root);
    if (d->active_path != NULL) FreeVec(d->active_path);
    result = class_dsm(cl, o, msg);
    if (show != NULL) DisposeObject(show);
    if (hide != NULL) DisposeObject(hide);
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
