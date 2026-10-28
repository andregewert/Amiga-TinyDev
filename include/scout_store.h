/**
 * @file scout_store.h
 * @brief The portable core of Scout: the parsers and languages it knows,
 *        the sessions with the results of their files, and the answers to
 *        the queries (symbols, diagnostics, tokens, definition,
 *        completion) as JSON.
 *
 * No Amiga headers and no file access, so the host tests can use it; the
 * commodity (scout_rexx.c, scout_jobs.c) runs the parsers and hands their
 * answers to it. The ARexx commands and their results are described in
 * src/scout/docs/Scout.md.
 */
#ifndef TINYDEV_SCOUT_STORE_H
#define TINYDEV_SCOUT_STORE_H

#include <stddef.h>
#include "json.h"

/** A range in a file (1-based lines and byte columns, end exclusive). */
typedef struct ScoutRange {
    long start_line;    /**< first line */
    long start_col;     /**< first column */
    long end_line;      /**< line of the end */
    long end_col;       /**< column after the last character */
} ScoutRange;

/** Visibility of a symbol ("scope" in the protocol). */
typedef enum ScoutScope {
    SCOUT_SCOPE_GLOBAL,     /**< visible to other files */
    SCOUT_SCOPE_FILE,       /**< only in its file */
    SCOUT_SCOPE_LOCAL       /**< inside another symbol */
} ScoutScope;

/** A symbol of a file, as its parser reported it. */
typedef struct ScoutSymbol {
    char *name;             /**< its name (allocated) */
    char *detail;           /**< its declaration as text (allocated, may be NULL) */
    int kind;               /**< SCOUT_SYMBOL_ value */
    ScoutScope scope;       /**< its visibility */
    int declaration;        /**< only declared there */
    long parent;            /**< index of the symbol it belongs to, -1 */
    ScoutRange range;       /**< the whole definition */
    ScoutRange selection;   /**< its name */
} ScoutSymbol;

/** A diagnostic of a file. */
typedef struct ScoutDiagnostic {
    int severity;           /**< SCOUT_SEVERITY_ value */
    char *message;          /**< its text (allocated) */
    char *code;             /**< its code (allocated, may be NULL) */
    ScoutRange range;       /**< where it is */
} ScoutDiagnostic;

/** A reference of a file to another one (an include). */
typedef struct ScoutReference {
    char *kind;             /**< "include" or "import" (allocated) */
    char *target;           /**< the name as written (allocated) */
    int system;             /**< looked up in the include paths only */
    ScoutRange range;       /**< where it is */
} ScoutReference;

/** The state of a file on disk when it was analysed. */
typedef struct ScoutStamp {
    long days;              /**< date: days */
    long minute;            /**< date: minutes */
    long tick;              /**< date: ticks */
    long size;              /**< size in bytes */
} ScoutStamp;

/** A file of a session and the result of its last analysis. */
typedef struct ScoutFile {
    char *path;             /**< its path, the key (allocated) */
    char *language;         /**< language of the result (allocated, NULL before) */
    int valid;              /**< there is a result */
    int in_project;         /**< below the project drawer (or analysed for the client), not only included */
    long version;           /**< the client's version of a snapshot, -1 for the file on disk */
    ScoutStamp stamp;       /**< the file on disk at the analysis (version -1) */
    ScoutSymbol *symbols;           /**< its symbols */
    long symbol_count;              /**< number of symbols */
    ScoutDiagnostic *diagnostics;   /**< its diagnostics */
    long diagnostic_count;          /**< number of diagnostics */
    ScoutReference *references;     /**< its references */
    long reference_count;           /**< number of references */
    char *tokens;           /**< the tokens' numbers as JSON array content, "1,2,3,4" (allocated, NULL for none) */
    char *snapshot;         /**< the client's snapshot the result is of (allocated, NULL for the file on disk) */
    struct ScoutFile *next; /**< the next file of the session */
} ScoutFile;

/** A language, as a parser described it. */
typedef struct ScoutLanguage {
    char *id;               /**< identifier, e.g. "c" (allocated) */
    char *name;             /**< name shown (allocated) */
    char **extensions;      /**< extensions with their dot, lower case (allocated) */
    long extension_count;   /**< number of extensions */
    char *syntax;           /**< the lexical rules as JSON on one line (allocated, NULL for none) */
    char **keywords;        /**< all words of the rules' keyword lists, for completion (allocated) */
    long keyword_count;     /**< number of them */
    int case_sensitive;     /**< keywords match with case */
    char ident[256];        /**< characters that may be part of a word (non-zero entries) */
    long parser;            /**< index of its parser in ScoutStore::parsers */
} ScoutLanguage;

/** A parser Scout knows. */
typedef struct ScoutParser {
    char *path;             /**< its program (allocated) */
    char *name;             /**< its name (allocated) */
    char *version;          /**< its version (allocated) */
    int disabled;           /**< not used any more (it hung) */
} ScoutParser;

/** A session: a client working on a project. */
typedef struct ScoutSession {
    long id;                /**< its number; 0 is the session without a project */
    char *project;          /**< the project drawer (allocated, NULL for session 0) */
    char **include;         /**< paths searched for included files (allocated) */
    long include_count;     /**< number of them */
    ScoutFile *files;       /**< its files */
    long file_count;        /**< number of them */
    long error_code;        /**< code of the last error (RC2), 0 */
    char *error_text;       /**< its text (allocated, may be NULL) */
    struct ScoutSession *next;  /**< the next session */
} ScoutSession;

/** Everything Scout knows. */
typedef struct ScoutStore {
    ScoutParser *parsers;           /**< the parsers */
    long parser_count;              /**< number of parsers */
    ScoutLanguage *languages;       /**< their languages */
    long language_count;            /**< number of languages */
    char **problems;                /**< problems to report in STATUS (allocated) */
    long problem_count;             /**< number of problems */
    ScoutSession *sessions;         /**< the sessions, session 0 first */
    long next_session;              /**< number of the next session */
} ScoutStore;

/**
 * @brief Match callback of scout_write_symbols() (an AmigaDOS pattern on
 *        the Amiga).
 * @param context The context given to scout_write_symbols().
 * @param name A symbol's name.
 * @return Non-zero if the symbol is to be listed.
 */
typedef int (*ScoutMatch)(void *context, const char *name);

/**
 * @brief Initialise an empty store with session 0.
 * @param st The store.
 * @return 1 on success, 0 if out of memory.
 */
int scout_store_init(ScoutStore *st);

/**
 * @brief Free everything in a store.
 * @param st The store.
 */
void scout_store_free(ScoutStore *st);

/**
 * @brief Forget all parsers, languages and problems (before reading the
 *        parsers again).
 * @param st The store.
 */
void scout_store_clear_parsers(ScoutStore *st);

/**
 * @brief Add a problem to report in STATUS.
 * @param st The store.
 * @param first First part of the text.
 * @param second Second part, appended (may be NULL).
 */
void scout_store_add_problem(ScoutStore *st, const char *first, const char *second);

/**
 * @brief Add a parser from the result of its scout/describe.
 *
 * A language or extension another parser claims already stays with that
 * one (a problem is reported).
 *
 * @param st The store.
 * @param path The parser's program.
 * @param describe The result of scout/describe (JSON text).
 * @param extension If not NULL: the parser takes this extension from any
 *                  other language for its first language (REGISTER).
 * @return 1 on success, 0 if the description is not usable (a problem is
 *         reported), -1 if out of memory.
 */
int scout_store_add_parser(ScoutStore *st, const char *path, const char *describe,
                           const char *extension);

/**
 * @brief Find a language by its identifier.
 * @param st The store.
 * @param id The identifier.
 * @return The language, or NULL.
 */
const ScoutLanguage *scout_store_language(const ScoutStore *st, const char *id);

/**
 * @brief Find the language of a file by its extension.
 * @param st The store.
 * @param path The file.
 * @return The language, or NULL.
 */
const ScoutLanguage *scout_store_language_for(const ScoutStore *st, const char *path);

/**
 * @brief Compare two paths without regard to case (as AmigaDOS does).
 * @param a A path.
 * @param b Another path.
 * @return Non-zero if they are equal.
 */
int scout_path_equal(const char *a, const char *b);

/**
 * @brief Tell whether a path lies in a drawer (or below it), without
 *        regard to case.
 * @param drawer The drawer (a full name, e.g. "Work:Project" or "Work:").
 * @param path The path (a full name).
 * @return Non-zero if it does.
 */
int scout_path_inside(const char *drawer, const char *path);

/**
 * @brief Open a session.
 * @param st The store.
 * @param project The project drawer.
 * @return The session, or NULL if out of memory.
 */
ScoutSession *scout_session_open(ScoutStore *st, const char *project);

/**
 * @brief Find a session.
 * @param st The store.
 * @param id Its number (0: the session without a project).
 * @return The session, or NULL.
 */
ScoutSession *scout_session_find(const ScoutStore *st, long id);

/**
 * @brief Close a session and free its files (not session 0, which is only
 *        emptied).
 * @param st The store.
 * @param id Its number.
 * @return 1 if it existed.
 */
int scout_session_close(ScoutStore *st, long id);

/**
 * @brief Add an include path to a session (once).
 * @param s The session.
 * @param path The path.
 * @return 1 on success, 0 if out of memory.
 */
int scout_session_add_include(ScoutSession *s, const char *path);

/**
 * @brief Remove all include paths of a session.
 * @param s The session.
 */
void scout_session_clear_include(ScoutSession *s);

/**
 * @brief Note the last error of a session (ERROR returns it).
 * @param s The session.
 * @param code Its number (RC2).
 * @param text Its text (copied).
 */
void scout_session_set_error(ScoutSession *s, long code, const char *text);

/**
 * @brief Find a file of a session.
 * @param s The session.
 * @param path The file.
 * @return The file, or NULL.
 */
ScoutFile *scout_file_find(const ScoutSession *s, const char *path);

/**
 * @brief Find a file of a session or add it (without a result).
 * @param s The session.
 * @param path The file.
 * @return The file, or NULL if out of memory.
 */
ScoutFile *scout_file_get(ScoutSession *s, const char *path);

/**
 * @brief Drop the result of a file (FORGET), so the file on disk counts
 *        again.
 * @param f The file.
 */
void scout_file_forget(ScoutFile *f);

/**
 * @brief Keep the result of an analysis (scout/analyze) of a file,
 *        replacing the old one.
 * @param f The file.
 * @param language The language it was analysed as.
 * @param version The client's version of the snapshot, -1 for the file
 *                on disk.
 * @param stamp The file on disk (may be NULL).
 * @param result The result (JSON text).
 * @return 1 on success; 0 if the result is not valid (the old one stays);
 *         -1 if out of memory (the old one stays).
 */
int scout_file_set_result(ScoutFile *f, const char *language, long version,
                          const ScoutStamp *stamp, const char *result);

/**
 * @brief Find the word at a column of a line, or the part of it before
 *        the column.
 * @param lang The language (its word characters), or NULL for letters,
 *             digits and '_'.
 * @param line The line (terminated, without its line end).
 * @param col The column (1-based).
 * @param before Only the part before @p col (the prefix to complete).
 * @param[out] out The word.
 * @param size Size of @p out.
 * @return Its length (0 if there is none).
 */
size_t scout_word_at(const ScoutLanguage *lang, const char *line, long col, int before,
                     char *out, size_t size);

/**
 * @brief Find a member of a JSON object in its text, without parsing it.
 *
 * Used to take the "result" out of a parser's answer as text, so it need
 * not be parsed and written again.
 *
 * @param obj The object's text.
 * @param key The member's key.
 * @param[out] value The first character of its value.
 * @param[out] length The length of the value's text.
 * @return 1 if found, 0 if not (or the text is no object).
 */
int scout_json_member(const char *obj, const char *key, const char **value, size_t *length);

/**
 * @brief Write the result of ANALYZE: {"file","version","language",
 *        "symbols","diagnostics"} (the counts).
 * @param w The writer.
 * @param f The file.
 */
void scout_write_analyze(JsonWriter *w, const ScoutFile *f);

/**
 * @brief Write the result of SYMBOLS: those of a file, or those of the
 *        project files (without locals, with "file").
 * @param w The writer.
 * @param s The session.
 * @param f The file, or NULL for the project.
 * @param match Selects the symbols listed (NULL: all).
 * @param context Passed to @p match.
 */
void scout_write_symbols(JsonWriter *w, const ScoutSession *s, const ScoutFile *f,
                         ScoutMatch match, void *context);

/**
 * @brief Write the result of DIAGNOSTICS: those of a file, or those of all
 *        project files that have any.
 * @param w The writer.
 * @param s The session.
 * @param f The file, or NULL for the project.
 */
void scout_write_diagnostics(JsonWriter *w, const ScoutSession *s, const ScoutFile *f);

/**
 * @brief Write the result of TOKENS.
 * @param w The writer.
 * @param f The file.
 */
void scout_write_tokens(JsonWriter *w, const ScoutFile *f);

/**
 * @brief Write the result of DEFINITION: where a name used at a position
 *        is defined.
 *
 * Looks among the locals of the function at the position, then the
 * symbols of the file, then the global symbols of the session's other
 * files; definitions come before declarations. Without a match, fields of
 * the name anywhere are listed.
 *
 * @param w The writer.
 * @param s The session.
 * @param f The file the name is used in.
 * @param line The position's line.
 * @param col Its column.
 * @param name The name.
 */
void scout_write_definition(JsonWriter *w, const ScoutSession *s, const ScoutFile *f,
                            long line, long col, const char *name);

/**
 * @brief Write the result of COMPLETE: names starting with a prefix
 *        (without regard to case), locals first, then the file's, the
 *        project's and the language's keywords.
 * @param w The writer.
 * @param st The store (the languages).
 * @param s The session.
 * @param f The file.
 * @param line The position's line.
 * @param col Its column.
 * @param prefix The prefix ("" for all).
 * @param max Most items written.
 */
void scout_write_complete(JsonWriter *w, const ScoutStore *st, const ScoutSession *s,
                          const ScoutFile *f, long line, long col, const char *prefix, long max);

/**
 * @brief Write the result of LANGUAGES.
 * @param w The writer.
 * @param st The store.
 */
void scout_write_languages(JsonWriter *w, const ScoutStore *st);

/**
 * @brief Write the parsers and problems for STATUS: ,"parsers":[...],
 *        "problems":[...] (with the leading comma).
 * @param w The writer.
 * @param st The store.
 */
void scout_write_parsers(JsonWriter *w, const ScoutStore *st);

#endif
