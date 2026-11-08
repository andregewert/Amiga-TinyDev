/**
 * @file e_parser.c
 * @brief The analysis of the AmigaE parser of Scout: two passes over the
 *        statements of a source, and the walk through a compiled module.
 *
 * It follows E-VO (the keywords and built-in functions are the tables of its
 * compiler), which also reads what the original Amiga E 3.3 accepted. It
 * is a heuristic: expressions are not parsed, only the statements that
 * define something or open and close a block.
 */
#include "e_parser.h"

#include <stdlib.h>
#include <string.h>

int (*e_parser_should_stop)(void) = NULL;

/** Most diagnostics reported for one source. */
#define MAX_DIAGNOSTICS 200
/** Most nested blocks that are followed. */
#define MAX_BLOCKS 64
/** Most open brackets that are followed. */
#define MAX_BRACKETS 64
/** Most nested \#ifdefs that are followed. */
#define MAX_CONDITIONALS 32
/** Most parameters and local DEFs of a PROC that are remembered. */
#define MAX_LOCALS 256
/** Slots of the hash table of keywords. */
#define WORD_SLOTS 512
/** Slots of the hash table of definitions. */
#define DEF_SLOTS 1024
/** Longest text made a symbol's detail. */
#define MAX_DETAIL 100
/** How many statements pass between two looks at e_parser_should_stop(). */
#define STOP_INTERVAL 64
/** The last module format this reader knows. */
#define MODULE_MAX_VERSION 16

/** What a keyword does for the analysis. */
typedef enum Op {
    OP_PLAIN,       /**< nothing special */
    OP_TYPE,        /**< a built-in type */
    OP_PTR,         /**< PTR, a built-in type */
    OP_CONT,        /**< an operator word: AND, OR, BUT and the like */
    OP_NOT,         /**< NOT */
    OP_OPT,         /**< OPT */
    OP_MODULE,      /**< MODULE */
    OP_EXPORT,      /**< EXPORT */
    OP_CONST,       /**< CONST */
    OP_ENUM,        /**< ENUM */
    OP_SET,         /**< SET */
    OP_DEF,         /**< DEF */
    OP_PROC,        /**< PROC */
    OP_ENDPROC,     /**< ENDPROC */
    OP_EXCEPT,      /**< EXCEPT */
    OP_HANDLE,      /**< HANDLE */
    OP_IS,          /**< IS */
    OP_RETURN,      /**< RETURN, also the older form of IS */
    OP_OF,          /**< OF */
    OP_TO,          /**< TO */
    OP_SIZEOF,      /**< SIZEOF */
    OP_OBJECT,      /**< OBJECT */
    OP_ENDOBJECT,   /**< ENDOBJECT */
    OP_IF,          /**< IF, IFN */
    OP_ELSEIF,      /**< ELSEIF, ELSEIFN */
    OP_ELSE,        /**< ELSE */
    OP_ENDIF,       /**< ENDIF */
    OP_THEN,        /**< THEN */
    OP_FOR,         /**< FOR */
    OP_ENDFOR,      /**< ENDFOR */
    OP_DO,          /**< DO */
    OP_WHILE,       /**< WHILE, WHILEN */
    OP_ELSEWHILE,   /**< ELSEWHILE, ELSEWHILEN */
    OP_ENDWHILE,    /**< ENDWHILE */
    OP_REPEAT,      /**< REPEAT */
    OP_UNTIL,       /**< UNTIL, UNTILN */
    OP_LOOP,        /**< LOOP */
    OP_ENDLOOP,     /**< ENDLOOP */
    OP_SELECT,      /**< SELECT */
    OP_CASE,        /**< CASE, DEFAULT */
    OP_ENDSELECT,   /**< ENDSELECT */
    OP_TRY,         /**< TRY */
    OP_CATCH,       /**< CATCH */
    OP_ENDTRY       /**< ENDTRY */
} Op;

/** A keyword. */
typedef struct Word {
    const char *name;   /**< its text */
    Op op;              /**< what it does */
    short next;         /**< next word of its hash slot, or -1 */
} Word;

/** The keywords of E-VO, with the built-in types. */
static Word words[] = {
    { "PROC", OP_PROC, -1 }, { "ENDPROC", OP_ENDPROC, -1 }, { "IF", OP_IF, -1 },
    { "ENDIF", OP_ENDIF, -1 }, { "VOID", OP_PLAIN, -1 }, { "WHILE", OP_WHILE, -1 },
    { "ENDWHILE", OP_ENDWHILE, -1 }, { "FOR", OP_FOR, -1 }, { "ENDFOR", OP_ENDFOR, -1 },
    { "SELECT", OP_SELECT, -1 }, { "CASE", OP_CASE, -1 }, { "DEFAULT", OP_CASE, -1 },
    { "ENDSELECT", OP_ENDSELECT, -1 }, { "REPEAT", OP_REPEAT, -1 }, { "UNTIL", OP_UNTIL, -1 },
    { "JUMP", OP_PLAIN, -1 }, { "DEF", OP_DEF, -1 }, { "ELSE", OP_ELSE, -1 },
    { "INCBIN", OP_PLAIN, -1 }, { "INC", OP_PLAIN, -1 }, { "DEC", OP_PLAIN, -1 },
    { "THEN", OP_THEN, -1 }, { "LOOP", OP_LOOP, -1 }, { "ENDLOOP", OP_ENDLOOP, -1 },
    { "DO", OP_DO, -1 }, { "AND", OP_CONT, -1 }, { "OR", OP_CONT, -1 },
    { "CONST", OP_CONST, -1 }, { "OPT", OP_OPT, -1 }, { "MODULE", OP_MODULE, -1 },
    { "STACK", OP_PLAIN, -1 }, { "EXIT", OP_PLAIN, -1 }, { "LARGE", OP_PLAIN, -1 },
    { "ASM", OP_PLAIN, -1 }, { "NOWARN", OP_PLAIN, -1 }, { "TO", OP_TO, -1 },
    { "STEP", OP_PLAIN, -1 }, { "DIR", OP_PLAIN, -1 }, { "OF", OP_OF, -1 },
    { "ELSEIF", OP_ELSEIF, -1 }, { "OBJECT", OP_OBJECT, -1 }, { "ENDOBJECT", OP_ENDOBJECT, -1 },
    { "SIZEOF", OP_SIZEOF, -1 }, { "RETURN", OP_RETURN, -1 }, { "OSVERSION", OP_PLAIN, -1 },
    { "ENUM", OP_ENUM, -1 }, { "SET", OP_SET, -1 }, { "BUT", OP_CONT, -1 },
    { "HANDLE", OP_HANDLE, -1 }, { "EXCEPT", OP_EXCEPT, -1 }, { "RAISE", OP_PLAIN, -1 },
    { "EXPORT", OP_EXPORT, -1 }, { "REG", OP_PLAIN, -1 }, { "END", OP_PLAIN, -1 },
    { "IS", OP_IS, -1 }, { "NEW", OP_PLAIN, -1 }, { "PUBLIC", OP_PLAIN, -1 },
    { "PRIVATE", OP_PLAIN, -1 }, { "SUPER", OP_PLAIN, -1 }, { "PREPROCESS", OP_PLAIN, -1 },
    { "LIBRARY", OP_PLAIN, -1 }, { "STRMERGE", OP_PLAIN, -1 }, { "NOALIGN", OP_PLAIN, -1 },
    { "NOT", OP_NOT, -1 }, { "UNION", OP_PLAIN, -1 }, { "ENDUNION", OP_PLAIN, -1 },
    { "SAFE", OP_PLAIN, -1 }, { "RUNBG", OP_PLAIN, -1 }, { "OLD", OP_PLAIN, -1 },
    { "EXTRA", OP_PLAIN, -1 }, { "CONT", OP_PLAIN, -1 }, { "ANDALSO", OP_CONT, -1 },
    { "ORELSE", OP_CONT, -1 }, { "POOL", OP_PLAIN, -1 }, { "PURE", OP_PLAIN, -1 },
    { "FATAL", OP_PLAIN, -1 }, { "WARN", OP_PLAIN, -1 }, { "WITH", OP_PLAIN, -1 },
    { "NOREGS", OP_PLAIN, -1 }, { "FPEXP", OP_PLAIN, -1 }, { "NOSTARTUP", OP_PLAIN, -1 },
    { "UTILLIB", OP_PLAIN, -1 }, { "INLINE", OP_PLAIN, -1 }, { "ELSEWHILE", OP_ELSEWHILE, -1 },
    { "ALWAYS", OP_PLAIN, -1 }, { "SECTION", OP_PLAIN, -1 }, { "CODE", OP_PLAIN, -1 },
    { "DATA", OP_PLAIN, -1 }, { "CHIP", OP_PLAIN, -1 }, { "FAST", OP_PLAIN, -1 },
    { "OFFSETOF", OP_PLAIN, -1 }, { "TRY", OP_TRY, -1 }, { "CATCH", OP_CATCH, -1 },
    { "ENDTRY", OP_ENDTRY, -1 }, { "UNTILN", OP_UNTIL, -1 }, { "WHILEN", OP_WHILE, -1 },
    { "ELSEWHILEN", OP_ELSEWHILE, -1 }, { "IFN", OP_IF, -1 }, { "ELSEIFN", OP_ELSEIF, -1 },
    { "EXITN", OP_PLAIN, -1 }, { "CONTN", OP_PLAIN, -1 }, { "LEGACYINT", OP_PLAIN, -1 },
    { "PSIZEOF", OP_PLAIN, -1 }, { "ARRAYSIZE", OP_PLAIN, -1 }, { "STACKCHECK", OP_PLAIN, -1 },
    { "GETA4", OP_PLAIN, -1 }, { "MODSETUP", OP_PLAIN, -1 }, { "MODCLEANUP", OP_PLAIN, -1 },
    { "CANPOOL", OP_PLAIN, -1 }, { "ESTRCONST", OP_PLAIN, -1 },
    { "LONG", OP_TYPE, -1 }, { "INT", OP_TYPE, -1 }, { "CHAR", OP_TYPE, -1 },
    { "BYTE", OP_TYPE, -1 }, { "WORD", OP_TYPE, -1 }, { "PTR", OP_PTR, -1 },
    { "ARRAY", OP_TYPE, -1 }, { "STRING", OP_TYPE, -1 }, { "LIST", OP_TYPE, -1 }
};

/** Number of keywords. */
#define WORD_COUNT ((long)(sizeof(words) / sizeof(words[0])))

const char *const e_parser_constants[] = {
    "TRUE", "FALSE", "NIL", "ALL", "STRLEN", "GADGETSIZE", "OLDFILE", "NEWFILE", "EMPTY",
    "TAG_DONE", "TAG_END", "TAG_IGNORE", "TAG_MORE", "TAG_SKIP", "TAG_USER",
    "OFFSET_BEGINNING", "OFFSET_BEGINING", "OFFSET_CURRENT", "OFFSET_END", "READWRITE",
    NULL
};

const char *const e_parser_functions[] = {
    "WriteF", "Mul", "Div", "OpenW", "OpenS", "Mouse", "Plot", "Line", "TextF", "Colour",
    "SetStdRast", "SetStdOut", "Long", "Int", "Char", "PutLong", "PutInt", "PutChar", "New",
    "CleanUp", "CloseW", "CloseS", "And", "Or", "Not", "Gadget", "SetTopaz", "StrCmp",
    "StrCopy", "StrAdd", "StrLen", "EstrLen", "StrMax", "String", "RightStr", "MidStr",
    "StringF", "Val", "InStr", "TrimStr", "UpperStr", "LowerStr", "ReadStr", "Out", "Inp",
    "KickVersion", "FileLength", "MouseX", "MouseY", "FreeStack", "CtrlC", "List", "ListCopy",
    "ListAdd", "ListCmp", "ListLen", "ListMax", "Even", "Odd", "Eval", "ForAll", "Exists",
    "MapList", "Abs", "Shl", "Shr", "Box", "Dispose", "DisposeLink", "Link", "Next", "Forward",
    "SetStr", "SetList", "WaitIMessage", "MsgCode", "MsgQualifier", "MsgIaddr", "Rnd", "RndQ",
    "Mod", "Eor", "Raise", "ListItem", "NewR", "Sign", "PrintF", "WaitLeftMouse", "LeftMouse",
    "SetStdIn", "Throw", "ReThrow", "SelectList", "SetColour", "NewM", "Bounds", "RealF",
    "RealVal", "Fabs", "Ffloor", "Fceil", "Fsin", "Fcos", "Ftan", "Fexp", "Flog", "Fpow",
    "Fsqrt", "Flog10", "FastDispose", "FastNew", "Min", "Max", "OstrCmp", "AstrCopy", "Cell",
    "FreeCells", "SetChunkSize", "Car", "Cdr", "_Cons", "FastDisposeList", "Fatan", "Fsincos",
    "Fsinh", "Fcosh", "Ftanh", "Ftieee", "Ffieee", "Fasin", "Facos", "StriCmp", "InStri",
    "StrClone", "ListClone", "GetA4", "AstringF", "DoMethod", "DoMethodA", "CoerceMethod",
    "CoerceMethodA", "DoSuperMethod", "DoSuperMethodA", "CtrlD", "CtrlE", "CtrlF", "Chk", "Eof",
    "Fopen", "Fclose", "Alloc", "Free", "PutF", "ReadB", "WriteB", "Size", "Lsl", "Lsr",
    "StrAddChar", "UpperChar", "LowerChar", "ListAddItem", "StrIns", "StrRem", "MemFill",
    "AstrClone", "ListInsItem", "ListRemItem", "ListSwapItem", "EndsWith", "AstpCopy",
    "UsedStack", "TotalStack", "Byte", "Word", "PutByte", "PutWord", "Compare", "Ucompare",
    "Fcompare", "StrCompare", "SelectFirst", "Rol", "Ror", "CharInStr", "CharInStri",
    "MemCompare", "MemDebugReport",
    NULL
};

const char *const e_parser_variables[] = {
    "self", "stdout", "stdin", "conout", "arg", "execbase", "dosbase", "intuitionbase",
    "gfxbase", "stdrast", "wbmessage", "mathieeesingbasbase", "exception", "exceptioninfo",
    "thistask", "utilitybase",
    NULL
};

/** Kinds of tokens of a statement. */
typedef enum TokType {
    T_WORD,     /**< a name or keyword */
    T_NUMBER,   /**< a number */
    T_STRING,   /**< a string in '' */
    T_CHAR,     /**< a character constant in "" */
    T_OP        /**< an operator or bracket */
} TokType;

/** A token of the current statement. */
typedef struct Tok {
    size_t start;       /**< offset of the first byte */
    size_t end;         /**< offset after the last byte */
    long line;          /**< its line */
    long col;           /**< its column */
    TokType type;       /**< what it is */
    const Word *word;   /**< the keyword of a T_WORD, or NULL for a name */
    int code;           /**< a T_OP's character, or OP2() of its two */
} Tok;

/** The code of an operator of two characters. */
#define OP2(a, b) (((int)(a) << 8) | (int)(b))

/** Kinds of blocks. */
typedef enum BlockType {
    B_IF,       /**< IF ... ENDIF */
    B_FOR,      /**< FOR ... ENDFOR */
    B_WHILE,    /**< WHILE ... ENDWHILE */
    B_REPEAT,   /**< REPEAT ... UNTIL */
    B_LOOP,     /**< LOOP ... ENDLOOP */
    B_SELECT,   /**< SELECT ... ENDSELECT */
    B_TRY       /**< TRY ... ENDTRY */
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
    DEF_PROC,       /**< a PROC */
    DEF_OBJECT,     /**< an OBJECT */
    DEF_CONSTANT,   /**< a CONST, ENUM or SET */
    DEF_GLOBAL,     /**< a global DEF */
    DEF_LABEL,      /**< a label */
    DEF_MACRO       /**< a \#define */
} DefKind;

/** A name defined in the source. */
typedef struct Def {
    size_t start;   /**< offset of its name */
    size_t length;  /**< length of its name */
    DefKind kind;   /**< what it is */
    long symbol;    /**< index of its symbol */
    long next;      /**< next definition of its hash slot, or -1 */
} Def;

/** A parameter or a local DEF of the current PROC. */
typedef struct Local {
    size_t start;   /**< offset of its name */
    size_t length;  /**< length of its name */
    int parameter;  /**< it is a parameter */
} Local;

/** The state of one analysis. */
typedef struct Parser {
    EResult *res;               /**< the result */
    const char *src;            /**< the source */
    size_t length;              /**< its length */
    int pass;                   /**< 1 finds definitions, 2 makes tokens */
    int lenient;                /**< no diagnostics about the structure: \#else or too deep */
    size_t pos;                 /**< where the lexer stands */
    size_t line_start;          /**< offset of the current line */
    long line;                  /**< the current line */
    long statements;            /**< statements read, for the stop check */
    long symbol_cap;            /**< room in EResult::symbols */
    long diagnostic_cap;        /**< room in EResult::diagnostics */
    long reference_cap;         /**< room in EResult::references */
    long token_cap;             /**< room in EResult::tokens, in values */
    Tok *toks;                  /**< the tokens of the current statement */
    long tok_count;             /**< number of them */
    long tok_cap;               /**< room in toks */
    long brackets[MAX_BRACKETS];/**< tokens of the open brackets */
    long bracket_depth;         /**< number of open brackets */
    Block blocks[MAX_BLOCKS];   /**< the open blocks */
    long block_count;           /**< number of them */
    Def *defs;                  /**< the definitions */
    long def_count;             /**< number of them */
    long def_cap;               /**< room in defs */
    long *def_slots;            /**< hash table of defs: first index, -1 */
    Local locals[MAX_LOCALS];   /**< the current PROC's parameters and DEFs */
    long local_count;           /**< number of them */
    int reset_locals;           /**< forget the locals before the next statement */
    int in_proc;                /**< inside a PROC */
    int proc_handle;            /**< the PROC has HANDLE */
    long proc_symbol;           /**< its symbol, or -1 */
    ERange proc_head;           /**< its name */
    int in_object;              /**< inside an OBJECT */
    long object_symbol;         /**< its symbol, or -1 */
    ERange object_head;         /**< its name */
    long content_start;         /**< first token after labels, ELSE and EXPORT */
    int is_module;              /**< OPT MODULE */
    int export_all;             /**< OPT EXPORT */
    int has_proc;               /**< there is a PROC */
    int has_main;               /**< there is a PROC main */
    long pp_depth;              /**< open \#ifdefs */
    ERange pp_open[MAX_CONDITIONALS]; /**< where they are */
} Parser;

/**
 * @brief The hash of a name.
 * @param text The name.
 * @param len Its length.
 * @return The hash.
 */
static unsigned long hash_name(const char *text, size_t len)
{
    unsigned long h = 5381;
    size_t i;
    for (i = 0; i < len; i++) h = h * 33 + (unsigned char)text[i];
    return h;
}

/** The hash table of the keywords. */
static short word_slots[WORD_SLOTS];
/** The table has been made. */
static int words_ready = 0;

/**
 * @brief Make the hash table of the keywords, once.
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
 * @brief Find a keyword.
 * @param text The text.
 * @param len Its length.
 * @return The keyword, or NULL.
 */
static const Word *find_word(const char *text, size_t len)
{
    short i = word_slots[hash_name(text, len) % WORD_SLOTS];
    while (i >= 0) {
        const Word *w = &words[i];
        if (strncmp(w->name, text, len) == 0 && w->name[len] == '\0') return w;
        i = w->next;
    }
    return NULL;
}

/**
 * @brief Tell whether a keyword belongs to the class "type".
 * @param w The keyword.
 * @return Non-zero if it does.
 */
static int is_type_word(const Word *w)
{
    return w->op == OP_TYPE || w->op == OP_PTR;
}

const char *e_parser_keyword_name(long index)
{
    long i;
    for (i = 0; i < WORD_COUNT; i++) {
        if (is_type_word(&words[i])) continue;
        if (index-- == 0) return words[i].name;
    }
    return NULL;
}

const char *e_parser_type_name(long index)
{
    long i;
    for (i = 0; i < WORD_COUNT; i++) {
        if (!is_type_word(&words[i])) continue;
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
    EResult *res = P->res;
    const char *hole = name ? strstr(format, "%s") : NULL;
    size_t flen = strlen(format);
    EDiagnostic *d;
    char *msg;
    if (P->pass != 1 || res->diagnostic_count >= MAX_DIAGNOSTICS) return;
    if (!grow(P, (void **)&res->diagnostics, res->diagnostic_count, &P->diagnostic_cap, sizeof(EDiagnostic))) return;
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
static void structure_error_at(Parser *P, const ERange *r, const char *format, const char *name)
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
    EResult *res = P->res;
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
static void tok_range(const Tok *t, ERange *r)
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
static long add_symbol(Parser *P, const char *name, size_t len, int kind, EScope scope, long parent,
                       char *detail, const ERange *range, const ERange *selection)
{
    EResult *res = P->res;
    ESymbol *s;
    char *copy;
    if (P->pass != 1 ||
        !grow(P, (void **)&res->symbols, res->symbol_count, &P->symbol_cap, sizeof(ESymbol)) ||
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
static long add_tok_symbol(Parser *P, const Tok *t, int kind, EScope scope, long parent, char *detail)
{
    ERange r;
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
 * @brief Find a definition.
 * @param P The parser.
 * @param text Its name.
 * @param len The length of the name.
 * @return The definition, or NULL.
 */
static Def *find_def(Parser *P, const char *text, size_t len)
{
    long i = P->def_slots[hash_name(text, len) % DEF_SLOTS];
    while (i >= 0) {
        Def *d = &P->defs[i];
        if (d->length == len && memcmp(P->src + d->start, text, len) == 0) return d;
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
 */
static void add_def(Parser *P, const Tok *t, DefKind kind, long symbol)
{
    unsigned long h;
    Def *d;
    if (P->pass != 1 || !grow(P, (void **)&P->defs, P->def_count, &P->def_cap, sizeof(Def))) return;
    h = hash_name(P->src + t->start, t->end - t->start) % DEF_SLOTS;
    d = &P->defs[P->def_count];
    d->start = t->start;
    d->length = t->end - t->start;
    d->kind = kind;
    d->symbol = symbol;
    d->next = P->def_slots[h];
    P->def_slots[h] = P->def_count++;
}

/**
 * @brief Find a parameter or local DEF of the current PROC.
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
        if (l->length == len && P->src[l->start] == text[0] && memcmp(P->src + l->start, text, len) == 0) {
            return l;
        }
    }
    return NULL;
}

/**
 * @brief Remember a parameter or local DEF, and report one defined twice.
 * @param P The parser.
 * @param t The token of its name.
 * @param parameter It is a parameter.
 */
static void add_local(Parser *P, const Tok *t, int parameter)
{
    Local *l;
    if (find_local(P, P->src + t->start, t->end - t->start)) {
        defined_twice(P, t);
        return;
    }
    if (P->local_count >= MAX_LOCALS) return;
    l = &P->locals[P->local_count++];
    l->start = t->start;
    l->length = t->end - t->start;
    l->parameter = parameter;
}

/**
 * @brief Tell whether a token is a name (a word and no keyword).
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
 * @brief Tell whether a token is a keyword of a certain role.
 * @param t The token.
 * @param op The role.
 * @return Non-zero if it is.
 */
static int is_kw(const Tok *t, Op op)
{
    return t->type == T_WORD && t->word && t->word->op == op;
}

/**
 * @brief Tell whether a token opens a bracket.
 * @param t The token.
 * @return Non-zero if it does.
 */
static int is_opener(const Tok *t)
{
    return is_op(t, '(') || is_op(t, '[') || is_op(t, '{');
}

/**
 * @brief Tell whether a token closes a bracket.
 * @param t The token.
 * @return Non-zero if it does.
 */
static int is_closer(const Tok *t)
{
    return is_op(t, ')') || is_op(t, ']') || is_op(t, '}');
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
    return is_name_start(c) || is_digit(c);
}

/**
 * @brief Tell whether a byte is an upper-case letter.
 * @param c The byte.
 * @return Non-zero if it is.
 */
static int is_upper(unsigned char c)
{
    return c >= 'A' && c <= 'Z';
}

/**
 * @brief Tell whether a byte is a lower-case letter.
 * @param c The byte.
 * @return Non-zero if it is.
 */
static int is_lower(unsigned char c)
{
    return c >= 'a' && c <= 'z';
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
 * @brief Skip a block comment; they nest.
 * @param P The parser, standing on the comment.
 */
static void skip_block_comment(Parser *P)
{
    long depth = 0, line = P->line, col = (long)(P->pos - P->line_start) + 1;
    while (P->pos < P->length) {
        char c = P->src[P->pos];
        char d = P->pos + 1 < P->length ? P->src[P->pos + 1] : '\0';
        if (c == '/' && d == '*') {
            depth++;
            P->pos += 2;
        } else if (c == '*' && d == '/') {
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
    add_diagnostic(P, SCOUT_SEVERITY_ERROR, line, col, col + 2, "comment not closed", NULL, 0);
}

/**
 * @brief Read a string or character constant.
 *
 * A backslash escapes the next character; a quote written twice is one quote.
 *
 * @param P The parser, standing on the quote.
 * @param quote The quote.
 */
static void read_string(Parser *P, char quote)
{
    size_t start = P->pos;
    int closed = 0;
    P->pos++;
    while (P->pos < P->length && P->src[P->pos] != '\n') {
        char c = P->src[P->pos];
        if (c == '\\' && P->pos + 1 < P->length && P->src[P->pos + 1] != '\n') {
            P->pos += 2;
        } else if (c == quote) {
            P->pos++;
            if (P->pos < P->length && P->src[P->pos] == quote) {
                P->pos++;
            } else {
                closed = 1;
                break;
            }
        } else {
            P->pos++;
        }
    }
    if (!push_tok(P, start, P->pos, quote == '\'' ? T_STRING : T_CHAR, 0)) return;
    if (!closed) {
        long col = (long)(start - P->line_start) + 1;
        add_diagnostic(P, SCOUT_SEVERITY_WARNING, P->line, col, col + 1, "string not closed", NULL, 0);
    }
}

/**
 * @brief Read a number: decimal or float, $hex or %binary.
 * @param P The parser, standing on it.
 */
static void read_number(Parser *P)
{
    size_t start = P->pos;
    char c = P->src[P->pos];
    P->pos++;
    if (c == '$' || c == '%') {
        while (P->pos < P->length && is_name_part((unsigned char)P->src[P->pos])) P->pos++;
    } else {
        while (P->pos < P->length && is_digit((unsigned char)P->src[P->pos])) P->pos++;
        if (P->pos + 1 < P->length && P->src[P->pos] == '.' && is_digit((unsigned char)P->src[P->pos + 1])) {
            P->pos++;
            while (P->pos < P->length && is_digit((unsigned char)P->src[P->pos])) P->pos++;
        }
    }
    push_tok(P, start, P->pos, T_NUMBER, 0);
}

/**
 * @brief Read an operator of one or two characters, and follow brackets.
 * @param P The parser, standing on it.
 */
static void read_operator(Parser *P)
{
    static const char twos[][3] = { ":=", "::", "<=", ">=", "<>", "++", "--", "||", "<<", ">>" };
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
            structure_error(P, t, "'%s' without an opening bracket", text);
        } else {
            P->bracket_depth--;
        }
    }
}

/**
 * @brief Tell whether the last line of a statement goes on in the next.
 * @param P The parser.
 * @return Non-zero if it does.
 */
static int continues(const Parser *P)
{
    const Tok *t = &P->toks[P->tok_count - 1];
    if (P->bracket_depth > 0) return 1;
    if (t->type == T_WORD) return t->word && (t->word->op == OP_CONT || t->word->op == OP_THEN || t->word->op == OP_IS);
    if (t->type != T_OP) return 0;
    switch (t->code) {
    case '+':
        /* MOVE.L D0,(A1)+ of inline assembly */
        if (P->tok_count >= 4 && is_op(t - 1, ')') && (t - 2)->end - (t - 2)->start == 2 && is_op(t - 3, '(')) {
            return 0;
        }
        return 1;
    case ',': case '-': case '*': case '/': case '=': case '<': case '&': case '.':
    case OP2('<', '='): case OP2('>', '='): case OP2('<', '>'): case OP2(':', '='):
    case OP2(':', ':'): case OP2('|', '|'):
        return 1;
    default:
        return 0;
    }
}

/**
 * @brief Tell whether the line at the lexer's position starts with a word
 *        that cannot continue a statement (a forgotten bracket).
 * @param P The parser.
 * @return Non-zero if it does.
 */
static int next_line_stops(const Parser *P)
{
    size_t i = P->pos, start;
    const Word *w;
    while (i < P->length && (P->src[i] == ' ' || P->src[i] == '\t')) i++;
    start = i;
    while (i < P->length && is_name_part((unsigned char)P->src[i])) i++;
    if (i == start) return 0;
    w = find_word(P->src + start, i - start);
    return w && (w->op == OP_PROC || w->op == OP_ENDPROC || w->op == OP_OBJECT ||
                 w->op == OP_ENDOBJECT || w->op == OP_EXPORT);
}

/**
 * @brief Handle a preprocessor line.
 *
 * Reads the whole line with its continuation lines; the final line end stays
 * for the caller.
 *
 * @param P The parser, standing on the '#'.
 */
static void preprocessor(Parser *P)
{
    size_t start = P->pos + 1, i, word;
    long line = P->line;
    size_t line_start = P->line_start;
    Tok name;
    size_t nlen;

    /* the end of the line, continuation lines included */
    i = P->pos;
    for (;;) {
        while (i < P->length && P->src[i] != '\n') i++;
        if (i >= P->length) break;
        if ((i > 0 && P->src[i - 1] == '\\') || (i > 1 && P->src[i - 1] == '\r' && P->src[i - 2] == '\\')) {
            i++;
            P->line++;
            P->line_start = i;
            continue;
        }
        break;
    }
    P->pos = i;

    i = start;
    while (i < P->length && (P->src[i] == ' ' || P->src[i] == '\t')) i++;
    word = i;
    while (i < P->length && is_name_part((unsigned char)P->src[i])) i++;
    nlen = i - word;
    memset(&name, 0, sizeof(name));
    name.line = line;
    name.col = (long)(word - line_start) + 1;
    name.start = word;
    name.end = i;

    if (nlen == 6 && memcmp(P->src + word, "define", 6) == 0) {
        size_t n;
        while (i < P->length && (P->src[i] == ' ' || P->src[i] == '\t')) i++;
        n = i;
        while (i < P->length && is_name_part((unsigned char)P->src[i])) i++;
        if (i > n) {
            long sym;
            name.col = (long)(n - line_start) + 1;
            name.start = n;
            name.end = i;
            sym = add_tok_symbol(P, &name, SCOUT_SYMBOL_MACRO,
                                 P->export_all ? E_SCOPE_GLOBAL : E_SCOPE_FILE, -1, NULL);
            add_def(P, &name, DEF_MACRO, sym);
        }
    } else if ((nlen == 5 && memcmp(P->src + word, "ifdef", 5) == 0) ||
               (nlen == 6 && memcmp(P->src + word, "ifndef", 6) == 0)) {
        if (P->pp_depth < MAX_CONDITIONALS) tok_range(&name, &P->pp_open[P->pp_depth]);
        P->pp_depth++;
    } else if (nlen == 5 && memcmp(P->src + word, "endif", 5) == 0) {
        if (P->pp_depth == 0) {
            add_diagnostic(P, SCOUT_SEVERITY_ERROR, name.line, name.col, name.col + 5,
                           "#endif without #ifdef", NULL, 0);
        } else {
            P->pp_depth--;
        }
    } else if ((nlen == 4 && memcmp(P->src + word, "else", 4) == 0) ||
               (nlen >= 4 && memcmp(P->src + word, "elif", 4) == 0)) {
        if (P->pp_depth == 0) {
            add_diagnostic(P, SCOUT_SEVERITY_ERROR, name.line, name.col, name.col + (long)nlen,
                           "#else without #ifdef", NULL, 0);
        }
    }
}

/**
 * @brief Read the next statement into the tokens.
 * @param P The parser.
 * @return Non-zero if there is one; 0 at the end of the source, or when
 *         the analysis has to stop.
 */
static int read_statement(Parser *P)
{
    P->tok_count = 0;
    P->bracket_depth = 0;
    if (++P->statements % STOP_INTERVAL == 0 && e_parser_should_stop && e_parser_should_stop()) {
        P->res->stopped = 1;
        return 0;
    }
    while (P->pos < P->length) {
        char c = P->src[P->pos];
        char d = P->pos + 1 < P->length ? P->src[P->pos + 1] : '\0';
        if (P->res->out_of_memory) return 0;
        if (c == '\n') {
            P->pos++;
            P->line++;
            P->line_start = P->pos;
            if (P->tok_count == 0) continue;
            if (continues(P) && !next_line_stops(P)) continue;
            break;
        }
        if (c == ' ' || c == '\t' || c == '\r' || c == '\f') {
            P->pos++;
        } else if (c == ';') {
            P->pos++;
            if (P->tok_count > 0) break;
        } else if (c == '/' && d == '*') {
            skip_block_comment(P);
        } else if ((c == '-' && d == '>') || (c == '/' && d == '/')) {
            while (P->pos < P->length && P->src[P->pos] != '\n') P->pos++;
        } else if (c == '#' && P->tok_count == 0) {
            preprocessor(P);
        } else if (c == '\'' || c == '"') {
            read_string(P, c);
        } else if (is_digit((unsigned char)c) || (c == '$' && is_name_part((unsigned char)d)) ||
                   (c == '%' && (d == '0' || d == '1'))) {
            read_number(P);
        } else if (is_name_start((unsigned char)c)) {
            size_t start = P->pos;
            Tok *t;
            while (P->pos < P->length && is_name_part((unsigned char)P->src[P->pos])) P->pos++;
            t = push_tok(P, start, P->pos, T_WORD, 0);
            if (t) t->word = find_word(P->src + start, P->pos - start);
        } else {
            read_operator(P);
        }
    }
    if (P->tok_count == 0) return 0;
    if (P->bracket_depth > 0 && P->bracket_depth <= MAX_BRACKETS) {
        const Tok *t = &P->toks[P->brackets[P->bracket_depth - 1]];
        char text[2];
        text[0] = P->src[t->start];
        text[1] = '\0';
        structure_error(P, t, "'%s' not closed", text);
    }
    return 1;
}

/**
 * @brief The scope of a definition outside of PROCs and OBJECTs.
 * @param P The parser.
 * @param exported It has EXPORT.
 * @return Its scope.
 */
static EScope outer_scope(const Parser *P, int exported)
{
    return exported || P->export_all ? E_SCOPE_GLOBAL : E_SCOPE_FILE;
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
    const char *format = NULL;
    switch (b->type) {
    case B_IF: format = "IF without ENDIF"; break;
    case B_FOR: format = "FOR without ENDFOR"; break;
    case B_WHILE: format = "WHILE without ENDWHILE"; break;
    case B_REPEAT: format = "REPEAT without UNTIL"; break;
    case B_LOOP: format = "LOOP without ENDLOOP"; break;
    case B_SELECT: format = "SELECT without ENDSELECT"; break;
    case B_TRY: format = "TRY without ENDTRY"; break;
    }
    if (P->lenient) return;
    add_diagnostic(P, SCOUT_SEVERITY_ERROR, b->line, b->col, b->end, format, NULL, 0);
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
 * @param keep Leave the block open (ELSE, CASE, ...).
 */
static void close_block(Parser *P, BlockType type, const Tok *t, int keep)
{
    long i, k;
    for (i = P->block_count - 1; i >= 0; i--) {
        if (P->blocks[i].type == type) break;
    }
    if (i < 0) {
        structure_error(P, t, "%s without a matching block", t->word->name);
        return;
    }
    for (k = i + 1; k < P->block_count; k++) report_open_block(P, &P->blocks[k]);
    P->block_count = keep ? i + 1 : i;
}

/**
 * @brief Tell whether a statement has a keyword outside of brackets.
 * @param P The parser.
 * @param from The first token.
 * @param op The keyword's role.
 * @return Non-zero if it has.
 */
static int has_keyword(const Parser *P, long from, Op op)
{
    long i, depth = 0;
    for (i = from; i < P->tok_count; i++) {
        const Tok *t = &P->toks[i];
        if (is_opener(t)) depth++;
        else if (is_closer(t)) depth--;
        else if (depth <= 0 && is_kw(t, op)) return 1;
    }
    return 0;
}

/**
 * @brief Skip a type: LONG, PTR TO x, ARRAY OF x or an object's name.
 * @param P The parser.
 * @param i The token of its first word.
 * @return The token after it.
 */
static long skip_type(const Parser *P, long i)
{
    long n = P->tok_count;
    if (i < n && is_kw(&P->toks[i], OP_PTR)) {
        i++;
        if (i < n && is_kw(&P->toks[i], OP_TO)) i += 2;
        return i;
    }
    if (i < n && P->toks[i].type == T_WORD && P->toks[i].word && P->toks[i].word->op == OP_TYPE &&
        i + 1 < n && is_kw(&P->toks[i + 1], OP_OF)) {
        return i + 3;
    }
    return i + 1;
}

/**
 * @brief Read a list of names (CONST, ENUM, SET): the first word of each
 *        comma-separated part defines a constant.
 * @param P The parser.
 * @param i The token after the keyword.
 * @param exported It has EXPORT.
 * @param keyword The keyword, as the symbols' detail.
 */
static void read_constants(Parser *P, long i, int exported, const char *keyword)
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
                const Def *d = find_def(P, P->src + t->start, t->end - t->start);
                long sym;
                if (d && d->kind == DEF_CONSTANT) defined_twice(P, t);
                sym = add_tok_symbol(P, t, SCOUT_SYMBOL_CONSTANT, outer_scope(P, exported), -1,
                                     dup_text(P, keyword, strlen(keyword)));
                add_def(P, t, DEF_CONSTANT, sym);
            }
            expect = 0;
        }
    }
}

/**
 * @brief Read a DEF statement.
 * @param P The parser.
 * @param i The token after the keyword.
 * @param exported It has EXPORT.
 */
static void read_def(Parser *P, long i, int exported)
{
    long n = P->tok_count;
    while (i < n) {
        const Tok *name = &P->toks[i];
        char *detail = NULL;
        long sym;
        if (!is_name(name)) {
            i++;
            continue;
        }
        i++;
        if (i < n && is_op(&P->toks[i], '[')) {
            long depth = 0;
            for (; i < n; i++) {
                if (is_opener(&P->toks[i])) depth++;
                else if (is_closer(&P->toks[i]) && --depth == 0) {
                    i++;
                    break;
                }
            }
        }
        if (i < n && is_op(&P->toks[i], ':')) {
            long end = skip_type(P, i + 1);
            if (i + 1 < n) detail = slice_detail(P, &P->toks[i + 1], &P->toks[(end <= n ? end : n) - 1]);
            i = end;
        }
        if (P->in_proc) {
            add_local(P, name, 0);
            add_tok_symbol(P, name, SCOUT_SYMBOL_VARIABLE, E_SCOPE_LOCAL, P->proc_symbol, detail);
        } else {
            sym = add_tok_symbol(P, name, SCOUT_SYMBOL_VARIABLE, outer_scope(P, exported), -1, detail);
            add_def(P, name, DEF_GLOBAL, sym);
        }
        /* an initial value may hold anything, up to the next comma */
        {
            long depth = 0;
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
}

/**
 * @brief End the PROC that is open.
 * @param P The parser.
 * @param last The last token of the PROC.
 */
static void end_proc(Parser *P, const Tok *last)
{
    if (P->pass == 1 && P->proc_symbol >= 0) {
        ERange *r = &P->res->symbols[P->proc_symbol].range;
        r->end_line = last->line;
        r->end_col = last->col + (long)(last->end - last->start);
    }
    P->in_proc = 0;
    P->proc_handle = 0;
    P->proc_symbol = -1;
    P->block_count = 0;
    P->reset_locals = 1;
}

/**
 * @brief Read a PROC statement.
 * @param P The parser.
 * @param i The token of the keyword.
 * @param exported It has EXPORT.
 */
static void read_proc(Parser *P, long i, int exported)
{
    long n = P->tok_count, j = i + 1, close_at = -1, of_at = -1;
    int is_single = 0, handle = 0;
    const Tok *kw = &P->toks[i], *name;
    long sym = -1;

    if (P->in_proc) {
        structure_error_at(P, &P->proc_head, "PROC without ENDPROC", NULL);
        if (P->pass == 1 && P->proc_symbol >= 0) {
            ERange *r = &P->res->symbols[P->proc_symbol].range;
            r->end_line = kw->line;
            r->end_col = kw->col;
        }
        P->in_proc = 0;
        P->block_count = 0;
    }
    P->local_count = 0;
    P->reset_locals = 0;
    P->has_proc = 1;
    if (j >= n || !is_name(&P->toks[j])) {
        structure_error(P, kw, "PROC without a name", NULL);
        P->in_proc = 1;
        P->proc_symbol = -1;
        tok_range(kw, &P->proc_head);
        return;
    }
    name = &P->toks[j++];

    /* find the end of the parameters and what follows them */
    if (j < n && is_op(&P->toks[j], '(')) {
        long k, depth = 0;
        for (k = j; k < n; k++) {
            if (is_opener(&P->toks[k])) depth++;
            else if (is_closer(&P->toks[k]) && --depth == 0) {
                close_at = k;
                break;
            }
        }
    }
    {
        long k;
        for (k = close_at >= 0 ? close_at + 1 : j; k < n; k++) {
            const Tok *t = &P->toks[k];
            if (is_kw(t, OP_IS) || is_kw(t, OP_RETURN)) {
                is_single = 1;
                break;
            }
            if (is_kw(t, OP_HANDLE)) handle = 1;
            if (is_kw(t, OP_OF) && k + 1 < n && is_name(&P->toks[k + 1])) of_at = k + 1;
        }
    }

    if (P->pass == 1) {
        ERange r, sel;
        char *detail = NULL;
        long parent = -1;
        tok_range(kw, &r);
        tok_range(name, &sel);
        if (close_at >= 0) detail = slice_detail(P, &P->toks[j], &P->toks[of_at >= 0 ? of_at : close_at]);
        if (of_at >= 0) {
            const Tok *o = &P->toks[of_at];
            const Def *d = find_def(P, P->src + o->start, o->end - o->start);
            if (d && d->kind == DEF_OBJECT) parent = d->symbol;
        } else {
            const Def *d = find_def(P, P->src + name->start, name->end - name->start);
            if (d && d->kind == DEF_PROC) defined_twice(P, name);
        }
        sym = add_symbol(P, P->src + name->start, name->end - name->start,
                         of_at >= 0 ? SCOUT_SYMBOL_METHOD : SCOUT_SYMBOL_FUNCTION,
                         outer_scope(P, exported), parent, detail, &r, &sel);
        if (of_at < 0) add_def(P, name, DEF_PROC, sym);
        if (name->end - name->start == 4 && memcmp(P->src + name->start, "main", 4) == 0) P->has_main = 1;
    }

    /* the parameters: the first word of each comma-separated part */
    if (close_at >= 0) {
        long k, depth = 0;
        int expect = 1;
        for (k = j + 1; k < close_at; k++) {
            const Tok *t = &P->toks[k];
            if (is_opener(t)) depth++;
            else if (is_closer(t)) depth--;
            else if (depth == 0 && is_op(t, ',')) expect = 1;
            else if (depth == 0 && expect && is_name(t)) {
                char *detail = NULL;
                expect = 0;
                add_local(P, t, 1);
                if (k + 2 < close_at && is_op(t + 1, ':')) {
                    long end = skip_type(P, k + 2);
                    if (end > close_at) end = close_at;
                    detail = slice_detail(P, &P->toks[k + 2], &P->toks[end - 1]);
                }
                add_tok_symbol(P, t, SCOUT_SYMBOL_VARIABLE, E_SCOPE_LOCAL, sym, detail);
            }
        }
    }

    if (is_single) {
        if (P->pass == 1 && sym >= 0) {
            const Tok *last = &P->toks[n - 1];
            P->res->symbols[sym].range.end_line = last->line;
            P->res->symbols[sym].range.end_col = last->col + (long)(last->end - last->start);
        }
        P->reset_locals = 1;
        return;
    }
    P->in_proc = 1;
    P->proc_handle = handle;
    P->proc_symbol = sym;
    tok_range(name, &P->proc_head);
}

/**
 * @brief Read the header of an OBJECT.
 * @param P The parser.
 * @param i The token of the keyword.
 * @param exported It has EXPORT.
 */
static void read_object(Parser *P, long i, int exported)
{
    const Tok *kw = &P->toks[i];
    const Tok *name = i + 1 < P->tok_count ? &P->toks[i + 1] : NULL;
    if (P->in_proc) {
        structure_error_at(P, &P->proc_head, "PROC without ENDPROC", NULL);
        end_proc(P, kw);
    }
    if (P->in_object) structure_error_at(P, &P->object_head, "OBJECT without ENDOBJECT", NULL);
    P->in_object = 1;
    P->object_symbol = -1;
    tok_range(kw, &P->object_head);
    if (!name || !is_name(name)) {
        structure_error(P, kw, "OBJECT without a name", NULL);
        return;
    }
    tok_range(name, &P->object_head);
    if (P->pass == 1) {
        const Def *d = find_def(P, P->src + name->start, name->end - name->start);
        ERange r, sel;
        char *detail = NULL;
        if (d && d->kind == DEF_OBJECT) defined_twice(P, name);
        if (i + 3 < P->tok_count && is_kw(&P->toks[i + 2], OP_OF)) {
            detail = slice_detail(P, &P->toks[i + 2], &P->toks[i + 3]);
        }
        tok_range(kw, &r);
        tok_range(name, &sel);
        P->object_symbol = add_symbol(P, P->src + name->start, name->end - name->start, SCOUT_SYMBOL_STRUCT,
                                      outer_scope(P, exported), -1, detail, &r, &sel);
        add_def(P, name, DEF_OBJECT, P->object_symbol);
    }
}

/**
 * @brief Read a statement inside an OBJECT: members, in any number, also
 *        in the bracketed groups of a UNION.
 * @param P The parser.
 * @param i The first token.
 */
static void read_members(Parser *P, long i)
{
    long n = P->tok_count;
    while (i < n) {
        const Tok *name = &P->toks[i];
        char *detail = NULL;
        if (!is_name(name)) {
            i++;
            continue;
        }
        i++;
        if (i < n && is_op(&P->toks[i], '[')) {
            long depth = 0;
            for (; i < n; i++) {
                if (is_opener(&P->toks[i])) depth++;
                else if (is_closer(&P->toks[i]) && --depth == 0) {
                    i++;
                    break;
                }
            }
        }
        if (i < n && is_op(&P->toks[i], ':')) {
            long end = skip_type(P, i + 1);
            if (i + 1 < n) detail = slice_detail(P, &P->toks[i + 1], &P->toks[(end <= n ? end : n) - 1]);
            i = end;
        }
        add_tok_symbol(P, name, SCOUT_SYMBOL_FIELD, E_SCOPE_LOCAL, P->object_symbol, detail);
        if (i < n && is_op(&P->toks[i], '=')) {
            while (i < n && !is_op(&P->toks[i], ',') && !is_op(&P->toks[i], ']')) i++;
        }
    }
}

/**
 * @brief Read an OPT statement.
 * @param P The parser.
 * @param i The token after the keyword.
 */
static void read_opt(Parser *P, long i)
{
    for (; i < P->tok_count; i++) {
        if (is_kw(&P->toks[i], OP_MODULE)) P->is_module = 1;
        else if (is_kw(&P->toks[i], OP_EXPORT)) P->export_all = 1;
    }
}

/**
 * @brief Read a MODULE statement.
 * @param P The parser.
 * @param i The token after the keyword.
 */
static void read_module_statement(Parser *P, long i)
{
    EResult *res = P->res;
    if (P->pass != 1) return;
    for (; i < P->tok_count; i++) {
        const Tok *t = &P->toks[i];
        size_t start, end;
        EReference *r;
        char *copy;
        int local;
        if (t->type != T_STRING || t->end - t->start < 3) continue;
        start = t->start + 1;
        end = P->src[t->end - 1] == '\'' ? t->end - 1 : t->end;
        if (end <= start) continue;
        local = P->src[start] == '*';
        if (local) start++;
        if (end <= start) continue;
        if (!grow(P, (void **)&res->references, res->reference_count, &P->reference_cap, sizeof(EReference))) return;
        copy = dup_text(P, P->src + start, end - start);
        if (!copy) return;
        r = &res->references[res->reference_count++];
        r->target = copy;
        r->local = local;
        tok_range(t, &r->range);
    }
}

/**
 * @brief Follow one statement: definitions and the block structure.
 * @param P The parser.
 */
static void process_statement(Parser *P)
{
    long i = 0, n = P->tok_count;
    int exported = 0;
    const Tok *t, *last = &P->toks[n - 1];

    if (P->reset_locals) {
        P->local_count = 0;
        P->reset_locals = 0;
    }

    /* what may come before the statement proper */
    for (; i < n; ) {
        t = &P->toks[i];
        if (!P->in_object && is_name(t) && i + 1 < n && is_op(&P->toks[i + 1], ':')) {
            long sym = add_tok_symbol(P, t, SCOUT_SYMBOL_LABEL, outer_scope(P, 0), -1, NULL);
            add_def(P, t, DEF_LABEL, sym);
            i += 2;
        } else if (is_kw(t, OP_ELSE)) {
            close_block(P, B_IF, t, 1);
            i++;
        } else if (is_kw(t, OP_EXPORT)) {
            exported = 1;
            i++;
        } else {
            break;
        }
    }
    P->content_start = i;
    if (i >= n) return;
    t = &P->toks[i];

    if (P->in_object && !is_kw(t, OP_PROC) && !is_kw(t, OP_ENDPROC) && !is_kw(t, OP_OBJECT) &&
        !is_kw(t, OP_ENDOBJECT)) {
        read_members(P, i);
        return;
    }
    if (t->type != T_WORD || !t->word) return;

    switch (t->word->op) {
    case OP_OPT: read_opt(P, i + 1); break;
    case OP_MODULE: read_module_statement(P, i + 1); break;
    case OP_CONST: read_constants(P, i + 1, exported, "CONST"); break;
    case OP_ENUM: read_constants(P, i + 1, exported, "ENUM"); break;
    case OP_SET: read_constants(P, i + 1, exported, "SET"); break;
    case OP_DEF: read_def(P, i + 1, exported); break;
    case OP_PROC:
        if (P->in_object) {
            structure_error_at(P, &P->object_head, "OBJECT without ENDOBJECT", NULL);
            P->in_object = 0;
        }
        read_proc(P, i, exported);
        break;
    case OP_ENDPROC:
        if (!P->in_proc) {
            structure_error(P, t, "ENDPROC without PROC", NULL);
        } else {
            close_all_blocks(P);
            end_proc(P, last);
        }
        break;
    case OP_EXCEPT:
        if (!P->in_proc || !P->proc_handle) {
            structure_error(P, t, "EXCEPT without PROC ... HANDLE", NULL);
        }
        close_all_blocks(P);
        break;
    case OP_OBJECT: read_object(P, i, exported); break;
    case OP_ENDOBJECT:
        if (!P->in_object) {
            structure_error(P, t, "ENDOBJECT without OBJECT", NULL);
        } else {
            if (P->pass == 1 && P->object_symbol >= 0) {
                ERange *r = &P->res->symbols[P->object_symbol].range;
                r->end_line = t->line;
                r->end_col = t->col + (long)(t->end - t->start);
            }
            P->in_object = 0;
        }
        break;
    case OP_IF:
        if (!has_keyword(P, i, OP_THEN)) open_block(P, B_IF, t);
        break;
    case OP_ELSEIF: close_block(P, B_IF, t, 1); break;
    case OP_ENDIF: close_block(P, B_IF, t, 0); break;
    case OP_FOR:
        if (!has_keyword(P, i, OP_DO)) open_block(P, B_FOR, t);
        break;
    case OP_ENDFOR: close_block(P, B_FOR, t, 0); break;
    case OP_WHILE:
        if (!has_keyword(P, i, OP_DO)) open_block(P, B_WHILE, t);
        break;
    case OP_ELSEWHILE: close_block(P, B_WHILE, t, 1); break;
    case OP_ENDWHILE: close_block(P, B_WHILE, t, 0); break;
    case OP_REPEAT: open_block(P, B_REPEAT, t); break;
    case OP_UNTIL: close_block(P, B_REPEAT, t, 0); break;
    case OP_LOOP: open_block(P, B_LOOP, t); break;
    case OP_ENDLOOP: close_block(P, B_LOOP, t, 0); break;
    case OP_SELECT: open_block(P, B_SELECT, t); break;
    case OP_CASE: close_block(P, B_SELECT, t, 1); break;
    case OP_ENDSELECT: close_block(P, B_SELECT, t, 0); break;
    case OP_TRY: open_block(P, B_TRY, t); break;
    case OP_CATCH: close_block(P, B_TRY, t, 1); break;
    case OP_ENDTRY: close_block(P, B_TRY, t, 0); break;
    default: break;
    }
}

/**
 * @brief Tell whether the statement looks like inline assembly.
 *
 * A statement of E does not start with a name in capitals that is not
 * called or assigned, nor with an operator word.
 *
 * @param P The parser.
 * @return Non-zero if it does.
 */
static int looks_like_assembly(Parser *P)
{
    const Tok *t = &P->toks[P->content_start];
    long next = P->content_start + 1;
    size_t i;
    if (t->type != T_WORD) return 0;
    if (t->word) return t->word->op == OP_CONT || t->word->op == OP_NOT;
    for (i = t->start; i < t->end; i++) {
        unsigned char c = (unsigned char)P->src[i];
        if (!is_upper(c) && !is_digit(c)) return 0;
    }
    if (next < P->tok_count && (is_op(&P->toks[next], '(') || is_op(&P->toks[next], OP2(':', '=')))) return 0;
    return find_def(P, P->src + t->start, t->end - t->start) == NULL;
}

/**
 * @brief Tell whether a word stands where only a type can.
 * @param P The parser.
 * @param i The word's index.
 * @return Non-zero if it does.
 */
static int at_type_position(const Parser *P, long i)
{
    const Tok *t = &P->toks[i], *prev;
    if (i == 0) return 0;
    prev = &P->toks[i - 1];
    if (is_kw(prev, OP_OBJECT) || is_kw(prev, OP_SIZEOF) || is_op(prev, OP2(':', ':'))) return 1;
    if (is_kw(prev, OP_TO)) return i >= 2 && is_kw(&P->toks[i - 2], OP_PTR);
    if (is_kw(prev, OP_OF)) return !is_kw(&P->toks[P->content_start], OP_SELECT);
    return is_op(prev, ':') && i != P->content_start && is_lower((unsigned char)P->src[t->start]);
}

/**
 * @brief Make the highlighting tokens of the statement.
 * @param P The parser.
 */
static void emit_tokens(Parser *P)
{
    long i;
    if (P->content_start >= P->tok_count) return;
    if (is_kw(&P->toks[P->content_start], OP_OPT)) return;
    if (!P->in_object && looks_like_assembly(P)) return;
    for (i = P->content_start; i < P->tok_count; i++) {
        const Tok *t = &P->toks[i];
        const char *text = P->src + t->start;
        size_t len = t->end - t->start;
        const Local *l;
        const Def *d;
        if (!is_name(t)) continue;
        if (i > 0 && is_op(&P->toks[i - 1], '.')) continue;
        if (at_type_position(P, i)) {
            add_token(P, t, SCOUT_TOKEN_TYPE);
            continue;
        }
        if (P->in_object) continue;
        l = find_local(P, text, len);
        if (l) {
            if (l->parameter) add_token(P, t, SCOUT_TOKEN_PARAMETER);
            continue;
        }
        d = find_def(P, text, len);
        if (d) {
            switch (d->kind) {
            case DEF_PROC: add_token(P, t, SCOUT_TOKEN_FUNCTION); break;
            case DEF_OBJECT: add_token(P, t, SCOUT_TOKEN_TYPE); break;
            case DEF_CONSTANT: add_token(P, t, SCOUT_TOKEN_CONSTANT); break;
            case DEF_MACRO: add_token(P, t, SCOUT_TOKEN_MACRO); break;
            default: break;
            }
            continue;
        }
        if (len >= 2 && is_upper((unsigned char)text[0]) && (is_upper((unsigned char)text[1]) || text[1] == '_')) {
            add_token(P, t, SCOUT_TOKEN_CONSTANT);
        } else if (i + 1 < P->tok_count && is_op(&P->toks[i + 1], '(')) {
            add_token(P, t, SCOUT_TOKEN_FUNCTION);
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
    P->block_count = 0;
    P->local_count = 0;
    P->reset_locals = 0;
    P->in_proc = 0;
    P->proc_handle = 0;
    P->proc_symbol = -1;
    P->in_object = 0;
    P->object_symbol = -1;
    P->pp_depth = 0;
    while (read_statement(P)) {
        process_statement(P);
        if (pass == 2) emit_tokens(P);
        if (P->res->out_of_memory) return;
    }
}

/** Reads a compiled module, big-endian and with every access checked. */
typedef struct ModReader {
    const unsigned char *d; /**< the module */
    size_t n;               /**< its length */
    size_t o;               /**< where it stands */
    int bad;                /**< it ran over the end */
} ModReader;

/**
 * @brief Read a 16-bit value.
 * @param r The reader.
 * @return It, signed; 0 past the end (then ModReader::bad is set).
 */
static long m_int(ModReader *r)
{
    long v;
    if (r->bad || r->n < 2 || r->o > r->n - 2) {
        r->bad = 1;
        return 0;
    }
    v = ((long)r->d[r->o] << 8) | (long)r->d[r->o + 1];
    r->o += 2;
    return v >= 0x8000L ? v - 0x10000L : v;
}

/**
 * @brief Read a 32-bit value.
 * @param r The reader.
 * @return It, signed; 0 past the end (then ModReader::bad is set).
 */
static long m_long(ModReader *r)
{
    unsigned long v;
    if (r->bad || r->n < 4 || r->o > r->n - 4) {
        r->bad = 1;
        return 0;
    }
    v = ((unsigned long)r->d[r->o] << 24) | ((unsigned long)r->d[r->o + 1] << 16) |
        ((unsigned long)r->d[r->o + 2] << 8) | (unsigned long)r->d[r->o + 3];
    r->o += 4;
    if (v >= 0x80000000UL) return -(long)(0xFFFFFFFFUL - v) - 1;
    return (long)v;
}

/**
 * @brief Skip bytes.
 * @param r The reader.
 * @param count How many; a negative count or one past the end sets
 *              ModReader::bad.
 */
static void m_skip(ModReader *r, long count)
{
    if (r->bad || count < 0 || (size_t)count > r->n - r->o) {
        r->bad = 1;
        return;
    }
    r->o += (size_t)count;
}

/**
 * @brief Add a symbol of a module.
 * @param P The parser.
 * @param name Its name; padding NULs at the end are left out.
 * @param len Its length, with them.
 * @param kind Its SCOUT_SYMBOL_ kind.
 * @param scope Where it can be seen.
 * @param parent Index of its parent, or -1.
 * @return Its index, or -1, also for what is no name (a damaged module).
 */
static long module_symbol(Parser *P, const unsigned char *name, size_t len, int kind, EScope scope, long parent)
{
    static const ERange first = { 1, 1, 1, 1 };
    size_t i;
    while (len > 0 && name[len - 1] == 0) len--;
    if (len == 0 || !is_name_start(name[0])) return -1;
    for (i = 1; i < len; i++) {
        if (!is_name_part(name[i])) return -1;
    }
    return add_symbol(P, (const char *)name, len, kind, scope, parent, NULL, &first, &first);
}

/**
 * @brief Read a name of a module and make it a symbol.
 * @param P The parser.
 * @param r The reader, standing on the name.
 * @param len The length of the name.
 * @param kind Its SCOUT_SYMBOL_ kind.
 * @param scope Where it can be seen.
 * @param parent Index of its parent, or -1.
 * @return Its index, or -1.
 */
static long m_symbol(Parser *P, ModReader *r, long len, int kind, EScope scope, long parent)
{
    const unsigned char *name;
    if (len < 0 || r->bad || (size_t)len > r->n - r->o) {
        r->bad = 1;
        return -1;
    }
    name = r->d + r->o;
    r->o += (size_t)len;
    return module_symbol(P, name, (size_t)len, kind, scope, parent);
}

/**
 * @brief Read the constants of a module.
 * @param P The parser.
 * @param r The reader, after the chunk's number.
 * @param v The module's format.
 */
static void m_constants(Parser *P, ModReader *r, long v)
{
    long len;
    if (v >= 6) m_skip(r, 4);
    while ((len = m_int(r)) != 0 && !r->bad) {
        if (len == -256) {
            long value;
            len = m_int(r);
            m_symbol(P, r, len, SCOUT_SYMBOL_CONSTANT, E_SCOPE_GLOBAL, -1);
            value = m_int(r);
            m_skip(r, value + 1);
            if (r->o & 1) m_skip(r, 1);
        } else {
            m_skip(r, 4);
            m_symbol(P, r, len, SCOUT_SYMBOL_CONSTANT, E_SCOPE_GLOBAL, -1);
        }
    }
}

/**
 * @brief Read an object of a module.
 * @param P The parser.
 * @param r The reader, after the chunk's number.
 * @param v The module's format.
 */
static void m_object(Parser *P, ModReader *r, long v)
{
    long len, sym, c;
    if (v >= 6) m_skip(r, 4);
    if (v >= 15) m_skip(r, 2);
    len = m_int(r);
    m_skip(r, 4);
    sym = m_symbol(P, r, len, SCOUT_SYMBOL_STRUCT, E_SCOPE_GLOBAL, -1);
    while ((len = m_int(r)) != 0 && !r->bad) {
        m_skip(r, 4);
        if (len > 0) m_symbol(P, r, len, SCOUT_SYMBOL_FIELD, E_SCOPE_LOCAL, sym);
        if (v >= 12) {
            long dims;
            if (v >= 13) m_skip(r, 2);
            m_skip(r, 2);
            dims = m_int(r);
            m_skip(r, dims * 2);
        }
        if (v >= 6) {
            c = m_int(r);
            if (c < 0) {
                len = m_int(r);
                m_skip(r, len);
            }
        }
    }
    m_skip(r, 2);
    if (v < 7 || r->bad || m_int(r) == 0) return;
    m_skip(r, 4);
    len = m_int(r);
    m_skip(r, len + 4);
    while ((c = m_int(r)) != -1 && !r->bad) {
        long narg, regs;
        m_skip(r, 2);
        len = m_int(r);
        m_symbol(P, r, len, SCOUT_SYMBOL_METHOD, E_SCOPE_GLOBAL, sym);
        narg = m_int(r);
        regs = m_int(r);
        m_skip(r, regs * 4);
        if (r->o + 2 <= r->n && r->d[r->o] == 0x80 && r->d[r->o + 1] == 0) {
            long c2;
            r->o += 2;
            c2 = m_int(r);
            if (narg && c2) m_skip(r, c2);
        }
    }
    while (m_int(r) != -1 && !r->bad) m_skip(r, 4);
}

/**
 * @brief Read the procedures and labels of a module.
 * @param P The parser.
 * @param r The reader, after the chunk's number.
 */
static void m_procs(Parser *P, ModReader *r)
{
    long len;
    while ((len = m_int(r)) > 0 && !r->bad) {
        long type, c;
        const unsigned char *name;
        if ((size_t)len > r->n - r->o) {
            r->bad = 1;
            return;
        }
        name = r->d + r->o;
        m_skip(r, len + 4);
        type = m_int(r);
        module_symbol(P, name, (size_t)len, type == 1 ? SCOUT_SYMBOL_FUNCTION : SCOUT_SYMBOL_LABEL,
                      E_SCOPE_GLOBAL, -1);
        if (type != 1) continue;
        m_skip(r, 2);
        m_skip(r, 2);
        c = m_int(r);
        if (c == -1) {
            c = m_int(r);
            m_skip(r, c * 2);
            c = m_int(r);
        }
        if (c == -2) {
            m_skip(r, 2);
            c = m_int(r);
        }
        m_skip(r, c * 4);
        c = m_int(r);
        m_skip(r, c);
    }
}

/**
 * @brief Read the global variables of a module.
 * @param P The parser.
 * @param r The reader, after the chunk's number.
 * @param v The module's format.
 */
static void m_globals(Parser *P, ModReader *r, long v)
{
    long val = 0;
    if (r->o + 2 <= r->n && r->d[r->o] == 0x80 && r->d[r->o + 1] == 0) m_skip(r, 6);
    if (v >= 14) val = m_int(r);
    while (val != -1 && !r->bad) {
        long len = m_int(r);
        long dims;
        if (len < 0) break;
        if (len > 0) {
            m_symbol(P, r, len, SCOUT_SYMBOL_VARIABLE, E_SCOPE_GLOBAL, -1);
            if (v >= 15) {
                m_skip(r, 6);
                m_skip(r, 4);
                dims = m_int(r);
                if (dims >= 0) m_skip(r, dims * 2);
            }
        } else if (v >= 15) {
            m_skip(r, 10);
            dims = m_int(r);
            if (dims >= 0) m_skip(r, dims * 2);
        }
        while (m_long(r) != 0 && !r->bad) {
            if (v >= 10) m_skip(r, 2);
        }
        val = v >= 14 ? m_int(r) : 0;
    }
}

/**
 * @brief Read the note of what a module uses from other modules.
 * @param r The reader, after the chunk's number.
 */
static void m_modinfo(ModReader *r)
{
    long len;
    m_skip(r, 4);
    while ((len = m_int(r)) != 0 && !r->bad) {
        long c;
        m_skip(r, len);
        while ((c = m_int(r)) != 0 && !r->bad) {
            len = m_int(r);
            m_skip(r, len);
            if (c == 2) {
                m_skip(r, 2);
                c = m_int(r);
                m_skip(r, c * 4);
            } else {
                c = m_int(r);
                m_skip(r, c * 6);
            }
        }
    }
}

/**
 * @brief Read the macros of a module.
 * @param P The parser.
 * @param r The reader, after the chunk's number.
 */
static void m_macros(Parser *P, ModReader *r)
{
    long len;
    while ((len = m_int(r)) != 0 && !r->bad) {
        m_symbol(P, r, len, SCOUT_SYMBOL_MACRO, E_SCOPE_GLOBAL, -1);
        m_skip(r, 4);
        len = m_int(r);
        m_skip(r, len);
    }
}

/**
 * @brief Read the library functions of a module (LIBRARY).
 * @param P The parser.
 * @param r The reader, after the chunk's number.
 */
static void m_library(Parser *P, ModReader *r)
{
    int strings = 2;
    size_t i = r->o;
    while (strings > 0 && i < r->n) {
        if (r->d[i++] == 0) strings--;
    }
    while (i < r->n && r->d[i] != 0xFF) {
        size_t start = i;
        if (r->d[i] == 16) {
            i++;
            continue;
        }
        while (i < r->n && r->d[i] > ' ') i++;
        if (i >= r->n) break;
        if (i == start) {   /* damaged: no name here, and no way on but this */
            i++;
            continue;
        }
        module_symbol(P, r->d + start, i - start, SCOUT_SYMBOL_FUNCTION, E_SCOPE_GLOBAL, -1);
        /* the argument registers, ended by 16 or more */
        while (i < r->n && r->d[i] < 16) i++;
        if (i < r->n && r->d[i] == 16) i++;
    }
}

/**
 * @brief Read a compiled module.
 *
 * The walk through its chunks follows ShowModule of E-VO. It stops at what
 * it does not know: all it costs is the symbols of the rest.
 *
 * @param P The parser.
 */
static void read_compiled(Parser *P)
{
    ModReader r;
    long v = 0;
    long chunks = 0;
    r.d = (const unsigned char *)P->src;
    r.n = P->length;
    r.o = 4;
    r.bad = 0;
    P->pass = 1;
    P->res->binary = 1;
    while (!r.bad && r.o < r.n && !P->res->out_of_memory) {
        long job = m_int(&r), c;
        if (++chunks % STOP_INTERVAL == 0 && e_parser_should_stop && e_parser_should_stop()) {
            P->res->stopped = 1;
            return;
        }
        switch (job) {
        case 1: m_constants(P, &r, v); break;
        case 2: m_object(P, &r, v); break;
        case 3:
            c = m_long(&r);
            if (c < 0 || c > (long)(r.n / 4)) return;
            m_skip(&r, c * 4);
            break;
        case 4: m_procs(P, &r); break;
        case 5:
            m_skip(&r, 4);
            m_skip(&r, 2);
            m_skip(&r, 4);
            m_skip(&r, 6);
            v = m_int(&r);
            if (v > 10) m_skip(&r, 4);
            m_skip(&r, 4);
            if (v > MODULE_MAX_VERSION) return;
            break;
        case 6: m_library(P, &r); return;
        case 7:
            c = m_long(&r);
            if (c < 0 || c > (long)(r.n / 4)) return;
            m_skip(&r, c * 4);
            break;
        case 8: m_globals(P, &r, v); break;
        case 9: m_modinfo(&r); break;
        case 10:
            while (m_long(&r) == 0x3F1 && !r.bad) {
                c = m_long(&r);
                m_skip(&r, 8);
                if (c < 2 || c > (long)(r.n / 4)) return;
                m_skip(&r, c * 4 - 8);
            }
            break;
        case 11: m_macros(P, &r); break;
        case 12:
            m_skip(&r, 2);
            c = m_int(&r);
            m_skip(&r, c);
            break;
        default:
            return;
        }
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
    const EDiagnostic *x = (const EDiagnostic *)a, *y = (const EDiagnostic *)b;
    if (x->range.start_line != y->range.start_line) return x->range.start_line < y->range.start_line ? -1 : 1;
    if (x->range.start_col != y->range.start_col) return x->range.start_col < y->range.start_col ? -1 : 1;
    return 0;
}

/**
 * @brief Tell whether a source has \#else or \#elif at the start of a line.
 * @param P The parser.
 * @return Non-zero if it has.
 */
static int has_alternative_branch(const Parser *P)
{
    size_t i;
    int at_start = 1;
    for (i = 0; i < P->length; i++) {
        char c = P->src[i];
        if (at_start && c == '#' && P->length - i >= 5 &&
            (memcmp(P->src + i + 1, "else", 4) == 0 || memcmp(P->src + i + 1, "elif", 4) == 0)) {
            return 1;
        }
        if (c == '\n') at_start = 1;
        else if (c != ' ' && c != '\t') at_start = 0;
    }
    return 0;
}

EResult *e_parser_analyze(const char *source, size_t length)
{
    Parser *P;
    EResult *res;
    long k;

    make_word_table();
    P = (Parser *)calloc(1, sizeof(Parser));
    if (!P) return NULL;
    res = (EResult *)calloc(1, sizeof(EResult));
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
    for (k = 0; k < DEF_SLOTS; k++) P->def_slots[k] = -1;

    if (length >= 4 && memcmp(source, "EMOD", 4) == 0) {
        read_compiled(P);
    } else {
        P->lenient = has_alternative_branch(P);
        run_pass(P, 1);
        if (!res->stopped && !res->out_of_memory) {
            if (P->in_proc) structure_error_at(P, &P->proc_head, "PROC without ENDPROC", NULL);
            close_all_blocks(P);
            if (P->in_object) structure_error_at(P, &P->object_head, "OBJECT without ENDOBJECT", NULL);
            if (P->pp_depth > 0 && P->pp_depth <= MAX_CONDITIONALS) {
                const ERange *r = &P->pp_open[P->pp_depth - 1];
                add_diagnostic(P, SCOUT_SEVERITY_ERROR, r->start_line, r->start_col, r->end_col,
                               "#ifdef without #endif", NULL, 0);
            }
            if (!P->is_module && P->has_proc && !P->has_main && !P->lenient) {
                add_diagnostic(P, SCOUT_SEVERITY_HINT, 1, 1, 1,
                               "no PROC main, and the source is not a module (OPT MODULE)", NULL, 0);
            }
            run_pass(P, 2);
        }
        if (res->diagnostic_count > 1) {
            qsort(res->diagnostics, (size_t)res->diagnostic_count, sizeof(EDiagnostic), compare_diagnostics);
        }
    }
    free(P->toks);
    free(P->defs);
    free(P->def_slots);
    free(P);
    return res;
}

void e_result_free(EResult *res)
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
