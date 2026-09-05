#include "editor.h"

#include <proto/clicktab.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/texteditor.h>
#include <proto/utility.h>
#include <reaction/reaction.h>
#include <gadgets/clicktab.h>
#include <gadgets/layout.h>
#include <gadgets/texteditor.h>

#include <stdio.h>
#include <string.h>

static char *copy_string(const char *s)
{
    size_t n = strlen(s) + 1;
    char *copy = AllocVec((ULONG)n, MEMF_ANY);
    if (copy != NULL) memcpy(copy, s, n);
    return copy;
}

static const char *base_name(const char *path)
{
    const char *p, *best = path;
    for (p = path; *p != '\0'; ++p) if (*p == '/' || *p == ':') best = p + 1;
    return *best != '\0' ? best : path;
}

Document *document_new(EditorApp *app)
{
    Document *doc = AllocVec(sizeof(*doc), MEMF_ANY | MEMF_CLEAR);
    if (doc == NULL) { ui_error(app, "AmiEditor", "Not enough memory for a document."); return NULL; }
    doc->number = ++app->next_document;
    snprintf(doc->title, sizeof(doc->title), "Untitled %lu", doc->number);
    strcpy(doc->tab_title, doc->title);
    doc->eol = EOL_LF;
    doc->language = SYNTAX_PLAIN;
    syntax_init_hook(&doc->highlight, doc->language);
    doc->editor = NewObject(TEXTEDITOR_GetClass(), NULL,
        GA_ID, (ULONG)(100 + doc->number),
        GA_RelVerify, TRUE,
        GA_TextAttr, (ULONG)(app->font != NULL ? &app->font_attr : NULL),
        GA_TEXTEDITOR_Contents, (ULONG)"",
        GA_TEXTEDITOR_FixedFont, TRUE,
        GA_TEXTEDITOR_ShowLineNumbers, (ULONG)app->line_numbers,
        GA_TEXTEDITOR_HighlighterHook, (ULONG)&doc->highlight.hook,
        GA_TEXTEDITOR_LineEndingExport, LINEENDING_ASIMPORT,
        TAG_END);
    if (doc->editor == NULL) { FreeVec(doc); ui_error(app, "AmiEditor", "Could not create TextEditor gadget."); return NULL; }
    doc->tab = AllocClickTabNode(TNA_Text, (ULONG)doc->tab_title, TNA_Number,
        (WORD)(doc->number & 0x7fffUL), TNA_UserData, (ULONG)doc,
        TNA_CloseGadget, TRUE, TAG_END);
    if (doc->tab == NULL) { DisposeObject(doc->editor); FreeVec(doc); ui_error(app, "AmiEditor", "Could not create tab."); return NULL; }
    AddTail(&app->documents, &doc->node);
    AddTail(&app->tab_nodes, doc->tab);
    if (app->pages != NULL) SetAttrs(app->pages, PAGE_Add, (ULONG)doc->editor, TAG_END);
    if (app->tabs != NULL) SetAttrs(app->tabs, CLICKTAB_Labels, (ULONG)&app->tab_nodes, TAG_END);
    document_activate(app, doc);
    return doc;
}

Document *document_find_path(EditorApp *app, const char *path)
{
    Document *doc;
    for (doc = (Document *)app->documents.lh_Head; doc->node.ln_Succ;
         doc = (Document *)doc->node.ln_Succ)
        if (doc->path != NULL && Stricmp(doc->path, path) == 0) return doc;
    return NULL;
}

Document *document_open(EditorApp *app, const char *path)
{
    Document *doc = document_find_path(app, path), *selected;
    int duplicate;
    if (doc != NULL) { document_activate(app, doc); return doc; }
    doc = document_new(app);
    if (doc == NULL) return NULL;
    if (!file_load(app, doc, path)) {
        selected = app->active;
        duplicate = selected != doc;
        document_close(app, doc, 0);
        if (duplicate) { document_activate(app, selected); return selected; }
        return NULL;
    }
    return doc;
}

void document_activate(EditorApp *app, Document *doc)
{
    ULONG index = 0; Document *scan;
    if (doc == NULL) return;
    app->active = doc;
    for (scan = (Document *)app->documents.lh_Head; scan->node.ln_Succ;
         scan = (Document *)scan->node.ln_Succ, ++index) if (scan == doc) break;
    if (app->pages != NULL) SetAttrs(app->pages, PAGE_Current, index, TAG_END);
    if (app->tabs != NULL) SetAttrs(app->tabs, CLICKTAB_CurrentNode, (ULONG)doc->tab, TAG_END);
    ui_refresh(app);
}

void document_set_dirty(EditorApp *app, Document *doc, int dirty)
{
    if (doc == NULL || doc->dirty == dirty) return;
    doc->dirty = dirty;
    snprintf(doc->tab_title, sizeof(doc->tab_title), "%s%s", doc->title, dirty ? "*" : "");
    SetClickTabNodeAttrs(doc->tab, TNA_Text, (ULONG)doc->tab_title, TAG_END);
    if (app->tabs != NULL) SetAttrs(app->tabs, CLICKTAB_MinorLabelChange, TRUE,
                                    CLICKTAB_Labels, (ULONG)&app->tab_nodes, TAG_END);
    ui_refresh(app);
}

int document_close(EditorApp *app, Document *doc, int ask)
{
    Document *next;
    if (doc == NULL || (ask && !ui_confirm_close(app, doc))) return 0;
    next = doc->node.ln_Succ->ln_Succ ? (Document *)doc->node.ln_Succ :
           (doc->node.ln_Pred->ln_Pred ? (Document *)doc->node.ln_Pred : NULL);
    if (app->pages != NULL) SetAttrs(app->pages, PAGE_Remove, (ULONG)doc->editor, TAG_END);
    Remove(doc->tab); Remove(&doc->node);
    FreeClickTabNode(doc->tab);
    DisposeObject(doc->editor);
    if (doc->path != NULL) FreeVec(doc->path);
    FreeVec(doc);
    if (app->tabs != NULL) SetAttrs(app->tabs, CLICKTAB_Labels, (ULONG)&app->tab_nodes, TAG_END);
    if (next != NULL) document_activate(app, next);
    else document_new(app);
    return 1;
}

void document_free_all(EditorApp *app)
{
    Document *doc;
    while ((doc = (Document *)RemHead(&app->documents)) != NULL) {
        if (doc->tab != NULL) { Remove(doc->tab); FreeClickTabNode(doc->tab); }
        if (app->pages != NULL && doc->editor != NULL)
            SetAttrs(app->pages, PAGE_Remove, (ULONG)doc->editor, TAG_END);
        if (doc->editor != NULL) DisposeObject(doc->editor);
        if (doc->path != NULL) FreeVec(doc->path);
        FreeVec(doc);
    }
}

void document_set_path(EditorApp *app, Document *doc, const char *path)
{
    char *copy = copy_string(path);
    if (copy == NULL) return;
    if (doc->path != NULL) FreeVec(doc->path);
    doc->path = copy;
    strncpy(doc->title, base_name(path), sizeof(doc->title) - 1);
    doc->title[sizeof(doc->title) - 1] = '\0';
    doc->language = syntax_language_for_path(path);
    doc->highlight.language = doc->language;
    document_set_dirty(app, doc, 1);
    document_set_dirty(app, doc, 0);
}
