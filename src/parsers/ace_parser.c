/**
 * @file ace_parser.c
 * @brief The analysis of the ACE BASIC parser of Scout: two passes over the
 *        statements of a source.
 *
 * It follows ACE's own compiler (the reserved words are its table). It is
 * a heuristic: expressions are not parsed, only the statements that define
 * something or open and close a block.
 */
#include "ace_parser.h"

#include <stdlib.h>
#include <string.h>

int (*ace_parser_should_stop)(void) = NULL;

/** Most diagnostics reported for one source. */
#define MAX_DIAGNOSTICS 200
/** Most nested blocks that are followed. */
#define MAX_BLOCKS 64
/** Most open brackets that are followed. */
#define MAX_BRACKETS 64
/** Most nested \#ifdefs that are followed. */
#define MAX_CONDITIONALS 32
/** Most parameters and local variables of a SUB that are remembered. */
#define MAX_LOCALS 256
/** Slots of the hash table of keywords. */
#define WORD_SLOTS 1024
/** Slots of the hash table of definitions. */
#define DEF_SLOTS 1024
/** Longest text made a symbol's detail. */
#define MAX_DETAIL 100
/** How many statements pass between two looks at ace_parser_should_stop(). */
#define STOP_INTERVAL 64

/** What a reserved word does for the analysis. */
typedef enum Op {
    OP_PLAIN,       /**< nothing special */
    OP_TYPE,        /**< a type: SHORTINT, LONGINT, ... */
    OP_IF,          /**< IF */
    OP_THEN,        /**< THEN */
    OP_ELSE,        /**< ELSE */
    OP_ELSEIF,      /**< ELSEIF */
    OP_GOTO,        /**< GOTO */
    OP_END,         /**< END */
    OP_FOR,         /**< FOR */
    OP_NEXT,        /**< NEXT */
    OP_WHILE,       /**< WHILE */
    OP_WEND,        /**< WEND */
    OP_REPEAT,      /**< REPEAT */
    OP_UNTIL,       /**< UNTIL */
    OP_CASE,        /**< CASE */
    OP_TYPECASE,    /**< TYPECASE */
    OP_BLOCK,       /**< BLOCK */
    OP_SUB,         /**< SUB */
    OP_METHOD,      /**< METHOD */
    OP_GENERIC,     /**< GENERIC */
    OP_STRUCT,      /**< STRUCT */
    OP_CLASS,       /**< CLASS */
    OP_EXTENDS,     /**< EXTENDS */
    OP_ASSEM,       /**< ASSEM */
    OP_DECLARE,     /**< DECLARE */
    OP_FUNCTION,    /**< FUNCTION */
    OP_EXTERNAL,    /**< EXTERNAL */
    OP_CONST,       /**< CONST */
    OP_DIM,         /**< DIM */
    OP_DEF,         /**< DEF */
    OP_GLOBAL,      /**< GLOBAL */
    OP_COMMON,      /**< COMMON */
    OP_DATA,        /**< DATA */
    OP_SIZEOF,      /**< SIZEOF */
    OP_REM          /**< REM */
} Op;

/** A reserved word. */
typedef struct Word {
    const char *name;   /**< its text, in capitals */
    Op op;              /**< what it does */
    short function;     /**< a function: for the class "builtin" of the rules */
    short next;         /**< next word of its hash slot, or -1 */
} Word;

/** The reserved words of ACE's compiler. */
static Word words[] = {
    { "ABS", OP_PLAIN, 1, -1 }, { "ALL", OP_PLAIN, 0, -1 }, { "AND", OP_PLAIN, 0, -1 },
    { "APPEND", OP_PLAIN, 0, -1 }, { "AREA", OP_PLAIN, 0, -1 }, { "AREAFILL", OP_PLAIN, 0, -1 },
    { "AS", OP_PLAIN, 0, -1 }, { "ASC", OP_PLAIN, 1, -1 }, { "ATN", OP_PLAIN, 1, -1 },
    { "BASE", OP_PLAIN, 0, -1 }, { "BEEP", OP_PLAIN, 0, -1 }, { "BREAK", OP_PLAIN, 0, -1 },
    { "CALL", OP_PLAIN, 0, -1 }, { "CDBL", OP_PLAIN, 0, -1 }, { "CHAIN", OP_PLAIN, 0, -1 },
    { "CHDIR", OP_PLAIN, 0, -1 }, { "CHR$", OP_PLAIN, 1, -1 }, { "CINT", OP_PLAIN, 1, -1 },
    { "CIRCLE", OP_PLAIN, 0, -1 }, { "CLEAR", OP_PLAIN, 0, -1 }, { "CLNG", OP_PLAIN, 1, -1 },
    { "CLOSE", OP_PLAIN, 0, -1 }, { "CLS", OP_PLAIN, 0, -1 }, { "COLLISION", OP_PLAIN, 0, -1 },
    { "COLOR", OP_PLAIN, 0, -1 }, { "COMMON", OP_COMMON, 0, -1 }, { "CONT", OP_PLAIN, 0, -1 },
    { "COS", OP_PLAIN, 1, -1 }, { "CSNG", OP_PLAIN, 1, -1 }, { "CSRLIN", OP_PLAIN, 0, -1 },
    { "CVD", OP_PLAIN, 0, -1 }, { "CVI", OP_PLAIN, 0, -1 }, { "CVL", OP_PLAIN, 0, -1 },
    { "CVS", OP_PLAIN, 0, -1 }, { "DATA", OP_DATA, 0, -1 }, { "DATE$", OP_PLAIN, 1, -1 },
    { "DECLARE", OP_DECLARE, 0, -1 }, { "DEF", OP_DEF, 0, -1 }, { "DEFDBL", OP_PLAIN, 0, -1 },
    { "DEFINT", OP_PLAIN, 0, -1 }, { "DEFLNG", OP_PLAIN, 0, -1 }, { "DEFSNG", OP_PLAIN, 0, -1 },
    { "DEFSTR", OP_PLAIN, 0, -1 }, { "DELETE", OP_PLAIN, 0, -1 }, { "DIM", OP_DIM, 0, -1 },
    { "ELSE", OP_ELSE, 0, -1 }, { "ELSEIF", OP_ELSEIF, 0, -1 }, { "END", OP_END, 0, -1 },
    { "EOF", OP_PLAIN, 1, -1 }, { "EQV", OP_PLAIN, 0, -1 }, { "ERASE", OP_PLAIN, 0, -1 },
    { "ERL", OP_PLAIN, 0, -1 }, { "ERR", OP_PLAIN, 0, -1 }, { "ERROR", OP_PLAIN, 0, -1 },
    { "EXIT", OP_PLAIN, 0, -1 }, { "EXP", OP_PLAIN, 1, -1 }, { "FIELD", OP_PLAIN, 0, -1 },
    { "FILES", OP_PLAIN, 0, -1 }, { "FIX", OP_PLAIN, 1, -1 }, { "FN", OP_PLAIN, 0, -1 },
    { "FOR", OP_FOR, 0, -1 }, { "FRE", OP_PLAIN, 1, -1 }, { "FREE", OP_PLAIN, 0, -1 },
    { "FUNCTION", OP_FUNCTION, 0, -1 }, { "GET", OP_PLAIN, 0, -1 }, { "GOSUB", OP_PLAIN, 0, -1 },
    { "GOTO", OP_GOTO, 0, -1 }, { "HEX$", OP_PLAIN, 1, -1 }, { "IF", OP_IF, 0, -1 },
    { "IMP", OP_PLAIN, 0, -1 }, { "INKEY$", OP_PLAIN, 1, -1 }, { "INPUT", OP_PLAIN, 0, -1 },
    { "INPUT$", OP_PLAIN, 1, -1 }, { "INSTR", OP_PLAIN, 1, -1 }, { "INT", OP_PLAIN, 1, -1 },
    { "KILL", OP_PLAIN, 0, -1 }, { "LBOUND", OP_PLAIN, 0, -1 }, { "LCASE$", OP_PLAIN, 1, -1 },
    { "LEFT$", OP_PLAIN, 1, -1 }, { "LEN", OP_PLAIN, 1, -1 }, { "LET", OP_PLAIN, 0, -1 },
    { "LIBRARY", OP_PLAIN, 0, -1 }, { "LINE", OP_PLAIN, 0, -1 }, { "LIST", OP_PLAIN, 0, -1 },
    { "LLIST", OP_PLAIN, 0, -1 }, { "LOAD", OP_PLAIN, 0, -1 }, { "LOC", OP_PLAIN, 1, -1 },
    { "LOCATE", OP_PLAIN, 0, -1 }, { "LOF", OP_PLAIN, 1, -1 }, { "LOG", OP_PLAIN, 1, -1 },
    { "LPOS", OP_PLAIN, 0, -1 }, { "LPRINT", OP_PLAIN, 0, -1 }, { "LSET", OP_PLAIN, 0, -1 },
    { "MENU", OP_PLAIN, 1, -1 }, { "MERGE", OP_PLAIN, 0, -1 }, { "MID$", OP_PLAIN, 1, -1 },
    { "MKD$", OP_PLAIN, 1, -1 }, { "MKI$", OP_PLAIN, 1, -1 }, { "MKL$", OP_PLAIN, 1, -1 },
    { "MKS$", OP_PLAIN, 1, -1 }, { "MOD", OP_PLAIN, 0, -1 }, { "MOUSE", OP_PLAIN, 1, -1 },
    { "NAME", OP_PLAIN, 0, -1 }, { "NEW", OP_PLAIN, 0, -1 }, { "NEXT", OP_NEXT, 0, -1 },
    { "NOT", OP_PLAIN, 0, -1 }, { "OBJECT.AX", OP_PLAIN, 0, -1 }, { "OBJECT.AY", OP_PLAIN, 0, -1 },
    { "OBJECT.CLIP", OP_PLAIN, 0, -1 }, { "OBJECT.CLOSE", OP_PLAIN, 0, -1 }, { "OBJECT.HIT", OP_PLAIN, 0, -1 },
    { "OBJECT.OFF", OP_PLAIN, 0, -1 }, { "OBJECT.ON", OP_PLAIN, 0, -1 }, { "OBJECT.PLANES", OP_PLAIN, 0, -1 },
    { "OBJECT.PRIORITY", OP_PLAIN, 0, -1 }, { "OBJECT.SHAPE", OP_PLAIN, 0, -1 }, { "OBJECT.START", OP_PLAIN, 0, -1 },
    { "OBJECT.STOP", OP_PLAIN, 0, -1 }, { "OBJECT.VX", OP_PLAIN, 0, -1 }, { "OBJECT.VY", OP_PLAIN, 0, -1 },
    { "OBJECT.X", OP_PLAIN, 0, -1 }, { "OBJECT.Y", OP_PLAIN, 0, -1 }, { "OCT$", OP_PLAIN, 1, -1 },
    { "OFF", OP_PLAIN, 0, -1 }, { "ON", OP_PLAIN, 0, -1 }, { "OPEN", OP_PLAIN, 0, -1 },
    { "OPTION", OP_PLAIN, 0, -1 }, { "OR", OP_PLAIN, 0, -1 }, { "OUTPUT", OP_PLAIN, 0, -1 },
    { "PAINT", OP_PLAIN, 0, -1 }, { "PALETTE", OP_PLAIN, 0, -1 }, { "PATTERN", OP_PLAIN, 0, -1 },
    { "PEEK", OP_PLAIN, 1, -1 }, { "PEEKL", OP_PLAIN, 1, -1 }, { "PEEKW", OP_PLAIN, 1, -1 },
    { "POINT", OP_PLAIN, 1, -1 }, { "POKE", OP_PLAIN, 0, -1 }, { "POKEL", OP_PLAIN, 0, -1 },
    { "POKEW", OP_PLAIN, 0, -1 }, { "POS", OP_PLAIN, 0, -1 }, { "PRESET", OP_PLAIN, 0, -1 },
    { "PRINT", OP_PLAIN, 0, -1 }, { "PSET", OP_PLAIN, 0, -1 }, { "PTAB", OP_PLAIN, 1, -1 },
    { "PUT", OP_PLAIN, 0, -1 }, { "RANDOMIZE", OP_PLAIN, 0, -1 }, { "READ", OP_PLAIN, 0, -1 },
    { "REM", OP_REM, 0, -1 }, { "RESTORE", OP_PLAIN, 0, -1 }, { "RESUME", OP_PLAIN, 0, -1 },
    { "RETURN", OP_PLAIN, 0, -1 }, { "RIGHT$", OP_PLAIN, 1, -1 }, { "RND", OP_PLAIN, 1, -1 },
    { "RSET", OP_PLAIN, 0, -1 }, { "RUN", OP_PLAIN, 0, -1 }, { "SADD", OP_PLAIN, 1, -1 },
    { "SAVE", OP_PLAIN, 0, -1 }, { "SAY", OP_PLAIN, 1, -1 }, { "SCREEN", OP_PLAIN, 1, -1 },
    { "SCROLL", OP_PLAIN, 0, -1 }, { "SGN", OP_PLAIN, 1, -1 }, { "SHARED", OP_PLAIN, 0, -1 },
    { "SIN", OP_PLAIN, 1, -1 }, { "SLEEP", OP_PLAIN, 0, -1 }, { "SOUND", OP_PLAIN, 0, -1 },
    { "SPACE$", OP_PLAIN, 1, -1 }, { "SPC", OP_PLAIN, 1, -1 }, { "SQR", OP_PLAIN, 1, -1 },
    { "STATIC", OP_PLAIN, 0, -1 }, { "STEP", OP_PLAIN, 0, -1 }, { "STICK", OP_PLAIN, 1, -1 },
    { "STOP", OP_PLAIN, 0, -1 }, { "STR$", OP_PLAIN, 1, -1 }, { "STRIG", OP_PLAIN, 1, -1 },
    { "STRING$", OP_PLAIN, 1, -1 }, { "SUB", OP_SUB, 0, -1 }, { "SWAP", OP_PLAIN, 0, -1 },
    { "SYSTEM", OP_PLAIN, 0, -1 }, { "TAB", OP_PLAIN, 1, -1 }, { "TAN", OP_PLAIN, 1, -1 },
    { "THEN", OP_THEN, 0, -1 }, { "TIME$", OP_PLAIN, 1, -1 }, { "TIMER", OP_PLAIN, 0, -1 },
    { "TO", OP_PLAIN, 0, -1 }, { "TRANSLATE$", OP_PLAIN, 1, -1 }, { "TROFF", OP_PLAIN, 0, -1 },
    { "TRON", OP_PLAIN, 0, -1 }, { "UBOUND", OP_PLAIN, 0, -1 }, { "UCASE$", OP_PLAIN, 1, -1 },
    { "USING", OP_PLAIN, 0, -1 }, { "VAL", OP_PLAIN, 1, -1 }, { "VARPTR", OP_PLAIN, 1, -1 },
    { "WAIT", OP_PLAIN, 0, -1 }, { "WAVE", OP_PLAIN, 0, -1 }, { "WEND", OP_WEND, 0, -1 },
    { "WHILE", OP_WHILE, 0, -1 }, { "WIDTH", OP_PLAIN, 0, -1 }, { "WINDOW", OP_PLAIN, 1, -1 },
    { "WRITE", OP_PLAIN, 0, -1 }, { "XOR", OP_PLAIN, 0, -1 }, { "ADDRESS", OP_TYPE, 0, -1 },
    { "ALLOC", OP_PLAIN, 1, -1 }, { "ARG$", OP_PLAIN, 1, -1 }, { "ARGCOUNT", OP_PLAIN, 0, -1 },
    { "ASSEM", OP_ASSEM, 0, -1 }, { "ASSERT", OP_PLAIN, 0, -1 }, { "ATOM", OP_TYPE, 0, -1 },
    { "BACK", OP_PLAIN, 0, -1 }, { "BEVELBOX", OP_PLAIN, 0, -1 }, { "BIN$", OP_PLAIN, 1, -1 },
    { "BIND", OP_PLAIN, 1, -1 }, { "BLOCK", OP_BLOCK, 0, -1 }, { "BUTTON", OP_PLAIN, 0, -1 },
    { "BUTTON_KIND", OP_PLAIN, 0, -1 }, { "BYTE", OP_TYPE, 0, -1 }, { "CALLBACK", OP_PLAIN, 0, -1 },
    { "CASE", OP_CASE, 0, -1 }, { "CHECKBOX_KIND", OP_PLAIN, 0, -1 }, { "CHIPSET", OP_PLAIN, 1, -1 },
    { "CLASS", OP_CLASS, 0, -1 }, { "CONST", OP_CONST, 0, -1 }, { "CSTR", OP_PLAIN, 1, -1 },
    { "CYCLE_KIND", OP_PLAIN, 0, -1 }, { "DAY", OP_PLAIN, 0, -1 }, { "ENDSWITH", OP_PLAIN, 1, -1 },
    { "EXTENDS", OP_EXTENDS, 0, -1 }, { "EXTERNAL", OP_EXTERNAL, 0, -1 }, { "FILEBOX$", OP_PLAIN, 1, -1 },
    { "FMT$", OP_PLAIN, 1, -1 }, { "FONT", OP_PLAIN, 0, -1 }, { "FORWARD", OP_PLAIN, 0, -1 },
    { "GADGET", OP_PLAIN, 1, -1 }, { "GENERIC", OP_GENERIC, 0, -1 }, { "GETATTR", OP_PLAIN, 0, -1 },
    { "GLOBAL", OP_GLOBAL, 0, -1 }, { "HANDLE", OP_PLAIN, 1, -1 }, { "INPUTBOX", OP_PLAIN, 1, -1 },
    { "INPUTBOX$", OP_PLAIN, 1, -1 }, { "INTEGER_KIND", OP_PLAIN, 0, -1 }, { "INVOKABLE", OP_PLAIN, 0, -1 },
    { "INVOKE", OP_PLAIN, 0, -1 }, { "LISTVIEW_KIND", OP_PLAIN, 0, -1 }, { "LONGINT", OP_TYPE, 0, -1 },
    { "LPAD$", OP_PLAIN, 1, -1 }, { "LTRIM$", OP_PLAIN, 1, -1 }, { "MESSAGE", OP_PLAIN, 0, -1 },
    { "METHOD", OP_METHOD, 0, -1 }, { "MSGBOX", OP_PLAIN, 1, -1 }, { "MX_KIND", OP_PLAIN, 0, -1 },
    { "NUMBER_KIND", OP_PLAIN, 0, -1 }, { "PALETTE_KIND", OP_PLAIN, 0, -1 }, { "POTX", OP_PLAIN, 1, -1 },
    { "POTY", OP_PLAIN, 1, -1 }, { "PRINTS", OP_PLAIN, 0, -1 }, { "REPEAT", OP_REPEAT, 0, -1 },
    { "REPEAT$", OP_PLAIN, 1, -1 }, { "REPLACE$", OP_PLAIN, 1, -1 }, { "REVERSE$", OP_PLAIN, 1, -1 },
    { "RINSTR", OP_PLAIN, 1, -1 }, { "RPAD$", OP_PLAIN, 1, -1 }, { "RTRIM$", OP_PLAIN, 1, -1 },
    { "SCROLLER_KIND", OP_PLAIN, 0, -1 }, { "SERIAL", OP_PLAIN, 1, -1 }, { "SETATTR", OP_PLAIN, 0, -1 },
    { "SHL", OP_PLAIN, 1, -1 }, { "SHORTINT", OP_TYPE, 0, -1 }, { "SHR", OP_PLAIN, 1, -1 },
    { "SINGLE", OP_TYPE, 0, -1 }, { "SIZE", OP_PLAIN, 0, -1 }, { "SIZEOF", OP_SIZEOF, 1, -1 },
    { "SLIDER_KIND", OP_PLAIN, 0, -1 }, { "STARTSWITH", OP_PLAIN, 1, -1 }, { "STRING", OP_TYPE, 0, -1 },
    { "STRING_KIND", OP_PLAIN, 0, -1 }, { "STRUCT", OP_STRUCT, 0, -1 }, { "STYLE", OP_PLAIN, 0, -1 },
    { "TASKPROC", OP_PLAIN, 0, -1 }, { "TEXT_KIND", OP_PLAIN, 0, -1 }, { "TRIM$", OP_PLAIN, 1, -1 },
    { "TYPECASE", OP_TYPECASE, 0, -1 }, { "UNTIL", OP_UNTIL, 0, -1 },
};

/** Number of reserved words. */
#define WORD_COUNT ((long)(sizeof(words) / sizeof(words[0])))

const char *const ace_parser_constants[] = { "NULL", "TRUE", "FALSE", NULL };

/** Kinds of tokens of a statement. */
typedef enum TokType {
    T_WORD,     /**< a name or reserved word */
    T_NUMBER,   /**< a number */
    T_STRING,   /**< a string */
    T_ATOM,     /**< an atom literal, \#:name */
    T_OP        /**< an operator or bracket */
} TokType;

/** A token of the current statement. */
typedef struct Tok {
    size_t start;       /**< offset of the first byte */
    size_t end;         /**< offset after the last byte */
    long line;          /**< its line */
    long col;           /**< its column */
    TokType type;       /**< what it is */
    const Word *word;   /**< the reserved word of a T_WORD, or NULL for a name */
    int code;           /**< a T_OP's character, or OP2() of its two */
} Tok;

/** The code of an operator of two characters. */
#define OP2(a, b) (((int)(a) << 8) | (int)(b))

/** Kinds of blocks. */
typedef enum BlockType {
    B_IF,       /**< IF ... END IF */
    B_FOR,      /**< FOR ... NEXT */
    B_WHILE,    /**< WHILE ... WEND */
    B_REPEAT,   /**< REPEAT ... UNTIL */
    B_CASE,     /**< CASE ... END CASE */
    B_TYPECASE, /**< TYPECASE ... END TYPECASE */
    B_BLOCK,    /**< BLOCK ... END BLOCK */
    B_GENERIC,  /**< GENERIC ... END GENERIC */
    B_ASSEM     /**< ASSEM ... END ASSEM */
} BlockType;

/** An open block. */
typedef struct Block {
    BlockType type;     /**< its kind */
    long line;          /**< line of its first word */
    long col;           /**< column of its first word */
    long end;           /**< column after its first word */
} Block;

/** Kinds of definitions. */
typedef enum DefKind {
    DEF_FUNCTION,   /**< a SUB, DEF FN, METHOD or declared function */
    DEF_TYPE,       /**< a STRUCT or CLASS */
    DEF_CONSTANT,   /**< a CONST */
    DEF_VARIABLE,   /**< a variable of the main program */
    DEF_LABEL,      /**< a label */
    DEF_MACRO       /**< a \#define */
} DefKind;

/** A name defined in the source. */
typedef struct Def {
    size_t start;   /**< offset of its name */
    size_t length;  /**< length of its name, without a qualifier */
    size_t full;    /**< length of its name as written */
    DefKind kind;   /**< what it is */
    int declared;   /**< only declared so far */
    long symbol;    /**< index of its symbol */
    long next;      /**< next definition of its hash slot, or -1 */
} Def;

/** A parameter or local variable of the current SUB. */
typedef struct Local {
    size_t start;   /**< offset of its name */
    size_t length;  /**< length of its name */
    int parameter;  /**< it is a parameter */
} Local;

/** The state of one analysis. */
typedef struct Parser {
    AceResult *res;             /**< the result */
    const char *src;            /**< the source */
    size_t length;              /**< its length */
    int is_include;             /**< an include file: its names are seen by others */
    int pass;                   /**< 1 finds definitions, 2 makes tokens */
    int lenient;                /**< no diagnostics about the structure: \#else or too deep */
    size_t pos;                 /**< where the lexer stands */
    size_t line_start;          /**< offset of the current line */
    long line;                  /**< the current line */
    long statements;            /**< statements read, for the stop check */
    long symbol_cap;            /**< room in AceResult::symbols */
    long diagnostic_cap;        /**< room in AceResult::diagnostics */
    long reference_cap;         /**< room in AceResult::references */
    long token_cap;             /**< room in AceResult::tokens, in values */
    Tok *toks;                  /**< the tokens of the current statement */
    long tok_count;             /**< number of them */
    long tok_cap;               /**< room in toks */
    int line_fresh;             /**< no statement has been read on this line */
    int first_on_line;          /**< the statement is the first of its line */
    int ended_by_colon;         /**< the statement ended at a ':', not at the line end */
    int single_if;              /**< the line has a one-line IF: an ELSE belongs to it */
    int in_assem;               /**< the lines up to END ASSEM are not BASIC */
    long brackets[MAX_BRACKETS];/**< tokens of the open brackets */
    long bracket_depth;         /**< number of open brackets */
    Block blocks[MAX_BLOCKS];   /**< the open blocks */
    long block_count;           /**< number of them */
    Def *defs;                  /**< the definitions */
    long def_count;             /**< number of them */
    long def_cap;               /**< room in defs */
    long *def_slots;            /**< hash table of defs: first index, -1 */
    Local locals[MAX_LOCALS];   /**< the current SUB's parameters and variables */
    long local_count;           /**< number of them */
    int reset_locals;           /**< forget the locals before the next statement */
    Op sub_op;                  /**< OP_SUB or OP_METHOD inside one, otherwise OP_PLAIN */
    long sub_symbol;            /**< its symbol, or -1 */
    AceRange sub_head;          /**< its name */
    Op struct_op;               /**< OP_STRUCT or OP_CLASS inside one, otherwise OP_PLAIN */
    long struct_symbol;         /**< its symbol, or -1 */
    AceRange struct_head;       /**< its name */
    long pp_depth;              /**< open \#ifdefs */
    AceRange pp_open[MAX_CONDITIONALS]; /**< where they are */
} Parser;

/**
 * @brief Fold a byte to lower case (ASCII: names of ACE are ASCII).
 * @param c The byte.
 * @return It in lower case.
 */
static unsigned char lower(unsigned char c)
{
    return c >= 'A' && c <= 'Z' ? (unsigned char)(c + 32) : c;
}

/**
 * @brief The hash of a name, ignoring case.
 * @param text The name.
 * @param len Its length.
 * @return The hash.
 */
static unsigned long hash_name(const char *text, size_t len)
{
    unsigned long h = 5381;
    size_t i;
    for (i = 0; i < len; i++) h = h * 33 + lower((unsigned char)text[i]);
    return h;
}

/**
 * @brief Compare two names of the same length, ignoring case.
 * @param a The first.
 * @param b The second.
 * @param len Their length.
 * @return Non-zero if they are equal.
 */
static int equal_fold(const char *a, const char *b, size_t len)
{
    size_t i;
    for (i = 0; i < len; i++) {
        if (lower((unsigned char)a[i]) != lower((unsigned char)b[i])) return 0;
    }
    return 1;
}

/**
 * @brief The length of a name without its type qualifier.
 * @param text The name.
 * @param len Its length.
 * @return Its length without a trailing % & ! \# or $.
 */
static size_t base_length(const char *text, size_t len)
{
    if (len > 1 && strchr("%&!#$", text[len - 1]) != NULL) return len - 1;
    return len;
}

/** The hash table of the reserved words. */
static short word_slots[WORD_SLOTS];
/** The table has been made. */
static int words_ready = 0;

/**
 * @brief Make the hash table of the reserved words, once.
 */
static void make_word_table(void)
{
    long i;
    if (words_ready) return;
    for (i = 0; i < WORD_SLOTS; i++) word_slots[i] = -1;
    for (i = 0; i < WORD_COUNT; i++) {
        unsigned long h = hash_name(words[i].name, strlen(words[i].name)) % WORD_SLOTS;
        words[i].next = word_slots[h];
        word_slots[h] = (short)i;
    }
    words_ready = 1;
}

/**
 * @brief Find a reserved word, in any case.
 * @param text The text.
 * @param len Its length.
 * @return The word, or NULL.
 */
static const Word *find_word(const char *text, size_t len)
{
    short i = word_slots[hash_name(text, len) % WORD_SLOTS];
    while (i >= 0) {
        const Word *w = &words[i];
        if (strlen(w->name) == len && equal_fold(w->name, text, len)) return w;
        i = w->next;
    }
    return NULL;
}

const char *ace_parser_word_name(int word_class, long index)
{
    long i;
    for (i = 0; i < WORD_COUNT; i++) {
        int c = words[i].op == OP_TYPE ? ACE_WORD_TYPE : words[i].function ? ACE_WORD_FUNCTION : ACE_WORD_KEYWORD;
        if (c != word_class) continue;
        if (index-- == 0) return words[i].name;
    }
    return NULL;
}

/**
 * @brief Make room in an array.
 * @param P The parser.
 * @param array The array.
 * @param count Its elements.
 * @param cap Its room.
 * @param size The size of an element.
 * @return Non-zero if there is room; otherwise the result is marked out of
 *         memory.
 */
static int grow(Parser *P, void **array, long count, long *cap, size_t size)
{
    void *bigger;
    long room;
    if (count < *cap) return 1;
    room = *cap ? *cap * 2 : 32;
    bigger = realloc(*array, (size_t)room * size);
    if (!bigger) {
        P->res->out_of_memory = 1;
        return 0;
    }
    *array = bigger;
    *cap = room;
    return 1;
}

/**
 * @brief Copy a text.
 * @param P The parser.
 * @param text The text.
 * @param len Its length.
 * @return The copy (malloc()), or NULL if out of memory.
 */
static char *dup_text(Parser *P, const char *text, size_t len)
{
    char *copy = (char *)malloc(len + 1);
    if (!copy) {
        P->res->out_of_memory = 1;
        return NULL;
    }
    memcpy(copy, text, len);
    copy[len] = '\0';
    return copy;
}

/**
 * @brief Add a diagnostic, in the first pass only.
 * @param P The parser.
 * @param severity Its SCOUT_SEVERITY_ value.
 * @param line Its line.
 * @param col Its first column.
 * @param end The column after it.
 * @param format The message, with at most one %s.
 * @param name What replaces the %s (may be NULL).
 * @param len Its length.
 */
static void add_diagnostic(Parser *P, int severity, long line, long col, long end, const char *format,
                           const char *name, size_t len)
{
    AceResult *res = P->res;
    const char *hole = name ? strstr(format, "%s") : NULL;
    size_t flen = strlen(format);
    AceDiagnostic *d;
    char *msg;
    if (P->pass != 1 || res->diagnostic_count >= MAX_DIAGNOSTICS) return;
    if (!grow(P, (void **)&res->diagnostics, res->diagnostic_count, &P->diagnostic_cap, sizeof(AceDiagnostic))) return;
    msg = (char *)malloc(flen + len + 1);
    if (!msg) {
        res->out_of_memory = 1;
        return;
    }
    if (hole) {
        size_t head = (size_t)(hole - format);
        memcpy(msg, format, head);
        memcpy(msg + head, name, len);
        strcpy(msg + head + len, hole + 2);
    } else {
        strcpy(msg, format);
    }
    d = &res->diagnostics[res->diagnostic_count++];
    d->message = msg;
    d->severity = severity;
    d->range.start_line = line;
    d->range.start_col = col;
    d->range.end_line = line;
    d->range.end_col = end;
}

/**
 * @brief Report a problem of the structure, unless the source has
 *        branches of conditionals that are both read.
 * @param P The parser.
 * @param t Where it is.
 * @param format The message, with at most one %s.
 * @param name What replaces the %s (NUL-terminated, may be NULL).
 */
static void structure_error(Parser *P, const Tok *t, const char *format, const char *name)
{
    if (P->lenient) return;
    add_diagnostic(P, SCOUT_SEVERITY_ERROR, t->line, t->col, t->col + (long)(t->end - t->start), format,
                   name, name ? strlen(name) : 0);
}

/**
 * @brief Report a problem of the structure at a remembered place.
 * @param P The parser.
 * @param r Where it is.
 * @param format The message, with at most one %s.
 * @param name What replaces the %s (NUL-terminated, may be NULL).
 */
static void structure_error_at(Parser *P, const AceRange *r, const char *format, const char *name)
{
    if (P->lenient) return;
    add_diagnostic(P, SCOUT_SEVERITY_ERROR, r->start_line, r->start_col, r->end_col, format,
                   name, name ? strlen(name) : 0);
}

/**
 * @brief Report that a name is defined twice.
 * @param P The parser.
 * @param t The second definition.
 */
static void defined_twice(Parser *P, const Tok *t)
{
    if (P->pp_depth > 0 || P->lenient) return;
    add_diagnostic(P, SCOUT_SEVERITY_ERROR, t->line, t->col, t->col + (long)(t->end - t->start),
                   "'%s' is defined twice", P->src + t->start, t->end - t->start);
}

/**
 * @brief Add a highlighting token, in the second pass.
 * @param P The parser.
 * @param t The token.
 * @param kind Its SCOUT_TOKEN_ kind.
 */
static void add_token(Parser *P, const Tok *t, long kind)
{
    AceResult *res = P->res;
    long n = res->token_count * 4;
    long *v;
    if (n + 4 > P->token_cap) {
        long cap = P->token_cap ? P->token_cap * 2 : 256;
        long *bigger = (long *)realloc(res->tokens, (size_t)cap * sizeof(long));
        if (!bigger) {
            res->out_of_memory = 1;
            return;
        }
        res->tokens = bigger;
        P->token_cap = cap;
    }
    v = res->tokens + n;
    v[0] = t->line;
    v[1] = t->col;
    v[2] = (long)(t->end - t->start);
    v[3] = kind;
    res->token_count++;
}

/**
 * @brief The range of a token.
 * @param t The token.
 * @param[out] r Its range.
 */
static void tok_range(const Tok *t, AceRange *r)
{
    r->start_line = t->line;
    r->start_col = t->col;
    r->end_line = t->line;
    r->end_col = t->col + (long)(t->end - t->start);
}

/**
 * @brief Add a symbol, in the first pass only.
 * @param P The parser.
 * @param name Its name.
 * @param len The length of the name.
 * @param kind Its SCOUT_SYMBOL_ kind.
 * @param scope Where it can be seen.
 * @param parent Index of its parent, or -1.
 * @param detail Its detail (malloc(), taken over; may be NULL).
 * @param range The definition.
 * @param selection Its name.
 * @return Its index, or -1.
 */
static long add_symbol(Parser *P, const char *name, size_t len, int kind, AceScope scope, long parent,
                       char *detail, const AceRange *range, const AceRange *selection)
{
    AceResult *res = P->res;
    AceSymbol *s;
    char *copy;
    if (P->pass != 1 ||
        !grow(P, (void **)&res->symbols, res->symbol_count, &P->symbol_cap, sizeof(AceSymbol)) ||
        (copy = dup_text(P, name, len)) == NULL) {
        free(detail);
        return -1;
    }
    s = &res->symbols[res->symbol_count];
    s->name = copy;
    s->detail = detail;
    s->kind = kind;
    s->scope = scope;
    s->parent = parent;
    s->range = *range;
    s->selection = *selection;
    s->declaration = 0;
    return res->symbol_count++;
}

/**
 * @brief Add a symbol named by a token.
 * @param P The parser.
 * @param t The token of its name.
 * @param kind Its SCOUT_SYMBOL_ kind.
 * @param scope Where it can be seen.
 * @param parent Index of its parent, or -1.
 * @param detail Its detail (malloc(), taken over; may be NULL).
 * @return Its index, or -1.
 */
static long add_tok_symbol(Parser *P, const Tok *t, int kind, AceScope scope, long parent, char *detail)
{
    AceRange r;
    tok_range(t, &r);
    return add_symbol(P, P->src + t->start, t->end - t->start, kind, scope, parent, detail, &r, &r);
}

/**
 * @brief The source between two tokens as a symbol's detail.
 * @param P The parser.
 * @param from The first token.
 * @param to The last token.
 * @return The text (malloc()); NULL in the second pass and if it is long
 *         or has more than one line.
 */
static char *slice_detail(Parser *P, const Tok *from, const Tok *to)
{
    if (P->pass != 1 || from->line != to->line || to->end <= from->start ||
        to->end - from->start > MAX_DETAIL) {
        return NULL;
    }
    return dup_text(P, P->src + from->start, to->end - from->start);
}

/**
 * @brief Find a definition; a type qualifier makes no difference.
 * @param P The parser.
 * @param text Its name.
 * @param len The length of the name.
 * @return The definition, or NULL.
 */
static Def *find_def(Parser *P, const char *text, size_t len)
{
    size_t base = base_length(text, len);
    long i = P->def_slots[hash_name(text, base) % DEF_SLOTS];
    while (i >= 0) {
        Def *d = &P->defs[i];
        if (d->length == base && equal_fold(P->src + d->start, text, base)) return d;
        i = d->next;
    }
    return NULL;
}

/**
 * @brief Find a definition with the very same name: SUB a$ and SUB a& are two.
 * @param P The parser.
 * @param t The token of the name.
 * @return The definition, or NULL.
 */
static Def *find_same(Parser *P, const Tok *t)
{
    size_t len = t->end - t->start, base = base_length(P->src + t->start, len);
    long i = P->def_slots[hash_name(P->src + t->start, base) % DEF_SLOTS];
    while (i >= 0) {
        Def *d = &P->defs[i];
        if (d->full == len && equal_fold(P->src + d->start, P->src + t->start, len)) return d;
        i = d->next;
    }
    return NULL;
}

/**
 * @brief Remember a definition, in the first pass only.
 * @param P The parser.
 * @param t The token of its name.
 * @param kind What it is.
 * @param symbol Index of its symbol.
 * @param declared It is only declared.
 */
static void add_def(Parser *P, const Tok *t, DefKind kind, long symbol, int declared)
{
    size_t base;
    unsigned long h;
    Def *d;
    if (P->pass != 1 || !grow(P, (void **)&P->defs, P->def_count, &P->def_cap, sizeof(Def))) return;
    base = base_length(P->src + t->start, t->end - t->start);
    h = hash_name(P->src + t->start, base) % DEF_SLOTS;
    d = &P->defs[P->def_count];
    d->start = t->start;
    d->length = base;
    d->full = t->end - t->start;
    d->kind = kind;
    d->declared = declared;
    d->symbol = symbol;
    d->next = P->def_slots[h];
    P->def_slots[h] = P->def_count++;
}

/**
 * @brief Find a parameter or local variable of the current SUB.
 * @param P The parser.
 * @param text Its name.
 * @param len The length of the name.
 * @return It, or NULL.
 */
static const Local *find_local(const Parser *P, const char *text, size_t len)
{
    long i;
    for (i = 0; i < P->local_count; i++) {
        const Local *l = &P->locals[i];
        if (l->length == len && equal_fold(P->src + l->start, text, len)) return l;
    }
    return NULL;
}

/**
 * @brief Remember a parameter or local variable.
 * @param P The parser.
 * @param t The token of its name.
 * @param parameter It is a parameter.
 */
static void add_local(Parser *P, const Tok *t, int parameter)
{
    Local *l;
    if (P->local_count >= MAX_LOCALS || find_local(P, P->src + t->start, t->end - t->start)) return;
    l = &P->locals[P->local_count++];
    l->start = t->start;
    l->length = t->end - t->start;
    l->parameter = parameter;
}

/**
 * @brief Tell whether a token is a name (a word and not reserved).
 * @param t The token.
 * @return Non-zero if it is.
 */
static int is_name(const Tok *t)
{
    return t->type == T_WORD && !t->word;
}

/**
 * @brief Tell whether a token is a certain operator.
 * @param t The token.
 * @param code The operator's code.
 * @return Non-zero if it is.
 */
static int is_op(const Tok *t, int code)
{
    return t->type == T_OP && t->code == code;
}

/**
 * @brief Tell whether a token is a reserved word of a certain role.
 * @param t The token.
 * @param op The role.
 * @return Non-zero if it is.
 */
static int is_kw(const Tok *t, Op op)
{
    return t->type == T_WORD && t->word && t->word->op == op;
}

/**
 * @brief Tell whether a word can name a SUB; sources define words of newer
 *        compilers (ASSERT) or replace built-in ones (LCASE$).
 * @param t The token.
 * @return Non-zero if it can.
 */
static int is_head_name(const Tok *t)
{
    return t->type == T_WORD && (!t->word || t->word->op == OP_PLAIN);
}

/**
 * @brief Tell whether a token opens a bracket.
 * @param t The token.
 * @return Non-zero if it does.
 */
static int is_opener(const Tok *t)
{
    return is_op(t, '(') || is_op(t, '[');
}

/**
 * @brief Tell whether a token closes a bracket.
 * @param t The token.
 * @return Non-zero if it does.
 */
static int is_closer(const Tok *t)
{
    return is_op(t, ')') || is_op(t, ']');
}

/**
 * @brief Tell whether a byte can start a name.
 * @param c The byte.
 * @return Non-zero if it can.
 */
static int is_name_start(unsigned char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

/**
 * @brief Tell whether a byte is a decimal digit.
 * @param c The byte.
 * @return Non-zero if it is.
 */
static int is_digit(unsigned char c)
{
    return c >= '0' && c <= '9';
}

/**
 * @brief Tell whether a byte can be part of a name.
 * @param c The byte.
 * @return Non-zero if it can.
 */
static int is_name_part(unsigned char c)
{
    return is_name_start(c) || is_digit(c) || c == '.';
}

/**
 * @brief Add a token to the statement.
 * @param P The parser.
 * @param start Its first byte.
 * @param end The offset after it.
 * @param type What it is.
 * @param code A T_OP's code.
 * @return The token, or NULL if out of memory.
 */
static Tok *push_tok(Parser *P, size_t start, size_t end, TokType type, int code)
{
    Tok *t;
    if (!grow(P, (void **)&P->toks, P->tok_count, &P->tok_cap, sizeof(Tok))) return NULL;
    t = &P->toks[P->tok_count++];
    t->start = start;
    t->end = end;
    t->line = P->line;
    t->col = (long)(start - P->line_start) + 1;
    t->type = type;
    t->word = NULL;
    t->code = code;
    return t;
}

/**
 * @brief Skip a block comment: { } as ACE's lexer reads it, which does not
 *        nest, or slash-star, which ACE's preprocessor nests.
 * @param P The parser, standing on the comment.
 * @param open The first character of the delimiter that opens it.
 */
static void skip_block_comment(Parser *P, char open)
{
    long depth = open == '{' ? 1 : 0, line = P->line, col = (long)(P->pos - P->line_start) + 1;
    while (P->pos < P->length) {
        char c = P->src[P->pos];
        char d = P->pos + 1 < P->length ? P->src[P->pos + 1] : '\0';
        if (open == '{' && c == '}') {
            P->pos++;
            return;
        } else if (open == '/' && c == '/' && d == '*') {
            depth++;
            P->pos += 2;
        } else if (open == '/' && c == '*' && d == '/') {
            P->pos += 2;
            if (--depth == 0) return;
        } else {
            P->pos++;
            if (c == '\n') {
                P->line++;
                P->line_start = P->pos;
            }
        }
    }
    add_diagnostic(P, SCOUT_SEVERITY_ERROR, line, col, col + 1, "comment not closed", NULL, 0);
}

/**
 * @brief Skip to the end of the line, leaving the line end.
 * @param P The parser.
 */
static void skip_line(Parser *P)
{
    while (P->pos < P->length && P->src[P->pos] != '\n') P->pos++;
}

/**
 * @brief Read a string, which ends at its quote or at the line end.
 * @param P The parser, standing on the quote.
 */
static void read_string(Parser *P)
{
    size_t start = P->pos++;
    int closed = 0;
    while (P->pos < P->length && P->src[P->pos] != '\n') {
        if (P->src[P->pos++] == '"') {
            closed = 1;
            break;
        }
    }
    if (!push_tok(P, start, P->pos, T_STRING, 0)) return;
    if (!closed) {
        long col = (long)(start - P->line_start) + 1;
        add_diagnostic(P, SCOUT_SEVERITY_WARNING, P->line, col, col + 1, "string not closed", NULL, 0);
    }
}

/**
 * @brief Read a number: decimal or float, &H hex or &O octal, with a
 *        qualifier.
 * @param P The parser, standing on it.
 */
static void read_number(Parser *P)
{
    size_t start = P->pos;
    if (P->src[P->pos] == '&') {
        P->pos += 2;
        while (P->pos < P->length && is_name_part((unsigned char)P->src[P->pos])) P->pos++;
    } else {
        while (P->pos < P->length && (is_digit((unsigned char)P->src[P->pos]) || P->src[P->pos] == '.')) P->pos++;
        if (P->pos < P->length && lower((unsigned char)P->src[P->pos]) == 'e' && P->pos + 1 < P->length &&
            (is_digit((unsigned char)P->src[P->pos + 1]) || P->src[P->pos + 1] == '+' || P->src[P->pos + 1] == '-')) {
            P->pos += 2;
            while (P->pos < P->length && is_digit((unsigned char)P->src[P->pos])) P->pos++;
        }
    }
    if (P->pos < P->length && strchr("%&!#", P->src[P->pos]) != NULL && P->src[P->pos] != '\0') {
        char d = P->pos + 1 < P->length ? P->src[P->pos + 1] : '\0';
        /* 1&H10 is no number with a qualifier, but nobody writes that */
        if (!is_name_part((unsigned char)d)) P->pos++;
    }
    push_tok(P, start, P->pos, T_NUMBER, 0);
}

/**
 * @brief Read a name or reserved word with its qualifier; REM is a comment.
 * @param P The parser, standing on it.
 */
static void read_word(Parser *P)
{
    size_t start = P->pos;
    Tok *t;
    while (P->pos < P->length && is_name_part((unsigned char)P->src[P->pos])) P->pos++;
    if (P->pos < P->length && P->src[P->pos] != '\0' && strchr("%&!$#", P->src[P->pos]) != NULL) {
        char q = P->src[P->pos];
        char d = P->pos + 1 < P->length ? P->src[P->pos + 1] : '\0';
        /* PRINT#1 and CLOSE#1: a '#' before a number is a file number */
        if (q != '#' || (!is_digit((unsigned char)d) && !is_name_start((unsigned char)d) && d != ':')) P->pos++;
    }
    t = push_tok(P, start, P->pos, T_WORD, 0);
    if (!t) return;
    t->word = find_word(P->src + start, P->pos - start);
    if (t->word && t->word->op == OP_REM) {
        P->tok_count--;
        skip_line(P);
    }
}

/**
 * @brief Tell whether a statement starts with DATA, whose values can be
 *        any text.
 * @param P The parser.
 * @return Non-zero if it does.
 */
static int is_data(const Parser *P)
{
    return P->tok_count > 0 && is_kw(&P->toks[0], OP_DATA);
}

/**
 * @brief Read an operator of one or two characters, and follow brackets.
 * @param P The parser, standing on it.
 */
static void read_operator(Parser *P)
{
    static const char twos[][3] = { "->", "++", "--", "<>", "<=", ">=" };
    size_t start = P->pos, i;
    char c = P->src[P->pos];
    char d = P->pos + 1 < P->length ? P->src[P->pos + 1] : '\0';
    int code = (unsigned char)c;
    Tok *t;
    for (i = 0; i < sizeof(twos) / sizeof(twos[0]); i++) {
        if (twos[i][0] == c && twos[i][1] == d) {
            code = OP2(c, d);
            break;
        }
    }
    P->pos += code > 255 ? 2 : 1;
    t = push_tok(P, start, P->pos, T_OP, code);
    if (!t) return;
    if (is_opener(t)) {
        if (P->bracket_depth < MAX_BRACKETS) P->brackets[P->bracket_depth] = P->tok_count - 1;
        P->bracket_depth++;
    } else if (is_closer(t)) {
        if (P->bracket_depth == 0) {
            char text[2];
            text[0] = c;
            text[1] = '\0';
            if (!is_data(P)) structure_error(P, t, "'%s' without an opening bracket", text);
        } else {
            P->bracket_depth--;
        }
    }
}

/**
 * @brief Tell whether only blanks stand up to the end of the line.
 * @param P The parser.
 * @param i Where to start looking.
 * @return Non-zero if they do.
 */
static int ends_line(const Parser *P, size_t i)
{
    while (i < P->length && (P->src[i] == ' ' || P->src[i] == '\t' || P->src[i] == '\r')) i++;
    return i >= P->length || P->src[i] == '\n';
}

/**
 * @brief Add the file of an \#include to the references.
 * @param P The parser.
 * @param i Where the name's delimiter stands.
 * @param end The end of the line.
 * @param line_start The offset of the line.
 */
static void add_include(Parser *P, size_t i, size_t end, size_t line_start)
{
    AceResult *res = P->res;
    char close;
    size_t name;
    AceReference *r;
    char *copy;
    int system;
    if (P->pass != 1 || i >= end || (P->src[i] != '"' && P->src[i] != '<')) return;
    system = P->src[i] == '<';
    close = system ? '>' : '"';
    name = i + 1;
    i = name;
    while (i < end && P->src[i] != close) i++;
    if (i >= end || i == name) return;
    if (!grow(P, (void **)&res->references, res->reference_count, &P->reference_cap, sizeof(AceReference))) return;
    copy = dup_text(P, P->src + name, i - name);
    if (!copy) return;
    r = &res->references[res->reference_count++];
    r->target = copy;
    r->system = system;
    r->range.start_line = P->line;
    r->range.start_col = (long)(name - line_start);
    r->range.end_line = P->line;
    r->range.end_col = (long)(i - line_start) + 2;
}

/**
 * @brief Handle a preprocessor line; the line end stays for the caller.
 * @param P The parser, standing on the '#'.
 */
static void preprocessor(Parser *P)
{
    size_t i = P->pos + 1, word, nlen, end, after;
    Tok name;
    skip_line(P);
    end = P->pos;
    while (i < end && (P->src[i] == ' ' || P->src[i] == '\t')) i++;
    word = i;
    while (i < end && is_name_part((unsigned char)P->src[i])) i++;
    nlen = i - word;
    after = i;
    memset(&name, 0, sizeof(name));
    name.line = P->line;
    name.col = (long)(word - P->line_start) + 1;
    name.start = word;
    name.end = i;

    if (nlen == 7 && equal_fold(P->src + word, "include", 7)) {
        while (i < end && (P->src[i] == ' ' || P->src[i] == '\t')) i++;
        add_include(P, i, end, P->line_start);
    } else if (nlen == 6 && equal_fold(P->src + word, "define", 6)) {
        size_t n;
        while (i < end && (P->src[i] == ' ' || P->src[i] == '\t')) i++;
        n = i;
        while (i < end && is_name_part((unsigned char)P->src[i])) i++;
        if (i > n) {
            long sym;
            name.col = (long)(n - P->line_start) + 1;
            name.start = n;
            name.end = i;
            sym = add_tok_symbol(P, &name, SCOUT_SYMBOL_MACRO, P->is_include ? ACE_SCOPE_GLOBAL : ACE_SCOPE_FILE, -1, NULL);
            add_def(P, &name, DEF_MACRO, sym, 0);
        }
    } else if ((nlen == 5 && equal_fold(P->src + word, "ifdef", 5)) ||
               (nlen == 6 && equal_fold(P->src + word, "ifndef", 6))) {
        if (P->pp_depth < MAX_CONDITIONALS) tok_range(&name, &P->pp_open[P->pp_depth]);
        P->pp_depth++;
    } else if (nlen == 5 && equal_fold(P->src + word, "endif", 5)) {
        if (P->pp_depth == 0) {
            add_diagnostic(P, SCOUT_SEVERITY_ERROR, name.line, name.col, name.col + 5, "#endif without #ifdef", NULL, 0);
        } else {
            P->pp_depth--;
        }
    } else if (nlen == 4 && equal_fold(P->src + word, "else", 4)) {
        if (P->pp_depth == 0) {
            add_diagnostic(P, SCOUT_SEVERITY_ERROR, name.line, name.col, name.col + 4, "#else without #ifdef", NULL, 0);
        }
    }

    /* the rest of the line: a comment may go on over several lines, and a
     * backslash at the end continues the directive */
    P->pos = after;
    while (P->pos < P->length && P->src[P->pos] != '\n') {
        char c = P->src[P->pos];
        if (c == '/' && P->pos + 1 < P->length && P->src[P->pos + 1] == '*') {
            skip_block_comment(P, '/');
        } else if (c == '"') {
            P->pos++;
            while (P->pos < P->length && P->src[P->pos] != '\n' && P->src[P->pos] != '"') P->pos++;
            if (P->pos < P->length && P->src[P->pos] == '"') P->pos++;
        } else if (c == '\\' && ends_line(P, P->pos + 1)) {
            skip_line(P);
            if (P->pos < P->length) {
                P->pos++;
                P->line++;
                P->line_start = P->pos;
            }
        } else {
            P->pos++;
        }
    }
}

/**
 * @brief Pass over the lines of inline assembly, up to END ASSEM.
 * @param P The parser, at the start of a line.
 */
static void skip_assembly(Parser *P)
{
    while (P->pos < P->length) {
        size_t i = P->pos;
        const char *s = P->src;
        while (i < P->length && (s[i] == ' ' || s[i] == '\t')) i++;
        if (P->length - i >= 3 && equal_fold(s + i, "end", 3)) {
            i += 3;
            while (i < P->length && (s[i] == ' ' || s[i] == '\t')) i++;
            if (P->length - i >= 5 && equal_fold(s + i, "assem", 5)) {
                P->in_assem = 0;
                return;
            }
        }
        skip_line(P);
        if (P->pos < P->length) {
            P->pos++;
            P->line++;
            P->line_start = P->pos;
        }
    }
}

/**
 * @brief Read the next statement into the tokens.
 *
 * A statement ends at a ':' or at the line end. A '~' is white space; at the
 * end of a line it, or a backslash (the include files use that, as ACE's
 * preprocessor joins such lines), continues the statement in the next.
 *
 * @param P The parser.
 * @return Non-zero if there is one; 0 at the end of the source, or when
 *         the analysis has to stop.
 */
static int read_statement(Parser *P)
{
    int tilde = 0;
    P->tok_count = 0;
    P->bracket_depth = 0;
    P->ended_by_colon = 0;
    if (++P->statements % STOP_INTERVAL == 0 && ace_parser_should_stop && ace_parser_should_stop()) {
        P->res->stopped = 1;
        return 0;
    }
    if (P->in_assem) {
        /* the rest of ASSEM's line, then the code */
        skip_line(P);
        if (P->pos < P->length) {
            P->pos++;
            P->line++;
            P->line_start = P->pos;
        }
        skip_assembly(P);
        P->line_fresh = 1;
    }
    P->first_on_line = P->line_fresh;
    while (P->pos < P->length) {
        char c = P->src[P->pos];
        char d = P->pos + 1 < P->length ? P->src[P->pos + 1] : '\0';
        if (P->res->out_of_memory) return 0;
        if (c == '\n') {
            P->pos++;
            P->line++;
            P->line_start = P->pos;
            if (tilde) {
                tilde = 0;
                continue;
            }
            P->line_fresh = 1;
            if (P->tok_count == 0) {
                P->first_on_line = 1;
                continue;
            }
            break;
        }
        if (c == ' ' || c == '\t' || c == '\r' || c == '\f') {
            P->pos++;
            continue;
        }
        if (c == '~') {
            P->pos++;
            tilde = 1;
            continue;
        }
        if (c == '\\' && ends_line(P, P->pos + 1)) {
            P->pos++;
            tilde = 1;
            continue;
        }
        tilde = 0;
        if (c == ':') {
            P->pos++;
            if (d == '=') {
                push_tok(P, P->pos - 1, P->pos + 1, T_OP, OP2(':', '='));
                P->pos++;
                continue;
            }
            P->line_fresh = 0;
            if (P->tok_count > 0) {
                P->ended_by_colon = 1;
                break;
            }
            P->first_on_line = 0;
        } else if (c == '\'') {
            skip_line(P);
        } else if (c == '{' || (c == '/' && d == '*')) {
            skip_block_comment(P, c);
        } else if (c == '#' && d == ':' && P->pos + 2 < P->length && is_name_start((unsigned char)P->src[P->pos + 2])) {
            size_t start = P->pos;
            P->pos += 2;
            while (P->pos < P->length && is_name_part((unsigned char)P->src[P->pos])) P->pos++;
            push_tok(P, start, P->pos, T_ATOM, 0);
        } else if (c == '#' && P->tok_count == 0 && P->first_on_line) {
            preprocessor(P);
        } else if (c == '"') {
            read_string(P);
        } else if (is_digit((unsigned char)c) || (c == '.' && is_digit((unsigned char)d)) ||
                   (c == '&' && (lower((unsigned char)d) == 'h' || lower((unsigned char)d) == 'o'))) {
            read_number(P);
        } else if (is_name_start((unsigned char)c)) {
            read_word(P);
        } else {
            read_operator(P);
        }
    }
    if (P->tok_count == 0) return 0;
    if (P->bracket_depth > 0 && P->bracket_depth <= MAX_BRACKETS && !is_data(P)) {
        const Tok *t = &P->toks[P->brackets[P->bracket_depth - 1]];
        char text[2];
        text[0] = P->src[t->start];
        text[1] = '\0';
        structure_error(P, t, "'%s' not closed", text);
    }
    return 1;
}

/**
 * @brief The scope of a definition outside of SUBs and STRUCTs.
 * @param P The parser.
 * @param external It is marked to be seen by other modules.
 * @return Its scope.
 */
static AceScope outer_scope(const Parser *P, int external)
{
    return external || P->is_include ? ACE_SCOPE_GLOBAL : ACE_SCOPE_FILE;
}

/**
 * @brief Open a block.
 * @param P The parser.
 * @param type Its kind.
 * @param t Its first word.
 */
static void open_block(Parser *P, BlockType type, const Tok *t)
{
    Block *b;
    if (P->block_count >= MAX_BLOCKS) {
        P->lenient = 1;
        return;
    }
    b = &P->blocks[P->block_count++];
    b->type = type;
    b->line = t->line;
    b->col = t->col;
    b->end = t->col + (long)(t->end - t->start);
}

/**
 * @brief Report an open block as not closed.
 * @param P The parser.
 * @param b The block.
 */
static void report_open_block(Parser *P, const Block *b)
{
    static const char *const texts[] = {
        "IF without END IF", "FOR without NEXT", "WHILE without WEND", "REPEAT without UNTIL",
        "CASE without END CASE", "TYPECASE without END TYPECASE", "BLOCK without END BLOCK",
        "GENERIC without END GENERIC", "ASSEM without END ASSEM"
    };
    if (P->lenient) return;
    add_diagnostic(P, SCOUT_SEVERITY_ERROR, b->line, b->col, b->end, texts[b->type], NULL, 0);
}

/**
 * @brief Report and forget all open blocks.
 * @param P The parser.
 */
static void close_all_blocks(Parser *P)
{
    long i;
    for (i = 0; i < P->block_count; i++) report_open_block(P, &P->blocks[i]);
    P->block_count = 0;
}

/**
 * @brief Close a block or go on in it.
 *
 * Blocks opened inside it and not closed are reported. If there is no such
 * block at all, the word is reported and the blocks stay.
 *
 * @param P The parser.
 * @param type The kind of block the word belongs to.
 * @param t The word.
 * @param format The message for a word without its block.
 * @param keep Leave the block open (ELSE, ELSEIF).
 */
static void close_block(Parser *P, BlockType type, const Tok *t, const char *format, int keep)
{
    long i, k;
    for (i = P->block_count - 1; i >= 0; i--) {
        if (P->blocks[i].type == type) break;
    }
    if (i < 0) {
        structure_error(P, t, format, NULL);
        return;
    }
    for (k = i + 1; k < P->block_count; k++) report_open_block(P, &P->blocks[k]);
    P->block_count = keep ? i + 1 : i;
}

/**
 * @brief Tell whether the innermost block is a kind of CASE, where a
 *        word and a colon are a branch and not a label.
 * @param P The parser.
 * @return Non-zero if it is.
 */
static int in_case(const Parser *P)
{
    return P->block_count > 0 && (P->blocks[P->block_count - 1].type == B_CASE ||
                                  P->blocks[P->block_count - 1].type == B_TYPECASE);
}

/**
 * @brief Find a reserved word outside of brackets.
 * @param P The parser.
 * @param from The first token.
 * @param op The word's role.
 * @return Its index, or -1.
 */
static long find_keyword(const Parser *P, long from, Op op)
{
    long i, depth = 0;
    for (i = from; i < P->tok_count; i++) {
        const Tok *t = &P->toks[i];
        if (is_opener(t)) depth++;
        else if (is_closer(t)) depth--;
        else if (depth <= 0 && is_kw(t, op)) return i;
    }
    return -1;
}

/**
 * @brief Find the bracket that closes one.
 * @param P The parser.
 * @param open The index of the opening bracket.
 * @return The index of the closing one, or -1.
 */
static long matching_bracket(const Parser *P, long open)
{
    long k, depth = 0;
    for (k = open; k < P->tok_count; k++) {
        if (is_opener(&P->toks[k])) depth++;
        else if (is_closer(&P->toks[k]) && --depth == 0) return k;
    }
    return -1;
}

/**
 * @brief Read a list of variables, each with an optional type before it.
 * @param P The parser.
 * @param i The first token of the list.
 * @param type The type of them all (may be NULL).
 */
static void read_variables(Parser *P, long i, const Tok *type)
{
    long n = P->tok_count;
    while (i < n) {
        const Tok *own = type;
        long depth = 0;
        if (is_kw(&P->toks[i], OP_TYPE)) own = &P->toks[i++];
        if (i < n && is_op(&P->toks[i], '*')) i++;
        if (i < n && is_name(&P->toks[i])) {
            const Tok *name = &P->toks[i];
            char *detail = own ? slice_detail(P, own, own) : NULL;
            if (P->sub_op != OP_PLAIN) {
                add_local(P, name, 0);
                add_tok_symbol(P, name, SCOUT_SYMBOL_VARIABLE, ACE_SCOPE_LOCAL, P->sub_symbol, detail);
            } else {
                long sym = add_tok_symbol(P, name, SCOUT_SYMBOL_VARIABLE, outer_scope(P, 0), -1, detail);
                add_def(P, name, DEF_VARIABLE, sym, 0);
            }
        }
        /* dimensions, SIZE, an initial value: up to the next comma */
        for (; i < n; i++) {
            const Tok *t = &P->toks[i];
            if (is_opener(t)) depth++;
            else if (is_closer(t)) depth--;
            else if (depth == 0 && is_op(t, ',')) {
                i++;
                break;
            }
        }
    }
}

/**
 * @brief Read the constants of a CONST statement.
 * @param P The parser.
 * @param i The token after the keyword.
 */
static void read_constants(Parser *P, long i)
{
    long depth = 0;
    int expect = 1;
    for (; i < P->tok_count; i++) {
        const Tok *t = &P->toks[i];
        if (is_opener(t)) depth++;
        else if (is_closer(t)) depth--;
        else if (depth == 0 && is_op(t, ',')) expect = 1;
        else if (depth == 0 && expect && is_name(t)) {
            if (P->pass == 1) {
                const Def *d = find_same(P, t);
                long sym;
                if (d && d->kind == DEF_CONSTANT) defined_twice(P, t);
                sym = add_tok_symbol(P, t, SCOUT_SYMBOL_CONSTANT, outer_scope(P, 0), -1, NULL);
                add_def(P, t, DEF_CONSTANT, sym, 0);
            }
            expect = 0;
        }
    }
}

/**
 * @brief The parameters of a function's head: the last word of each
 *        comma-separated part, with what stands before it as its type.
 * @param P The parser.
 * @param open The index of the '('.
 * @param close The index of the ')'.
 * @param symbol The function's symbol.
 */
static void read_parameters(Parser *P, long open, long close, long symbol)
{
    long k = open + 1;
    while (k < close) {
        long start = k, name = -1, depth = 0;
        for (; k < close; k++) {
            const Tok *t = &P->toks[k];
            if (is_opener(t)) depth++;
            else if (is_closer(t)) depth--;
            else if (depth == 0 && is_op(t, ',')) break;
            else if (depth == 0 && is_name(t)) name = k;
        }
        if (name >= 0) {
            add_local(P, &P->toks[name], 1);
            add_tok_symbol(P, &P->toks[name], SCOUT_SYMBOL_VARIABLE, ACE_SCOPE_LOCAL, symbol,
                           name > start ? slice_detail(P, &P->toks[start], &P->toks[name - 1]) : NULL);
        }
        k++;
    }
}

/**
 * @brief End the SUB or METHOD that is open.
 * @param P The parser.
 * @param last The last token of it.
 */
static void end_sub(Parser *P, const Tok *last)
{
    if (P->pass == 1 && P->sub_symbol >= 0) {
        AceRange *r = &P->res->symbols[P->sub_symbol].range;
        r->end_line = last->line;
        r->end_col = last->col + (long)(last->end - last->start);
    }
    P->sub_op = OP_PLAIN;
    P->sub_symbol = -1;
    P->block_count = 0;
    P->reset_locals = 1;
}

/**
 * @brief Step over the return type in the head of a function.
 *
 * It is a type word or a name that stands for one (STRUCTPTR, from a
 * \#define): a name that another name follows.
 *
 * @param P The parser.
 * @param j The index of the first token after the keyword.
 * @return The index of the function's name.
 */
static long skip_return_type(const Parser *P, long j)
{
    if (j < P->tok_count && is_kw(&P->toks[j], OP_TYPE)) return j + 1;
    if (j + 1 < P->tok_count && is_name(&P->toks[j]) && is_name(&P->toks[j + 1])) return j + 1;
    return j;
}

/**
 * @brief Add a function named in a head.
 * @param P The parser.
 * @param name The token of its name.
 * @param kw The first word of its head.
 * @param kind Its SCOUT_SYMBOL_ kind.
 * @param scope Where it can be seen.
 * @param close The index of the head's ')', or -1.
 * @param open The index of the head's '('.
 * @param declaration It is only declared here.
 * @param strip Leave the type qualifier out of its name.
 * @return Its symbol, or -1.
 */
static long add_function(Parser *P, const Tok *name, const Tok *kw, int kind, AceScope scope, long open, long close,
                         int declaration, int strip)
{
    AceRange r, sel;
    size_t len = name->end - name->start;
    char *detail = NULL;
    long sym;
    if (P->pass != 1) return -1;
    if (strip) len = base_length(P->src + name->start, len);
    tok_range(kw, &r);
    r.end_line = name->line;
    r.end_col = name->col + (long)(name->end - name->start);
    tok_range(name, &sel);
    if (close >= 0) {
        const Tok *last = &P->toks[close];
        r.end_line = last->line;
        r.end_col = last->col + 1;
        detail = slice_detail(P, &P->toks[open], last);
    }
    sym = add_symbol(P, P->src + name->start, len, kind, scope, -1, detail, &r, &sel);
    if (sym >= 0) P->res->symbols[sym].declaration = declaration;
    return sym;
}

/**
 * @brief Read the head of a SUB or METHOD and open it.
 * @param P The parser.
 * @param i The token of the keyword.
 */
static void read_sub(Parser *P, long i)
{
    const Tok *kw = &P->toks[i];
    long n = P->tok_count, j = i + 1, open = -1, close = -1, sym = -1;
    int method = is_kw(kw, OP_METHOD);
    const Tok *name;

    if (P->sub_op != OP_PLAIN) {
        structure_error_at(P, &P->sub_head, P->sub_op == OP_METHOD ? "METHOD without END METHOD" : "SUB without END SUB", NULL);
        end_sub(P, kw);
    } else {
        close_all_blocks(P);
    }
    P->local_count = 0;
    P->reset_locals = 0;
    P->sub_op = kw->word->op;
    P->sub_symbol = -1;
    tok_range(kw, &P->sub_head);
    j = skip_return_type(P, j);
    if (j >= n || !is_head_name(&P->toks[j])) {
        structure_error(P, kw, "%s without a name", kw->word->name);
        return;
    }
    name = &P->toks[j++];
    tok_range(name, &P->sub_head);
    if (j < n && is_op(&P->toks[j], '(')) {
        open = j;
        close = matching_bracket(P, j);
    }
    if (P->pass == 1) {
        Def *d = method ? NULL : find_same(P, name);
        int external = find_keyword(P, close >= 0 ? close + 1 : j, OP_EXTERNAL) >= 0;
        if (d && d->kind == DEF_FUNCTION && !d->declared) defined_twice(P, name);
        sym = add_function(P, name, kw, method ? SCOUT_SYMBOL_METHOD : SCOUT_SYMBOL_FUNCTION,
                           outer_scope(P, external), open, close, 0, 0);
        P->sub_symbol = sym;
        if (!method) {
            if (d && d->kind == DEF_FUNCTION) {
                d->declared = 0;
                d->symbol = sym;
            } else {
                add_def(P, name, DEF_FUNCTION, sym, 0);
            }
        }
    }
    if (close >= 0) read_parameters(P, open, close, P->sub_symbol);
}

/**
 * @brief Read a DEF statement: a function of one expression.
 * @param P The parser.
 * @param i The token of the keyword.
 */
static void read_def_fn(Parser *P, long i)
{
    const Tok *kw = &P->toks[i];
    long n = P->tok_count, j = i + 1, open = -1, close = -1, sym;
    const Tok *name;
    j = skip_return_type(P, j);
    if (j >= n || !is_name(&P->toks[j])) return;
    name = &P->toks[j++];
    if (j < n && is_op(&P->toks[j], '(')) {
        open = j;
        close = matching_bracket(P, j);
    }
    if (P->pass == 1) {
        const Def *d = find_same(P, name);
        if (d && d->kind == DEF_FUNCTION && !d->declared) defined_twice(P, name);
    }
    sym = add_function(P, name, kw, SCOUT_SYMBOL_FUNCTION, outer_scope(P, 0), open, close, 0, 0);
    add_def(P, name, DEF_FUNCTION, sym, 0);
    /* only the expression sees the parameters; their symbols would need a
     * SUB to belong to, and Scout finds locals through the range of it */
    if (P->sub_op == OP_PLAIN) P->local_count = 0;
    if (close >= 0) read_parameters(P, open, close, sym);
    if (P->sub_op == OP_PLAIN) P->reset_locals = 1;
}

/**
 * @brief Read a GENERIC METHOD: its name, and the block of its ON lines.
 * @param P The parser.
 * @param i The token of the keyword.
 */
static void read_generic(Parser *P, long i)
{
    const Tok *kw = &P->toks[i];
    long n = P->tok_count, j = i + 1, open = -1, close = -1;
    if (j < n && is_kw(&P->toks[j], OP_TYPE)) j++;
    if (j < n && is_kw(&P->toks[j], OP_METHOD)) j++;
    open_block(P, B_GENERIC, kw);
    j = skip_return_type(P, j);
    if (j >= n || !is_head_name(&P->toks[j])) return;
    if (j + 1 < n && is_op(&P->toks[j + 1], '(')) {
        open = j + 1;
        close = matching_bracket(P, j + 1);
    }
    add_def(P, &P->toks[j],  DEF_FUNCTION,
            add_function(P, &P->toks[j], kw, SCOUT_SYMBOL_FUNCTION, outer_scope(P, 0), open, close, 0, 0), 0);
}

/**
 * @brief Read a DECLARE statement.
 * @param P The parser.
 * @param i The token of the keyword.
 */
static void read_declare(Parser *P, long i)
{
    const Tok *kw = &P->toks[i];
    long n = P->tok_count, j = i + 1, open = -1, close = -1;
    const Tok *name;
    int function;
    if (j >= n) return;
    if (is_kw(&P->toks[j], OP_STRUCT) || is_kw(&P->toks[j], OP_CLASS)) {
        if (j + 2 < n) read_variables(P, j + 2, &P->toks[j + 1]);
        return;
    }
    function = is_kw(&P->toks[j], OP_FUNCTION);
    if (!function && !is_kw(&P->toks[j], OP_SUB)) return;
    j = skip_return_type(P, j + 1);
    if (j >= n || !is_head_name(&P->toks[j])) return;
    name = &P->toks[j++];
    if (j < n && is_op(&P->toks[j], '(')) {
        open = j;
        close = matching_bracket(P, j);
    }
    if (P->pass == 1 && !find_same(P, name)) {
        int external = find_keyword(P, close >= 0 ? close + 1 : j, OP_EXTERNAL) >= 0;
        add_def(P, name, DEF_FUNCTION,
                add_function(P, name, kw, SCOUT_SYMBOL_FUNCTION, outer_scope(P, external), open, close, 1, function), 1);
    }
}

/**
 * @brief Read an EXTERNAL statement: a function or variable of elsewhere.
 * @param P The parser.
 * @param i The token of the keyword.
 */
static void read_external(Parser *P, long i)
{
    const Tok *kw = &P->toks[i];
    long n = P->tok_count, j = i + 1;
    int function = 0;
    if (j < n && is_kw(&P->toks[j], OP_FUNCTION)) {
        function = 1;
        j++;
    }
    if (j < n && is_kw(&P->toks[j], OP_TYPE)) j++;
    if (j >= n || !is_name(&P->toks[j]) || P->pass != 1) return;
    if (!find_def(P, P->src + P->toks[j].start, P->toks[j].end - P->toks[j].start)) {
        add_def(P, &P->toks[j], DEF_FUNCTION,
                add_function(P, &P->toks[j], kw, function ? SCOUT_SYMBOL_FUNCTION : SCOUT_SYMBOL_VARIABLE,
                             ACE_SCOPE_GLOBAL, -1, -1, 1, 1), 1);
    }
}

/**
 * @brief Read the head of a STRUCT or CLASS.
 * @param P The parser.
 * @param i The token of the keyword.
 */
static void read_struct(Parser *P, long i)
{
    const Tok *kw = &P->toks[i];
    const Tok *name = i + 1 < P->tok_count ? &P->toks[i + 1] : NULL;
    if (P->struct_op != OP_PLAIN) {
        structure_error_at(P, &P->struct_head, P->struct_op == OP_CLASS ? "CLASS without END CLASS" : "STRUCT without END STRUCT", NULL);
    }
    P->struct_op = kw->word->op;
    P->struct_symbol = -1;
    tok_range(kw, &P->struct_head);
    if (!name || !is_head_name(name)) {
        structure_error(P, kw, "%s without a name", kw->word->name);
        return;
    }
    tok_range(name, &P->struct_head);
    if (P->pass == 1) {
        const Def *d = find_def(P, P->src + name->start, name->end - name->start);
        AceRange r, sel;
        char *detail = NULL;
        if (d && d->kind == DEF_TYPE) defined_twice(P, name);
        if (i + 3 < P->tok_count && is_kw(&P->toks[i + 2], OP_EXTENDS)) {
            detail = slice_detail(P, &P->toks[i + 2], &P->toks[i + 3]);
        }
        tok_range(kw, &r);
        tok_range(name, &sel);
        P->struct_symbol = add_symbol(P, P->src + name->start, name->end - name->start,
                                      kw->word->op == OP_CLASS ? SCOUT_SYMBOL_CLASS : SCOUT_SYMBOL_STRUCT,
                                      outer_scope(P, 0), -1, detail, &r, &sel);
        add_def(P, name, DEF_TYPE, P->struct_symbol, 0);
    }
}

/**
 * @brief Read a member of a STRUCT or CLASS: type, optional '*', name.
 *
 * The name may be a reserved word, as in STRING name SIZE 64.
 *
 * @param P The parser.
 * @param i The first token.
 */
static void read_member(Parser *P, long i)
{
    long n = P->tok_count, j = i + 1, depth = 0;
    const Tok *type = &P->toks[i];
    if (type->type != T_WORD) return;
    for (;;) {
        if (j < n && is_op(&P->toks[j], '*')) j++;
        if (j >= n || P->toks[j].type != T_WORD) return;
        add_tok_symbol(P, &P->toks[j], SCOUT_SYMBOL_FIELD, ACE_SCOPE_LOCAL, P->struct_symbol, slice_detail(P, type, type));
        for (j++; j < n; j++) {
            const Tok *t = &P->toks[j];
            if (is_opener(t)) depth++;
            else if (is_closer(t)) depth--;
            else if (depth == 0 && is_op(t, ',')) break;
        }
        if (j >= n) return;
        j++;
    }
}

/**
 * @brief Close a STRUCT or CLASS.
 * @param P The parser.
 * @param op OP_STRUCT or OP_CLASS.
 * @param t The token of END's second word.
 */
static void close_struct(Parser *P, Op op, const Tok *t)
{
    if (P->struct_op != op) {
        structure_error(P, t, op == OP_CLASS ? "END CLASS without CLASS" : "END STRUCT without STRUCT", NULL);
        return;
    }
    if (P->pass == 1 && P->struct_symbol >= 0) {
        AceRange *r = &P->res->symbols[P->struct_symbol].range;
        r->end_line = t->line;
        r->end_col = t->col + (long)(t->end - t->start);
    }
    P->struct_op = OP_PLAIN;
}

/**
 * @brief Handle END and the word after it.
 * @param P The parser.
 * @param i The index of END.
 */
static void read_end(Parser *P, long i)
{
    const Tok *t = &P->toks[i], *what;
    if (i + 1 >= P->tok_count) return;  /* END alone stops the program */
    what = &P->toks[i + 1];
    if (what->type != T_WORD || !what->word) return;
    switch (what->word->op) {
    case OP_IF: close_block(P, B_IF, t, "END IF without IF", 0); break;
    case OP_CASE: close_block(P, B_CASE, t, "END CASE without CASE", 0); break;
    case OP_TYPECASE: close_block(P, B_TYPECASE, t, "END TYPECASE without TYPECASE", 0); break;
    case OP_BLOCK: close_block(P, B_BLOCK, t, "END BLOCK without BLOCK", 0); break;
    case OP_GENERIC: close_block(P, B_GENERIC, t, "END GENERIC without GENERIC", 0); break;
    case OP_ASSEM: close_block(P, B_ASSEM, t, "END ASSEM without ASSEM", 0); break;
    case OP_STRUCT: case OP_CLASS: close_struct(P, what->word->op, what); break;
    case OP_SUB: case OP_METHOD:
        if (P->sub_op != what->word->op) {
            structure_error(P, t, what->word->op == OP_METHOD ? "END METHOD without METHOD" : "END SUB without SUB", NULL);
        } else {
            close_all_blocks(P);
            end_sub(P, what);
        }
        break;
    default:
        break;
    }
}

/**
 * @brief Follow the statement that starts at a token.
 * @param P The parser.
 * @param i The first token.
 */
static void process_head(Parser *P, long i)
{
    const Tok *t = &P->toks[i];
    long then;
    if (t->type != T_WORD || !t->word) return;
    /* old sources have variables called class or struct */
    if (i + 1 < P->tok_count && is_op(&P->toks[i + 1], '=')) return;
    switch (t->word->op) {
    case OP_TYPE: read_variables(P, i + 1, t); break;
    case OP_DIM: read_variables(P, i + 1, NULL); break;
    case OP_GLOBAL: case OP_COMMON: read_variables(P, i + 1, NULL); break;
    case OP_CONST: read_constants(P, i + 1); break;
    case OP_SUB: case OP_METHOD: read_sub(P, i); break;
    case OP_DEF: read_def_fn(P, i); break;
    case OP_GENERIC: read_generic(P, i); break;
    case OP_DECLARE: read_declare(P, i); break;
    case OP_EXTERNAL: read_external(P, i); break;
    case OP_STRUCT: case OP_CLASS: read_struct(P, i); break;
    case OP_END: read_end(P, i); break;
    case OP_IF:
        then = find_keyword(P, i, OP_THEN);
        if (then < 0 && find_keyword(P, i, OP_GOTO) < 0) {
            structure_error(P, t, "IF without THEN", NULL);
        } else if (then == P->tok_count - 1 && !P->ended_by_colon) {
            open_block(P, B_IF, t);
        } else {
            P->single_if = 1;
        }
        break;
    case OP_ELSEIF: close_block(P, B_IF, t, "ELSEIF without IF", 1); break;
    case OP_FOR: open_block(P, B_FOR, t); break;
    case OP_NEXT: close_block(P, B_FOR, t, "NEXT without FOR", 0); break;
    case OP_WHILE: open_block(P, B_WHILE, t); break;
    case OP_WEND: close_block(P, B_WHILE, t, "WEND without WHILE", 0); break;
    case OP_REPEAT: open_block(P, B_REPEAT, t); break;
    case OP_UNTIL: close_block(P, B_REPEAT, t, "UNTIL without REPEAT", 0); break;
    case OP_CASE:
        /* CASE with a type after it is a branch of TYPECASE */
        if (i + 1 == P->tok_count) open_block(P, B_CASE, t);
        break;
    case OP_TYPECASE: open_block(P, B_TYPECASE, t); break;
    case OP_BLOCK: open_block(P, B_BLOCK, t); break;
    case OP_ASSEM:
        open_block(P, B_ASSEM, t);
        P->in_assem = 1;
        break;
    default:
        break;
    }
}

/**
 * @brief Follow one statement: its definitions and the block structure.
 *
 * Statements can stand after THEN and ELSE of a one-line IF, so each of
 * those parts is followed like a statement of its own.
 *
 * @param P The parser.
 */
static void process_statement(Parser *P)
{
    long i = 0, n = P->tok_count;
    const Tok *t = &P->toks[0];

    if (P->reset_locals) {
        P->local_count = 0;
        P->reset_locals = 0;
    }
    if (P->first_on_line) {
        P->single_if = 0;
        if (t->type == T_NUMBER) {
            i = 1;              /* a line number */
            if (i >= n) return;
            t = &P->toks[i];
        }
        /* a word and a colon: a label, unless it calls a SUB */
        if (is_name(t) && i + 1 == n && P->ended_by_colon && !in_case(P) && P->struct_op == OP_PLAIN) {
            const Def *d = find_def(P, P->src + t->start, t->end - t->start);
            if (!d || d->kind != DEF_FUNCTION) {
                if (P->pass == 1) {
                    long sym;
                    if (d && d->kind == DEF_LABEL) defined_twice(P, t);
                    sym = add_tok_symbol(P, t, SCOUT_SYMBOL_LABEL, outer_scope(P, 0), -1, NULL);
                    add_def(P, t, DEF_LABEL, sym, 0);
                }
                return;
            }
        }
    }
    if (P->struct_op != OP_PLAIN) {
        if (is_kw(t, OP_END)) read_end(P, i);
        else read_member(P, i);
        return;
    }
    for (;;) {
        long j, depth = 0, next = -1;
        t = &P->toks[i];
        if (is_kw(t, OP_ELSE)) {
            if (!P->single_if && !in_case(P)) close_block(P, B_IF, t, "ELSE without IF", 1);
            i++;
            if (i >= n) return;
            continue;
        }
        process_head(P, i);
        for (j = i + 1; j < n - 1; j++) {
            t = &P->toks[j];
            if (is_opener(t)) depth++;
            else if (is_closer(t)) depth--;
            else if (depth <= 0 && (is_kw(t, OP_THEN) || is_kw(t, OP_ELSE))) {
                next = j + 1;
                break;
            }
        }
        if (next < 0) return;
        i = next;
    }
}

/**
 * @brief Tell whether a word stands where only a type can.
 * @param P The parser.
 * @param i The word's index.
 * @return Non-zero if it does.
 */
static int at_type_position(const Parser *P, long i)
{
    const Tok *prev;
    if (i == 0) return 0;
    prev = &P->toks[i - 1];
    return is_kw(prev, OP_STRUCT) || is_kw(prev, OP_CLASS) || is_kw(prev, OP_EXTENDS) ||
           (i >= 2 && is_op(prev, '(') && is_kw(&P->toks[i - 2], OP_SIZEOF));
}

/**
 * @brief Make the highlighting tokens of the statement.
 * @param P The parser.
 */
static void emit_tokens(Parser *P)
{
    long i, first = 0;
    if (P->struct_op != OP_PLAIN) {
        const Tok *t = &P->toks[0];
        if (is_name(t)) add_token(P, t, SCOUT_TOKEN_TYPE);
        return;
    }
    if (is_kw(&P->toks[0], OP_DATA)) return;
    if (P->first_on_line && P->toks[0].type == T_NUMBER) first = 1;
    for (i = first; i < P->tok_count; i++) {
        const Tok *t = &P->toks[i];
        const char *text = P->src + t->start;
        size_t len = t->end - t->start;
        const Local *l;
        const Def *d;
        if (t->type == T_ATOM) {
            add_token(P, t, SCOUT_TOKEN_CONSTANT);
            continue;
        }
        if (!is_name(t)) continue;
        if (i > 0 && is_op(&P->toks[i - 1], OP2('-', '>'))) continue;
        if (at_type_position(P, i)) {
            add_token(P, t, SCOUT_TOKEN_TYPE);
            continue;
        }
        l = find_local(P, text, len);
        if (l) {
            if (l->parameter) add_token(P, t, SCOUT_TOKEN_PARAMETER);
            continue;
        }
        d = find_def(P, text, len);
        if (!d) continue;
        switch (d->kind) {
        case DEF_FUNCTION: add_token(P, t, SCOUT_TOKEN_FUNCTION); break;
        case DEF_TYPE: add_token(P, t, SCOUT_TOKEN_TYPE); break;
        case DEF_CONSTANT: add_token(P, t, SCOUT_TOKEN_CONSTANT); break;
        case DEF_MACRO: add_token(P, t, SCOUT_TOKEN_MACRO); break;
        default: break;
        }
    }
}

/**
 * @brief Make one pass over the source.
 * @param P The parser.
 * @param pass 1 or 2.
 */
static void run_pass(Parser *P, int pass)
{
    P->pass = pass;
    P->pos = 0;
    P->line = 1;
    P->line_start = 0;
    P->line_fresh = 1;
    P->single_if = 0;
    P->in_assem = 0;
    P->block_count = 0;
    P->local_count = 0;
    P->reset_locals = 0;
    P->sub_op = OP_PLAIN;
    P->sub_symbol = -1;
    P->struct_op = OP_PLAIN;
    P->struct_symbol = -1;
    P->pp_depth = 0;
    while (read_statement(P)) {
        process_statement(P);
        if (pass == 2) emit_tokens(P);
        if (P->res->out_of_memory) return;
    }
}

/**
 * @brief Compare two diagnostics by place, for qsort().
 * @param a The first.
 * @param b The second.
 * @return Their order.
 */
static int compare_diagnostics(const void *a, const void *b)
{
    const AceDiagnostic *x = (const AceDiagnostic *)a, *y = (const AceDiagnostic *)b;
    if (x->range.start_line != y->range.start_line) return x->range.start_line < y->range.start_line ? -1 : 1;
    if (x->range.start_col != y->range.start_col) return x->range.start_col < y->range.start_col ? -1 : 1;
    return 0;
}

/**
 * @brief Tell whether a source has \#else at the start of a line.
 * @param P The parser.
 * @return Non-zero if it has.
 */
static int has_alternative_branch(const Parser *P)
{
    size_t i;
    int at_start = 1;
    for (i = 0; i < P->length; i++) {
        char c = P->src[i];
        if (at_start && c == '#' && P->length - i >= 5 && equal_fold(P->src + i + 1, "else", 4)) return 1;
        if (c == '\n') at_start = 1;
        else if (c != ' ' && c != '\t') at_start = 0;
    }
    return 0;
}

AceResult *ace_parser_analyze(const char *source, size_t length, int is_include)
{
    Parser *P;
    AceResult *res;
    long k;

    make_word_table();
    P = (Parser *)calloc(1, sizeof(Parser));
    if (!P) return NULL;
    res = (AceResult *)calloc(1, sizeof(AceResult));
    P->def_slots = (long *)malloc(DEF_SLOTS * sizeof(long));
    if (!res || !P->def_slots) {
        free(P->def_slots);
        free(res);
        free(P);
        return NULL;
    }
    P->res = res;
    P->src = source;
    P->length = length;
    P->is_include = is_include;
    for (k = 0; k < DEF_SLOTS; k++) P->def_slots[k] = -1;
    P->lenient = has_alternative_branch(P);
    run_pass(P, 1);
    if (!res->stopped && !res->out_of_memory) {
        if (P->sub_op != OP_PLAIN) {
            structure_error_at(P, &P->sub_head, P->sub_op == OP_METHOD ? "METHOD without END METHOD" : "SUB without END SUB", NULL);
        }
        close_all_blocks(P);
        if (P->struct_op != OP_PLAIN) {
            structure_error_at(P, &P->struct_head, P->struct_op == OP_CLASS ? "CLASS without END CLASS" : "STRUCT without END STRUCT", NULL);
        }
        if (P->pp_depth > 0 && P->pp_depth <= MAX_CONDITIONALS) {
            const AceRange *r = &P->pp_open[P->pp_depth - 1];
            add_diagnostic(P, SCOUT_SEVERITY_ERROR, r->start_line, r->start_col, r->end_col, "#ifdef without #endif", NULL, 0);
        }
        run_pass(P, 2);
    }
    if (res->diagnostic_count > 1) {
        qsort(res->diagnostics, (size_t)res->diagnostic_count, sizeof(AceDiagnostic), compare_diagnostics);
    }
    free(P->toks);
    free(P->defs);
    free(P->def_slots);
    free(P);
    return res;
}

void ace_result_free(AceResult *res)
{
    long i;
    if (!res) return;
    for (i = 0; i < res->symbol_count; i++) {
        free(res->symbols[i].name);
        free(res->symbols[i].detail);
    }
    for (i = 0; i < res->diagnostic_count; i++) free(res->diagnostics[i].message);
    for (i = 0; i < res->reference_count; i++) free(res->references[i].target);
    free(res->symbols);
    free(res->diagnostics);
    free(res->references);
    free(res->tokens);
    free(res);
}
