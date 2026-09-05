#include "editor.h"

#include <libraries/asl.h>
#include <proto/asl.h>
#include <proto/diskfont.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/layout.h>
#include <intuition/gadgetclass.h>

#include <string.h>

static char *font_name_copy(const char *name)
{
    size_t length = strlen(name) + 1;
    char *copy = AllocVec((ULONG)length, MEMF_ANY);
    if (copy != NULL) memcpy(copy, name, length);
    return copy;
}

static int install_font(EditorApp *app, const struct TextAttr *attr)
{
    struct TextFont *font; char *name;
    struct TextAttr stable = *attr;
    name = font_name_copy(attr->ta_Name);
    if (name == NULL) return 0;
    stable.ta_Name = name;
    font = OpenDiskFont(&stable);
    if (font == NULL) { FreeVec(name); return 0; }
    if ((font->tf_Flags & FPF_PROPORTIONAL) != 0) {
        CloseFont(font); FreeVec(name); return 0;
    }
    app->font_attr = stable;
    { struct TextFont *old = app->font; char *old_name = app->font_name;
      app->font = font; app->font_name = name; font_apply_all(app);
      if (old != NULL) CloseFont(old);
      if (old_name != NULL) FreeVec(old_name); }
    return 1;
}

int font_open_default(EditorApp *app)
{
    struct TextAttr attr = { "topaz.font", 8, FS_NORMAL, FPF_ROMFONT };
    return install_font(app, &attr);
}

void font_apply_all(EditorApp *app)
{
    Document *doc;
    for (doc = (Document *)app->documents.lh_Head; doc->node.ln_Succ;
         doc = (Document *)doc->node.ln_Succ) {
        SetAttrs(doc->editor, GA_TextAttr,
                 (ULONG)&app->font_attr, TAG_END);
    }
    ui_relayout(app);
    if (app->window != NULL && app->active != NULL)
        RefreshPageGadget((struct Gadget *)app->active->page, app->pages,
                          app->window, NULL);
}

int font_request(EditorApp *app)
{
    struct FontRequester *fr = AllocAslRequestTags(ASL_FontRequest,
        ASLFO_TitleText, (ULONG)"Choose fixed-width outline font",
        ASLFO_FixedWidthOnly, TRUE, ASLFO_OTagOnly, TRUE,
        ASLFO_ScalableOnly, TRUE, TAG_END);
    int result = 0;
    if (fr == NULL) { ui_error(app, "Font", "Could not allocate the font requester."); return 0; }
    if (AslRequestTags(fr, ASLFO_Window, (ULONG)app->window, ASLFO_SleepWindow, TRUE,
                       ASLFO_InitialName, (ULONG)app->font_attr.ta_Name,
                       ASLFO_InitialSize, app->font_attr.ta_YSize, TAG_END)) {
        result = install_font(app, &fr->fo_Attr);
        if (!result) ui_error(app, "Font unavailable",
            "The selected outline font could not be opened as fixed-width. The previous font remains active.");
    }
    FreeAslRequest(fr); return result;
}

void font_close(EditorApp *app)
{
    if (app->font != NULL) CloseFont(app->font);
    if (app->font_name != NULL) FreeVec(app->font_name);
    app->font = NULL; app->font_name = NULL;
}
