#ifndef TINYDEV_EDITOR_H
#define TINYDEV_EDITOR_H

#include <exec/lists.h>
#include <exec/libraries.h>
#include <graphics/text.h>
#include <intuition/classusr.h>
#include <intuition/intuition.h>
#include <workbench/startup.h>

#include "syntax.h"

#define EDITOR_MAX_FILE (8UL * 1024UL * 1024UL)
#define EDITOR_PATH_MAX 1024
#define EDITOR_TITLE_MAX 128
#define EDITOR_COLOR_COUNT 6

/* Content-layout column sizing, shared by the folder tree (tree.c) and the
 * minimap (minimap.c) so both collapse/restore their columns consistently. */
#define TREE_MIN_WIDTH        140
#define TREE_DEFAULT_WEIGHT   25
#define MINIMAP_MIN_WIDTH     48
#define MINIMAP_DEFAULT_WEIGHT 20
#define EDITOR_DEFAULT_WEIGHT 75

enum EditorColor {
    EDITOR_COLOR_BACKGROUND = 0,
    EDITOR_COLOR_TEXT,
    EDITOR_COLOR_KEYWORD,
    EDITOR_COLOR_STRING,
    EDITOR_COLOR_COMMENT,
    EDITOR_COLOR_PREPROCESSOR
};

enum GadgetId {
    GID_TABS = 1,
    GID_TREE = 2,
    GID_VSCROLL = 3,
    GID_HSCROLL = 4,
    GID_MINIMAP = 5,
    GID_NEW = 10, GID_OPEN, GID_SAVE, GID_UNDO, GID_REDO,
    GID_CUT, GID_COPY, GID_PASTE,
    GID_LAST
};
enum MenuId {
    MID_NEW = 1, MID_OPEN, MID_OPEN_DIRECTORY, MID_SAVE, MID_SAVE_AS, MID_CLOSE, MID_QUIT,
    MID_UNDO, MID_REDO, MID_CUT, MID_COPY, MID_PASTE, MID_SELECT_ALL,
    MID_FOLDER_TREE, MID_LINE_NUMBERS, MID_MINIMAP,
    MID_ABOUT
};

typedef enum LineEnding { EOL_LF = 0, EOL_CR = 1, EOL_CRLF = 2 } LineEnding;

typedef struct Document {
    struct Node node;
    Object *page;
    Object *editor_row;
    Object *editor;
    Object *vscroll;
    Object *hscroll;
    struct Node *tab;
    char *path;
    char title[EDITOR_TITLE_MAX];
    char tab_title[EDITOR_TITLE_MAX + 2];
    unsigned long number;
    int dirty;
    LineEnding eol;
    SyntaxLanguage language;
    SyntaxHookContext highlight;
} Document;

struct Minimap;

typedef struct EditorApp {
    struct List documents;
    struct List tab_nodes;
    struct List tree_nodes;
    Document *active;
    Object *window_object;
    Object *layout;
    Object *toolbar;
    Object *content_layout;
    Object *tree;
    Object *tree_show_image;
    Object *tree_hide_image;
    /* Close glyph used by the tab close gadgets: the external
     * TBImages:list_remove image, loaded through the bitmap.image class and
     * handed to CLICKTAB_CloseImage. */
    Object *tab_close_image;
    Object *tabs;
    Object *pages;
    Object *statusbar;
    Object *minimap;
    struct Minimap *minimap_ctx;
    Object *toolbar_images[8];
    struct Window *window;
    struct Screen *screen;
    struct DrawInfo *screen_draw_info;
    struct DrawInfo editor_draw_info;
    UWORD editor_draw_pens[NUMDRIPENS];
    struct Hook editor_backfill_hook;
    struct Hook tab_idcmp_hook;
    /* Render hook attached to the minimap space.gadget so the last rendered
     * minimap bitmap is redrawn by the gadget itself on every layout refresh
     * (e.g. WeightBar drags), instead of leaving the area cleared to grey. */
    struct Hook minimap_render_hook;
    struct Node *pending_close;
    long editor_pens[EDITOR_COLOR_COUNT];
    long minimap_view_pen;
    int editor_colors_open;
    char *tree_root;
    unsigned long next_document;
    long scroll_signal;
    int line_numbers;
    int tree_visible;
    int minimap_visible;
    int minimap_attached;
    int minimap_dragging;
    int scrolling;
    int running;
} EditorApp;

extern struct Library *AslBase, *GadToolsBase, *IconBase, *UtilityBase;
extern struct Library *WindowBase, *LayoutBase, *ClickTabBase, *TextFieldBase;
extern struct Library *ButtonBase, *BitMapBase, *SpaceBase;
extern struct Library *ListBrowserBase;
extern struct Library *GlyphBase;
extern struct Library *ScrollerBase;

/**
 * @brief Open every library and ReAction class required by the editor.
 * @param from_workbench Non-zero when launched from Workbench (enables GUI errors).
 * @return Non-zero if all required libraries opened, zero otherwise.
 */
int app_open_libraries(int from_workbench);
/** @brief Close all libraries and classes opened by app_open_libraries(). */
void app_close_libraries(void);
/**
 * @brief Create the main ReAction window and all its gadgets.
 * @param app The application state.
 * @return Non-zero on success, zero on failure.
 */
int ui_create(EditorApp *app);
/**
 * @brief Run the main event loop until the user quits.
 * @param app The application state.
 * @return Non-zero on a clean exit.
 */
int ui_run(EditorApp *app);
/**
 * @brief Dispose the window and release all UI resources.
 * @param app The application state.
 */
void ui_destroy(EditorApp *app);
/**
 * @brief Refresh the editor view to reflect the active document.
 * @param app The application state.
 */
void ui_refresh(EditorApp *app);
/**
 * @brief Update the status line with the active document's state.
 * @param app The application state.
 */
void ui_update_status(EditorApp *app);
/**
 * @brief Re-run the window layout after a structural change.
 * @param app The application state.
 */
void ui_relayout(EditorApp *app);
/**
 * @brief Show a modal error requester (or print to stderr without a window).
 * @param app The application state.
 * @param title The requester title.
 * @param message The message body.
 */
void ui_error(EditorApp *app, const char *title, const char *message);
/**
 * @brief Show the modal About requester with application information.
 * @param app The application state.
 */
void ui_about(EditorApp *app);
/**
 * @brief Ask the user whether to close a document with unsaved changes.
 * @param app The application state.
 * @param doc The document being closed.
 * @return Non-zero if the close may proceed, zero to cancel.
 */
int ui_confirm_close(EditorApp *app, Document *doc);

/**
 * @brief Create a new, empty document in its own tab and activate it.
 * @param app The application state.
 * @return The new document, or NULL on failure.
 */
Document *document_new(EditorApp *app);
/**
 * @brief Open a file into a new (or reused) document tab.
 * @param app The application state.
 * @param path The file path to open.
 * @return The document holding the file, or NULL on failure.
 */
Document *document_open(EditorApp *app, const char *path);
/**
 * @brief Find an open document by its canonical path.
 * @param app The application state.
 * @param path The canonical path to look for.
 * @return The matching document, or NULL if none is open.
 */
Document *document_find_path(EditorApp *app, const char *path);
/**
 * @brief Make a document the active tab and refresh the view.
 * @param app The application state.
 * @param doc The document to activate.
 */
void document_activate(EditorApp *app, Document *doc);
/**
 * @brief Close a document, optionally confirming unsaved changes first.
 * @param app The application state.
 * @param doc The document to close.
 * @param ask Non-zero to prompt when the document is dirty.
 * @return Non-zero if the document was closed, zero if cancelled.
 */
int document_close(EditorApp *app, Document *doc, int ask);
/**
 * @brief Close and free every open document.
 * @param app The application state.
 */
void document_free_all(EditorApp *app);
/**
 * @brief Set a document's dirty flag and update its tab/status display.
 * @param app The application state.
 * @param doc The document to update.
 * @param dirty Non-zero to mark the document as modified.
 */
void document_set_dirty(EditorApp *app, Document *doc, int dirty);
/**
 * @brief Set a document's path, updating its title, tab and language.
 * @param app The application state.
 * @param doc The document to update.
 * @param path The new canonical path (NULL for an unnamed document).
 */
void document_set_path(EditorApp *app, Document *doc, const char *path);
/**
 * @brief Synchronise the scroller gadgets with the document's viewport.
 * @param app The application state.
 * @param doc The document whose scrollers are updated.
 */
void document_sync_scrollers(EditorApp *app, Document *doc);
/**
 * @brief Scroll the active document in response to a scroller gadget.
 * @param app The application state.
 * @param horizontal Non-zero for the horizontal scroller, zero for vertical.
 */
void document_scroll(EditorApp *app, int horizontal);
/**
 * @brief Apply a live scroll update while a scroller is being dragged.
 * @param app The application state.
 */
void document_scroll_live(EditorApp *app);
/**
 * @brief Finish a scroll interaction and restore normal rendering.
 * @param app The application state.
 * @param horizontal Non-zero for the horizontal scroller, zero for vertical.
 */
void document_scroll_finish(EditorApp *app, int horizontal);
/**
 * @brief Suspend syntax highlighting during an active scrollbar drag.
 * @param app The application state.
 */
void document_suspend_highlight(EditorApp *app);
/**
 * @brief Resume syntax highlighting and force a redraw after a drag.
 * @param app The application state.
 */
void document_resume_highlight(EditorApp *app);

/**
 * @brief Create the minimap space.gadget and its render hook.
 * @param app The application state.
 * @return The created gadget object, or NULL on failure.
 */
Object *minimap_create_gadget(EditorApp *app);
/**
 * @brief Start the background minimap render task and message ports.
 * @param app The application state.
 * @return Non-zero on success, zero on failure.
 */
int minimap_start(EditorApp *app);
/**
 * @brief Stop and join the minimap render task and release its resources.
 * @param app The application state.
 */
void minimap_stop(EditorApp *app);
/**
 * @brief Show or hide the minimap column and enable/disable rendering.
 * @param app The application state.
 * @param visible Non-zero to show the minimap, zero to hide it.
 */
void minimap_set_visible(EditorApp *app, int visible);
/**
 * @brief Request a (coalesced) re-render of the active document's minimap.
 * @param app The application state.
 */
void minimap_request(EditorApp *app);
/**
 * @brief Poll for and dispatch pending minimap render replies.
 * @param app The application state.
 */
void minimap_poll(EditorApp *app);
/**
 * @brief Handle a completed render reply by adopting the new bitmap.
 * @param app The application state.
 */
void minimap_handle_reply(EditorApp *app);
/**
 * @brief Handle a mouse click inside the minimap to reposition the view.
 * @param app The application state.
 */
void minimap_handle_mouse(EditorApp *app);
/**
 * @brief Handle minimap-related button events identified by a gadget code.
 * @param app The application state.
 * @param code The gadget/message code to act on.
 */
void minimap_handle_buttons(EditorApp *app, UWORD code);
/**
 * @brief Return the exec signal mask used to wake on minimap replies.
 * @param app The application state.
 * @return The signal mask to include in the main Wait().
 */
ULONG minimap_signal_mask(EditorApp *app);

/**
 * @brief Load a file's contents into a document.
 * @param app The application state.
 * @param doc The document to load into.
 * @param path The file path to load.
 * @return Non-zero on success, zero on failure.
 */
int file_load(EditorApp *app, Document *doc, const char *path);
/**
 * @brief Save a document to a path using a safe temporary-file scheme.
 * @param app The application state.
 * @param doc The document to save.
 * @param path The destination path.
 * @return Non-zero on success, zero on failure.
 */
int file_save(EditorApp *app, Document *doc, const char *path);
/**
 * @brief Prompt the user for a file to open via an ASL requester.
 * @param app The application state.
 * @return Non-zero if a document was opened, zero otherwise.
 */
int file_request_open(EditorApp *app);
/**
 * @brief Prompt the user for a destination and save a document via ASL.
 * @param app The application state.
 * @param doc The document to save.
 * @return Non-zero if the document was saved, zero otherwise.
 */
int file_request_save(EditorApp *app, Document *doc);

/**
 * @brief Prompt for and load a directory into the folder tree.
 * @param app The application state.
 * @return Non-zero if a directory was chosen and loaded, zero otherwise.
 */
int tree_request_directory(EditorApp *app);
/**
 * @brief Clear the folder tree and free all of its nodes.
 * @param app The application state.
 */
void tree_clear(EditorApp *app);
/**
 * @brief Handle a folder-tree event (expand, collapse or open a file).
 * @param app The application state.
 */
void tree_handle_event(EditorApp *app);
/**
 * @brief Show or hide the folder-tree column.
 * @param app The application state.
 * @param visible Non-zero to show the tree, zero to hide it.
 */
void tree_set_visible(EditorApp *app, int visible);

#endif
