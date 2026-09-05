#ifndef AMIEDITOR_EDITOR_H
#define AMIEDITOR_EDITOR_H

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

enum GadgetId {
    GID_TABS = 1,
    GID_TREE = 2,
    GID_VSCROLL = 3,
    GID_HSCROLL = 4,
    GID_NEW = 10, GID_OPEN, GID_SAVE, GID_UNDO, GID_REDO,
    GID_CUT, GID_COPY, GID_PASTE,
    GID_LAST
};
enum MenuId {
    MID_NEW = 1, MID_OPEN, MID_OPEN_DIRECTORY, MID_SAVE, MID_SAVE_AS, MID_CLOSE, MID_QUIT,
    MID_UNDO, MID_REDO, MID_CUT, MID_COPY, MID_PASTE, MID_SELECT_ALL,
    MID_FOLDER_TREE, MID_LINE_NUMBERS, MID_FONT
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
    Object *tab_close_image;
    Object *tabs;
    Object *pages;
    Object *toolbar_images[8];
    struct Window *window;
    struct Screen *screen;
    struct TextFont *font;
    struct TextAttr font_attr;
    char *font_name;
    char *tree_root;
    unsigned long next_document;
    long scroll_signal;
    int line_numbers;
    int tree_visible;
    int running;
} EditorApp;

extern struct Library *AslBase, *DiskfontBase, *GadToolsBase, *IconBase, *UtilityBase;
extern struct Library *WindowBase, *LayoutBase, *ClickTabBase, *TextFieldBase;
extern struct Library *ButtonBase, *BitMapBase;
extern struct Library *ListBrowserBase;
extern struct Library *GlyphBase;
extern struct Library *ScrollerBase;

int app_open_libraries(int from_workbench);
void app_close_libraries(void);
int ui_create(EditorApp *app);
int ui_run(EditorApp *app);
void ui_destroy(EditorApp *app);
void ui_refresh(EditorApp *app);
void ui_relayout(EditorApp *app);
void ui_error(EditorApp *app, const char *title, const char *message);
int ui_confirm_close(EditorApp *app, Document *doc);

Document *document_new(EditorApp *app);
Document *document_open(EditorApp *app, const char *path);
Document *document_find_path(EditorApp *app, const char *path);
void document_activate(EditorApp *app, Document *doc);
int document_close(EditorApp *app, Document *doc, int ask);
void document_free_all(EditorApp *app);
void document_set_dirty(EditorApp *app, Document *doc, int dirty);
void document_set_path(EditorApp *app, Document *doc, const char *path);
void document_sync_scrollers(EditorApp *app, Document *doc);
void document_scroll(EditorApp *app, int horizontal);

int file_load(EditorApp *app, Document *doc, const char *path);
int file_save(EditorApp *app, Document *doc, const char *path);
int file_request_open(EditorApp *app);
int file_request_save(EditorApp *app, Document *doc);

int font_open_default(EditorApp *app);
int font_request(EditorApp *app);
void font_apply_all(EditorApp *app);
void font_close(EditorApp *app);

int tree_request_directory(EditorApp *app);
void tree_clear(EditorApp *app);
void tree_handle_event(EditorApp *app);
void tree_set_visible(EditorApp *app, int visible);

#endif
