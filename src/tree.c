#include "editor.h"

#include <dos/dos.h>
#include <gadgets/layout.h>
#include <gadgets/listbrowser.h>
#include <libraries/asl.h>
#include <proto/asl.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/listbrowser.h>
#include <proto/utility.h>
#include <clib/alib_protos.h>

#include <string.h>

#define TREE_MAX_DEPTH 16
#define TREE_MAX_NODES 2048

typedef struct TreeEntry {
    int directory;
    int placeholder;
    int expanded;
    char *name;
    char path[1];
} TreeEntry;

/**
 * @brief Duplicate a NUL-terminated string into a freshly allocated buffer.
 *
 * Allocates memory with AllocVec and copies the string (including its
 * terminating NUL) into it. The caller owns the returned buffer and must
 * release it with FreeVec.
 *
 * @param value The NUL-terminated source string to copy.
 * @return A newly allocated copy of the string, or NULL if allocation failed.
 */
static char *tree_string(const char *value)
{
    size_t length = strlen(value) + 1;
    char *copy = AllocVec((ULONG)length, MEMF_ANY);
    if (copy != NULL) memcpy(copy, value, length);
    return copy;
}

/**
 * @brief Allocate and initialize a TreeEntry describing a tree item.
 *
 * Allocates a single cleared block large enough to hold the TreeEntry header
 * together with the path and name strings, then copies both strings into it
 * and points the name field just past the stored path. The caller owns the
 * returned entry and must release it with FreeVec.
 *
 * @param name The display name of the entry.
 * @param path The full filesystem path of the entry.
 * @param directory Non-zero if the entry represents a directory.
 * @param placeholder Non-zero if the entry is a placeholder node.
 * @return The initialized TreeEntry, or NULL if allocation failed.
 */
static TreeEntry *tree_entry(const char *name, const char *path,
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
 * @brief Create a listbrowser node backed by a new TreeEntry.
 *
 * Builds a TreeEntry for the given item and wraps it in an
 * AllocListBrowserNode, storing the entry as the node's user data, setting the
 * node generation, and marking directories with LBFLG_HASCHILDREN. If node
 * allocation fails the previously allocated entry is freed.
 *
 * @param name The display name of the entry.
 * @param path The full filesystem path of the entry.
 * @param generation The generation (tree depth) of the node.
 * @param directory Non-zero if the entry represents a directory.
 * @param placeholder Non-zero if the entry is a placeholder node.
 * @return The allocated listbrowser node, or NULL on failure.
 */
static struct Node *new_tree_node(const char *name, const char *path,
                                  ULONG generation, int directory,
                                  int placeholder)
{
    TreeEntry *entry = tree_entry(name, path, directory, placeholder);
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
 * @brief Create a placeholder listbrowser node with the given label.
 *
 * Placeholders mark directories as expandable or convey status text such as
 * "Loading...", "(empty)", or "(unavailable)". When requested, the node is
 * flagged hidden so it does not appear until its parent is expanded.
 *
 * @param generation The generation (tree depth) of the placeholder node.
 * @param label The text shown for the placeholder.
 * @param hidden Non-zero to create the node with the LBFLG_HIDDEN flag set.
 * @return The allocated placeholder node, or NULL on failure.
 */
static struct Node *new_placeholder(ULONG generation, const char *label,
                                    int hidden)
{
    struct Node *node = new_tree_node(label, "", generation, 0, 1);
    if (node != NULL && hidden)
        SetListBrowserNodeAttrs(node, LBNA_Flags, LBFLG_HIDDEN, TAG_END);
    return node;
}

/**
 * @brief Retrieve the TreeEntry stored as a node's user data.
 *
 * @param node The listbrowser node to query.
 * @return The associated TreeEntry, or NULL if none is set.
 */
static TreeEntry *node_entry(struct Node *node)
{
    TreeEntry *entry = NULL;
    GetListBrowserNodeAttrs(node, LBNA_UserData, (ULONG)&entry, TAG_END);
    return entry;
}

/**
 * @brief Retrieve the generation (tree depth) attribute of a node.
 *
 * @param node The listbrowser node to query.
 * @return The node's generation value.
 */
static ULONG node_generation(struct Node *node)
{
    ULONG generation = 0;
    GetListBrowserNodeAttrs(node, LBNA_Generation, (ULONG)&generation, TAG_END);
    return generation;
}

/**
 * @brief Detach the label list from the tree listbrowser gadget.
 *
 * Temporarily removes the node list from the gadget so the list can be safely
 * modified. Uses SetGadgetAttrs when a window is present and SetAttrs
 * otherwise. Does nothing if the tree gadget has not been created.
 *
 * @param app The editor application state.
 */
static void detach_tree(EditorApp *app)
{
    if (app->tree == NULL) return;
    if (app->window != NULL)
        SetGadgetAttrs((struct Gadget *)app->tree, app->window, NULL,
                       LISTBROWSER_Labels, ~0UL, TAG_END);
    else SetAttrs(app->tree, LISTBROWSER_Labels, ~0UL, TAG_END);
}

/**
 * @brief Re-attach the application's node list to the tree listbrowser gadget.
 *
 * Reconnects the tree_nodes label list to the gadget after modifications.
 * Uses SetGadgetAttrs when a window is present and SetAttrs otherwise. Does
 * nothing if the tree gadget has not been created.
 *
 * @param app The editor application state.
 */
static void attach_tree(EditorApp *app)
{
    if (app->tree == NULL) return;
    if (app->window != NULL)
        SetGadgetAttrs((struct Gadget *)app->tree, app->window, NULL,
                       LISTBROWSER_Labels, (ULONG)&app->tree_nodes, TAG_END);
    else SetAttrs(app->tree, LISTBROWSER_Labels,
                  (ULONG)&app->tree_nodes, TAG_END);
}

/**
 * @brief Remove a node from its list and free the node and its TreeEntry.
 *
 * Unlinks the node, releases the listbrowser node, and frees the associated
 * TreeEntry if present.
 *
 * @param node The listbrowser node to remove and free.
 */
static void free_tree_node(struct Node *node)
{
    TreeEntry *entry = node_entry(node);
    Remove(node);
    FreeListBrowserNode(node);
    if (entry != NULL) FreeVec(entry);
}

/**
 * @brief Remove and free all descendant nodes of a parent node.
 *
 * Walks the list following the parent, freeing every node whose generation is
 * greater than the parent's, and stops at the first sibling or list end. This
 * prunes an entire expanded subtree.
 *
 * @param parent The node whose descendants are removed.
 */
static void remove_descendants(struct Node *parent)
{
    ULONG generation = node_generation(parent);
    struct Node *node = parent->ln_Succ;
    while (node->ln_Succ != NULL && node_generation(node) > generation) {
        struct Node *next = node->ln_Succ;
        free_tree_node(node);
        node = next;
    }
}

/**
 * @brief Count the number of nodes currently in the tree node list.
 *
 * @param app The editor application state.
 * @return The total number of nodes in the tree_nodes list.
 */
static ULONG tree_node_count(EditorApp *app)
{
    ULONG count = 0; struct Node *node;
    for (node = app->tree_nodes.lh_Head; node->ln_Succ; node = node->ln_Succ)
        ++count;
    return count;
}

/**
 * @brief Decide whether a new child should sort before an existing entry.
 *
 * Directories sort before files; within the same kind entries are ordered
 * case-insensitively, falling back to a case-sensitive comparison to break
 * ties.
 *
 * @param name The name of the child being inserted.
 * @param directory Non-zero if the new child is a directory.
 * @param other The existing entry to compare against.
 * @return Non-zero if the new child should be placed before @p other.
 */
static int child_sorts_before(const char *name, int directory,
                              const TreeEntry *other)
{
    LONG order;
    if (directory != other->directory) return directory > other->directory;
    order = Stricmp(name, other->name);
    if (order != 0) return order < 0;
    return strcmp(name, other->name) < 0;
}

/**
 * @brief Find the node after which a new sorted child should be inserted.
 *
 * Scans the parent's direct children (nodes one generation deeper) and returns
 * the predecessor of the first child that the new item sorts before, honoring
 * the directory-first, case-insensitive ordering. Placeholder children are
 * skipped. If no such child is found, the predecessor of the subtree's end is
 * returned so the new item is appended after existing children.
 *
 * @param parent The parent node whose children are examined.
 * @param name The name of the child to insert.
 * @param directory Non-zero if the new child is a directory.
 * @return The node after which the new child should be inserted.
 */
static struct Node *sorted_child_predecessor(struct Node *parent,
                                              const char *name,
                                              int directory)
{
    ULONG parent_generation = node_generation(parent);
    ULONG child_generation = parent_generation + 1;
    struct Node *node = parent->ln_Succ;
    while (node->ln_Succ != NULL &&
           node_generation(node) > parent_generation) {
        if (node_generation(node) == child_generation) {
            TreeEntry *other = node_entry(node);
            if (other != NULL && !other->placeholder &&
                child_sorts_before(name, directory, other))
                return node->ln_Pred;
        }
        node = node->ln_Succ;
    }
    return node->ln_Pred;
}

/**
 * @brief Insert a placeholder node after a given position in the tree list.
 *
 * Creates a placeholder one generation below the parent and inserts it after
 * the node pointed to by @p after, updating @p after to reference the newly
 * inserted placeholder on success.
 *
 * @param app The editor application state.
 * @param parent The parent node whose generation determines the placeholder's.
 * @param after In/out pointer to the node to insert after; updated to the new
 *              placeholder on success.
 * @param label The text shown for the placeholder.
 * @param hidden Non-zero to create the placeholder as a hidden node.
 * @return Non-zero on success, or 0 if the placeholder could not be allocated.
 */
static int insert_placeholder(EditorApp *app, struct Node *parent,
                              struct Node **after, const char *label,
                              int hidden)
{
    struct Node *placeholder = new_placeholder(node_generation(parent) + 1,
                                                label, hidden);
    if (placeholder == NULL) return 0;
    Insert(&app->tree_nodes, placeholder, *after);
    *after = placeholder;
    return 1;
}

/**
 * @brief Lazily load the direct children of a directory node.
 *
 * Locks and examines the parent directory, then inserts a sorted node for each
 * entry returned by ExNext, adding a hidden "Loading..." placeholder under any
 * child directory so it can be expanded later. Traversal is bounded: it stops
 * once TREE_MAX_NODES nodes exist and does nothing for non-directories or when
 * the parent's generation reaches TREE_MAX_DEPTH. When no children are added,
 * an "(empty)" or "(unavailable)" placeholder is inserted. The parent is
 * finally flagged to show its children. The lock and FileInfoBlock are always
 * released before returning.
 *
 * @param app The editor application state.
 * @param parent The directory node whose children are loaded.
 * @return Non-zero if the directory was read completely, or 0 on error or
 *         partial read (e.g. lock/examine failure or allocation failure).
 */
static int load_children(EditorApp *app, struct Node *parent)
{
    TreeEntry *entry = node_entry(parent);
    ULONG generation = node_generation(parent), count = tree_node_count(app);
    struct Node *after = parent; struct FileInfoBlock *fib = NULL;
    BPTR lock = 0; int ok = 1, children = 0;
    if (entry == NULL || !entry->directory || generation >= TREE_MAX_DEPTH) return 1;
    lock = Lock(entry->path, ACCESS_READ);
    fib = AllocDosObject(DOS_FIB, NULL);
    if (lock == 0 || fib == NULL || !Examine(lock, fib) || fib->fib_DirEntryType <= 0) {
        ok = 0; goto done;
    }
    while (count < TREE_MAX_NODES && ExNext(lock, fib)) {
        char child_path[EDITOR_PATH_MAX];
        struct Node *child;
        int directory = fib->fib_DirEntryType > 0;
        if (count + (directory ? 2UL : 1UL) > TREE_MAX_NODES) break;
        strncpy(child_path, entry->path, sizeof(child_path) - 1);
        child_path[sizeof(child_path) - 1] = '\0';
        if (!AddPart(child_path, fib->fib_FileName, sizeof(child_path))) continue;
        child = new_tree_node(fib->fib_FileName, child_path, generation + 1,
                              directory, 0);
        if (child == NULL) { ok = 0; break; }
        after = sorted_child_predecessor(parent, fib->fib_FileName, directory);
        Insert(&app->tree_nodes, child, after);
        after = child; ++count; ++children;
        if (directory) {
            if (!insert_placeholder(app, child, &after, "Loading...", 1)) {
                ok = 0; break;
            }
            HideListBrowserNodeChildren(child);
            ++count;
        }
    }
done:
    if (fib != NULL) FreeDosObject(DOS_FIB, fib);
    if (lock != 0) UnLock(lock);
    if (children == 0) {
        after = parent;
        if (!insert_placeholder(app, parent, &after,
                                ok ? "(empty)" : "(unavailable)", 0)) ok = 0;
    }
    SetListBrowserNodeAttrs(parent, LBNA_Flags,
        LBFLG_HASCHILDREN | LBFLG_SHOWCHILDREN, TAG_END);
    ShowListBrowserNodeChildren(parent, 1);
    return ok;
}

/**
 * @brief Expand a directory node by loading and showing its children.
 *
 * Detaches the list, removes any existing descendants, loads the directory's
 * children (reporting an error if the read was incomplete), marks the entry as
 * expanded, and re-attaches the list.
 *
 * @param app The editor application state.
 * @param node The directory node to expand.
 * @param entry The TreeEntry associated with @p node.
 */
static void expand_node(EditorApp *app, struct Node *node, TreeEntry *entry)
{
    detach_tree(app);
    remove_descendants(node);
    if (!load_children(app, node))
        ui_error(app, "Folder tree", "The directory could not be read completely.");
    entry->expanded = 1;
    attach_tree(app);
}

/**
 * @brief Collapse a directory node, freeing its loaded children.
 *
 * Detaches the list, removes all descendants, re-inserts a hidden
 * "Loading..." placeholder so the node remains expandable, updates the node's
 * flags and hides its children, marks the entry as collapsed, and re-attaches
 * the list. An error is reported if the placeholder could not be allocated.
 *
 * @param app The editor application state.
 * @param node The directory node to collapse.
 * @param entry The TreeEntry associated with @p node.
 */
static void collapse_node(EditorApp *app, struct Node *node, TreeEntry *entry)
{
    struct Node *after = node;
    detach_tree(app);
    remove_descendants(node);
    if (!insert_placeholder(app, node, &after, "Loading...", 1))
        ui_error(app, "Folder tree", "Not enough memory for the folder marker.");
    SetListBrowserNodeAttrs(node, LBNA_Flags, LBFLG_HASCHILDREN, TAG_END);
    HideListBrowserNodeChildren(node);
    entry->expanded = 0;
    attach_tree(app);
}

/**
 * @brief Remove all tree nodes and reset the tree's root state.
 *
 * Detaches the list, then removes and frees every node together with its
 * TreeEntry, re-initializes the node list to empty, and releases the cached
 * tree root path if one is set.
 *
 * @param app The editor application state.
 */
void tree_clear(EditorApp *app)
{
    struct Node *node;
    detach_tree(app);
    while ((node = RemHead(&app->tree_nodes)) != NULL) {
        TreeEntry *entry = node_entry(node);
        FreeListBrowserNode(node);
        if (entry != NULL) FreeVec(entry);
    }
    NewList(&app->tree_nodes);
    if (app->tree_root != NULL) { FreeVec(app->tree_root); app->tree_root = NULL; }
}

/**
 * @brief Load a directory as the new root of the folder tree.
 *
 * Locks and examines the given path, resolves its canonical name, clears any
 * existing tree, and creates a root node whose children are loaded lazily. On
 * success the root is marked expanded, the window is laid out once, and a
 * minimap re-render is requested (because the relayout repaints the minimap's
 * space.gadget). Any allocation or DOS failure is cleaned up and reported via
 * a requester. The lock and FileInfoBlock are always released before
 * returning.
 *
 * @param app The editor application state.
 * @param path The directory path to open.
 * @return Non-zero on success, or 0 if the directory could not be opened.
 */
static int load_directory(EditorApp *app, const char *path)
{
    BPTR lock = Lock(path, ACCESS_READ); struct FileInfoBlock *fib = NULL;
    char canonical[EDITOR_PATH_MAX]; struct Node *root = NULL; int ok = 0;
    fib = AllocDosObject(DOS_FIB, NULL);
    if (lock == 0 || fib == NULL || !Examine(lock, fib) ||
        fib->fib_DirEntryType <= 0 ||
        !NameFromLock(lock, canonical, sizeof(canonical))) goto done;
    tree_clear(app);
    app->tree_root = tree_string(canonical);
    root = new_tree_node(fib->fib_FileName, canonical, 0, 1, 0);
    if (app->tree_root == NULL || root == NULL) {
        if (root != NULL) {
            TreeEntry *entry = node_entry(root);
            FreeListBrowserNode(root);
            if (entry != NULL) FreeVec(entry);
        }
        tree_clear(app); goto done;
    }
    AddTail(&app->tree_nodes, root);
    if (!load_children(app, root))
        ui_error(app, "Folder tree", "The directory could not be read completely.");
    {
        TreeEntry *entry = node_entry(root);
        if (entry != NULL) entry->expanded = 1;
    }
    attach_tree(app);
    /* The tree is populated for the first time here, so lay the window out
     * once.  Later expand/collapse operations only change the listbrowser's
     * label list, which the gadget refreshes itself, and must NOT trigger a
     * full RethinkLayout: that would reflow the whole window and clear the
     * minimap's space.gadget to grey without re-rendering it. */
    ui_relayout(app);
    /* The relayout above repaints the minimap's space.gadget grey, so ask for a
     * re-render; otherwise opening a directory leaves the minimap grey. */
    minimap_request(app);
    ok = 1;
done:
    if (fib != NULL) FreeDosObject(DOS_FIB, fib);
    if (lock != 0) UnLock(lock);
    if (!ok) ui_error(app, "Open directory failed", "The directory could not be opened.");
    return ok;
}

/**
 * @brief Prompt the user for a directory and load it into the tree.
 *
 * Opens an ASL drawers-only file requester, combines the chosen drawer and
 * optional file component into a full path, and loads that directory. Reports
 * an error if the requester cannot be allocated or the combined path is too
 * long. The requester is always freed before returning.
 *
 * @param app The editor application state.
 * @return Non-zero if a directory was successfully loaded, otherwise 0.
 */
int tree_request_directory(EditorApp *app)
{
    struct FileRequester *fr = AllocAslRequestTags(ASL_FileRequest,
        ASLFR_TitleText, (ULONG)"Open directory",
        ASLFR_DrawersOnly, TRUE, TAG_END);
    char path[EDITOR_PATH_MAX]; int result = 0;
    if (fr == NULL) { ui_error(app, "tinyDE", "Could not allocate the directory requester."); return 0; }
    if (AslRequestTags(fr, ASLFR_Window, (ULONG)app->window,
                       ASLFR_SleepWindow, TRUE, TAG_END)) {
        strncpy(path, fr->fr_Drawer, sizeof(path) - 1); path[sizeof(path) - 1] = '\0';
        if (fr->fr_File[0] != '\0' && !AddPart(path, fr->fr_File, sizeof(path)))
            ui_error(app, "Open directory failed", "The selected path is too long.");
        else result = load_directory(app, path);
    }
    FreeAslRequest(fr); return result;
}

/**
 * @brief Handle a listbrowser event from the folder tree gadget.
 *
 * Reads the relative event and the cursor or selected node, then acts on
 * directory nodes: show-children expands, hide-children collapses, and a
 * double-click toggles expansion. Double-clicking a real (non-placeholder)
 * file node opens that file as a document. Does nothing when no node is
 * available.
 *
 * @param app The editor application state.
 */
void tree_handle_event(EditorApp *app)
{
    ULONG event = 0, value = 0; struct Node *node; TreeEntry *entry;
    GetAttr(LISTBROWSER_RelEvent, app->tree, &event);
    GetAttr(LISTBROWSER_CursorNode, app->tree, &value);
    if (value == 0) GetAttr(LISTBROWSER_SelectedNode, app->tree, &value);
    node = (struct Node *)value;
    if (node == NULL) return;
    entry = node_entry(node);
    if ((event & LBRE_SHOWCHILDREN) != 0 && entry != NULL && entry->directory) {
        expand_node(app, node, entry);
    } else if ((event & LBRE_HIDECHILDREN) != 0 && entry != NULL && entry->directory) {
        collapse_node(app, node, entry);
    } else if ((event & LBRE_DOUBLECLICK) != 0 && entry != NULL && entry->directory) {
        if (entry->expanded) collapse_node(app, node, entry);
        else expand_node(app, node, entry);
    } else if ((event & LBRE_DOUBLECLICK) != 0 && entry != NULL &&
               !entry->directory && !entry->placeholder) {
        document_open(app, entry->path);
    }
}

/**
 * @brief Show or hide the folder tree column and relayout the window.
 *
 * Records the requested visibility and adjusts the tree gadget's layout
 * constraints (minimum, maximum, and weighted width) so it either occupies its
 * default width or collapses to zero, then triggers a relayout. Showing the
 * tree always restores the default column width; the previous on-screen width
 * is not remembered. Does nothing if the content layout or tree gadget does
 * not exist.
 *
 * @param app The editor application state.
 * @param visible Non-zero to show the tree, zero to hide it.
 */
void tree_set_visible(EditorApp *app, int visible)
{
    app->tree_visible = visible;
    if (app->content_layout == NULL || app->tree == NULL) return;
    /* Showing the tree always uses the default column width; the previous
     * on-screen width is intentionally not remembered or restored. */
    if (app->window != NULL)
        SetGadgetAttrs((struct Gadget *)app->content_layout, app->window, NULL,
            LAYOUT_ModifyChild, (ULONG)app->tree,
            CHILD_MinWidth, visible ? TREE_MIN_WIDTH : 0,
            CHILD_MaxWidth, visible ? ~0UL : 0,
            CHILD_WeightedWidth, visible ? TREE_DEFAULT_WEIGHT : 0,
            TAG_END);
    else SetAttrs(app->content_layout,
        LAYOUT_ModifyChild, (ULONG)app->tree,
        CHILD_MinWidth, visible ? TREE_MIN_WIDTH : 0,
        CHILD_MaxWidth, visible ? ~0UL : 0,
        CHILD_WeightedWidth, visible ? TREE_DEFAULT_WEIGHT : 0,
        TAG_END);
    ui_relayout(app);
}
