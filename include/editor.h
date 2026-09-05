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

enum GadgetId { GID_TABS = 1, GID_LAST };
enum MenuId {
    MID_NEW = 1, MID_OPEN, MID_SAVE, MID_SAVE_AS, MID_CLOSE, MID_QUIT,
    MID_UNDO, MID_REDO, MID_CUT, MID_COPY, MID_PASTE, MID_SELECT_ALL,
    MID_LINE_NUMBERS, MID_FONT
};

typedef enum LineEnding { EOL_LF = 0, EOL_CR = 1, EOL_CRLF = 2 } LineEnding;

typedef struct Document {
    struct Node node;
    Object *editor;
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
    Document *active;
    Object *window_object;
    Object *layout;
    Object *tabs;
    Object *pages;
    struct Window *window;
    struct TextFont *font;
    struct TextAttr font_attr;
    char *font_name;
    unsigned long next_document;
    int line_numbers;
    int running;
} EditorApp;

extern struct Library *AslBase, *DiskfontBase, *GadToolsBase, *IconBase, *UtilityBase;
extern struct Library *WindowBase, *LayoutBase, *ClickTabBase, *TextFieldBase;

int app_open_libraries(void);
void app_close_libraries(void);
int ui_create(EditorApp *app);
int ui_run(EditorApp *app);
void ui_destroy(EditorApp *app);
void ui_refresh(EditorApp *app);
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

int file_load(EditorApp *app, Document *doc, const char *path);
int file_save(EditorApp *app, Document *doc, const char *path);
int file_request_open(EditorApp *app);
int file_request_save(EditorApp *app, Document *doc);

int font_open_default(EditorApp *app);
int font_request(EditorApp *app);
void font_apply_all(EditorApp *app);
void font_close(EditorApp *app);

#endif
