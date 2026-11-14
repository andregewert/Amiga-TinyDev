/**
 * @file fileio.c
 * @brief Loading and saving documents: line endings, safe saves through a temporary file, file requesters.
 *
 * Contents are 8-bit text (no UTF-8 decoding); files with NUL bytes are
 * refused. LF, CR and CRLF are kept as loaded: the editor always exports
 * LF, and a save puts the file's line ends back. texteditor.gadget 47.69
 * must not export CRLF: GM_TEXTEDITOR_ExportText allocates one byte per
 * line end but writes two, overrunning its buffer by a byte per line (seen
 * in its machine code). A save writes a temporary
 * file first, so a failed write leaves the original intact. The temporary
 * file gets the original's protection bits and comment before it takes
 * the original's place (AmigaDOS cannot create a file with them): without
 * the script bit a saved Shell script would no longer run, nor be taken
 * for one by Scout. The archive bit is cleared (the file changed); a
 * delete-protected file gets that bit only after the renaming. Opening a file
 * that is open already shows its tab. Once a directory is open, the
 * requesters for new files start in it (Save As of a named file starts in
 * its drawer).
 */
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

int file_load(EditorApp *app, Document *doc, const char *path)
{
    BPTR file = 0; struct FileInfoBlock *fib = NULL; char *data = NULL;
    char canonical[EDITOR_PATH_MAX]; LONG got; int ok = 0; ULONG imported = LINEENDING_LF;
    if (!canonical_path(path, canonical, sizeof(canonical))) {
        ui_error(app, S(MSG_OPEN_FAILED), S(MSG_ERR_NAMETOOLONG)); return 0;
    }
    if (document_find_path(app, canonical) != NULL && document_find_path(app, canonical) != doc) {
        document_activate(app, document_find_path(app, canonical)); return 0;
    }
    file = Open(canonical, MODE_OLDFILE);
    fib = AllocDosObject(DOS_FIB, NULL);
    if (file == 0 || fib == NULL || !ExamineFH(file, fib) || fib->fib_DirEntryType >= 0 ||
        fib->fib_Size < 0 || (ULONG)fib->fib_Size > EDITOR_MAX_FILE) {
        ui_error(app, S(MSG_OPEN_FAILED), S(MSG_ERR_CANNOTOPEN)); goto done;
    }
    data = AllocVec((ULONG)fib->fib_Size + 1, MEMF_ANY);
    if (data == NULL) { ui_error(app, S(MSG_OPEN_FAILED), S(MSG_ERR_NOMEMFILE)); goto done; }
    got = Read(file, data, fib->fib_Size);
    if (got != fib->fib_Size) { ui_error(app, S(MSG_OPEN_FAILED), S(MSG_ERR_READ)); goto done; }
    data[got] = '\0';
    doc->final_eol = got == 0 || data[got - 1] == '\n' || data[got - 1] == '\r';
    if (memchr(data, '\0', (size_t)got) != NULL) {
        ui_error(app, S(MSG_OPEN_FAILED), S(MSG_ERR_NUL)); goto done;
    }
    SetAttrs(doc->editor, GA_TEXTEDITOR_Contents, (ULONG)data, TAG_END);
    document_mark_saved(doc, data, got, 1);
    GetAttr(GA_TEXTEDITOR_LineEndingImported, doc->editor, &imported);
    doc->eol = imported <= LINEENDING_CRLF ? (LineEnding)imported : EOL_LF;
    document_set_path(app, doc, canonical);
    SetAttrs(doc->editor, GA_TEXTEDITOR_HasChanged, FALSE,
             GA_TEXTEDITOR_LineEndingExport, LINEENDING_LF, TAG_END);
    document_set_dirty(app, doc, 0); ok = 1;
    bookmarks_document_loaded(app, doc);
    scoutlink_document_saved(doc);
    if (app->window != NULL) {
        /* The editor takes text set without the window only with a new
         * layout (otherwise a reused empty tab stays empty); collected with
         * the other updates while a file is opened, so a new tab is still
         * laid out only once. */
        ui_relayout(app);
        /* Loading into the currently active document (e.g. the initial empty
         * tab reused for the first file opened from the tree) does not go
         * through document_activate(); tell the minimap that the text
         * changed so it shows the freshly loaded contents. */
        minimap_request(app);
    }
done:
    if (data != NULL) FreeVec(data);
    if (fib != NULL) FreeDosObject(DOS_FIB, fib);
    if (file != 0 && !Close(file) && ok) { ui_error(app, S(MSG_OPEN_FAILED), S(MSG_ERR_CLOSE)); ok = 0; }
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
 * @brief Read a file's protection bits and comment.
 * @param path The file.
 * @param[out] protection Its bits.
 * @param[out] comment Its comment ("" if none).
 * @param size Size of @p comment.
 * @return Non-zero if the file exists and was examined.
 */
static int read_attributes(const char *path, LONG *protection, char *comment, size_t size)
{
    struct FileInfoBlock *fib;
    BPTR lock;
    int ok = 0;
    comment[0] = '\0';
    if ((lock = Lock((STRPTR)path, ACCESS_READ)) == 0) return 0;
    if ((fib = (struct FileInfoBlock *)AllocDosObject(DOS_FIB, NULL)) != NULL) {
        if (Examine(lock, fib) && fib->fib_DirEntryType < 0) {
            *protection = fib->fib_Protection;
            strncpy(comment, fib->fib_Comment, size - 1);
            comment[size - 1] = '\0';
            ok = 1;
        }
        FreeDosObject(DOS_FIB, fib);
    }
    UnLock(lock);
    return ok;
}

/**
 * @brief Write the text exported from the editor with the line ends of its
 *        file.
 *
 * @param file The file.
 * @param text The text, with LF line ends.
 * @param length Its length.
 * @param eol The line ends to write.
 * @return Non-zero on success.
 */
static int write_text(BPTR file, const char *text, LONG length, LineEnding eol)
{
    LONG i, lines = 0, n = 0;
    char *out;
    int ok;
    if (eol != EOL_CR && eol != EOL_CRLF) return write_complete(file, text, length);
    for (i = 0; i < length; i++) {
        if (text[i] == '\n') lines++;
    }
    out = AllocVec((ULONG)(length + lines + 1), MEMF_ANY);
    if (out == NULL) return 0;
    for (i = 0; i < length; i++) {
        if (text[i] != '\n') {
            out[n++] = text[i];
        } else {
            out[n++] = '\r';
            if (eol == EOL_CRLF) out[n++] = '\n';
        }
    }
    ok = write_complete(file, out, n);
    FreeVec(out);
    return ok;
}

int file_save(EditorApp *app, Document *doc, const char *path)
{
    char canonical[EDITOR_PATH_MAX], resolved[EDITOR_PATH_MAX];
    char temp[EDITOR_PATH_MAX], backup[EDITOR_PATH_MAX], comment[80];
    char *text; LONG length, protection = 0; BPTR file, collision; int had_old, wrote, closed, attrs;
    unsigned long old_hash = doc->saved_hash; long old_length = doc->saved_length;
    if (!canonical_path(path, canonical, sizeof(canonical))) return 0;
    if (document_find_path(app, canonical) != NULL && document_find_path(app, canonical) != doc) {
        document_activate(app, document_find_path(app, canonical));
        ui_error(app, S(MSG_SAVE_FAILED), S(MSG_ERR_ALREADYOPEN)); return 0;
    }
    if (snprintf(temp, sizeof(temp), "%s.ae%08lx.tmp", canonical, doc->number) >= (int)sizeof(temp) ||
        snprintf(backup, sizeof(backup), "%s.ae%08lx.bak", canonical, doc->number) >= (int)sizeof(backup)) {
        ui_error(app, S(MSG_SAVE_FAILED), S(MSG_ERR_DESTTOOLONG)); return 0;
    }
    text = (char *)DoMethod(doc->editor, GM_TEXTEDITOR_ExportText, NULL);
    if (text == NULL) { ui_error(app, S(MSG_SAVE_FAILED), S(MSG_ERR_EXPORT)); return 0; }
    length = (LONG)strlen(text);
    collision = Lock(temp, ACCESS_READ);
    if (collision != 0) { UnLock(collision); FreeVec(text); ui_error(app, S(MSG_SAVE_FAILED), S(MSG_ERR_TEMPEXISTS)); return 0; }
    collision = Lock(backup, ACCESS_READ);
    if (collision != 0) { UnLock(collision); FreeVec(text); ui_error(app, S(MSG_SAVE_FAILED), S(MSG_ERR_BACKUPEXISTS)); return 0; }
    file = Open(temp, MODE_NEWFILE);
    wrote = file != 0 && write_text(file, text, length, doc->eol);
    /* the editor exports the text without the line end at its end */
    if (wrote && doc->final_eol && length > 0 && text[length - 1] != '\n' && text[length - 1] != '\r') {
        static const char *const eols[] = { "\n", "\r", "\r\n" };
        const char *e = eols[doc->eol <= EOL_CRLF ? doc->eol : EOL_LF];
        wrote = write_complete(file, e, (LONG)strlen(e));
    }
    closed = file != 0 ? Close(file) : 0;
    if (!wrote || !closed) {
        DeleteFile(temp); FreeVec(text);
        ui_error(app, S(MSG_SAVE_FAILED), S(MSG_ERR_WRITETEMP)); return 0;
    }
    document_mark_saved(doc, text, length, 0);
    FreeVec(text);
    { BPTR lock = Lock(canonical, ACCESS_READ); had_old = lock != 0; if (lock != 0) UnLock(lock); }
    /* the original's bits and comment, before the file takes its place;
     * archived no more, and deletable until the renaming is done */
    attrs = had_old && read_attributes(canonical, &protection, comment, sizeof(comment));
    if (attrs) {
        protection &= ~FIBF_ARCHIVE;
        SetProtection(temp, protection & ~FIBF_DELETE);
        if (comment[0] != '\0') SetComment(temp, comment);
    }
    if (had_old && !Rename(canonical, backup)) goto replace_failed;
    if (!Rename(temp, canonical)) {
        if (had_old) Rename(backup, canonical);
        goto replace_failed;
    }
    if (attrs && (protection & FIBF_DELETE)) {
        SetProtection(canonical, protection);
        SetProtection(backup, 0);       /* the original's copy goes */
    }
    if (had_old) DeleteFile(backup);
    if (canonical_path(canonical, resolved, sizeof(resolved))) document_set_path(app, doc, resolved);
    else document_set_path(app, doc, canonical);
    SetAttrs(doc->editor, GA_TEXTEDITOR_HasChanged, FALSE, TAG_END);
    document_set_dirty(app, doc, 0);
    bookmarks_save(app);                /* the lines of its bookmarks may have moved */
    scoutlink_document_saved(doc);
    build_document_saved(doc->path);    /* it may be the project file */
    git_status_request(app);            /* the file's git state may change */
    return 1;
replace_failed:
    doc->saved_hash = old_hash; doc->saved_length = old_length;    /* the file did not change */
    DeleteFile(temp);
    ui_error(app, S(MSG_SAVE_FAILED), S(MSG_ERR_REPLACE));
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
    char drawer[EDITOR_PATH_MAX]; const char *file = "";
    /* Where the requester starts: a file that has a name in its drawer,
     * otherwise the project (the folder tree's directory), otherwise the
     * drawer of the active document. */
    drawer[0] = '\0';
    if (save && doc != NULL && doc->path != NULL) {
        strncpy(drawer, doc->path, sizeof(drawer) - 1); drawer[sizeof(drawer) - 1] = '\0';
        *PathPart(drawer) = '\0';
        file = (const char *)FilePart(doc->path);
    } else if (!tree_project_root(app, drawer, sizeof(drawer)) &&
               app->active != NULL && app->active->path != NULL) {
        strncpy(drawer, app->active->path, sizeof(drawer) - 1); drawer[sizeof(drawer) - 1] = '\0';
        *PathPart(drawer) = '\0';
    }
    fr = AllocAslRequestTags(ASL_FileRequest, ASLFR_TitleText,
        (ULONG)S(save ? MSG_ASL_SAVE : MSG_ASL_OPEN), ASLFR_DoSaveMode, (ULONG)save,
        ASLFR_RejectIcons, TRUE, TAG_END);
    if (fr == NULL) { ui_error(app, APP_NAME, S(MSG_ERR_FILEREQ)); return 0; }
    if (AslRequestTags(fr, ASLFR_Window, (ULONG)app->window, ASLFR_SleepWindow, TRUE,
                       drawer[0] != '\0' ? ASLFR_InitialDrawer : TAG_IGNORE, (ULONG)drawer,
                       ASLFR_InitialFile, (ULONG)file, TAG_END)) {
        strncpy(path, fr->fr_Drawer, sizeof(path) - 1); path[sizeof(path) - 1] = '\0';
        if (!AddPart(path, fr->fr_File, sizeof(path))) ui_error(app, APP_NAME, S(MSG_ERR_PATHTOOLONG));
        else result = save ? file_save(app, doc, path) : document_open(app, path) != NULL;
    }
    FreeAslRequest(fr); return result;
}

int file_request_open(EditorApp *app) { return request_file(app, NULL, 0); }
int file_request_save(EditorApp *app, Document *doc) { return request_file(app, doc, 1); }
