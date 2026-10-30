/**
 * @file rules.h
 * @brief The lexical rules of a language as a parser of TinyDev-Scout
 *        describes them (SYNTAX), and the generic scanner that highlights
 *        a line with them.
 *
 * The rules' format is in src/scout/docs/Scout-Protocol.md ("Lexical
 * rules"). Portable C without Amiga headers (host-tested). The scanner
 * runs in the highlighter hook and in the minimap's render process: it
 * only reads the rules and allocates nothing.
 */
#ifndef TINYDEV_RULES_H
#define TINYDEV_RULES_H

#include "syntax.h"

/** Most delimiters of a kind (line comments, block comments, strings, number prefixes). */
#define RULES_MAX 4
/** Longest delimiter. */
#define RULES_DELIM 8

/** A pair of delimiters: a block comment or a string. */
typedef struct RulesPair {
    char open[RULES_DELIM];     /**< what opens it */
    char close[RULES_DELIM];    /**< what closes it */
    unsigned char escape;       /**< escape character of a string, 0 for none */
    unsigned char multiline;    /**< a string may go on to the next line */
    unsigned char nested;       /**< an open inside a comment counts */
} RulesPair;

/** The lexical rules of a language. */
typedef struct SyntaxRules {
    int case_sensitive;                         /**< keywords match with case */
    unsigned char start[256];                   /**< characters that may start a word */
    unsigned char part[256];                    /**< characters that may go on with a word */
    unsigned char first[256];                   /**< 1: a comment or string may start with it; 2: a number prefix may */
    char line_comments[RULES_MAX][RULES_DELIM]; /**< what starts a comment to the end of the line */
    int line_comment_count;                     /**< number of them */
    RulesPair blocks[RULES_MAX];                /**< block comments */
    int block_count;                            /**< number of them */
    RulesPair strings[RULES_MAX];               /**< strings */
    int string_count;                           /**< number of them */
    char prefixes[RULES_MAX][RULES_DELIM];      /**< prefixes of numbers ("0x", "$") */
    int prefix_count;                           /**< number of them */
    char pp_prefix[RULES_DELIM];                /**< prefix of a preprocessor line, "" for none */
    unsigned char pp_continuation;              /**< a preprocessor line ending in it goes on, 0 */
    char **words;                               /**< the keywords (lower case if not case sensitive) */
    unsigned char *classes;                     /**< the style of each (SyntaxStyle) */
    long word_count;                            /**< number of keywords */
    long *slots;                                /**< hash table of the keywords (index, -1) */
    unsigned long mask;                         /**< size of the table minus one */
} SyntaxRules;

/**
 * @brief Read lexical rules from their JSON text (SYNTAX's result).
 * @param json The text.
 * @return The rules (free them with rules_free()), or NULL if the text is
 *         not usable or memory ran out.
 */
SyntaxRules *rules_parse(const char *json);

/**
 * @brief Free rules.
 * @param rules The rules (may be NULL).
 */
void rules_free(SyntaxRules *rules);

/**
 * @brief Scan a line with lexical rules.
 *
 * Keywords get SYNTAX_KEYWORD, type keywords SYNTAX_TYPE, constants and
 * numbers SYNTAX_CONSTANT, builtins SYNTAX_FUNCTION. A preprocessor line
 * is SYNTAX_PREPROCESSOR as a whole, with its comments and strings emitted
 * after it.
 *
 * @param rules The rules.
 * @param text The line (terminated, without its line end).
 * @param previous The state the previous line ended in (0 at the start).
 * @param emit Called for each span (may be NULL).
 * @param context Passed to @p emit.
 * @return The state the line ends in.
 */
unsigned long rules_scan_line(const SyntaxRules *rules, const char *text, unsigned long previous,
                              SyntaxEmit emit, void *context);

#endif
