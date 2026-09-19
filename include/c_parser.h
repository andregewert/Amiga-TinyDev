#ifndef TINYDEV_C_PARSER_H
#define TINYDEV_C_PARSER_H

#include <stddef.h>
#include "lsp_json.h"

#ifdef __cplusplus
extern "C" {
#endif

/* LSP Symbol Kinds */
#define LSP_SYMBOL_KIND_FILE        1
#define LSP_SYMBOL_KIND_MODULE      2
#define LSP_SYMBOL_KIND_NAMESPACE   3
#define LSP_SYMBOL_KIND_PACKAGE     4
#define LSP_SYMBOL_KIND_CLASS       5
#define LSP_SYMBOL_KIND_METHOD      6
#define LSP_SYMBOL_KIND_PROPERTY    7
#define LSP_SYMBOL_KIND_FIELD       8
#define LSP_SYMBOL_KIND_CONSTRUCTOR 9
#define LSP_SYMBOL_KIND_ENUM        10
#define LSP_SYMBOL_KIND_INTERFACE   11
#define LSP_SYMBOL_KIND_FUNCTION    12
#define LSP_SYMBOL_KIND_VARIABLE    13
#define LSP_SYMBOL_KIND_CONSTANT    14
#define LSP_SYMBOL_KIND_STRUCT      23

/* LSP Diagnostic Severities */
#define LSP_DIAGNOSTIC_ERROR        1
#define LSP_DIAGNOSTIC_WARNING      2
#define LSP_DIAGNOSTIC_INFO         3
#define LSP_DIAGNOSTIC_HINT         4

/* LSP Completion Item Kinds */
#define LSP_COMPLETION_KIND_TEXT        1
#define LSP_COMPLETION_KIND_METHOD      2
#define LSP_COMPLETION_KIND_FUNCTION    3
#define LSP_COMPLETION_KIND_CONSTRUCTOR 4
#define LSP_COMPLETION_KIND_FIELD       5
#define LSP_COMPLETION_KIND_VARIABLE    6
#define LSP_COMPLETION_KIND_CLASS       7
#define LSP_COMPLETION_KIND_INTERFACE   8
#define LSP_COMPLETION_KIND_MODULE      9
#define LSP_COMPLETION_KIND_PROPERTY    10
#define LSP_COMPLETION_KIND_UNIT        11
#define LSP_COMPLETION_KIND_VALUE       12
#define LSP_COMPLETION_KIND_ENUM        13
#define LSP_COMPLETION_KIND_KEYWORD     14
#define LSP_COMPLETION_KIND_SNIPPET     15
#define LSP_COMPLETION_KIND_STRUCT      22

typedef struct CSymbol {
    char *name;
    char *detail;
    int kind;
    long start_line;    /* 1-based */
    long start_col;     /* 1-based */
    long end_line;      /* 1-based */
    long end_col;       /* 1-based */
    struct CSymbol *next;
} CSymbol;

typedef struct CDiagnostic {
    char *message;
    int severity;
    long line;          /* 1-based */
    long col;           /* 1-based */
    long end_line;      /* 1-based */
    long end_col;       /* 1-based */
    struct CDiagnostic *next;
} CDiagnostic;

typedef struct CCompletion {
    char *label;
    char *detail;
    int kind;
    struct CCompletion *next;
} CCompletion;

typedef struct CParseResult {
    char *uri;
    char *filepath;
    CSymbol *symbols;
    size_t symbol_count;
    CDiagnostic *diagnostics;
    size_t diagnostic_count;
} CParseResult;

/**
 * @brief Parse C source text and extract symbols and diagnostics.
 *
 * @param source NUL-terminated C source code.
 * @param uri File URI or path (copied).
 * @return Allocated parse result; free with c_parse_result_free().
 */
CParseResult *c_parser_analyze(const char *source, const char *uri);

/**
 * @brief Parse a C source file from disk.
 *
 * @param filepath Path to C source file.
 * @return Allocated parse result, or NULL if file cannot be read.
 */
CParseResult *c_parser_analyze_file(const char *filepath);

/**
 * @brief Free all memory associated with a parse result.
 */
void c_parse_result_free(CParseResult *res);

/**
 * @brief Find the definition location for a symbol name or word at coordinates.
 *
 * @param res Parsed C file result.
 * @param symbol_name Identifier to find, or NULL.
 * @param line 1-based line number (if symbol_name is NULL).
 * @param col 1-based column number (if symbol_name is NULL).
 * @param out_sym Output pointer to matched symbol (borrowed reference).
 * @return 1 if found, 0 otherwise.
 */
int c_parser_find_definition(const CParseResult *res, const char *symbol_name,
                             long line, long col, const CSymbol **out_sym);

/**
 * @brief Get code completions matching a prefix.
 *
 * @param res Parsed C file result (optional, can be NULL for keywords only).
 * @param prefix Prefix string to match candidates against (case-sensitive or insensitive).
 * @param out_list Pointer to head of allocated completion list.
 * @return Number of completion candidates generated.
 */
size_t c_parser_get_completions(const CParseResult *res, const char *prefix,
                                CCompletion **out_list);

/**
 * @brief Free completion candidate list.
 */
void c_completions_free(CCompletion *list);

/**
 * @brief Process a single JSON-RPC 2.0 request string and produce response string.
 *
 * @param request_json Single-line or multi-line JSON-RPC request.
 * @param response_writer Writer to receive formatted single-line JSON-RPC response.
 * @return 1 on success (even if LSP error response produced), 0 on fatal writer error.
 */
int c_parser_process_jsonrpc(const char *request_json, LspJsonWriter *response_writer);

#ifdef __cplusplus
}
#endif

#endif /* TINYDEV_C_PARSER_H */
