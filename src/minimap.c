/**
 * @file minimap.c
 * @brief Connects minimap.gadget to the editor: active document, syntax colours, re-render requests, scrolling from the minimap.
 *
 * The minimap itself (render process, coalesced jobs, scaled picture,
 * mouse handling) lives in minimap.gadget (src/classes/minimap/). This
 * module connects it to the editor: which document it shows, its syntax
 * colours, when it must re-render, and scrolling the editor when the user
 * clicks or drags in the minimap.
 */
#include "editor.h"

#include <gadgets/layout.h>
#include <gadgets/minimap.h>
#include <gadgets/texteditor.h>
#include <intuition/icclass.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/layout.h>
#include <proto/utility.h>
#include <clib/alib_protos.h>

#include <string.h>

/** Context of the colour emit callback: the line's pens and the pen set. */
typedef struct ColorFill {
    UBYTE *pens;        /**< pens of the line's characters */
    ULONG length;       /**< length of the line */
    const SyntaxHookContext *colors;    /**< the document's highlighting pens */
} ColorFill;

/**
 * @brief Syntax emit callback: store the pen of a styled span.
 *
 * @param context The ColorFill of the current line.
 * @param span The styled span; its end is clamped to the line length.
 */
static void minimap_fill_pens(void *context, const SyntaxSpan *span)
{
    ColorFill *cf = (ColorFill *)context;
    const SyntaxHookContext *hc = cf->colors;
    unsigned short pen;
    size_t i, end = span->end;
    pen = hc->pens[span->style < SYNTAX_STYLE_COUNT ? span->style : SYNTAX_NORMAL];
    if (end > cf->length) end = cf->length;
    for (i = span->start; i < end; ++i) cf->pens[i] = (UBYTE)pen;
}

/**
 * @brief MINIMAP_ColorHook of a document: colour one line with the syntax
 *        scanner and the document's highlighter pens.
 *
 * Called from the render process of minimap.gadget; it only reads the
 * document (which the gadget never outlives, see minimap_forget_document())
 * and uses no DOS or Intuition.
 *
 * @param hook The document's minimap hook (h_Data = Document).
 * @param obj The minimap gadget (unused).
 * @param msg The line to colour.
 * @return The syntax state for the next line.
 */
static ULONG minimap_color_line(struct Hook *hook, Object *obj,
                                struct MinimapLineMsg *msg)
{
    Document *doc = (Document *)hook->h_Data;
    ColorFill cf;
    (void)obj;
    cf.pens = msg->mlm_Pens;
    cf.length = msg->mlm_Length;
    cf.colors = &doc->highlight;
    return (ULONG)syntax_scan(doc->language, doc->highlight.rules, doc->highlight.tokens,
                              (const char *)msg->mlm_Text, msg->mlm_State, minimap_fill_pens, &cf);
}

void minimap_init_document(Document *doc)
{
    memset(&doc->minimap_hook, 0, sizeof(doc->minimap_hook));
    doc->minimap_hook.h_Entry = (ULONG (*)())HookEntry;
    doc->minimap_hook.h_SubEntry = (ULONG (*)())minimap_color_line;
    doc->minimap_hook.h_Data = doc;
}

/**
 * @brief Set attributes of the minimap (with the window when it is open).
 * @param app The editor application.
 * @param tags The tag list.
 */
static void minimap_set(EditorApp *app, struct TagItem *tags)
{
    if (app->minimap == NULL) return;
    if (app->window != NULL)
        SetGadgetAttrsA((struct Gadget *)app->minimap, app->window, NULL, tags);
    else SetAttrsA(app->minimap, tags);
}

Object *minimap_create_gadget(EditorApp *app)
{
    app->minimap_editor = NULL;
    app->minimap_notified = 0;
    app->minimap = NewObject(NULL, MINIMAP_CLASSNAME,
        GA_ID, GID_MINIMAP,
        GA_RelVerify, TRUE,
        ICA_TARGET, (ULONG)ICTARGET_IDCMP,
        MINIMAP_TextPen, (ULONG)app->editor_pens[EDITOR_COLOR_TEXT],
        MINIMAP_ViewFillPen, (ULONG)app->minimap_view_pen,
        TAG_END);
    return app->minimap;
}

void minimap_set_visible(EditorApp *app, int visible)
{
    app->minimap_visible = visible;
    if (app->content_layout == NULL || app->minimap == NULL) return;
    if (app->window != NULL)
        SetGadgetAttrs((struct Gadget *)app->content_layout, app->window, NULL,
            LAYOUT_ModifyChild, (ULONG)app->minimap,
            CHILD_MinWidth, visible ? MINIMAP_MIN_WIDTH : 0,
            CHILD_MaxWidth, visible ? ~0UL : 0,
            CHILD_WeightedWidth, visible ? MINIMAP_DEFAULT_WEIGHT : 0,
            TAG_END);
    else SetAttrs(app->content_layout,
        LAYOUT_ModifyChild, (ULONG)app->minimap,
        CHILD_MinWidth, visible ? MINIMAP_MIN_WIDTH : 0,
        CHILD_MaxWidth, visible ? ~0UL : 0,
        CHILD_WeightedWidth, visible ? MINIMAP_DEFAULT_WEIGHT : 0,
        TAG_END);
    app->minimap_attached = 1;
    ui_relayout(app);
}

void minimap_request(EditorApp *app)
{
    struct TagItem tags[2];
    tags[0].ti_Tag = MINIMAP_TextChanged; tags[0].ti_Data = TRUE;
    tags[1].ti_Tag = TAG_END;             tags[1].ti_Data = 0;
    minimap_set(app, tags);
}

void minimap_poll(EditorApp *app)
{
    struct TagItem tags[4];
    Object *editor = app->active != NULL ? app->active->editor : NULL;
    int n = 0;
    if (app->minimap == NULL || app->window == NULL || !app->minimap_visible) return;
    if (app->scrolling && !app->minimap_dragging) return;
    if (editor != app->minimap_editor) {
        tags[n].ti_Tag = MINIMAP_TextEditor;
        tags[n++].ti_Data = (ULONG)editor;
        tags[n].ti_Tag = MINIMAP_ColorHook;
        tags[n++].ti_Data = app->active != NULL ? (ULONG)&app->active->minimap_hook : 0;
        app->minimap_editor = editor;
    }
    tags[n].ti_Tag = MINIMAP_Update;
    tags[n++].ti_Data = TRUE;
    tags[n].ti_Tag = TAG_END;
    tags[n].ti_Data = 0;
    minimap_set(app, tags);
}

void minimap_forget_document(EditorApp *app, Document *doc)
{
    struct TagItem tags[3];
    if (app->minimap_editor == NULL) return;
    if (doc != NULL && doc->editor != app->minimap_editor) return;
    tags[0].ti_Tag = MINIMAP_TextEditor; tags[0].ti_Data = 0;
    tags[1].ti_Tag = MINIMAP_ColorHook;  tags[1].ti_Data = 0;
    tags[2].ti_Tag = TAG_END;            tags[2].ti_Data = 0;
    minimap_set(app, tags);
    app->minimap_editor = NULL;
}

ULONG minimap_signal_mask(EditorApp *app)
{
    ULONG mask = 0;
    if (app->minimap != NULL) GetAttr(MINIMAP_SignalMask, app->minimap, &mask);
    return mask;
}

int minimap_handle_notify(EditorApp *app, struct TagItem *tags)
{
    struct TagItem *top = FindTagItem(MINIMAP_Top, tags);
    if (top == NULL) return 0;
    app->minimap_top = top->ti_Data;
    app->minimap_drag_state = (int)GetTagData(MINIMAP_Dragging, FALSE, tags);
    app->minimap_notified = 1;
    return 1;
}

void minimap_handle_scroll(EditorApp *app)
{
    Document *doc = app->active;
    if (!app->minimap_notified) return;
    app->minimap_notified = 0;
    if (app->minimap_drag_state && !app->minimap_dragging) {
        app->minimap_dragging = 1;
        document_suspend_highlight(app);
    }
    if (doc != NULL && app->window != NULL) {
        SetGadgetAttrs((struct Gadget *)doc->editor, app->window, NULL,
            GA_TEXTEDITOR_Prop_First, app->minimap_top, TAG_END);
        RefreshPageGadget((struct Gadget *)doc->page, app->pages, app->window, NULL);
        document_sync_scrollers(app, doc);
    }
    if (!app->minimap_drag_state && app->minimap_dragging) {
        app->minimap_dragging = 0;
        document_resume_highlight(app);
    }
}
