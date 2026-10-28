/**
 * @file test_scout_store.c
 * @brief Host-native tests of Scout's portable core, fed with the answers
 *        of the C parser for the sample sources in tests/data/c_parser/.
 */
#include "scout_store.h"
#include "scout_protocol.h"
#include "scout_rpc.h"
#include "c_parser.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** Directory of the sample sources (tests run from the project root). */
#define DATA "tests/data/c_parser/"

/** Number of checks made. */
static int g_test_count = 0;
/** Number of checks that failed. */
static int g_fail_count = 0;

/**
 * @brief Check a condition and report it if it fails (counting both).
 * @param cond The condition.
 */
#define TEST_ASSERT(cond) do { \
    g_test_count++; \
    if (!(cond)) { \
        printf("FAIL: %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        g_fail_count++; \
    } \
} while (0)

/**
 * @brief Ask the C parser and return the result of its answer.
 * @param request The request.
 * @return The result as JSON text (free() it), or NULL on an error.
 */
static char *parser_result(const char *request)
{
    JsonWriter w, r;
    ScoutRpcMessage m;
    char *text = NULL;
    json_writer_init_alloc(&w, 1024);
    c_parser_handle_request(request, &w);
    if (scout_rpc_parse(json_writer_get(&w), &m) && m.result) {
        json_writer_init_alloc(&r, 1024);
        json_write_value(&r, m.result, -1);
        text = (char *)malloc(json_writer_length(&r) + 1);
        if (text) memcpy(text, json_writer_get(&r), json_writer_length(&r) + 1);
        json_writer_free(&r);
    }
    scout_rpc_free(&m);
    json_writer_free(&w);
    return text;
}

/**
 * @brief Analyse a sample with the C parser and keep the result in a file
 *        of a session.
 * @param s The session.
 * @param name The sample's name.
 * @param path The path the file gets in the session.
 * @return The file.
 */
static ScoutFile *analyze(ScoutSession *s, const char *name, const char *path)
{
    char request[512];
    char *result;
    ScoutFile *f = scout_file_get(s, path);
    snprintf(request, sizeof(request),
             "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"scout/analyze\",\"params\":"
             "{\"path\":\"%s\",\"text\":\"" DATA "%s\"}}", path, name);
    result = parser_result(request);
    TEST_ASSERT(result != NULL);
    TEST_ASSERT(f && result && scout_file_set_result(f, "c", -1, NULL, result) == 1);
    free(result);
    return f;
}

/**
 * @brief Write with a function into a fresh writer and parse the text.
 * @param w The writer (initialised here; freed by the caller).
 * @return The parsed text (json_value_free() it), or NULL.
 */
static JsonValue *parse_written(JsonWriter *w)
{
    return json_parse(json_writer_get(w), NULL);
}

/**
 * @brief Count the locations of a DEFINITION answer and return the file
 *        and line of the first.
 * @param st The store.
 * @param s The session.
 * @param f The file.
 * @param line The position's line.
 * @param col Its column.
 * @param name The name.
 * @param[out] file The first location's file (copied), or "".
 * @param size Size of @p file.
 * @param[out] at The first location's line, 0.
 * @return The number of locations.
 */
static size_t definition(const ScoutStore *st, const ScoutSession *s, const ScoutFile *f,
                         long line, long col, const char *name, char *file, size_t size, long *at)
{
    JsonWriter w;
    JsonValue *v;
    const JsonValue *locs;
    size_t n = 0;
    (void)st;
    json_writer_init_alloc(&w, 256);
    scout_write_definition(&w, s, f, line, col, name);
    v = parse_written(&w);
    locs = v ? json_obj_get_array(v, "locations") : NULL;
    file[0] = '\0';
    *at = 0;
    if (locs && (n = json_array_length(locs)) > 0) {
        const JsonValue *l = json_array_get(locs, 0);
        const JsonValue *sel = json_obj_get_obj(l, "selection");
        const JsonValue *start = sel ? json_obj_get_array(sel, "start") : NULL;
        snprintf(file, size, "%s", json_obj_get_string(l, "file", ""));
        if (start) *at = json_array_get(start, 0)->u.num_val.int_val;
    }
    json_value_free(v);
    json_writer_free(&w);
    return n;
}

/**
 * @brief Return the labels of a COMPLETE answer, separated by spaces.
 * @param st The store.
 * @param s The session.
 * @param f The file.
 * @param line The position's line.
 * @param col Its column.
 * @param prefix The prefix.
 * @param max Most items.
 * @param[out] out The labels.
 * @param size Size of @p out.
 */
static void complete(const ScoutStore *st, const ScoutSession *s, const ScoutFile *f,
                     long line, long col, const char *prefix, long max, char *out, size_t size)
{
    JsonWriter w;
    JsonValue *v;
    const JsonValue *items;
    size_t i, len = 0;
    json_writer_init_alloc(&w, 256);
    scout_write_complete(&w, st, s, f, line, col, prefix, max);
    v = parse_written(&w);
    items = v ? json_obj_get_array(v, "items") : NULL;
    out[0] = '\0';
    for (i = 0; items && i < json_array_length(items); i++) {
        const char *label = json_obj_get_string(json_array_get(items, i), "label", "?");
        len += (size_t)snprintf(out + len, size - len, "%s%s", i ? " " : "", label);
        if (len >= size) break;
    }
    json_value_free(v);
    json_writer_free(&w);
}

/**
 * @brief Match callback for the tests: names starting with the context.
 * @param context The start (a string).
 * @param name A symbol's name.
 * @return Non-zero if it starts so.
 */
static int starts_with(void *context, const char *name)
{
    return strncmp(name, (const char *)context, strlen((const char *)context)) == 0;
}

/**
 * @brief Parsers and languages: the C parser's description, conflicts,
 *        REGISTER, invalid descriptions.
 * @param st The store (left with the C parser).
 */
static void test_parsers(ScoutStore *st)
{
    char *describe = parser_result("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"scout/describe\"}");
    const ScoutLanguage *c;
    JsonWriter w;
    JsonValue *v;
    TEST_ASSERT(describe != NULL);
    if (!describe) return;

    TEST_ASSERT(scout_store_add_parser(st, "Scout:Parsers/c_parser", describe, NULL) == 1);
    c = scout_store_language(st, "c");
    TEST_ASSERT(c && c->extension_count == 2 && c->case_sensitive && c->keyword_count > 30);
    TEST_ASSERT(c && c->syntax && strstr(c->syntax, "\"lineComments\"") != NULL);
    TEST_ASSERT(c && c->ident['_'] && c->ident['9'] && !c->ident['-']);
    TEST_ASSERT(scout_store_language_for(st, "Work:src/MAIN.C") == c);
    TEST_ASSERT(scout_store_language_for(st, "Work:inc/x.h") == c);
    TEST_ASSERT(scout_store_language_for(st, "Work:README") == NULL);
    TEST_ASSERT(scout_store_language_for(st, "Work:a.b/README") == NULL);

    /* a second parser for the same language is refused */
    TEST_ASSERT(scout_store_add_parser(st, "Scout:Parsers/other", describe, NULL) == 1);
    TEST_ASSERT(st->language_count == 1 && st->problem_count == 1);
    /* REGISTER: an extension moves to another parser */
    TEST_ASSERT(scout_store_add_parser(st, "Work:my_parser",
        "{\"protocol\":1,\"name\":\"mine\",\"languages\":[{\"id\":\"mine\",\"extensions\":[\".mine\"]}]}",
        ".H") == 1);
    TEST_ASSERT(scout_store_language_for(st, "x.h") == scout_store_language(st, "mine"));
    TEST_ASSERT(scout_store_language(st, "c")->extension_count == 1);
    /* unusable descriptions */
    TEST_ASSERT(scout_store_add_parser(st, "bad1", "not json", NULL) == 0);
    TEST_ASSERT(scout_store_add_parser(st, "bad2", "{\"protocol\":2,\"languages\":[{\"id\":\"x\"}]}", NULL) == 0);
    TEST_ASSERT(scout_store_add_parser(st, "bad3", "{\"protocol\":1,\"languages\":[]}", NULL) == 0);
    TEST_ASSERT(st->problem_count == 4);

    json_writer_init_alloc(&w, 256);
    scout_write_parsers(&w, st);
    TEST_ASSERT(strncmp(json_writer_get(&w), ",\"parsers\":[{\"name\":\"c_parser\"", 30) == 0);
    json_writer_free(&w);

    /* again, as RESCAN does */
    scout_store_clear_parsers(st);
    TEST_ASSERT(st->parser_count == 0 && st->language_count == 0 && st->problem_count == 0);
    TEST_ASSERT(scout_store_add_parser(st, "Scout:Parsers/c_parser", describe, NULL) == 1);
    json_writer_init_alloc(&w, 256);
    scout_write_languages(&w, st);
    v = parse_written(&w);
    TEST_ASSERT(v && json_array_length(json_obj_get_array(v, "languages")) == 1);
    json_value_free(v);
    json_writer_free(&w);
    free(describe);
}

/**
 * @brief Sessions and files: results, queries of a file and the project,
 *        definition, completion, forgetting, closing.
 * @param st The store with the C parser.
 */
static void test_sessions(ScoutStore *st)
{
    ScoutSession *s = scout_session_open(st, "Work:Hello");
    ScoutFile *c, *h;
    JsonWriter w;
    JsonValue *v;
    char file[256], labels[1024], word[64];
    long at;
    size_t n;
    ScoutStamp stamp = { 1, 2, 3, 4 };

    TEST_ASSERT(s && s->id == 1 && scout_session_find(st, 1) == s && scout_session_find(st, 0) != NULL);
    if (!s) return;
    TEST_ASSERT(scout_session_add_include(s, "Work:SDK") && scout_session_add_include(s, "work:sdk"));
    TEST_ASSERT(s->include_count == 1);

    c = analyze(s, "sample.c", "Work:Hello/sample.c");
    h = analyze(s, "sample.h", "Work:Hello/sample.h");
    TEST_ASSERT(s->file_count == 2 && scout_file_find(s, "WORK:hello/SAMPLE.C") == c);
    c->in_project = h->in_project = 1;
    TEST_ASSERT(c->valid && c->symbol_count == 17 && c->reference_count == 2 && c->tokens != NULL);
    TEST_ASSERT(c->tokens && strchr(c->tokens, ' ') == NULL);

    /* ANALYZE */
    json_writer_init_alloc(&w, 256);
    scout_write_analyze(&w, c);
    v = parse_written(&w);
    TEST_ASSERT(v && json_obj_get_int(v, "symbols", 0) == c->symbol_count);
    TEST_ASSERT(v && json_obj_get(v, "version")->type == JSON_NULL);
    json_value_free(v);
    json_writer_free(&w);

    /* SYMBOLS of a file, matched, and of the project (no locals) */
    json_writer_init_alloc(&w, 1024);
    scout_write_symbols(&w, s, c, NULL, NULL);
    v = parse_written(&w);
    TEST_ASSERT(v && (long)json_array_length(json_obj_get_array(v, "symbols")) == c->symbol_count);
    json_value_free(v);
    json_writer_free(&w);
    json_writer_init_alloc(&w, 1024);
    scout_write_symbols(&w, s, NULL, starts_with, "add_");
    v = parse_written(&w);
    TEST_ASSERT(v && json_array_length(json_obj_get_array(v, "symbols")) == 2);
    TEST_ASSERT(v && strcmp(json_obj_get_string(json_array_get(json_obj_get_array(v, "symbols"), 0), "file", ""),
                            "Work:Hello/sample.c") == 0);
    json_value_free(v);
    json_writer_free(&w);
    json_writer_init_alloc(&w, 1024);
    scout_write_symbols(&w, s, NULL, starts_with, "mask");
    TEST_ASSERT(strstr(json_writer_get(&w), "\"mask\"") == NULL);
    json_writer_free(&w);

    /* TOKENS: passed on as the parser gave them */
    json_writer_init_alloc(&w, 1024);
    scout_write_tokens(&w, c);
    v = parse_written(&w);
    TEST_ASSERT(v && json_array_length(json_obj_get_array(v, "tokens")) % 4 == 0 &&
                json_array_length(json_obj_get_array(v, "tokens")) > 40);
    json_value_free(v);
    json_writer_free(&w);

    /* DIAGNOSTICS: none in the samples */
    json_writer_init_alloc(&w, 256);
    scout_write_diagnostics(&w, s, NULL);
    TEST_ASSERT(strcmp(json_writer_get(&w), "{\"files\":[]}") == 0);
    json_writer_free(&w);

    /* DEFINITION: a local, a function of the file, a type from the header,
       a macro from the header, a field, nothing */
    n = definition(st, s, c, 20, 17, "mask", file, sizeof(file), &at);
    TEST_ASSERT(n == 1 && at == 12 && strcmp(file, "Work:Hello/sample.c") == 0);
    n = definition(st, s, c, 20, 17, "p", file, sizeof(file), &at);
    TEST_ASSERT(n == 1 && at == 10);
    n = definition(st, s, c, 28, 20, "helper", file, sizeof(file), &at);
    TEST_ASSERT(n == 1 && at == 10);
    n = definition(st, s, c, 10, 19, "Point", file, sizeof(file), &at);
    TEST_ASSERT(n == 2 && strcmp(file, "Work:Hello/sample.h") == 0 && at == 10);
    n = definition(st, s, c, 17, 16, "MAX_ITEMS", file, sizeof(file), &at);
    TEST_ASSERT(n == 1 && strcmp(file, "Work:Hello/sample.h") == 0 && at == 7);
    /* the definition in sample.c wins over the prototype in sample.h */
    n = definition(st, s, h, 20, 6, "add_numbers", file, sizeof(file), &at);
    TEST_ASSERT(n == 1 && strcmp(file, "Work:Hello/sample.c") == 0 && at == 26);
    n = definition(st, s, c, 17, 12, "x", file, sizeof(file), &at);
    TEST_ASSERT(n == 1 && strcmp(file, "Work:Hello/sample.h") == 0 && at == 11);
    n = definition(st, s, c, 1, 1, "nothing", file, sizeof(file), &at);
    TEST_ASSERT(n == 0);
    /* "mask" outside its function is unknown */
    n = definition(st, s, c, 28, 5, "mask", file, sizeof(file), &at);
    TEST_ASSERT(n == 0);

    /* COMPLETE: locals first, then file, project and keywords */
    complete(st, s, c, 20, 9, "", 5, labels, sizeof(labels));
    TEST_ASSERT(strcmp(labels, "c done flags i k") == 0);
    complete(st, s, c, 20, 9, "ma", 10, labels, sizeof(labels));
    TEST_ASSERT(strcmp(labels, "mask MAX_ITEMS") == 0);
    complete(st, s, c, 29, 5, "wh", 10, labels, sizeof(labels));
    TEST_ASSERT(strcmp(labels, "while") == 0);
    complete(st, s, c, 29, 5, "RE", 10, labels, sizeof(labels));
    TEST_ASSERT(strcmp(labels, "RED register restrict return") == 0);

    /* words */
    TEST_ASSERT(scout_word_at(scout_store_language(st, "c"), "    mask += SQUARE(i);", 7, 0, word, sizeof(word)) == 4 &&
                strcmp(word, "mask") == 0);
    TEST_ASSERT(scout_word_at(NULL, "    mask += SQUARE(i);", 7, 1, word, sizeof(word)) == 2 &&
                strcmp(word, "ma") == 0);
    TEST_ASSERT(scout_word_at(NULL, "x = SQUARE", 11, 0, word, sizeof(word)) == 6 && strcmp(word, "SQUARE") == 0);
    TEST_ASSERT(scout_word_at(NULL, "a + b", 3, 0, word, sizeof(word)) == 0);
    TEST_ASSERT(scout_word_at(NULL, "", 5, 0, word, sizeof(word)) == 0);

    /* a snapshot with a version, then FORGET */
    TEST_ASSERT(scout_file_set_result(c, "c", 7, NULL,
        "{\"language\":\"c\",\"symbols\":[{\"name\":\"x\",\"kind\":13,\"parent\":4}],"
        "\"diagnostics\":[{\"severity\":1,\"message\":\"oops\",\"range\":{\"start\":[2,3],\"end\":[2,4]}}],"
        "\"tokens\":[ 1, 2,\n 3, 4 ]}") == 1);
    TEST_ASSERT(c->version == 7 && c->symbol_count == 1 && c->symbols[0].parent == -1);
    TEST_ASSERT(c->symbols[0].selection.start_line == 1 && c->tokens && strcmp(c->tokens, "1,2,3,4") == 0);
    json_writer_init_alloc(&w, 256);
    scout_write_diagnostics(&w, s, NULL);
    v = parse_written(&w);
    TEST_ASSERT(v && json_array_length(json_obj_get_array(v, "files")) == 1);
    json_value_free(v);
    json_writer_free(&w);
    json_writer_init_alloc(&w, 256);
    scout_write_tokens(&w, c);
    TEST_ASSERT(strcmp(json_writer_get(&w),
                       "{\"file\":\"Work:Hello/sample.c\",\"version\":7,\"tokens\":[1,2,3,4]}") == 0);
    json_writer_free(&w);
    /* invalid results change nothing */
    TEST_ASSERT(scout_file_set_result(c, "c", 8, NULL, "{\"tokens\":[1,\"x\"]}") == 0);
    TEST_ASSERT(scout_file_set_result(c, "c", 8, NULL, "[1]") == 0);
    TEST_ASSERT(c->version == 7 && c->symbol_count == 1);
    scout_file_forget(c);
    TEST_ASSERT(!c->valid && c->version == -1 && c->symbol_count == 0);
    TEST_ASSERT(scout_file_set_result(c, "c", -1, &stamp, "{}") == 1);
    TEST_ASSERT(c->valid && c->stamp.size == 4 && c->tokens == NULL);

    scout_session_set_error(s, 5, "the parser failed");
    TEST_ASSERT(s->error_code == 5 && strcmp(s->error_text, "the parser failed") == 0);

    TEST_ASSERT(scout_session_close(st, 1) && scout_session_find(st, 1) == NULL);
    TEST_ASSERT(!scout_session_close(st, 1));
    TEST_ASSERT(scout_session_close(st, 0) && scout_session_find(st, 0) != NULL);
    s = scout_session_open(st, "Work:Other");
    TEST_ASSERT(s && s->id == 2);
}

/**
 * @brief Run the tests of Scout's core.
 * @return 0 when all checks pass, 1 otherwise.
 */
int main(void)
{
    ScoutStore st;
    const char *v;
    size_t n;
    printf("Running Scout store tests...\n");
    TEST_ASSERT(scout_json_member("{\"jsonrpc\":\"2.0\", \"id\" : 7 ,\"result\":{\"a\":[1,\"}]\"],\"b\":{}}}",
                                  "result", &v, &n));
    TEST_ASSERT(n == 21 && strncmp(v, "{\"a\":[1,\"}]\"],\"b\":{}}", n) == 0);
    TEST_ASSERT(scout_json_member("{\"jsonrpc\":\"2.0\",\"id\":7,\"result\":null}", "id", &v, &n) &&
                n == 1 && *v == '7');
    TEST_ASSERT(!scout_json_member("{\"jsonrpc\":\"2.0\"}", "result", &v, &n));
    TEST_ASSERT(!scout_json_member("[1]", "result", &v, &n));
    TEST_ASSERT(!scout_json_member("{\"result\":[1,2", "result", &v, &n));
    TEST_ASSERT(scout_store_init(&st));
    test_parsers(&st);
    test_sessions(&st);
    scout_store_free(&st);
    printf("Scout store tests completed: %d tests run, %d failures.\n", g_test_count, g_fail_count);
    return g_fail_count ? 1 : 0;
}
