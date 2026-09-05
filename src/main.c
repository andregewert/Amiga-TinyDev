#include "editor.h"

#include <dos/dos.h>
#include <dos/dosextens.h>
#include <exec/execbase.h>
#include <graphics/gfxbase.h>
#include <intuition/intuitionbase.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <clib/alib_protos.h>

#include <stdio.h>
#include <string.h>

struct Library *AslBase, *DiskfontBase, *GadToolsBase, *IconBase, *UtilityBase;
struct Library *WindowBase, *LayoutBase, *ClickTabBase, *TextFieldBase;
static struct IntuitionBase *old_intuition_base;
static struct GfxBase *old_gfx_base;

static int open_one(struct Library **base, const char *name, ULONG version)
{
    *base = OpenLibrary(name, version);
    if (*base == NULL) fprintf(stderr, "AmiEditor: requires %s version %lu\n", name, (unsigned long)version);
    return *base != NULL;
}

int app_open_libraries(void)
{
    struct Library *base;
    old_intuition_base = IntuitionBase; old_gfx_base = GfxBase;
    base = OpenLibrary("intuition.library", 47);
    if (base == NULL) { fprintf(stderr, "AmiEditor: requires intuition.library version 47\n"); return 0; }
    IntuitionBase = (struct IntuitionBase *)base;
    base = OpenLibrary("graphics.library", 47);
    if (base == NULL) { fprintf(stderr, "AmiEditor: requires graphics.library version 47\n"); return 0; }
    GfxBase = (struct GfxBase *)base;
    return DOSBase != NULL && DOSBase->dl_lib.lib_Version >= 47 &&
           open_one(&UtilityBase, "utility.library", 47) &&
           open_one(&AslBase, "asl.library", 47) &&
           open_one(&DiskfontBase, "diskfont.library", 47) &&
           open_one(&GadToolsBase, "gadtools.library", 47) &&
           open_one(&IconBase, "icon.library", 47) &&
           open_one(&WindowBase, "window.class", 47) &&
           open_one(&LayoutBase, "gadgets/layout.gadget", 47) &&
           open_one(&ClickTabBase, "gadgets/clicktab.gadget", 47) &&
           open_one(&TextFieldBase, "gadgets/texteditor.gadget", 47);
}

void app_close_libraries(void)
{
#define CLOSE_BASE(x) do { if ((x) != NULL) { CloseLibrary((x)); (x) = NULL; } } while (0)
    CLOSE_BASE(TextFieldBase); CLOSE_BASE(ClickTabBase); CLOSE_BASE(LayoutBase);
    CLOSE_BASE(WindowBase); CLOSE_BASE(IconBase); CLOSE_BASE(GadToolsBase);
    CLOSE_BASE(DiskfontBase); CLOSE_BASE(AslBase); CLOSE_BASE(UtilityBase);
    if (GfxBase != NULL && GfxBase != old_gfx_base) CloseLibrary((struct Library *)GfxBase);
    if (IntuitionBase != NULL && IntuitionBase != old_intuition_base) CloseLibrary((struct Library *)IntuitionBase);
    GfxBase = old_gfx_base; IntuitionBase = old_intuition_base;
#undef CLOSE_BASE
}

static void open_cli_files(EditorApp *app, int argc, char **argv)
{
    int i;
    for (i = 1; i < argc; ++i) document_open(app, argv[i]);
}

static void open_workbench_files(EditorApp *app, struct WBStartup *startup)
{
    LONG i; char path[EDITOR_PATH_MAX];
    for (i = 1; i < startup->sm_NumArgs; ++i) {
        struct WBArg *arg = &startup->sm_ArgList[i];
        if (!NameFromLock(arg->wa_Lock, path, sizeof(path)) ||
            !AddPart(path, arg->wa_Name, sizeof(path)))
            ui_error(app, "Open failed", "A Workbench argument path is too long.");
        else document_open(app, path);
    }
}

int main(int argc, char **argv)
{
    EditorApp app; int status = 20;
    memset(&app, 0, sizeof(app));
    NewList(&app.documents); NewList(&app.tab_nodes); app.line_numbers = 1;
    if (!app_open_libraries()) goto done;
    if (!font_open_default(&app)) fprintf(stderr, "AmiEditor: topaz.font unavailable; using the screen font\n");
    if (!ui_create(&app)) { fprintf(stderr, "AmiEditor: could not create the ReAction window\n"); goto done; }
    if (argc == 0) open_workbench_files(&app, (struct WBStartup *)argv);
    else open_cli_files(&app, argc, argv);
    if (IsListEmpty(&app.documents)) document_new(&app);
    ui_run(&app); status = 0;
done:
    ui_destroy(&app); font_close(&app); app_close_libraries();
    return status;
}
