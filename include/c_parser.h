/**
 * @file c_parser.h
 * @brief Interface of the C parser of Scout (symbols, diagnostics, the Scout protocol).
 */
#ifndef TINYDEV_C_PARSER_H
#define TINYDEV_C_PARSER_H

#include <stddef.h>
#include "json.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @name Symbol kinds
 * The same numbers as the LSP SymbolKind.
 */
/** @{ */
#define SCOUT_SYMBOL_KIND_FILE        1     /**< a file */
#define SCOUT_SYMBOL_KIND_MODULE      2     /**< a module */
#define SCOUT_SYMBOL_KIND_NAMESPACE   3     /**< a namespace */
#define SCOUT_SYMBOL_KIND_PACKAGE     4     /**< a package */
#define SCOUT_SYMBOL_KIND_CLASS       5     /**< a class */
#define SCOUT_SYMBOL_KIND_METHOD      6     /**< a method */
#define SCOUT_SYMBOL_KIND_PROPERTY    7     /**< a property */
#define SCOUT_SYMBOL_KIND_FIELD       8     /**< a field of a struct or union */
#define SCOUT_SYMBOL_KIND_CONSTRUCTOR 9     /**< a constructor */
#define SCOUT_SYMBOL_KIND_ENUM        10    /**< an enumeration */
#define SCOUT_SYMBOL_KIND_INTERFACE   11    /**< an interface */
#define SCOUT_SYMBOL_KIND_FUNCTION    12    /**< a function */
#define SCOUT_SYMBOL_KIND_VARIABLE    13    /**< a variable */
#define SCOUT_SYMBOL_KIND_CONSTANT    14    /**< a constant (macro, enumerator) */
#define SCOUT_SYMBOL_KIND_STRUCT      23    /**< a struct or union */
/** @} */

/**
 * @name Diagnostic severities
 * The same numbers as the LSP DiagnosticSeverity.
 */
/** @{ */
#define SCOUT_DIAGNOSTIC_ERROR        1     /**< an error */
#define SCOUT_DIAGNOSTIC_WARNING      2     /**< a warning */
#define SCOUT_DIAGNOSTIC_INFO         3     /**< information */
#define SCOUT_DIAGNOSTIC_HINT         4     /**< a hint */
/** @} */

/**
 * @name Completion item kinds
 * The same numbers as the LSP CompletionItemKind.
 */
/** @{ */
#define SCOUT_COMPLETION_KIND_TEXT        1     /**< plain text */
#define SCOUT_COMPLETION_KIND_METHOD      2     /**< a method */
#define SCOUT_COMPLETION_KIND_FUNCTION    3     /**< a function */
#define SCOUT_COMPLETION_KIND_CONSTRUCTOR 4     /**< a constructor */
#define SCOUT_COMPLETION_KIND_FIELD       5     /**< a field */
#define SCOUT_COMPLETION_KIND_VARIABLE    6     /**< a variable */
#define SCOUT_COMPLETION_KIND_CLASS       7     /**< a class */
#define SCOUT_COMPLETION_KIND_INTERFACE   8     /**< an interface */
#define SCOUT_COMPLETION_KIND_MODULE      9     /**< a module */
#define SCOUT_COMPLETION_KIND_PROPERTY    10    /**< a property */
#define SCOUT_COMPLETION_KIND_UNIT        11    /**< a unit */
#define SCOUT_COMPLETION_KIND_VALUE       12    /**< a value */
#define SCOUT_COMPLETION_KIND_ENUM        13    /**< an enumeration */
#define SCOUT_COMPLETION_KIND_KEYWORD     14    /**< a keyword of the language */
#define SCOUT_COMPLETION_KIND_SNIPPET     15    /**< a snippet */
#define SCOUT_COMPLETION_KIND_STRUCT      22    /**< a struct or union */
/** @} */

/** A symbol found in a C source (a node of a singly linked list). */
typedef struct CSymbol {
    char *name;         /**< its name (allocated) */
    char *detail;       /**< further information, e.g. the declaration (allocated, may be NULL) */
    int kind;           /**< SCOUT_SYMBOL_KIND_ value */
    long start_line;    /**< first line, 1-based */
    long start_col;     /**< first column, 1-based */
    long end_line;      /**< last line, 1-based */
    long end_col;       /**< column after the end, 1-based */
    struct CSymbol *next;   /**< the next symbol, NULL at the end */
} CSymbol;

/** A problem found in a C source (a node of a singly linked list). */
typedef struct CDiagnostic {
    char *message;      /**< the description (allocated) */
    int severity;       /**< SCOUT_DIAGNOSTIC_ value */
    long line;          /**< first line, 1-based */
    long col;           /**< first column, 1-based */
    long end_line;      /**< last line, 1-based */
    long end_col;       /**< column after the end, 1-based */
    struct CDiagnostic *next;   /**< the next diagnostic, NULL at the end */
} CDiagnostic;

/** A completion candidate (a node of a singly linked list). */
typedef struct CCompletion {
    char *label;        /**< the text to insert (allocated) */
    char *detail;       /**< further information (allocated, may be NULL) */
    int kind;           /**< SCOUT_COMPLETION_KIND_ value */
    struct CCompletion *next;   /**< the next candidate, NULL at the end */
} CCompletion;

/** The result of parsing a C source: its symbols and diagnostics. */
typedef struct CParseResult {
    char *uri;          /**< URI or path the source was given (allocated) */
    char *filepath;     /**< path of the file, if it was read from one (allocated, may be NULL) */
    CSymbol *symbols;   /**< the symbols, in the order found */
    size_t symbol_count;    /**< number of symbols */
    CDiagnostic *diagnostics;   /**< the diagnostics */
    size_t diagnostic_count;    /**< number of diagnostics */
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
 * @param res The result of c_parser_analyze() or c_parser_analyze_file()
 *            (may be NULL).
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
 * @brief Free a list of completion candidates.
 * @param list The list of c_parser_get_completions() (may be NULL).
 */
void c_completions_free(CCompletion *list);

/**
 * @brief Process a single JSON-RPC 2.0 request string and produce response string.
 *
 * @param request_json Single-line or multi-line JSON-RPC request.
 * @param response_writer Writer to receive formatted single-line JSON-RPC response.
 * @return 1 on success (even if a Scout error response produced), 0 on fatal writer error.
 */
int c_parser_process_jsonrpc(const char *request_json, JsonWriter *response_writer);

#ifdef __cplusplus
}
#endif

#endif /* TINYDEV_C_PARSER_H */
