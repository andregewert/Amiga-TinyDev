#include "editor.h"

#include <gadgets/dirtree.h>
#include <gadgets/layout.h>
#include <libraries/asl.h>
#include <proto/asl.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/intuition.h>

#include <string.h>

/* The folder tree itself (lazy loading, depth and entry limits, node
 * ownership) lives in dirtree.gadget (src/classes/dirtree/). This module only
 * connects it to the editor: choosing the root, opening files and
 * showing/hiding the column. */

/**
 * @brief Report a dirtree.gadget status as an error requester.
 *
 * @param app The editor application state.
 * @param status The DIRTREE_ST_# value to report (DIRTREE_ST_OK is silent).
 * @param failed_title Requester title used for DIRTREE_ST_FAILED.
 * @param failed_text Requester text used for DIRTREE_ST_FAILED.
 */
static void report_status(EditorApp *app, ULONG status, const char *failed_title,
                          const char *failed_text)
{
    if (status == DIRTREE_ST_INCOMPLETE)
        ui_error(app, S(MSG_TREE_TITLE), S(MSG_ERR_DIRINCOMPLETE));
    else if (status == DIRTREE_ST_FAILED)
        ui_error(app, failed_title, failed_text);
}

/**
 * @brief Make a directory the root of the folder tree.
 *
 * Hands the path to dirtree.gadget, which keeps the old tree if the
 * directory cannot be read. On success the window is laid out once (the
 * tree is populated for the first time). Later expand/collapse operations
 * only change the listbrowser's labels, which the gadget redraws itself,
 * and need no full RethinkLayout.
 *
 * @param app The editor application state.
 * @param path The directory path to open.
 * @return Non-zero on success, or 0 if the directory could not be opened.
 */
static int load_directory(EditorApp *app, const char *path)
{
    ULONG status = DIRTREE_ST_FAILED;
    if (app->tree == NULL) return 0;
    SetGadgetAttrs((struct Gadget *)app->tree, app->window, NULL,
                   DIRTREE_Root, (ULONG)path, TAG_END);
    GetAttr(DIRTREE_Status, app->tree, &status);
    report_status(app, status, S(MSG_OPENDIR_FAILED), S(MSG_ERR_DIROPEN));
    if (status == DIRTREE_ST_FAILED) return 0;
    ui_relayout(app);
    bookmarks_project_opened(app);
    return 1;
}

ULONG tree_signal_mask(EditorApp *app)
{
    ULONG mask = 0;
    if (app->tree != NULL) GetAttr(DIRTREE_SignalMask, app->tree, &mask);
    return mask;
}

void tree_refresh(EditorApp *app, int all)
{
    ULONG status = DIRTREE_ST_OK;
    if (app->tree == NULL || app->window == NULL) return;
    SetGadgetAttrs((struct Gadget *)app->tree, app->window, NULL,
                   DIRTREE_Refresh, all ? DIRTREE_RF_ALL : DIRTREE_RF_CHANGED, TAG_END);
    GetAttr(DIRTREE_Status, app->tree, &status);
    /* only a refresh the user asked for reports a directory it could not read */
    if (all) report_status(app, status, S(MSG_TREE_TITLE), S(MSG_ERR_TREEMEM));
}

int tree_project_root(EditorApp *app, char *out, LONG size)
{
    STRPTR root = NULL;
    BPTR lock;
    int ok;
    if (app->tree == NULL) return 0;
    GetAttr(DIRTREE_Root, app->tree, (ULONG *)&root);
    if (root == NULL || root[0] == '\0' || (lock = Lock(root, ACCESS_READ)) == 0) return 0;
    ok = NameFromLock(lock, (STRPTR)out, size) != 0;
    UnLock(lock);
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
        ASLFR_TitleText, (ULONG)S(MSG_ASL_DIR),
        ASLFR_DrawersOnly, TRUE, TAG_END);
    char path[EDITOR_PATH_MAX]; int result = 0;
    if (fr == NULL) { ui_error(app, APP_NAME, S(MSG_ERR_DIRREQ)); return 0; }
    if (AslRequestTags(fr, ASLFR_Window, (ULONG)app->window,
                       ASLFR_SleepWindow, TRUE, TAG_END)) {
        strncpy(path, fr->fr_Drawer, sizeof(path) - 1); path[sizeof(path) - 1] = '\0';
        if (fr->fr_File[0] != '\0' && !AddPart(path, fr->fr_File, sizeof(path)))
            ui_error(app, S(MSG_OPENDIR_FAILED), S(MSG_ERR_PATHTOOLONG));
        else result = load_directory(app, path);
    }
    FreeAslRequest(fr); return result;
}

/**
 * @brief Handle a GADGETUP of the folder tree.
 *
 * Lets dirtree.gadget process the event on this process (it may read a
 * directory, which is not allowed during input handling). A double-clicked
 * file is opened as a document; read errors are reported.
 *
 * @param app The editor application state.
 */
void tree_handle_event(EditorApp *app)
{
    ULONG event = DIRTREE_EV_NONE, status = DIRTREE_ST_OK;
    STRPTR path = NULL;
    if (app->tree == NULL || app->window == NULL) return;
    /* SetGadgetAttrs() reliably passes the GadgetInfo the gadget needs to
     * redraw the changed list; a custom method called through
     * DoGadgetMethodA() left the expanded directory undrawn. */
    SetGadgetAttrs((struct Gadget *)app->tree, app->window, NULL,
                   DIRTREE_HandleEvent, TRUE, TAG_END);
    GetAttr(DIRTREE_Event, app->tree, &event);
    GetAttr(DIRTREE_Status, app->tree, &status);
    report_status(app, status, S(MSG_TREE_TITLE), S(MSG_ERR_TREEMEM));
    if (event == DIRTREE_EV_ACTIVATE) {
        GetAttr(DIRTREE_ActivePath, app->tree, (ULONG *)&path);
        if (path != NULL) document_open(app, (const char *)path);
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
