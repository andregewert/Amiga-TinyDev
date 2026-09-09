#include "editor.h"

#include <dos/dos.h>
#include <libraries/asl.h>
#include <proto/asl.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/layout.h>
#include <clib/alib_protos.h>
#include <gadgets/texteditor.h>

#include <stdio.h>
#include <string.h>

/**
 * @brief Resolve a path to its canonical AmigaDOS form.
 *
 * Locks @p path and derives the full name from the lock. If the path cannot
 * be locked (for example a not-yet-existing save target) the input path is
 * copied verbatim, truncated to fit @p size.
 *
 * @param path The path to canonicalise.
 * @param out Destination buffer for the resolved (NUL-terminated) path.
 * @param size Size of @p out in bytes.
 * @return Non-zero on success, zero if the name could not be obtained.
 */
static int canonical_path(const char *path, char *out, size_t size)
{
    BPTR lock = Lock(path, ACCESS_READ);
    if (lock == 0) {
        strncpy(out, path, size - 1); out[size - 1] = '\0';
        return 1;
    }
    if (!NameFromLock(lock, out, (LONG)size)) { UnLock(lock); return 0; }
    UnLock(lock); return 1;
}

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
int file_load(EditorApp *app, Document *doc, const char *path)
{
    BPTR file = 0; struct FileInfoBlock *fib = NULL; char *data = NULL;
    char canonical[EDITOR_PATH_MAX]; LONG got; int ok = 0; ULONG imported = LINEENDING_LF;
    if (!canonical_path(path, canonical, sizeof(canonical))) {
        ui_error(app, "Open failed", "The full file name is too long."); return 0;
    }
    if (document_find_path(app, canonical) != NULL && document_find_path(app, canonical) != doc) {
        document_activate(app, document_find_path(app, canonical)); return 0;
    }
    file = Open(canonical, MODE_OLDFILE);
    fib = AllocDosObject(DOS_FIB, NULL);
    if (file == 0 || fib == NULL || !ExamineFH(file, fib) || fib->fib_DirEntryType >= 0 ||
        fib->fib_Size < 0 || (ULONG)fib->fib_Size > EDITOR_MAX_FILE) {
        ui_error(app, "Open failed", "Cannot open this regular file (or it exceeds 8 MiB)."); goto done;
    }
    data = AllocVec((ULONG)fib->fib_Size + 1, MEMF_ANY);
    if (data == NULL) { ui_error(app, "Open failed", "Not enough memory to load this file."); goto done; }
    got = Read(file, data, fib->fib_Size);
    if (got != fib->fib_Size) { ui_error(app, "Open failed", "The file could not be read completely."); goto done; }
    data[got] = '\0';
    if (memchr(data, '\0', (size_t)got) != NULL) {
        ui_error(app, "Open failed", "This file contains an embedded NUL byte; only 8-bit text is supported."); goto done;
    }
    SetAttrs(doc->editor, GA_TEXTEDITOR_Contents, (ULONG)data, TAG_END);
    GetAttr(GA_TEXTEDITOR_LineEndingImported, doc->editor, &imported);
    doc->eol = imported <= LINEENDING_CRLF ? (LineEnding)imported : EOL_LF;
    document_set_path(app, doc, canonical);
    SetAttrs(doc->editor, GA_TEXTEDITOR_HasChanged, FALSE,
             GA_TEXTEDITOR_LineEndingExport, LINEENDING_ASIMPORT, TAG_END);
    document_set_dirty(app, doc, 0); ok = 1;
    if (app->window != NULL) {
        RefreshPageGadget((struct Gadget *)doc->page, app->pages,
                          app->window, NULL);
        ui_relayout(app);
        /* Loading into the currently active document (e.g. the initial empty
         * tab reused for the first file opened from the tree) does not go
         * through document_activate(), and the ui_relayout() above repaints the
         * minimap's space.gadget grey.  Request a re-render so the freshly
         * loaded contents are drawn instead of leaving a grey area. */
        minimap_request(app);
    }
done:
    if (data != NULL) FreeVec(data);
    if (fib != NULL) FreeDosObject(DOS_FIB, fib);
    if (file != 0 && !Close(file) && ok) { ui_error(app, "Open failed", "The file could not be closed cleanly."); ok = 0; }
    return ok;
}

/**
 * @brief Write an entire buffer to a DOS file handle, handling short writes.
 *
 * Loops until all @p length bytes are written or an error occurs.
 *
 * @param file The open DOS file handle to write to.
 * @param text The buffer to write.
 * @param length The number of bytes to write.
 * @return Non-zero if every byte was written, zero on a write error.
 */
static int write_complete(BPTR file, const char *text, LONG length)
{
    LONG total = 0;
    while (total < length) {
        LONG wrote = Write(file, text + total, length - total);
        if (wrote <= 0) return 0;
        total += wrote;
    }
    return 1;
}

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
int file_save(EditorApp *app, Document *doc, const char *path)
{
    char canonical[EDITOR_PATH_MAX], resolved[EDITOR_PATH_MAX];
    char temp[EDITOR_PATH_MAX], backup[EDITOR_PATH_MAX];
    char *text; LONG length; BPTR file, collision; int had_old, wrote, closed;
    if (!canonical_path(path, canonical, sizeof(canonical))) return 0;
    if (document_find_path(app, canonical) != NULL && document_find_path(app, canonical) != doc) {
        document_activate(app, document_find_path(app, canonical));
        ui_error(app, "Save failed", "That file is already open in another tab."); return 0;
    }
    if (snprintf(temp, sizeof(temp), "%s.ae%08lx.tmp", canonical, doc->number) >= (int)sizeof(temp) ||
        snprintf(backup, sizeof(backup), "%s.ae%08lx.bak", canonical, doc->number) >= (int)sizeof(backup)) {
        ui_error(app, "Save failed", "The destination file name is too long."); return 0;
    }
    text = (char *)DoMethod(doc->editor, GM_TEXTEDITOR_ExportText, NULL);
    if (text == NULL) { ui_error(app, "Save failed", "TextEditor could not export the document."); return 0; }
    length = (LONG)strlen(text);
    collision = Lock(temp, ACCESS_READ);
    if (collision != 0) { UnLock(collision); FreeVec(text); ui_error(app, "Save failed", "A temporary sibling file already exists."); return 0; }
    collision = Lock(backup, ACCESS_READ);
    if (collision != 0) { UnLock(collision); FreeVec(text); ui_error(app, "Save failed", "A backup sibling file already exists."); return 0; }
    file = Open(temp, MODE_NEWFILE);
    wrote = file != 0 && write_complete(file, text, length);
    closed = file != 0 ? Close(file) : 0;
    if (!wrote || !closed) {
        DeleteFile(temp); FreeVec(text);
        ui_error(app, "Save failed", "Could not write and close the temporary file."); return 0;
    }
    FreeVec(text);
    { BPTR lock = Lock(canonical, ACCESS_READ); had_old = lock != 0; if (lock != 0) UnLock(lock); }
    if (had_old && !Rename(canonical, backup)) goto replace_failed;
    if (!Rename(temp, canonical)) {
        if (had_old) Rename(backup, canonical);
        goto replace_failed;
    }
    if (had_old) DeleteFile(backup);
    if (canonical_path(canonical, resolved, sizeof(resolved))) document_set_path(app, doc, resolved);
    else document_set_path(app, doc, canonical);
    SetAttrs(doc->editor, GA_TEXTEDITOR_HasChanged, FALSE, TAG_END);
    document_set_dirty(app, doc, 0);
    return 1;
replace_failed:
    DeleteFile(temp);
    ui_error(app, "Save failed", "Could not replace the destination; the original was retained.");
    return 0;
}

/**
 * @brief Present an ASL file requester and act on the chosen file.
 *
 * Builds the full path from the requester's drawer and file name, then either
 * saves @p doc or opens a new document depending on @p save.
 *
 * @param app The application state.
 * @param doc The document to save (used only when @p save is non-zero).
 * @param save Non-zero for a save requester, zero for an open requester.
 * @return Non-zero if a file was successfully saved or opened, zero otherwise.
 */
static int request_file(EditorApp *app, Document *doc, int save)
{
    struct FileRequester *fr; char path[EDITOR_PATH_MAX]; int result = 0;
    fr = AllocAslRequestTags(ASL_FileRequest, ASLFR_TitleText,
        (ULONG)(save ? "Save document" : "Open document"), ASLFR_DoSaveMode, (ULONG)save,
        ASLFR_RejectIcons, TRUE, TAG_END);
    if (fr == NULL) { ui_error(app, "tinyDE", "Could not allocate the file requester."); return 0; }
    if (AslRequestTags(fr, ASLFR_Window, (ULONG)app->window, ASLFR_SleepWindow, TRUE, TAG_END)) {
        strncpy(path, fr->fr_Drawer, sizeof(path) - 1); path[sizeof(path) - 1] = '\0';
        if (!AddPart(path, fr->fr_File, sizeof(path))) ui_error(app, "tinyDE", "Selected path is too long.");
        else result = save ? file_save(app, doc, path) : document_open(app, path) != NULL;
    }
    FreeAslRequest(fr); return result;
}

/**
 * @brief Prompt the user for a file to open via an ASL requester.
 *
 * @param app The application state.
 * @return Non-zero if a document was opened, zero otherwise.
 */
int file_request_open(EditorApp *app) { return request_file(app, NULL, 0); }
/**
 * @brief Prompt the user for a destination and save a document via ASL.
 *
 * @param app The application state.
 * @param doc The document to save.
 * @return Non-zero if the document was saved, zero otherwise.
 */
int file_request_save(EditorApp *app, Document *doc) { return request_file(app, doc, 1); }
