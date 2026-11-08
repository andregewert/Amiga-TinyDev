/**
 * @file pascal_parser.c
 * @brief The analysis of the Free Pascal parser of Scout: the source is
 *        read into tokens, then two passes follow its sections and blocks.
 *
 * A heuristic: expressions are not parsed, only what declares something or
 * opens and closes a block.
 */
#include "pascal_parser.h"

#include <stdlib.h>
#include <string.h>

int (*pascal_parser_should_stop)(void) = NULL;

/** Most diagnostics reported for one source. */
#define MAX_DIAGNOSTICS 200
/** Most nested blocks that are followed. */
#define MAX_BLOCKS 64
/** Most open brackets that are followed. */
#define MAX_BRACKETS 64
/** Most nested \$IFs that are followed. */
#define MAX_CONDITIONALS 32
/** Most nested routines that are followed. */
#define MAX_ROUTINES 16
/** Most parameters and local names that are remembered. */
#define MAX_LOCALS 256
/** Slots of the hash table of keywords. */
#define WORD_SLOTS 256
/** Slots of the hash table of definitions. */
#define DEF_SLOTS 1024
/** Longest text made a symbol's detail. */
#define MAX_DETAIL 100
/** How many tokens pass between two looks at pascal_parser_should_stop(). */
#define STOP_INTERVAL 1024

/** What a reserved word does for the analysis. */
typedef enum Op {
    OP_PLAIN,           /**< nothing special */
    OP_BEGIN,           /**< BEGIN */
    OP_END,             /**< END */
    OP_CASE,            /**< CASE */
    OP_TRY,             /**< TRY */
    OP_EXCEPT,          /**< EXCEPT */
    OP_FINALLY,         /**< FINALLY */
    OP_REPEAT,          /**< REPEAT */
    OP_UNTIL,           /**< UNTIL */
    OP_ASM,             /**< ASM */
    OP_IF,              /**< IF */
    OP_THEN,            /**< THEN */
    OP_ELSE,            /**< ELSE */
    OP_WHILE,           /**< WHILE */
    OP_FOR,             /**< FOR */
    OP_WITH,            /**< WITH */
    OP_DO,              /**< DO */
    OP_OF,              /**< OF */
    OP_RECORD,          /**< RECORD */
    OP_OBJECT,          /**< OBJECT */
    OP_CLASS,           /**< CLASS */
    OP_INTERFACE,       /**< INTERFACE */
    OP_MODULE,          /**< PROGRAM, UNIT, LIBRARY */
    OP_USES,            /**< USES */
    OP_IMPLEMENTATION,  /**< IMPLEMENTATION */
    OP_INITIALIZATION,  /**< INITIALIZATION */
    OP_FINALIZATION,    /**< FINALIZATION */
    OP_CONST,           /**< CONST, RESOURCESTRING */
    OP_VAR,             /**< VAR, THREADVAR */
    OP_TYPE,            /**< TYPE */
    OP_LABEL,           /**< LABEL */
    OP_PROCEDURE,       /**< PROCEDURE */
    OP_FUNCTION,        /**< FUNCTION */
    OP_CONSTRUCTOR,     /**< CONSTRUCTOR, DESTRUCTOR */
    OP_OPERATOR,        /**< OPERATOR */
    OP_PROPERTY,        /**< PROPERTY */
    OP_EXPORTS,         /**< EXPORTS */
    OP_PACKED,          /**< PACKED, BITPACKED */
    OP_STATEMENT,       /**< other words that start a statement: GOTO, RAISE */
    OP_VALUE            /**< words that can end an expression: NIL, STRING, INHERITED */
} Op;

/** A reserved word. */
typedef struct Word {
    const char *name;   /**< its text, in lower case */
    Op op;              /**< what it does */
    short soft;         /**< only reserved in some modes: can be a name */
    short next;         /**< next word of its hash slot, or -1 */
} Word;

/** The reserved words. */
static Word words[] = {
    { "and", OP_PLAIN, 0, -1 }, { "array", OP_PLAIN, 0, -1 }, { "as", OP_PLAIN, 1, -1 },
    { "asm", OP_ASM, 0, -1 }, { "begin", OP_BEGIN, 0, -1 }, { "bitpacked", OP_PACKED, 0, -1 },
    { "case", OP_CASE, 0, -1 }, { "class", OP_CLASS, 1, -1 }, { "const", OP_CONST, 0, -1 },
    { "constructor", OP_CONSTRUCTOR, 0, -1 }, { "destructor", OP_CONSTRUCTOR, 0, -1 },
    { "dispinterface", OP_INTERFACE, 1, -1 }, { "div", OP_PLAIN, 0, -1 }, { "do", OP_DO, 0, -1 },
    { "downto", OP_PLAIN, 0, -1 }, { "else", OP_ELSE, 0, -1 }, { "end", OP_END, 0, -1 },
    { "except", OP_EXCEPT, 1, -1 }, { "exports", OP_EXPORTS, 1, -1 }, { "file", OP_VALUE, 0, -1 },
    { "finalization", OP_FINALIZATION, 1, -1 }, { "finally", OP_FINALLY, 1, -1 }, { "for", OP_FOR, 0, -1 },
    { "function", OP_FUNCTION, 0, -1 }, { "goto", OP_STATEMENT, 0, -1 }, { "if", OP_IF, 0, -1 },
    { "implementation", OP_IMPLEMENTATION, 0, -1 }, { "in", OP_PLAIN, 0, -1 }, { "inherited", OP_VALUE, 0, -1 },
    { "initialization", OP_INITIALIZATION, 1, -1 }, { "interface", OP_INTERFACE, 0, -1 }, { "is", OP_PLAIN, 1, -1 },
    { "label", OP_LABEL, 0, -1 }, { "library", OP_MODULE, 1, -1 }, { "mod", OP_PLAIN, 0, -1 },
    { "nil", OP_VALUE, 0, -1 }, { "not", OP_PLAIN, 0, -1 }, { "object", OP_OBJECT, 0, -1 },
    { "of", OP_OF, 0, -1 }, { "operator", OP_OPERATOR, 1, -1 }, { "or", OP_PLAIN, 0, -1 },
    { "packed", OP_PACKED, 0, -1 }, { "procedure", OP_PROCEDURE, 0, -1 }, { "program", OP_MODULE, 0, -1 },
    { "property", OP_PROPERTY, 1, -1 }, { "raise", OP_STATEMENT, 1, -1 }, { "record", OP_RECORD, 0, -1 },
    { "repeat", OP_REPEAT, 0, -1 }, { "resourcestring", OP_CONST, 1, -1 }, { "set", OP_PLAIN, 0, -1 },
    { "shl", OP_PLAIN, 0, -1 }, { "shr", OP_PLAIN, 0, -1 }, { "string", OP_VALUE, 0, -1 },
    { "then", OP_THEN, 0, -1 }, { "threadvar", OP_VAR, 1, -1 }, { "to", OP_PLAIN, 0, -1 },
    { "try", OP_TRY, 1, -1 }, { "type", OP_TYPE, 0, -1 }, { "unit", OP_MODULE, 0, -1 },
    { "until", OP_UNTIL, 0, -1 }, { "uses", OP_USES, 0, -1 }, { "var", OP_VAR, 0, -1 },
    { "while", OP_WHILE, 0, -1 }, { "with", OP_WITH, 0, -1 }, { "xor", OP_PLAIN, 0, -1 }
};

/** Number of reserved words. */
#define WORD_COUNT ((long)(sizeof(words) / sizeof(words[0])))

/** The modifiers of a routine's head that the rules show as keywords. */
static const char *const shown_modifiers[] = {
    "abstract", "assembler", "cdecl", "external", "forward", "inline", "overload", "override",
    "public", "reintroduce", "stdcall", "syscall", "virtual", NULL
};

/** Modifiers that are common names as well: not shown as keywords. */
static const char *const other_modifiers[] = {
    "alias", "compilerproc", "cvar", "deprecated", "dynamic", "experimental", "export", "far", "final", "interrupt",
    "iocheck", "message", "mwpascal", "near", "noreturn", "nostackframe", "pascal", "platform", "register",
    "safecall", "softfloat", "static", "unimplemented", "varargs", "weakexternal", "winapi", NULL
};

/** Further words the rules show as keywords: names to the parser. */
static const char *const shown_names[] = {
    "absolute", "generic", "on", "out", "private", "protected", "published", "self", "specialize", "strict", NULL
};

const char *const pascal_parser_types[] = {
    "AnsiChar", "AnsiString", "Boolean", "Byte", "ByteBool", "Cardinal", "Char", "Comp", "Currency", "Double",
    "DWord", "Extended", "Int16", "Int32", "Int64", "Int8", "Integer", "LongBool", "LongInt", "LongWord",
    "NativeInt", "NativeUInt", "PAnsiChar", "PChar", "PtrInt", "PtrUInt", "Pointer", "PWideChar", "QWord", "Real",
    "RawByteString", "ShortInt", "ShortString", "Single", "SizeInt", "SizeUInt", "SmallInt", "Text",
    "TObject", "UInt16", "UInt32", "UInt64", "UInt8", "UnicodeString", "WideChar", "WideString", "Word", "WordBool", NULL
};

const char *const pascal_parser_builtins[] = {
    "Abs", "Addr", "Append", "ArcTan", "Assign", "Assigned", "BlockRead", "BlockWrite", "Break", "Chr", "Close",
    "Concat", "Continue", "Copy", "Cos", "Dec", "Delete", "Dispose", "Eof", "Eoln", "Erase", "Exclude", "Exit", "Exp",
    "FilePos", "FileSize", "FillChar", "Flush", "Frac", "FreeMem", "GetDir", "GetMem", "Halt", "Hi", "High", "Inc",
    "Include", "Insert", "Int", "IOResult", "Length", "Ln", "Lo", "Low", "MkDir", "Move", "New", "Odd", "Ord",
    "ParamCount", "ParamStr", "Pi", "Pos", "Pred", "Ptr", "Random", "Randomize", "Read", "ReadLn", "ReallocMem",
    "Rename", "Reset", "Rewrite", "RmDir", "Round", "RunError", "Seek", "SeekEof", "SeekEoln", "SetLength",
    "SetTextBuf", "Sin", "SizeOf", "Sqr", "Sqrt", "Str", "Succ", "Swap", "Trunc", "Truncate", "TypeOf", "UpCase",
    "Val", "Write", "WriteLn", NULL
};

const char *const pascal_parser_constants[] = { "False", "MaxInt", "MaxLongInt", "True", NULL };

/** Kinds of tokens. */
typedef enum TokType {
    T_WORD,     /**< a name or reserved word */
    T_NUMBER,   /**< a number */
    T_STRING,   /**< a string or a character constant */
    T_OP        /**< an operator or bracket */
} TokType;

/** A token. */
typedef struct Tok {
    size_t start;       /**< offset of the first byte */
    size_t end;         /**< offset after the last byte */
    long line;          /**< its line */
    long col;           /**< its column */
    const Word *word;   /**< the reserved word of a T_WORD, or NULL for a name */
    short type;         /**< what it is (TokType) */
    short conditional;  /**< between \$IF and \$ENDIF: may not be compiled */
    int code;           /**< a T_OP's character, or OP2() of its two */
} Tok;

/** The code of an operator of two characters. */
#define OP2(a, b) (((int)(a) << 8) | (int)(b))

/** Kinds of blocks of statements. */
typedef enum BlockType {
    B_BEGIN,    /**< BEGIN ... END */
    B_ASM,      /**< ASM ... END */
    B_CASE,     /**< CASE ... END */
    B_TRY,      /**< TRY ... END */
    B_REPEAT,   /**< REPEAT ... UNTIL */
    B_INIT      /**< the INITIALIZATION of a unit ... END */
} BlockType;

/** An open block. */
typedef struct Block {
    BlockType type;     /**< its kind */
    const Tok *tok;     /**< its first word */
    int in_except;      /**< a TRY after its EXCEPT or FINALLY */
} Block;

/** Kinds of definitions. */
typedef enum DefKind {
    DEF_FUNCTION,   /**< a routine */
    DEF_TYPE,       /**< a type */
    DEF_CONSTANT,   /**< a constant or enumeration member */
    DEF_VARIABLE,   /**< a variable */
    DEF_LABEL,      /**< a label */
    DEF_MACRO       /**< a \$DEFINE */
} DefKind;

/** A name defined at the outer level. */
typedef struct Def {
    size_t start;   /**< offset of its name */
    size_t length;  /**< length of its name */
    DefKind kind;   /**< what it is */
    long next;      /**< next definition of its hash slot, or -1 */
} Def;

/** A parameter or other name of the routines open. */
typedef struct Local {
    size_t start;   /**< offset of its name */
    size_t length;  /**< length of its name */
    long token;     /**< the SCOUT_TOKEN_ kind of its uses, 0 for none */
} Local;

/** A routine whose body is still to come. */
typedef struct Routine {
    long symbol;    /**< its symbol */
    long mark;      /**< number of locals before it */
    const Tok *name;/**< its name */
} Routine;

/** The sections of declarations. */
typedef enum Section {
    S_NONE,     /**< none */
    S_CONST,    /**< const */
    S_VAR,      /**< var */
    S_TYPE,     /**< type */
    S_LABEL     /**< label */
} Section;

/** The parts of a unit. */
typedef enum Part {
    PART_START,             /**< before INTERFACE */
    PART_INTERFACE,         /**< its interface */
    PART_IMPLEMENTATION     /**< its implementation */
} Part;

/** The state of one analysis. */
typedef struct Parser {
    PasResult *res;             /**< the result */
    const char *src;            /**< the source */
    size_t length;              /**< its length */
    int is_include;             /**< an include file: a fragment, its names are seen by others */
    int pass;                   /**< 1 finds definitions, 2 makes tokens */
    int lenient;                /**< no diagnostics about the structure: \$ELSE or an include file */
    int no_nesting;             /**< comments do not nest (\$MODE TP, DELPHI) */
    int has_macros;             /**< \$MACRO: the text is not what the compiler reads */
    int has_includes;           /**< \$I: part of the structure may be elsewhere */
    size_t pos;                 /**< where the lexer stands */
    size_t line_start;          /**< offset of the current line */
    long line;                  /**< the current line */
    long symbol_cap;            /**< room in PasResult::symbols */
    long diagnostic_cap;        /**< room in PasResult::diagnostics */
    long reference_cap;         /**< room in PasResult::references */
    long token_cap;             /**< room in PasResult::tokens, in values */
    Tok *toks;                  /**< the tokens of the source */
    long tok_count;             /**< number of them */
    long tok_cap;               /**< room in toks */
    long i;                     /**< the token the passes stand on */
    long emitted;               /**< tokens up to here have been looked at for highlighting */
    int finished;               /**< the final END has been read */
    int is_unit;                /**< the source is a unit */
    Part part;                  /**< the part of a unit */
    Section section;            /**< the section of declarations */
    Block blocks[MAX_BLOCKS];   /**< the open blocks */
    long block_count;           /**< number of them */
    Routine routines[MAX_ROUTINES]; /**< the routines open */
    long routine_count;         /**< number of them */
    Local locals[MAX_LOCALS];   /**< the names of the routines open */
    long local_count;           /**< number of them */
    Def *defs;                  /**< the definitions */
    long def_count;             /**< number of them */
    long def_cap;               /**< room in defs */
    long *def_slots;            /**< hash table of defs: first index, -1 */
    long pp_depth;              /**< open \$IFs */
    PasRange pp_open[MAX_CONDITIONALS]; /**< where they are */
} Parser;

/**
 * @brief Fold a byte to lower case.
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

const char *pascal_parser_keyword_name(long index)
{
    long i;
    if (index < WORD_COUNT) return index >= 0 ? words[index].name : NULL;
    index -= WORD_COUNT;
    for (i = 0; shown_modifiers[i]; i++) {
        if (index-- == 0) return shown_modifiers[i];
    }
    for (i = 0; shown_names[i]; i++) {
        if (index-- == 0) return shown_names[i];
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
    PasResult *res = P->res;
    const char *hole = name ? strstr(format, "%s") : NULL;
    size_t flen = strlen(format);
    PasDiagnostic *d;
    char *msg;
    if (P->pass != 1 || res->diagnostic_count >= MAX_DIAGNOSTICS) return;
    if (!grow(P, (void **)&res->diagnostics, res->diagnostic_count, &P->diagnostic_cap, sizeof(PasDiagnostic))) return;
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
    PasResult *res = P->res;
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
static void tok_range(const Tok *t, PasRange *r)
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
static long add_symbol(Parser *P, const char *name, size_t len, int kind, PasScope scope, long parent,
                       char *detail, const PasRange *range, const PasRange *selection)
{
    PasResult *res = P->res;
    PasSymbol *s;
    char *copy;
    if (P->pass != 1 ||
        !grow(P, (void **)&res->symbols, res->symbol_count, &P->symbol_cap, sizeof(PasSymbol)) ||
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
static long add_tok_symbol(Parser *P, const Tok *t, int kind, PasScope scope, long parent, char *detail)
{
    PasRange r;
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
 * @brief Find a definition, in any case.
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
        if (d->length == len && equal_fold(P->src + d->start, text, len)) return d;
        i = d->next;
    }
    return NULL;
}

/**
 * @brief Remember a definition, in the first pass only.
 * @param P The parser.
 * @param t The token of its name.
 * @param kind What it is.
 */
static void add_def(Parser *P, const Tok *t, DefKind kind)
{
    unsigned long h;
    Def *d;
    if (P->pass != 1 || !grow(P, (void **)&P->defs, P->def_count, &P->def_cap, sizeof(Def))) return;
    h = hash_name(P->src + t->start, t->end - t->start) % DEF_SLOTS;
    d = &P->defs[P->def_count];
    d->start = t->start;
    d->length = t->end - t->start;
    d->kind = kind;
    d->next = P->def_slots[h];
    P->def_slots[h] = P->def_count++;
}

/**
 * @brief Find a name of the routines open, the innermost first.
 * @param P The parser.
 * @param text Its name.
 * @param len The length of the name.
 * @return It, or NULL.
 */
static const Local *find_local(const Parser *P, const char *text, size_t len)
{
    long i;
    for (i = P->local_count - 1; i >= 0; i--) {
        const Local *l = &P->locals[i];
        if (l->length == len && equal_fold(P->src + l->start, text, len)) return l;
    }
    return NULL;
}

/**
 * @brief Remember a name of the routine open.
 * @param P The parser.
 * @param t The token of its name.
 * @param token The SCOUT_TOKEN_ kind of its uses, 0 for none.
 */
static void add_local(Parser *P, const Tok *t, long token)
{
    Local *l;
    if (P->local_count >= MAX_LOCALS) return;
    l = &P->locals[P->local_count++];
    l->start = t->start;
    l->length = t->end - t->start;
    l->token = token;
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
 * @brief Tell whether a word is a certain text, in any case.
 * @param P The parser.
 * @param t The token.
 * @param text The text, in lower case.
 * @return Non-zero if it is.
 */
static int is_text(const Parser *P, const Tok *t, const char *text)
{
    size_t n = strlen(text);
    return t->type == T_WORD && t->end - t->start == n && equal_fold(P->src + t->start, text, n);
}

/**
 * @brief Tell whether a word is in a list.
 * @param P The parser.
 * @param t The token.
 * @param list The list, NULL-terminated.
 * @return Non-zero if it is.
 */
static int in_list(const Parser *P, const Tok *t, const char *const *list)
{
    long i;
    for (i = 0; list[i]; i++) {
        if (is_text(P, t, list[i])) return 1;
    }
    return 0;
}

/**
 * @brief Tell whether a word is a modifier of a routine.
 * @param P The parser.
 * @param t The token.
 * @return Non-zero if it is.
 */
static int is_modifier(const Parser *P, const Tok *t)
{
    return in_list(P, t, shown_modifiers) || in_list(P, t, other_modifiers);
}

/**
 * @brief Tell whether a token can end an expression.
 * @param t The token.
 * @return Non-zero if it can.
 */
static int is_value_end(const Tok *t)
{
    return is_name(t) || t->type == T_NUMBER || t->type == T_STRING || is_closer(t) || is_op(t, '^') ||
           is_kw(t, OP_VALUE) || is_kw(t, OP_END);
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
 * @brief Tell whether a byte is a hexadecimal digit.
 * @param c The byte.
 * @return Non-zero if it is.
 */
static int is_hex(unsigned char c)
{
    return is_digit(c) || (lower(c) >= 'a' && lower(c) <= 'f');
}

/**
 * @brief The byte at an offset of the source.
 * @param P The parser.
 * @param i The offset.
 * @return The byte, or 0 behind the end.
 */
static char at(const Parser *P, size_t i)
{
    return i < P->length ? P->src[i] : '\0';
}

/**
 * @brief Add a token.
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
    t->type = (short)type;
    t->conditional = P->pp_depth > 0;
    t->word = NULL;
    t->code = code;
    return t;
}

/**
 * @brief Step over the end of a line, or one byte.
 * @param P The parser.
 */
static void advance(Parser *P)
{
    if (P->src[P->pos++] == '\n') {
        P->line++;
        P->line_start = P->pos;
    }
}

/**
 * @brief Skip a block comment, { } or (* *). Free Pascal nests each kind.
 * @param P The parser, standing on the comment.
 * @param len The length of its delimiters.
 */
static void skip_block_comment(Parser *P, size_t len)
{
    char open = P->src[P->pos];
    long depth = 0, line = P->line, col = (long)(P->pos - P->line_start) + 1;
    while (P->pos < P->length) {
        char c = P->src[P->pos], d = at(P, P->pos + 1);
        if ((open == '{' && c == '{') || (open == '(' && c == '(' && d == '*')) {
            if (depth == 0 || !P->no_nesting) depth++;
            P->pos += len;
        } else if ((open == '{' && c == '}') || (open == '(' && c == '*' && d == ')')) {
            P->pos += len;
            if (--depth == 0) return;
        } else {
            advance(P);
        }
    }
    add_diagnostic(P, SCOUT_SEVERITY_ERROR, line, col, col + 1, "comment not closed", NULL, 0);
}

/**
 * @brief Add the file of a \$I to the references.
 * @param P The parser.
 * @param i The offset of the name.
 * @param end The offset of the end of the directive.
 */
static void add_include(Parser *P, size_t i, size_t end)
{
    PasResult *res = P->res;
    size_t name, stop;
    int dotted = 0;
    PasReference *r;
    char *copy;
    size_t k;
    while (i < end && (P->src[i] == ' ' || P->src[i] == '\t')) i++;
    while (end > i && (P->src[end - 1] == ' ' || P->src[end - 1] == '\t')) end--;
    if (i < end && P->src[i] == '\'') {
        i++;
        if (end > i && P->src[end - 1] == '\'') end--;
    }
    name = i;
    stop = end;
    /* {$I %DATE%} is a text of the compiler */
    if (stop <= name || P->src[name] == '%') return;
    for (k = name; k < stop; k++) {
        if (P->src[k] == '.') dotted = 1;
    }
    if (!grow(P, (void **)&res->references, res->reference_count, &P->reference_cap, sizeof(PasReference))) return;
    copy = (char *)malloc(stop - name + 5);
    if (!copy) {
        res->out_of_memory = 1;
        return;
    }
    memcpy(copy, P->src + name, stop - name);
    copy[stop - name] = '\0';
    if (!dotted) strcat(copy, ".inc");
    r = &res->references[res->reference_count++];
    r->target = copy;
    r->unit = 0;
    r->import = 0;
    r->range.start_line = P->line;
    r->range.start_col = (long)(name - P->line_start) + 1;
    r->range.end_line = P->line;
    r->range.end_col = (long)(stop - P->line_start) + 1;
    P->has_includes = 1;
}

/** Tell whether the name of the directive in directive() is a certain text. */
#define IS(text) (n == sizeof(text) - 1 && equal_fold(P->src + word, text, n))

/**
 * @brief Handle a compiler directive.
 * @param P The parser.
 * @param i The offset of its name, after the '$'.
 * @param end The offset of the end of its text.
 */
static void directive(Parser *P, size_t i, size_t end)
{
    size_t word = i, n;
    Tok name;
    while (i < end && is_name_part((unsigned char)P->src[i])) i++;
    n = i - word;
    memset(&name, 0, sizeof(name));
    name.line = P->line;
    name.col = (long)(word - P->line_start) + 1;
    name.start = word;
    name.end = i;

    if ((IS("i") && i < end && (P->src[i] == ' ' || P->src[i] == '\t')) || IS("include")) {
        add_include(P, i, end);
    } else if (IS("define")) {
        size_t s;
        while (i < end && (P->src[i] == ' ' || P->src[i] == '\t')) i++;
        s = i;
        while (i < end && is_name_part((unsigned char)P->src[i])) i++;
        if (i > s) {
            name.col = (long)(s - P->line_start) + 1;
            name.start = s;
            name.end = i;
            add_tok_symbol(P, &name, SCOUT_SYMBOL_MACRO, P->is_include ? PAS_SCOPE_GLOBAL : PAS_SCOPE_FILE, -1, NULL);
            add_def(P, &name, DEF_MACRO);
        }
    } else if (IS("ifdef") || IS("ifndef") || IS("ifopt") || IS("if") || IS("ifc")) {
        if (P->pp_depth < MAX_CONDITIONALS) tok_range(&name, &P->pp_open[P->pp_depth]);
        P->pp_depth++;
    } else if (IS("endif") || IS("ifend") || IS("endc")) {
        if (P->pp_depth == 0) {
            add_diagnostic(P, SCOUT_SEVERITY_ERROR, name.line, name.col, name.col + (long)n, "$ENDIF without $IF", NULL, 0);
        } else {
            P->pp_depth--;
        }
    } else if (IS("else") || IS("elseif") || IS("elifc") || IS("elsec")) {
        P->lenient = 1;
        if (P->pp_depth == 0) {
            add_diagnostic(P, SCOUT_SEVERITY_ERROR, name.line, name.col, name.col + (long)n, "$ELSE without $IF", NULL, 0);
        }
    } else if (IS("macro")) {
        P->has_macros = 1;
    } else if (IS("mode")) {
        while (i < end && (P->src[i] == ' ' || P->src[i] == '\t')) i++;
        if ((end - i >= 2 && equal_fold(P->src + i, "tp", 2)) || (end - i >= 6 && equal_fold(P->src + i, "delphi", 6))) {
            P->no_nesting = 1;
        }
    }
#undef IS
}

/**
 * @brief Pass over the assembly of an ASM block, up to its END.
 * @param P The parser, after ASM.
 */
static void skip_assembly(Parser *P)
{
    while (P->pos < P->length) {
        char c = P->src[P->pos], d = at(P, P->pos + 1);
        if (c == '{') {
            skip_block_comment(P, 1);
        } else if (c == '(' && d == '*') {
            skip_block_comment(P, 2);
        } else if (c == ';' || (c == '/' && d == '/')) {
            while (P->pos < P->length && P->src[P->pos] != '\n') P->pos++;
        } else if (c == '\'' || c == '"') {
            P->pos++;
            while (P->pos < P->length && P->src[P->pos] != '\n' && P->src[P->pos] != c) P->pos++;
            if (at(P, P->pos) == c) P->pos++;
        } else if (is_name_start((unsigned char)c)) {
            size_t s = P->pos;
            while (P->pos < P->length && is_name_part((unsigned char)P->src[P->pos])) P->pos++;
            if (P->pos - s == 3 && equal_fold(P->src + s, "end", 3)) {
                P->pos = s;
                return;
            }
        } else {
            advance(P);
        }
    }
}

/**
 * @brief Read a string; '' stands for a quote and the line ends it.
 * @param P The parser, standing on the quote.
 */
static void read_string(Parser *P)
{
    size_t start = P->pos++;
    int closed = 0;
    while (P->pos < P->length && P->src[P->pos] != '\n') {
        if (P->src[P->pos] == '\'') {
            if (at(P, P->pos + 1) != '\'') {
                P->pos++;
                closed = 1;
                break;
            }
            P->pos++;
        }
        P->pos++;
    }
    if (!push_tok(P, start, P->pos, T_STRING, 0)) return;
    if (!closed) {
        long col = (long)(start - P->line_start) + 1;
        /* in a branch the compiler may skip it does no harm */
        add_diagnostic(P, P->pp_depth > 0 ? SCOUT_SEVERITY_WARNING : SCOUT_SEVERITY_ERROR, P->line, col, col + 1,
                       "string not closed", NULL, 0);
    }
}

/**
 * @brief Read a decimal number; "1..2" is a range and no fraction.
 * @param P The parser, standing on it.
 */
static void read_number(Parser *P)
{
    size_t start = P->pos;
    while (is_digit((unsigned char)at(P, P->pos))) P->pos++;
    if (at(P, P->pos) == '.' && is_digit((unsigned char)at(P, P->pos + 1))) {
        P->pos++;
        while (is_digit((unsigned char)at(P, P->pos))) P->pos++;
    }
    if (lower((unsigned char)at(P, P->pos)) == 'e' &&
        (is_digit((unsigned char)at(P, P->pos + 1)) ||
         ((at(P, P->pos + 1) == '+' || at(P, P->pos + 1) == '-') && is_digit((unsigned char)at(P, P->pos + 2))))) {
        P->pos += 2;
        while (is_digit((unsigned char)at(P, P->pos))) P->pos++;
    }
    push_tok(P, start, P->pos, T_NUMBER, 0);
}

/**
 * @brief Read an operator of one or two characters.
 * @param P The parser, standing on it.
 */
static void read_operator(Parser *P)
{
    static const char twos[][3] = { ":=", "..", "<=", ">=", "<>", "><", "**", "<<", ">>", "+=", "-=", "*=", "/=" };
    size_t start = P->pos, i;
    char c = P->src[P->pos], d = at(P, P->pos + 1);
    int code = (unsigned char)c;
    for (i = 0; i < sizeof(twos) / sizeof(twos[0]); i++) {
        if (twos[i][0] == c && twos[i][1] == d) {
            code = OP2(c, d);
            break;
        }
    }
    P->pos += code > 255 ? 2 : 1;
    push_tok(P, start, P->pos, T_OP, code);
}

/**
 * @brief Read a name or reserved word, and what stands in an ASM block.
 * @param P The parser, standing on it.
 * @return Non-zero if the final END has been read.
 */
static int read_word(Parser *P)
{
    size_t start = P->pos;
    Tok *t;
    while (is_name_part((unsigned char)at(P, P->pos))) P->pos++;
    t = push_tok(P, start, P->pos, T_WORD, 0);
    if (!t) return 0;
    t->word = find_word(P->src + start, P->pos - start);
    if (is_kw(t, OP_ASM)) {
        skip_assembly(P);
    } else if (is_kw(t, OP_END) && P->pp_depth == 0) {
        size_t i = P->pos;
        while (i < P->length && (P->src[i] == ' ' || P->src[i] == '\t' || P->src[i] == '\r' || P->src[i] == '\n')) i++;
        if (at(P, i) == '.' && at(P, i + 1) != '.' && !is_name_part((unsigned char)at(P, i + 1))) return 1;
    }
    return 0;
}

/**
 * @brief Read the source into tokens.
 * @param P The parser.
 */
static void tokenize(Parser *P)
{
    long count = 0;
    int last = 0;
    long k;
    P->pos = 0;
    P->line = 1;
    P->line_start = 0;
    P->pass = 1;
    while (P->pos < P->length && !P->res->out_of_memory) {
        char c = P->src[P->pos], d = at(P, P->pos + 1);
        if (++count % STOP_INTERVAL == 0 && pascal_parser_should_stop && pascal_parser_should_stop()) {
            P->res->stopped = 1;
            return;
        }
        if (last) {
            /* the '.' of the final END, and nothing after it */
            if (c == '.') {
                read_operator(P);
                break;
            }
            advance(P);
            continue;
        }
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f') {
            advance(P);
        } else if ((c == '{' && d == '$') || (c == '(' && d == '*' && at(P, P->pos + 2) == '$')) {
            /* a directive ends at the first close, whatever it contains */
            size_t len = c == '{' ? 1 : 2, body = P->pos + len + 1, e = body;
            long line = P->line, col = (long)(P->pos - P->line_start) + 1;
            while (e < P->length && P->src[e] != '\n' && P->src[e] != (c == '{' ? '}' : '*')) e++;
            directive(P, body, e);
            P->pos = body;
            while (P->pos < P->length && !(c == '{' ? P->src[P->pos] == '}' : P->src[P->pos] == '*' && at(P, P->pos + 1) == ')')) {
                advance(P);
            }
            if (P->pos < P->length) P->pos += len;
            else add_diagnostic(P, SCOUT_SEVERITY_ERROR, line, col, col + 1, "comment not closed", NULL, 0);
        } else if (c == '{') {
            skip_block_comment(P, 1);
        } else if (c == '(' && d == '*') {
            skip_block_comment(P, 2);
        } else if (c == '/' && d == '/') {
            while (P->pos < P->length && P->src[P->pos] != '\n') P->pos++;
        } else if (c == '\'') {
            read_string(P);
        } else if (c == '#' && (is_digit((unsigned char)d) || d == '$')) {
            size_t s = P->pos++;
            if (d == '$') P->pos++;
            while (is_hex((unsigned char)at(P, P->pos))) P->pos++;
            push_tok(P, s, P->pos, T_STRING, 0);
        } else if (is_digit((unsigned char)c)) {
            read_number(P);
        } else if ((c == '$' && is_hex((unsigned char)d)) || (c == '%' && (d == '0' || d == '1')) ||
                   (c == '&' && is_digit((unsigned char)d))) {
            size_t s = P->pos++;
            while (is_hex((unsigned char)at(P, P->pos))) P->pos++;
            push_tok(P, s, P->pos, T_NUMBER, 0);
        } else if (c == '&' && is_name_start((unsigned char)d)) {
            /* a reserved word used as a name */
            size_t s = P->pos++;
            while (is_name_part((unsigned char)at(P, P->pos))) P->pos++;
            push_tok(P, s, P->pos, T_WORD, 0);
        } else if (is_name_start((unsigned char)c)) {
            last = read_word(P);
        } else {
            read_operator(P);
        }
    }
    /* words reserved only in some modes are names where only a name fits */
    for (k = 0; k + 1 < P->tok_count; k++) {
        Tok *t = &P->toks[k];
        const Tok *n = &P->toks[k + 1];
        if (t->word && t->word->soft &&
            (is_op(n, OP2(':', '=')) || is_op(n, ':') || is_op(n, ',') || is_op(n, '^') || is_op(n, ')'))) {
            t->word = NULL;
        }
    }
}

/**
 * @brief Report brackets that do not fit together.
 * @param P The parser.
 */
static void check_brackets(Parser *P)
{
    long stack[MAX_BRACKETS], depth = 0, i;
    for (i = 0; i < P->tok_count; i++) {
        const Tok *t = &P->toks[i];
        if (is_opener(t)) {
            if (depth < MAX_BRACKETS) stack[depth] = i;
            depth++;
        } else if (is_closer(t)) {
            char text[2];
            text[0] = P->src[t->start];
            text[1] = '\0';
            if (depth == 0) {
                structure_error(P, t, "'%s' without an opening bracket", text);
            } else {
                depth--;
                if (depth < MAX_BRACKETS && P->src[P->toks[stack[depth]].start] != (text[0] == ')' ? '(' : '[')) {
                    structure_error(P, t, "'%s' does not match its bracket", text);
                }
            }
        } else if (depth > 0 && (is_kw(t, OP_BEGIN) || is_kw(t, OP_END) || is_kw(t, OP_THEN) || is_kw(t, OP_DO) ||
                                 is_kw(t, OP_UNTIL) || is_kw(t, OP_REPEAT))) {
            if (depth <= MAX_BRACKETS) {
                char text[2];
                text[0] = P->src[P->toks[stack[depth - 1]].start];
                text[1] = '\0';
                structure_error(P, &P->toks[stack[depth - 1]], "'%s' not closed", text);
            }
            depth = 0;
        }
    }
    if (depth > 0 && depth <= MAX_BRACKETS) {
        char text[2];
        text[0] = P->src[P->toks[stack[depth - 1]].start];
        text[1] = '\0';
        structure_error(P, &P->toks[stack[depth - 1]], "'%s' not closed", text);
    }
}

/**
 * @brief The highlighting token of a word.
 * @param P The parser.
 * @param i Its index.
 */
static void classify(Parser *P, long i)
{
    const Tok *t = &P->toks[i];
    const char *text = P->src + t->start;
    size_t len = t->end - t->start;
    const Local *l;
    const Def *d;
    if (!is_name(t) || (i > 0 && is_op(&P->toks[i - 1], '.'))) return;
    l = find_local(P, text, len);
    if (l) {
        if (l->token) add_token(P, t, l->token);
        return;
    }
    d = find_def(P, text, len);
    if (!d) return;
    switch (d->kind) {
    case DEF_FUNCTION: add_token(P, t, SCOUT_TOKEN_FUNCTION); break;
    case DEF_TYPE: add_token(P, t, SCOUT_TOKEN_TYPE); break;
    case DEF_CONSTANT: add_token(P, t, SCOUT_TOKEN_CONSTANT); break;
    case DEF_MACRO: add_token(P, t, SCOUT_TOKEN_MACRO); break;
    default: break;
    }
}

/**
 * @brief Make the highlighting tokens up to a token.
 * @param P The parser.
 * @param to The token to stop at.
 */
static void emit_to(Parser *P, long to)
{
    if (P->pass != 2) return;
    if (to > P->tok_count) to = P->tok_count;
    for (; P->emitted < to; P->emitted++) classify(P, P->emitted);
}

/**
 * @brief The scope of a declaration outside of records and routines.
 * @param P The parser.
 * @return Its scope.
 */
static PasScope outer_scope(const Parser *P)
{
    if (P->routine_count > 0) return PAS_SCOPE_LOCAL;
    return P->is_include || P->part == PART_INTERFACE ? PAS_SCOPE_GLOBAL : PAS_SCOPE_FILE;
}

/**
 * @brief The symbol that new local names belong to.
 * @param P The parser.
 * @return The innermost routine's symbol, or -1.
 */
static long outer_parent(const Parser *P)
{
    return P->routine_count > 0 ? P->routines[P->routine_count - 1].symbol : -1;
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
 * @brief Step over a type or an initial value.
 *
 * Stops at the ';' that ends the declaration, at a ')' or END that closes
 * what the declaration is in, or where a new declaration seems to start
 * because the ';' is missing.
 *
 * @param P The parser.
 * @param i The first token.
 * @return The token it stopped at.
 */
static long skip_type(const Parser *P, long i)
{
    long start = i, depth = 0, nest = 0;
    for (; i < P->tok_count; i++) {
        const Tok *t = &P->toks[i];
        const Tok *n = i + 1 < P->tok_count ? &P->toks[i + 1] : NULL;
        if (is_opener(t)) {
            depth++;
        } else if (is_closer(t)) {
            if (depth == 0) return i;
            depth--;
        } else if (depth > 0) {
            continue;
        } else if (is_kw(t, OP_RECORD) || (is_kw(t, OP_OBJECT) && !(i > 0 && is_kw(&P->toks[i - 1], OP_OF)))) {
            nest++;
        } else if ((is_kw(t, OP_CLASS) || is_kw(t, OP_INTERFACE)) && n && !is_kw(n, OP_OF) && !is_op(n, ';')) {
            nest++;
        } else if (is_kw(t, OP_END)) {
            if (nest == 0) return i;
            nest--;
        } else if (nest == 0) {
            if (is_op(t, ';')) return i;
            if (i > start && is_value_end(&P->toks[i - 1])) {
                /* a new line that looks like "name: ...": the ';' is missing */
                if (is_op(t, ':') && i - 1 > start && is_name(&P->toks[i - 1]) &&
                    P->toks[i - 1].line > P->toks[i - 2].line) return i - 1;
                if (is_kw(t, OP_BEGIN) || is_kw(t, OP_VAR) || is_kw(t, OP_CONST) || is_kw(t, OP_TYPE) ||
                    is_kw(t, OP_IMPLEMENTATION) || is_kw(t, OP_PROCEDURE) || is_kw(t, OP_FUNCTION)) return i;
            }
        }
    }
    return i;
}

/**
 * @brief Step over a ';', or report that it is missing.
 * @param P The parser.
 * @param i Where it should be.
 * @return The token after it.
 */
static long expect_semicolon(Parser *P, long i)
{
    if (i < P->tok_count && is_op(&P->toks[i], ';')) return i + 1;
    if (i > 0 && i < P->tok_count && !P->toks[i - 1].conditional) structure_error(P, &P->toks[i - 1], "';' expected", NULL);
    return i;
}

/**
 * @brief Step over everything up to and including the next ';'.
 * @param P The parser.
 * @param i The first token.
 * @return The token after the ';', and at least one token further than i.
 */
static long skip_declaration(const Parser *P, long i)
{
    long j = skip_type(P, i);
    if (j < P->tok_count && (is_op(&P->toks[j], ';') || is_closer(&P->toks[j]) || j == i)) j++;
    return j;
}

/**
 * @brief Report a name defined twice at the outer level.
 * @param P The parser.
 * @param t The token of the name.
 */
static void check_twice(Parser *P, const Tok *t)
{
    const Def *d;
    if (P->pass != 1 || P->routine_count > 0 || t->conditional) return;
    d = find_def(P, P->src + t->start, t->end - t->start);
    if (d && (d->kind == DEF_CONSTANT || d->kind == DEF_TYPE || d->kind == DEF_VARIABLE)) defined_twice(P, t);
}

/**
 * @brief Add a name of a const, type or var section, or an enumeration member.
 * @param P The parser.
 * @param t The token of the name.
 * @param kind Its SCOUT_SYMBOL_ kind.
 * @param def_kind Its DefKind.
 * @param detail Its detail (may be NULL).
 * @return Its symbol, or -1.
 */
static long add_declared(Parser *P, const Tok *t, int kind, DefKind def_kind, char *detail)
{
    long sym;
    check_twice(P, t);
    sym = add_tok_symbol(P, t, kind, outer_scope(P), outer_parent(P), detail);
    if (P->routine_count > 0) {
        add_local(P, t, def_kind == DEF_CONSTANT ? SCOUT_TOKEN_CONSTANT : def_kind == DEF_TYPE ? SCOUT_TOKEN_TYPE :
                        def_kind == DEF_FUNCTION ? SCOUT_TOKEN_FUNCTION : 0);
    } else {
        add_def(P, t, def_kind);
    }
    return sym;
}

/**
 * @brief Make a symbol's range reach to the end of a token.
 * @param P The parser.
 * @param sym The symbol, or -1.
 * @param t The token.
 */
static void extend_symbol(Parser *P, long sym, const Tok *t)
{
    if (P->pass != 1 || sym < 0) return;
    P->res->symbols[sym].range.end_line = t->line;
    P->res->symbols[sym].range.end_col = t->col + (long)(t->end - t->start);
}

/**
 * @brief Read one constant, or a modifier after one.
 * @param P The parser, standing on its name.
 */
static void read_const(Parser *P)
{
    const Tok *t = &P->toks[P->i];
    const Tok *n = P->i + 1 < P->tok_count ? &P->toks[P->i + 1] : NULL;
    if (n && (is_op(n, '=') || is_op(n, ':'))) {
        add_declared(P, t, SCOUT_SYMBOL_CONSTANT, DEF_CONSTANT, NULL);
        P->i = expect_semicolon(P, skip_type(P, P->i + 2));
        return;
    }
    if (!is_modifier(P, t) && !t->conditional) structure_error(P, n ? n : t, "'=' expected", NULL);
    P->i = skip_declaration(P, P->i);
}

/**
 * @brief Read the names of a list, up to what follows them.
 * @param P The parser.
 * @param i The first name.
 * @return The token after the last name.
 */
static long skip_names(const Parser *P, long i)
{
    long n = P->tok_count;
    while (i + 2 < n && is_name(&P->toks[i]) && is_op(&P->toks[i + 1], ',') && is_name(&P->toks[i + 2])) i += 2;
    return i + 1;
}

/**
 * @brief Read one line of variables, or a modifier after one.
 * @param P The parser, standing on the first name.
 */
static void read_var(Parser *P)
{
    long first = P->i, after = skip_names(P, first), k, end;
    const Tok *t = &P->toks[first];
    if (after < P->tok_count && is_op(&P->toks[after], ':')) {
        end = skip_type(P, after + 1);
        for (k = first; k < after; k += 2) {
            add_declared(P, &P->toks[k], SCOUT_SYMBOL_VARIABLE, DEF_VARIABLE,
                         end > after + 1 ? slice_detail(P, &P->toks[after + 1], &P->toks[end - 1]) : NULL);
        }
        P->i = expect_semicolon(P, end);
        return;
    }
    if (!is_modifier(P, t) && !t->conditional) {
        structure_error(P, after < P->tok_count ? &P->toks[after] : t, "':' expected", NULL);
    }
    P->i = skip_declaration(P, P->i);
}

/**
 * @brief Read the labels of a label section.
 * @param P The parser, after the keyword.
 */
static void read_labels(Parser *P)
{
    while (P->i < P->tok_count && !is_op(&P->toks[P->i], ';')) {
        const Tok *t = &P->toks[P->i];
        if (t->type == T_WORD && t->word) return;
        if (is_name(t) || t->type == T_NUMBER) add_declared(P, t, SCOUT_SYMBOL_LABEL, DEF_LABEL, NULL);
        P->i++;
    }
    if (P->i < P->tok_count) P->i++;
    P->section = S_NONE;
}

static long read_routine(Parser *P, long i, long parent);

/**
 * @brief Read the members of a record, object, class or interface.
 * @param P The parser.
 * @param i The token after the head.
 * @param sym Its symbol.
 * @param kw The word that opened it.
 * @return The token after its END.
 */
static long read_members(Parser *P, long i, long sym, const Tok *kw)
{
    long n = P->tok_count;
    while (i < n) {
        const Tok *t = &P->toks[i];
        const Tok *next = i + 1 < n ? &P->toks[i + 1] : NULL;
        if (is_kw(t, OP_END)) {
            extend_symbol(P, sym, t);
            return i + 1;
        }
        if (is_kw(t, OP_PROCEDURE) || is_kw(t, OP_FUNCTION) || is_kw(t, OP_CONSTRUCTOR) || is_kw(t, OP_OPERATOR) ||
            (is_kw(t, OP_CLASS) && next && (is_kw(next, OP_PROCEDURE) || is_kw(next, OP_FUNCTION) ||
                                            is_kw(next, OP_CONSTRUCTOR) || is_kw(next, OP_OPERATOR)))) {
            i = read_routine(P, i, sym);
        } else if (is_kw(t, OP_PROPERTY)) {
            if (next && is_name(next)) {
                add_tok_symbol(P, next, SCOUT_SYMBOL_PROPERTY, PAS_SCOPE_LOCAL, sym, NULL);
            }
            i = skip_declaration(P, i + 1);
        } else if (is_kw(t, OP_CASE)) {
            /* the variants of a record: their fields come in brackets */
            while (i < n && !is_kw(&P->toks[i], OP_OF) && !is_kw(&P->toks[i], OP_END)) i++;
            if (i < n && is_kw(&P->toks[i], OP_OF)) i++;
        } else if (is_name(t)) {
            long after = skip_names(P, i), k;
            if (after < n && is_op(&P->toks[after], ':') && after + 1 < n && !is_op(&P->toks[after + 1], '(')) {
                long end = skip_type(P, after + 1);
                for (k = i; k < after; k += 2) {
                    add_tok_symbol(P, &P->toks[k], SCOUT_SYMBOL_FIELD, PAS_SCOPE_LOCAL, sym,
                                   end > after + 1 ? slice_detail(P, &P->toks[after + 1], &P->toks[end - 1]) : NULL);
                }
                i = end;
                if (i < n && is_op(&P->toks[i], ';')) i++;
            } else if (after < n && is_op(&P->toks[after], ':')) {
                i = after + 1;      /* a label of a variant */
            } else {
                i++;                /* public, strict and so on */
            }
        } else {
            i++;
        }
    }
    {
        char text[20];
        size_t len = kw->end - kw->start < sizeof(text) ? kw->end - kw->start : sizeof(text) - 1;
        memcpy(text, P->src + kw->start, len);
        text[len] = '\0';
        structure_error(P, kw, "'%s' without END", text);
    }
    return i;
}

/**
 * @brief Read one type.
 * @param P The parser, standing on the name (or GENERIC).
 */
static void read_type(Parser *P)
{
    long n = P->tok_count, j = P->i;
    const Tok *name, *t;
    long sym;
    if (is_text(P, &P->toks[j], "generic") && j + 1 < n && is_name(&P->toks[j + 1])) j++;
    name = &P->toks[j++];
    if (j < n && is_op(&P->toks[j], '<')) {
        while (j < n && !is_op(&P->toks[j], '>') && !is_op(&P->toks[j], ';')) j++;
        if (j < n && is_op(&P->toks[j], '>')) j++;
    }
    if (j >= n || !is_op(&P->toks[j], '=')) {
        if (!is_modifier(P, name) && !name->conditional) structure_error(P, j < n ? &P->toks[j] : name, "'=' expected", NULL);
        P->i = skip_declaration(P, P->i);
        return;
    }
    j++;
    while (j < n && (is_kw(&P->toks[j], OP_PACKED) || is_kw(&P->toks[j], OP_TYPE))) j++;
    if (j >= n) {
        P->i = j;
        return;
    }
    t = &P->toks[j];
    if (is_kw(t, OP_RECORD) && !(j + 1 < n && is_text(P, &P->toks[j + 1], "helper"))) {
        sym = add_declared(P, name, SCOUT_SYMBOL_STRUCT, DEF_TYPE, NULL);
        j = read_members(P, j + 1, sym, t);
        P->i = expect_semicolon(P, skip_type(P, j));
        return;
    }
    if (is_kw(t, OP_RECORD) || is_kw(t, OP_OBJECT) || is_kw(t, OP_CLASS) || is_kw(t, OP_INTERFACE)) {
        const Tok *after = j + 1 < n ? &P->toks[j + 1] : NULL;
        long k = j + 1;
        char *detail = NULL;
        int kind = is_kw(t, OP_INTERFACE) ? SCOUT_SYMBOL_INTERFACE : SCOUT_SYMBOL_CLASS;
        if (after && is_kw(after, OP_OF)) {
            add_declared(P, name, SCOUT_SYMBOL_TYPE_ALIAS, DEF_TYPE, NULL);
            P->i = expect_semicolon(P, skip_type(P, k));
            return;
        }
        if (after && is_op(after, ';')) {   /* declared in advance */
            P->i = k + 1;
            return;
        }
        if (after && is_text(P, after, "helper")) {
            while (k < n && !is_text(P, &P->toks[k], "for")) k++;
            k += 2;
        }
        if (k < n && is_op(&P->toks[k], '(')) {
            long close = matching_bracket(P, k);
            if (close >= 0) {
                detail = slice_detail(P, &P->toks[k], &P->toks[close]);
                k = close + 1;
            }
        }
        if (k < n && is_op(&P->toks[k], '[')) {     /* the GUID of an interface */
            long close = matching_bracket(P, k);
            if (close >= 0) k = close + 1;
        }
        sym = add_declared(P, name, kind, DEF_TYPE, detail);
        if (k < n && is_op(&P->toks[k], ';')) {     /* no members */
            P->i = k + 1;
            return;
        }
        j = read_members(P, k, sym, t);
        P->i = expect_semicolon(P, skip_type(P, j));
        return;
    }
    if (is_op(t, '(')) {
        long close = matching_bracket(P, j);
        if (close >= 0 && close + 1 < n && is_op(&P->toks[close + 1], ';')) {
            long k = j + 1;
            int expect = 1;
            sym = add_declared(P, name, SCOUT_SYMBOL_ENUM, DEF_TYPE, NULL);
            for (; k < close; k++) {
                const Tok *m = &P->toks[k];
                if (is_op(m, ',')) {
                    expect = 1;
                } else if (expect && is_name(m)) {
                    add_declared(P, m, SCOUT_SYMBOL_ENUM_MEMBER, DEF_CONSTANT, NULL);
                    expect = 0;
                }
            }
            extend_symbol(P, sym, &P->toks[close]);
            P->i = close + 2;
            return;
        }
    }
    {
        long end = skip_type(P, j);
        add_declared(P, name, SCOUT_SYMBOL_TYPE_ALIAS, DEF_TYPE,
                     end > j ? slice_detail(P, t, &P->toks[end - 1]) : NULL);
        P->i = expect_semicolon(P, end);
    }
}

/**
 * @brief Read the parameters of a routine with a body.
 * @param P The parser.
 * @param open The index of the '('.
 * @param close The index of the ')'.
 * @param symbol The routine's symbol.
 */
static void read_parameters(Parser *P, long open, long close, long symbol)
{
    long k = open + 1;
    while (k < close) {
        long first, last, depth = 0, colon = -1, m;
        if (is_kw(&P->toks[k], OP_CONST) || is_kw(&P->toks[k], OP_VAR)) k++;
        else if (k + 1 < close && is_name(&P->toks[k]) && is_name(&P->toks[k + 1])) k++;    /* out, constref */
        first = k;
        for (; k < close; k++) {
            const Tok *t = &P->toks[k];
            if (is_opener(t)) depth++;
            else if (is_closer(t)) depth--;
            else if (depth == 0 && is_op(t, ';')) break;
            else if (depth == 0 && colon < 0 && is_op(t, ':')) colon = k;
        }
        last = colon >= 0 ? colon : k;
        for (m = first; m < last; m++) {
            const Tok *t = &P->toks[m];
            if (!is_name(t)) continue;
            add_local(P, t, SCOUT_TOKEN_PARAMETER);
            add_tok_symbol(P, t, SCOUT_SYMBOL_VARIABLE, PAS_SCOPE_LOCAL, symbol,
                           colon >= 0 && k > colon + 1 ? slice_detail(P, &P->toks[colon + 1], &P->toks[k - 1]) : NULL);
        }
        k++;
    }
}

/**
 * @brief Read the head of a routine.
 *
 * In a class, a unit's interface, or marked forward, external or syscall it
 * is a declaration; otherwise the routine is opened and its body will come.
 *
 * @param P The parser.
 * @param i The first token (CLASS or the keyword).
 * @param parent The class it stands in, or -1.
 * @return The token after the head and its modifiers.
 */
static long read_routine(Parser *P, long i, long parent)
{
    long n = P->tok_count, j = i, open = -1, close = -1, last, sym = -1;
    const Tok *kw, *name;
    int in_class = parent >= 0, method, declaration = 0, kind;
    PasScope scope;
    char *detail = NULL;

    if (is_kw(&P->toks[j], OP_CLASS)) j++;
    kw = &P->toks[j++];
    if (j >= n) return j;
    name = &P->toks[j++];
    if (name->type != T_WORD && !is_kw(kw, OP_OPERATOR) && name->type != T_OP) {
        return skip_declaration(P, j);
    }
    method = in_class || is_kw(kw, OP_CONSTRUCTOR);
    while (j + 1 < n) {
        if (is_op(&P->toks[j], '<') && !is_kw(kw, OP_OPERATOR)) {    /* generics */
            while (j < n && !is_op(&P->toks[j], '>') && !is_op(&P->toks[j], ';')) j++;
            if (j < n && is_op(&P->toks[j], '>')) j++;
            continue;
        }
        if (!is_op(&P->toks[j], '.')) break;
        name = &P->toks[j + 1];
        j += 2;
        method = 1;
    }
    if (j < n && is_op(&P->toks[j], '(')) {
        open = j;
        close = matching_bracket(P, j);
        if (close >= 0) j = close + 1;
    }
    last = j - 1;
    /* the return type: up to the ';' or what ends the head */
    if (j < n && (is_op(&P->toks[j], ':') || (is_kw(kw, OP_OPERATOR) && is_name(&P->toks[j])))) {
        while (j < n && !is_op(&P->toks[j], ';') && !is_op(&P->toks[j], '[') &&
               !(P->toks[j].type == T_WORD && P->toks[j].word && !is_kw(&P->toks[j], OP_VALUE))) {
            last = j++;
        }
    }
    if (open >= 0 && close >= 0) detail = slice_detail(P, &P->toks[open], &P->toks[last]);
    else if (last >= i && is_op(&P->toks[j > 0 ? j - 1 : 0], ';') == 0 && last > 0 && !is_kw(&P->toks[last], OP_PROCEDURE) &&
             &P->toks[last] != name && open < 0) {
        /* no parameters: ": type" alone */
        long c = last;
        while (c > i && !is_op(&P->toks[c], ':')) c--;
        if (is_op(&P->toks[c], ':')) detail = slice_detail(P, &P->toks[c], &P->toks[last]);
    }

    /* modifiers, each with its own ';' */
    if (j < n && !is_op(&P->toks[j], ';') && !is_op(&P->toks[j], '[') && !is_modifier(P, &P->toks[j])) {
        if (!P->toks[j - 1].conditional) structure_error(P, &P->toks[j - 1], "';' expected", NULL);
    }
    for (;;) {
        const Tok *t;
        if (j < n && is_op(&P->toks[j], ';')) j++;
        if (j >= n) break;
        t = &P->toks[j];
        if (is_op(t, '[')) {
            long c = matching_bracket(P, j);
            if (c < 0) break;
            j = c + 1;
            continue;
        }
        if (t->type != T_WORD || !is_modifier(P, t)) break;
        /* in a class these can be sections or fields */
        if (in_class && is_text(P, t, "public")) break;
        if (j + 1 < n && (is_op(&P->toks[j + 1], ':') || is_op(&P->toks[j + 1], ','))) break;
        if (is_text(P, t, "forward") || is_text(P, t, "external") || is_text(P, t, "syscall") ||
            is_text(P, t, "weakexternal")) declaration = 1;
        /* Turbo Pascal's machine code: inline($B4/$2C) is the whole body */
        if (is_text(P, t, "inline") && j + 1 < n && is_op(&P->toks[j + 1], '(')) declaration = 1;
        while (j < n && !is_op(&P->toks[j], ';')) {
            if (is_kw(&P->toks[j], OP_BEGIN) || is_kw(&P->toks[j], OP_VAR)) break;
            j++;
        }
        if (j >= n || !is_op(&P->toks[j], ';')) break;
    }

    if (in_class || (P->part == PART_INTERFACE && P->routine_count == 0)) declaration = 1;
    kind = method ? (is_kw(kw, OP_CONSTRUCTOR) ? SCOUT_SYMBOL_CONSTRUCTOR : SCOUT_SYMBOL_METHOD) :
           is_kw(kw, OP_PROCEDURE) ? SCOUT_SYMBOL_PROCEDURE : SCOUT_SYMBOL_FUNCTION;
    if (in_class) scope = PAS_SCOPE_LOCAL;
    else if (P->routine_count > 0) scope = PAS_SCOPE_LOCAL;
    else if (P->is_include || P->is_unit) {
        /* what the interface declares is defined in the implementation */
        const Def *d = find_def(P, P->src + name->start, name->end - name->start);
        scope = P->is_include || P->part == PART_INTERFACE || method || (d && d->kind == DEF_FUNCTION) ?
                PAS_SCOPE_GLOBAL : PAS_SCOPE_FILE;
    } else {
        scope = PAS_SCOPE_FILE;
    }
    if (P->pass == 1) {
        PasRange r, sel;
        tok_range(kw, &r);
        r.end_line = P->toks[last].line;
        r.end_col = P->toks[last].col + (long)(P->toks[last].end - P->toks[last].start);
        tok_range(name, &sel);
        sym = add_symbol(P, P->src + name->start, name->end - name->start, kind, scope,
                         in_class ? parent : outer_parent(P), detail, &r, &sel);
        if (sym >= 0) P->res->symbols[sym].declaration = declaration;
    }
    if (!in_class && name->type == T_WORD) {
        if (P->routine_count > 0) add_local(P, name, SCOUT_TOKEN_FUNCTION);
        else if (!method) add_def(P, name, DEF_FUNCTION);
    }
    if (!declaration) {
        if (P->routine_count < MAX_ROUTINES) {
            Routine *rt = &P->routines[P->routine_count++];
            rt->symbol = sym;
            rt->mark = P->local_count;
            rt->name = name;
        } else {
            P->lenient = 1;
        }
        if (open >= 0 && close >= 0) read_parameters(P, open, close, sym);
        P->section = S_NONE;
    }
    return j;
}

/**
 * @brief Report an open block as not closed.
 * @param P The parser.
 * @param b The block.
 */
static void report_block(Parser *P, const Block *b)
{
    static const char *const texts[] = {
        "BEGIN without END", "ASM without END", "CASE without END", "TRY without END",
        "REPEAT without UNTIL", "INITIALIZATION without END"
    };
    structure_error(P, b->tok, texts[b->type], NULL);
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
    b->tok = t;
    b->in_except = 0;
}

/**
 * @brief Close the routine whose body has ended.
 * @param P The parser.
 * @param t The last token of its body.
 */
static void close_routine(Parser *P, const Tok *t)
{
    Routine *r = &P->routines[--P->routine_count];
    extend_symbol(P, r->symbol, t);
    P->local_count = r->mark;
    P->section = S_NONE;
}

/**
 * @brief What follows the END of a body, or of a unit without one.
 * @param P The parser.
 * @param j The index of the END.
 */
static void finish_body(Parser *P, long j)
{
    const Tok *t = &P->toks[j];
    const Tok *n = j + 1 < P->tok_count ? &P->toks[j + 1] : NULL;
    emit_to(P, j + 1);
    if (P->routine_count > 0) {
        P->i = j + 1;
        if (n && is_op(n, ';')) P->i++;
        else if (n && !t->conditional) structure_error(P, t, "';' expected", NULL);
        close_routine(P, t);
        return;
    }
    P->i = j + 1;
    if (n && is_op(n, '.')) {
        P->i++;
        P->finished = 1;
    } else if (!P->is_include) {
        structure_error(P, t, "'.' expected after the final END", NULL);
        P->finished = 1;
    }
}

/**
 * @brief Tell whether a word is on the list of those a clause may stop at.
 * @param t The token.
 * @return Non-zero if a clause ends there.
 */
static int ends_clause(const Tok *t)
{
    return is_op(t, ';') || is_kw(t, OP_END) || is_kw(t, OP_UNTIL) || is_kw(t, OP_BEGIN) || is_kw(t, OP_ELSE) ||
           is_kw(t, OP_THEN) || is_kw(t, OP_DO) || is_kw(t, OP_OF);
}

/**
 * @brief Check that the word a statement needs follows its head.
 * @param P The parser.
 * @param j The index of the statement's word.
 * @param op The word it needs.
 * @param message The message if it is missing.
 */
static void need_word(Parser *P, long j, Op op, const char *message)
{
    long k, depth = 0;
    for (k = j + 1; k < P->tok_count; k++) {
        const Tok *t = &P->toks[k];
        if (is_opener(t)) depth++;
        else if (is_closer(t)) depth--;
        else if (depth <= 0 && is_kw(t, op)) return;
        else if (depth <= 0 && ends_clause(t)) break;
    }
    if (k < P->tok_count && P->toks[k].conditional) return;
    if (!P->toks[j].conditional) structure_error(P, &P->toks[j], message, NULL);
}

/**
 * @brief Read a body or the initialization of a unit, up to its END.
 * @param P The parser, standing on the word that opens it.
 */
static void read_body(Parser *P)
{
    long n = P->tok_count, j, prev = -1, depth = 0;
    int start = 1;
    P->block_count = 0;
    for (j = P->i; j < n; j++) {
        const Tok *t = &P->toks[j];
        int was_start = start;
        start = 0;
        if (j % STOP_INTERVAL == 0 && pascal_parser_should_stop && pascal_parser_should_stop()) {
            P->res->stopped = 1;
            P->i = n;
            return;
        }
        if (is_opener(t)) depth++;
        else if (is_closer(t) && depth > 0) depth--;
        else if (is_kw(t, OP_BEGIN) || is_kw(t, OP_END) || is_kw(t, OP_UNTIL) || is_kw(t, OP_REPEAT)) depth = 0;
        if (prev >= 0 && depth == 0 && !P->has_macros && t->line > P->toks[prev].line &&
            is_value_end(&P->toks[prev]) && !t->conditional && !P->toks[prev].conditional &&
            (is_name(t) || is_kw(t, OP_IF) || is_kw(t, OP_WHILE) || is_kw(t, OP_FOR) || is_kw(t, OP_REPEAT) ||
             is_kw(t, OP_BEGIN) || is_kw(t, OP_WITH) || is_kw(t, OP_TRY) || is_kw(t, OP_STATEMENT) || is_kw(t, OP_CASE))) {
            structure_error(P, &P->toks[prev], "';' expected", NULL);
        }
        if (was_start && is_name(t) && j + 1 < n && is_op(&P->toks[j + 1], '=') && !t->conditional) {
            structure_error(P, &P->toks[j + 1], "'=' where ':=' is meant", NULL);
        }
        if (t->type == T_WORD && t->word && depth == 0) {
            Block *top = P->block_count > 0 ? &P->blocks[P->block_count - 1] : NULL;
            switch (t->word->op) {
            case OP_BEGIN: open_block(P, B_BEGIN, t); start = 1; break;
            case OP_ASM: open_block(P, B_ASM, t); break;
            case OP_REPEAT: open_block(P, B_REPEAT, t); start = 1; break;
            case OP_TRY: open_block(P, B_TRY, t); start = 1; break;
            case OP_INITIALIZATION: open_block(P, B_INIT, t); start = 1; break;
            case OP_FINALIZATION:
                if (!top || top->type != B_INIT) structure_error(P, t, "FINALIZATION without INITIALIZATION", NULL);
                start = 1;
                break;
            case OP_CASE:
                need_word(P, j, OP_OF, "CASE without OF");
                open_block(P, B_CASE, t);
                break;
            case OP_IF: need_word(P, j, OP_THEN, "IF without THEN"); break;
            case OP_WHILE: need_word(P, j, OP_DO, "WHILE without DO"); break;
            case OP_FOR: need_word(P, j, OP_DO, "FOR without DO"); break;
            case OP_WITH: need_word(P, j, OP_DO, "WITH without DO"); break;
            case OP_THEN: case OP_DO: start = 1; break;
            case OP_ELSE:
                start = 1;
                if (prev >= 0 && is_op(&P->toks[prev], ';') && top && !t->conditional && !P->toks[prev].conditional &&
                    (top->type == B_BEGIN || top->type == B_REPEAT || top->type == B_INIT ||
                     (top->type == B_TRY && !top->in_except))) {
                    structure_error(P, &P->toks[prev], "';' before ELSE", NULL);
                }
                break;
            case OP_EXCEPT: case OP_FINALLY:
                start = 1;
                if (!top || top->type != B_TRY) {
                    structure_error(P, t, t->word->op == OP_EXCEPT ? "EXCEPT without TRY" : "FINALLY without TRY", NULL);
                } else {
                    top->in_except = 1;
                }
                break;
            case OP_UNTIL: {
                long k = P->block_count - 1;
                while (k >= 0 && P->blocks[k].type != B_REPEAT) k--;
                if (k < 0) {
                    structure_error(P, t, "UNTIL without REPEAT", NULL);
                } else {
                    long m;
                    for (m = k + 1; m < P->block_count; m++) report_block(P, &P->blocks[m]);
                    P->block_count = k;
                }
                break;
            }
            case OP_END:
                while (P->block_count > 1 && P->blocks[P->block_count - 1].type == B_REPEAT) {
                    report_block(P, &P->blocks[--P->block_count]);
                }
                P->block_count--;
                if (P->block_count <= 0) {
                    P->block_count = 0;
                    finish_body(P, j);
                    return;
                }
                break;
            default:
                break;
            }
        } else if (is_op(t, ';')) {
            start = 1;
        }
        prev = j;
    }
    {
        long k;
        for (k = 0; k < P->block_count; k++) report_block(P, &P->blocks[k]);
    }
    P->block_count = 0;
    emit_to(P, n);
    P->i = n;
    if (P->routine_count > 0) P->routine_count = 0;
    P->finished = 1;    /* reported already */
}

/**
 * @brief Read the head of a program, unit or library.
 * @param P The parser, standing on the keyword.
 */
static void read_module(Parser *P)
{
    long n = P->tok_count, j = P->i + 1, last;
    const Tok *kw = &P->toks[P->i];
    if (is_text(P, kw, "unit")) P->is_unit = 1;
    if (j >= n || P->toks[j].type != T_WORD) {
        structure_error(P, kw, "name expected", NULL);
        P->i = j;
        return;
    }
    last = j;
    while (last + 2 < n && is_op(&P->toks[last + 1], '.') && P->toks[last + 2].type == T_WORD) last += 2;
    if (P->pass == 1) {
        PasRange r, sel;
        tok_range(kw, &r);
        tok_range(&P->toks[j], &sel);
        sel.end_line = P->toks[last].line;
        sel.end_col = P->toks[last].col + (long)(P->toks[last].end - P->toks[last].start);
        r.end_line = sel.end_line;
        r.end_col = sel.end_col;
        add_symbol(P, P->src + P->toks[j].start, P->toks[last].end - P->toks[j].start,
                   SCOUT_SYMBOL_MODULE, PAS_SCOPE_FILE, -1, NULL, &r, &sel);
    }
    j = last + 1;
    if (j < n && is_op(&P->toks[j], '(')) {     /* program x(input, output) */
        long close = matching_bracket(P, j);
        if (close >= 0) j = close + 1;
    }
    P->i = expect_semicolon(P, j);
}

/**
 * @brief Read a uses clause.
 * @param P The parser, standing on USES.
 */
static void read_uses(Parser *P)
{
    long n = P->tok_count, j = P->i + 1;
    PasResult *res = P->res;
    while (j < n && !is_op(&P->toks[j], ';')) {
        const Tok *t = &P->toks[j];
        long last = j;
        if (t->type != T_WORD) {
            j++;
            continue;
        }
        if (t->word) break;     /* the ';' is missing */
        while (last + 2 < n && is_op(&P->toks[last + 1], '.') && P->toks[last + 2].type == T_WORD) last += 2;
        if (P->pass == 1 && grow(P, (void **)&res->references, res->reference_count, &P->reference_cap, sizeof(PasReference))) {
            const Tok *e = &P->toks[last];
            const Tok *s = t;
            int unit = 1;
            size_t a = t->start, b = e->end;
            char *copy;
            /* uses x in 'file.pas' */
            if (last + 2 < n && is_kw(&P->toks[last + 1], OP_PLAIN) && is_text(P, &P->toks[last + 1], "in") &&
                P->toks[last + 2].type == T_STRING && P->toks[last + 2].end - P->toks[last + 2].start > 2) {
                s = e = &P->toks[last + 2];
                a = s->start + 1;
                b = s->end - 1;
                unit = 0;
            }
            copy = dup_text(P, P->src + a, b - a);
            if (copy) {
                PasReference *r = &res->references[res->reference_count++];
                r->target = copy;
                r->unit = unit;
                r->import = 1;
                tok_range(s, &r->range);
                r->range.end_line = e->line;
                r->range.end_col = e->col + (long)(e->end - e->start);
            }
        }
        j = last + 1;
        if (j < n && is_text(P, &P->toks[j], "in")) j += 2;
    }
    P->i = j < n && is_op(&P->toks[j], ';') ? j + 1 : j;
    if (j < n && !is_op(&P->toks[j], ';')) structure_error(P, &P->toks[j > 0 ? j - 1 : 0], "';' expected", NULL);
}

/**
 * @brief Follow one element of the declarations.
 * @param P The parser.
 */
static void step_declarations(Parser *P)
{
    const Tok *t = &P->toks[P->i];
    const Tok *n = P->i + 1 < P->tok_count ? &P->toks[P->i + 1] : NULL;
    if (t->type == T_WORD && t->word) {
        switch (t->word->op) {
        case OP_MODULE: read_module(P); return;
        case OP_USES: read_uses(P); return;
        case OP_INTERFACE: P->part = PART_INTERFACE; P->section = S_NONE; P->i++; return;
        case OP_IMPLEMENTATION: P->part = PART_IMPLEMENTATION; P->section = S_NONE; P->i++; return;
        case OP_CONST: P->section = S_CONST; P->i++; return;
        case OP_VAR: P->section = S_VAR; P->i++; return;
        case OP_TYPE: P->section = S_TYPE; P->i++; return;
        case OP_LABEL:
            P->i++;
            read_labels(P);
            return;
        case OP_EXPORTS: case OP_PROPERTY: P->i = skip_declaration(P, P->i + 1); return;
        case OP_CLASS:
            if (n && (is_kw(n, OP_PROCEDURE) || is_kw(n, OP_FUNCTION) || is_kw(n, OP_CONSTRUCTOR) || is_kw(n, OP_OPERATOR))) {
                P->i = read_routine(P, P->i, -1);
            } else {
                P->i++;
            }
            return;
        case OP_PROCEDURE: case OP_FUNCTION: case OP_CONSTRUCTOR: case OP_OPERATOR:
            P->i = read_routine(P, P->i, -1);
            return;
        case OP_BEGIN: case OP_ASM: case OP_INITIALIZATION:
            read_body(P);
            return;
        case OP_END:
            if (P->routine_count == 0 && P->is_unit && P->part != PART_START) {
                finish_body(P, P->i);
            } else {
                structure_error(P, t, "END without BEGIN", NULL);
                P->i++;
            }
            return;
        default:
            P->i++;
            return;
        }
    }
    if (is_name(t) && !(n && is_kw(n, OP_PROCEDURE)) && !(n && is_kw(n, OP_FUNCTION))) {
        switch (P->section) {
        case S_CONST: read_const(P); return;
        case S_VAR: read_var(P); return;
        case S_TYPE: read_type(P); return;
        default: break;
        }
    }
    P->i++;
}

/**
 * @brief Make one pass over the tokens.
 * @param P The parser.
 * @param pass 1 or 2.
 */
static void run_pass(Parser *P, int pass)
{
    long steps = 0;
    P->pass = pass;
    P->i = 0;
    P->emitted = 0;
    P->finished = 0;
    P->part = PART_START;
    P->section = S_NONE;
    P->block_count = 0;
    P->routine_count = 0;
    P->local_count = 0;
    P->is_unit = 0;
    while (P->i < P->tok_count && !P->finished && !P->res->out_of_memory && !P->res->stopped) {
        if (++steps % STOP_INTERVAL == 0 && pascal_parser_should_stop && pascal_parser_should_stop()) {
            P->res->stopped = 1;
            return;
        }
        step_declarations(P);
        emit_to(P, P->i);
    }
    if (pass == 1 && !P->finished && P->tok_count > 0 && !P->has_includes && !P->res->stopped &&
        P->res->diagnostic_count == 0) {
        const Tok *last = &P->toks[P->tok_count - 1];
        long k;
        for (k = 0; k < P->routine_count; k++) {
            structure_error(P, P->routines[k].name, "routine without a body", NULL);
        }
        if (P->routine_count == 0) structure_error(P, last, "the final END is missing", NULL);
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
    const PasDiagnostic *x = (const PasDiagnostic *)a, *y = (const PasDiagnostic *)b;
    if (x->range.start_line != y->range.start_line) return x->range.start_line < y->range.start_line ? -1 : 1;
    if (x->range.start_col != y->range.start_col) return x->range.start_col < y->range.start_col ? -1 : 1;
    return 0;
}

PasResult *pascal_parser_analyze(const char *source, size_t length, int is_include)
{
    Parser *P;
    PasResult *res;
    long k;

    make_word_table();
    P = (Parser *)calloc(1, sizeof(Parser));
    if (!P) return NULL;
    res = (PasResult *)calloc(1, sizeof(PasResult));
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
    P->lenient = is_include;
    for (k = 0; k < DEF_SLOTS; k++) P->def_slots[k] = -1;
    tokenize(P);
    if (!res->stopped && !res->out_of_memory) {
        if (P->pp_depth > 0 && P->pp_depth <= MAX_CONDITIONALS) {
            const PasRange *r = &P->pp_open[P->pp_depth - 1];
            add_diagnostic(P, SCOUT_SEVERITY_ERROR, r->start_line, r->start_col, r->end_col, "$IF without $ENDIF", NULL, 0);
        }
        check_brackets(P);
        run_pass(P, 1);
        if (!res->stopped && !res->out_of_memory) run_pass(P, 2);
    }
    if (res->diagnostic_count > 1) {
        qsort(res->diagnostics, (size_t)res->diagnostic_count, sizeof(PasDiagnostic), compare_diagnostics);
    }
    free(P->toks);
    free(P->defs);
    free(P->def_slots);
    free(P);
    return res;
}

void pascal_result_free(PasResult *res)
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
