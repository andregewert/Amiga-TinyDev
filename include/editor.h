#ifndef TINYDEV_EDITOR_H
#define TINYDEV_EDITOR_H

#include <exec/lists.h>
#include <exec/libraries.h>
#include <graphics/text.h>
#include <intuition/classusr.h>
#include <intuition/intuition.h>
#include <workbench/startup.h>

#include "syntax.h"

struct HintInfo;

/* Name and version; the version changes only with a release. */
#define APP_NAME "TinyDev"
#define APP_VERSION "1.0 (18.10.2026)"

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
    GID_TOOLBAR = 6,    /* speedbar; the GADGETUP code is the MID_ of the button */
    GID_OVERFLOW = 7,   /* chooser with the buttons that do not fit */
    GID_LAST
};

/** What the toolbar buttons show (tool type TOOLBAR=IMAGES|BOTH|TEXT). */
enum ToolbarMode { TBMODE_IMAGES = 0, TBMODE_BOTH, TBMODE_TEXT, TBMODE_COUNT };

/** Configurable colours of the editor (syntax highlighting), in this order. */
enum SettingsColour {
    SCOL_TEXT = 0, SCOL_KEYWORD, SCOL_STRING, SCOL_COMMENT, SCOL_PREPROCESSOR,
    SCOL_COUNT
};

/** Settings from the tool types of the program icon (and project icons),
 *  edited in the settings window. */
typedef struct Settings {
    int tbmode;         /* ToolbarMode, takes effect at the next start */
    int tbframes;       /* frames around the toolbar buttons (TOOLBARFRAMES) */
    int splash;         /* splash window while starting (NOSPLASH turns it off) */
    long winwidth;      /* main window at the start: 0 = automatic */
    long winheight;
    long winleft;       /* -1 = centred */
    long wintop;
    unsigned long colours[SCOL_COUNT];  /* 0xRRGGBB, SettingsColour order */
    char icon_name[300];/* program icon without .info, for splash and About */
} Settings;

/** Default colours, SettingsColour order. */
extern const unsigned long settings_default_colours[SCOL_COUNT];
/** Results of prefs_dialog(). */
enum { PREFS_CANCEL = 0, PREFS_USE, PREFS_SAVE };
enum MenuId {
    MID_NEW = 1, MID_OPEN, MID_OPEN_DIRECTORY, MID_SAVE, MID_SAVE_AS, MID_CLOSE, MID_QUIT,
    MID_UNDO, MID_REDO, MID_CUT, MID_COPY, MID_PASTE, MID_SELECT_ALL,
    MID_FOLDER_TREE, MID_LINE_NUMBERS, MID_MINIMAP,
    MID_ABOUT, MID_SETTINGS
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
    struct Hook minimap_hook;   /* MINIMAP_ColorHook, h_Data = document */
} Document;

typedef struct EditorApp {
    Settings settings;
    struct List documents;
    struct List tab_nodes;
    Document *active;
    Object *window_object;
    Object *layout;
    Object *toolbar;
    Object *content_layout;
    Object *tree;               /* dirtree.gadget */
    /* Close symbol of the tab close gadgets (CLICKTAB_CloseImage): a
     * built-in black cross (closeimage.c). */
    Object *tab_close_image;
    Object *tabs;
    Object *pages;
    Object *statusbar;
    Object *minimap;            /* minimap.gadget */
    Object *minimap_editor;     /* editor connected to the minimap */
    struct Window *window;
    struct Screen *screen;
    struct DrawInfo *screen_draw_info;
    struct DrawInfo editor_draw_info;
    UWORD editor_draw_pens[NUMDRIPENS];
    struct Hook editor_backfill_hook;
    struct Hook tab_idcmp_hook;
    struct Node *pending_close;
    long editor_pens[EDITOR_COLOR_COUNT];
    long minimap_view_pen;
    int editor_colors_open;
    unsigned long next_document;
    long scroll_signal;
    int line_numbers;
    int tree_visible;
    int minimap_visible;
    int minimap_attached;
    int minimap_dragging;       /* highlighter suspended for a minimap drag */
    int minimap_notified;       /* a minimap notification is pending */
    int minimap_drag_state;     /* MINIMAP_Dragging of that notification */
    ULONG minimap_top;          /* MINIMAP_Top of that notification */
    int scrolling;
    int scrollbar_dragging;     /* a scroller is being used, do not write it */
    int scroll_idle_ticks;      /* IntuiTicks since the last scroll signal */
    int defer_depth;            /* ui_begin_update() nesting */
    int pending_layout;         /* a relayout was requested meanwhile */
    int pending_refresh;        /* a page redraw was requested meanwhile */
    int running;
} EditorApp;

extern struct Library *AslBase, *GadToolsBase, *IconBase, *UtilityBase;
extern struct Library *WindowBase, *LayoutBase, *ClickTabBase, *TextFieldBase;
extern struct Library *ButtonBase, *BitMapBase;
extern struct Library *DirTreeBase;
extern struct Library *MinimapBase;
extern struct Library *ScrollerBase;
extern struct Library *SpeedBarBase, *LabelBase, *ChooserBase;
extern struct Library *DiskfontBase;

/**
 * @brief Set the default settings.
 * @param s The settings to fill in.
 */
void settings_default(Settings *s);
/**
 * @brief Write the settings into the tool types of the program icon. Other
 *        tool types and the NewIcons block stay; a setting at its default
 *        value stays as a disabled entry "(KEY=value)".
 * @param s The settings (icon_name names the icon).
 * @return Non-zero on success.
 */
int settings_save(const Settings *s);
/**
 * @brief Show the modal settings window (categories Window, Colours,
 *        Toolbar).
 * @param app The application state (main window and screen).
 * @param s In: the current settings; out: the edited ones (not on cancel).
 * @return PREFS_CANCEL, PREFS_USE or PREFS_SAVE.
 */
int prefs_dialog(EditorApp *app, Settings *s);
/** @brief Close the classes opened by the settings window. */
void prefs_cleanup(void);
/**
 * @brief Take over edited settings: colours at once, the rest at the next
 *        start.
 * @param app The application state.
 * @param n The new settings.
 */
void ui_apply_settings(EditorApp *app, const Settings *n);

/**
 * @brief Read the settings from the tool types of the program icon and, on
 *        a Workbench start, of the project icons (which take precedence).
 * @param s The settings, preset with settings_default().
 * @param argc The argument count, 0 for a Workbench start.
 * @param argv The arguments, or the WBStartup message for a Workbench start.
 */
void settings_load(Settings *s, int argc, char **argv);

/**
 * @brief Create the close symbol of the editor tabs (a black cross).
 * @return The image object, or NULL.
 */
Object *close_image_new(void);
/** @brief Free its class once no close symbol exists any more. */
void close_image_free_class(void);

/**
 * @brief Open the splash window (Intuition and graphics only).
 * @param icon_name The program icon (without .info).
 * @param steps Number of steps of the progress bar.
 */
void splash_open(const char *icon_name, ULONG steps);
/**
 * @brief Show the current start-up step in the splash window.
 * @param text The step.
 */
void splash_status(const char *text);
/** @brief Advance the splash window's progress bar by one step. */
void splash_step(void);
/** @brief Close the splash window if it is open. */
void splash_close(void);
/**
 * @brief Show the modal About window centred over the main window.
 * @param app The application state.
 * @param details The details below the head, lines separated by '\n'.
 * @return Non-zero if shown, zero if it could not be opened.
 */
int about_window(EditorApp *app, const char *details);

/**
 * @brief Return the number of toolbar buttons (steps of the splash window).
 * @return The number of buttons.
 */
ULONG toolbar_tool_count(void);
/**
 * @brief Build the toolbar (speedbar plus overflow chooser) into app->toolbar.
 * @param app The application state (screen and settings are used).
 * @return Non-zero on success, zero on failure.
 */
int toolbar_create(EditorApp *app);
/**
 * @brief Return the help bubbles of the toolbar for WINDOW_HintInfo.
 * @return The HintInfo array, terminated with hi_GadgetID -1.
 */
struct HintInfo *toolbar_hints(void);
/**
 * @brief Tell the toolbar its window once it is open (for help bubbles).
 * @param app The application state.
 */
void toolbar_window_opened(EditorApp *app);
/**
 * @brief Ghost the buttons that would do nothing and refresh the overflow
 *        list; call on every tick of the event loop.
 * @param app The application state.
 */
void toolbar_update(EditorApp *app);
/**
 * @brief Return the menu command of an entry of the overflow chooser.
 * @param index The entry's index (the GADGETUP code).
 * @return The MID_ command, or 0 if none.
 */
ULONG toolbar_overflow_command(ULONG index);
/**
 * @brief Free the toolbar's nodes, images, font and pen; call after the
 *        window (which disposes the gadgets) and before the screen is
 *        unlocked.
 * @param app The application state.
 */
void toolbar_dispose(EditorApp *app);

/**
 * @brief Open the libraries the splash window and the settings need
 *        (intuition, graphics, utility, icon).
 * @param from_workbench Non-zero when launched from Workbench (enables GUI errors).
 * @return Non-zero if all of them opened, zero otherwise.
 */
int app_open_core(int from_workbench);
/**
 * @brief Open the remaining libraries and the ReAction and TinyDev classes.
 * @return Non-zero if all required ones opened, zero otherwise.
 */
int app_open_classes(void);
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
 * @brief Redraw a document's page, or note it while updates are collected.
 * @param app The application state.
 * @param doc The document.
 */
void ui_refresh_page(EditorApp *app, Document *doc);
/**
 * @brief Set up to two attributes of a gadget (with the window if open).
 * @param app The application state.
 * @param obj The gadget.
 * @param tag1 First attribute (TAG_IGNORE for none).
 * @param data1 Its value.
 * @param tag2 Second attribute (TAG_IGNORE for none).
 * @param data2 Its value.
 */
void ui_gadget_set(EditorApp *app, Object *obj, ULONG tag1, ULONG data1,
                   ULONG tag2, ULONG data2);
/**
 * @brief Start collecting relayouts and page redraws (nestable).
 * @param app The application state.
 */
void ui_begin_update(EditorApp *app);
/**
 * @brief Finish collecting: at most one relayout or one page redraw.
 * @param app The application state.
 */
void ui_end_update(EditorApp *app);
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
 * @brief Finish a scrollbar interaction after two IntuiTicks without a
 *        scroll signal (it may send no GADGETUP).
 * @param app The application state.
 */
void document_scroll_tick(EditorApp *app);
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
 * @brief Create the minimap.gadget.
 * @param app The application state.
 * @return The created gadget object, or NULL on failure.
 */
Object *minimap_create_gadget(EditorApp *app);
/**
 * @brief Set up a document's minimap colour hook.
 * @param doc The new document.
 */
void minimap_init_document(Document *doc);
/**
 * @brief Show or hide the minimap column.
 * @param app The application state.
 * @param visible Non-zero to show the minimap, zero to hide it.
 */
void minimap_set_visible(EditorApp *app, int visible);
/**
 * @brief Tell the minimap that the active document's text changed.
 * @param app The application state.
 */
void minimap_request(EditorApp *app);
/**
 * @brief Connect the active document and let the minimap update itself.
 * @param app The application state.
 */
void minimap_poll(EditorApp *app);
/**
 * @brief Disconnect a document from the minimap before it is closed.
 * @param app The application state.
 * @param doc The document, or NULL to disconnect whatever is connected.
 */
void minimap_forget_document(EditorApp *app, Document *doc);
/**
 * @brief Return the signal mask of finished minimap render jobs.
 * @param app The application state.
 * @return The signal mask to include in the main Wait().
 */
ULONG minimap_signal_mask(EditorApp *app);
/**
 * @brief Record a minimap notification from the window's IDCMP hook.
 * @param app The application state.
 * @param tags The IDCMPUPDATE tag list.
 * @return Non-zero if the notification came from the minimap.
 */
int minimap_handle_notify(EditorApp *app, struct TagItem *tags);
/**
 * @brief Scroll the editor to the line last chosen in the minimap.
 * @param app The application state.
 */
void minimap_handle_scroll(EditorApp *app);

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
