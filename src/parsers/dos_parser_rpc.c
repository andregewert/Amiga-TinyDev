/**
 * @file dos_parser_rpc.c
 * @brief The Scout protocol of the AmigaDOS script parser: scout/describe,
 *        scout/analyze and the program reading one request per line from
 *        STDIN.
 */
#include "dos_parser.h"
#include "scout_rpc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** @name Parts of the result of scout/analyze ("want") */
/** @{ */
#define WANT_SYMBOLS        1   /**< "symbols" */
#define WANT_DIAGNOSTICS    2   /**< "diagnostics" */
#define WANT_REFERENCES     4   /**< "references" */
#define WANT_TOKENS         8   /**< "tokens" */
#define WANT_ALL            15  /**< all of them */
/** @} */

/**
 * @brief Write a list of words as a JSON array.
 * @param w The writer.
 * @param list The words, ended by NULL.
 */
static void write_words(JsonWriter *w, const char *const *list)
{
    size_t i;
    json_write_arr_start(w);
    for (i = 0; list[i]; i++) {
        if (i > 0) json_write_raw(w, ",");
        json_write_string(w, list[i]);
    }
    json_write_arr_end(w);
}

void dos_parser_describe(JsonWriter *w)
{
    char version[8];
    version[0] = (char)('0' + SCOUT_PROTOCOL_VERSION);
    version[1] = '\0';
    json_write_raw(w, "{\"protocol\":");
    json_write_raw(w, version);
    json_write_raw(w, ",\"name\":\"dos_parser\",\"version\":\"1.0\","
                      "\"languages\":[{\"id\":\"amigados\",\"name\":\"AmigaDOS\","
                      "\"extensions\":[\".script\",\".dos\"],"
                      "\"syntax\":{\"caseSensitive\":false,"
                      "\"identifier\":{\"start\":\"A-Za-z_\",\"part\":\"A-Za-z0-9_\"},"
                      "\"lineComments\":[\";\"],"
                      "\"strings\":[{\"open\":\"\\\"\",\"close\":\"\\\"\",\"escape\":\"*\",\"multiline\":false}],"
                      "\"preprocessor\":{\"prefix\":\".\"},"
                      "\"keywords\":{\"keyword\":");
    write_words(w, dos_parser_keywords);
    json_write_raw(w, ",\"builtin\":");
    write_words(w, dos_parser_commands);
    json_write_raw(w, ",\"constant\":");
    write_words(w, dos_parser_constants);
    json_write_raw(w, "}}}],\"capabilities\":{\"symbols\":true,\"diagnostics\":true,"
                      "\"references\":false,\"tokens\":true}}");
}

/**
 * @brief Write a range as {"start":[l,c],"end":[l,c]}.
 * @param w The writer.
 * @param r The range.
 */
static void write_range(JsonWriter *w, const DosRange *r)
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
 * @brief Write the result of scout/analyze.
 * @param w The writer.
 * @param res The analysis.
 * @param want The parts to write (WANT_ flags).
 */
static void write_result(JsonWriter *w, const DosParseResult *res, int want)
{
    long i;
    json_write_raw(w, "{\"language\":\"amigados\"");
    if (want & WANT_SYMBOLS) {
        json_write_raw(w, ",\"symbols\":[");
        for (i = 0; i < res->symbol_count; i++) {
            const DosSymbol *s = &res->symbols[i];
            if (i > 0) json_write_raw(w, ",");
            json_write_raw(w, "{\"name\":");
            json_write_string(w, s->name);
            json_write_raw(w, ",\"kind\":");
            json_write_int(w, s->kind);
            json_write_raw(w, s->global ? ",\"scope\":\"global\"" : ",\"scope\":\"file\"");
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
        json_write_raw(w, "]");
    }
    if (want & WANT_DIAGNOSTICS) {
        json_write_raw(w, ",\"diagnostics\":[");
        for (i = 0; i < res->diagnostic_count; i++) {
            const DosDiagnostic *d = &res->diagnostics[i];
            if (i > 0) json_write_raw(w, ",");
            json_write_raw(w, "{\"severity\":");
            json_write_int(w, d->severity);
            json_write_raw(w, ",\"message\":");
            json_write_string(w, d->message);
            json_write_raw(w, ",\"range\":");
            write_range(w, &d->range);
            json_write_raw(w, "}");
        }
        json_write_raw(w, "]");
    }
    if (want & WANT_REFERENCES) json_write_raw(w, ",\"references\":[]");
    if (want & WANT_TOKENS) {
        json_write_raw(w, ",\"tokens\":[");
        for (i = 0; i < res->token_count * 4; i++) {
            if (i > 0) json_write_raw(w, ",");
            json_write_int(w, res->tokens[i]);
        }
        json_write_raw(w, "]");
    }
    json_write_raw(w, "}");
}

/**
 * @brief Read a whole file.
 * @param path The file.
 * @param[out] length Its length.
 * @param[out] out_of_memory Set to non-zero if memory ran out.
 * @return Its contents (free() it), or NULL.
 */
static char *read_file(const char *path, size_t *length, int *out_of_memory)
{
    FILE *f = fopen(path, "rb");
    char *buf;
    long size;
    *out_of_memory = 0;
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0 || (size = ftell(f)) < 0 || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return NULL;
    }
    buf = (char *)malloc((size_t)size + 1);
    if (!buf) {
        fclose(f);
        *out_of_memory = 1;
        return NULL;
    }
    *length = fread(buf, 1, (size_t)size, f);
    if (*length != (size_t)size && ferror(f)) {
        free(buf);
        fclose(f);
        return NULL;
    }
    fclose(f);
    return buf;
}

/**
 * @brief Answer scout/analyze.
 * @param id The request's ID.
 * @param params Its parameters.
 * @param out The writer of the answer.
 * @return 1 if an answer was written, 0 if the analysis was stopped.
 */
static int analyze(long id, const JsonValue *params, JsonWriter *out)
{
    const char *path = json_obj_get_string(params, "path", NULL);
    const char *text = json_obj_get_string(params, "text", NULL);
    const char *language = json_obj_get_string(params, "language", NULL);
    const JsonValue *wanted = json_obj_get_array(params, "want");
    const char *name;
    int want = WANT_ALL, oom;
    size_t length = 0;
    char *source;
    DosParseResult *res;
    JsonWriter w;

    if (!path || !*path) {
        scout_rpc_format_error(out, id, SCOUT_RPC_ERR_INVALID_PARAMS, "missing path");
        return 1;
    }
    if (language && strcmp(language, "amigados") != 0) {
        scout_rpc_format_error(out, id, SCOUT_ERR_LANGUAGE, "language not handled by dos_parser");
        return 1;
    }
    if (wanted) {
        size_t i, n = json_array_length(wanted);
        want = 0;
        for (i = 0; i < n; i++) {
            const JsonValue *v = json_array_get(wanted, i);
            const char *part = v && v->type == JSON_STRING ? v->u.str_val : "";
            if (strcmp(part, "symbols") == 0) want |= WANT_SYMBOLS;
            else if (strcmp(part, "diagnostics") == 0) want |= WANT_DIAGNOSTICS;
            else if (strcmp(part, "references") == 0) want |= WANT_REFERENCES;
            else if (strcmp(part, "tokens") == 0) want |= WANT_TOKENS;
        }
    }

    name = text && *text ? text : path;
    source = read_file(name, &length, &oom);
    if (!source) {
        if (oom) {
            scout_rpc_format_error(out, id, SCOUT_ERR_MEMORY, "out of memory");
        } else {
            JsonWriter msg;
            json_writer_init_alloc(&msg, 128);
            json_write_raw(&msg, "cannot read ");
            json_write_raw(&msg, name);
            scout_rpc_format_error(out, id, SCOUT_ERR_FILE, json_writer_get(&msg));
            json_writer_free(&msg);
        }
        return 1;
    }
    res = dos_parser_analyze(source, length);
    free(source);
    if (res && res->stopped) {
        dos_parse_result_free(res);
        return 0;
    }
    if (!res || res->out_of_memory) {
        dos_parse_result_free(res);
        scout_rpc_format_error(out, id, SCOUT_ERR_MEMORY, "out of memory");
        return 1;
    }
    json_writer_init_alloc(&w, 4096);
    write_result(&w, res, want);
    dos_parse_result_free(res);
    if (w.error) {
        scout_rpc_format_error(out, id, SCOUT_ERR_MEMORY, "out of memory");
    } else if (!scout_rpc_format_response(out, id, json_writer_get(&w))) {
        out->length = 0;
        out->error = 0;
        if (out->buffer) out->buffer[0] = '\0';
        scout_rpc_format_error(out, id, SCOUT_ERR_MEMORY, "out of memory");
    }
    json_writer_free(&w);
    return 1;
}

int dos_parser_handle_request(const char *request, JsonWriter *response)
{
    ScoutRpcMessage req;
    int done = 1;

    if (!scout_rpc_parse(request, &req)) {
        scout_rpc_format_error(response, 0, SCOUT_RPC_ERR_PARSE_ERROR, "not valid JSON-RPC");
        scout_rpc_free(&req);
        return 1;
    }
    if (!req.is_request || !req.method) {
        scout_rpc_format_error(response, req.id, SCOUT_RPC_ERR_INVALID_REQUEST, "not a request");
    } else if (strcmp(req.method, "scout/describe") == 0) {
        JsonWriter w;
        json_writer_init_alloc(&w, 2048);
        dos_parser_describe(&w);
        scout_rpc_format_response(response, req.id, json_writer_get(&w));
        json_writer_free(&w);
    } else if (strcmp(req.method, "scout/analyze") == 0) {
        done = analyze(req.id, req.params, response);
    } else {
        scout_rpc_format_error(response, req.id, SCOUT_RPC_ERR_METHOD_NOT_FOUND, "unknown method");
    }
    scout_rpc_free(&req);
    return done;
}

#ifndef DOS_PARSER_NO_MAIN

#ifdef __amigaos__
#include <proto/exec.h>
#include <dos/dosextens.h>

/** libnix swaps to a stack of this size at the start (-Wl,-u,___stkinit). */
unsigned long __stack = 32768;

/**
 * @brief Check for Ctrl-C (dos_parser_should_stop()); the signal is cleared.
 * @return Non-zero if it came.
 */
static int ctrl_c(void)
{
    return (SetSignal(0, SIGBREAKF_CTRL_C) & SIGBREAKF_CTRL_C) != 0;
}
#endif

/**
 * @brief Read a line of any length from a file, without its line end.
 * @param f The file.
 * @param buf The buffer (grown as needed; free() it at the end).
 * @param cap Its size.
 * @return The line (in @p buf), or NULL at the end of the file or if out
 *         of memory.
 */
static char *read_line(FILE *f, char **buf, size_t *cap)
{
    size_t len = 0;
    if (!*buf) {
        *cap = 4096;
        *buf = (char *)malloc(*cap);
        if (!*buf) return NULL;
    }
    for (;;) {
        if (!fgets(*buf + len, (int)(*cap - len), f)) {
            if (len == 0) return NULL;
            break;
        }
        len += strlen(*buf + len);
        if (len > 0 && (*buf)[len - 1] == '\n') break;
        if (len + 1 >= *cap) {
            char *bigger = (char *)realloc(*buf, *cap * 2);
            if (!bigger) return NULL;
            *buf = bigger;
            *cap *= 2;
        }
    }
    while (len > 0 && ((*buf)[len - 1] == '\n' || (*buf)[len - 1] == '\r')) (*buf)[--len] = '\0';
    return *buf;
}

/**
 * @brief The parser program: answers each request line from STDIN with one
 *        line on STDOUT until the end of the input.
 * @return 0 at the end of the input, 20 if stopped by Ctrl-C or out of
 *         memory.
 */
int main(void)
{
    char *line = NULL;
    size_t cap = 0;
    int rc = 0;
    JsonWriter out;

#ifdef __amigaos__
    /* no requesters ("Please insert volume ...") for a missing file */
    ((struct Process *)FindTask(NULL))->pr_WindowPtr = (APTR)-1;
    dos_parser_should_stop = ctrl_c;
#endif
    json_writer_init_alloc(&out, 4096);
    while (read_line(stdin, &line, &cap)) {
        if (!line[0]) continue;
        out.length = 0;
        out.error = 0;
        if (out.buffer) out.buffer[0] = '\0';
        if (!dos_parser_handle_request(line, &out)) {
            rc = 20;
            break;
        }
        fputs(json_writer_get(&out), stdout);
        fputc('\n', stdout);
        fflush(stdout);
#ifdef __amigaos__
        if (ctrl_c()) {
            rc = 20;
            break;
        }
#endif
    }
    json_writer_free(&out);
    free(line);
    return rc;
}

#endif
