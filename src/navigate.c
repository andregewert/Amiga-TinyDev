/**
 * @file navigate.c
 * @brief Edit/Go to Definition and Edit/Go Back: jumping to where the name
 *        at the cursor is defined (asked from TinyDev-Scout) and back.
 *
 * Go to Definition takes the name at the cursor from the editor's text
 * (not from Scout's last snapshot, which may be older) and asks Scout with
 * DEFINITION, giving the position as well, so locals of the function
 * there are found first. The answer lists definitions before
 * declarations; TinyDev goes to the first. When there are several, the
 * status line says so, and Go to Definition again (with the cursor still
 * where it went) goes to the next one, round.
 *
 * Every jump first notes where the cursor was (at most HISTORY_MAX
 * places); Go Back returns there, also into another file.
 */
#include "editor.h"
#include "json.h"

#include <gadgets/texteditor.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/utility.h>
#include <clib/alib_protos.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** Most places Go Back can return to. */
#define HISTORY_MAX 32
/** Most definitions of a name that are kept. */
#define LOCATIONS_MAX 32
/** Longest name. */
#define NAME_MAX 128

/** A place in a file. */
typedef struct Place {
    char *path;         /**< the file (malloc()) */
    long line;          /**< the line (1-based) */
    long col;           /**< the column (1-based) */
} Place;

/** The state of the navigation. */
static struct {
    Place history[HISTORY_MAX];     /* where the jumps came from, oldest first */
    int depth;                      /* number of places */
    Place found[LOCATIONS_MAX];     /* the definitions of the last name */
    int count;                      /* number of them */
    int current;                    /* the one gone to */
    char name[NAME_MAX];            /* the name */
    int waiting;                    /* the answer is on its way */
    ULONG asked_at;                 /* when it was asked (CurrentTime() seconds) */
    Place from;                     /* where the request came from */
} nv;

/**
 * @brief Free a place's path.
 * @param p The place.
 */
static void clear_place(Place *p)
{
    free(p->path);
    p->path = NULL;
}

/**
 * @brief Copy a string with malloc().
 * @param s The string.
 * @return The copy, or NULL.
 */
static char *dup_str(const char *s)
{
    char *copy = malloc(strlen(s) + 1);
    if (copy != NULL) strcpy(copy, s);
    return copy;
}

/**
 * @brief Read the cursor of a document.
 * @param doc The document.
 * @param[out] line Its line (1-based).
 * @param[out] col Its column (1-based).
 */
static void cursor_of(Document *doc, long *line, long *col)
{
    ULONG x = 0, y = 0;
    GetAttr(GA_TEXTEDITOR_CursorX, doc->editor, &x);
    GetAttr(GA_TEXTEDITOR_CursorY, doc->editor, &y);
    *line = (long)y + 1;
    *col = (long)x + 1;
}

/**
 * @brief Tell whether a character may be part of a name.
 * @param c The character.
 * @return Non-zero if it may.
 */
static int name_char(unsigned char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

/**
 * @brief Take the name at the cursor from the editor's text.
 * @param doc The document.
 * @param line The cursor's line (1-based).
 * @param col Its column (1-based).
 * @param[out] name The name ("" if there is none).
 * @param size Size of @p name.
 */
static void name_at(Document *doc, long line, long col, char *name, size_t size)
{
    char *text = (char *)DoMethod(doc->editor, GM_TEXTEDITOR_ExportText, NULL);
    const char *p, *end;
    long n = 1, start, stop, len;
    name[0] = '\0';
    if (text == NULL) return;
    for (p = text; *p != '\0' && n < line; ++p)
        if (*p == '\n') ++n;
    for (end = p; *end != '\0' && *end != '\n'; ++end) ;
    len = (long)(end - p);
    start = col - 1 < len ? col - 1 : len;
    stop = start;
    while (start > 0 && name_char((unsigned char)p[start - 1])) --start;
    while (stop < len && name_char((unsigned char)p[stop])) ++stop;
    if (stop > start && (size_t)(stop - start) < size && !(p[start] >= '0' && p[start] <= '9')) {
        memcpy(name, p + start, (size_t)(stop - start));
        name[stop - start] = '\0';
    }
    FreeVec(text);
}

/**
 * @brief Note where the cursor is, for Go Back.
 * @param doc The active document.
 */
static void push_history(Document *doc)
{
    Place *p;
    if (doc == NULL || doc->path == NULL) return;
    if (nv.depth == HISTORY_MAX) {
        clear_place(&nv.history[0]);
        memmove(&nv.history[0], &nv.history[1], (HISTORY_MAX - 1) * sizeof(Place));
        --nv.depth;
    }
    p = &nv.history[nv.depth];
    if ((p->path = dup_str(doc->path)) == NULL) return;
    cursor_of(doc, &p->line, &p->col);
    ++nv.depth;
}

/**
 * @brief Go to one of the definitions found and say which.
 * @param app The application state.
 * @param index Its index in nv.found.
 */
static void go_to_found(EditorApp *app, int index)
{
    char text[200];
    nv.current = index;
    document_go_to(app, nv.found[index].path, nv.found[index].line, nv.found[index].col);
    if (nv.count > 1) {
        snprintf(text, sizeof(text), S(MSG_NAV_FOUND), nv.name, (long)(index + 1), (long)nv.count);
        ui_status_message(app, text);
    } else {
        ui_status_message(app, NULL);       /* "Looking for ..." is done */
    }
}

/**
 * @brief Tell whether the cursor is still where the last jump went.
 * @param doc The active document.
 * @return Non-zero if it is.
 */
static int at_current(Document *doc)
{
    long line, col;
    const Place *p;
    if (nv.count == 0 || doc == NULL || doc->path == NULL) return 0;
    p = &nv.found[nv.current];
    cursor_of(doc, &line, &col);
    return line == p->line && Stricmp((STRPTR)doc->path, (STRPTR)p->path) == 0;
}

void navigate_definition(EditorApp *app)
{
    Document *doc = app->active;
    char name[NAME_MAX], text[200];
    long line, col;
    int why;
    ULONG secs = 0, micros = 0;
    CurrentTime(&secs, &micros);
    /* an answer that does not come (Scout quit) does not block for long */
    if (nv.waiting && secs - nv.asked_at > 10) nv.waiting = 0;
    if (doc == NULL || nv.waiting) return;
    if (doc->path == NULL) {
        ui_status_message(app, S(MSG_SYM_NOFILE));
        return;
    }
    cursor_of(doc, &line, &col);
    name_at(doc, line, col, name, sizeof(name));
    /* again on the place it went to: the next definition */
    if (nv.count > 1 && at_current(doc) && strcmp(name, nv.name) == 0) {
        go_to_found(app, (nv.current + 1) % nv.count);
        return;
    }
    if (name[0] == '\0') {
        DisplayBeep(app->screen);
        ui_status_message(app, S(MSG_NAV_NONAME));
        return;
    }
    if ((why = scoutlink_unavailable(app)) != 0) {
        ui_status_message(app, S(why == 1 ? MSG_SYM_OFF : MSG_SYM_WAIT));
        return;
    }
    if (!scoutlink_request_definition(app, doc, line, col, name)) {
        ui_status_message(app, S(MSG_SYM_WAIT));
        return;
    }
    strcpy(nv.name, name);
    clear_place(&nv.from);
    nv.from.path = dup_str(doc->path);
    nv.from.line = line;
    nv.from.col = col;
    nv.waiting = 1;
    nv.asked_at = secs;
    snprintf(text, sizeof(text), S(MSG_NAV_LOOKING), name);
    ui_status_message(app, text);
}

void navigate_definition_answer(EditorApp *app, const char *json)
{
    JsonValue *root;
    const JsonValue *list;
    size_t i, n;
    char text[200];
    Document *doc = app->active;
    nv.waiting = 0;
    for (i = 0; i < (size_t)nv.count; ++i) clear_place(&nv.found[i]);
    nv.count = 0;
    nv.current = 0;
    root = json != NULL ? json_parse(json, NULL) : NULL;
    list = root != NULL ? json_obj_get_array(root, "locations") : NULL;
    n = list != NULL ? json_array_length(list) : 0;
    for (i = 0; i < n && nv.count < LOCATIONS_MAX; ++i) {
        const JsonValue *v = json_array_get(list, i);
        const char *file = json_obj_get_string(v, "file", NULL);
        const JsonValue *sel = json_obj_get_obj(v, "selection");
        const JsonValue *start = sel != NULL ? json_obj_get_array(sel, "start") : NULL;
        Place *p = &nv.found[nv.count];
        if (file == NULL || (p->path = dup_str(file)) == NULL) continue;
        p->line = start != NULL && json_array_length(start) > 0 ? json_array_get(start, 0)->u.num_val.int_val : 1;
        p->col = start != NULL && json_array_length(start) > 1 ? json_array_get(start, 1)->u.num_val.int_val : 1;
        ++nv.count;
    }
    json_value_free(root);
    if (nv.count == 0) {
        DisplayBeep(app->screen);
        snprintf(text, sizeof(text), S(json != NULL ? MSG_NAV_NOTFOUND : MSG_NAV_FAILED), nv.name);
        ui_status_message(app, text);
        return;
    }
    /* only if the user did not go to another document meanwhile */
    if (doc == NULL || doc->path == NULL || nv.from.path == NULL ||
        Stricmp((STRPTR)doc->path, (STRPTR)nv.from.path) != 0) {
        ui_status_message(app, NULL);
        return;
    }
    push_history(doc);
    go_to_found(app, 0);
}

void navigate_back(EditorApp *app)
{
    Place p;
    if (nv.depth == 0) {
        DisplayBeep(app->screen);
        ui_status_message(app, S(MSG_NAV_NOBACK));
        return;
    }
    p = nv.history[--nv.depth];
    document_go_to(app, p.path, p.line, p.col);
    free(p.path);
}

void navigate_cleanup(void)
{
    int i;
    for (i = 0; i < nv.depth; ++i) clear_place(&nv.history[i]);
    for (i = 0; i < nv.count; ++i) clear_place(&nv.found[i]);
    clear_place(&nv.from);
    nv.depth = nv.count = 0;
}
