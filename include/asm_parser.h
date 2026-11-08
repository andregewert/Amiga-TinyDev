/**
 * @file asm_parser.h
 * @brief Interface of the assembler parser of Scout: the analysis of one
 *        68k source in Motorola syntax as vasm and AsmPro read it (labels,
 *        equates, macros, the structure of conditionals and macros, local
 *        labels, highlighting tokens) and the handling of the requests of
 *        the Scout protocol.
 *
 * Portable C without Amiga headers, so the host tests can use it.
 */
#ifndef TINYDEV_ASM_PARSER_H
#define TINYDEV_ASM_PARSER_H

#include <stddef.h>
#include "json.h"
#include "scout_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

/** A range in the source (1-based lines and byte columns, end exclusive). */
typedef struct AsmRange {
    long start_line;    /**< first line */
    long start_col;     /**< first column */
    long end_line;      /**< line of the end */
    long end_col;       /**< column after the last character */
} AsmRange;

/** Where a symbol can be seen. */
typedef enum AsmScope {
    ASM_SCOPE_GLOBAL,   /**< also by other files */
    ASM_SCOPE_FILE,     /**< in its own file */
    ASM_SCOPE_LOCAL     /**< near its global label: a local label */
} AsmScope;

/** A symbol defined in the source. */
typedef struct AsmSymbol {
    char *name;         /**< its name as written (allocated) */
    char *detail;       /**< what defines it, as text (allocated, may be NULL) */
    int kind;           /**< SCOUT_SYMBOL_LABEL, _CONSTANT, _VARIABLE, _FIELD, _MACRO or _STRUCT */
    AsmScope scope;     /**< where it can be seen */
    long parent;        /**< index of the global label of a local label or the STRUCTURE of a field, or -1 */
    AsmRange range;     /**< the definition */
    AsmRange selection; /**< its name */
} AsmSymbol;

/** A problem found in the source. */
typedef struct AsmDiagnostic {
    char *message;      /**< one line of text (allocated) */
    int severity;       /**< SCOUT_SEVERITY_ value */
    AsmRange range;     /**< where it is */
} AsmDiagnostic;

/** An INCLUDE. */
typedef struct AsmReference {
    char *target;       /**< the file as written (allocated) */
    AsmRange range;     /**< where it is written */
} AsmReference;

/** The result of analysing a source. */
typedef struct AsmResult {
    AsmSymbol *symbols;             /**< the symbols, in the order of the source */
    long symbol_count;              /**< number of symbols */
    AsmDiagnostic *diagnostics;     /**< the diagnostics, sorted by place */
    long diagnostic_count;          /**< number of diagnostics */
    AsmReference *references;       /**< the INCLUDEs, in the order of the source */
    long reference_count;           /**< number of references */
    long *tokens;                   /**< highlighting tokens, four values each: line, column, length, SCOUT_TOKEN_ kind; sorted, no overlaps */
    long token_count;               /**< number of tokens (values / 4) */
    int out_of_memory;              /**< memory ran out; the result is incomplete */
    int stopped;                    /**< asm_parser_should_stop() asked to stop; the result is incomplete */
} AsmResult;

/**
 * @brief Optional function the analysis calls now and then; when it
 *        returns non-zero, the analysis stops (AsmResult::stopped).
 *
 * The program sets it to a Ctrl-C check; NULL (the default) never stops.
 */
extern int (*asm_parser_should_stop)(void);

/** @name Words of the lexical rules (NULL-terminated lists, lower case) */
/** @{ */
extern const char *const asm_parser_instructions[];     /**< the instructions of the 68000 to 68060, FPU and MMU (class "builtin") */
extern const char *const asm_parser_registers[];        /**< the registers (class "type") */
/** @} */

/**
 * @brief The name of a directive, for the lexical rules (class "keyword").
 * @param index Its index, from 0.
 * @return Its name in lower case, or NULL after the last.
 */
const char *asm_parser_directive_name(long index);

/**
 * @brief Analyse an assembler source.
 *
 * A line is [label[:]] [mnemonic[.size] [operands]] [comment]. A ';' starts
 * a comment anywhere, a '*' in the first column makes the line one, and
 * after the operands the rest of the line is a comment without any mark (a
 * blank ends the operands outside quotes and parentheses; one after a comma
 * does not). A word in the first column is a label, unless it is a known
 * mnemonic or macro, has no colon and no mnemonic follows it (rts, add
 * d0,d1), as AsmPro accepts those there; "text dc.b 1" is a label. An
 * indented word is a label if a ':' follows it or a directive that defines
 * a name (EQU, =, SET, MACRO, RS ...).
 *
 * The analysis makes two passes over the lines: the first finds what is
 * defined and the block structure, the second makes the tokens and checks
 * the uses, so a name may be used before its definition.
 *
 * A local label starts with '.' or ends in '$'. It belongs to the part of
 * the source between two global labels; EQU and the like do not start a new
 * part, nor does a global label inside a conditional, which may not be
 * assembled.
 *
 * Symbols: global labels, local labels with their global label as parent,
 * EQU, =, EQUR and REG as constants, SET as a variable (once), RS, SO and FO
 * as fields, macros. What a macro body defines is not listed: it only
 * exists when the macro is called. A name in an include file can be seen
 * by other files, as can a label named by XDEF; other names only in their
 * own file.
 *
 * Calls of the macros of the NDK define what they would: STRUCTURE a
 * structure with the fields that follow as its children (FPTR, BOOL, BYTE,
 * UBYTE, WORD, UWORD, SHORT, USHORT, LONG, ULONG, FLOAT, DOUBLE, APTR, CPTR,
 * RPTR, LABEL, STRUCT), EITEM, LIBDEF and DEVCMD a constant, BITDEF the
 * constants prefixB_name and prefixF_name, FUNCDEF the constant _LVOname. A
 * macro of the source itself goes first, of whatever name; if a line of its
 * body defines a name with \\1 in it by EQU or =, as _LVO\\1 EQU count does,
 * each call defines that name.
 *
 * Diagnostics: errors for a conditional without ENDC, ENDC or ELSE without
 * a conditional, a second ELSE, MACRO without ENDM, ENDM without MACRO,
 * REPT without ENDR, ENDR without REPT, a name defined twice (not inside
 * conditionals or REPT), a local label that does not exist, a size an
 * instruction does not have and parentheses that do not match. Lookups of
 * local labels ignore case, as AsmPro may. A warning for a string not
 * closed. A hint for an unknown mnemonic, only in a source without INCLUDE,
 * which could bring macros in. The bodies of macros are not checked, and a
 * source in GNU syntax (.text, .globl, %d0, \#NO_APP) gets no diagnostics.
 *
 * Tokens: labels and macro parameters (\\1, \\\@) as
 * SCOUT_TOKEN_PARAMETER, macro calls as SCOUT_TOKEN_MACRO, equates and
 * fields as SCOUT_TOKEN_CONSTANT, the names of structures as
 * SCOUT_TOKEN_TYPE.
 *
 * @param source The text (any bytes; need not be terminated).
 * @param length Its length.
 * @param is_include Non-zero for an include file (.i).
 * @return The result (free it with asm_result_free()), or NULL if out of
 *         memory.
 */
AsmResult *asm_parser_analyze(const char *source, size_t length, int is_include);

/**
 * @brief Free a result.
 * @param res The result (may be NULL).
 */
void asm_result_free(AsmResult *res);

/**
 * @brief Write the result of scout/describe.
 * @param w The writer.
 */
void asm_parser_describe(JsonWriter *w);

/**
 * @brief Answer one request of the Scout protocol.
 *
 * Knows scout/describe and scout/analyze; other methods are answered with
 * the "unknown method" error.
 *
 * @param request The request (one line of JSON).
 * @param response The writer of the answer (one line of JSON, without the
 *                 line end).
 * @return 1 if an answer was written (also an error answer); 0 if the
 *         analysis was stopped (asm_parser_should_stop()) and nothing was
 *         written.
 */
int asm_parser_handle_request(const char *request, JsonWriter *response);

#ifdef __cplusplus
}
#endif

#endif /* TINYDEV_ASM_PARSER_H */
