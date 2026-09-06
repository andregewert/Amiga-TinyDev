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

static char *tree_string(const char *value)
{
    size_t length = strlen(value) + 1;
    char *copy = AllocVec((ULONG)length, MEMF_ANY);
    if (copy != NULL) memcpy(copy, value, length);
    return copy;
}

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

static struct Node *new_placeholder(ULONG generation, const char *label,
                                    int hidden)
{
    struct Node *node = new_tree_node(label, "", generation, 0, 1);
    if (node != NULL && hidden)
        SetListBrowserNodeAttrs(node, LBNA_Flags, LBFLG_HIDDEN, TAG_END);
    return node;
}

static TreeEntry *node_entry(struct Node *node)
{
    TreeEntry *entry = NULL;
    GetListBrowserNodeAttrs(node, LBNA_UserData, (ULONG)&entry, TAG_END);
    return entry;
}

static ULONG node_generation(struct Node *node)
{
    ULONG generation = 0;
    GetListBrowserNodeAttrs(node, LBNA_Generation, (ULONG)&generation, TAG_END);
    return generation;
}

static void detach_tree(EditorApp *app)
{
    if (app->tree == NULL) return;
    if (app->window != NULL)
        SetGadgetAttrs((struct Gadget *)app->tree, app->window, NULL,
                       LISTBROWSER_Labels, ~0UL, TAG_END);
    else SetAttrs(app->tree, LISTBROWSER_Labels, ~0UL, TAG_END);
}

static void attach_tree(EditorApp *app)
{
    if (app->tree == NULL) return;
    if (app->window != NULL)
        SetGadgetAttrs((struct Gadget *)app->tree, app->window, NULL,
                       LISTBROWSER_Labels, (ULONG)&app->tree_nodes, TAG_END);
    else SetAttrs(app->tree, LISTBROWSER_Labels,
                  (ULONG)&app->tree_nodes, TAG_END);
}

static void free_tree_node(struct Node *node)
{
    TreeEntry *entry = node_entry(node);
    Remove(node);
    FreeListBrowserNode(node);
    if (entry != NULL) FreeVec(entry);
}

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

static ULONG tree_node_count(EditorApp *app)
{
    ULONG count = 0; struct Node *node;
    for (node = app->tree_nodes.lh_Head; node->ln_Succ; node = node->ln_Succ)
        ++count;
    return count;
}

static int child_sorts_before(const char *name, int directory,
                              const TreeEntry *other)
{
    LONG order;
    if (directory != other->directory) return directory > other->directory;
    order = Stricmp(name, other->name);
    if (order != 0) return order < 0;
    return strcmp(name, other->name) < 0;
}

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

static void expand_node(EditorApp *app, struct Node *node, TreeEntry *entry)
{
    detach_tree(app);
    remove_descendants(node);
    if (!load_children(app, node))
        ui_error(app, "Folder tree", "The directory could not be read completely.");
    entry->expanded = 1;
    attach_tree(app);
}

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

int tree_request_directory(EditorApp *app)
{
    struct FileRequester *fr = AllocAslRequestTags(ASL_FileRequest,
        ASLFR_TitleText, (ULONG)"Open directory",
        ASLFR_DrawersOnly, TRUE, TAG_END);
    char path[EDITOR_PATH_MAX]; int result = 0;
    if (fr == NULL) { ui_error(app, "AmiEditor", "Could not allocate the directory requester."); return 0; }
    if (AslRequestTags(fr, ASLFR_Window, (ULONG)app->window,
                       ASLFR_SleepWindow, TRUE, TAG_END)) {
        strncpy(path, fr->fr_Drawer, sizeof(path) - 1); path[sizeof(path) - 1] = '\0';
        if (fr->fr_File[0] != '\0' && !AddPart(path, fr->fr_File, sizeof(path)))
            ui_error(app, "Open directory failed", "The selected path is too long.");
        else result = load_directory(app, path);
    }
    FreeAslRequest(fr); return result;
}

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
