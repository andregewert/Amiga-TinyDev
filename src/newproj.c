/**
 * @file newproj.c
 * @brief The wizard of Project/New Project... and Project/New File from
 *        Template...: choose a template, fill in its values, and let
 *        Sprout create the project or the files.
 *
 * The templates come from "Sprout LIST ICON <drawer>" (Sprout next to
 * TinyDev) (run once per opening of
 * the window, its JSON read with sprout_template_from_json()); only those
 * of the kind asked for ("project" or "file") are offered. The window is modal and built like the
 * project settings window (projwin.c). A page.gadget holds the pages:
 *
 * - page 0, the template: the categories (listbrowser), the templates of
 *   the chosen one (listbrowser, the symbol of the icon type drawn by a
 *   render hook, tmplsym.c), the chosen template's icon (or its symbol
 *   enlarged) with its name and description; below them, for a project,
 *   its name, the name of its drawer and where that drawer is created
 *   (string gadgets, ASL button; the setting "projectdir" is suggested,
 *   else the drawer holding the current project), with a line that shows
 *   the drawer that will be created (or that it exists already); for
 *   file templates the drawer of the files. The drawer's name follows
 *   the project's name (spaces and characters AmigaDOS paths do not like
 *   become '_') until the user changes it; string.gadget sends nothing
 *   while one types, so the fields are looked at on every wake-up
 *   (IDCMP_INTUITICKS);
 * - then one page per template with a form generated from its
 *   placeholders: a string gadget per value, a chooser where the template
 *   gives choices, the description as help bubble. All forms are built
 *   with the window, so no gadget is added to a layout later; the page
 *   gadget is as large as the largest form.
 *
 * The icons of the templates come with the list: Sprout copies them to a
 * drawer in the temporary drawer, TinyDev loads them (GetDiskObject(),
 * LayoutIconA()) and deletes the files at once. The box of the icon is as
 * large as the largest one; an icon larger than #ICON_MAX either way is
 * left out (the symbol is shown).
 *
 * The fields are filled in with the defaults expanded like Sprout does
 * (sprout_builtins() and sprout_expand() of sprout_tmpl.c, PROJECT and
 * DRAWER from page 0) whenever a form is shown; a field the user changed
 * keeps its value (it no longer equals the value filled in last). The
 * settings AUTHOR and AUTHORMAIL (Project Defaults) count as given
 * values: they fill fields of these keys before the template's defaults
 * and go to Sprout also when the template does not ask for them. A new
 * project's drawer must not exist yet.
 *
 * Create writes all values (and PROJECT if a name was given) as a JSON
 * object to a file in the drawer of the temporary files and runs
 * "<TinyDev's drawer>/Sprout <id> <drawer> VALUES <file> QUIET" synchronously (busy
 * pointer). Sprout writes its messages to the error stream of the Shell
 * (pr_CES, else Output()); TinyDev points both its output and, for the
 * time of the call, its own pr_CES and NP_Error at one file, so the
 * messages land there whichever stream the Shell hands on. Untested
 * assumption: SystemTags() passes NP_Error on to CreateNewProc() (the
 * own pr_CES covers a Shell that inherits it, Output() one that has
 * none). On an error the messages are shown and the window stays open;
 * on success the drawer becomes the project (tree_open_project()) and
 * the template's files of "open" are opened.
 */
#include "editor.h"
#include "json.h"
#include "sprout_tmpl.h"

#include <classes/window.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <dos/dostags.h>
#include <gadgets/button.h>
#include <gadgets/chooser.h>
#include <gadgets/layout.h>
#include <gadgets/listbrowser.h>
#include <gadgets/string.h>
#include <images/bevel.h>
#include <images/label.h>
#include <intuition/gadgetclass.h>
#include <intuition/imageclass.h>
#include <libraries/gadtools.h>
#include <workbench/icon.h>
#include <workbench/workbench.h>
#include <libraries/asl.h>
#include <proto/asl.h>
#include <proto/button.h>
#include <proto/chooser.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/icon.h>
#include <proto/intuition.h>
#include <proto/label.h>
#include <proto/layout.h>
#include <proto/listbrowser.h>
#include <proto/string.h>
#include <proto/utility.h>
#include <proto/window.h>
#include <clib/alib_protos.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern struct Library *ListBrowserBase;    /* shared with the settings window */
extern struct Library *StringBase;         /* shared with the find window */

/** Sprout, next to TinyDev (the Shell that runs it has no PROGDIR: of
 *  TinyDev, so the drawer is put in front by its full name). */
#define SPROUT_NAME "Sprout"

/** Gadget IDs of the wizard. */
enum {
    NP_CATS = 100, NP_TMPLS, NP_DESC, NP_NAME, NP_DRAWER, NP_CHOOSE,
    NP_BACK, NP_NEXT, NP_CREATE, NP_CANCEL, NP_SUBDIR,
    NP_FIELD = 1000             /* + template * FIELDS_PER + placeholder */
};

/** Gadget IDs per template form (IDs are WORDs for the help bubbles). */
#define FIELDS_PER 100
/** At most this many templates are offered (their field IDs stay WORDs). */
#define TEMPLATES_MAX 300

/**
 * @name Spacing
 * As in the settings window, in "virtual pixels".
 */
/** @{ */
#define FRAME_SIDE   8              /**< inside the frame of a page, left and right */
#define FRAME_TOPBOT 6              /**< inside the frame of a page, above and below */
#define ROW_SPACING  4              /**< between rows */
#define AREA_SPACING 6              /**< window border, list, frame, buttons */
/** @} */

#define ROW_PADDING 1               /**< above and below a template row */
#define LEFT_PADDING 2              /**< left of a template symbol */
#define TEXT_GAP 5                  /**< between symbol and name */
#define BIG_SCALE 3                 /**< the enlarged symbol: pixels per symbol pixel */
#define DESC_CHARS 34               /**< width of the description, in average characters */
#define ICON_MAX 128                /**< the largest template icon shown, in pixels either way */

/** Longest drawer name the project's name suggests (FFS without long names). */
#define SUBDIR_MAX 30
/** Characters a suggested drawer name gets as '_' instead. */
#define SUBDIR_BAD " :/\"#?*"

/** the child just added keeps its height */
#define FIXED CHILD_WeightedHeight, 0

/** A template offered by the wizard, with its form. */
typedef struct NpTemplate {
    char *id;                   /**< Sprout's name of the template (malloc()ed) */
    SproutTemplate t;           /**< its metadata */
    int category;               /**< index in NpState::cats */
    Object **fields;            /**< per placeholder: string gadget or chooser */
    char **autos;               /**< per placeholder: the value filled in last (malloc()ed), or NULL */
    char **labels;              /**< per placeholder: the label's text (malloc()ed) */
    STRPTR **choices;           /**< per placeholder: chooser labels (NULL-terminated, malloc()ed), or NULL */
    char *frame;                /**< the form's frame title (malloc()ed) */
    struct DiskObject *icon;    /**< the template's own icon, or NULL */
} NpTemplate;

/** State of the open wizard (there is one at a time). */
static struct {
    EditorApp *app;
    NpTemplate *tmpl;               /* the templates */
    int count;
    char **cats;                    /* the categories, sorted (pointers into the templates) */
    int ncats;
    int cat;                        /* the category shown */
    int cur;                        /* the template chosen, or -1 */
    int page;                       /* the page shown: 0, or 1 + template */
    int files;                      /* file templates (else project templates) */
    char project[128];              /* file templates: the open project's name, "" if none */
    long days;                      /* today, days since 1978 */
    UWORD iconw, iconh;             /* the box of the icon: the largest icon */
    Object *winobj, *layout, *page_gad, *catlist_gad, *tmpllist_gad, *desc_gad;
    Object *icon, *title, *name, *drawer, *back, *next, *create;
    Object *subdir, *result;        /* projects: the drawer's name, the line with the result */
    char last_name[81];             /* the project's name looked at last */
    char last_auto[SUBDIR_MAX + 1]; /* the drawer's name filled in last */
    char last_target[EDITOR_PATH_MAX];  /* the drawer the result line shows */
    char result_text[EDITOR_PATH_MAX + 64]; /* the result line */
    int result_set;                 /* the result line shows last_target */
    struct Window *win;
    struct DrawInfo *dri;
    struct List catnodes, tmplnodes, descnodes;
    struct ColumnInfo catcols[2], tmplcols[2];
    struct Hook render_hook;
    WORD row_height;
    struct HintInfo *hints;
} np;

/* ---- helpers --------------------------------------------------------- */

/**
 * @brief Read an attribute.
 * @param o The object.
 * @param attr The attribute.
 * @return Its value.
 */
static ULONG get(Object *o, ULONG attr)
{
    ULONG v = 0;
    GetAttr(attr, o, &v);
    return v;
}

/**
 * @brief Copy a string with malloc().
 * @param s The string.
 * @return The copy, or NULL if out of memory.
 */
static char *dup(const char *s)
{
    size_t n = strlen(s) + 1;
    char *d = malloc(n);
    if (d != NULL) memcpy(d, s, n);
    return d;
}

/**
 * @brief Choose the text for the kind of the wizard.
 * @param project The text for project templates (MSG_).
 * @param file The text for file templates (MSG_).
 * @return The text.
 */
static const char *text_for(LONG project, LONG file)
{
    return S(np.files ? file : project);
}

/**
 * @brief The project's name for PROJECT: the one given on page 0, or for
 *        file templates the open project's.
 * @return The name, "" if none.
 */
static const char *project_name(void)
{
    const char *name;
    if (np.files) return np.project;
    name = (const char *)get(np.name, STRINGA_TextVal);
    return name != NULL ? name : "";
}

/**
 * @brief Read a string gadget's text.
 * @param o The gadget (may be NULL).
 * @return Its text, "" if none.
 */
static const char *text_of(Object *o)
{
    const char *t = o != NULL ? (const char *)get(o, STRINGA_TextVal) : NULL;
    return t != NULL ? t : "";
}

/**
 * @brief Suggest a drawer name for a project's name: spaces at the ends
 *        dropped, SUBDIR_BAD characters as '_', at most SUBDIR_MAX.
 * @param name The project's name.
 * @param[out] out The drawer name.
 * @param size Size of @p out.
 */
static void suggest_subdir(const char *name, char *out, size_t size)
{
    size_t n = 0, len = strlen(name);
    while (len > 0 && name[len - 1] == ' ') --len;
    while (len > 0 && *name == ' ') { ++name; --len; }
    for (; len > 0 && n + 1 < size && n < SUBDIR_MAX; ++name, --len)
        out[n++] = strchr(SUBDIR_BAD, *name) != NULL ? '_' : *name;
    out[n] = '\0';
}

/**
 * @brief Work out the drawer Sprout is to create (a project's: "Create
 *        in" and the drawer's name) or fill (file templates).
 * @param[out] out The drawer.
 * @param size Size of @p out.
 * @return Non-zero if there is one; 0 if a part is missing or too long.
 */
static int target_drawer(char *out, size_t size)
{
    const char *where = text_of(np.drawer), *sub;
    out[0] = '\0';
    if (np.files) {
        if (where[0] == '\0' || strlen(where) >= size) return 0;
        strcpy(out, where);
        return 1;
    }
    sub = text_of(np.subdir);
    if (where[0] == '\0' || sub[0] == '\0' || strlen(where) >= size) return 0;
    strcpy(out, where);
    return AddPart((STRPTR)out, (STRPTR)sub, (ULONG)size) != 0;
}

static void set_page(Object *o, ULONG attr, ULONG value);

/**
 * @brief Projects: let the drawer's name follow the project's name (until
 *        the user changed it) and show the drawer that will be created.
 *        Called on every wake-up of the window, as the string gadgets tell
 *        nothing while one types.
 */
static void follow_fields(void)
{
    char target[EDITOR_PATH_MAX];
    const char *name, *shown;
    BPTR lock;
    if (np.files || np.win == NULL || np.subdir == NULL) return;
    name = text_of(np.name);
    if (strcmp(name, np.last_name) != 0) {
        strncpy(np.last_name, name, sizeof(np.last_name) - 1);
        if (strcmp(text_of(np.subdir), np.last_auto) == 0) {
            suggest_subdir(name, np.last_auto, sizeof(np.last_auto));
            set_page(np.subdir, STRINGA_TextVal, (ULONG)np.last_auto);
        }
    }
    if (!target_drawer(target, sizeof(target))) target[0] = '\0';
    if (np.result_set && strcmp(target, np.last_target) == 0) return;
    strcpy(np.last_target, target);
    np.result_set = 1;
    shown = np.result_text;
    if (target[0] == '\0') {
        shown = "";
    } else if ((lock = Lock((STRPTR)target, ACCESS_READ)) != 0) {
        UnLock(lock);
        snprintf(np.result_text, sizeof(np.result_text), S(MSG_NP_EXISTSNOTE), target);
    } else {
        snprintf(np.result_text, sizeof(np.result_text), S(MSG_NP_RESULT), target);
    }
    set_page(np.result, GA_Text, (ULONG)shown);
}

/**
 * @brief Show a message over the wizard (or the main window); the text is
 *        not taken as a format.
 * @param text The message.
 */
static void message(const char *text)
{
    struct EasyStruct es = {sizeof(es), 0, (STRPTR)text_for(MSG_NP_TITLE, MSG_NF_TITLE), (STRPTR)"%s",
                            (STRPTR)S(MSG_OK)};
    ULONG args[1];
    args[0] = (ULONG)text;
    EasyRequestArgs(np.win != NULL ? np.win : np.app->window, &es, NULL, args);
}

/**
 * @brief Show a message with a %s filled in.
 * @param id The message (MSG_, with one %s).
 * @param arg The text for it.
 */
static void message_arg(LONG id, const char *arg)
{
    size_t n = strlen(S(id)) + strlen(arg) + 1;
    char *text = malloc(n);
    if (text == NULL) {
        message(S(MSG_PJ_NOMEM));
        return;
    }
    snprintf(text, n, S(id), arg);
    message(text);
    free(text);
}

/**
 * @brief A label.image with a text; the text may contain '_' (no
 *        shortcut).
 * @param text The text.
 * @param underscore Non-zero if '_' marks a shortcut.
 * @return The label.
 */
static Object *label(const char *text, int underscore)
{
    return NewObject(LABEL_GetClass(), NULL, LABEL_Underscore, (ULONG)(underscore ? '_' : 0),
                     LABEL_Text, (ULONG)text, TAG_END);
}

/**
 * @brief A button as wide as its text.
 * @param id Gadget ID.
 * @param text Its text.
 * @return The gadget.
 */
static Object *button(ULONG id, const char *text)
{
    return NewObject(BUTTON_GetClass(), NULL,
        GA_ID, id, GA_RelVerify, TRUE, GA_Text, (ULONG)text,
        BUTTON_TextPadding, TRUE, TAG_END);
}

/**
 * @brief A text field.
 * @param id Gadget ID.
 * @param text Its contents.
 * @param max Its size (characters plus one).
 * @return The gadget.
 */
static Object *string(ULONG id, const char *text, ULONG max)
{
    return NewObject(STRING_GetClass(), NULL,
        GA_ID, id, GA_RelVerify, TRUE, GA_TabCycle, TRUE,
        STRINGA_MaxChars, max, STRINGA_TextVal, (ULONG)text, TAG_END);
}

/** @brief An empty group that takes the remaining space. @return It. */
static Object *filler(void)
{
    return NewObject(LAYOUT_GetClass(), NULL, TAG_END);
}

/**
 * @brief Width of a number of average characters of the screen font.
 * @param n The number of characters.
 * @return The width in pixels.
 */
static ULONG chars(ULONG n)
{
    LONG w = TextLength(&np.app->screen->RastPort, (STRPTR)"n", 1);
    return n * (ULONG)w;
}

/**
 * @brief Height of a number of text lines of the screen font.
 * @param n The number of lines.
 * @return The height in pixels.
 */
static ULONG lines(ULONG n)
{
    return n * (ULONG)(np.app->screen->RastPort.TxHeight + 1U);
}

/**
 * @brief Drop the input of a window that waits under a modal one.
 * @param winobj The window (window.class).
 */
static void drop_input(Object *winobj)
{
    WORD code;
    while (DoMethod(winobj, WM_HANDLEINPUT, &code) != WMHI_LASTMSG) ;
}

/**
 * @brief Set an attribute of a gadget that may be on a hidden page.
 * @param o The gadget.
 * @param attr The attribute.
 * @param value Its value.
 */
static void set_page(Object *o, ULONG attr, ULONG value)
{
    SetPageGadgetAttrs((struct Gadget *)o, np.page_gad, np.win, NULL, attr, value, TAG_END);
}

/**
 * @brief Put a string in double quotes for the Shell (BCPL escapes: *",
 *        ** and *N).
 * @param[out] out The result.
 * @param size Its size.
 * @param s The string.
 * @return Non-zero if it fitted.
 */
static int quote(char *out, size_t size, const char *s)
{
    size_t n = 0;
    if (size < 3) return 0;
    out[n++] = '"';
    for (; *s != '\0'; ++s) {
        const char *put = *s == '"' ? "*\"" : *s == '*' ? "**" : *s == '\n' ? "*N" : NULL;
        size_t len = put != NULL ? 2 : 1;
        if (n + len + 2 > size) return 0;
        if (put != NULL) memcpy(out + n, put, 2);
        else out[n] = *s;
        n += len;
    }
    out[n++] = '"';
    out[n] = '\0';
    return 1;
}

/**
 * @brief Run Sprout synchronously, its output and messages to files.
 * @param args The arguments after the command.
 * @param out The file for the output.
 * @param err The file for the messages (may be @p out).
 * @return Sprout's return code, or -1 if it could not be run.
 */
static LONG run_sprout(const char *args, const char *out, const char *err)
{
    char program[EDITOR_PATH_MAX], quoted[EDITOR_PATH_MAX + 16], command[EDITOR_PATH_MAX * 4 + 64];
    struct Process *me = (struct Process *)FindTask(NULL);
    BPTR in, ofh, efh, old_ces;
    LONG rc = -1;

    if (!NameFromLock(GetProgramDir(), (STRPTR)program, sizeof(program)) ||
        !AddPart((STRPTR)program, (STRPTR)SPROUT_NAME, sizeof(program)) ||
        !quote(quoted, sizeof(quoted), program))
        return -1;
    snprintf(command, sizeof(command), "%s %s", quoted, args);
    if ((in = Open((STRPTR)"NIL:", MODE_OLDFILE)) == 0) return -1;
    if ((ofh = Open((STRPTR)out, MODE_NEWFILE)) == 0) {
        Close(in);
        return -1;
    }
    efh = strcmp(out, err) == 0 ? ofh : Open((STRPTR)err, MODE_NEWFILE);
    if (efh != 0) {
        /* the messages go to pr_CES, else Output(): see the file comment */
        old_ces = me->pr_CES;
        me->pr_CES = efh;
        rc = SystemTags((STRPTR)command, SYS_Input, (ULONG)in, SYS_Output, (ULONG)ofh,
                        NP_Error, (ULONG)efh, NP_CloseError, FALSE, TAG_DONE);
        me->pr_CES = old_ces;
        if (efh != ofh) Close(efh);
    }
    Close(ofh);
    Close(in);
    return rc;
}

/**
 * @brief Read a file of Sprout's messages for showing them: at most about
 *        1500 characters, without the trailing line end.
 * @param path The file.
 * @return The text (free() it); NULL if there is none.
 */
static char *read_messages(const char *path)
{
    char *text = NULL;
    size_t n;
    if (datafile_read(path, &text) <= 0) return NULL;
    n = strlen(text);
    if (n > 1500) {
        n = 1500;
        memcpy(text + n - 3, "...", 3);
    }
    while (n > 0 && (text[n - 1] == '\n' || text[n - 1] == ' ')) --n;
    text[n] = '\0';
    if (n == 0) {
        free(text);
        return NULL;
    }
    return text;
}

/* ---- the templates --------------------------------------------------- */

/**
 * @brief Free the templates and their forms' texts (the gadgets went with
 *        the window).
 */
static void free_templates(void)
{
    int i, k;
    for (i = 0; i < np.count; ++i) {
        NpTemplate *p = &np.tmpl[i];
        for (k = 0; k < p->t.nph; ++k) {
            if (p->autos != NULL) free(p->autos[k]);
            if (p->labels != NULL) free(p->labels[k]);
            if (p->choices != NULL) free(p->choices[k]);
        }
        free(p->fields);
        free(p->autos);
        free(p->labels);
        free(p->choices);
        free(p->frame);
        free(p->id);
        if (p->icon != NULL) FreeDiskObject(p->icon);
        sprout_template_free(&p->t);
    }
    free(np.tmpl);
    free(np.cats);
    np.tmpl = NULL;
    np.cats = NULL;
    np.count = np.ncats = 0;
}

/**
 * @brief Take the templates of Sprout's LIST output: the valid project
 *        templates, and their categories sorted, Sprout's "Other" (for
 *        templates without a category) in the user's language and last.
 * @param root The output.
 * @return 1 on success; -1 if out of memory.
 */
static int take_templates(const JsonValue *root)
{
    const char *other = S(MSG_NP_CAT_OTHER);
    const JsonValue *arr = json_obj_get_array(root, "templates");
    size_t i, n = json_array_length(arr);
    int c, j;

    if (n > TEMPLATES_MAX) n = TEMPLATES_MAX;
    if (n > 0 && (np.tmpl = calloc(n, sizeof(NpTemplate))) == NULL) return -1;
    for (i = 0; i < n; ++i) {
        const JsonValue *e = json_array_get(arr, i);
        const char *id = json_obj_get_string(e, "id", NULL);
        NpTemplate *p = &np.tmpl[np.count];
        int r;
        /* templates Sprout could not read come with an "error": left out */
        if (id == NULL || json_obj_get(e, "error") != NULL) continue;
        r = sprout_template_from_json(&p->t, e, NULL, NULL, NULL);
        if (r < 0) return -1;
        if (r == 0 || (p->t.kind != NULL && strcmp(p->t.kind, np.files ? "file" : "project") != 0)) {
            sprout_template_free(&p->t);
            continue;
        }
        if ((p->id = dup(id)) == NULL) {
            sprout_template_free(&p->t);
            return -1;
        }
        ++np.count;
        /* Sprout's category for templates without one: in the user's language */
        if (strcmp(p->t.category, "Other") == 0) {
            char *cat = dup(other);
            if (cat == NULL) return -1;
            free(p->t.category);
            p->t.category = cat;
        }
    }
    /* the categories: each once, sorted, "Other" last (insertion sort; few of them) */
    if (np.count > 0 && (np.cats = malloc((size_t)np.count * sizeof(char *))) == NULL) return -1;
    for (i = 0; i < (size_t)np.count; ++i) {
        const char *cat = np.tmpl[i].t.category;
        for (c = 0; c < np.ncats && strcmp(np.cats[c], cat) != 0; ++c) ;
        if (c < np.ncats) continue;
        for (c = np.ncats; c > 0 && strcmp(cat, other) != 0 &&
                           (strcmp(np.cats[c - 1], other) == 0 || Stricmp((STRPTR)np.cats[c - 1], (STRPTR)cat) > 0); --c)
            np.cats[c] = np.cats[c - 1];
        np.cats[c] = (char *)cat;
        ++np.ncats;
    }
    for (i = 0; i < (size_t)np.count; ++i)
        for (j = 0; j < np.ncats; ++j)
            if (strcmp(np.cats[j], np.tmpl[i].t.category) == 0) np.tmpl[i].category = j;
    return 1;
}

/**
 * @brief Load the icons Sprout copied to a drawer (\<id\>.info) and note
 *        the largest size.
 * @param dir The drawer.
 */
static void load_icons(const char *dir)
{
    char path[EDITOR_PATH_MAX];
    int i;
    WORD w, h;
    if (IconBase == NULL) return;
    for (i = 0; i < np.count; ++i) {
        NpTemplate *p = &np.tmpl[i];
        strcpy(path, dir);
        if (p->t.icon_file == NULL || !AddPart((STRPTR)path, (STRPTR)p->id, sizeof(path)) ||
            (p->icon = GetDiskObject((STRPTR)path)) == NULL)
            continue;
        LayoutIconA(p->icon, np.app->screen, NULL);
        if (!tmplsym_icon_size(p->icon, &w, &h) || w > ICON_MAX || h > ICON_MAX) {
            FreeDiskObject(p->icon);
            p->icon = NULL;
            continue;
        }
        if ((UWORD)w > np.iconw) np.iconw = (UWORD)w;
        if ((UWORD)h > np.iconh) np.iconh = (UWORD)h;
    }
}

/**
 * @brief Delete the drawer of the icons: the icon of every template in
 *        Sprout's list (also those the wizard leaves out), then the drawer.
 * @param dir The drawer.
 * @param root Sprout's list, may be NULL.
 */
static void delete_icons(const char *dir, const JsonValue *root)
{
    const JsonValue *arr = json_obj_get_array(root, "templates");
    char path[EDITOR_PATH_MAX];
    size_t i, n = json_array_length(arr);
    for (i = 0; i < n; ++i) {
        const char *id = json_obj_get_string(json_array_get(arr, i), "id", NULL);
        strcpy(path, dir);
        if (id != NULL && AddPart((STRPTR)path, (STRPTR)id, sizeof(path)) && strlen(path) + 5 < sizeof(path)) {
            strcat(path, ".info");
            DeleteFile((STRPTR)path);
        }
    }
    DeleteFile((STRPTR)dir);
}

/**
 * @brief Read the templates with "Sprout LIST".
 * @return Non-zero if there is at least one (otherwise a message was
 *         shown).
 */
static int load_templates(void)
{
    char out[EDITOR_PATH_MAX], err[EDITOR_PATH_MAX], icons[EDITOR_PATH_MAX];
    char args[EDITOR_PATH_MAX + 16], quoted[EDITOR_PATH_MAX];
    char *text = NULL, *msgs;
    JsonValue *root = NULL;
    BPTR lock;
    LONG rc;
    int ok = 0;

    if (!temp_name(np.app, ".json", out, sizeof(out)) || !temp_name(np.app, ".txt", err, sizeof(err)) ||
        !temp_name(np.app, "", icons, sizeof(icons))) {
        message(S(MSG_PJ_NOMEM));
        return 0;
    }
    /* the icons into a drawer of their own; without it, no icons */
    strcpy(args, "LIST");
    if ((lock = CreateDir((STRPTR)icons)) != 0) {
        UnLock(lock);
        if (quote(quoted, sizeof(quoted), icons)) snprintf(args, sizeof(args), "LIST ICON %s", quoted);
    } else icons[0] = '\0';
    rc = run_sprout(args, out, err);
    if (rc < 0) {
        message(S(MSG_NP_NOSPROUT));
        goto out;
    }
    if (datafile_read(out, &text) > 0) root = json_parse(text, NULL);
    if (rc >= 10 || root == NULL) {
        /* the messages, or whatever came instead of the list */
        msgs = read_messages(err);
        if (msgs == NULL) msgs = read_messages(out);
        message_arg(MSG_NP_LISTFAILED, msgs != NULL ? msgs : "");
        free(msgs);
        goto out;
    }
    if (take_templates(root) < 0) message(S(MSG_PJ_NOMEM));
    else if (np.count == 0) message(text_for(MSG_NP_NOTEMPLATES, MSG_NF_NOTEMPLATES));
    else ok = 1;
out:
    if (icons[0] != '\0') {
        if (ok) load_icons(icons);
        delete_icons(icons, root);
    }
    json_value_free(root);
    free(text);
    DeleteFile((STRPTR)out);
    DeleteFile((STRPTR)err);
    return ok;
}

/* ---- page 0: the template ------------------------------------------- */

/**
 * @brief Render hook of the template rows: background, symbol of the icon
 *        type and name (cut to the row's width). Graphics only.
 * @param hook The hook.
 * @param node The row's node.
 * @param msg The draw message.
 * @return LBCB_OK, or LBCB_UNKNOWN for other methods.
 */
static ULONG render_row(struct Hook *hook, struct Node *node, struct LBDrawMsg *msg)
{
    struct RastPort *rp = msg->lbdm_RastPort;
    struct DrawInfo *dri = msg->lbdm_DrawInfo;
    struct Rectangle *b = &msg->lbdm_Bounds;
    int selected = msg->lbdm_State == LBR_SELECTED;
    ULONG index = 0, length, fit;
    const NpTemplate *p;
    WORD x, height = (WORD)(b->MaxY - b->MinY + 1);
    struct TextExtent te;

    (void)hook;
    if (msg->lbdm_MethodID != LV_DRAW) return LBCB_UNKNOWN;
    if (dri == NULL) return LBCB_OK;
    GetListBrowserNodeAttrs(node, LBNA_UserData, (ULONG)&index, TAG_END);
    if ((int)index >= np.count) return LBCB_OK;
    p = &np.tmpl[index];
    SetAPen(rp, (UBYTE)dri->dri_Pens[selected ? FILLPEN : BACKGROUNDPEN]);
    RectFill(rp, b->MinX, b->MinY, b->MaxX, b->MaxY);
    x = (WORD)(b->MinX + LEFT_PADDING);
    tmplsym_draw(rp, x, (WORD)(b->MinY + (height - TMPLSYM_SIZE) / 2), dri, tmplsym_type(p->t.icon_type), 1);
    x = (WORD)(x + TMPLSYM_SIZE + TEXT_GAP);
    if (x > b->MaxX) return LBCB_OK;
    length = strlen(p->t.name);
    fit = TextFit(rp, (STRPTR)p->t.name, (UWORD)length, &te, NULL, 1, (UWORD)(b->MaxX - x + 1),
                  (UWORD)height);
    SetAPen(rp, (UBYTE)dri->dri_Pens[selected ? FILLTEXTPEN : TEXTPEN]);
    SetDrMd(rp, JAM1);
    Move(rp, x, (WORD)(b->MinY + (height - rp->TxHeight) / 2 + rp->TxBaseline));
    Text(rp, (STRPTR)p->t.name, (UWORD)fit);
    return LBCB_OK;
}

/**
 * @brief Set a listbrowser's nodes (detach, change, attach).
 * @param o The listbrowser.
 * @param list The nodes, or NULL to detach them.
 * @param sel The row to select, or -1.
 */
static void set_labels(Object *o, struct List *list, LONG sel)
{
    if (list == NULL) {
        set_page(o, LISTBROWSER_Labels, ~0UL);
        return;
    }
    SetPageGadgetAttrs((struct Gadget *)o, np.page_gad, np.win, NULL,
        LISTBROWSER_Labels, (ULONG)list, LISTBROWSER_Selected, (ULONG)sel,
        sel >= 0 ? LISTBROWSER_MakeVisible : TAG_IGNORE, (ULONG)sel, TAG_END);
}

/**
 * @brief Build the rows of a description, broken into lines that fit the
 *        description's width.
 * @param text The description, may be NULL.
 */
static void make_desc(const char *text)
{
    struct RastPort *rp = &np.app->screen->RastPort;
    ULONG width = chars(DESC_CHARS);
    const char *line, *end, *brk;
    char buf[256];
    struct Node *n;

    NewList(&np.descnodes);
    for (line = text; line != NULL && *line != '\0'; line = end) {
        /* the longest part up to a space that fits, or one word */
        brk = NULL;
        for (end = line; *end != '\0' && *end != '\n'; ++end) {
            if (*end == ' ') brk = end;
            if (end - line >= (long)sizeof(buf) - 1 ||
                (ULONG)TextLength(rp, (STRPTR)line, (UWORD)(end - line + 1)) > width) {
                if (brk != NULL) end = brk;
                break;
            }
        }
        if (end == line && *end != '\0' && *end != '\n') ++end;     /* nothing fits: one character */
        memcpy(buf, line, (size_t)(end - line));
        buf[end - line] = '\0';
        if ((n = AllocListBrowserNode(1, LBNCA_CopyText, TRUE, LBNCA_Text, (ULONG)buf, TAG_END)) != NULL)
            AddTail(&np.descnodes, n);
        if (*end == ' ' || *end == '\n') ++end;
    }
}

/**
 * @brief Show the chosen template on the right: symbol, name, description;
 *        ghost Next and Create without one.
 */
static void show_details(void)
{
    const NpTemplate *p = np.cur >= 0 ? &np.tmpl[np.cur] : NULL;
    struct Image *im = (struct Image *)np.icon;

    SetAttrs(np.icon, TMPLSYM_Type, (ULONG)(p != NULL ? tmplsym_type(p->t.icon_type) : TMPLSYM_GENERIC),
             TMPLSYM_Icon, (ULONG)(p != NULL ? p->icon : NULL), TAG_END);
    if (np.win != NULL && np.page == 0 && np.dri != NULL) {
        EraseRect(np.win->RPort, im->LeftEdge, im->TopEdge,
                  (WORD)(im->LeftEdge + im->Width - 1), (WORD)(im->TopEdge + im->Height - 1));
        if (p != NULL) DrawImageState(np.win->RPort, im, 0, 0, IDS_NORMAL, np.dri);
    }
    set_page(np.title, GA_Text, (ULONG)(p != NULL ? p->t.name : ""));
    set_labels(np.desc_gad, NULL, -1);
    FreeListBrowserList(&np.descnodes);
    make_desc(p != NULL ? p->t.description : NULL);
    set_labels(np.desc_gad, &np.descnodes, -1);
    SetGadgetAttrs((struct Gadget *)np.next, np.win, NULL, GA_Disabled, (ULONG)(p == NULL || np.page != 0),
                   TAG_END);
    SetGadgetAttrs((struct Gadget *)np.create, np.win, NULL, GA_Disabled, (ULONG)(p == NULL), TAG_END);
}

/**
 * @brief Show the templates of a category, sorted by name, the first one
 *        chosen.
 * @param cat The category.
 */
static void show_category(int cat)
{
    int i, k, count = 0, *order;
    struct Node *n;

    np.cat = cat;
    np.cur = -1;
    if (np.tmpllist_gad != NULL) set_labels(np.tmpllist_gad, NULL, -1);
    if (np.tmplnodes.lh_Head != NULL) FreeListBrowserList(&np.tmplnodes);
    NewList(&np.tmplnodes);
    if ((order = malloc((size_t)np.count * sizeof(int))) != NULL) {
        for (i = 0; i < np.count; ++i) {
            if (np.tmpl[i].category != cat) continue;
            for (k = count; k > 0 && Stricmp((STRPTR)np.tmpl[order[k - 1]].t.name,
                                             (STRPTR)np.tmpl[i].t.name) > 0; --k)
                order[k] = order[k - 1];
            order[k] = i;
            ++count;
        }
        for (k = 0; k < count; ++k) {
            n = AllocListBrowserNode(1, LBNA_UserData, (ULONG)order[k],
                LBNA_Column, 0, LBNCA_RenderHook, (ULONG)&np.render_hook,
                LBNCA_HookHeight, (ULONG)np.row_height, TAG_END);
            if (n != NULL) AddTail(&np.tmplnodes, n);
        }
        if (count > 0) np.cur = order[0];
        free(order);
    }
    if (np.tmpllist_gad != NULL) {
        set_labels(np.tmpllist_gad, &np.tmplnodes, np.cur >= 0 ? 0 : -1);
        show_details();
    }
}

/**
 * @brief Take the template chosen in the list.
 */
static void template_chosen(void)
{
    struct Node *n = (struct Node *)get(np.tmpllist_gad, LISTBROWSER_SelectedNode);
    ULONG index = 0;
    if (n == NULL) return;
    GetListBrowserNodeAttrs(n, LBNA_UserData, (ULONG)&index, TAG_END);
    if ((int)index >= np.count || (int)index == np.cur) return;
    np.cur = (int)index;
    show_details();
}

/**
 * @brief Let the user choose with the ASL requester where a project is
 *        created, or the drawer of the files.
 */
static void choose_drawer(void)
{
    const char *cur = (const char *)get(np.drawer, STRINGA_TextVal);
    struct FileRequester *fr = AllocAslRequestTags(ASL_FileRequest,
        ASLFR_TitleText, (ULONG)text_for(MSG_NP_ASL, MSG_NF_ASL), ASLFR_DrawersOnly, TRUE,
        TAG_END);
    if (fr == NULL) return;
    if (AslRequestTags(fr, ASLFR_Window, (ULONG)np.win, ASLFR_SleepWindow, TRUE,
                       cur != NULL && cur[0] != '\0' ? ASLFR_InitialDrawer : TAG_IGNORE, (ULONG)cur,
                       TAG_END) && fr->fr_Drawer[0] != '\0')
        set_page(np.drawer, STRINGA_TextVal, (ULONG)fr->fr_Drawer);
    FreeAslRequest(fr);
}

/* ---- the forms ------------------------------------------------------- */

/**
 * @brief Return the value in a field of a form.
 * @param p The template.
 * @param k The placeholder.
 * @return The value (owned by the gadget or the template).
 */
static const char *field_value(const NpTemplate *p, int k)
{
    const char *v;
    if (p->choices != NULL && p->choices[k] != NULL) {
        ULONG sel = get(p->fields[k], CHOOSER_Selected);
        return sel < (ULONG)p->t.ph[k].nchoices ? p->t.ph[k].choices[sel] : "";
    }
    v = (const char *)get(p->fields[k], STRINGA_TextVal);
    return v != NULL ? v : "";
}

/**
 * @brief Work out the values of a template's form: the built-ins from
 *        page 0, then each field in its order; a field the user has not
 *        changed gets its default expanded anew.
 * @param p The template.
 * @param[out] v The values (initialised here; sprout_values_free() it).
 * @param fill Non-zero to write the defaults into the fields.
 * @return 1 on success; -1 if out of memory.
 */
static int form_values(NpTemplate *p, SproutValues *v, int fill)
{
    char target[EDITOR_PATH_MAX], drawer[EDITOR_PATH_MAX];
    int k, j;

    sprout_values_init(v);
    if (!target_drawer(target, sizeof(target))) target[0] = '\0';
    sprout_drawer_name(target, drawer, sizeof(drawer));
    if (sprout_builtins(v, project_name(), drawer, np.days) < 0 ||
        sprout_values_set(v, "AUTHOR", np.app->settings.author) < 0 ||
        sprout_values_set(v, "AUTHORMAIL", np.app->settings.authormail) < 0)
        goto nomem;
    for (k = 0; k < p->t.nph; ++k) {
        const SproutPlaceholder *ph = &p->t.ph[k];
        const char *cur = field_value(p, k);
        char *expanded = NULL;
        int r;
        /* a chooser always shows a value; before the first filling in it
         * counts as unchanged */
        if (fill && (p->autos[k] == NULL ? ph->nchoices > 0 || cur[0] == '\0' : strcmp(cur, p->autos[k]) == 0)) {
            /* unchanged: the default, else the first choice, else empty */
            const char *val = ph->nchoices > 0 ? ph->choices[0] : "";
            size_t n;
            if (sprout_key_standard(ph->key) && sprout_values_get(v, ph->key)[0] != '\0') {
                /* AUTHOR, AUTHORMAIL from the settings: like a given value */
                val = sprout_values_get(v, ph->key);
            } else if (ph->def != NULL) {
                if ((expanded = sprout_expand(ph->def, strlen(ph->def), v, &n)) == NULL) goto nomem;
                val = expanded;
            }
            if (ph->nchoices > 0) {
                for (j = 0; j < ph->nchoices && strcmp(ph->choices[j], val) != 0; ++j) ;
                if (j == ph->nchoices) j = 0;
                set_page(p->fields[k], CHOOSER_Selected, (ULONG)j);
                val = ph->choices[j];
            } else set_page(p->fields[k], STRINGA_TextVal, (ULONG)val);
            free(p->autos[k]);
            if ((p->autos[k] = dup(val)) == NULL) {
                free(expanded);
                goto nomem;
            }
            cur = field_value(p, k);
        }
        r = sprout_values_set(v, ph->key, cur);
        free(expanded);
        if (r < 0) goto nomem;
    }
    return 1;
nomem:
    sprout_values_free(v);
    return -1;
}

/**
 * @brief Fill in the defaults of a template's form.
 * @param p The template.
 * @return Non-zero on success (otherwise a message was shown).
 */
static int fill_form(NpTemplate *p)
{
    SproutValues v;
    if (form_values(p, &v, 1) < 0) {
        message(S(MSG_PJ_NOMEM));
        return 0;
    }
    sprout_values_free(&v);
    return 1;
}

/**
 * @brief Append a tag to a tag list being built.
 * @param tags The list.
 * @param[in,out] t The number of tags in it.
 * @param tag The tag.
 * @param data Its value.
 */
static void put_tag(struct TagItem *tags, int *t, ULONG tag, ULONG data)
{
    tags[*t].ti_Tag = tag;
    tags[*t].ti_Data = data;
    ++*t;
}

/**
 * @brief Build the form page of a template (with one tag list: adding
 *        children with OM_SET needs layout.gadget V47).
 * @param i The template.
 * @return The page, or NULL.
 */
static Object *build_form(int i)
{
    NpTemplate *p = &np.tmpl[i];
    struct TagItem *tags;
    Object *group;
    int k, t = 0, required = 0, n = p->t.nph;
    size_t len;

    len = strlen(S(MSG_NP_VALUES)) + strlen(p->t.name) + 1;
    if ((p->frame = malloc(len)) == NULL) return NULL;
    snprintf(p->frame, len, S(MSG_NP_VALUES), p->t.name);
    if (n > FIELDS_PER) n = p->t.nph = FIELDS_PER;  /* more cannot be told apart by ID */
    if (n > 0 && ((p->fields = calloc((size_t)n, sizeof(Object *))) == NULL ||
                  (p->autos = calloc((size_t)n, sizeof(char *))) == NULL ||
                  (p->labels = calloc((size_t)n, sizeof(char *))) == NULL ||
                  (p->choices = calloc((size_t)n, sizeof(STRPTR *))) == NULL))
        return NULL;
    /* 9 fixed tags, 4 per field, 2 per note line, the filler, the end */
    if ((tags = malloc(((size_t)n * 4 + 16) * sizeof(struct TagItem))) == NULL) return NULL;
    put_tag(tags, &t, LAYOUT_Orientation, (ULONG)LAYOUT_ORIENT_VERT);
    put_tag(tags, &t, LAYOUT_BevelStyle, (ULONG)BVS_GROUP);
    put_tag(tags, &t, LAYOUT_Label, (ULONG)p->frame);
    put_tag(tags, &t, LAYOUT_SpaceOuter, (ULONG)TRUE);
    put_tag(tags, &t, LAYOUT_LeftSpacing, (ULONG)FRAME_SIDE);
    put_tag(tags, &t, LAYOUT_RightSpacing, (ULONG)FRAME_SIDE);
    put_tag(tags, &t, LAYOUT_TopSpacing, (ULONG)FRAME_TOPBOT);
    put_tag(tags, &t, LAYOUT_BottomSpacing, (ULONG)FRAME_TOPBOT);
    put_tag(tags, &t, LAYOUT_InnerSpacing, (ULONG)ROW_SPACING);
    for (k = 0; k < n; ++k) {
        const SproutPlaceholder *ph = &p->t.ph[k];
        const char *text = ph->label != NULL ? ph->label : ph->key;
        ULONG id = (ULONG)(NP_FIELD + i * FIELDS_PER + k);
        int c;
        len = strlen(text) + 4;
        if ((p->labels[k] = malloc(len)) == NULL) break;
        /* labels end with a space; required ones are marked with a '*' */
        snprintf(p->labels[k], len, ph->required ? "%s* " : "%s ", text);
        required |= ph->required;
        if (ph->nchoices > 0) {
            if ((p->choices[k] = calloc((size_t)ph->nchoices + 1, sizeof(STRPTR))) == NULL) break;
            for (c = 0; c < ph->nchoices; ++c) p->choices[k][c] = (STRPTR)ph->choices[c];
            p->fields[k] = NewObject(CHOOSER_GetClass(), NULL,
                GA_ID, id, GA_RelVerify, TRUE, CHOOSER_PopUp, TRUE,
                CHOOSER_LabelArray, (ULONG)p->choices[k], CHOOSER_Selected, 0, TAG_END);
        } else p->fields[k] = string(id, "", 256);
        if (p->fields[k] == NULL) break;
        put_tag(tags, &t, LAYOUT_AddChild, (ULONG)(p->fields[k]));
        put_tag(tags, &t, CHILD_WeightedHeight, (ULONG)0);
        put_tag(tags, &t, CHILD_Label, (ULONG)(label(p->labels[k], 0)));
    }
    if (k < n) {                    /* out of memory: the gadgets made so far */
        while (--k >= 0) if (p->fields[k] != NULL) DisposeObject(p->fields[k]);
        free(tags);
        return NULL;
    }
    if (n == 0) {
        put_tag(tags, &t, LAYOUT_AddImage, (ULONG)(label(S(MSG_NP_NOVALUES), 1)));
        put_tag(tags, &t, CHILD_WeightedHeight, (ULONG)0);
    }
    if (required) {
        put_tag(tags, &t, LAYOUT_AddImage, (ULONG)(label(S(MSG_NP_REQUIREDNOTE), 1)));
        put_tag(tags, &t, CHILD_WeightedHeight, (ULONG)0);
    }
    put_tag(tags, &t, LAYOUT_AddChild, (ULONG)(filler()));
    put_tag(tags, &t, TAG_END, (ULONG)0);
    group = NewObjectA(LAYOUT_GetClass(), NULL, tags);
    free(tags);
    return group;
}

/**
 * @brief Build the help bubbles of the fields: the placeholders'
 *        descriptions.
 */
static void build_hints(void)
{
    int i, k, count = 0;
    for (i = 0; i < np.count; ++i)
        for (k = 0; k < np.tmpl[i].t.nph; ++k)
            if (np.tmpl[i].t.ph[k].description != NULL) ++count;
    if ((np.hints = calloc((size_t)count + 1, sizeof(struct HintInfo))) == NULL) return;
    count = 0;
    for (i = 0; i < np.count; ++i)
        for (k = 0; k < np.tmpl[i].t.nph; ++k)
            if (np.tmpl[i].t.ph[k].description != NULL) {
                np.hints[count].hi_GadgetID = (WORD)(NP_FIELD + i * FIELDS_PER + k);
                np.hints[count].hi_Code = -1;
                np.hints[count].hi_Text = (STRPTR)np.tmpl[i].t.ph[k].description;
                ++count;
            }
    np.hints[count].hi_GadgetID = -1;
}

/**
 * @brief Show a page: 0 (the template) or a template's form (its
 *        defaults filled in first).
 * @param page The page.
 */
static void show_page(int page)
{
    if (page > 0 && !fill_form(&np.tmpl[page - 1])) return;
    np.page = page;
    SetGadgetAttrs((struct Gadget *)np.page_gad, np.win, NULL, PAGE_Current, (ULONG)page, TAG_END);
    RethinkLayout((struct Gadget *)np.layout, np.win, NULL, TRUE);
    SetGadgetAttrs((struct Gadget *)np.back, np.win, NULL, GA_Disabled, (ULONG)(page == 0), TAG_END);
    SetGadgetAttrs((struct Gadget *)np.next, np.win, NULL, GA_Disabled, (ULONG)(page != 0 || np.cur < 0),
                   TAG_END);
    if (page > 0 && np.tmpl[page - 1].t.nph > 0)
        ActivateLayoutGadget((struct Gadget *)np.layout, np.win, NULL, (ULONG)np.tmpl[page - 1].fields[0]);
}

/* ---- the window ------------------------------------------------------ */

/**
 * @brief Width of the category list: its longest entry plus margins.
 * @return The width in pixels.
 */
static ULONG cat_width(void)
{
    ULONG w = (ULONG)TextLength(&np.app->screen->RastPort, (STRPTR)S(MSG_NP_COL_CATEGORY),
                                (UWORD)strlen(S(MSG_NP_COL_CATEGORY))), t;
    int c;
    for (c = 0; c < np.ncats; ++c) {
        t = (ULONG)TextLength(&np.app->screen->RastPort, (STRPTR)np.cats[c], (UWORD)strlen(np.cats[c]));
        if (t > w) w = t;
    }
    return w + 24;
}

/**
 * @brief Build the wizard's contents.
 * @param drawer The project drawer to suggest.
 * @return The root layout, or NULL.
 */
static Object *build(const char *drawer)
{
    Object *page0, *form;
    struct TagItem *tags;
    int c, i;

    if ((np.icon = tmplsym_image_new(BIG_SCALE, np.iconw, np.iconh)) == NULL) return NULL;
    NewList(&np.catnodes);
    for (c = 0; c < np.ncats; ++c) {
        struct Node *n = AllocListBrowserNode(1, LBNCA_Text, (ULONG)np.cats[c], TAG_END);
        if (n != NULL) AddTail(&np.catnodes, n);
    }
    np.catcols[0].ci_Width = 100;
    np.catcols[0].ci_Title = (STRPTR)S(MSG_NP_COL_CATEGORY);
    np.catcols[1].ci_Width = -1;
    np.catcols[1].ci_Title = (STRPTR)~0;
    np.catcols[1].ci_Flags = (ULONG)-1;
    np.tmplcols[0] = np.catcols[0];
    np.tmplcols[0].ci_Title = (STRPTR)S(MSG_NP_COL_TEMPLATE);
    np.tmplcols[1] = np.catcols[1];
    np.catlist_gad = NewObject(LISTBROWSER_GetClass(), NULL,
        GA_ID, NP_CATS, GA_RelVerify, TRUE,
        LISTBROWSER_ColumnInfo, (ULONG)np.catcols,
        LISTBROWSER_ColumnTitles, TRUE,
        LISTBROWSER_Labels, (ULONG)&np.catnodes,
        LISTBROWSER_ShowSelected, TRUE,
        LISTBROWSER_Selected, 0,
        TAG_END);
    show_category(0);
    np.tmpllist_gad = NewObject(LISTBROWSER_GetClass(), NULL,
        GA_ID, NP_TMPLS, GA_RelVerify, TRUE,
        LISTBROWSER_ColumnInfo, (ULONG)np.tmplcols,
        LISTBROWSER_ColumnTitles, TRUE,
        LISTBROWSER_Labels, (ULONG)&np.tmplnodes,
        LISTBROWSER_ShowSelected, TRUE,
        LISTBROWSER_Selected, (ULONG)(np.cur >= 0 ? 0 : -1),
        TAG_END);
    if (np.cur >= 0)
        SetAttrs(np.icon, TMPLSYM_Type, (ULONG)tmplsym_type(np.tmpl[np.cur].t.icon_type),
                 TMPLSYM_Icon, (ULONG)np.tmpl[np.cur].icon, TAG_END);
    np.title = NewObject(BUTTON_GetClass(), NULL,
        GA_ID, 0, GA_ReadOnly, TRUE, GA_Underscore, 0,
        GA_Text, (ULONG)(np.cur >= 0 ? np.tmpl[np.cur].t.name : ""),
        BUTTON_BevelStyle, BVS_NONE, BUTTON_Transparent, TRUE,
        BUTTON_Justification, BCJ_LEFT, TAG_END);
    make_desc(np.cur >= 0 ? np.tmpl[np.cur].t.description : NULL);
    np.desc_gad = NewObject(LISTBROWSER_GetClass(), NULL,
        GA_ID, NP_DESC, GA_ReadOnly, TRUE,
        LISTBROWSER_Labels, (ULONG)&np.descnodes,
        LISTBROWSER_ShowSelected, FALSE,
        LISTBROWSER_Selected, (ULONG)-1,
        TAG_END);
    if (!np.files) {
        np.name = string(NP_NAME, "", 80);
        np.subdir = string(NP_SUBDIR, "", 108);
        /* a path may hold a '_'; before GA_Text, see the style guide */
        np.result = NewObject(BUTTON_GetClass(), NULL, GA_ReadOnly, TRUE, GA_Underscore, 0,
            GA_Text, (ULONG)"", BUTTON_BevelStyle, BVS_NONE, BUTTON_Transparent, TRUE,
            BUTTON_Justification, BCJ_LEFT, TAG_END);
    }
    np.drawer = string(NP_DRAWER, drawer, EDITOR_PATH_MAX);

    page0 = NewObject(LAYOUT_GetClass(), NULL,
        LAYOUT_Orientation, LAYOUT_ORIENT_VERT,
        LAYOUT_InnerSpacing, AREA_SPACING,
        LAYOUT_AddChild, (ULONG)NewObject(LAYOUT_GetClass(), NULL,
            LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ,
            LAYOUT_InnerSpacing, AREA_SPACING,
            LAYOUT_AddChild, (ULONG)np.catlist_gad,
            CHILD_WeightedWidth, 25,
            CHILD_MinWidth, cat_width(),
            CHILD_MinHeight, lines(10),
            LAYOUT_AddChild, (ULONG)np.tmpllist_gad,
            CHILD_WeightedWidth, 35,
            CHILD_MinWidth, chars(24),
            /* the chosen template */
            LAYOUT_AddChild, (ULONG)NewObject(LAYOUT_GetClass(), NULL,
                LAYOUT_Orientation, LAYOUT_ORIENT_VERT,
                LAYOUT_BevelStyle, BVS_GROUP,
                LAYOUT_SpaceOuter, TRUE,
                LAYOUT_LeftSpacing, FRAME_SIDE, LAYOUT_RightSpacing, FRAME_SIDE,
                LAYOUT_TopSpacing, FRAME_TOPBOT, LAYOUT_BottomSpacing, FRAME_TOPBOT,
                LAYOUT_InnerSpacing, ROW_SPACING,
                LAYOUT_AddChild, (ULONG)NewObject(LAYOUT_GetClass(), NULL,
                    LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ,
                    LAYOUT_InnerSpacing, AREA_SPACING,
                    LAYOUT_VertAlignment, LALIGN_CENTER,
                    LAYOUT_AddImage, (ULONG)np.icon,
                    CHILD_WeightedWidth, 0,
                    CHILD_MinWidth, (ULONG)((struct Image *)np.icon)->Width,
                    CHILD_MinHeight, (ULONG)((struct Image *)np.icon)->Height,
                    CHILD_MaxWidth, (ULONG)((struct Image *)np.icon)->Width,
                    CHILD_MaxHeight, (ULONG)((struct Image *)np.icon)->Height,
                    LAYOUT_AddChild, (ULONG)np.title,
                    TAG_END),
                FIXED,
                LAYOUT_AddChild, (ULONG)np.desc_gad,
                CHILD_MinWidth, chars(DESC_CHARS) + 24,
                TAG_END),
            CHILD_WeightedWidth, 40,
            TAG_END),
        LAYOUT_AddChild, (ULONG)NewObject(LAYOUT_GetClass(), NULL,
            LAYOUT_Orientation, LAYOUT_ORIENT_VERT,
            LAYOUT_InnerSpacing, ROW_SPACING,
            /* file templates: no name, the files belong to the open project */
            np.files ? TAG_IGNORE : LAYOUT_AddChild, (ULONG)np.name,
            np.files ? TAG_IGNORE : CHILD_WeightedHeight, 0,
            np.files ? TAG_IGNORE : CHILD_Label, np.files ? 0 : (ULONG)label(S(MSG_NP_NAME), 1),
            np.files ? TAG_IGNORE : LAYOUT_AddChild, (ULONG)np.subdir,
            np.files ? TAG_IGNORE : CHILD_WeightedHeight, 0,
            np.files ? TAG_IGNORE : CHILD_Label, np.files ? 0 : (ULONG)label(S(MSG_NP_SUBDIR), 1),
            LAYOUT_AddChild, (ULONG)NewObject(LAYOUT_GetClass(), NULL,
                LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ,
                LAYOUT_AddChild, (ULONG)np.drawer,
                LAYOUT_AddChild, (ULONG)button(NP_CHOOSE, S(MSG_NP_CHOOSE)),
                CHILD_WeightedWidth, 0,
                TAG_END),
            FIXED,
            CHILD_Label, (ULONG)label(S(np.files ? MSG_NP_DRAWER : MSG_NP_PARENT), 1),
            /* projects: the drawer that will be created; files: a note */
            np.files ? LAYOUT_AddImage : LAYOUT_AddChild,
            np.files ? (ULONG)label(S(MSG_NF_DRAWERNOTE), 1) : (ULONG)np.result,
            FIXED,
            TAG_END),
        FIXED,
        TAG_END);
    if (page0 == NULL) return NULL;
    /* the pages with one tag list, too */
    if ((tags = malloc(((size_t)np.count + 3) * sizeof(struct TagItem))) == NULL) {
        DisposeObject(page0);
        return NULL;
    }
    tags[0].ti_Tag = PAGE_Add;
    tags[0].ti_Data = (ULONG)page0;
    for (i = 0; i < np.count; ++i) {
        if ((form = build_form(i)) == NULL) {
            while (--i >= 0) DisposeObject((Object *)tags[i + 1].ti_Data);
            DisposeObject(page0);
            free(tags);
            return NULL;
        }
        tags[i + 1].ti_Tag = PAGE_Add;
        tags[i + 1].ti_Data = (ULONG)form;
    }
    tags[np.count + 1].ti_Tag = PAGE_Current;
    tags[np.count + 1].ti_Data = 0;
    tags[np.count + 2].ti_Tag = TAG_END;
    np.page_gad = NewObjectA(PAGE_GetClass(), NULL, tags);
    free(tags);
    if (np.page_gad == NULL) return NULL;
    build_hints();

    np.back = button(NP_BACK, S(MSG_NP_BACK));
    np.next = button(NP_NEXT, S(MSG_NP_NEXT));
    np.create = button(NP_CREATE, S(MSG_NP_CREATE));
    SetAttrs(np.back, GA_Disabled, TRUE, TAG_END);
    return NewObject(LAYOUT_GetClass(), NULL,
        LAYOUT_Orientation, LAYOUT_ORIENT_VERT,
        LAYOUT_SpaceOuter, TRUE,
        LAYOUT_LeftSpacing, AREA_SPACING,
        LAYOUT_RightSpacing, AREA_SPACING,
        LAYOUT_TopSpacing, AREA_SPACING,
        LAYOUT_BottomSpacing, AREA_SPACING,
        LAYOUT_InnerSpacing, AREA_SPACING,
        LAYOUT_DeferLayout, TRUE,
        LAYOUT_AddChild, (ULONG)np.page_gad,
        LAYOUT_AddChild, (ULONG)NewObject(LAYOUT_GetClass(), NULL,
            LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ,
            LAYOUT_InnerSpacing, ROW_SPACING,
            LAYOUT_AddChild, (ULONG)np.back,
            CHILD_WeightedWidth, 0,
            LAYOUT_AddChild, (ULONG)np.next,
            CHILD_WeightedWidth, 0,
            LAYOUT_AddChild, (ULONG)np.create,
            CHILD_WeightedWidth, 0,
            LAYOUT_AddChild, (ULONG)filler(),
            LAYOUT_AddChild, (ULONG)button(NP_CANCEL, S(MSG_NP_CANCEL)),
            CHILD_WeightedWidth, 0,
            TAG_END),
        FIXED,
        TAG_END);
}

/* ---- creating -------------------------------------------------------- */

/**
 * @brief Write the values of the chosen template's form to a file, as
 *        Sprout's VALUES file: PROJECT (if a name was given), every
 *        placeholder, and AUTHOR and AUTHORMAIL from the settings.
 * @param v The form's values.
 * @param path The file.
 * @return Non-zero on success.
 */
static int write_values(const SproutValues *v, const char *path)
{
    const NpTemplate *p = &np.tmpl[np.cur];
    const char *name = project_name();
    JsonValue *obj = json_new_object(), *s;
    JsonWriter w;
    int k, ok = obj != NULL;

    if (ok && name != NULL && name[0] != '\0')
        ok = (s = json_new_string(name)) != NULL && json_obj_set(obj, "PROJECT", s);
    for (k = 0; ok && k < p->t.nph; ++k)
        ok = (s = json_new_string(sprout_values_get(v, p->t.ph[k].key))) != NULL &&
             json_obj_set(obj, p->t.ph[k].key, s);
    /* the standard keys from the settings, if the template does not ask for them */
    for (k = 0; ok && k < 2; ++k) {
        static const char *const keys[2] = { "AUTHOR", "AUTHORMAIL" };
        const char *val = sprout_values_get(v, keys[k]);
        if (json_obj_get(obj, keys[k]) == NULL && val != NULL && val[0] != '\0')
            ok = (s = json_new_string(val)) != NULL && json_obj_set(obj, keys[k], s);
    }
    if (ok) {
        json_writer_init_alloc(&w, 256);
        json_write_value(&w, obj, 0);
        ok = !w.error && datafile_write(path, json_writer_get(&w));
        json_writer_free(&w);
    }
    json_value_free(obj);
    return ok;
}

/**
 * @brief Check the chosen template's values and create the project with
 *        Sprout.
 * @param[out] v The values used (initialised here; sprout_values_free()
 *               it), for opening the files afterwards.
 * @return Non-zero if the project was created (warnings were shown);
 *         zero if not (the reason was shown).
 */
static int create(SproutValues *v)
{
    NpTemplate *p = &np.tmpl[np.cur];
    char drawer[EDITOR_PATH_MAX], values[EDITOR_PATH_MAX], out[EDITOR_PATH_MAX];
    const char *sub = text_of(np.subdir);
    Object *wrong = NULL;
    LONG problem = 0;
    BPTR lock;
    char args[EDITOR_PATH_MAX * 3 + 16], qid[EDITOR_PATH_MAX], qdrawer[EDITOR_PATH_MAX], qvalues[EDITOR_PATH_MAX];
    char *msgs;
    LONG rc;
    int k;

    sprout_values_init(v);
    /* page 0 first: where, and for a project a new drawer's name */
    if (text_of(np.drawer)[0] == '\0') {
        problem = np.files ? MSG_NF_NODRAWER : MSG_NP_NODRAWER;
        wrong = np.drawer;
    } else if (!np.files && sub[0] == '\0') {
        problem = MSG_NP_NOSUBDIR;
        wrong = np.subdir;
    } else if (!np.files && (strchr(sub, ':') != NULL || strchr(sub, '/') != NULL)) {
        problem = MSG_NP_BADSUBDIR;
        wrong = np.subdir;
    } else if (!target_drawer(drawer, sizeof(drawer))) {
        problem = MSG_ERR_PATHTOOLONG;
        wrong = np.files ? np.drawer : np.subdir;
    }
    if (problem == 0 && !np.files && (lock = Lock((STRPTR)drawer, ACCESS_READ)) != 0) {
        UnLock(lock);
        message_arg(MSG_NP_EXISTS, drawer);
        wrong = np.subdir;
    } else if (problem != 0) {
        message(S(problem));
    }
    if (wrong != NULL) {
        if (np.page != 0) show_page(0);
        ActivateLayoutGadget((struct Gadget *)np.layout, np.win, NULL, (ULONG)wrong);
        return 0;
    }
    if (form_values(p, v, 1) < 0) {
        message(S(MSG_PJ_NOMEM));
        return 0;
    }
    for (k = 0; k < p->t.nph; ++k)
        if (p->t.ph[k].required && sprout_values_get(v, p->t.ph[k].key)[0] == '\0') {
            message_arg(MSG_NP_REQUIRED, p->t.ph[k].label != NULL ? p->t.ph[k].label : p->t.ph[k].key);
            if (np.page != np.cur + 1) show_page(np.cur + 1);
            ActivateLayoutGadget((struct Gadget *)np.layout, np.win, NULL, (ULONG)p->fields[k]);
            return 0;
        }
    if (!temp_name(np.app, ".json", values, sizeof(values)) || !temp_name(np.app, ".txt", out, sizeof(out)) ||
        !write_values(v, values)) {
        message(S(MSG_NP_NOVALUESFILE));
        return 0;
    }
    if (!quote(qid, sizeof(qid), p->id) || !quote(qdrawer, sizeof(qdrawer), drawer) ||
        !quote(qvalues, sizeof(qvalues), values)) {
        DeleteFile((STRPTR)values);
        message(S(MSG_ERR_PATHTOOLONG));
        return 0;
    }
    /* QUIET: no list of the files made, only warnings and errors */
    snprintf(args, sizeof(args), "%s %s VALUES %s QUIET", qid, qdrawer, qvalues);
    SetAttrs(np.winobj, WA_BusyPointer, TRUE, TAG_END);
    rc = run_sprout(args, out, out);
    SetAttrs(np.winobj, WA_BusyPointer, FALSE, TAG_END);
    msgs = read_messages(out);
    DeleteFile((STRPTR)values);
    DeleteFile((STRPTR)out);
    if (rc < 0) message(S(MSG_NP_NOSPROUT));
    else if (rc >= 10) message_arg(np.files ? MSG_NF_FAILED : MSG_NP_FAILED, msgs != NULL ? msgs : "");
    else if (rc >= 5 && msgs != NULL) message_arg(np.files ? MSG_NF_WARNINGS : MSG_NP_WARNINGS, msgs);
    free(msgs);
    return rc >= 0 && rc < 10;
}

/**
 * @brief Open the new project (not for file templates) and the template's
 *        files to open.
 * @param p The template.
 * @param drawer The project drawer (or the drawer of the files) as given.
 * @param v The values Sprout used.
 */
static void open_project(const NpTemplate *p, const char *drawer, const SproutValues *v)
{
    char full[EDITOR_PATH_MAX], path[EDITOR_PATH_MAX], text[EDITOR_PATH_MAX + 64];
    BPTR lock;
    int k, bad;

    if ((lock = Lock((STRPTR)drawer, ACCESS_READ)) == 0) return;
    k = NameFromLock(lock, (STRPTR)full, sizeof(full)) != 0;
    UnLock(lock);
    if (!k || (!np.files && !tree_open_project(np.app, full))) return;
    for (k = 0; k < p->t.nopen; ++k) {
        char *rel = sprout_expand_path(p->t.open[k], v, &bad);
        if (rel == NULL) continue;
        strcpy(path, full);
        if (AddPart((STRPTR)path, (STRPTR)rel, sizeof(path))) document_open(np.app, path);
        free(rel);
    }
    snprintf(text, sizeof(text), text_for(MSG_NP_CREATED, MSG_NF_CREATED),
             np.files ? p->t.name : sprout_values_get(v, "PROJECT"));
    ui_status_message(np.app, text);
}

/**
 * @brief The wizard for project or file templates.
 * @param app The application state.
 * @param files Non-zero for file templates.
 * @param into The drawer to suggest for file templates, NULL to choose one.
 */
static void wizard(EditorApp *app, int files, const char *into)
{
    char drawer[EDITOR_PATH_MAX], root[EDITOR_PATH_MAX];
    struct DateStamp ds;
    SproutValues values;
    ULONG sig = 0, mainsig = 0, result;
    WORD code = 0;
    int done = 0, created = 0, c;

    if (app->window == NULL) return;
    memset(&np, 0, sizeof(np));
    np.app = app;
    np.cur = -1;
    np.files = files;
    if (files) ui_project_name(app, np.project, sizeof(np.project));
    sprout_values_init(&values);
    DateStamp(&ds);
    np.days = ds.ds_Days;
    if (ListBrowserBase == NULL) ListBrowserBase = OpenLibrary((STRPTR)"gadgets/listbrowser.gadget", 44);
    if (StringBase == NULL) StringBase = OpenLibrary((STRPTR)"gadgets/string.gadget", 44);
    if (ListBrowserBase == NULL || StringBase == NULL || ChooserBase == NULL) {
        char text[160];
        snprintf(text, sizeof(text), S(MSG_NP_NEEDCLASS),
                 ListBrowserBase == NULL ? "gadgets/listbrowser.gadget" :
                 StringBase == NULL ? "gadgets/string.gadget" : "gadgets/chooser.gadget");
        ui_error(app, text_for(MSG_NP_TITLE, MSG_NF_TITLE), text);
        return;
    }
    SetAttrs(app->window_object, WA_BusyPointer, TRUE, TAG_END);
    c = load_templates();
    SetAttrs(app->window_object, WA_BusyPointer, FALSE, TAG_END);
    if (!c) goto out;

    /* suggested drawer: for files the one asked for, else the one selected
     * in the tree, else the active document's, else the project's; where a
     * project is created: the setting PROJECTDIR, else
     * the drawer holding the current project */
    drawer[0] = '\0';
    if (!files && app->settings.projectdir[0] != '\0') {
        size_t n;
        strncpy(drawer, app->settings.projectdir, sizeof(drawer) - 1);
        drawer[sizeof(drawer) - 1] = '\0';
        n = strlen(drawer);
        if (n > 1 && drawer[n - 1] == '/') drawer[n - 1] = '\0';
    } else if (files) {
        if (into != NULL) {
            strncpy(drawer, into, sizeof(drawer) - 1);
            drawer[sizeof(drawer) - 1] = '\0';
        } else if (treeops_selected_drawer(app, drawer, sizeof(drawer))) {
            /* drawer is set */
        } else if (app->active != NULL && app->active->path != NULL) {
            strncpy(drawer, app->active->path, sizeof(drawer) - 1);
            drawer[sizeof(drawer) - 1] = '\0';
            *PathPart((STRPTR)drawer) = '\0';
        } else if (!tree_project_root(app, drawer, sizeof(drawer))) drawer[0] = '\0';
    } else if (tree_project_root(app, root, sizeof(root))) {
        *PathPart((STRPTR)root) = '\0';
        strcpy(drawer, root);
    }
    np.render_hook.h_Entry = (ULONG (*)())HookEntry;
    np.render_hook.h_SubEntry = (ULONG (*)())render_row;
    c = app->screen->RastPort.TxHeight;
    np.row_height = (WORD)((c > TMPLSYM_SIZE ? c : TMPLSYM_SIZE) + 2 * ROW_PADDING);
    np.dri = GetScreenDrawInfo(app->screen);
    if ((np.layout = build(drawer)) == NULL) {
        message(S(MSG_PJ_NOMEM));
        goto out;
    }
    np.winobj = NewObject(WINDOW_GetClass(), NULL,
        WA_Title, (ULONG)text_for(MSG_NP_TITLE, MSG_NF_TITLE),
        WA_PubScreen, (ULONG)app->screen,
        WA_SmartRefresh, TRUE,     /* scrolled lists: see "Scrolling..." in TECHNICAL.md */
        WA_Activate, TRUE,
        WA_DepthGadget, TRUE,
        WA_DragBar, TRUE,
        WA_CloseGadget, TRUE,
        WA_SizeGadget, TRUE,
        WA_IDCMP, IDCMP_INTUITICKS,     /* follow_fields() while one types */
        WINDOW_RefWindow, (ULONG)app->window,
        WINDOW_Position, WPOS_CENTERWINDOW,
        WINDOW_ParentGroup, (ULONG)np.layout,
        np.hints != NULL ? WINDOW_HintInfo : TAG_IGNORE, (ULONG)np.hints,
        WINDOW_GadgetHelp, TRUE,
        TAG_END);
    if (np.winobj == NULL) {
        DisposeObject(np.layout);
        goto out;
    }
    if ((np.win = (struct Window *)DoMethod(np.winobj, WM_OPEN, NULL)) == NULL) goto out;
    show_details();
    follow_fields();
    ActivateLayoutGadget((struct Gadget *)np.layout, np.win, NULL, (ULONG)(np.files ? np.drawer : np.name));

    SetAttrs(app->window_object, WA_BusyPointer, TRUE, TAG_END);
    GetAttr(WINDOW_SigMask, app->window_object, &mainsig);
    GetAttr(WINDOW_SigMask, np.winobj, &sig);
    while (!done) {
        ULONG got = Wait(sig | mainsig);
        if ((got & mainsig) != 0) drop_input(app->window_object);
        while ((result = DoMethod(np.winobj, WM_HANDLEINPUT, &code)) != WMHI_LASTMSG) {
            ULONG id = result & WMHI_GADGETMASK;
            if ((result & WMHI_CLASSMASK) == WMHI_CLOSEWINDOW) { done = 1; continue; }
            if ((result & WMHI_CLASSMASK) == WMHI_INTUITICK) { follow_fields(); continue; }
            if ((result & WMHI_CLASSMASK) != WMHI_GADGETUP) continue;
            switch (id) {
                case NP_CATS:
                    c = (int)(LONG)get(np.catlist_gad, LISTBROWSER_Selected);
                    if (c >= 0 && c < np.ncats && c != np.cat) show_category(c);
                    break;
                case NP_TMPLS:
                    template_chosen();
                    if ((get(np.tmpllist_gad, LISTBROWSER_RelEvent) & LBRE_DOUBLECLICK) != 0 && np.cur >= 0)
                        show_page(np.cur + 1);
                    break;
                case NP_CHOOSE:
                    choose_drawer();
                    break;
                case NP_NEXT:
                    if (np.cur >= 0) show_page(np.cur + 1);
                    break;
                case NP_BACK:
                    show_page(0);
                    break;
                case NP_CREATE:
                    if (np.cur >= 0 && create(&values)) done = created = 1;
                    else sprout_values_free(&values);
                    break;
                case NP_CANCEL:
                    done = 1;
                    break;
            }
        }
    }
    SetAttrs(app->window_object, WA_BusyPointer, FALSE, TAG_END);
    if (created) {
        /* the drawer's text goes with the window */
        if (!target_drawer(drawer, sizeof(drawer))) drawer[0] = '\0';
    }

out:
    if (np.winobj != NULL) DisposeObject(np.winobj);         /* the gadgets with it */
    np.win = NULL;
    if (np.catnodes.lh_Head != NULL) FreeListBrowserList(&np.catnodes);
    if (np.tmplnodes.lh_Head != NULL) FreeListBrowserList(&np.tmplnodes);
    if (np.descnodes.lh_Head != NULL) FreeListBrowserList(&np.descnodes);
    if (np.dri != NULL) FreeScreenDrawInfo(app->screen, np.dri);
    free(np.hints);
    if (created) open_project(&np.tmpl[np.cur], drawer, &values);
    sprout_values_free(&values);
    free_templates();
    memset(&np, 0, sizeof(np));
}

void new_project(EditorApp *app)
{
    wizard(app, 0, NULL);
}

void new_from_template(EditorApp *app)
{
    wizard(app, 1, NULL);
}

void new_from_template_in(EditorApp *app, const char *drawer)
{
    wizard(app, 1, drawer);
}
