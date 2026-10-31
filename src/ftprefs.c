/**
 * @file ftprefs.c
 * @brief The settings per file type: PROGDIR:TinyDev.json and the
 *        editors that follow them.
 *
 * The settings (filetypes.c) are kept in TinyDev's drawer, not in the tool
 * types: they are a table per language that people may edit by hand. A
 * file that cannot be read is reported at the start and then never
 * written, so nothing in it is lost; TinyDev uses the built-in settings.
 *
 * A document's editor gets the settings of its language
 * (scoutlink_document_language()) whenever that changes, which
 * scoutlink.c checks with the highlighting rules, and again when the
 * settings change. Line numbers belong to them too; View/Line Numbers
 * switches them for the active document until its file type or the
 * settings change.
 *
 * GA_TEXTEDITOR_AutoIndent and GA_TEXTEDITOR_CutCopyLineWhenNoSelection
 * are only in the V47 header, not in the Autodoc (untested assumption:
 * they work as TextEdit's file type options).
 */
#include "editor.h"

#include <gadgets/texteditor.h>
#include <proto/intuition.h>
#include <proto/layout.h>

#include <stdlib.h>
#include <string.h>

/** The file of the settings. */
#define FTPREFS_FILE "PROGDIR:" APP_NAME ".json"

/** The file could not be read at the start: it is never written. */
static int unreadable;

int ftprefs_load(EditorApp *app)
{
    char *text = NULL;
    int r = datafile_read(FTPREFS_FILE, &text);
    filetypes_init(&app->filetypes);
    if (r > 0 && filetypes_parse(&app->filetypes, text) <= 0) r = -1;
    free(text);
    unreadable = r < 0;
    return !unreadable;
}

int ftprefs_save(const FileTypes *ft)
{
    char *text = NULL, *json = NULL;
    FileTypes check;
    int r, ok = 0;
    if (unreadable) return 0;
    r = datafile_read(FTPREFS_FILE, &text);
    filetypes_init(&check);
    /* changed by hand meanwhile into something unreadable: kept too */
    if (r == 0 || (r > 0 && filetypes_parse(&check, text) > 0))
        ok = (json = filetypes_format(ft, text)) != NULL && datafile_write(FTPREFS_FILE, json);
    filetypes_clear(&check);
    free(json);
    free(text);
    return ok;
}

void ftprefs_apply(EditorApp *app, Document *doc, int force)
{
    const char *id = scoutlink_document_language(doc);
    const FileTypeOptions *o;
    int numbers_changed;

    if (!force && doc->ft_applied && strcmp(id, doc->ft_language) == 0) return;
    strncpy(doc->ft_language, id, sizeof(doc->ft_language) - 1);
    doc->ft_language[sizeof(doc->ft_language) - 1] = '\0';
    doc->ft_applied = 1;
    o = filetypes_get(&app->filetypes, doc->ft_language);
    numbers_changed = doc->line_numbers != o->line_numbers;
    doc->line_numbers = o->line_numbers;
    /* editors of hidden pages without a window: they draw when shown */
    SetAttrs(doc->editor,
        GA_TEXTEDITOR_TabSize, (ULONG)o->tab_size,
        GA_TEXTEDITOR_IndentWidth, (ULONG)o->indent,
        GA_TEXTEDITOR_AutoIndent, (ULONG)o->auto_indent,
        GA_TEXTEDITOR_TabKeyPolicy, (ULONG)(o->tab_after ? GV_TEXTEDITOR_TabKey_IndentsAfter
                                                          : GV_TEXTEDITOR_TabKey_IndentsLine),
        GA_TEXTEDITOR_CutCopyLineWhenNoSelection, (ULONG)o->cut_copy_line,
        GA_TEXTEDITOR_ShowLineNumbers, (ULONG)o->line_numbers,
        TAG_END);
    if (doc == app->active && app->window != NULL) {
        if (numbers_changed) ui_relayout(app);
        RefreshPageGadget((struct Gadget *)doc->page, app->pages, app->window, NULL);
    }
}
