/**
 * @file c_parser.c
 * @brief The C parser of Scout: symbols and diagnostics of C sources, speaking the Scout protocol over STDIN/STDOUT.
 */
#include "c_parser.h"
#include "scout_rpc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/**
 * @brief Copy a string with malloc().
 * @param s The string (may be NULL).
 * @return The copy, or NULL.
 */
static char *c_strdup(const char *s)
{
    if (!s) return NULL;
    size_t len = strlen(s);
    char *copy = (char *)malloc(len + 1);
    if (copy) {
        memcpy(copy, s, len + 1);
    }
    return copy;
}

/**
 * @brief Turn a "file://" URI into a path (other strings are copied).
 * @param uri The URI or path.
 * @return The path (free() it), or NULL.
 */
static char *clean_uri_to_path(const char *uri)
{
    if (!uri) return NULL;
    if (strncmp(uri, "file://", 7) == 0) {
        return c_strdup(uri + 7);
    }
    return c_strdup(uri);
}

/**
 * @brief Append a symbol to a parse result.
 * @param res The result.
 * @param name Its name (copied).
 * @param detail Further information (copied, may be NULL).
 * @param kind SCOUT_SYMBOL_KIND_ value.
 * @param s_line First line, 1-based.
 * @param s_col First column, 1-based.
 * @param e_line Last line, 1-based.
 * @param e_col Column after the end, 1-based.
 */
static void add_symbol(CParseResult *res, const char *name, const char *detail,
                       int kind, long s_line, long s_col, long e_line, long e_col)
{
    if (!res || !name || !*name) return;

    CSymbol *sym = (CSymbol *)malloc(sizeof(CSymbol));
    if (!sym) return;

    sym->name = c_strdup(name);
    sym->detail = detail ? c_strdup(detail) : NULL;
    sym->kind = kind;
    sym->start_line = s_line;
    sym->start_col = s_col;
    sym->end_line = e_line >= s_line ? e_line : s_line;
    sym->end_col = e_col;
    sym->next = NULL;

    if (!res->symbols) {
        res->symbols = sym;
    } else {
        CSymbol *cur = res->symbols;
        while (cur->next) cur = cur->next;
        cur->next = sym;
    }
    res->symbol_count++;
}

/**
 * @brief Append a diagnostic to a parse result.
 * @param res The result.
 * @param msg The message (copied).
 * @param severity SCOUT_DIAGNOSTIC_ value.
 * @param s_line First line, 1-based.
 * @param s_col First column, 1-based.
 * @param e_line Last line, 1-based.
 * @param e_col Column after the end, 1-based.
 */
static void add_diagnostic(CParseResult *res, const char *msg, int severity,
                           long s_line, long s_col, long e_line, long e_col)
{
    if (!res || !msg) return;

    CDiagnostic *diag = (CDiagnostic *)malloc(sizeof(CDiagnostic));
    if (!diag) return;

    diag->message = c_strdup(msg);
    diag->severity = severity;
    diag->line = s_line;
    diag->col = s_col;
    diag->end_line = e_line >= s_line ? e_line : s_line;
    diag->end_col = e_col >= s_col ? e_col : s_col + 1;
    diag->next = NULL;

    if (!res->diagnostics) {
        res->diagnostics = diag;
    } else {
        CDiagnostic *cur = res->diagnostics;
        while (cur->next) cur = cur->next;
        cur->next = diag;
    }
    res->diagnostic_count++;
}

/**
 * The C keywords (and NULL, TRUE, FALSE) offered as completions; ends with NULL.
 */
static const char *C_KEYWORDS[] = {
    "auto", "break", "case", "char", "const", "continue", "default", "do",
    "double", "else", "enum", "extern", "float", "for", "goto", "if",
    "inline", "int", "long", "register", "restrict", "return", "short",
    "signed", "sizeof", "static", "struct", "switch", "typedef", "union",
    "unsigned", "void", "volatile", "while", "_Bool", "_Complex", "_Imaginary",
    "NULL", "TRUE", "FALSE", "true", "false",
    NULL
};

/**
 * @brief Tell whether a name is a C keyword (C_KEYWORDS).
 * @param name The name.
 * @return Non-zero for a keyword.
 */
static int is_c_keyword(const char *name)
{
    if (!name) return 0;
    for (size_t i = 0; C_KEYWORDS[i]; i++) {
        if (strcmp(C_KEYWORDS[i], name) == 0) return 1;
    }
    return 0;
}

/**
 * An open bracket on the stack used to find unbalanced brackets.
 */
typedef struct BracketFrame {
    char ch;        /**< the bracket: '(', '[' or '{' */
    long line;      /**< its line, 1-based */
    long col;       /**< its column, 1-based */
} BracketFrame;

CParseResult *c_parser_analyze(const char *source, const char *uri)
{
    if (!source) return NULL;

    CParseResult *res = (CParseResult *)calloc(1, sizeof(CParseResult));
    if (!res) return NULL;

    res->uri = c_strdup(uri ? uri : "file://unknown.c");
    res->filepath = clean_uri_to_path(res->uri);

    BracketFrame bracket_stack[512];
    size_t bracket_sp = 0;

    const char *p = source;
    long line = 1;
    long col = 1;

    char token[256];
    size_t tok_len = 0;

    char prev_token[256];
    prev_token[0] = '\0';

    char prev_prev_token[256];
    prev_prev_token[0] = '\0';

    int in_typedef = 0;
    int paren_depth = 0;
    int brace_depth = 0;

    while (*p) {
        /* Line and column tracking */
        if (*p == '\n') {
            line++;
            col = 1;
            p++;
            continue;
        }
        if (*p == '\r') {
            p++;
            if (*p == '\n') p++;
            line++;
            col = 1;
            continue;
        }

        /* Whitespace */
        if ((unsigned char)*p <= ' ' || *p == '\t') {
            col++;
            p++;
            continue;
        }

        /* Comments */
        if (*p == '/' && *(p + 1) == '/') {
            /* Line comment */
            p += 2;
            col += 2;
            while (*p && *p != '\n' && *p != '\r') {
                p++;
                col++;
            }
            continue;
        }

        if (*p == '/' && *(p + 1) == '*') {
            /* Block comment */
            long s_line = line;
            long s_col = col;
            p += 2;
            col += 2;
            int closed = 0;
            while (*p) {
                if (*p == '*' && *(p + 1) == '/') {
                    p += 2;
                    col += 2;
                    closed = 1;
                    break;
                }
                if (*p == '\n') {
                    line++;
                    col = 1;
                    p++;
                } else if (*p == '\r') {
                    p++;
                    if (*p == '\n') p++;
                    line++;
                    col = 1;
                } else {
                    p++;
                    col++;
                }
            }
            if (!closed) {
                add_diagnostic(res, "Unclosed block comment", SCOUT_DIAGNOSTIC_ERROR,
                               s_line, s_col, line, col);
            }
            continue;
        }

        /* Preprocessor Directives */
        if (*p == '#' && col == 1) {
            long s_line = line;
            p++;
            col++;
            while (*p && (*p == ' ' || *p == '\t')) { p++; col++; }

            char dir[64];
            size_t dlen = 0;
            while (*p && isalpha((unsigned char)*p) && dlen < sizeof(dir) - 1) {
                dir[dlen++] = *p++;
                col++;
            }
            dir[dlen] = '\0';

            if (strcmp(dir, "define") == 0) {
                while (*p && (*p == ' ' || *p == '\t')) { p++; col++; }
                char macro_name[128];
                size_t mlen = 0;
                long m_col = col;
                while (*p && (isalnum((unsigned char)*p) || *p == '_') && mlen < sizeof(macro_name) - 1) {
                    macro_name[mlen++] = *p++;
                    col++;
                }
                macro_name[mlen] = '\0';

                if (mlen > 0) {
                    add_symbol(res, macro_name, "macro", SCOUT_SYMBOL_KIND_CONSTANT,
                               s_line, m_col, s_line, col);
                }
            }

            /* Consume rest of preprocessor line */
            while (*p && *p != '\n' && *p != '\r') {
                if (*p == '\\') {
                    p++; col++;
                    if (*p == '\r') { p++; }
                    if (*p == '\n') { p++; line++; col = 1; }
                } else {
                    p++; col++;
                }
            }
            continue;
        }

        /* String literals */
        if (*p == '"') {
            long s_line = line;
            long s_col = col;
            p++; col++;
            int closed = 0;
            while (*p && *p != '\n' && *p != '\r') {
                if (*p == '"') {
                    p++; col++;
                    closed = 1;
                    break;
                }
                if (*p == '\\') {
                    p++; col++;
                    if (*p) { p++; col++; }
                } else {
                    p++; col++;
                }
            }
            if (!closed) {
                add_diagnostic(res, "Unterminated string literal", SCOUT_DIAGNOSTIC_ERROR,
                               s_line, s_col, line, col);
            }
            continue;
        }

        /* Char literals */
        if (*p == '\'') {
            long s_line = line;
            long s_col = col;
            p++; col++;
            int closed = 0;
            while (*p && *p != '\n' && *p != '\r') {
                if (*p == '\'') {
                    p++; col++;
                    closed = 1;
                    break;
                }
                if (*p == '\\') {
                    p++; col++;
                    if (*p) { p++; col++; }
                } else {
                    p++; col++;
                }
            }
            if (!closed) {
                add_diagnostic(res, "Unterminated character literal", SCOUT_DIAGNOSTIC_ERROR,
                               s_line, s_col, line, col);
            }
            continue;
        }

        /* Brackets tracking */
        if (*p == '{' || *p == '(' || *p == '[') {
            if (bracket_sp < sizeof(bracket_stack) / sizeof(bracket_stack[0])) {
                bracket_stack[bracket_sp].ch = *p;
                bracket_stack[bracket_sp].line = line;
                bracket_stack[bracket_sp].col = col;
                bracket_sp++;
            }
            if (*p == '{') brace_depth++;
            if (*p == '(') paren_depth++;
            p++; col++;
            continue;
        }

        if (*p == '}' || *p == ')' || *p == ']') {
            char expected = (*p == '}') ? '{' : (*p == ')') ? '(' : '[';
            if (bracket_sp > 0 && bracket_stack[bracket_sp - 1].ch == expected) {
                bracket_sp--;
            } else {
                char err_buf[64];
                snprintf(err_buf, sizeof(err_buf), "Mismatched bracket '%c'", *p);
                add_diagnostic(res, err_buf, SCOUT_DIAGNOSTIC_ERROR, line, col, line, col + 1);
            }
            if (*p == '}' && brace_depth > 0) brace_depth--;
            if (*p == ')' && paren_depth > 0) paren_depth--;
            p++; col++;
            continue;
        }

        /* Identifiers & Keywords */
        if (isalpha((unsigned char)*p) || *p == '_') {
            long tok_s_line = line;
            long tok_s_col = col;
            tok_len = 0;
            while (*p && (isalnum((unsigned char)*p) || *p == '_')) {
                if (tok_len < sizeof(token) - 1) {
                    token[tok_len++] = *p;
                }
                p++; col++;
            }
            token[tok_len] = '\0';

            /* Skip whitespace after token */
            const char *look = p;
            while (*look && ((unsigned char)*look <= ' ' || *look == '\t' || *look == '\r' || *look == '\n')) {
                look++;
            }

            /* Struct / Union / Enum tag */
            if (strcmp(prev_token, "struct") == 0 || strcmp(prev_token, "union") == 0 || strcmp(prev_token, "enum") == 0) {
                if (!is_c_keyword(token) && (!in_typedef || *look == '{')) {
                    int kind = (strcmp(prev_token, "enum") == 0) ? SCOUT_SYMBOL_KIND_ENUM : SCOUT_SYMBOL_KIND_STRUCT;
                    add_symbol(res, token, prev_token, kind,
                               tok_s_line, tok_s_col, tok_s_line, tok_s_col + (long)tok_len);
                }
            }
            /* Function declaration or definition */
            else if (*look == '(' && brace_depth == 0) {
                if (!is_c_keyword(token)) {
                    /* Find matching closing parenthesis */
                    const char *p_scan = look + 1;
                    int depth = 1;
                    long f_end_line = line;
                    while (*p_scan && depth > 0) {
                        if (*p_scan == '\n') f_end_line++;
                        if (*p_scan == '(') depth++;
                        else if (*p_scan == ')') depth--;
                        p_scan++;
                    }
                    /* Check if function has a body { ... } */
                    while (*p_scan && ((unsigned char)*p_scan <= ' ' || *p_scan == '\t' || *p_scan == '\r' || *p_scan == '\n')) {
                        if (*p_scan == '\n') f_end_line++;
                        p_scan++;
                    }
                    if (*p_scan == '{') {
                        depth = 1;
                        p_scan++;
                        while (*p_scan && depth > 0) {
                            if (*p_scan == '\n') f_end_line++;
                            if (*p_scan == '{') depth++;
                            else if (*p_scan == '}') depth--;
                            p_scan++;
                        }
                    }
                    add_symbol(res, token, "function", SCOUT_SYMBOL_KIND_FUNCTION,
                               tok_s_line, tok_s_col, f_end_line, col);
                }
            }
            /* Typedef alias */
            else if (in_typedef && (*look == ';' || *look == ',')) {
                if (!is_c_keyword(token)) {
                    add_symbol(res, token, "typedef", SCOUT_SYMBOL_KIND_STRUCT,
                               tok_s_line, tok_s_col, tok_s_line, tok_s_col + (long)tok_len);
                }
            }

            if (strcmp(token, "typedef") == 0) {
                in_typedef = 1;
            }

            memcpy(prev_prev_token, prev_token, sizeof(prev_token));
            memcpy(prev_token, token, sizeof(token));
            prev_prev_token[sizeof(prev_prev_token) - 1] = '\0';
            prev_token[sizeof(prev_token) - 1] = '\0';
            continue;
        }

        /* Statement separators */
        if (*p == ';') {
            in_typedef = 0;
            prev_token[0] = '\0';
            prev_prev_token[0] = '\0';
        }

        p++;
        col++;
    }

    /* Check for unclosed brackets remaining on stack */
    while (bracket_sp > 0) {
        bracket_sp--;
        char err_buf[64];
        snprintf(err_buf, sizeof(err_buf), "Unclosed bracket '%c'", bracket_stack[bracket_sp].ch);
        add_diagnostic(res, err_buf, SCOUT_DIAGNOSTIC_ERROR,
                       bracket_stack[bracket_sp].line, bracket_stack[bracket_sp].col,
                       line, col);
    }

    return res;
}

CParseResult *c_parser_analyze_file(const char *filepath)
{
    if (!filepath) return NULL;
    FILE *f = fopen(filepath, "rb");
    if (!f) return NULL;

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (sz < 0) {
        fclose(f);
        return NULL;
    }

    char *buf = (char *)malloc((size_t)sz + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }

    size_t read_bytes = fread(buf, 1, (size_t)sz, f);
    buf[read_bytes] = '\0';
    fclose(f);

    char uri_buf[1024];
    snprintf(uri_buf, sizeof(uri_buf), "file://%s", filepath);

    CParseResult *res = c_parser_analyze(buf, uri_buf);
    free(buf);
    return res;
}

void c_parse_result_free(CParseResult *res)
{
    if (!res) return;

    if (res->uri) free(res->uri);
    if (res->filepath) free(res->filepath);

    CSymbol *sym = res->symbols;
    while (sym) {
        CSymbol *next = sym->next;
        if (sym->name) free(sym->name);
        if (sym->detail) free(sym->detail);
        free(sym);
        sym = next;
    }

    CDiagnostic *diag = res->diagnostics;
    while (diag) {
        CDiagnostic *next = diag->next;
        if (diag->message) free(diag->message);
        free(diag);
        diag = next;
    }

    free(res);
}

int c_parser_find_definition(const CParseResult *res, const char *symbol_name,
                             long line, long col, const CSymbol **out_sym)
{
    (void)line;
    (void)col;
    if (!res || !out_sym) return 0;
    *out_sym = NULL;

    if (!symbol_name || !*symbol_name) return 0;

    for (const CSymbol *s = res->symbols; s; s = s->next) {
        if (s->name && strcmp(s->name, symbol_name) == 0) {
            *out_sym = s;
            return 1;
        }
    }
    return 0;
}

/**
 * @brief Tell whether a string starts with a prefix, ignoring case.
 * @param str The string.
 * @param pfx The prefix (NULL or empty: always).
 * @return Non-zero if it does.
 */
static int str_case_prefix(const char *str, const char *pfx)
{
    if (!pfx || !*pfx) return 1;
    if (!str) return 0;
    while (*pfx) {
        if (tolower((unsigned char)*str) != tolower((unsigned char)*pfx)) {
            return 0;
        }
        str++;
        pfx++;
    }
    return 1;
}

/**
 * @brief Append a completion candidate unless its label is there already.
 * @param head The list.
 * @param count The number of candidates, counted up.
 * @param label The text to insert (copied).
 * @param detail Further information (copied, may be NULL).
 * @param kind SCOUT_COMPLETION_KIND_ value.
 */
static void add_completion(CCompletion **head, size_t *count,
                           const char *label, const char *detail, int kind)
{
    if (!head || !label) return;

    /* Check duplicate */
    for (CCompletion *c = *head; c; c = c->next) {
        if (c->label && strcmp(c->label, label) == 0) {
            return;
        }
    }

    CCompletion *item = (CCompletion *)malloc(sizeof(CCompletion));
    if (!item) return;

    item->label = c_strdup(label);
    item->detail = detail ? c_strdup(detail) : NULL;
    item->kind = kind;
    item->next = *head;
    *head = item;
    if (count) (*count)++;
}

size_t c_parser_get_completions(const CParseResult *res, const char *prefix,
                                CCompletion **out_list)
{
    if (!out_list) return 0;
    *out_list = NULL;
    size_t count = 0;

    /* Add matching C keywords */
    for (size_t i = 0; C_KEYWORDS[i]; i++) {
        if (str_case_prefix(C_KEYWORDS[i], prefix)) {
            add_completion(out_list, &count, C_KEYWORDS[i], "keyword", SCOUT_COMPLETION_KIND_KEYWORD);
        }
    }

    /* Add symbols from parse result */
    if (res) {
        for (const CSymbol *s = res->symbols; s; s = s->next) {
            if (s->name && str_case_prefix(s->name, prefix)) {
                int c_kind = SCOUT_COMPLETION_KIND_VARIABLE;
                if (s->kind == SCOUT_SYMBOL_KIND_FUNCTION) c_kind = SCOUT_COMPLETION_KIND_FUNCTION;
                else if (s->kind == SCOUT_SYMBOL_KIND_STRUCT) c_kind = SCOUT_COMPLETION_KIND_STRUCT;
                else if (s->kind == SCOUT_SYMBOL_KIND_ENUM) c_kind = SCOUT_COMPLETION_KIND_ENUM;
                else if (s->kind == SCOUT_SYMBOL_KIND_CONSTANT) c_kind = SCOUT_COMPLETION_KIND_VALUE;

                add_completion(out_list, &count, s->name, s->detail, c_kind);
            }
        }
    }

    return count;
}

void c_completions_free(CCompletion *list)
{
    while (list) {
        CCompletion *next = list->next;
        if (list->label) free(list->label);
        if (list->detail) free(list->detail);
        free(list);
        list = next;
    }
}

/* JSON-RPC Request Handler */

int c_parser_process_jsonrpc(const char *request_json, JsonWriter *response_writer)
{
    if (!request_json || !response_writer) return 0;

    ScoutRpcMessage req;
    if (!scout_rpc_parse(request_json, &req) || !req.is_request) {
        scout_rpc_format_error(response_writer, 0, SCOUT_RPC_ERR_PARSE_ERROR, "Parse error");
        scout_rpc_free(&req);
        return 1;
    }

    long id = req.id;
    const char *method = req.method;

    if (!method) {
        scout_rpc_format_error(response_writer, id, SCOUT_RPC_ERR_INVALID_REQUEST, "Missing method");
        scout_rpc_free(&req);
        return 1;
    }

    const JsonValue *params = req.params;
    const JsonValue *text_doc = json_obj_get_obj(params, "textDocument");
    const char *uri = text_doc ? json_obj_get_string(text_doc, "uri", NULL) : NULL;
    const char *text = text_doc ? json_obj_get_string(text_doc, "text", NULL) : NULL;

    CParseResult *res = NULL;
    if (text) {
        res = c_parser_analyze(text, uri);
    } else if (uri) {
        char *path = clean_uri_to_path(uri);
        res = c_parser_analyze_file(path);
        free(path);
    }

    if (strcmp(method, "textDocument/documentSymbol") == 0) {
        JsonWriter sym_writer;
        json_writer_init_alloc(&sym_writer, 256);
        json_write_arr_start(&sym_writer);

        if (res && res->symbols) {
            int first = 1;
            for (const CSymbol *s = res->symbols; s; s = s->next) {
                if (!first) json_write_raw(&sym_writer, ",");
                first = 0;

                json_write_obj_start(&sym_writer);
                json_write_obj_key(&sym_writer, "name");
                json_write_string(&sym_writer, s->name);
                json_write_raw(&sym_writer, ",");
                json_write_obj_key(&sym_writer, "kind");
                json_write_int(&sym_writer, s->kind);
                json_write_raw(&sym_writer, ",");

                if (s->detail) {
                    json_write_obj_key(&sym_writer, "detail");
                    json_write_string(&sym_writer, s->detail);
                    json_write_raw(&sym_writer, ",");
                }

                json_write_obj_key(&sym_writer, "range");
                json_write_obj_start(&sym_writer);
                json_write_obj_key(&sym_writer, "start");
                json_write_obj_start(&sym_writer);
                json_write_obj_key(&sym_writer, "line");
                json_write_int(&sym_writer, s->start_line - 1);
                json_write_raw(&sym_writer, ",");
                json_write_obj_key(&sym_writer, "character");
                json_write_int(&sym_writer, s->start_col - 1);
                json_write_obj_end(&sym_writer);
                json_write_raw(&sym_writer, ",");

                json_write_obj_key(&sym_writer, "end");
                json_write_obj_start(&sym_writer);
                json_write_obj_key(&sym_writer, "line");
                json_write_int(&sym_writer, s->end_line - 1);
                json_write_raw(&sym_writer, ",");
                json_write_obj_key(&sym_writer, "character");
                json_write_int(&sym_writer, s->end_col - 1);
                json_write_obj_end(&sym_writer);
                json_write_obj_end(&sym_writer);

                json_write_obj_end(&sym_writer);
            }
        }
        json_write_arr_end(&sym_writer);

        scout_rpc_format_response(response_writer, id, json_writer_get(&sym_writer));
        json_writer_free(&sym_writer);
    }
    else if (strcmp(method, "textDocument/publishDiagnostics") == 0 ||
             strcmp(method, "textDocument/diagnostics") == 0) {
        JsonWriter diag_writer;
        json_writer_init_alloc(&diag_writer, 256);
        json_write_arr_start(&diag_writer);

        if (res && res->diagnostics) {
            int first = 1;
            for (const CDiagnostic *d = res->diagnostics; d; d = d->next) {
                if (!first) json_write_raw(&diag_writer, ",");
                first = 0;

                json_write_obj_start(&diag_writer);
                json_write_obj_key(&diag_writer, "message");
                json_write_string(&diag_writer, d->message);
                json_write_raw(&diag_writer, ",");
                json_write_obj_key(&diag_writer, "severity");
                json_write_int(&diag_writer, d->severity);
                json_write_raw(&diag_writer, ",");

                json_write_obj_key(&diag_writer, "range");
                json_write_obj_start(&diag_writer);
                json_write_obj_key(&diag_writer, "start");
                json_write_obj_start(&diag_writer);
                json_write_obj_key(&diag_writer, "line");
                json_write_int(&diag_writer, d->line - 1);
                json_write_raw(&diag_writer, ",");
                json_write_obj_key(&diag_writer, "character");
                json_write_int(&diag_writer, d->col - 1);
                json_write_obj_end(&diag_writer);
                json_write_raw(&diag_writer, ",");

                json_write_obj_key(&diag_writer, "end");
                json_write_obj_start(&diag_writer);
                json_write_obj_key(&diag_writer, "line");
                json_write_int(&diag_writer, d->end_line - 1);
                json_write_raw(&diag_writer, ",");
                json_write_obj_key(&diag_writer, "character");
                json_write_int(&diag_writer, d->end_col - 1);
                json_write_obj_end(&diag_writer);
                json_write_obj_end(&diag_writer);

                json_write_obj_end(&diag_writer);
            }
        }
        json_write_arr_end(&diag_writer);

        scout_rpc_format_response(response_writer, id, json_writer_get(&diag_writer));
        json_writer_free(&diag_writer);
    }
    else if (strcmp(method, "textDocument/definition") == 0) {
        const JsonValue *pos = json_obj_get_obj(params, "position");
        long line = pos ? json_obj_get_int(pos, "line", 0) + 1 : 1;
        long col = pos ? json_obj_get_int(pos, "character", 0) + 1 : 1;
        const char *sym_name = json_obj_get_string(params, "symbol", NULL);

        const CSymbol *target = NULL;
        if (res) {
            c_parser_find_definition(res, sym_name, line, col, &target);
        }

        if (target) {
            JsonWriter loc_writer;
            json_writer_init_alloc(&loc_writer, 128);
            json_write_obj_start(&loc_writer);
            json_write_obj_key(&loc_writer, "uri");
            json_write_string(&loc_writer, uri ? uri : "");
            json_write_raw(&loc_writer, ",");

            json_write_obj_key(&loc_writer, "range");
            json_write_obj_start(&loc_writer);
            json_write_obj_key(&loc_writer, "start");
            json_write_obj_start(&loc_writer);
            json_write_obj_key(&loc_writer, "line");
            json_write_int(&loc_writer, target->start_line - 1);
            json_write_raw(&loc_writer, ",");
            json_write_obj_key(&loc_writer, "character");
            json_write_int(&loc_writer, target->start_col - 1);
            json_write_obj_end(&loc_writer);
            json_write_raw(&loc_writer, ",");

            json_write_obj_key(&loc_writer, "end");
            json_write_obj_start(&loc_writer);
            json_write_obj_key(&loc_writer, "line");
            json_write_int(&loc_writer, target->end_line - 1);
            json_write_raw(&loc_writer, ",");
            json_write_obj_key(&loc_writer, "character");
            json_write_int(&loc_writer, target->end_col - 1);
            json_write_obj_end(&loc_writer);
            json_write_obj_end(&loc_writer);

            json_write_obj_end(&loc_writer);
            scout_rpc_format_response(response_writer, id, json_writer_get(&loc_writer));
            json_writer_free(&loc_writer);
        } else {
            scout_rpc_format_response(response_writer, id, "null");
        }
    }
    else if (strcmp(method, "textDocument/completion") == 0) {
        const JsonValue *ctx = json_obj_get_obj(params, "context");
        const char *pfx = ctx ? json_obj_get_string(ctx, "prefix", "") : "";

        CCompletion *comp_list = NULL;
        c_parser_get_completions(res, pfx, &comp_list);

        JsonWriter comp_writer;
        json_writer_init_alloc(&comp_writer, 256);
        json_write_arr_start(&comp_writer);

        int first = 1;
        for (CCompletion *c = comp_list; c; c = c->next) {
            if (!first) json_write_raw(&comp_writer, ",");
            first = 0;

            json_write_obj_start(&comp_writer);
            json_write_obj_key(&comp_writer, "label");
            json_write_string(&comp_writer, c->label);
            json_write_raw(&comp_writer, ",");
            json_write_obj_key(&comp_writer, "kind");
            json_write_int(&comp_writer, c->kind);
            if (c->detail) {
                json_write_raw(&comp_writer, ",");
                json_write_obj_key(&comp_writer, "detail");
                json_write_string(&comp_writer, c->detail);
            }
            json_write_obj_end(&comp_writer);
        }
        json_write_arr_end(&comp_writer);

        scout_rpc_format_response(response_writer, id, json_writer_get(&comp_writer));
        json_writer_free(&comp_writer);
        c_completions_free(comp_list);
    }
    else {
        scout_rpc_format_error(response_writer, id, SCOUT_RPC_ERR_METHOD_NOT_FOUND, "Method not supported");
    }

    if (res) c_parse_result_free(res);
    scout_rpc_free(&req);
    return 1;
}

#ifndef C_PARSER_NO_MAIN
/**
 * @brief The parser program: reads one JSON-RPC request per line from
 *        STDIN and writes the response as one line to STDOUT.
 * @return 0 at the end of the input.
 */
int main(void)
{
    char line_buf[4096];
    JsonWriter resp_writer;
    json_writer_init_alloc(&resp_writer, 512);

    while (fgets(line_buf, sizeof(line_buf), stdin)) {
        /* Strip trailing newlines */
        size_t l = strlen(line_buf);
        while (l > 0 && (line_buf[l - 1] == '\r' || line_buf[l - 1] == '\n')) {
            line_buf[--l] = '\0';
        }
        if (l == 0) continue;

        resp_writer.length = 0;
        if (resp_writer.buffer) resp_writer.buffer[0] = '\0';
        resp_writer.error = 0;

        c_parser_process_jsonrpc(line_buf, &resp_writer);

        printf("%s\n", json_writer_get(&resp_writer));
        fflush(stdout);
    }

    json_writer_free(&resp_writer);
    return 0;
}
#endif
