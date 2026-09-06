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

struct Library *AslBase = NULL;
struct Library *GadToolsBase = NULL;
struct Library *IconBase = NULL;
struct Library *UtilityBase = NULL;
struct Library *WindowBase = NULL;
struct Library *LayoutBase = NULL;
struct Library *ClickTabBase = NULL;
struct Library *TextFieldBase = NULL;
struct Library *ButtonBase = NULL;
struct Library *BitMapBase = NULL;
struct Library *SpaceBase = NULL;
struct Library *ListBrowserBase = NULL;
struct Library *GlyphBase = NULL;
struct Library *ScrollerBase = NULL;
static struct IntuitionBase *old_intuition_base;
static struct GfxBase *old_gfx_base;
static int workbench_launch;

static void report_library_error(const char *name, ULONG version)
{
    char message[160];
    snprintf(message, sizeof(message), "AmiEditor requires %s version %lu or newer.",
             name, (unsigned long)version);
    fprintf(stderr, "%s\n", message);
    if (workbench_launch && IntuitionBase != NULL) {
        struct EasyStruct requester = {
            sizeof(requester), 0, "AmiEditor startup error", message, "OK"
        };
        EasyRequestArgs(NULL, &requester, NULL, NULL);
    }
}

static int open_one(struct Library **base, const char *name, ULONG version)
{
    *base = OpenLibrary(name, version);
    if (*base == NULL) report_library_error(name, version);
    return *base != NULL;
}

int app_open_libraries(int from_workbench)
{
    struct Library *base;
    workbench_launch = from_workbench;
    old_intuition_base = IntuitionBase; old_gfx_base = GfxBase;
    base = OpenLibrary("intuition.library", 47);
    if (base == NULL) { report_library_error("intuition.library", 47); return 0; }
    IntuitionBase = (struct IntuitionBase *)base;
    base = OpenLibrary("graphics.library", 47);
    if (base == NULL) { report_library_error("graphics.library", 47); return 0; }
    GfxBase = (struct GfxBase *)base;
    if (DOSBase == NULL || DOSBase->dl_lib.lib_Version < 47) {
        report_library_error("dos.library", 47); return 0;
    }
    return open_one(&UtilityBase, "utility.library", 47) &&
           open_one(&AslBase, "asl.library", 47) &&
           open_one(&GadToolsBase, "gadtools.library", 47) &&
           open_one(&IconBase, "icon.library", 47) &&
           open_one(&WindowBase, "window.class", 47) &&
           open_one(&LayoutBase, "gadgets/layout.gadget", 47) &&
           open_one(&ButtonBase, "gadgets/button.gadget", 47) &&
           open_one(&SpaceBase, "gadgets/space.gadget", 47) &&
           open_one(&BitMapBase, "images/bitmap.image", 47) &&
           open_one(&GlyphBase, "images/glyph.image", 47) &&
           open_one(&ScrollerBase, "gadgets/scroller.gadget", 47) &&
           open_one(&ListBrowserBase, "gadgets/listbrowser.gadget", 47) &&
           open_one(&ClickTabBase, "gadgets/clicktab.gadget", 47) &&
           open_one(&TextFieldBase, "gadgets/texteditor.gadget", 47);
}

void app_close_libraries(void)
{
#define CLOSE_BASE(x) do { if ((x) != NULL) { CloseLibrary((x)); (x) = NULL; } } while (0)
    CLOSE_BASE(TextFieldBase); CLOSE_BASE(ClickTabBase); CLOSE_BASE(ListBrowserBase);
    CLOSE_BASE(ScrollerBase); CLOSE_BASE(GlyphBase); CLOSE_BASE(BitMapBase);
    CLOSE_BASE(SpaceBase); CLOSE_BASE(ButtonBase); CLOSE_BASE(LayoutBase);
    CLOSE_BASE(WindowBase); CLOSE_BASE(IconBase); CLOSE_BASE(GadToolsBase);
    CLOSE_BASE(AslBase); CLOSE_BASE(UtilityBase);
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
    app.scroll_signal = -1;
    NewList(&app.documents); NewList(&app.tab_nodes); NewList(&app.tree_nodes);
    app.line_numbers = 1; app.tree_visible = 1;
    if (!app_open_libraries(argc == 0)) goto done;
    app.scroll_signal = AllocSignal(-1);
    if (app.scroll_signal < 0) {
        fprintf(stderr, "AmiEditor: no free signal for live scrolling\n");
        goto done;
    }
    if (!ui_create(&app)) { fprintf(stderr, "AmiEditor: could not create the ReAction window\n"); goto done; }
    if (argc == 0) open_workbench_files(&app, (struct WBStartup *)argv);
    else open_cli_files(&app, argc, argv);
    if (IsListEmpty(&app.documents)) document_new(&app);
    ui_run(&app); status = 0;
done:
    ui_destroy(&app);
    if (app.scroll_signal >= 0) FreeSignal((BYTE)app.scroll_signal);
    app_close_libraries();
    return status;
}
