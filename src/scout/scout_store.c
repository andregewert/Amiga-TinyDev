/**
 * @file scout_store.c
 * @brief The portable core of Scout: parsers, languages, sessions, the
 *        results of their files, and the answers to the queries.
 *
 * A file's result is kept as structures, except for the highlighting
 * tokens: they are by far the largest part and only ever passed on, so
 * they stay the text of the JSON array. They are cut out of the parser's
 * answer before the rest is parsed, which keeps tens of thousands of
 * numbers out of the JSON tree.
 */
#include "scout_store.h"
#include "scout_protocol.h"

#include <stdlib.h>
#include <string.h>

/** Most locations DEFINITION lists. */
#define MAX_LOCATIONS 32

/** Most candidates COMPLETE collects before sorting. */
#define MAX_CANDIDATES 2000

/**
 * @brief Copy a string with malloc().
 * @param s The string (may be NULL).
 * @return The copy, or NULL (also for NULL).
 */
static char *dup_str(const char *s)
{
    size_t n;
    char *copy;
    if (!s) return NULL;
    n = strlen(s) + 1;
    copy = (char *)malloc(n);
    if (copy) memcpy(copy, s, n);
    return copy;
}

/**
 * @brief Fold a Latin-1 character to lower case.
 * @param c The character.
 * @return Its lower case form.
 */
static unsigned char fold(unsigned char c)
{
    if ((c >= 'A' && c <= 'Z') || (c >= 0xC0 && c <= 0xDE && c != 0xD7)) return (unsigned char)(c + 32);
    return c;
}

int scout_path_equal(const char *a, const char *b)
{
    while (*a && fold((unsigned char)*a) == fold((unsigned char)*b)) {
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

int scout_path_inside(const char *drawer, const char *path)
{
    size_t n = strlen(drawer), i;
    if (n == 0) return 0;
    for (i = 0; i < n; i++) {
        if (fold((unsigned char)drawer[i]) != fold((unsigned char)path[i])) return 0;
    }
    return drawer[n - 1] == ':' || drawer[n - 1] == '/' || path[n] == '/';
}

/**
 * @brief Tell whether a name starts with a prefix, without regard to case.
 * @param name The name.
 * @param prefix The prefix.
 * @return Non-zero if it does.
 */
static int has_prefix(const char *name, const char *prefix)
{
    while (*prefix) {
        if (fold((unsigned char)*name) != fold((unsigned char)*prefix)) return 0;
        name++;
        prefix++;
    }
    return 1;
}

/**
 * @brief Compare two names for sorting, without regard to case first.
 * @param a A name.
 * @param b Another name.
 * @return Less than, equal to or greater than 0.
 */
static int name_order(const char *a, const char *b)
{
    const char *p = a, *q = b;
    while (*p && fold((unsigned char)*p) == fold((unsigned char)*q)) {
        p++;
        q++;
    }
    if (*p || *q) return (int)fold((unsigned char)*p) - (int)fold((unsigned char)*q);
    return strcmp(a, b);
}

/**
 * @brief Free an array of strings.
 * @param list The array (may be NULL).
 * @param count Number of strings.
 */
static void free_strings(char **list, long count)
{
    long i;
    for (i = 0; i < count; i++) free(list[i]);
    free(list);
}

/**
 * @brief Append a string to an array of strings.
 * @param list The array (pointer to it).
 * @param count Number of strings, counted up.
 * @param s The string (copied).
 * @return 1 on success, 0 if out of memory.
 */
static int add_string(char ***list, long *count, const char *s)
{
    char **bigger = (char **)realloc(*list, (size_t)(*count + 1) * sizeof(char *));
    char *copy;
    if (!bigger) return 0;
    *list = bigger;
    copy = dup_str(s);
    if (!copy) return 0;
    bigger[(*count)++] = copy;
    return 1;
}

/* ---- Parsers and languages ---- */

int scout_store_init(ScoutStore *st)
{
    memset(st, 0, sizeof(*st));
    st->sessions = (ScoutSession *)calloc(1, sizeof(ScoutSession));
    if (!st->sessions) return 0;
    st->next_session = 1;
    return 1;
}

/**
 * @brief Free a language's contents.
 * @param l The language.
 */
static void free_language(ScoutLanguage *l)
{
    free(l->id);
    free(l->name);
    free_strings(l->extensions, l->extension_count);
    free(l->syntax);
    free_strings(l->keywords, l->keyword_count);
}

void scout_store_clear_parsers(ScoutStore *st)
{
    long i;
    for (i = 0; i < st->parser_count; i++) {
        free(st->parsers[i].path);
        free(st->parsers[i].name);
        free(st->parsers[i].version);
    }
    free(st->parsers);
    for (i = 0; i < st->language_count; i++) free_language(&st->languages[i]);
    free(st->languages);
    free_strings(st->problems, st->problem_count);
    st->parsers = NULL;
    st->languages = NULL;
    st->problems = NULL;
    st->parser_count = st->language_count = st->problem_count = 0;
}

/**
 * @brief Free a file's result (not the file).
 * @param f The file.
 */
static void free_result(ScoutFile *f)
{
    long i;
    for (i = 0; i < f->symbol_count; i++) {
        free(f->symbols[i].name);
        free(f->symbols[i].detail);
    }
    for (i = 0; i < f->diagnostic_count; i++) {
        free(f->diagnostics[i].message);
        free(f->diagnostics[i].code);
    }
    for (i = 0; i < f->reference_count; i++) {
        free(f->references[i].kind);
        free(f->references[i].target);
    }
    free(f->symbols);
    free(f->diagnostics);
    free(f->references);
    free(f->tokens);
    free(f->language);
    free(f->snapshot);
    f->snapshot = NULL;
    f->symbols = NULL;
    f->diagnostics = NULL;
    f->references = NULL;
    f->tokens = NULL;
    f->language = NULL;
    f->symbol_count = f->diagnostic_count = f->reference_count = 0;
    f->valid = 0;
}

/**
 * @brief Free a session's files, include paths and error.
 * @param s The session.
 */
static void empty_session(ScoutSession *s)
{
    ScoutFile *f = s->files;
    while (f) {
        ScoutFile *next = f->next;
        free_result(f);
        free(f->path);
        free(f);
        f = next;
    }
    s->files = NULL;
    s->file_count = 0;
    scout_session_clear_include(s);
    free(s->error_text);
    s->error_text = NULL;
    s->error_code = 0;
}

void scout_store_free(ScoutStore *st)
{
    ScoutSession *s = st->sessions;
    scout_store_clear_parsers(st);
    while (s) {
        ScoutSession *next = s->next;
        empty_session(s);
        free(s->project);
        free(s);
        s = next;
    }
    st->sessions = NULL;
}

void scout_store_add_problem(ScoutStore *st, const char *first, const char *second)
{
    size_t n1 = strlen(first), n2 = second ? strlen(second) : 0;
    char *text = (char *)malloc(n1 + n2 + 1);
    char **bigger;
    if (!text) return;
    memcpy(text, first, n1);
    if (n2) memcpy(text + n1, second, n2);
    text[n1 + n2] = '\0';
    bigger = (char **)realloc(st->problems, (size_t)(st->problem_count + 1) * sizeof(char *));
    if (!bigger) {
        free(text);
        return;
    }
    st->problems = bigger;
    st->problems[st->problem_count++] = text;
}

/**
 * @brief Fill the word characters of a language from the protocol's
 *        character class ("A-Za-z0-9_").
 * @param ident The table (256 entries, cleared first).
 * @param cls The class, NULL for the default.
 */
static void set_class(char *ident, const char *cls)
{
    const unsigned char *p;
    memset(ident, 0, 256);
    if (!cls) cls = "A-Za-z0-9_";
    for (p = (const unsigned char *)cls; *p; p++) {
        if (p[1] == '-' && p[2]) {
            unsigned c;
            for (c = p[0]; c <= p[2]; c++) ident[c] = 1;
            p += 2;
        } else {
            ident[*p] = 1;
        }
    }
}

/**
 * @brief Find the language that claims an extension.
 * @param st The store.
 * @param ext The extension (lower case, with its dot).
 * @param[out] index Index of the extension in the language's list.
 * @return The language's index, or -1.
 */
static long find_extension(const ScoutStore *st, const char *ext, long *index)
{
    long i, j;
    for (i = 0; i < st->language_count; i++) {
        for (j = 0; j < st->languages[i].extension_count; j++) {
            if (scout_path_equal(st->languages[i].extensions[j], ext)) {
                if (index) *index = j;
                return i;
            }
        }
    }
    return -1;
}

/**
 * @brief Make a lower case copy of an extension.
 * @param ext The extension.
 * @param[out] out The copy.
 * @param size Size of @p out.
 */
static void lower_ext(const char *ext, char *out, size_t size)
{
    size_t i;
    for (i = 0; ext[i] && i + 1 < size; i++) out[i] = (char)fold((unsigned char)ext[i]);
    out[i] = '\0';
}

/**
 * @brief Take an extension away from the language that claims it.
 * @param st The store.
 * @param ext The extension.
 */
static void drop_extension(ScoutStore *st, const char *ext)
{
    long j, i = find_extension(st, ext, &j);
    if (i < 0) return;
    free(st->languages[i].extensions[j]);
    memmove(&st->languages[i].extensions[j], &st->languages[i].extensions[j + 1],
            (size_t)(st->languages[i].extension_count - j - 1) * sizeof(char *));
    st->languages[i].extension_count--;
}

/**
 * @brief Add the language of a parser's description.
 * @param st The store.
 * @param parser Index of the parser.
 * @param l The language's description.
 * @param path The parser (for problems).
 * @return 1 on success, 0 if it was not added, -1 if out of memory.
 */
static int add_language(ScoutStore *st, long parser, const JsonValue *l, const char *path)
{
    const char *id = json_obj_get_string(l, "id", NULL);
    const JsonValue *exts = json_obj_get_array(l, "extensions");
    const JsonValue *syntax = json_obj_get_obj(l, "syntax");
    ScoutLanguage *lang, *bigger;
    size_t i;

    if (!id || !*id) return 0;
    if (scout_store_language(st, id)) {
        scout_store_add_problem(st, "language already handled by another parser: ", path);
        return 0;
    }
    bigger = (ScoutLanguage *)realloc(st->languages, (size_t)(st->language_count + 1) * sizeof(ScoutLanguage));
    if (!bigger) return -1;
    st->languages = bigger;
    lang = &st->languages[st->language_count];
    memset(lang, 0, sizeof(*lang));
    lang->parser = parser;
    lang->case_sensitive = 1;
    lang->id = dup_str(id);
    lang->name = dup_str(json_obj_get_string(l, "name", id));
    if (!lang->id || !lang->name) {
        free_language(lang);
        return -1;
    }
    for (i = 0; exts && i < json_array_length(exts); i++) {
        const JsonValue *e = json_array_get(exts, i);
        char ext[32];
        if (!e || e->type != JSON_STRING || e->u.str_val[0] != '.') continue;
        lower_ext(e->u.str_val, ext, sizeof(ext));
        if (find_extension(st, ext, NULL) >= 0) {
            scout_store_add_problem(st, "extension already handled by another parser: ", path);
            continue;
        }
        if (!add_string(&lang->extensions, &lang->extension_count, ext)) {
            free_language(lang);
            return -1;
        }
    }
    set_class(lang->ident, NULL);
    if (syntax) {
        JsonWriter w;
        const JsonValue *ident = json_obj_get_obj(syntax, "identifier");
        const JsonValue *kw = json_obj_get_obj(syntax, "keywords");
        json_writer_init_alloc(&w, 1024);
        json_write_value(&w, syntax, -1);
        lang->syntax = w.error ? NULL : dup_str(json_writer_get(&w));
        json_writer_free(&w);
        if (!lang->syntax) {
            free_language(lang);
            return -1;
        }
        lang->case_sensitive = json_obj_get_bool(syntax, "caseSensitive", 1);
        if (ident) set_class(lang->ident, json_obj_get_string(ident, "part", NULL));
        if (kw && kw->type == JSON_OBJECT) {
            const JsonMember *m;
            for (m = kw->u.obj_val.first; m; m = m->next) {
                if (!m->value || m->value->type != JSON_ARRAY) continue;
                for (i = 0; i < json_array_length(m->value); i++) {
                    const JsonValue *k = json_array_get(m->value, i);
                    if (k && k->type == JSON_STRING &&
                        !add_string(&lang->keywords, &lang->keyword_count, k->u.str_val)) {
                        free_language(lang);
                        return -1;
                    }
                }
            }
        }
    }
    st->language_count++;
    return 1;
}

int scout_store_add_parser(ScoutStore *st, const char *path, const char *describe,
                           const char *extension)
{
    JsonValue *root = json_parse(describe, NULL);
    const JsonValue *langs;
    ScoutParser *bigger, *p;
    long index, first_lang;
    size_t i;
    int rc = 1;

    if (!root || root->type != JSON_OBJECT) {
        json_value_free(root);
        scout_store_add_problem(st, "invalid description: ", path);
        return 0;
    }
    if (json_obj_get_int(root, "protocol", 0) != SCOUT_PROTOCOL_VERSION) {
        json_value_free(root);
        scout_store_add_problem(st, "unknown protocol version: ", path);
        return 0;
    }
    langs = json_obj_get_array(root, "languages");
    if (!langs || json_array_length(langs) == 0) {
        json_value_free(root);
        scout_store_add_problem(st, "no languages: ", path);
        return 0;
    }
    bigger = (ScoutParser *)realloc(st->parsers, (size_t)(st->parser_count + 1) * sizeof(ScoutParser));
    if (!bigger) {
        json_value_free(root);
        return -1;
    }
    st->parsers = bigger;
    index = st->parser_count;
    p = &st->parsers[index];
    memset(p, 0, sizeof(*p));
    p->path = dup_str(path);
    p->name = dup_str(json_obj_get_string(root, "name", path));
    p->version = dup_str(json_obj_get_string(root, "version", ""));
    if (!p->path || !p->name || !p->version) {
        free(p->path);
        free(p->name);
        free(p->version);
        json_value_free(root);
        return -1;
    }
    st->parser_count++;

    if (extension) {
        char ext[32];
        lower_ext(extension, ext, sizeof(ext));
        drop_extension(st, ext);
    }
    first_lang = st->language_count;
    for (i = 0; i < json_array_length(langs) && rc >= 0; i++) {
        if (add_language(st, index, json_array_get(langs, i), path) < 0) rc = -1;
    }
    if (rc > 0 && extension) {
        char ext[32];
        lower_ext(extension, ext, sizeof(ext));
        if (st->language_count > first_lang && find_extension(st, ext, NULL) < 0 &&
            !add_string(&st->languages[first_lang].extensions, &st->languages[first_lang].extension_count, ext)) {
            rc = -1;
        }
    }
    json_value_free(root);
    return rc;
}

const ScoutLanguage *scout_store_language(const ScoutStore *st, const char *id)
{
    long i;
    for (i = 0; i < st->language_count; i++) {
        if (strcmp(st->languages[i].id, id) == 0) return &st->languages[i];
    }
    return NULL;
}

const ScoutLanguage *scout_store_language_for(const ScoutStore *st, const char *path)
{
    const char *dot = strrchr(path, '.');
    long i;
    if (!dot || strchr(dot, '/') || strchr(dot, ':')) return NULL;
    i = find_extension(st, dot, NULL);
    return i >= 0 ? &st->languages[i] : NULL;
}

/* ---- Sessions and files ---- */

ScoutSession *scout_session_open(ScoutStore *st, const char *project)
{
    ScoutSession *s = (ScoutSession *)calloc(1, sizeof(ScoutSession)), *last;
    if (!s) return NULL;
    s->project = dup_str(project);
    if (!s->project) {
        free(s);
        return NULL;
    }
    s->id = st->next_session++;
    for (last = st->sessions; last->next; last = last->next);
    last->next = s;
    return s;
}

ScoutSession *scout_session_find(const ScoutStore *st, long id)
{
    ScoutSession *s;
    for (s = st->sessions; s; s = s->next) {
        if (s->id == id) return s;
    }
    return NULL;
}

int scout_session_close(ScoutStore *st, long id)
{
    ScoutSession *s, *prev = NULL;
    for (s = st->sessions; s; prev = s, s = s->next) {
        if (s->id != id) continue;
        empty_session(s);
        if (id == 0) return 1;
        if (prev) prev->next = s->next;
        free(s->project);
        free(s);
        return 1;
    }
    return 0;
}

ScoutSession *scout_session_idlest(const ScoutStore *st, unsigned long before)
{
    ScoutSession *s, *best = NULL;
    for (s = st->sessions; s; s = s->next) {
        if (s->id != 0 && s->last_used <= before && (!best || s->last_used < best->last_used)) best = s;
    }
    return best;
}

int scout_session_add_include(ScoutSession *s, const char *path)
{
    long i;
    for (i = 0; i < s->include_count; i++) {
        if (scout_path_equal(s->include[i], path)) return 1;
    }
    return add_string(&s->include, &s->include_count, path);
}

void scout_session_clear_include(ScoutSession *s)
{
    free_strings(s->include, s->include_count);
    s->include = NULL;
    s->include_count = 0;
}

void scout_session_set_error(ScoutSession *s, long code, const char *text)
{
    free(s->error_text);
    s->error_code = code;
    s->error_text = dup_str(text);
}

ScoutFile *scout_file_find(const ScoutSession *s, const char *path)
{
    ScoutFile *f;
    for (f = s->files; f; f = f->next) {
        if (scout_path_equal(f->path, path)) return f;
    }
    return NULL;
}

ScoutFile *scout_file_get(ScoutSession *s, const char *path)
{
    ScoutFile *f = scout_file_find(s, path), *last;
    if (f) return f;
    f = (ScoutFile *)calloc(1, sizeof(ScoutFile));
    if (!f) return NULL;
    f->path = dup_str(path);
    if (!f->path) {
        free(f);
        return NULL;
    }
    f->version = -1;
    if (!s->files) {
        s->files = f;
    } else {
        for (last = s->files; last->next; last = last->next);
        last->next = f;
    }
    s->file_count++;
    return f;
}

int scout_file_remove(ScoutSession *s, const char *path)
{
    ScoutFile **p;
    for (p = &s->files; *p; p = &(*p)->next) {
        ScoutFile *f = *p;
        if (!scout_path_equal(f->path, path)) continue;
        *p = f->next;
        free_result(f);
        free(f->path);
        free(f);
        s->file_count--;
        return 1;
    }
    return 0;
}

void scout_file_forget(ScoutFile *f)
{
    free_result(f);
    f->version = -1;
    memset(&f->stamp, 0, sizeof(f->stamp));
}

/* ---- Results ---- */

/**
 * @brief Skip white space in JSON text.
 * @param p The position.
 * @return The first other character.
 */
static char *skip_ws(char *p)
{
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    return p;
}

/**
 * @brief Cut the array of "tokens" out of a result's text: its numbers
 *        are returned, and the array in the text is blanked to "[ ]".
 * @param text The result's text (changed).
 * @param[out] tokens The numbers as "1,2,3" (allocated, NULL if there are
 *                    none).
 * @return 1 on success (also without tokens), 0 if the array holds
 *         anything but integers, -1 if out of memory.
 */
static int cut_tokens(char *text, char **tokens)
{
    char *p = text, *open, *q, *out;
    size_t n = 0;
    *tokens = NULL;
    for (;;) {
        p = strstr(p, "\"tokens\"");
        if (!p) return 1;
        q = skip_ws(p + 8);
        if (*q == ':') {
            q = skip_ws(q + 1);
            if (*q == '[') break;
        }
        p += 8;
    }
    open = q;
    for (q = open + 1; *q && *q != ']'; q++) {
        if (!((*q >= '0' && *q <= '9') || *q == ',' || *q == '-' || *q == ' ' || *q == '\t' ||
              *q == '\n' || *q == '\r')) {
            return 0;
        }
        n++;
    }
    if (*q != ']') return 0;
    out = (char *)malloc(n + 1);
    if (!out) return -1;
    n = 0;
    for (p = open + 1; p < q; p++) {
        if (*p != ' ' && *p != '\t' && *p != '\n' && *p != '\r') out[n++] = *p;
        *p = ' ';
    }
    out[n] = '\0';
    if (n == 0) {
        free(out);
        out = NULL;
    }
    *tokens = out;
    return 1;
}

/**
 * @brief Read a position [line, col].
 * @param v The array.
 * @param[out] line The line.
 * @param[out] col The column.
 */
static void read_pos(const JsonValue *v, long *line, long *col)
{
    const JsonValue *l = v && v->type == JSON_ARRAY ? json_array_get(v, 0) : NULL;
    const JsonValue *c = v && v->type == JSON_ARRAY ? json_array_get(v, 1) : NULL;
    *line = l && l->type == JSON_NUMBER ? l->u.num_val.int_val : 1;
    *col = c && c->type == JSON_NUMBER ? c->u.num_val.int_val : 1;
}

/**
 * @brief Read a range {"start":[l,c],"end":[l,c]}.
 * @param obj The object holding it.
 * @param key Its key.
 * @param[out] r The range.
 */
static void read_range(const JsonValue *obj, const char *key, ScoutRange *r)
{
    const JsonValue *v = json_obj_get_obj(obj, key);
    read_pos(v ? json_obj_get(v, "start") : NULL, &r->start_line, &r->start_col);
    read_pos(v ? json_obj_get(v, "end") : NULL, &r->end_line, &r->end_col);
}

/**
 * @brief Read the symbols of a result.
 * @param f The file (its symbols are empty).
 * @param list The array.
 * @return 1 on success, 0 if out of memory.
 */
static int read_symbols(ScoutFile *f, const JsonValue *list)
{
    size_t i, n = json_array_length(list);
    if (n == 0) return 1;
    f->symbols = (ScoutSymbol *)calloc(n, sizeof(ScoutSymbol));
    if (!f->symbols) return 0;
    for (i = 0; i < n; i++) {
        const JsonValue *v = json_array_get(list, i);
        ScoutSymbol *s = &f->symbols[f->symbol_count];
        const char *name = v ? json_obj_get_string(v, "name", NULL) : NULL;
        const char *scope = v ? json_obj_get_string(v, "scope", "global") : "";
        if (!name || !*name) continue;
        s->name = dup_str(name);
        s->detail = dup_str(json_obj_get_string(v, "detail", NULL));
        if (!s->name) return 0;
        s->kind = (int)json_obj_get_int(v, "kind", 0);
        s->scope = strcmp(scope, "file") == 0 ? SCOUT_SCOPE_FILE :
                   strcmp(scope, "local") == 0 ? SCOUT_SCOPE_LOCAL : SCOUT_SCOPE_GLOBAL;
        s->declaration = json_obj_get_bool(v, "declaration", 0);
        s->parent = json_obj_get_int(v, "parent", -1);
        read_range(v, "range", &s->range);
        read_range(v, "selection", &s->selection);
        if (!json_obj_get_obj(v, "selection")) s->selection = s->range;
        f->symbol_count++;
    }
    /* parents refer to the parser's indices; skipped entries would shift
       them, so drop parents that are no earlier symbol */
    for (i = 0; i < (size_t)f->symbol_count; i++) {
        if (f->symbols[i].parent >= (long)i) f->symbols[i].parent = -1;
    }
    return 1;
}

/**
 * @brief Read the diagnostics of a result.
 * @param f The file (its diagnostics are empty).
 * @param list The array.
 * @return 1 on success, 0 if out of memory.
 */
static int read_diagnostics(ScoutFile *f, const JsonValue *list)
{
    size_t i, n = json_array_length(list);
    if (n == 0) return 1;
    f->diagnostics = (ScoutDiagnostic *)calloc(n, sizeof(ScoutDiagnostic));
    if (!f->diagnostics) return 0;
    for (i = 0; i < n; i++) {
        const JsonValue *v = json_array_get(list, i);
        ScoutDiagnostic *d = &f->diagnostics[f->diagnostic_count];
        const char *msg = v ? json_obj_get_string(v, "message", NULL) : NULL;
        if (!msg) continue;
        d->message = dup_str(msg);
        d->code = dup_str(json_obj_get_string(v, "code", NULL));
        if (!d->message) return 0;
        d->severity = (int)json_obj_get_int(v, "severity", SCOUT_SEVERITY_ERROR);
        read_range(v, "range", &d->range);
        f->diagnostic_count++;
    }
    return 1;
}

/**
 * @brief Read the references of a result.
 * @param f The file (its references are empty).
 * @param list The array.
 * @return 1 on success, 0 if out of memory.
 */
static int read_references(ScoutFile *f, const JsonValue *list)
{
    size_t i, n = json_array_length(list);
    if (n == 0) return 1;
    f->references = (ScoutReference *)calloc(n, sizeof(ScoutReference));
    if (!f->references) return 0;
    for (i = 0; i < n; i++) {
        const JsonValue *v = json_array_get(list, i);
        ScoutReference *r = &f->references[f->reference_count];
        const char *target = v ? json_obj_get_string(v, "target", NULL) : NULL;
        if (!target || !*target) continue;
        r->target = dup_str(target);
        r->kind = dup_str(json_obj_get_string(v, "kind", "include"));
        if (!r->target || !r->kind) return 0;
        r->system = json_obj_get_bool(v, "system", 0);
        read_range(v, "range", &r->range);
        f->reference_count++;
    }
    return 1;
}

int scout_file_set_result(ScoutFile *f, const char *language, long version,
                          const ScoutStamp *stamp, const char *result)
{
    ScoutFile tmp;
    char *text = dup_str(result);
    char *tokens = NULL;
    JsonValue *root;
    int rc;

    if (!text) return -1;
    rc = cut_tokens(text, &tokens);
    if (rc <= 0) {
        free(text);
        return rc;
    }
    root = json_parse(text, NULL);
    free(text);
    if (!root || root->type != JSON_OBJECT) {
        json_value_free(root);
        free(tokens);
        return 0;
    }
    memset(&tmp, 0, sizeof(tmp));
    tmp.tokens = tokens;
    tmp.language = dup_str(language);
    if (!tmp.language ||
        !read_symbols(&tmp, json_obj_get_array(root, "symbols")) ||
        !read_diagnostics(&tmp, json_obj_get_array(root, "diagnostics")) ||
        !read_references(&tmp, json_obj_get_array(root, "references"))) {
        json_value_free(root);
        free_result(&tmp);
        return -1;
    }
    json_value_free(root);

    free_result(f);
    f->language = tmp.language;
    f->symbols = tmp.symbols;
    f->symbol_count = tmp.symbol_count;
    f->diagnostics = tmp.diagnostics;
    f->diagnostic_count = tmp.diagnostic_count;
    f->references = tmp.references;
    f->reference_count = tmp.reference_count;
    f->tokens = tmp.tokens;
    f->valid = 1;
    f->version = version;
    if (stamp) {
        f->stamp = *stamp;
    } else {
        memset(&f->stamp, 0, sizeof(f->stamp));
    }
    return 1;
}

size_t scout_word_at(const ScoutLanguage *lang, const char *line, long col, int before,
                     char *out, size_t size)
{
    char deflt[256];
    const char *ident;
    long len = (long)strlen(line), start, end;
    size_t n;
    if (!lang) set_class(deflt, NULL);
    ident = lang ? lang->ident : deflt;
    if (col < 1) col = 1;
    if (col > len + 1) col = len + 1;
    start = col - 1;
    end = col - 1;
    while (start > 0 && ident[(unsigned char)line[start - 1]]) start--;
    if (!before) {
        while (end < len && ident[(unsigned char)line[end]]) end++;
    }
    n = (size_t)(end - start);
    if (size == 0) return 0;
    if (n >= size) n = size - 1;
    memcpy(out, line + start, n);
    out[n] = '\0';
    return n;
}

/**
 * @brief Skip white space in constant JSON text.
 * @param p The position.
 * @return The first other character.
 */
static const char *skip_cws(const char *p)
{
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    return p;
}

/**
 * @brief Skip a JSON string.
 * @param p Its opening quote.
 * @return The character after its closing quote, or NULL if it is not
 *         closed.
 */
static const char *skip_string(const char *p)
{
    for (p++; *p && *p != '"'; p++) {
        if (*p == '\\' && p[1]) p++;
    }
    return *p ? p + 1 : NULL;
}

/**
 * @brief Skip a JSON value.
 * @param p Its first character.
 * @return The character after it, or NULL if it is broken.
 */
static const char *skip_value(const char *p)
{
    int depth = 0;
    if (*p == '"') return skip_string(p);
    if (*p != '{' && *p != '[') {
        while (*p && *p != ',' && *p != '}' && *p != ']' && *p != ' ' && *p != '\t' &&
               *p != '\n' && *p != '\r') {
            p++;
        }
        return p;
    }
    while (*p) {
        if (*p == '"') {
            p = skip_string(p);
            if (!p) return NULL;
            continue;
        }
        if (*p == '{' || *p == '[') depth++;
        if (*p == '}' || *p == ']') {
            if (--depth == 0) return p + 1;
        }
        p++;
    }
    return NULL;
}

int scout_json_member(const char *obj, const char *key, const char **value, size_t *length)
{
    const char *p = skip_cws(obj);
    size_t klen = strlen(key);
    if (*p++ != '{') return 0;
    for (;;) {
        const char *k, *end, *v;
        p = skip_cws(p);
        if (*p != '"') return 0;
        k = p + 1;
        end = skip_string(p);
        if (!end) return 0;
        p = skip_cws(end);
        if (*p++ != ':') return 0;
        v = skip_cws(p);
        p = skip_value(v);
        if (!p || p == v) return 0;
        if ((size_t)(end - 1 - k) == klen && memcmp(k, key, klen) == 0) {
            *value = v;
            *length = (size_t)(p - v);
            return 1;
        }
        p = skip_cws(p);
        if (*p++ != ',') return 0;
    }
}

/* ---- Answers ---- */

/**
 * @brief Write a range as {"start":[l,c],"end":[l,c]}.
 * @param w The writer.
 * @param r The range.
 */
static void write_range(JsonWriter *w, const ScoutRange *r)
{
    json_write_raw(w, "{\"start\":[");
    json_write_int(w, r->start_line);
    json_write_raw(w, ",");
    json_write_int(w, r->start_col);
    json_write_raw(w, "],\"end\":[");
    json_write_int(w, r->end_line);
    json_write_raw(w, ",");
    json_write_int(w, r->end_col);
    json_write_raw(w, "]}");
}

/**
 * @brief Write "file" and "version" of a file (without braces).
 * @param w The writer.
 * @param f The file.
 */
static void write_file_head(JsonWriter *w, const ScoutFile *f)
{
    json_write_raw(w, "\"file\":");
    json_write_string(w, f->path);
    json_write_raw(w, ",\"version\":");
    if (f->version >= 0) {
        json_write_int(w, f->version);
    } else {
        json_write_null(w);
    }
}

void scout_write_analyze(JsonWriter *w, const ScoutFile *f)
{
    json_write_raw(w, "{");
    write_file_head(w, f);
    json_write_raw(w, ",\"language\":");
    json_write_string(w, f->language);
    json_write_raw(w, ",\"symbols\":");
    json_write_int(w, f->symbol_count);
    json_write_raw(w, ",\"diagnostics\":");
    json_write_int(w, f->diagnostic_count);
    json_write_raw(w, "}");
}

/**
 * @brief Write a symbol as in the protocol.
 * @param w The writer.
 * @param s The symbol.
 * @param file Its file, written as "file" (NULL: left out).
 */
static void write_symbol(JsonWriter *w, const ScoutSymbol *s, const char *file)
{
    static const char *const scopes[] = { "global", "file", "local" };
    json_write_raw(w, "{\"name\":");
    json_write_string(w, s->name);
    json_write_raw(w, ",\"kind\":");
    json_write_int(w, s->kind);
    json_write_raw(w, ",\"scope\":\"");
    json_write_raw(w, scopes[s->scope]);
    json_write_raw(w, "\"");
    if (s->parent >= 0 && !file) {
        json_write_raw(w, ",\"parent\":");
        json_write_int(w, s->parent);
    }
    if (s->declaration) json_write_raw(w, ",\"declaration\":true");
    if (file) {
        json_write_raw(w, ",\"file\":");
        json_write_string(w, file);
    }
    json_write_raw(w, ",\"range\":");
    write_range(w, &s->range);
    json_write_raw(w, ",\"selection\":");
    write_range(w, &s->selection);
    if (s->detail) {
        json_write_raw(w, ",\"detail\":");
        json_write_string(w, s->detail);
    }
    json_write_raw(w, "}");
}

void scout_write_symbols(JsonWriter *w, const ScoutSession *s, const ScoutFile *f,
                         ScoutMatch match, void *context)
{
    int first = 1;
    long i;
    if (f) {
        json_write_raw(w, "{");
        write_file_head(w, f);
        json_write_raw(w, ",\"symbols\":[");
        for (i = 0; i < f->symbol_count; i++) {
            if (match && !match(context, f->symbols[i].name)) continue;
            if (!first) json_write_raw(w, ",");
            first = 0;
            write_symbol(w, &f->symbols[i], NULL);
        }
        json_write_raw(w, "]}");
        return;
    }
    json_write_raw(w, "{\"symbols\":[");
    for (f = s->files; f; f = f->next) {
        if (!f->in_project || !f->valid) continue;
        for (i = 0; i < f->symbol_count; i++) {
            const ScoutSymbol *sym = &f->symbols[i];
            if (sym->scope == SCOUT_SCOPE_LOCAL || (match && !match(context, sym->name))) continue;
            if (!first) json_write_raw(w, ",");
            first = 0;
            write_symbol(w, sym, f->path);
        }
    }
    json_write_raw(w, "]}");
}

/**
 * @brief Write the diagnostics of a file as an array.
 * @param w The writer.
 * @param f The file.
 */
static void write_diag_list(JsonWriter *w, const ScoutFile *f)
{
    long i;
    json_write_raw(w, "[");
    for (i = 0; i < f->diagnostic_count; i++) {
        const ScoutDiagnostic *d = &f->diagnostics[i];
        if (i > 0) json_write_raw(w, ",");
        json_write_raw(w, "{\"severity\":");
        json_write_int(w, d->severity);
        json_write_raw(w, ",\"message\":");
        json_write_string(w, d->message);
        if (d->code) {
            json_write_raw(w, ",\"code\":");
            json_write_string(w, d->code);
        }
        json_write_raw(w, ",\"range\":");
        write_range(w, &d->range);
        json_write_raw(w, "}");
    }
    json_write_raw(w, "]");
}

void scout_write_diagnostics(JsonWriter *w, const ScoutSession *s, const ScoutFile *f)
{
    int first = 1;
    if (f) {
        json_write_raw(w, "{");
        write_file_head(w, f);
        json_write_raw(w, ",\"diagnostics\":");
        write_diag_list(w, f);
        json_write_raw(w, "}");
        return;
    }
    json_write_raw(w, "{\"files\":[");
    for (f = s->files; f; f = f->next) {
        if (!f->in_project || !f->valid || f->diagnostic_count == 0) continue;
        if (!first) json_write_raw(w, ",");
        first = 0;
        json_write_raw(w, "{");
        write_file_head(w, f);
        json_write_raw(w, ",\"diagnostics\":");
        write_diag_list(w, f);
        json_write_raw(w, "}");
    }
    json_write_raw(w, "]}");
}

void scout_write_tokens(JsonWriter *w, const ScoutFile *f)
{
    json_write_raw(w, "{");
    write_file_head(w, f);
    json_write_raw(w, ",\"tokens\":[");
    if (f->tokens) json_write_raw(w, f->tokens);
    json_write_raw(w, "]}");
}

/**
 * @brief Tell whether a position lies in a range.
 * @param r The range.
 * @param line The line.
 * @param col The column.
 * @return Non-zero if it does.
 */
static int in_range(const ScoutRange *r, long line, long col)
{
    if (line < r->start_line || line > r->end_line) return 0;
    if (line == r->start_line && col < r->start_col) return 0;
    if (line == r->end_line && col > r->end_col) return 0;
    return 1;
}

/**
 * @brief Tell whether a symbol kind is a function of some sort (whose
 *        locals are looked up).
 * @param kind SCOUT_SYMBOL_ value.
 * @return Non-zero if it is.
 */
static int is_function_kind(int kind)
{
    return kind == SCOUT_SYMBOL_FUNCTION || kind == SCOUT_SYMBOL_METHOD ||
           kind == SCOUT_SYMBOL_CONSTRUCTOR || kind == SCOUT_SYMBOL_PROCEDURE;
}

/**
 * @brief Find the innermost function of a file at a position.
 * @param f The file.
 * @param line The line.
 * @param col The column.
 * @return Its index, or -1.
 */
static long function_at(const ScoutFile *f, long line, long col)
{
    long i, best = -1;
    for (i = 0; i < f->symbol_count; i++) {
        const ScoutSymbol *s = &f->symbols[i];
        if (!is_function_kind(s->kind) || s->declaration || !in_range(&s->range, line, col)) continue;
        if (best < 0 || f->symbols[best].range.start_line < s->range.start_line ||
            (f->symbols[best].range.start_line == s->range.start_line &&
             f->symbols[best].range.start_col < s->range.start_col)) {
            best = i;
        }
    }
    return best;
}

/**
 * @brief Tell whether a symbol belongs (directly or further down) to
 *        another one.
 * @param f The file.
 * @param i The symbol.
 * @param ancestor The other one.
 * @return Non-zero if it does.
 */
static int belongs_to(const ScoutFile *f, long i, long ancestor)
{
    int guard = 64;
    while (i >= 0 && guard-- > 0) {
        i = f->symbols[i].parent;
        if (i == ancestor) return 1;
    }
    return 0;
}

/** A location found by DEFINITION. */
typedef struct Location {
    const ScoutFile *file;      /**< the file */
    const ScoutSymbol *sym;     /**< the symbol */
} Location;

/**
 * @brief Write the result of DEFINITION.
 * @param w The writer.
 * @param name The name looked for.
 * @param loc The locations.
 * @param n Their number.
 */
static void write_locations(JsonWriter *w, const char *name, const Location *loc, long n)
{
    long i;
    json_write_raw(w, "{\"name\":");
    json_write_string(w, name);
    json_write_raw(w, ",\"locations\":[");
    for (i = 0; i < n; i++) {
        if (i > 0) json_write_raw(w, ",");
        json_write_raw(w, "{\"file\":");
        json_write_string(w, loc[i].file->path);
        json_write_raw(w, ",\"name\":");
        json_write_string(w, loc[i].sym->name);
        json_write_raw(w, ",\"kind\":");
        json_write_int(w, loc[i].sym->kind);
        if (loc[i].sym->declaration) json_write_raw(w, ",\"declaration\":true");
        json_write_raw(w, ",\"range\":");
        write_range(w, &loc[i].sym->range);
        json_write_raw(w, ",\"selection\":");
        write_range(w, &loc[i].sym->selection);
        if (loc[i].sym->detail) {
            json_write_raw(w, ",\"detail\":");
            json_write_string(w, loc[i].sym->detail);
        }
        json_write_raw(w, "}");
    }
    json_write_raw(w, "]}");
}

/**
 * @brief Collect the non-local symbols of a name in a file.
 * @param f The file.
 * @param name The name.
 * @param global_only Only symbols visible to other files.
 * @param declarations Collect declarations (otherwise definitions).
 * @param loc The locations, appended to.
 * @param n Their number, counted up.
 */
static void collect(const ScoutFile *f, const char *name, int global_only, int declarations,
                    Location *loc, long *n)
{
    long i;
    if (!f->valid) return;
    for (i = 0; i < f->symbol_count && *n < MAX_LOCATIONS; i++) {
        const ScoutSymbol *s = &f->symbols[i];
        if (s->scope == SCOUT_SCOPE_LOCAL || (global_only && s->scope != SCOUT_SCOPE_GLOBAL)) continue;
        if (!s->declaration != !declarations) continue;
        if (strcmp(s->name, name) != 0) continue;
        loc[*n].file = f;
        loc[(*n)++].sym = s;
    }
}

void scout_write_definition(JsonWriter *w, const ScoutSession *s, const ScoutFile *f,
                            long line, long col, const char *name)
{
    Location loc[MAX_LOCATIONS];
    long n = 0, i, func;
    const ScoutFile *g;
    int pass;

    if (f && f->valid && (func = function_at(f, line, col)) >= 0) {
        for (i = func + 1; i < f->symbol_count && n < MAX_LOCATIONS; i++) {
            const ScoutSymbol *sym = &f->symbols[i];
            if (sym->kind == SCOUT_SYMBOL_FIELD || strcmp(sym->name, name) != 0) continue;
            if (!belongs_to(f, i, func)) continue;
            loc[n].file = f;
            loc[n++].sym = sym;
        }
        if (n > 0) {
            write_locations(w, name, loc, n);
            return;
        }
    }
    for (pass = 0; pass < 2 && n == 0; pass++) {
        if (f) collect(f, name, 0, pass, loc, &n);
        for (g = s->files; g; g = g->next) {
            if (g != f) collect(g, name, 1, pass, loc, &n);
        }
    }
    for (g = s->files; g && n == 0; g = g->next) {
        if (!g->valid) continue;
        for (i = 0; i < g->symbol_count && n < MAX_LOCATIONS; i++) {
            if (g->symbols[i].kind == SCOUT_SYMBOL_FIELD && strcmp(g->symbols[i].name, name) == 0) {
                loc[n].file = g;
                loc[n++].sym = &g->symbols[i];
            }
        }
    }
    write_locations(w, name, loc, n);
}

/** A candidate of COMPLETE. */
typedef struct Candidate {
    const char *label;          /**< the text */
    const ScoutSymbol *sym;     /**< its symbol, NULL for a keyword */
    int rank;                   /**< 0 local, 1 file, 2 project, 3 keyword */
} Candidate;

/** The candidates of COMPLETE being collected. */
typedef struct Candidates {
    Candidate *list;            /**< the candidates */
    long count;                 /**< their number */
    const char *prefix;         /**< the prefix */
} Candidates;

/**
 * @brief Add a candidate, unless its text is there already.
 * @param c The candidates.
 * @param label The text.
 * @param sym Its symbol, NULL for a keyword.
 * @param rank Its rank.
 */
static void add_candidate(Candidates *c, const char *label, const ScoutSymbol *sym, int rank)
{
    long i;
    if (c->count >= MAX_CANDIDATES || !has_prefix(label, c->prefix)) return;
    for (i = 0; i < c->count; i++) {
        if (strcmp(c->list[i].label, label) == 0) return;
    }
    c->list[c->count].label = label;
    c->list[c->count].sym = sym;
    c->list[c->count++].rank = rank;
}

/**
 * @brief qsort() order of candidates: by rank, then by name.
 * @param a A candidate.
 * @param b Another candidate.
 * @return Less than, equal to or greater than 0.
 */
static int candidate_order(const void *a, const void *b)
{
    const Candidate *x = (const Candidate *)a, *y = (const Candidate *)b;
    if (x->rank != y->rank) return x->rank - y->rank;
    return name_order(x->label, y->label);
}

void scout_write_complete(JsonWriter *w, const ScoutStore *st, const ScoutSession *s,
                          const ScoutFile *f, long line, long col, const char *prefix, long max)
{
    Candidates c;
    const ScoutFile *g;
    long i, func;

    c.list = (Candidate *)malloc(MAX_CANDIDATES * sizeof(Candidate));
    c.count = 0;
    c.prefix = prefix;
    if (c.list) {
        if (f && f->valid) {
            func = function_at(f, line, col);
            for (i = func + 1; func >= 0 && i < f->symbol_count; i++) {
                const ScoutSymbol *sym = &f->symbols[i];
                if (sym->kind != SCOUT_SYMBOL_FIELD && belongs_to(f, i, func)) add_candidate(&c, sym->name, sym, 0);
            }
            for (i = 0; i < f->symbol_count; i++) {
                const ScoutSymbol *sym = &f->symbols[i];
                if (sym->scope != SCOUT_SCOPE_LOCAL) add_candidate(&c, sym->name, sym, 1);
            }
        }
        for (g = s->files; g; g = g->next) {
            if (g == f || !g->valid) continue;
            for (i = 0; i < g->symbol_count; i++) {
                const ScoutSymbol *sym = &g->symbols[i];
                if (sym->scope == SCOUT_SCOPE_GLOBAL) add_candidate(&c, sym->name, sym, 2);
            }
        }
        if (f && f->language) {
            const ScoutLanguage *lang = scout_store_language(st, f->language);
            for (i = 0; lang && i < lang->keyword_count; i++) add_candidate(&c, lang->keywords[i], NULL, 3);
        }
        qsort(c.list, (size_t)c.count, sizeof(Candidate), candidate_order);
    }
    json_write_raw(w, "{\"prefix\":");
    json_write_string(w, prefix);
    json_write_raw(w, ",\"items\":[");
    for (i = 0; i < c.count && i < max; i++) {
        if (i > 0) json_write_raw(w, ",");
        json_write_raw(w, "{\"label\":");
        json_write_string(w, c.list[i].label);
        if (c.list[i].sym) {
            json_write_raw(w, ",\"kind\":");
            json_write_int(w, c.list[i].sym->kind);
            if (c.list[i].sym->detail) {
                json_write_raw(w, ",\"detail\":");
                json_write_string(w, c.list[i].sym->detail);
            }
        } else {
            json_write_raw(w, ",\"keyword\":true");
        }
        json_write_raw(w, "}");
    }
    json_write_raw(w, "]}");
    free(c.list);
}

void scout_write_languages(JsonWriter *w, const ScoutStore *st)
{
    long i, j;
    json_write_raw(w, "{\"languages\":[");
    for (i = 0; i < st->language_count; i++) {
        const ScoutLanguage *l = &st->languages[i];
        if (i > 0) json_write_raw(w, ",");
        json_write_raw(w, "{\"id\":");
        json_write_string(w, l->id);
        json_write_raw(w, ",\"name\":");
        json_write_string(w, l->name);
        json_write_raw(w, ",\"extensions\":[");
        for (j = 0; j < l->extension_count; j++) {
            if (j > 0) json_write_raw(w, ",");
            json_write_string(w, l->extensions[j]);
        }
        json_write_raw(w, "],\"parser\":");
        json_write_string(w, st->parsers[l->parser].name);
        json_write_raw(w, "}");
    }
    json_write_raw(w, "]}");
}

void scout_write_parsers(JsonWriter *w, const ScoutStore *st)
{
    long i, j;
    json_write_raw(w, ",\"parsers\":[");
    for (i = 0; i < st->parser_count; i++) {
        const ScoutParser *p = &st->parsers[i];
        int first = 1;
        if (i > 0) json_write_raw(w, ",");
        json_write_raw(w, "{\"name\":");
        json_write_string(w, p->name);
        json_write_raw(w, ",\"version\":");
        json_write_string(w, p->version);
        json_write_raw(w, ",\"path\":");
        json_write_string(w, p->path);
        json_write_raw(w, ",\"languages\":[");
        for (j = 0; j < st->language_count; j++) {
            if (st->languages[j].parser != i) continue;
            if (!first) json_write_raw(w, ",");
            first = 0;
            json_write_string(w, st->languages[j].id);
        }
        json_write_raw(w, "]");
        if (p->disabled) json_write_raw(w, ",\"disabled\":true");
        json_write_raw(w, "}");
    }
    json_write_raw(w, "],\"problems\":[");
    for (i = 0; i < st->problem_count; i++) {
        if (i > 0) json_write_raw(w, ",");
        json_write_string(w, st->problems[i]);
    }
    json_write_raw(w, "]");
}
