/**
 * @file editor.h
 * @brief Shared types and cross-module declarations of TinyDev.
 */
#ifndef TINYDEV_EDITOR_H
#define TINYDEV_EDITOR_H

#include <exec/lists.h>
#include <exec/libraries.h>
#include <graphics/text.h>
#include <intuition/classusr.h>
#include <intuition/intuition.h>
#include <workbench/startup.h>

#include "syntax.h"
#include "tinydev_strings.h"

struct HintInfo;

/** @name Program */
/** @{ */
#define APP_NAME "TinyDev"                  /**< name of the program (windows, requesters) */
#define APP_VERSION "1.0 (18.10.2026)"      /**< version and date; changes only with a release */
/** @} */

/** @name Limits */
/** @{ */
#define EDITOR_MAX_FILE (8UL * 1024UL * 1024UL) /**< largest file that is loaded (bytes) */
#define EDITOR_PATH_MAX 1024                /**< size of path buffers */
#define EDITOR_TITLE_MAX 128                /**< size of a document's title */
#define EDITOR_COLOR_COUNT 6                /**< number of editor colours (enum EditorColor) */
/** @} */

/**
 * @name Column sizes of the content layout
 * Shared by the folder tree (tree.c) and the minimap (minimap.c), so both
 * hide and show their columns the same way.
 */
/** @{ */
#define TREE_MIN_WIDTH        140   /**< smallest width of the folder tree */
#define TREE_DEFAULT_WEIGHT   25    /**< weight of the folder tree column */
#define MINIMAP_MIN_WIDTH     48    /**< smallest width of the minimap */
#define MINIMAP_DEFAULT_WEIGHT 9    /**< weight of the minimap column (tree 25, editor 75: about 1/12 of the width) */
#define EDITOR_DEFAULT_WEIGHT 75    /**< weight of the editor column */
/** @} */

/** Colours of the editor (pens in EditorApp.editor_pens), in this order. */
enum EditorColor {
    EDITOR_COLOR_BACKGROUND = 0,    /**< background of the text */
    EDITOR_COLOR_TEXT,              /**< normal text */
    EDITOR_COLOR_KEYWORD,           /**< keywords */
    EDITOR_COLOR_STRING,            /**< strings and character constants */
    EDITOR_COLOR_COMMENT,           /**< comments */
    EDITOR_COLOR_PREPROCESSOR       /**< preprocessor lines */
};

/** Gadget IDs of the main window (an editor has 100 + its document number). */
enum GadgetId {
    GID_TABS = 1,       /**< clicktab of the documents */
    GID_TREE = 2,       /**< dirtree.gadget */
    GID_VSCROLL = 3,    /**< vertical scroller of an editor */
    GID_HSCROLL = 4,    /**< horizontal scroller of an editor */
    GID_MINIMAP = 5,    /**< minimap.gadget */
    GID_TOOLBAR = 6,    /**< speedbar; the GADGETUP code is the MID_ of the button */
    GID_OVERFLOW = 7,   /**< chooser with the buttons that do not fit */
    GID_LAST            /**< first free ID */
};

/** What the toolbar buttons show (tool type TOOLBAR=IMAGES|BOTH|TEXT). */
enum ToolbarMode {
    TBMODE_IMAGES = 0,  /**< images (the default) */
    TBMODE_BOTH,        /**< images with a text below */
    TBMODE_TEXT,        /**< text only, no images are loaded */
    TBMODE_COUNT        /**< number of modes */
};

/** Configurable colours of the editor (syntax highlighting), in this order. */
enum SettingsColour {
    SCOL_TEXT = 0,      /**< normal text */
    SCOL_KEYWORD,       /**< keywords */
    SCOL_STRING,        /**< strings */
    SCOL_COMMENT,       /**< comments */
    SCOL_PREPROCESSOR,  /**< preprocessor lines */
    SCOL_COUNT          /**< number of colours */
};

/**
 * Settings from the tool types of the program icon (and project icons),
 * edited in the settings window.
 */
typedef struct Settings {
    int tbmode;         /**< ToolbarMode, takes effect at the next start */
    int tbframes;       /**< frames around the toolbar buttons (TOOLBARFRAMES) */
    int splash;         /**< splash window while starting (NOSPLASH turns it off) */
    int foldertree;     /**< folder tree shown at the start (NOFOLDERTREE turns it off) */
    int linenumbers;    /**< line numbers shown at the start (NOLINENUMBERS turns them off) */
    int minimap;        /**< minimap shown at the start (MINIMAP) */
    long winwidth;      /**< width of the main window at the start: 0 = automatic */
    long winheight;     /**< its height: 0 = automatic */
    long winleft;       /**< its left edge: -1 = centred */
    long wintop;        /**< its top edge: -1 = centred */
    unsigned long colours[SCOL_COUNT];  /**< 0xRRGGBB, SettingsColour order */
    long bmwin[4];      /**< bookmark list as snapshot: left, top, width, height; width 0 = automatic */
    int bmlist;         /**< bookmark list open at the start (BOOKMARKLIST) */
    long outwin[4];     /**< output window as snapshot (OUTPUTWINDOW); width 0 = automatic */
    int outlist;        /**< output window open at the start (OUTPUT) */
    long symwin[4];     /**< symbol list as snapshot (SYMBOLWINDOW); width 0 = automatic */
    int symlist;        /**< symbol list open at the start (SYMBOLLIST) */
    long diagwin[4];    /**< problem list as snapshot (PROBLEMWINDOW); width 0 = automatic */
    int diaglist;       /**< problem list open at the start (PROBLEMLIST) */
    int scout;          /**< analyse the sources with TinyDev-Scout (NOSCOUT turns it off) */
    char tempdir[256];  /**< drawer of the temporary files (TEMPDIR, default T:) */
    char icon_name[300];/**< program icon without .info, for splash and About */
} Settings;

/** Default colours, SettingsColour order. */
extern const unsigned long settings_default_colours[SCOL_COUNT];

/** Results of prefs_dialog(). */
enum {
    PREFS_CANCEL = 0,   /**< cancelled, nothing changes */
    PREFS_USE,          /**< use the settings in this session */
    PREFS_SAVE          /**< use them and write them into the program icon */
};

/** Commands of the menus and the toolbar (the menu items' user data). */
enum MenuId {
    MID_NEW = 1,        /**< Project/New */
    MID_OPEN,           /**< Project/Open... */
    MID_OPEN_DIRECTORY, /**< Project/Open Directory... */
    MID_SAVE,           /**< Project/Save */
    MID_SAVE_AS,        /**< Project/Save As... */
    MID_CLOSE,          /**< Project/Close */
    MID_QUIT,           /**< Project/Quit */
    MID_UNDO,           /**< Edit/Undo */
    MID_REDO,           /**< Edit/Redo */
    MID_CUT,            /**< Edit/Cut */
    MID_COPY,           /**< Edit/Copy */
    MID_PASTE,          /**< Edit/Paste */
    MID_SELECT_ALL,     /**< Edit/Select All */
    MID_FOLDER_TREE,    /**< View/Folder Tree (check item) */
    MID_LINE_NUMBERS,   /**< View/Line Numbers (check item) */
    MID_MINIMAP,        /**< View/Minimap (check item) */
    MID_ABOUT,          /**< Project/About... */
    MID_SETTINGS,       /**< Project/Settings... */
    MID_FIND,           /**< Edit/Find... */
    MID_FIND_NEXT,      /**< Edit/Find Next */
    MID_BM_TOGGLE,      /**< Bookmarks/Set/Remove Bookmark */
    MID_BM_NEXT,        /**< Bookmarks/Next Bookmark */
    MID_BM_PREV,        /**< Bookmarks/Previous Bookmark */
    MID_BM_CLEAR,       /**< Bookmarks/Remove All in This File */
    MID_TREE_REFRESH,   /**< Project/Refresh Folder Tree */
    MID_BM_LIST,        /**< View/Bookmarks: the bookmark list (check item) */
    MID_SNAPSHOT_TOOLS, /**< View/Snapshot Tool Windows */
    MID_HELP,           /**< Project/Help...: starts the HelpViewer */
    MID_ICONIFY,        /**< Project/Iconify: the main window becomes an icon */
    MID_PROJECT_SETTINGS, /**< Build/Project Settings...: the project's commands */
    MID_BUILD,          /**< Build/Build Project: the default Build command */
    MID_RUN,            /**< Build/Run: the default Run command */
    MID_DEBUG,          /**< Build/Debug: the default Debug command */
    MID_STOP,           /**< Build/Stop Build */
    MID_OUTPUT,         /**< View/Output Window (check item) */
    MID_SYMLIST,        /**< View/Symbol List (check item) */
    MID_DIAGLIST,       /**< View/Problem List (check item) */
    MID_DEFINITION,     /**< Edit/Go to Definition */
    MID_GOBACK          /**< Edit/Go Back */
};

/** Bookmarks of a document that TinyDev keeps track of. */
#define BOOKMARKS_MAX 64
/** Characters of a bookmark's line that are kept (BMSTORE_TEXT_MAX). */
#define BOOKMARK_TEXT_MAX 120

/** Which bookmarks the bookmark list shows. */
enum {
    BMSCOPE_CURRENT = 0,    /**< those of the active document */
    BMSCOPE_OPEN,           /**< those of all open documents */
    BMSCOPE_PROJECT,        /**< those of the project's files, open or not */
    BMSCOPE_COUNT           /**< number of scopes */
};

/** Most lines of a document marked in the left bar for diagnostics. */
#define DIAG_MARKS_MAX 256

/** A line marked in the left bar (read by the left bar hook). */
typedef struct DiagMark {
    LONG line;                  /**< the line, from 0 */
    UWORD severity;             /**< the worst diagnostic of the line (SCOUT_SEVERITY_, 1 = error) */
} DiagMark;

/** A diagnostic of a document, as Scout reported it. */
typedef struct DiagItem {
    long line;                  /**< its line (1-based) */
    long col;                   /**< its column (1-based) */
    int severity;               /**< SCOUT_SEVERITY_ value */
    char *message;              /**< its text (malloc()) */
} DiagItem;

/** Line endings of a document, as loaded (and saved again). */
typedef enum LineEnding {
    EOL_LF = 0,     /**< LF (Amiga, Unix) */
    EOL_CR = 1,     /**< CR (old Mac OS) */
    EOL_CRLF = 2    /**< CR LF (DOS, Windows) */
} LineEnding;

/** A document: a tab with its page, editor and scrollers, and its file. */
typedef struct Document {
    struct Node node;           /**< in EditorApp.documents (tab order) */
    Object *page;               /**< its page in the page.gadget */
    Object *editor_row;         /**< layout of editor and vertical scroller */
    Object *editor;             /**< texteditor.gadget (GA_ID 100 + number) */
    Object *vscroll;            /**< vertical scroller */
    Object *hscroll;            /**< horizontal scroller */
    struct Node *tab;           /**< its node in the clicktab (TNA_UserData = document) */
    char *path;                 /**< full name of the file (AllocVec()), NULL if it has none */
    char title[EDITOR_TITLE_MAX];       /**< file name or "Untitled n" */
    char tab_title[EDITOR_TITLE_MAX + 2];   /**< title with '*' when modified */
    unsigned long number;       /**< unique number of the document in this session */
    int dirty;                  /**< modified since it was loaded or saved */
    unsigned long saved_hash;   /**< checksum of the text as loaded or saved ... */
    long saved_length;          /**< ... and its length (document_mark_saved()) */
    int saved_from_file;        /**< ... taken from the file, not from the editor */
    int check_pending;          /**< changed since the last document_check_unchanged() */
    LineEnding eol;             /**< line endings of the file */
    int final_eol;              /**< the file ends with a line end (new documents: yes); kept when saving, as the editor exports the text without it */
    SyntaxLanguage language;    /**< language for the highlighting */
    SyntaxHookContext highlight;    /**< the editor's highlighter hook and its state */
    struct Hook minimap_hook;   /**< MINIMAP_ColorHook, h_Data = document */
    struct Hook leftbar_hook;   /**< GA_TEXTEDITOR_LeftBarHook, h_Data = document */
    ULONG bm_lines[BOOKMARKS_MAX];  /**< lines with a bookmark (from the editor, from 0) */
    long bm_count;              /**< number of them */
    LONG bm_click_line;         /**< line clicked in the left bar, -1 if none */
    int bm_apply;               /**< the project's bookmarks are still to be set */
    long bm_goto;               /**< line to go to once they are set, -1 if none ... */
    char bm_goto_text[BOOKMARK_TEXT_MAX + 1];   /**< ... and its text (to find it again) */
    int scout_pending;          /**< to be analysed by Scout (after a pause in typing) */
    int scout_now;              /**< ... at once (loaded or saved) */
    int scout_busy;             /**< an analysis is on its way */
    int scout_none;             /**< Scout has no parser for it: not asked again */
    long scout_version;         /**< version of the last snapshot sent */
    char *scout_snapshot;       /**< its snapshot file for Scout (AllocVec()), NULL */
    long scout_symbols;         /**< symbols of the last analysis, -1 if none */
    long scout_problems;        /**< diagnostics of the last analysis */
    unsigned long scout_generation; /**< counts the answered analyses (the symbol list follows it) */
    DiagMark diag_marks[DIAG_MARKS_MAX];    /**< lines marked in the left bar, by line (changed under Forbid()) */
    LONG diag_mark_count;       /**< number of them */
    DiagItem *diag_items;       /**< its diagnostics, by line (malloc()), NULL */
    long diag_count;            /**< number of them */
    unsigned long diag_generation;  /**< counts the changes of the diagnostics (the problem list follows it) */
    long goto_line;             /**< line to put the cursor on once the document is shown (1-based), 0 */
    long goto_col;              /**< its column (1-based) */
} Document;

/** The state of TinyDev: settings, documents, the main window and its gadgets. */
typedef struct EditorApp {
    Settings settings;          /**< the settings in use */
    struct List documents;      /**< the open documents (Document), in tab order */
    struct List tab_nodes;      /**< the clicktab's nodes */
    Document *active;           /**< the document in the visible tab, or NULL */
    Object *window_object;      /**< window.class object of the main window */
    Object *layout;             /**< its root layout */
    Object *toolbar;            /**< layout with the speedbar and the overflow chooser */
    Object *content_layout;     /**< layout of folder tree, editor area and minimap */
    Object *tree;               /**< dirtree.gadget */
    Object *tab_close_image;    /**< close symbol of the tabs (CLICKTAB_CloseImage, closeimage.c) */
    Object *tabs;               /**< clicktab.gadget of the documents */
    Object *pages;              /**< page.gadget with a page per document */
    Object *statusbar;          /**< status line (read-only button) */
    Object *minimap;            /**< minimap.gadget */
    Object *minimap_editor;     /**< editor connected to the minimap */
    struct Window *window;      /**< the open main window, NULL when closed */
    struct Screen *screen;      /**< the locked public screen */
    struct DrawInfo *screen_draw_info;  /**< its DrawInfo */
    struct DrawInfo editor_draw_info;   /**< copy for the editors (their own pens) */
    UWORD editor_draw_pens[NUMDRIPENS]; /**< pens of editor_draw_info */
    struct Hook editor_backfill_hook;   /**< GA_BackFill of the editors */
    struct Hook tab_idcmp_hook; /**< window IDCMP hook (tab close, minimap notifications) */
    struct Node *pending_close; /**< tab whose close gadget was clicked, NULL if none */
    long editor_pens[EDITOR_COLOR_COUNT];   /**< obtained pens, EditorColor order */
    long minimap_view_pen;      /**< pen of the minimap's view frame, -1 if none */
    int editor_colors_open;     /**< the editor pens are obtained */
    unsigned long next_document;    /**< number of the last document created */
    long scroll_signal;         /**< signal of the scrollers while they are dragged, -1 if none */
    int line_numbers;           /**< View/Line Numbers is on */
    int tree_visible;           /**< View/Folder Tree is on */
    int minimap_visible;        /**< View/Minimap is on */
    int minimap_attached;       /**< the minimap is in the content layout */
    int minimap_dragging;       /**< highlighter suspended for a minimap drag */
    int minimap_notified;       /**< a minimap notification is pending */
    int minimap_drag_state;     /**< MINIMAP_Dragging of that notification */
    ULONG minimap_top;          /**< MINIMAP_Top of that notification */
    int scrolling;              /**< the highlighting is suspended for scrolling */
    int scrollbar_dragging;     /**< a scroller is being used, do not write it */
    int scroll_idle_ticks;      /**< IntuiTicks since the last scroll signal */
    int change_idle_ticks;      /**< IntuiTicks since the last change of the text */
    int focus_editor;           /**< give the editor the focus when the window is active */
    long bm_box[4];             /**< box of the bookmark list in this session */
    struct MsgPort *app_port;   /**< AppPort of the main window (iconifying), NULL if none */
    WORD main_box[4];           /**< box of the main window while it is iconified */
    int find_was_open;          /**< the find window was open when the main window was iconified */
    int bmlist_was_open;        /**< the bookmark list was open when the main window was iconified */
    long out_box[4];            /**< box of the output window in this session */
    int outwin_was_open;        /**< the output window was open when the main window was iconified */
    long sym_box[4];            /**< box of the symbol list in this session */
    int symlist_was_open;       /**< the symbol list was open when the main window was iconified */
    long diag_box[4];           /**< box of the problem list in this session */
    int diaglist_was_open;      /**< the problem list was open when the main window was iconified */
    int defer_depth;            /**< ui_begin_update() nesting */
    int pending_layout;         /**< a relayout was requested meanwhile */
    int pending_refresh;        /**< a page redraw was requested meanwhile */
    int running;                /**< the event loop runs */
} EditorApp;

/** @name Library bases (main.c; the windows open some classes when needed) */
/** @{ */
extern struct Library *AslBase;         /**< asl.library */
extern struct Library *GadToolsBase;    /**< gadtools.library (menus) */
extern struct Library *IconBase;        /**< icon.library */
extern struct Library *UtilityBase;     /**< utility.library */
extern struct Library *WindowBase;      /**< window.class */
extern struct Library *LayoutBase;      /**< layout.gadget */
extern struct Library *ClickTabBase;    /**< clicktab.gadget */
extern struct Library *TextFieldBase;   /**< texteditor.gadget */
extern struct Library *ButtonBase;      /**< button.gadget */
extern struct Library *BitMapBase;      /**< bitmap.image */
extern struct Library *DirTreeBase;     /**< dirtree.gadget */
extern struct Library *MinimapBase;     /**< minimap.gadget */
extern struct Library *ScrollerBase;    /**< scroller.gadget */
extern struct Library *SpeedBarBase;    /**< speedbar.gadget */
extern struct Library *LabelBase;       /**< label.image */
extern struct Library *ChooserBase;     /**< chooser.gadget (may be NULL) */
extern struct Library *DiskfontBase;    /**< diskfont.library (may be NULL) */
/** @} */

/** @brief Open locale.library and TinyDev's catalog (if there is one). */
void locale_open(void);
/** @brief Close the catalog and locale.library. */
void locale_close(void);
/**
 * @brief Return a text in the user's language.
 * @param id The MSG_ number (include/tinydev_strings.h).
 * @return The text from the catalog, or the built-in English one.
 */
const char *S(LONG id);

/**
 * @brief Set the default settings.
 * @param s The settings to fill in.
 */
void settings_default(Settings *s);
/**
 * @brief Write the settings into the tool types of the program icon.
 *
 * Other tool types and the NewIcons block (from "*** DON'T EDIT") stay
 * where they are; new entries go in front of the NewIcons block. A setting
 * that is now at its default stays as a disabled entry "(KEY=value)", so
 * the icon keeps showing what can be set.
 *
 * @param s The settings.
 * @return Non-zero on success.
 */
int settings_save(const Settings *s);
/**
 * @brief Write the tool windows' boxes and open state (Snapshot Tool
 *        Windows) into the program icon; its other settings stay as they
 *        are in the icon.
 * @param cur The settings with the tool windows' values.
 * @return Non-zero on success.
 */
int settings_save_toolwindows(const Settings *cur);
/**
 * @brief Show the modal settings window (categories Window, Colours,
 *        Toolbar).
 * @param app The application state (main window and screen).
 * @param s In: the current settings; out: the edited ones (not on cancel).
 * @return PREFS_CANCEL, PREFS_USE or PREFS_SAVE.
 */
int prefs_dialog(EditorApp *app, Settings *s);
/** @brief Close the classes opened for the settings window. */
void prefs_cleanup(void);
/**
 * @brief Show the modal project settings window: the commands of the
 *        actions Build, Run and Debug of the project (the folder tree's
 *        root), saved in \<project\>/.tinydev/project.json.
 *
 * Without a project, or if the project file cannot be read, it shows a
 * message instead.
 *
 * @param app The application state (main window and screen).
 */
void project_settings(EditorApp *app);
/**
 * @brief Take over edited settings.
 *
 * New highlighting colours take effect at once: the new pens are obtained,
 * every document's highlighter and the minimap use them, the visible editor
 * is highlighted and drawn again, then the old pens are released. If a pen
 * cannot be obtained the old colours stay. Window, splash and toolbar
 * settings take effect at the next start.
 *
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
/** @brief Close the splash window (if it is open). */
void splash_close(void);
/**
 * @brief Show the modal About window centred over the main window.
 *
 * The main window shows the busy pointer and its input is dropped while the
 * About window is open. Return, Esc, the close gadget and OK close it.
 *
 * @param app The application state (main window and screen).
 * @param details The details, lines separated by '\n'.
 * @return Non-zero if the window was shown, zero if it could not be opened
 *         (the caller shows a requester instead).
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
 * @brief Tell the toolbar that its window closes (iconifying).
 */
void toolbar_window_closed(void);
/**
 * @brief Ghost the buttons that would do nothing and refresh the overflow
 *        list: Save with an unchanged document, Undo/Redo without steps,
 *        Cut/Copy without a selection.
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
 * @brief Free the nodes, images, font and pen of the toolbar.
 *
 * The speedbar frees neither its nodes nor their images; a label disposes
 * its image. Must run after the gadgets are disposed (with the window) and
 * before the screen is unlocked.
 *
 * @param app The application state.
 */
void toolbar_dispose(EditorApp *app);

/**
 * @brief Open the libraries needed before the splash window.
 *
 * Opens intuition.library and graphics.library (saving the previous global
 * bases so they can be restored on shutdown), verifies dos.library and opens
 * utility.library and icon.library. Any failure is reported via
 * report_library_error() and aborts the sequence.
 *
 * @param from_workbench Non-zero when launched from Workbench, which enables
 *        GUI error requesters.
 * @return Non-zero if all of them opened, zero otherwise.
 */
int app_open_core(int from_workbench);
/**
 * @brief Open the remaining libraries and the ReAction and TinyDev classes.
 *
 * Runs while the splash window shows; any failure is reported via
 * report_library_error() (which closes the splash window) and aborts.
 *
 * @return Non-zero if all required ones opened, zero otherwise.
 */
int app_open_classes(void);
/**
 * @brief Close all libraries and classes opened by app_open_libraries().
 *
 * Closes the ReAction classes and helper libraries in reverse order, then
 * restores the intuition.library and graphics.library global bases to the
 * values captured at startup. Safe to call even if opening only partially
 * succeeded.
 */
void app_close_libraries(void);
/**
 * @brief Build the application's screen resources, gadgets, and main window.
 *
 * Locks the public screen, reserves editor colors, and creates the page group,
 * click tabs (with a masked close image and IDCMP-targeted close notification),
 * folder tree, minimap gadget, toolbar, content and root layouts, and the
 * status bar.  Sets up the tab IDCMP hook, creates the window (requesting the
 * IDCMP flags needed for resizing and minimap interaction), ensures at least
 * one document exists, opens the window, performs the initial layout and
 * activation, starts the minimap task, and hides the minimap by default.
 *
 * @param app The application to initialize.
 * @return 1 on success, 0 on any failure.
 */
int ui_create(EditorApp *app);
/**
 * @brief Run the main event loop until the application should quit.
 *
 * Waits on the window, live-scroll, and minimap signals plus CTRL-C, then
 * services minimap replies, live scrolling, and break requests.  Drains
 * WM_HANDLEINPUT, dispatching close, tab/tree/scrollbar/toolbar/menu gadget
 * events and minimap mouse/resize handling.  After each input batch it mirrors
 * keyboard/mouse-driven editor scrolling onto the scrollbars, marks the active
 * document dirty when it changed, processes pending tab closes, and refreshes
 * the status line and minimap.
 *
 * @param app The application to run.
 * @return Always 1 when the loop exits.
 */
int ui_run(EditorApp *app);
/**
 * @brief Tear down the window, gadgets, and screen resources.
 *
 * Disconnects the minimap from the editors (it waits for a running render
 * job, whose colour hook reads the document) and frees all documents; the
 * minimap.gadget stops its render process itself when it is disposed with the
 * window.  Disposes
 * the window/layout/gadget hierarchy following the established ownership and
 * cleanup ordering, disposes a detached minimap gadget if the column was
 * hidden, frees toolbar and tree images and the tab close image, releases the
 * editor colors, unlocks the public screen, and clears all cached pointers.
 *
 * @param app The application to tear down.
 */
void ui_destroy(EditorApp *app);
/**
 * @brief Update the window title to reflect the active document.
 *
 * Sets the title to "TinyDev" when no document is active, otherwise to
 * "TinyDev - <title>" with a trailing '*' when the active document is dirty.
 *
 * @param app The application whose window title is refreshed.
 */
void ui_refresh(EditorApp *app);
/**
 * @brief Refresh the bottom status line with document and line counts.
 *
 * Counts the open documents and reads the line count of the active document
 * from its texteditor gadget, then updates the status bar text.  Uses
 * SetGadgetAttrs when a window is open so the gadget redraws, otherwise
 * SetAttrs.  Does nothing if the status bar does not exist.
 *
 * @param app The application whose status line is updated.
 */
void ui_update_status(EditorApp *app);
/**
 * @brief Recompute the window's layout after a structural change.
 *
 * Calls RethinkLayout on the root layout when both the window and layout
 * exist; otherwise does nothing.
 *
 * @param app The application whose layout is recomputed.
 */
void ui_relayout(EditorApp *app);
/**
 * @brief Redraw a document's page (editor and scrollers), or note it for
 *        ui_end_update() while updates are collected.
 * @param app The application state.
 * @param doc The document.
 */
void ui_refresh_page(EditorApp *app, Document *doc);
/**
 * @brief Set up to two attributes of a gadget in the window.
 *
 * Always with the window (SetGadgetAttrs()) when it is open: clicktab.gadget
 * and page.gadget do not take changes made without it (SetAttrs()) properly,
 * the tabs and the page stayed empty even after a relayout.
 *
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
 * @brief Check or uncheck a menu item of the main window.
 * @param app The application state.
 * @param id The item's MID_.
 * @param on Non-zero to check it.
 */
void ui_set_menu_check(EditorApp *app, ULONG id, int on);
/**
 * @brief Start collecting relayouts and page redraws (nestable).
 *
 * An operation that changes several things (opening a file: a new tab, its
 * page, the loaded text, the tab title) would otherwise lay out and redraw
 * the whole window several times, which is slow on real hardware.
 *
 * @param app The application state.
 */
void ui_begin_update(EditorApp *app);
/**
 * @brief Finish collecting: one relayout of the window if any was requested
 *        (it redraws everything), otherwise one redraw of the active page.
 * @param app The application state.
 */
void ui_end_update(EditorApp *app);
/**
 * @brief Display a simple modal error/notification requester with an OK button.
 *
 * @param app The application; its window is used as the requester parent (may
 *            be NULL).
 * @param title The requester title text.
 * @param message The requester body text.
 */
void ui_error(EditorApp *app, const char *title, const char *message);
/**
 * @brief Show the About window (as in Kickdown), with a requester as fallback.
 *
 * The details name the website, mail address and licence and the versions
 * of TinyDev's own gadget classes in use.
 *
 * @param app The application owning the main window.
 */
void ui_about(EditorApp *app);
/**
 * @brief Confirm closing a document, prompting to save unsaved changes.
 *
 * Returns immediately allowing the close for a document that is not dirty.
 * Otherwise presents a Save/Discard/Cancel requester: Discard allows the
 * close, Cancel blocks it, and Save saves to the document's existing path or
 * prompts for one before allowing the close.
 *
 * @param app The application owning the window used for the requester.
 * @param doc The document being closed.
 * @return Nonzero if the document may be closed, 0 to cancel the close.
 */
int ui_confirm_close(EditorApp *app, Document *doc);

/**
 * @brief Create, register and activate a new empty document; the window is
 *        laid out and drawn once at the end (see ui_begin_update()).
 * @param app The owning application.
 * @return The newly created document, or NULL on failure.
 */
Document *document_new(EditorApp *app);
/**
 * @brief Open a file; the window is laid out and redrawn only once at the
 *        end (see ui_begin_update()).
 * @param app The owning application.
 * @param path The file path to open.
 * @return The document showing the file, the pre-existing document for a
 *         duplicate, or NULL if opening failed.
 */
Document *document_open(EditorApp *app, const char *path);
/**
 * @brief Find an open document by its file path.
 *
 * Performs a case-insensitive comparison (Stricmp) of the given path against
 * the stored path of every open document.
 *
 * @param app The application whose documents are searched.
 * @param path The file path to look for.
 * @return The matching document, or NULL if no open document uses @p path.
 */
Document *document_find_path(EditorApp *app, const char *path);
/**
 * @brief Make the given document the active one and update the UI.
 *
 * Sets app->active, selects the document's tab, brings its page gadget to the
 * front, refreshes the UI, synchronizes the scrollers, and requests a minimap
 * update. Does nothing when @p doc is NULL.
 *
 * @param app The owning application.
 * @param doc The document to activate.
 */
void document_activate(EditorApp *app, Document *doc);
/**
 * @brief Close a document, optionally confirming unsaved changes.
 *
 * When @p ask is set the user is prompted and the close is aborted if declined.
 * Otherwise the document's page is removed from the pager, its tab and list
 * node are unlinked, tabs are renumbered, and all its resources (tab node,
 * page, path buffer, and the document itself) are freed. The next remaining
 * document is activated, or a fresh empty document is created when none remain,
 * followed by a relayout.
 *
 * @param app The owning application.
 * @param doc The document to close.
 * @param ask Non-zero to ask the user for confirmation before closing.
 * @return 1 if the document was closed, 0 if the close was cancelled or
 *         @p doc was NULL.
 */
int document_close(EditorApp *app, Document *doc, int ask);
/**
 * @brief Free every document without touching UI activation logic.
 *
 * Removes each document from the list in turn and releases its tab node, page
 * object, path buffer, and the document structure itself. Intended for teardown
 * where no replacement document should be created.
 *
 * @param app The application whose documents are all released.
 */
void document_free_all(EditorApp *app);
/**
 * @brief Set a document's modified (dirty) state and update its tab title.
 *
 * When the state actually changes, updates the tab title to append a '*' marker
 * for dirty documents, pushes the new label to the click-tab gadget as a minor
 * label change (the tab bar redraws itself) and updates the window title. The
 * window is not laid out again: that redrew everything on the first change of
 * a document and on every save. Does nothing when @p doc is NULL or already
 * in the requested state.
 *
 * @param app The owning application.
 * @param doc The document whose dirty state changes.
 * @param dirty Non-zero to mark the document modified, zero to mark it clean.
 */
void document_set_dirty(EditorApp *app, Document *doc, int dirty);
/**
 * @brief Remember the text as it is on disk (after loading or saving), so
 *        that undoing all changes makes the document unchanged again.
 * @param doc The document.
 * @param text The text as loaded or saved.
 * @param length Its length.
 * @param from_file Non-zero if the text was read from the file (it is
 *                  taken again from the editor once that is idle).
 */
void document_mark_saved(Document *doc, const char *text, long length, int from_file);
/**
 * @brief Note that the active document's text changed (the editor's
 *        GA_TEXTEDITOR_HasChanged was set): mark it modified and check it
 *        once the editor is idle (document_change_tick()).
 * @param app The application state.
 */
void document_changed(EditorApp *app);
/**
 * @brief On every IntuiTick: two ticks after the last change, check whether
 *        the active document equals its saved text again.
 * @param app The application state.
 */
void document_change_tick(EditorApp *app);
/**
 * @brief Mark a modified document as unchanged again if its text equals the
 *        one on disk.
 *
 * texteditor.gadget keeps GA_TEXTEDITOR_HasChanged set after its changes
 * were undone, so the exported text is compared with the checksum and
 * length remembered by document_mark_saved(). Called only after Undo and
 * Redo, as it exports the whole text.
 *
 * @param app The owning application.
 * @param doc The document (may be NULL).
 */
void document_check_unchanged(EditorApp *app, Document *doc);
/**
 * @brief Assign a file path to a document and derive its title and language.
 *
 * Duplicates the path, replaces any previous path, sets the document title from
 * the path's base name, and selects the syntax language from the path (updating
 * both the document and its highlighter). Toggling the dirty flag on then off
 * refreshes the tab title while leaving the document marked clean. Returns
 * without changes if the path copy could not be allocated.
 *
 * @param app The owning application.
 * @param doc The document to update.
 * @param path The new file path to associate with the document.
 */
void document_set_path(EditorApp *app, Document *doc, const char *path);
/**
 * @brief Synchronize the scrollers with the editor's current view.
 *
 * Queries the TextEditor's vertical and horizontal prop attributes (total
 * entries, visible amount, first line/column) and pushes them to the vertical
 * and horizontal scrollers, clamping zero totals/visibles to one. Does nothing
 * unless @p doc is the active document.
 *
 * @param app The owning application.
 * @param doc The document whose scrollers are synchronized.
 */
void document_sync_scrollers(EditorApp *app, Document *doc);
/**
 * @brief Apply the current scroller position to the active editor on one axis.
 *
 * Reads the top position from the chosen (horizontal or vertical) scroller and,
 * when it differs from the editor's current first line/column, applies it to
 * the editor and redraws only the editor gadget. Refreshing the whole page
 * would also redraw the scroller the user may be dragging, which then loses
 * its pressed look. The scrollers themselves are not written here (see
 * document_sync_scrollers()). Does nothing without an active document and
 * window.
 *
 * @param app The owning application.
 * @param horizontal Non-zero to scroll the horizontal axis, zero for vertical.
 */
void document_scroll(EditorApp *app, int horizontal);
/**
 * @brief Handle an intermediate scrollbar drag update.
 *
 * On the first live update the highlighter hook is switched off (setting the
 * shared scrolling flag) so the rapid redraws that follow the mouse stay fast;
 * the position of both the vertical and horizontal axes is then mirrored onto
 * the editor.
 *
 * @param app The owning application.
 */
void document_scroll_live(EditorApp *app);
/**
 * @brief Handle the end of a scrollbar interaction.
 *
 * Covers button release, arrow, and page-click events on the given axis
 * (GADGETUP) and the end found by document_scroll_tick(). After applying
 * the final position, re-enables highlighting if it was disabled during a
 * drag (clearing the scrolling flag) and forces a full page redraw so the
 * now-visible lines are highlighted again.
 *
 * @param app The owning application.
 * @param horizontal Non-zero for the horizontal axis, zero for vertical.
 */
void document_scroll_finish(EditorApp *app, int horizontal);
/**
 * @brief Finish a scrollbar interaction that sent no GADGETUP.
 *
 * Called for every IntuiTick (about ten per second while the window is
 * active). The scroller sends a GADGETUP only when the button is released
 * over it, and a last "moved" signal can arrive after the GADGETUP; either
 * left the highlighting switched off. So when two ticks pass without a
 * scroll signal, the scrolling counts as finished: both axes are applied
 * and the highlighting comes back. Pausing during a drag does the same,
 * the next movement switches it off again.
 *
 * @param app The owning application.
 */
void document_scroll_tick(EditorApp *app);
/**
 * @brief Suspend the syntax highlighter during an interactive minimap drag.
 *
 * Behaves exactly like a scrollbar drag, disabling the highlighter on the
 * active document so the rapid re-scrolls that follow the mouse stay fast. The
 * shared app->scrolling flag also tells minimap_poll() not to re-render the
 * viewport overlay until the drag has finished.
 *
 * @param app The owning application.
 */
void document_suspend_highlight(EditorApp *app);
/**
 * @brief Restore the syntax highlighter after an interactive minimap drag.
 *
 * Clears the shared scrolling flag, re-enables highlighting on the active
 * document, and forces a full page redraw so the now-visible lines are
 * highlighted again. Does nothing if no drag was in progress.
 *
 * @param app The owning application.
 */
void document_resume_highlight(EditorApp *app);

/**
 * @brief Create the minimap.gadget.
 *
 * Text is drawn in the editor's text pen, the visible part tinted with the
 * optional view pen; background and frame use the screen's pens like the
 * editor area. Notifications go to the window's IDCMP (IDCMP_IDCMPUPDATE)
 * and are picked up by the window's IDCMP hook.
 *
 * @param app The editor application.
 * @return The gadget, or NULL on failure.
 */
Object *minimap_create_gadget(EditorApp *app);
/**
 * @brief Set up a document's minimap colour hook.
 * @param doc The new document.
 */
void minimap_init_document(Document *doc);
/**
 * @brief Toggle the visibility of the minimap column in the content layout.
 *
 * Show/hide works like the folder tree: the minimap child always stays in the
 * layout and is only collapsed to zero width when hidden, never added or
 * removed at runtime and never via the WeightBar. A collapsed minimap has no
 * size, so minimap.gadget renders nothing while it is hidden. Showing it
 * always uses the default column width.
 *
 * @param app The editor application whose content layout is updated.
 * @param visible Non-zero to show the minimap column, zero to collapse it.
 */
void minimap_set_visible(EditorApp *app, int visible);
/**
 * @brief Tell the minimap that the text of the active document changed.
 * @param app The editor application.
 */
void minimap_request(EditorApp *app);
/**
 * @brief Keep the minimap up to date; call after every event batch.
 *
 * Connects the active document's editor and colour hook when the active
 * document changed, then lets minimap.gadget show a finished picture and
 * start a new render job if the text, the editor's scroll position or the
 * gadget's size changed. Skipped while the minimap is hidden and while a
 * scrollbar is dragged, so nothing is
 * re-rendered during scrolling; a minimap drag is the exception, there the
 * viewport box follows the pointer.
 *
 * @param app The editor application.
 */
void minimap_poll(EditorApp *app);
/**
 * @brief Disconnect a document before its editor is disposed.
 *
 * minimap.gadget waits for a running render job (which may still call the
 * document's colour hook) before it lets go of the editor.
 *
 * @param app The editor application.
 * @param doc The document about to be closed, or NULL to disconnect any.
 */
void minimap_forget_document(EditorApp *app, Document *doc);
/**
 * @brief Signal mask of finished minimap render jobs (for the main Wait()).
 * @param app The editor application.
 * @return The signal mask, or 0 without a minimap.
 */
ULONG minimap_signal_mask(EditorApp *app);
/**
 * @brief Record a minimap notification (from the window's IDCMP hook).
 *
 * The hook runs inside WM_HANDLEINPUT; the editor is scrolled afterwards by
 * minimap_handle_scroll() in the event loop.
 *
 * @param app The editor application.
 * @param tags The notification's tag list.
 * @return Non-zero if the tags came from the minimap.
 */
int minimap_handle_notify(EditorApp *app, struct TagItem *tags);
/**
 * @brief Scroll the editor to the line chosen in the minimap.
 *
 * At the start of a drag the syntax highlighter is suspended (as for a
 * scrollbar drag), at its end it is restored with a redraw.
 *
 * @param app The editor application.
 */
void minimap_handle_scroll(EditorApp *app);

/**
 * @brief Load a file's contents into a document.
 *
 * Canonicalises @p path, rejects files that are already open in another tab
 * (activating that tab instead), directories, oversized files (> 8 MiB) and
 * files containing embedded NUL bytes. On success the text is imported into
 * the TextEditor gadget, the imported line-ending style is recorded, the
 * document path is set and the dirty flag cleared. When a window exists the
 * tab page and minimap are refreshed.
 *
 * @param app The application state.
 * @param doc The document to load into.
 * @param path The file path to load.
 * @return Non-zero on success, zero on failure (an error requester is shown).
 */
int file_load(EditorApp *app, Document *doc, const char *path);
/**
 * @brief Save a document to a path using a safe temporary-file scheme.
 *
 * Exports the TextEditor contents, writes them to a sibling temporary file,
 * optionally renames any existing target to a backup, then renames the
 * temporary file into place and removes the backup. If any step fails the
 * original file is left untouched. Refuses to overwrite a file already open
 * in another tab and guards against pre-existing temporary/backup siblings.
 *
 * @param app The application state.
 * @param doc The document being saved.
 * @param path The destination path.
 * @return Non-zero on success, zero on failure (an error requester is shown).
 */
int file_save(EditorApp *app, Document *doc, const char *path);
/**
 * @brief Prompt the user for a file to open via an ASL requester.
 *
 * @param app The application state.
 * @return Non-zero if a document was opened, zero otherwise.
 */
int file_request_open(EditorApp *app);
/**
 * @brief Prompt the user for a destination and save a document via ASL.
 *
 * @param app The application state.
 * @param doc The document to save.
 * @return Non-zero if the document was saved, zero otherwise.
 */
int file_request_save(EditorApp *app, Document *doc);

/**
 * @brief Carry out a command of the Bookmarks menu on the active document.
 * @param app The application state.
 * @param id The MID_BM_ command.
 */
void bookmarks_command(EditorApp *app, ULONG id);
/**
 * @brief Set up a document's left bar hook (before its editor is created).
 * @param doc The new document.
 */
void bookmarks_init_document(Document *doc);
/**
 * @brief Set the background pen of the editors' left bar.
 * @param pen The pen (the screen's BACKGROUNDPEN).
 */
void bookmarks_set_background(UWORD pen);
/**
 * @brief Read a document's bookmarks again from its editor (they move with
 *        the text).
 * @param app The application state.
 * @param doc The document (may be NULL).
 */
void bookmarks_refresh(EditorApp *app, Document *doc);
/**
 * @brief Toggle the bookmark of a line clicked in the left bar and set the
 *        project's bookmarks in documents that were opened.
 * @param app The application state.
 */
void bookmarks_poll(EditorApp *app);
/**
 * @brief A directory was opened in the folder tree: save the bookmarks of
 *        the previous project and read those of the new one.
 * @param app The application state.
 */
void bookmarks_project_opened(EditorApp *app);
/**
 * @brief A file was loaded into a document: its bookmarks from the project
 *        are set one turn of the event loop later.
 * @param app The application state.
 * @param doc The document.
 */
void bookmarks_document_loaded(EditorApp *app, Document *doc);
/**
 * @brief A document is closed: keep its bookmarks in the project.
 * @param app The application state.
 * @param doc The document.
 */
void bookmarks_document_closing(EditorApp *app, Document *doc);
/* build.c */

/**
 * @brief Run a command of the Build menu: Build, Run, Debug (each the
 *        action's default command from the project settings) or Stop.
 *
 * Build saves the modified documents with a name and runs in the
 * background, its output in the output window; Run and Debug start in a
 * console window of their own. Problems are shown as messages.
 *
 * @param app The application state.
 * @param id MID_BUILD, MID_RUN, MID_DEBUG or MID_STOP.
 */
void build_action(EditorApp *app, ULONG id);
/**
 * @brief Stop the build that runs (Ctrl-C to its Shell).
 * @param app The application state.
 */
void build_stop(EditorApp *app);
/**
 * @brief Tell whether a build runs.
 * @return Non-zero if one runs.
 */
int build_running(void);
/**
 * @brief Return the signals a running build wakes the event loop with.
 * @return The mask, 0 if no build runs.
 */
ULONG build_sigmask(void);
/**
 * @brief Handle the signals of a running build: read its output, notice
 *        its end.
 * @param app The application state.
 * @param signals The signals received.
 */
void build_handle(EditorApp *app, ULONG signals);
/**
 * @brief Ask before quitting while a build runs.
 * @param app The application state.
 * @return Non-zero if TinyDev may quit (no build, or the user agreed to
 *         stop it).
 */
int build_may_quit(EditorApp *app);
/**
 * @brief Stop a running build and wait for its end; remove the scripts of
 *        Run and Debug. Called when TinyDev quits.
 * @param app The application state.
 */
void build_cleanup(EditorApp *app);

/* outwin.c */

/**
 * @brief Add text to the output: complete lines are shown, an unfinished
 *        one waits for the rest.
 * @param app The application state.
 * @param text The text (need not be terminated).
 * @param length Its length.
 */
void outwin_add_text(EditorApp *app, const char *text, long length);
/**
 * @brief Show an unfinished last line of the output.
 * @param app The application state.
 */
void outwin_flush(EditorApp *app);
/**
 * @brief Remove all lines of the output.
 * @param app The application state.
 */
void outwin_clear(EditorApp *app);
/**
 * @brief Set the status line of the output window.
 * @param text The text (copied).
 * @param running Non-zero while a build runs (Stop is not ghosted).
 */
void outwin_set_status(const char *text, int running);
/**
 * @brief Open the output window (or bring it to the front).
 * @param app The application state.
 */
void outwin_open(EditorApp *app);
/**
 * @brief Remember the box of the open output window (for the session).
 * @param app The application state.
 */
void outwin_remember(EditorApp *app);
/**
 * @brief Close the output window, keeping its box for the session (the
 *        lines stay).
 * @param app The application state.
 */
void outwin_close(EditorApp *app);
/**
 * @brief Tell whether the output window is open.
 * @return Non-zero if it is open.
 */
int outwin_is_open(void);
/**
 * @brief Return the signal mask of the output window.
 * @return The mask, 0 if it is closed.
 */
ULONG outwin_sigmask(void);
/**
 * @brief Handle the output window's input.
 * @param app The application state.
 */
void outwin_handle(EditorApp *app);
/**
 * @brief Close the output window and free its lines.
 * @param app The application state.
 */
void outwin_cleanup(EditorApp *app);

/* projdata.c */

/**
 * @brief Build the name of a file in a project's directory .tinydev.
 * @param root The project directory.
 * @param name The file name, or NULL for the directory itself.
 * @param out The buffer.
 * @param size Its size.
 * @return Non-zero on success (zero without a project or if too long).
 */
int projdata_path(const char *root, const char *name, char *out, LONG size);
/**
 * @brief Read a file of a project's directory .tinydev whole.
 * @param root The project directory.
 * @param name The file name.
 * @param[out] text The contents, NUL-terminated (free() it); NULL unless
 *                  1 is returned.
 * @return 1 if read; 0 if there is no such file; -1 if it cannot be read
 *         (also when too large or out of memory).
 */
int projdata_read(const char *root, const char *name, char **text);
/**
 * @brief Tell whether a file of a project's directory .tinydev exists.
 * @param root The project directory.
 * @param name The file name.
 * @return Non-zero if it exists.
 */
int projdata_exists(const char *root, const char *name);
/**
 * @brief Write a file of a project's directory .tinydev through a
 *        temporary file; the directory is made if needed.
 * @param root The project directory.
 * @param name The file name.
 * @param text The contents.
 * @return Non-zero on success (a failed write leaves the old file).
 */
int projdata_write(const char *root, const char *name, const char *text);

/** A bookmark as the bookmark list shows it (bookmarks_collect()). */
typedef struct BookmarkItem {
    Document *doc;      /**< the open document, NULL for a closed file of the project */
    char *path;         /**< full name of the file, NULL for a document without one */
    char *name;         /**< shown name: relative to the project, or the title */
    char *file;         /**< relative to the project, NULL outside of it */
    long line;          /**< counted from 0 */
    char *text;         /**< the line's text (shortened) */
} BookmarkItem;

/**
 * @brief Collect the bookmarks of the active document, the open documents
 *        or the project (open and closed files), sorted by file and line.
 * @param app The application state.
 * @param scope BMSCOPE_CURRENT, BMSCOPE_OPEN or BMSCOPE_PROJECT.
 * @param items Set to the array (bookmarks_free_items()), NULL if none.
 * @return The number of bookmarks.
 */
long bookmarks_collect(EditorApp *app, int scope, BookmarkItem **items);
/**
 * @brief Free an array of bookmarks_collect().
 * @param items The array (may be NULL).
 * @param count Its size.
 */
void bookmarks_free_items(BookmarkItem *items, long count);
/**
 * @brief Go to a bookmark: activate its document, or open its file, and
 *        put the cursor on its line.
 * @param app The application state.
 * @param item The bookmark.
 */
void bookmarks_go_to(EditorApp *app, const BookmarkItem *item);
/**
 * @brief Remove a bookmark (in its editor, or from the project's file).
 * @param app The application state.
 * @param item The bookmark.
 */
void bookmarks_remove(EditorApp *app, const BookmarkItem *item);
/**
 * @brief A number that changes whenever a bookmark was set, removed or
 *        moved, or a document came or went (for the bookmark list).
 * @return The number.
 */
ULONG bookmarks_generation(void);
/**
 * @brief Write the project's bookmarks file (after a document was saved).
 * @param app The application state.
 */
void bookmarks_save(EditorApp *app);
/**
 * @brief Save the project's bookmarks and free them; call before the
 *        documents are freed.
 * @param app The application state.
 */
void bookmarks_cleanup(EditorApp *app);

/**
 * @brief Where a tool window opens: its remembered box (pushed into the
 *        screen), or next to the main window (right of it if there is
 *        room, otherwise over its right edge; or below it).
 * @param app The application state.
 * @param saved The remembered box (left, top, width, height; width 0 =
 *              none).
 * @param min_width Smallest width.
 * @param min_height Smallest height.
 * @param below Non-zero for a wide window below the main window (as wide
 *              as it, over its lower edge if the screen has no room).
 * @param box Out: left, top, width, height.
 */
void toolwin_box(EditorApp *app, const long saved[4], WORD min_width, WORD min_height,
                 int below, WORD box[4]);
/**
 * @brief Remember the box of a tool window.
 * @param win The window (may be NULL).
 * @param saved Out: left, top, width, height.
 */
void toolwin_remember(struct Window *win, long saved[4]);

/**
 * @brief Start the HelpViewer (PROGDIR:HelpViewer) on the help pages in
 *        PROGDIR:Help, independent of TinyDev (it stays open when TinyDev
 *        quits), as TinyDev's single viewer (PORTNAME TinyDev). Shows a
 *        requester if that fails.
 * @param app The application state.
 */
void help_open(EditorApp *app);

/**
 * @brief Open the bookmark list (or bring it to the front).
 * @param app The application state.
 */
void bmlist_open(EditorApp *app);
/**
 * @brief Close the bookmark list, remembering its box.
 * @param app The application state.
 */
void bmlist_close(EditorApp *app);
/**
 * @brief Tell whether the bookmark list is open.
 * @return Non-zero if it is.
 */
int bmlist_is_open(void);
/**
 * @brief Remember the box of the open bookmark list for this session.
 * @param app The application state.
 */
void bmlist_remember(EditorApp *app);
/**
 * @brief Return the signal mask of the bookmark list (0 if closed).
 * @return The signal mask to include in the main Wait().
 */
ULONG bmlist_sigmask(void);
/**
 * @brief Handle the input of the bookmark list.
 * @param app The application state.
 */
void bmlist_handle(EditorApp *app);
/**
 * @brief Build the bookmark list again if the bookmarks (or, for the
 *        active file, the active document) changed; call on every turn of
 *        the event loop.
 * @param app The application state.
 */
void bmlist_update(EditorApp *app);
/**
 * @brief Close the bookmark list and release its pens. Call before the
 *        documents are freed.
 * @param app The application state.
 */
void bmlist_cleanup(EditorApp *app);

/** A step-by-step walk through the files of a directory tree (filescan.c). */
typedef struct FileScan FileScan;
/**
 * @brief Start walking through the files below a directory (at most 16
 *        levels deep; links and icon files are skipped).
 * @param root The directory.
 * @param pattern AmigaDOS pattern for the file names (NULL or "" for all).
 * @return The walk, or NULL if the directory cannot be read, the pattern
 *         is invalid or there is not enough memory.
 */
FileScan *filescan_start(const char *root, const char *pattern);
/**
 * @brief Go on with a walk: looks at a few directory entries.
 * @param scan The walk.
 * @param path The buffer for the full name of the next file.
 * @param size Its size.
 * @return 1 if a file was found, 0 if not yet (call again), -1 when the
 *         walk is complete.
 */
int filescan_next(FileScan *scan, char *path, long size);
/**
 * @brief Tell whether directories were skipped (too deep, unreadable).
 * @param scan The walk.
 * @return Non-zero if the walk is incomplete.
 */
int filescan_incomplete(const FileScan *scan);
/**
 * @brief End a walk and free it.
 * @param scan The walk (may be NULL).
 */
void filescan_end(FileScan *scan);

/**
 * @brief Open the find and replace window (or bring it to the front).
 * @param app The application state.
 */
void find_open(EditorApp *app);
/**
 * @brief Repeat the last search in the active document without the
 *        window (opens it if there was no search yet).
 * @param app The application state.
 */
void find_next(EditorApp *app);
/**
 * @brief Return the signal mask of the find window (0 if it is closed).
 * @return The signal mask to include in the main Wait().
 */
ULONG find_sigmask(void);
/**
 * @brief Handle the input of the find window.
 * @param app The application state.
 */
void find_handle(EditorApp *app);
/**
 * @brief Forget the last hit if it was in a document that is closed.
 * @param doc The document.
 */
void find_forget_document(Document *doc);
/**
 * @brief Close and dispose the find window and close string.gadget; call
 *        before the main window is closed.
 */
void find_cleanup(void);
/**
 * @brief Close the find window while the main window is iconified,
 *        remembering its box; a project search pauses.
 * @return Non-zero if the window was open.
 */
int find_hide(void);
/**
 * @brief Open the find window again after find_hide(), in its box, and
 *        let a paused project search go on.
 * @param app The application state.
 */
void find_show(EditorApp *app);

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
int tree_request_directory(EditorApp *app);
/**
 * @brief Handle a GADGETUP of the folder tree.
 *
 * Lets dirtree.gadget process the event on this process (it may read a
 * directory, which is not allowed during input handling). A double-clicked
 * file is opened as a document; read errors are reported.
 *
 * @param app The editor application state.
 */
void tree_handle_event(EditorApp *app);
/**
 * @brief Return the project directory (the folder tree's root) in its
 *        full form.
 * @param app The application state.
 * @param out The buffer for the name.
 * @param size Its size.
 * @return Non-zero if there is a project.
 */
int tree_project_root(EditorApp *app, char *out, LONG size);
/**
 * @brief Return the signal mask of the folder tree's directory
 *        notifications (0 if it does not watch).
 * @param app The application state.
 * @return The signal mask to include in the main Wait().
 */
ULONG tree_signal_mask(EditorApp *app);
/**
 * @brief Bring the folder tree up to date.
 * @param app The application state.
 * @param all Non-zero to read every shown directory again (Refresh Folder
 *            Tree), zero for those DOS reported changed.
 */
void tree_refresh(EditorApp *app, int all);
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
void tree_set_visible(EditorApp *app, int visible);

/**
 * @brief Make the name of a temporary file: TEMPDIR/TinyDev-PROCESS-N plus
 *        a suffix (PROCESS: TinyDev's process in hex, N: a counter), so
 *        no other program or TinyDev running in parallel uses it.
 * @param app The application state (Settings::tempdir).
 * @param suffix The end of the name, e.g. ".script".
 * @param[out] out The name.
 * @param size Size of @p out.
 * @return Non-zero on success (0 if the name does not fit).
 */
int temp_name(EditorApp *app, const char *suffix, char *out, size_t size);

/**
 * @brief The project (the folder tree's root) changed: close Scout's
 *        session of the old one and open one for the new (starting Scout
 *        if needed).
 * @param app The application state.
 */
void scoutlink_project_opened(EditorApp *app);
/**
 * @brief The settings changed: Scout turned on or off.
 * @param app The application state.
 */
void scoutlink_settings_changed(EditorApp *app);
/**
 * @brief A document's text changed: analyse it after a pause in typing.
 * @param doc The document.
 */
void scoutlink_document_changed(Document *doc);
/**
 * @brief A document was loaded or saved: analyse the file at once.
 * @param doc The document.
 */
void scoutlink_document_saved(Document *doc);
/**
 * @brief A document is closed: drop its snapshot (in Scout and on disk).
 * @param app The application state.
 * @param doc The document.
 */
void scoutlink_document_closing(EditorApp *app, Document *doc);
/**
 * @brief Return the signals of Scout's answers and of the client's timer.
 * @return The mask to include in the main Wait(), 0 if none.
 */
ULONG scoutlink_sigmask(void);
/**
 * @brief Handle Scout's answers and the client's timer (waiting for a
 *        starting Scout, the pause in typing, sending the analyses).
 * @param app The application state.
 */
void scoutlink_handle(EditorApp *app);
/**
 * @brief Close the session, delete the snapshots and wait for the answers
 *        still on their way. Called when TinyDev quits, before the
 *        documents are freed.
 * @param app The application state.
 */
void scoutlink_cleanup(EditorApp *app);

/**
 * @brief Ask Scout for symbols: those of a document, or those of the
 *        project; the answer goes to symlist_symbols().
 * @param app The application state.
 * @param doc The document, NULL for the project.
 * @return Non-zero if the request was sent (0: Scout not ready, no
 *         session for the project, or the document has no file).
 */
int scoutlink_request_symbols(EditorApp *app, Document *doc);
/**
 * @brief Return a number that changes whenever the project's symbols may
 *        have changed (a file was analysed from disk, a session opened).
 * @return The number.
 */
unsigned long scoutlink_project_generation(void);
/**
 * @brief Tell whether Scout is indexing the project.
 * @return -1 if not; otherwise the number of jobs waiting (0 while not
 *         known yet).
 */
long scoutlink_indexing(void);
/**
 * @brief Tell why Scout cannot be asked now.
 * @param app The application state.
 * @return 0 if it can; 1 if the setting is off; 2 while it starts or is
 *         not reachable.
 */
int scoutlink_unavailable(EditorApp *app);

/**
 * @brief Open the symbol list, a tool window (or bring it to the front).
 * @param app The application state.
 */
void symlist_open(EditorApp *app);
/**
 * @brief Close the symbol list (remembering its box).
 * @param app The application state.
 */
void symlist_close(EditorApp *app);
/**
 * @brief Tell whether the symbol list is open.
 * @return Non-zero if it is.
 */
int symlist_is_open(void);
/**
 * @brief Remember the symbol list's box in EditorApp::sym_box.
 * @param app The application state.
 */
void symlist_remember(EditorApp *app);
/**
 * @brief Return the symbol list's signal mask (0 when closed).
 * @return The mask.
 */
ULONG symlist_sigmask(void);
/**
 * @brief Handle the symbol list's input.
 * @param app The application state.
 */
void symlist_handle(EditorApp *app);
/**
 * @brief Ask for new symbols when the active document or its analysis
 *        changed (called every turn of the event loop).
 * @param app The application state.
 */
void symlist_update(EditorApp *app);
/**
 * @brief Take Scout's answer to SYMBOLS (JSON) into the list.
 * @param app The application state.
 * @param doc_number The document it is for, 0 for the project.
 * @param json The answer's text, NULL if the request failed.
 */
void symlist_symbols(EditorApp *app, unsigned long doc_number, const char *json);
/**
 * @brief Close the symbol list and free what it holds.
 * @param app The application state.
 */
void symlist_cleanup(EditorApp *app);

/**
 * @brief Close all documents, asking first about each changed one; an
 *        empty untitled document is left.
 * @param app The application state.
 * @return 1 if they were closed, 0 if the user cancelled (none is closed
 *         then).
 */
int document_close_all(EditorApp *app);

/**
 * @brief Go to a position in a file: activate its document or open it,
 *        and put the cursor there once the document is shown
 *        (document_goto_poll()).
 * @param app The application state.
 * @param path The file, NULL for the active document.
 * @param line The line (1-based).
 * @param col The column (1-based).
 */
void document_go_to(EditorApp *app, const char *path, long line, long col);
/**
 * @brief Put the cursor where document_go_to() asked, once the active
 *        document is shown (called every turn of the event loop).
 * @param app The application state.
 */
void document_goto_poll(EditorApp *app);

/**
 * @brief Ask Scout for diagnostics: those of a document (they go to
 *        diag_set_document()), or those of the project (diaglist_project()).
 * @param app The application state.
 * @param doc The document, NULL for the project.
 * @return Non-zero if the request was sent.
 */
int scoutlink_request_diagnostics(EditorApp *app, Document *doc);

/**
 * @brief Obtain the pens of the left bar's marks (once the screen is known).
 * @param app The application state.
 */
void diag_open(EditorApp *app);
/**
 * @brief Release the pens of the marks.
 * @param app The application state.
 */
void diag_close(EditorApp *app);
/**
 * @brief Take Scout's answer to DIAGNOSTICS for a document: its list and
 *        the marks of the left bar (redrawn if they changed).
 * @param app The application state.
 * @param doc The document.
 * @param json The answer's text, NULL to drop the diagnostics.
 */
void diag_set_document(EditorApp *app, Document *doc, const char *json);
/**
 * @brief Free a document's diagnostics (when it is closed).
 * @param doc The document.
 */
void diag_free_document(Document *doc);
/**
 * @brief Return the worst diagnostic of a line.
 * @param doc The document.
 * @param line The line (1-based).
 * @return The diagnostic, or NULL.
 */
const DiagItem *diag_at_line(const Document *doc, long line);
/**
 * @brief Draw the mark of a line into the left bar, if it has one (called
 *        by the left bar hook: graphics only, no allocation).
 * @param doc The document.
 * @param rp The RastPort, positioned at the top left of the line's bar.
 * @param line The line (from 0).
 * @param width Width of the bar.
 * @param height Height of a line.
 */
void diag_draw_mark(const Document *doc, struct RastPort *rp, LONG line, WORD width, WORD height);
/**
 * @brief Return the pen of a severity's mark (also for the problem list).
 * @param severity SCOUT_SEVERITY_ value.
 * @param fallback The pen if none could be obtained.
 * @return The pen.
 */
UWORD diag_pen(int severity, UWORD fallback);

/** @brief Open the problem list (or bring it to the front). @param app The application state. */
void diaglist_open(EditorApp *app);
/** @brief Close the problem list (remembering its box). @param app The application state. */
void diaglist_close(EditorApp *app);
/** @brief Tell whether the problem list is open. @return Non-zero if it is. */
int diaglist_is_open(void);
/** @brief Remember the problem list's box in EditorApp::diag_box. @param app The application state. */
void diaglist_remember(EditorApp *app);
/** @brief Return the problem list's signal mask (0 when closed). @return The mask. */
ULONG diaglist_sigmask(void);
/** @brief Handle the problem list's input. @param app The application state. */
void diaglist_handle(EditorApp *app);
/**
 * @brief Show what changed (another document, new diagnostics, a new
 *        project state); called every turn of the event loop.
 * @param app The application state.
 */
void diaglist_update(EditorApp *app);
/**
 * @brief Take Scout's answer to DIAGNOSTICS of the project (JSON).
 * @param app The application state.
 * @param json The answer's text, NULL if the request failed.
 */
void diaglist_project(EditorApp *app, const char *json);
/** @brief Close the problem list and free what it holds. @param app The application state. */
void diaglist_cleanup(EditorApp *app);

/**
 * @brief Show a message in the status line for a few seconds (instead of
 *        the counts).
 * @param app The application state.
 * @param text The message (copied), NULL to remove the message at once.
 */
void ui_status_message(EditorApp *app, const char *text);

/**
 * @brief Ask Scout where a name used at a position is defined; the answer
 *        goes to navigate_definition_answer().
 * @param app The application state.
 * @param doc The document (with a file).
 * @param line The position's line (1-based).
 * @param col Its column (1-based).
 * @param name The name.
 * @return Non-zero if the request was sent.
 */
int scoutlink_request_definition(EditorApp *app, Document *doc, long line, long col, const char *name);

/**
 * @brief Edit/Go to Definition: ask Scout where the name at the cursor is
 *        defined; again at the place it went to: the next definition.
 * @param app The application state.
 */
void navigate_definition(EditorApp *app);
/**
 * @brief Take Scout's answer to DEFINITION: go to the first definition
 *        (noting the place for Go Back), or say that there is none.
 * @param app The application state.
 * @param json The answer, NULL if the request failed.
 */
void navigate_definition_answer(EditorApp *app, const char *json);
/**
 * @brief Edit/Go Back: return to where the last jump came from.
 * @param app The application state.
 */
void navigate_back(EditorApp *app);
/** @brief Free the places noted for Go Back. */
void navigate_cleanup(void);

#endif
