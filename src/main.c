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

/**
 * @brief Report a failed library or class open to the user.
 *
 * Always writes a message to stderr. When the program was started from
 * Workbench and intuition.library is available, the same message is also
 * shown in an EasyRequest so a GUI user sees the failure.
 *
 * @param name The name of the library or class that failed to open.
 * @param version The minimum required version reported to the user.
 */
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

/**
 * @brief Open one library/class and report failure if it cannot be opened.
 *
 * @param base Address of the library base pointer to fill in.
 * @param name The library or class name to open.
 * @param version The minimum required version.
 * @return Non-zero on success, zero if the library could not be opened.
 */
static int open_one(struct Library **base, const char *name, ULONG version)
{
    *base = OpenLibrary(name, version);
    if (*base == NULL) report_library_error(name, version);
    return *base != NULL;
}

/**
 * @brief Open every library and ReAction class required by the editor.
 *
 * Opens intuition.library and graphics.library first (saving the previous
 * global bases so they can be restored on shutdown), verifies dos.library,
 * then opens the remaining ReAction gadget/image classes. Any failure is
 * reported via report_library_error() and aborts the sequence.
 *
 * @param from_workbench Non-zero when launched from Workbench, which enables
 *        GUI error requesters.
 * @return Non-zero if all required libraries opened successfully, zero otherwise.
 */
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

/**
 * @brief Close all libraries and classes opened by app_open_libraries().
 *
 * Closes the ReAction classes and helper libraries in reverse order, then
 * restores the intuition.library and graphics.library global bases to the
 * values captured at startup. Safe to call even if opening only partially
 * succeeded.
 */
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

/**
 * @brief Open every file named on the Shell/CLI command line.
 *
 * @param app The application state.
 * @param argc The CLI argument count.
 * @param argv The CLI argument vector; entries 1..argc-1 are opened as files.
 */
static void open_cli_files(EditorApp *app, int argc, char **argv)
{
    int i;
    for (i = 1; i < argc; ++i) document_open(app, argv[i]);
}

/**
 * @brief Open every file passed as a Workbench startup argument.
 *
 * Resolves each WBArg lock/name pair into a full path and opens it. A path
 * that is too long is reported through ui_error() and skipped.
 *
 * @param app The application state.
 * @param startup The Workbench startup message.
 */
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

/**
 * @brief Program entry point for both Shell and Workbench launches.
 *
 * Initialises the application state, opens the required libraries, allocates
 * the live-scroll signal, creates the ReAction window and opens any files
 * requested on the command line (or via Workbench). If no document ends up
 * open, a fresh empty one is created. Finally runs the event loop and tears
 * everything down on exit. A Workbench launch is detected by @p argc being 0,
 * in which case @p argv is really a struct WBStartup pointer.
 *
 * @param argc The argument count, or 0 when started from Workbench.
 * @param argv The argument vector, or the WBStartup message when @p argc is 0.
 * @return 0 on success, or a non-zero DOS return code on startup failure.
 */
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
