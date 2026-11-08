/**
 * @file asm_parser.c
 * @brief The analysis of the assembler parser of Scout: two passes over the
 *        lines of a 68k source in Motorola syntax.
 *
 * Both assemblers it is written for, vasm (motorola syntax) and AsmPro,
 * descend from Devpac and differ only in details. Where they could differ,
 * the parser takes the reading that gives no false error.
 *
 * What instructions and sizes exist was taken from vasm 2.0b: every
 * instruction was assembled with every size and a list of operands.
 */
#include "asm_parser.h"

#include <stdlib.h>
#include <string.h>

int (*asm_parser_should_stop)(void) = NULL;

/** Most diagnostics reported for one source. */
#define MAX_DIAGNOSTICS 200

/** Lines between two calls of asm_parser_should_stop(). */
#define STOP_INTERVAL 64

/** Longest text of a symbol's detail. */
#define MAX_DETAIL 60

/** @name Sizes of an instruction (Word::sizes) */
/** @{ */
#define S_B 1       /**< .b */
#define S_W 2       /**< .w */
#define S_L 4       /**< .l */
#define S_S 8       /**< .s */
/** @} */

/** The instructions, in the groups of size_groups, then those without a check of the size. */
const char *const asm_parser_instructions[] = {
    /* sizes: b */
    "abcd", "nbcd", "sbcd", "tas", "st", "sf", "shi", "sls", "scc", "shs", "scs", "slo", "sne",
    "seq", "svc", "svs", "spl", "smi", "sge", "slt", "sgt", "sle",
    /* sizes: bl */
    "bchg", "bclr", "bset", "btst",
    /* sizes: bwl */
    "add", "addi", "addq", "addx", "and", "andi", "asl", "asr", "cas", "chk2", "clr", "cmp",
    "cmpi", "cmpm", "cmp2", "eor", "eori", "lsl", "lsr", "move", "movea", "moves", "neg",
    "negx", "not", "or", "ori", "rol", "ror", "roxl", "roxr", "sub", "subi", "subq", "subx",
    "tst",
    /* sizes: bwls */
    "bra", "bsr", "bhi", "bls", "bcc", "bhs", "bcs", "blo", "bne", "beq", "bvc", "bvs", "bpl",
    "bmi", "bge", "blt", "bgt", "ble",
    /* sizes: l */
    "divsl", "divul", "exg", "extb", "lea", "movec", "moveq", "pea",
    /* sizes: w */
    "lpstop", "swap", "dbt", "dbf", "dbhi", "dbls", "dbcc", "dbhs", "dbcs", "dblo", "dbne",
    "dbeq", "dbvc", "dbvs", "dbpl", "dbmi", "dbge", "dblt", "dbgt", "dble", "dbra",
    /* sizes: wl */
    "adda", "cas2", "chk", "cmpa", "divs", "divu", "ext", "link", "movem", "movep", "muls",
    "mulu", "suba",
    /* sizes not checked */
    "bfchg", "bfclr", "bfexts", "bfextu", "bfffo", "bfins", "bfset", "bftst", "bgnd", "bkpt",
    "callm", "cinva", "cinvl", "cinvp", "cpusha", "cpushl", "cpushp", "illegal", "jmp", "jsr",
    "move16", "nop", "pack", "pflush", "pflusha", "pflushan", "pflushn", "pflushr", "pflushs",
    "ploadr", "ploadw", "pmove", "pmovefd", "prestore", "psave", "ptestr", "ptestw", "plpar",
    "plpaw", "reset", "rtd", "rte", "rtm", "rtr", "rts", "stop", "trap", "trapv", "unlk",
    "unpk", "trapt", "trapf", "traphi", "trapls", "trapcc", "traphs", "trapcs", "traplo",
    "trapne", "trapeq", "trapvc", "trapvs", "trappl", "trapmi", "trapge", "traplt", "trapgt",
    "traple",
    /* FPU */
    "fabs", "facos", "fadd", "fasin", "fatan", "fatanh", "fcmp", "fcos", "fcosh", "fdabs",
    "fdadd", "fddiv", "fdiv", "fdmove", "fdmul", "fdneg", "fdsqrt", "fdsub", "fetox", "fetoxm1",
    "fgetexp", "fgetman", "fint", "fintrz", "flog10", "flog2", "flogn", "flognp1", "fmod",
    "fmove", "fmovecr", "fmovem", "fmul", "fneg", "fnop", "frem", "frestore", "fsabs", "fsadd",
    "fsave", "fscale", "fsdiv", "fsgldiv", "fsglmul", "fsin", "fsincos", "fsinh", "fsmove",
    "fsmul", "fsneg", "fsqrt", "fssqrt", "fssub", "fsub", "ftan", "ftanh", "ftentox", "ftst",
    "ftwotox", "fbeq", "fbne", "fbgt", "fbngt", "fbge", "fbnge", "fblt", "fbnlt", "fble",
    "fbnle", "fbgl", "fbngl", "fbgle", "fbngle", "fbogt", "fbule", "fboge", "fbult", "fbolt",
    "fbuge", "fbole", "fbugt", "fbogl", "fbueq", "fbor", "fbun", "fbf", "fbt", "fbsf", "fbst",
    "fbseq", "fbsne", "fdbeq", "fdbne", "fdbgt", "fdbngt", "fdbge", "fdbnge", "fdblt", "fdbnlt",
    "fdble", "fdbnle", "fdbgl", "fdbngl", "fdbgle", "fdbngle", "fdbogt", "fdbule", "fdboge",
    "fdbult", "fdbolt", "fdbuge", "fdbole", "fdbugt", "fdbogl", "fdbueq", "fdbor", "fdbun",
    "fdbf", "fdbt", "fdbsf", "fdbst", "fdbseq", "fdbsne", "fseq", "fsne", "fsgt", "fsngt",
    "fsge", "fsnge", "fslt", "fsnlt", "fsle", "fsnle", "fsgl", "fsngl", "fsgle", "fsngle",
    "fsogt", "fsule", "fsoge", "fsult", "fsolt", "fsuge", "fsole", "fsugt", "fsogl", "fsueq",
    "fsor", "fsun", "fsf", "fst", "fssf", "fsst", "fsseq", "fssne", "ftrapeq", "ftrapne",
    "ftrapgt", "ftrapngt", "ftrapge", "ftrapnge", "ftraplt", "ftrapnlt", "ftraple", "ftrapnle",
    "ftrapgl", "ftrapngl", "ftrapgle", "ftrapngle", "ftrapogt", "ftrapule", "ftrapoge",
    "ftrapult", "ftrapolt", "ftrapuge", "ftrapole", "ftrapugt", "ftrapogl", "ftrapueq",
    "ftrapor", "ftrapun", "ftrapf", "ftrapt", "ftrapsf", "ftrapst", "ftrapseq", "ftrapsne",
    NULL
};

/** A group of asm_parser_instructions with the same sizes. */
typedef struct SizeGroup {
    long count;             /**< number of instructions */
    unsigned char sizes;    /**< their sizes (S_ flags) */
} SizeGroup;

/** The groups at the start of asm_parser_instructions. */
static const SizeGroup size_groups[] = {
    { 22, S_B },
    { 4, S_B | S_L },
    { 36, S_B | S_W | S_L },
    { 18, S_B | S_W | S_L | S_S },
    { 8, S_L },
    { 21, S_W },
    { 13, S_W | S_L }
};

/** Instructions without operands: what follows them is a comment. */
static const char *const no_operands[] = {
    "bgnd", "fnop", "illegal", "nop", "pflusha", "pflushan", "reset", "rte", "rtr", "rts", "trapv",
    NULL
};

const char *const asm_parser_registers[] = {
    "d0", "d1", "d2", "d3", "d4", "d5", "d6", "d7",
    "a0", "a1", "a2", "a3", "a4", "a5", "a6", "a7",
    "sp", "pc", "sr", "ccr", "usp", "vbr", "sfc", "dfc", "cacr", "caar",
    "msp", "isp", "itt0", "itt1", "dtt0", "dtt1", "tc", "mmusr", "urp", "srp",
    "crp", "fp0", "fp1", "fp2", "fp3", "fp4", "fp5", "fp6", "fp7", "fpcr",
    "fpsr", "fpiar",
    NULL
};

/** What a directive does, as far as it matters. */
typedef enum Op {
    OP_NONE,        /**< nothing that matters here, or an instruction */
    OP_IF,          /**< opens a conditional */
    OP_ELSE,        /**< ELSE */
    OP_ELIF,        /**< ELIF */
    OP_ENDC,        /**< ENDC, ENDIF */
    OP_MACRO,       /**< MACRO */
    OP_ENDM,        /**< ENDM */
    OP_REPT,        /**< REPT */
    OP_ENDR,        /**< ENDR */
    OP_END,         /**< END */
    OP_REM,         /**< REM */
    OP_EREM,        /**< EREM */
    OP_EQU,         /**< defines a constant */
    OP_SET,         /**< defines a variable */
    OP_RS,          /**< defines a field */
    OP_XDEF,        /**< exports names */
    OP_INCLUDE      /**< INCLUDE */
} Op;

/** @name Flags of a directive or instruction (Word::flags) */
/** @{ */
#define F_NOARG     1   /**< has no operands: what follows is a comment */
#define F_FREE      2   /**< the operands are text, not expressions */
#define F_INSN      4   /**< an instruction */
/** @} */

/** A directive in the table. */
typedef struct Directive {
    const char *name;       /**< its name in lower case */
    unsigned char op;       /**< its Op */
    unsigned char flags;    /**< F_ flags */
} Directive;

/** The directives of vasm's motorola syntax, AsmPro and Devpac. */
static const Directive directives[] = {
    { "abs", OP_NONE, 0 }, { "align", OP_NONE, 0 }, { "auto", OP_NONE, F_FREE },
    { "basereg", OP_NONE, 0 }, { "blk", OP_NONE, 0 }, { "bss", OP_NONE, F_FREE },
    { "bss_c", OP_NONE, F_FREE }, { "bss_f", OP_NONE, F_FREE }, { "cargs", OP_NONE, 0 },
    { "clrfo", OP_NONE, F_NOARG }, { "clrso", OP_NONE, F_NOARG }, { "cnop", OP_NONE, 0 },
    { "code", OP_NONE, F_FREE }, { "code_c", OP_NONE, F_FREE }, { "code_f", OP_NONE, F_FREE },
    { "comment", OP_NONE, F_FREE }, { "cseg", OP_NONE, F_FREE }, { "data", OP_NONE, F_FREE },
    { "data_c", OP_NONE, F_FREE }, { "data_f", OP_NONE, F_FREE }, { "dc", OP_NONE, 0 },
    { "dcb", OP_NONE, 0 }, { "dr", OP_NONE, 0 }, { "ds", OP_NONE, 0 }, { "dseg", OP_NONE, F_FREE },
    { "dx", OP_NONE, 0 }, { "einline", OP_NONE, F_NOARG }, { "elif", OP_ELIF, 0 },
    { "else", OP_ELSE, F_NOARG }, { "elseif", OP_ELIF, 0 }, { "end", OP_END, F_NOARG },
    { "endb", OP_NONE, F_FREE }, { "endc", OP_ENDC, F_NOARG }, { "endif", OP_ENDC, F_NOARG },
    { "endm", OP_ENDM, F_NOARG }, { "endr", OP_ENDR, F_NOARG }, { "equ", OP_EQU, 0 },
    { "equr", OP_EQU, F_FREE }, { "erem", OP_EREM, F_NOARG }, { "even", OP_NONE, F_NOARG },
    { "fail", OP_NONE, F_FREE }, { "far", OP_NONE, F_FREE }, { "fequ", OP_EQU, F_FREE },
    { "fo", OP_RS, 0 }, { "global", OP_XDEF, F_FREE }, { "idnt", OP_NONE, F_FREE },
    { "if", OP_IF, 0 }, { "if1", OP_IF, F_FREE }, { "if2", OP_IF, F_FREE }, { "ifb", OP_IF, F_FREE },
    { "ifc", OP_IF, F_FREE }, { "ifd", OP_IF, F_FREE }, { "ifeq", OP_IF, 0 }, { "ifge", OP_IF, 0 },
    { "ifgt", OP_IF, 0 }, { "ifle", OP_IF, 0 }, { "iflt", OP_IF, 0 },
    { "ifmacrod", OP_IF, F_FREE }, { "ifmacrond", OP_IF, F_FREE }, { "ifnb", OP_IF, F_FREE },
    { "ifnc", OP_IF, F_FREE }, { "ifnd", OP_IF, F_FREE }, { "ifne", OP_IF, 0 },
    { "ifp1", OP_IF, F_FREE }, { "incbin", OP_NONE, F_FREE }, { "incdir", OP_NONE, F_FREE },
    { "include", OP_INCLUDE, F_FREE }, { "inline", OP_NONE, F_NOARG }, { "jumperr", OP_NONE, F_FREE },
    { "jumpptr", OP_NONE, F_FREE }, { "list", OP_NONE, F_FREE }, { "llen", OP_NONE, 0 },
    { "load", OP_NONE, 0 }, { "machine", OP_NONE, F_FREE }, { "macro", OP_MACRO, F_FREE },
    { "mc68000", OP_NONE, F_NOARG }, { "mc68010", OP_NONE, F_NOARG }, { "mc68020", OP_NONE, F_NOARG },
    { "mc68030", OP_NONE, F_NOARG }, { "mc68040", OP_NONE, F_NOARG }, { "mc68060", OP_NONE, F_NOARG },
    { "mexit", OP_NONE, F_NOARG }, { "msource", OP_NONE, F_FREE }, { "near", OP_NONE, F_FREE },
    { "nolist", OP_NONE, F_FREE }, { "nopage", OP_NONE, F_FREE }, { "nref", OP_NONE, F_FREE },
    { "odd", OP_NONE, F_NOARG }, { "offset", OP_NONE, 0 }, { "opt", OP_NONE, F_FREE },
    { "org", OP_NONE, 0 }, { "output", OP_NONE, F_FREE }, { "page", OP_NONE, F_FREE },
    { "plen", OP_NONE, 0 }, { "printt", OP_NONE, F_FREE }, { "printv", OP_NONE, F_FREE },
    { "public", OP_XDEF, F_FREE }, { "reg", OP_EQU, F_FREE }, { "rem", OP_REM, F_FREE },
    { "rept", OP_REPT, 0 }, { "rorg", OP_NONE, 0 }, { "rs", OP_RS, 0 },
    { "rsreset", OP_NONE, F_NOARG }, { "rsset", OP_NONE, 0 }, { "section", OP_NONE, F_FREE },
    { "set", OP_SET, 0 }, { "setfo", OP_NONE, 0 }, { "setso", OP_NONE, 0 }, { "so", OP_RS, 0 },
    { "spc", OP_NONE, 0 }, { "text", OP_NONE, F_FREE }, { "ttl", OP_NONE, F_FREE },
    { "weak", OP_XDEF, F_FREE }, { "xdef", OP_XDEF, F_FREE }, { "xref", OP_NONE, F_FREE }
};

/** Number of directives. */
#define DIRECTIVE_COUNT ((long)(sizeof(directives) / sizeof(directives[0])))

/** What a macro of the NDK (exec/types.i, exec/libraries.i, exec/io.i) defines. */
typedef enum NdkAction {
    NDK_NOTHING,    /**< nothing */
    NDK_STRUCTURE,  /**< STRUCTURE: the name of a structure */
    NDK_FIELD,      /**< a field: its first operand */
    NDK_STRUCT,     /**< STRUCT: a field with a size */
    NDK_CONSTANT,   /**< a constant: its first operand */
    NDK_BITDEF,     /**< BITDEF: the bit number and the flag */
    NDK_FUNCDEF     /**< FUNCDEF: _LVO and its operand */
} NdkAction;

/** A macro of the NDK. */
typedef struct NdkMacro {
    const char *name;       /**< its name */
    unsigned char action;   /**< its NdkAction */
} NdkMacro;

/**
 * The macros of the NDK that define names or are common without an INCLUDE
 * of their own. They are defined in a file that is not read here; that a
 * source can define its own macro of such a name is checked at the call.
 * These names are not in the table of words, as they are also usual labels.
 */
static const NdkMacro ndk_macros[] = {
    { "STRUCTURE", NDK_STRUCTURE },
    { "FPTR", NDK_FIELD }, { "BOOL", NDK_FIELD }, { "BYTE", NDK_FIELD }, { "UBYTE", NDK_FIELD },
    { "WORD", NDK_FIELD }, { "UWORD", NDK_FIELD }, { "SHORT", NDK_FIELD }, { "USHORT", NDK_FIELD },
    { "LONG", NDK_FIELD }, { "ULONG", NDK_FIELD }, { "FLOAT", NDK_FIELD }, { "DOUBLE", NDK_FIELD },
    { "APTR", NDK_FIELD }, { "CPTR", NDK_FIELD }, { "RPTR", NDK_FIELD }, { "LABEL", NDK_FIELD },
    { "STRUCT", NDK_STRUCT },
    { "EITEM", NDK_CONSTANT }, { "LIBDEF", NDK_CONSTANT }, { "DEVCMD", NDK_CONSTANT },
    { "BITDEF", NDK_BITDEF },
    { "FUNCDEF", NDK_FUNCDEF },
    { "ALIGNWORD", NDK_NOTHING }, { "ALIGNLONG", NDK_NOTHING }, { "ENUM", NDK_NOTHING },
    { "LIBINIT", NDK_NOTHING }, { "DEVINIT", NDK_NOTHING }
};

/** Longest name made from a macro's operands. */
#define MAX_MADE_NAME 128

/** A known mnemonic or directive. */
typedef struct Word {
    const char *name;       /**< its name in lower case */
    unsigned char op;       /**< its Op */
    unsigned char flags;    /**< F_ flags */
    unsigned char sizes;    /**< the sizes it has (S_ flags), 0 if not checked */
    short next;             /**< next word with the same hash, -1 */
} Word;

/** Room in the table of words. */
#define WORD_ROOM 1024

/** Slots of the hash table of words (a power of two). */
#define WORD_SLOTS 1024

static Word words[WORD_ROOM];       /**< the known words */
static short word_slots[WORD_SLOTS];/**< hash table of words: first index, -1 */
static long word_count = 0;         /**< number of known words, 0 before make_word_table() */

/** The meaning of "=" (no word in the table: the rules could not use it). */
static const Word equals_word = { "=", OP_EQU, 0, 0, -1 };

/** A part of the source. */
typedef struct Span {
    size_t start;   /**< offset of its first byte */
    size_t end;     /**< offset after its last byte; the same if empty */
} Span;

/** The parts of a line. */
typedef struct Line {
    Span label;         /**< the label, if any */
    int colon;          /**< the label had a colon */
    Span mnemonic;      /**< the mnemonic, if any */
    Span size;          /**< the size after its dot, if any */
    Span operands;      /**< the operands, if any */
    const Word *word;   /**< the mnemonic if known */
} Line;

/** The kinds of name defined. */
typedef enum DefKind {
    DEF_LABEL,      /**< a label */
    DEF_CONST,      /**< an equate */
    DEF_SET,        /**< a SET */
    DEF_FIELD,      /**< a field */
    DEF_MACRO,      /**< a macro */
    DEF_STRUCT      /**< a structure */
} DefKind;

/** A name defined in the source. */
typedef struct Def {
    const char *text;   /**< its name */
    long len;           /**< its length */
    long scope;         /**< 0, or the part a local label belongs to */
    long next;          /**< next with the same hash, -1 */
    long symbol;        /**< its symbol, -1 */
    DefKind kind;       /**< what it is */
    int plain;          /**< defined outside conditionals and REPT */
    long line;          /**< its line */
    Span pattern;       /**< of a macro: the name its body defines with \\1 in it; empty if none */
} Def;

/** An open conditional or REPT. */
typedef struct Block {
    long line;          /**< its line */
    long col;           /**< its column */
    long end_col;       /**< the column after its directive */
    int seen_else;      /**< an ELSE has been seen */
} Block;

/** State of one analysis. */
typedef struct Parser {
    AsmResult *res;         /**< the result being built */
    long symbol_cap;        /**< room for symbols */
    long diagnostic_cap;    /**< room for diagnostics */
    long reference_cap;     /**< room for references */
    long token_cap;         /**< room for token values */
    const char *src;        /**< the text */
    size_t length;          /**< its length */
    size_t next;            /**< offset of the next line */
    size_t ls;              /**< offset of the current line */
    size_t le;              /**< offset of the end of the current line */
    long line;              /**< number of the current line, from 1 */
    Def *defs;              /**< the names defined */
    long def_count;         /**< number of them */
    long def_cap;           /**< room for them */
    long *slots;            /**< hash table of the names: first index, -1 */
    unsigned long mask;     /**< size of the table minus one */
    Block *ifs;             /**< the open conditionals (first pass) */
    long if_cap;            /**< room for them */
    long if_depth;          /**< number of open conditionals */
    Block *repts;           /**< the open REPTs (first pass) */
    long rept_cap;          /**< room for them */
    long rept_depth;        /**< number of open REPTs */
    long macro_depth;       /**< nesting of macro definitions: above 0 inside a body */
    long macro_symbol;      /**< symbol of the macro being defined, -1 */
    long macro_def;         /**< definition of the macro being defined, -1 */
    long structure;         /**< symbol of the last STRUCTURE, -1 */
    int is_include;         /**< the source is an include file: what it defines is seen by others */
    Block macro_open;       /**< where the outermost macro definition started */
    int in_rem;             /**< between REM and EREM */
    long scope;             /**< number of the current part between global labels */
    long last_label;        /**< symbol of the last global label, -1 */
    int includes;           /**< the source has an INCLUDE */
    int gnu;                /**< the source is in GNU syntax */
    int ended;              /**< END was reached */
    int second_pass;        /**< in the second pass */
} Parser;

/**
 * @brief Make a byte lower case (ASCII only: names are not Latin-1 here).
 * @param c The byte.
 * @return It in lower case.
 */
static unsigned char lower(unsigned char c)
{
    return (c >= 'A' && c <= 'Z') ? (unsigned char)(c + 32) : c;
}

/**
 * @brief The hash of a name without regard to case.
 * @param text The name.
 * @param len Its length.
 * @return The hash.
 */
static unsigned long hash_name(const char *text, size_t len)
{
    unsigned long h = 0;
    size_t i;
    for (i = 0; i < len; i++) h = (h << 5) - h + lower((unsigned char)text[i]);
    return h;
}

/**
 * @brief Add a word to the table, unless there is no room.
 * @param name Its name in lower case.
 * @param op Its Op.
 * @param flags Its F_ flags.
 * @param sizes Its S_ flags.
 */
static void add_word(const char *name, int op, int flags, int sizes)
{
    unsigned long h = hash_name(name, strlen(name)) & (WORD_SLOTS - 1);
    Word *w;
    if (word_count >= WORD_ROOM) return;
    w = &words[word_count];
    w->name = name;
    w->op = (unsigned char)op;
    w->flags = (unsigned char)flags;
    w->sizes = (unsigned char)sizes;
    w->next = word_slots[h];
    word_slots[h] = (short)word_count++;
}

/**
 * @brief Find a known word.
 * @param text The word (any case).
 * @param len Its length.
 * @return The word, or NULL.
 */
static Word *find_word(const char *text, size_t len)
{
    short i = word_slots[hash_name(text, len) & (WORD_SLOTS - 1)];
    for (; i >= 0; i = words[i].next) {
        const char *n = words[i].name;
        size_t k;
        for (k = 0; k < len && n[k] && n[k] == (char)lower((unsigned char)text[k]); k++) {}
        if (k == len && n[k] == '\0') return &words[i];
    }
    return NULL;
}

/** @brief Build the table of words once per process. */
static void make_word_table(void)
{
    long i, g, at = 0;
    if (word_count > 0) return;
    for (i = 0; i < WORD_SLOTS; i++) word_slots[i] = -1;
    for (g = 0; g < (long)(sizeof(size_groups) / sizeof(size_groups[0])); g++)
        for (i = 0; i < size_groups[g].count; i++)
            add_word(asm_parser_instructions[at++], OP_NONE, F_INSN, size_groups[g].sizes);
    for (; asm_parser_instructions[at]; at++) add_word(asm_parser_instructions[at], OP_NONE, F_INSN, 0);
    for (i = 0; no_operands[i]; i++) {
        Word *w = find_word(no_operands[i], strlen(no_operands[i]));
        if (w) w->flags |= F_NOARG;
    }
    for (i = 0; i < DIRECTIVE_COUNT; i++) add_word(directives[i].name, directives[i].op, directives[i].flags, 0);
}

const char *asm_parser_directive_name(long index)
{
    return (index >= 0 && index < DIRECTIVE_COUNT) ? directives[index].name : NULL;
}

/**
 * @brief Make room for one more element of an array.
 * @param P The parser.
 * @param array The array.
 * @param count Elements in use.
 * @param cap The room, updated.
 * @param size The size of an element.
 * @return Non-zero if there is room.
 */
static int grow(Parser *P, void **array, long count, long *cap, size_t size)
{
    void *bigger;
    long n;
    if (count < *cap) return 1;
    n = *cap ? *cap * 2 : 16;
    bigger = realloc(*array, (size_t)n * size);
    if (!bigger) {
        P->res->out_of_memory = 1;
        return 0;
    }
    *array = bigger;
    *cap = n;
    return 1;
}

/**
 * @brief Copy a part of the source.
 * @param P The parser.
 * @param text The first byte.
 * @param len The length.
 * @return The copy (allocated), or NULL.
 */
static char *dup_text(Parser *P, const char *text, size_t len)
{
    char *s = (char *)malloc(len + 1);
    if (!s) {
        P->res->out_of_memory = 1;
        return NULL;
    }
    memcpy(s, text, len);
    s[len] = '\0';
    return s;
}

/**
 * @brief The column of an offset in the current line.
 * @param P The parser.
 * @param at The offset.
 * @return The column, from 1.
 */
static long col_of(const Parser *P, size_t at)
{
    return (long)(at - P->ls) + 1;
}

/**
 * @brief Report a problem within one line.
 *
 * @param P The parser.
 * @param severity The SCOUT_SEVERITY_ value.
 * @param line The line.
 * @param col The first column.
 * @param end The column after it.
 * @param format The text; a "%s" stands for the name.
 * @param name The name (may be NULL).
 * @param len Its length.
 */
static void add_diagnostic(Parser *P, int severity, long line, long col, long end, const char *format,
                           const char *name, size_t len)
{
    AsmResult *res = P->res;
    const char *hole = name ? strstr(format, "%s") : NULL;
    size_t flen = strlen(format);
    AsmDiagnostic *d;
    char *msg;
    if (res->diagnostic_count >= MAX_DIAGNOSTICS) return;
    if (!grow(P, (void **)&res->diagnostics, res->diagnostic_count, &P->diagnostic_cap, sizeof(AsmDiagnostic))) return;
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
    d->range.start_line = d->range.end_line = line;
    d->range.start_col = col;
    d->range.end_col = end > col ? end : col + 1;
}

/**
 * @brief Report a problem at a part of the current line, naming it.
 * @param P The parser.
 * @param severity The SCOUT_SEVERITY_ value.
 * @param s The part.
 * @param format The text; a "%s" stands for the part.
 */
static void diag_span(Parser *P, int severity, Span s, const char *format)
{
    add_diagnostic(P, severity, P->line, col_of(P, s.start), col_of(P, s.end), format, P->src + s.start, s.end - s.start);
}

/**
 * @brief Add a token on the current line.
 * @param P The parser.
 * @param start Its first byte.
 * @param end The offset after it.
 * @param kind Its SCOUT_TOKEN_ kind.
 */
static void add_token(Parser *P, size_t start, size_t end, long kind)
{
    AsmResult *res = P->res;
    long n = res->token_count * 4;
    long *t;
    if (end <= start) return;
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
    t = res->tokens + n;
    t[0] = P->line;
    t[1] = col_of(P, start);
    t[2] = (long)(end - start);
    t[3] = kind;
    res->token_count++;
}

/**
 * @brief Tell whether a byte is a blank within a line.
 * @param c The byte.
 * @return Non-zero if it is.
 */
static int is_blank(char c)
{
    return c == ' ' || c == '\t';
}

/**
 * @brief Tell whether a byte can go on with a name.
 * @param c The byte.
 * @return Non-zero if it can.
 */
static int is_name_char(char c)
{
    unsigned char u = (unsigned char)c;
    return (u >= 'a' && u <= 'z') || (u >= 'A' && u <= 'Z') || (u >= '0' && u <= '9') || u == '_' || u == '$' || u >= 0x80;
}

/**
 * @brief The end of the macro parameter at a backslash.
 * @param P The parser.
 * @param i The backslash.
 * @param e The end of the text.
 * @return The offset after the parameter.
 */
static size_t parameter_end(const Parser *P, size_t i, size_t e)
{
    const char *s = P->src;
    size_t k = i + 1;
    if (k >= e) return e;
    if (s[k] == '<') {
        while (k < e && s[k] != '>') k++;
        return k < e ? k + 1 : e;
    }
    if (s[k] == '?') k++;
    if (k < e && is_name_char(s[k]) && !(s[k] >= '0' && s[k] <= '9')) {
        while (k < e && is_name_char(s[k]) && s[k] != '$') k++;
        return k;
    }
    return k < e ? k + 1 : e;
}

/**
 * @brief Read a name: an optional dot, then name characters and macro
 *        parameters.
 * @param P The parser.
 * @param i Its start.
 * @param e The end of the text.
 * @return The offset after the name; i if there is none.
 */
static size_t read_name(const Parser *P, size_t i, size_t e)
{
    const char *s = P->src;
    size_t k = i;
    if (k >= e || s[k] == '$') return i;
    if (s[k] == '.') k++;
    while (k < e) {
        if (s[k] == '\\') k = parameter_end(P, k, e);
        else if (is_name_char(s[k])) k++;
        else break;
    }
    return (k == i + 1 && s[i] == '.') ? i : k;
}

/**
 * @brief Tell whether a name has a macro parameter in it.
 * @param P The parser.
 * @param s The name.
 * @return Non-zero if it has.
 */
static int has_parameter(const Parser *P, Span s)
{
    return memchr(P->src + s.start, '\\', s.end - s.start) != NULL;
}

/**
 * @brief Tell whether a name is a local label's.
 * @param text The name.
 * @param len Its length.
 * @return Non-zero if it is.
 */
static int is_local_name(const char *text, size_t len)
{
    return len > 0 && (text[0] == '.' || text[len - 1] == '$');
}

/**
 * @brief Tell whether a part of the source is a local label's name.
 * @param P The parser.
 * @param start The name.
 * @param end The offset after it.
 * @return Non-zero if it is.
 */
static int is_local(const Parser *P, size_t start, size_t end)
{
    return end > start && is_local_name(P->src + start, end - start);
}

/**
 * @brief Find a name.
 *
 * Prefers one with the same case, as both spellings can be defined.
 *
 * @param P The parser.
 * @param text The name.
 * @param len Its length.
 * @param scope 0, or the part a local label is looked for in.
 * @return The definition, or NULL.
 */
static Def *find_def(const Parser *P, const char *text, size_t len, long scope)
{
    Def *found = NULL;
    long i;
    if (!P->slots) return NULL;
    for (i = P->slots[hash_name(text, len) & P->mask]; i >= 0; i = P->defs[i].next) {
        Def *d = &P->defs[i];
        if (d->scope != scope || d->len != (long)len) continue;
        if (memcmp(d->text, text, len) == 0) return d;
        if (!found) {
            size_t k;
            for (k = 0; k < len && lower((unsigned char)text[k]) == lower((unsigned char)d->text[k]); k++) {}
            if (k == len) found = d;
        }
    }
    return found;
}

/**
 * @brief Tell whether the next word of a line defines the word before it.
 * @param P The parser.
 * @param i Where to look from.
 * @param e The end of the line.
 * @return Non-zero if it does.
 */
static int definer_follows(const Parser *P, size_t i, size_t e)
{
    const char *s = P->src;
    size_t w;
    const Word *word;
    while (i < e && is_blank(s[i])) i++;
    if (i < e && s[i] == '=') return 1;
    w = read_name(P, i, e);
    if (w == i || (w < e && !is_blank(s[w]) && s[w] != '.' && s[w] != ';')) return 0;
    word = find_word(s + i, w - i);
    return word && (word->op == OP_EQU || word->op == OP_SET || word->op == OP_RS || word->op == OP_MACRO);
}

/**
 * @brief Tell whether the next word of a line is a mnemonic or macro, so
 *        that the word before it, in the first column, is a label.
 * @param P The parser.
 * @param i Where to look from.
 * @param e The end of the line.
 * @return Non-zero if it is.
 */
static int mnemonic_follows(const Parser *P, size_t i, size_t e)
{
    const char *s = P->src;
    const Def *d;
    size_t w;
    while (i < e && is_blank(s[i])) i++;
    w = read_name(P, i, e);
    if (w == i || (w < e && !is_blank(s[w]) && s[w] != '.' && s[w] != ';')) return 0;
    if (find_word(s + i, w - i)) return 1;
    d = find_def(P, s + i, w - i, 0);
    return d && d->kind == DEF_MACRO && d->line < P->line;
}

/**
 * @brief Note whether a word at the start of a line is typical of GNU
 *        syntax, where these checks would be wrong.
 * @param P The parser.
 * @param start The word.
 * @param end The offset after it.
 */
static void check_gnu(Parser *P, size_t start, size_t end)
{
    static const char *const names[] = {
        ".text", ".data", ".bss", ".globl", ".global", ".align", ".long", ".word", ".byte", ".ascii",
        ".asciz", ".section", ".comm", ".file", ".type", ".size", ".ident",
        NULL
    };
    size_t len = end - start;
    long i;
    if (len < 4 || P->src[start] != '.') return;
    for (i = 0; names[i]; i++)
        if (strlen(names[i]) == len && memcmp(names[i], P->src + start, len) == 0) P->gnu = 1;
}

/**
 * @brief Split the current line into its parts.
 * @param P The parser.
 * @param[out] L The parts.
 */
static void split_line(Parser *P, Line *L)
{
    const char *s = P->src;
    size_t i = P->ls, e = P->le, w;
    int depth = 0;

    memset(L, 0, sizeof(*L));
    if (i >= e || s[i] == '*' || s[i] == ';') return;
    if (s[i] == '#') {
        if (e - i >= 4 && memcmp(s + i + 1, "APP", 3) == 0) P->gnu = 1;
        if (e - i >= 7 && memcmp(s + i + 1, "NO_APP", 6) == 0) P->gnu = 1;
        return;
    }
    if (!is_blank(s[i])) {
        w = read_name(P, i, e);
        if (w == i || (s[i] >= '0' && s[i] <= '9' && s[w - 1] != '$')) return;
        check_gnu(P, i, w);
        if (w < e && s[w] == ':') {
            L->label.start = i;
            L->label.end = w;
            L->colon = 1;
            i = w + 1;
            if (i < e && s[i] == ':') i++;
        } else if (w == e || is_blank(s[w]) || s[w] == ';' || s[w] == '=') {
            const Def *d = find_def(P, s + i, w - i, 0);
            int macro = d && d->kind == DEF_MACRO && d->line < P->line;
            if ((find_word(s + i, w - i) || macro) && !mnemonic_follows(P, w, e) && (w == e || s[w] != '=')) {
                /* a mnemonic in the first column: the code below reads it */
            } else {
                L->label.start = i;
                L->label.end = w;
                i = w;
            }
        } else if (!(s[w] == '.' && find_word(s + i, w - i))) {
            return;     /* nothing that can be read */
        }
    }

    while (i < e && is_blank(s[i])) i++;
    if (i >= e || s[i] == ';' || s[i] == '*') return;
    if (s[i] == '=') {
        L->mnemonic.start = i;
        L->mnemonic.end = ++i;
        L->word = &equals_word;
    } else {
        w = read_name(P, i, e);
        if (w == i) return;
        check_gnu(P, i, w);
        if (L->label.end == L->label.start) {
            /* an indented label: followed by a colon, or by what defines it */
            if (w < e && s[w] == ':') {
                L->label.start = i;
                L->label.end = w;
                L->colon = 1;
                i = w + 1;
                if (i < e && s[i] == ':') i++;
                while (i < e && is_blank(s[i])) i++;
                if (i >= e || s[i] == ';' || s[i] == '*') return;
                w = read_name(P, i, e);
                if (w == i && s[i] != '=') return;
            } else if (!find_word(s + i, w - i) && definer_follows(P, w, e)) {
                L->label.start = i;
                L->label.end = w;
                i = w;
                while (i < e && is_blank(s[i])) i++;
                w = read_name(P, i, e);
            }
            if (i < e && s[i] == '=') {
                L->mnemonic.start = i;
                L->mnemonic.end = ++i;
                L->word = &equals_word;
                w = i;
            }
        }
        if (!L->word) {
            L->mnemonic.start = i;
            L->mnemonic.end = w;
            i = w;
            if (i < e && s[i] == '.') {
                size_t z = i + 1;
                while (z < e && (is_name_char(s[z]) || s[z] == '\\')) {
                    if (s[z] == '\\') z = parameter_end(P, z, e);
                    else z++;
                }
                L->size.start = i + 1;
                L->size.end = z;
                i = z;
            }
            L->word = find_word(s + L->mnemonic.start, L->mnemonic.end - L->mnemonic.start);
        }
    }
    if (L->word && (L->word->flags & F_NOARG)) return;

    /* the operands: up to a blank outside quotes and parentheses */
    while (i < e && is_blank(s[i])) i++;
    L->operands.start = i;
    while (i < e) {
        char c = s[i];
        if (c == ';') break;
        if (c == '\'' || c == '"') {
            size_t q = i + 1;
            while (q < e && s[q] != c) q++;
            i = q < e ? q + 1 : e;
            continue;
        }
        if (c == '(') depth++;
        else if (c == ')' && depth > 0) depth--;
        else if (is_blank(c) && depth == 0) {
            if (i > L->operands.start && s[i - 1] == ',') {
                size_t k = i;
                while (k < e && is_blank(s[k])) k++;
                if (k < e && s[k] != ';') {
                    i = k;
                    continue;
                }
            }
            break;
        }
        i++;
    }
    /* blanks before a ';' inside parentheses that were never closed */
    while (i > L->operands.start && is_blank(s[i - 1])) i--;
    L->operands.end = i;
}

/**
 * @brief Go to the next line.
 * @param P The parser.
 * @return Non-zero if there is one.
 */
static int next_line(Parser *P)
{
    const char *s = P->src;
    size_t i = P->next;
    if (i >= P->length) return 0;
    P->ls = i;
    while (i < P->length && s[i] != '\n' && s[i] != '\r') i++;
    P->le = i;
    if (i < P->length) {
        if (s[i] == '\r' && i + 1 < P->length && s[i + 1] == '\n') i++;
        i++;
    }
    P->next = i;
    P->line++;
    return 1;
}

/** @brief Go back to the first line and the state before a pass.
 * @param P The parser.
 */
static void rewind_source(Parser *P)
{
    P->next = 0;
    P->line = 0;
    P->scope = 1;
    P->last_label = -1;
    P->if_depth = 0;
    P->rept_depth = 0;
    P->macro_depth = 0;
    P->macro_symbol = -1;
    P->macro_def = -1;
    P->structure = -1;
    P->in_rem = 0;
    P->ended = 0;
}

/**
 * @brief Turn every run of blanks in a text into one space.
 * @param text The text, changed in place.
 */
static void collapse_blanks(char *text)
{
    char *to = text;
    const char *from = text;
    while (*from) {
        if (is_blank(*from)) {
            while (is_blank(*from)) from++;
            *to++ = ' ';
        } else {
            *to++ = *from++;
        }
    }
    *to = '\0';
}

/**
 * @brief Make a symbol for a name on the current line.
 *
 * @param P The parser.
 * @param text The name.
 * @param len Its length.
 * @param name Where it stands, or the operand it was made from.
 * @param kind Its SCOUT_SYMBOL_ kind.
 * @param scope Its AsmScope.
 * @param parent The index of its parent, or -1.
 * @param L The line.
 * @return Its index, or -1.
 */
static long add_symbol(Parser *P, const char *text, size_t len, Span name, int kind, int scope, long parent, const Line *L)
{
    AsmResult *res = P->res;
    AsmSymbol *sym;
    char *copy, *detail = NULL;
    size_t end = name.end;
    if (L->mnemonic.end > end) end = L->mnemonic.end;
    if (L->operands.end > end) end = L->operands.end;
    if (kind != SCOUT_SYMBOL_LABEL && kind != SCOUT_SYMBOL_MACRO && L->mnemonic.end > L->mnemonic.start) {
        size_t from = L->mnemonic.start;
        size_t to = L->operands.end > L->operands.start ? L->operands.end : (L->size.end > 0 ? L->size.end : L->mnemonic.end);
        size_t room = to - from;
        if (room > MAX_DETAIL) room = MAX_DETAIL;
        detail = dup_text(P, P->src + from, room);
        if (detail) collapse_blanks(detail);
    }
    if (!grow(P, (void **)&res->symbols, res->symbol_count, &P->symbol_cap, sizeof(AsmSymbol))
        || (copy = dup_text(P, text, len)) == NULL) {
        free(detail);
        return -1;
    }
    sym = &res->symbols[res->symbol_count];
    memset(sym, 0, sizeof(*sym));
    sym->name = copy;
    sym->detail = detail;
    sym->kind = kind;
    sym->scope = scope;
    sym->parent = parent;
    sym->range.start_line = sym->range.end_line = P->line;
    sym->range.start_col = col_of(P, name.start);
    sym->range.end_col = col_of(P, end);
    sym->selection.start_line = sym->selection.end_line = P->line;
    sym->selection.start_col = col_of(P, name.start);
    sym->selection.end_col = col_of(P, name.end);
    return res->symbol_count++;
}

/**
 * @brief Define a name of the current line, written there or made from what
 *        is written there.
 *
 * @param P The parser.
 * @param text The name.
 * @param len Its length.
 * @param name Where it stands, or the operand it was made from.
 * @param kind What it is.
 * @param L The line.
 * @return The index of its symbol, or -1.
 */
static long define_name(Parser *P, const char *text, size_t len, Span name, DefKind kind, const Line *L)
{
    static const int symbol_kinds[] = {
        SCOUT_SYMBOL_LABEL, SCOUT_SYMBOL_CONSTANT, SCOUT_SYMBOL_VARIABLE, SCOUT_SYMBOL_FIELD, SCOUT_SYMBOL_MACRO,
        SCOUT_SYMBOL_STRUCT
    };
    int local = is_local_name(text, len);
    int plain = P->if_depth == 0 && P->rept_depth == 0;
    long scope = local ? P->scope : 0, symbol, parent = local ? P->last_label : -1;
    unsigned long h;
    Def *d, *n;

    if (len == 0 || memchr(text, '\\', len) != NULL) return -1;
    d = find_def(P, text, len, scope);
    if (d && kind == DEF_SET && d->kind == DEF_SET) return -1;
    if (d && memcmp(d->text, text, len) == 0) {
        if (plain && d->plain && kind != DEF_SET && d->kind != DEF_SET)
            add_diagnostic(P, SCOUT_SEVERITY_ERROR, P->line, col_of(P, name.start), col_of(P, name.end),
                           local ? "local label '%s' is defined twice" : "'%s' is defined twice", text, len);
        return -1;
    }
    if (kind == DEF_FIELD && L->word == NULL) parent = P->structure;
    symbol = add_symbol(P, text, len, name, symbol_kinds[kind],
                        local ? ASM_SCOPE_LOCAL : P->is_include ? ASM_SCOPE_GLOBAL : ASM_SCOPE_FILE, parent, L);
    if (!grow(P, (void **)&P->defs, P->def_count, &P->def_cap, sizeof(Def))) return symbol;
    h = hash_name(text, len) & P->mask;
    n = &P->defs[P->def_count];
    /* a name made from operands is not in the source: its symbol keeps it */
    n->text = symbol >= 0 ? P->res->symbols[symbol].name : text;
    n->pattern.start = n->pattern.end = 0;
    n->len = (long)len;
    n->scope = scope;
    n->next = P->slots[h];
    n->symbol = symbol;
    n->kind = kind;
    n->plain = plain;
    n->line = P->line;
    P->slots[h] = P->def_count++;
    return symbol;
}

/**
 * @brief Define a name written on the current line.
 *
 * @param P The parser.
 * @param name The name.
 * @param kind What it is.
 * @param L The line.
 * @return The index of its symbol, or -1.
 */
static long define(Parser *P, Span name, DefKind kind, const Line *L)
{
    return define_name(P, P->src + name.start, name.end - name.start, name, kind, L);
}

/**
 * @brief Replace the detail of a symbol by two texts with a blank between.
 *
 * @param P The parser.
 * @param symbol The symbol, or -1 for none.
 * @param first The first text.
 * @param first_len Its length.
 * @param second The second text.
 * @param second_len Its length; 0 for none.
 */
static void set_detail(Parser *P, long symbol, const char *first, size_t first_len, const char *second, size_t second_len)
{
    AsmSymbol *sym;
    char *text;
    if (symbol < 0) return;
    sym = &P->res->symbols[symbol];
    if (first_len + second_len + 1 > MAX_DETAIL) return;
    if ((text = (char *)malloc(first_len + second_len + 2)) == NULL) {
        P->res->out_of_memory = 1;
        return;
    }
    memcpy(text, first, first_len);
    text[first_len] = '\0';
    if (second_len) {
        text[first_len] = ' ';
        memcpy(text + first_len + 1, second, second_len);
        text[first_len + 1 + second_len] = '\0';
    }
    free(sym->detail);
    sym->detail = text;
}

/**
 * @brief Find an operand of the current line.
 *
 * @param P The parser.
 * @param L The line.
 * @param index Its number, from 0.
 * @param[out] op The operand, without blanks around it.
 * @return Non-zero if it exists and is not empty.
 */
static int nth_operand(const Parser *P, const Line *L, int index, Span *op)
{
    const char *s = P->src;
    size_t i = L->operands.start, e = L->operands.end;
    int depth = 0;
    for (;;) {
        size_t start = i, end;
        while (i < e && (s[i] != ',' || depth > 0)) {
            if (s[i] == '(') depth++;
            else if (s[i] == ')' && depth > 0) depth--;
            i++;
        }
        if (index-- == 0) {
            end = i;
            while (start < end && is_blank(s[start])) start++;
            while (end > start && is_blank(s[end - 1])) end--;
            op->start = start;
            op->end = end;
            return end > start;
        }
        if (i >= e) return 0;
        i++;
    }
}

/**
 * @brief Find a macro of the NDK.
 * @param P The parser.
 * @param name Its name as written (any case).
 * @return The macro, or NULL.
 */
static const NdkMacro *find_ndk_macro(const Parser *P, Span name)
{
    size_t len = name.end - name.start, k;
    long i;
    for (i = 0; i < (long)(sizeof(ndk_macros) / sizeof(ndk_macros[0])); i++) {
        const char *n = ndk_macros[i].name;
        for (k = 0; k < len && n[k] && lower((unsigned char)n[k]) == lower((unsigned char)P->src[name.start + k]); k++) {}
        if (k == len && n[k] == '\0') return &ndk_macros[i];
    }
    return NULL;
}

/**
 * @brief Define a name made of up to three parts of text.
 *
 * @param P The parser.
 * @param a The first part.
 * @param a_len Its length.
 * @param b The second part.
 * @param b_len Its length.
 * @param c The third part.
 * @param c_len Its length.
 * @param where The operand it was made from.
 * @param L The line.
 * @return The index of its symbol, or -1.
 */
static long define_made(Parser *P, const char *a, size_t a_len, const char *b, size_t b_len, const char *c, size_t c_len,
                        Span where, const Line *L)
{
    char name[MAX_MADE_NAME];
    if (a_len + b_len + c_len > sizeof(name)) return -1;
    memcpy(name, a, a_len);
    memcpy(name + a_len, b, b_len);
    memcpy(name + a_len + b_len, c, c_len);
    return define_name(P, name, a_len + b_len + c_len, where, DEF_CONST, L);
}

/**
 * @brief Define what a call of a macro on the current line defines: by a
 *        macro of this source with a name pattern, or by one of the NDK.
 * @param P The parser.
 * @param L The line.
 */
static void note_macro_call(Parser *P, const Line *L)
{
    const char *s = P->src;
    const char *mnem = s + L->mnemonic.start;
    size_t mnem_len = L->mnemonic.end - L->mnemonic.start;
    const Def *d = find_def(P, mnem, mnem_len, 0);
    const NdkMacro *ndk;
    Span a, b, c;
    long symbol;

    if (d && d->kind == DEF_MACRO) {
        /* the pattern is prefix \1 suffix; the macro's text stays in the source */
        if (d->pattern.end > d->pattern.start && nth_operand(P, L, 0, &a)) {
            const char *hole = (const char *)memchr(s + d->pattern.start, '\\', d->pattern.end - d->pattern.start);
            size_t head = (size_t)(hole - (s + d->pattern.start));
            symbol = define_made(P, s + d->pattern.start, head, s + a.start, a.end - a.start, hole + 2,
                                 d->pattern.end - d->pattern.start - head - 2, a, L);
            set_detail(P, symbol, mnem, mnem_len, NULL, 0);
        }
        return;
    }
    if ((ndk = find_ndk_macro(P, L->mnemonic)) == NULL || !nth_operand(P, L, 0, &a)) return;
    switch ((NdkAction)ndk->action) {
    case NDK_STRUCTURE:
        symbol = define(P, a, DEF_STRUCT, L);
        if (symbol >= 0) P->structure = symbol;
        set_detail(P, symbol, mnem, mnem_len, NULL, 0);
        break;
    case NDK_FIELD:
    case NDK_STRUCT:
        symbol = define(P, a, DEF_FIELD, L);
        if (ndk->action == NDK_STRUCT && nth_operand(P, L, 1, &b)) set_detail(P, symbol, mnem, mnem_len, s + b.start, b.end - b.start);
        else set_detail(P, symbol, mnem, mnem_len, NULL, 0);
        if (symbol >= 0 && P->structure >= 0) {
            AsmSymbol *st = &P->res->symbols[P->structure];
            st->range.end_line = P->line;
            st->range.end_col = col_of(P, L->operands.end);
        }
        break;
    case NDK_CONSTANT:
        symbol = define(P, a, DEF_CONST, L);
        set_detail(P, symbol, mnem, mnem_len, NULL, 0);
        break;
    case NDK_FUNCDEF:
        symbol = define_made(P, "_LVO", 4, s + a.start, a.end - a.start, "", 0, a, L);
        set_detail(P, symbol, mnem, mnem_len, NULL, 0);
        break;
    case NDK_BITDEF:
        if (!nth_operand(P, L, 1, &b) || !nth_operand(P, L, 2, &c)) break;
        symbol = define_made(P, s + a.start, a.end - a.start, "B_", 2, s + b.start, b.end - b.start, b, L);
        set_detail(P, symbol, "bit", 3, s + c.start, c.end - c.start);
        symbol = define_made(P, s + a.start, a.end - a.start, "F_", 2, s + b.start, b.end - b.start, b, L);
        set_detail(P, symbol, "1 <<", 4, s + c.start, c.end - c.start);
        break;
    default:
        break;
    }
}

/**
 * @brief Remember the name a macro's body defines, if it is made with \\1.
 * @param P The parser.
 * @param L A line of the body.
 */
static void note_macro_pattern(Parser *P, const Line *L)
{
    const char *s = P->src;
    const char *hole;
    Def *d;
    size_t len = L->label.end - L->label.start, head;

    if (P->macro_depth != 1 || P->macro_def < 0 || !L->word || L->word->op != OP_EQU || len < 2) return;
    d = &P->defs[P->macro_def];
    if (d->pattern.end > d->pattern.start) return;
    hole = (const char *)memchr(s + L->label.start, '\\', len);
    if (!hole) return;
    head = (size_t)(hole - (s + L->label.start));
    if (head + 2 > len || hole[1] != '1' || memchr(hole + 2, '\\', len - head - 2) != NULL) return;
    d->pattern = L->label;
}

/**
 * @brief Remember where a block starts.
 * @param P The parser.
 * @param stack The stack of blocks.
 * @param depth Blocks on it.
 * @param cap The room.
 * @param L The line.
 */
static void push_block(Parser *P, Block **stack, long *depth, long *cap, const Line *L)
{
    Block *b;
    if (!grow(P, (void **)stack, *depth, cap, sizeof(Block))) return;
    b = &(*stack)[(*depth)++];
    b->line = P->line;
    b->col = col_of(P, L->mnemonic.start);
    b->end_col = col_of(P, L->mnemonic.end);
    b->seen_else = 0;
}

/**
 * @brief The span of the first name in the operands.
 * @param P The parser.
 * @param L The line.
 * @return The name; empty if there is none.
 */
static Span first_operand_name(const Parser *P, const Line *L)
{
    Span n;
    n.start = L->operands.start;
    n.end = read_name(P, n.start, L->operands.end);
    return n;
}

/**
 * @brief Note an INCLUDE of the current line.
 * @param P The parser.
 * @param L The line.
 */
static void note_include(Parser *P, const Line *L)
{
    AsmResult *res = P->res;
    const char *s = P->src;
    Span t = L->operands;
    AsmReference *r;
    char *target;

    P->includes = 1;
    if (t.end <= t.start) return;
    if (s[t.start] == '"' || s[t.start] == '\'') {
        char q = s[t.start];
        const char *close = (const char *)memchr(s + t.start + 1, q, t.end - t.start - 1);
        if (!close) return;
        t.start++;
        t.end = (size_t)(close - s);
    }
    if (t.end <= t.start || has_parameter(P, t)) return;
    if (!grow(P, (void **)&res->references, res->reference_count, &P->reference_cap, sizeof(AsmReference))) return;
    if ((target = dup_text(P, s + t.start, t.end - t.start)) == NULL) return;
    r = &res->references[res->reference_count++];
    r->target = target;
    r->range.start_line = r->range.end_line = P->line;
    r->range.start_col = col_of(P, t.start);
    r->range.end_col = col_of(P, t.end);
}

/**
 * @brief Follow the conditionals and REPTs on the current line.
 *
 * The first pass also reports what does not match; the second only keeps
 * count, as the scopes depend on it.
 *
 * @param P The parser.
 * @param L The line.
 */
static void track_blocks(Parser *P, const Line *L)
{
    int first = !P->second_pass;
    switch (L->word ? L->word->op : OP_NONE) {
    case OP_IF:
        if (first) push_block(P, &P->ifs, &P->if_depth, &P->if_cap, L);
        else P->if_depth++;
        break;
    case OP_ELSE:
    case OP_ELIF:
        if (!first) break;
        if (P->if_depth == 0) {
            diag_span(P, SCOUT_SEVERITY_ERROR, L->mnemonic, "%s without a conditional");
        } else if (P->ifs[P->if_depth - 1].seen_else) {
            diag_span(P, SCOUT_SEVERITY_ERROR, L->mnemonic, "%s after the ELSE of this conditional");
        } else if (L->word->op == OP_ELSE) {
            P->ifs[P->if_depth - 1].seen_else = 1;
        }
        break;
    case OP_ENDC:
        if (P->if_depth > 0) P->if_depth--;
        else if (first) diag_span(P, SCOUT_SEVERITY_ERROR, L->mnemonic, "%s without a conditional");
        break;
    case OP_REPT:
        if (first) push_block(P, &P->repts, &P->rept_depth, &P->rept_cap, L);
        else P->rept_depth++;
        break;
    case OP_ENDR:
        if (P->rept_depth > 0) P->rept_depth--;
        else if (first) diag_span(P, SCOUT_SEVERITY_ERROR, L->mnemonic, "%s without REPT");
        break;
    default:
        break;
    }
}

/**
 * @brief Tell whether a global label on this line starts a new part.
 * @param P The parser.
 * @return Non-zero outside conditionals and REPT.
 */
static int starts_scope(const Parser *P)
{
    return P->if_depth == 0 && P->rept_depth == 0;
}

/**
 * @brief The first pass: what is defined, and the block structure.
 * @param P The parser.
 */
static void first_pass(Parser *P)
{
    long lines = 0;
    Line L;

    rewind_source(P);
    while (!P->ended && next_line(P)) {
        int op;
        if ((++lines % STOP_INTERVAL) == 0 && asm_parser_should_stop && asm_parser_should_stop()) {
            P->res->stopped = 1;
            return;
        }
        split_line(P, &L);
        op = L.word ? L.word->op : OP_NONE;
        if (P->in_rem) {
            if (op == OP_EREM) P->in_rem = 0;
            continue;
        }
        if (P->macro_depth > 0) {
            if (op == OP_MACRO) P->macro_depth++;
            else if (op == OP_ENDM && --P->macro_depth == 0 && P->macro_symbol >= 0) {
                AsmSymbol *m = &P->res->symbols[P->macro_symbol];
                m->range.end_line = P->line;
                m->range.end_col = col_of(P, L.mnemonic.end);
            } else {
                note_macro_pattern(P, &L);
            }
            continue;
        }
        if (op == OP_REM) {
            P->in_rem = 1;
            continue;
        }
        track_blocks(P, &L);
        if (op == OP_INCLUDE) note_include(P, &L);
        if (op == OP_ENDM) diag_span(P, SCOUT_SEVERITY_ERROR, L.mnemonic, "%s without MACRO");
        if (op == OP_MACRO) {
            Span name = L.label;
            if (name.end <= name.start) name = first_operand_name(P, &L);
            P->macro_depth = 1;
            P->macro_symbol = -1;
            P->macro_open.line = P->line;
            P->macro_open.col = col_of(P, L.mnemonic.start);
            P->macro_open.end_col = col_of(P, L.mnemonic.end);
            if (name.end > name.start) {
                long before = P->def_count;
                P->macro_symbol = define(P, name, DEF_MACRO, &L);
                if (P->def_count > before) P->macro_def = before;
            }
            continue;
        }
        if (L.label.end > L.label.start) {
            if (op == OP_EQU) define(P, L.label, DEF_CONST, &L);
            else if (op == OP_SET) define(P, L.label, DEF_SET, &L);
            else if (op == OP_RS) define(P, L.label, DEF_FIELD, &L);
            else {
                int local = is_local(P, L.label.start, L.label.end);
                long symbol;
                if (!local && starts_scope(P)) P->scope++;
                symbol = define(P, L.label, DEF_LABEL, &L);
                if (!local && symbol >= 0) P->last_label = symbol;
            }
        }
        if (!L.word && L.mnemonic.end > L.mnemonic.start && !has_parameter(P, L.mnemonic)) note_macro_call(P, &L);
        if (op == OP_END && P->if_depth == 0 && P->rept_depth == 0) P->ended = 1;
    }
    {
        long i;
        for (i = 0; i < P->if_depth; i++)
            add_diagnostic(P, SCOUT_SEVERITY_ERROR, P->ifs[i].line, P->ifs[i].col, P->ifs[i].end_col, "conditional without ENDC", NULL, 0);
        for (i = 0; i < P->rept_depth; i++)
            add_diagnostic(P, SCOUT_SEVERITY_ERROR, P->repts[i].line, P->repts[i].col, P->repts[i].end_col, "REPT without ENDR", NULL, 0);
        if (P->macro_depth > 0)
            add_diagnostic(P, SCOUT_SEVERITY_ERROR, P->macro_open.line, P->macro_open.col, P->macro_open.end_col, "MACRO without ENDM", NULL, 0);
    }
}

/**
 * @brief Make tokens for the macro parameters in a part of a line.
 * @param P The parser.
 * @param s The part.
 */
static void parameter_tokens(Parser *P, Span s)
{
    size_t i = s.start;
    while (i < s.end) {
        if (P->src[i] == '\\') {
            size_t k = parameter_end(P, i, s.end);
            add_token(P, i, k, SCOUT_TOKEN_PARAMETER);
            i = k;
        } else {
            i++;
        }
    }
}

/**
 * @brief The kind of token a definition is shown with.
 * @param d The definition.
 * @return Its SCOUT_TOKEN_ kind.
 */
static long token_of(const Def *d)
{
    switch (d->kind) {
    case DEF_CONST:
    case DEF_FIELD:
    case DEF_SET:
        return SCOUT_TOKEN_CONSTANT;
    case DEF_MACRO:
        return SCOUT_TOKEN_MACRO;
    case DEF_STRUCT:
        return SCOUT_TOKEN_TYPE;
    default:
        return SCOUT_TOKEN_PARAMETER;
    }
}

/**
 * @brief Look at a name used in the operands.
 *
 * @param P The parser.
 * @param start The name.
 * @param end The offset after it.
 * @param check Non-zero to report a local label that does not exist.
 * @param exporting Non-zero if the line is an XDEF.
 */
static void use_name(Parser *P, size_t start, size_t end, int check, int exporting)
{
    const char *text = P->src + start;
    size_t len = end - start;
    Def *d;
    if (is_local(P, start, end)) {
        d = find_def(P, text, len, P->scope);
        if (d) {
            add_token(P, start, end, SCOUT_TOKEN_PARAMETER);
        } else if (check) {
            Span s;
            s.start = start;
            s.end = end;
            diag_span(P, SCOUT_SEVERITY_ERROR, s, "local label '%s' is not defined here");
        }
        return;
    }
    d = find_def(P, text, len, 0);
    if (!d) return;
    add_token(P, start, end, token_of(d));
    if (exporting && d->symbol >= 0) P->res->symbols[d->symbol].scope = ASM_SCOPE_GLOBAL;
}

/**
 * @brief Go through the operands of a line: tokens, and checks.
 *
 * @param P The parser.
 * @param L The line.
 * @param check Non-zero to report mistakes.
 * @param exporting Non-zero if the line is an XDEF.
 */
static void scan_operands(Parser *P, const Line *L, int check, int exporting)
{
    const char *s = P->src;
    size_t i = L->operands.start, e = L->operands.end;
    int depth = 0, backslash = 0;

    while (i < e) {
        char c = s[i];
        if (c == '\\') {
            size_t k = parameter_end(P, i, e);
            backslash = 1;
            if (P->macro_depth > 0) add_token(P, i, k, SCOUT_TOKEN_PARAMETER);
            i = k;
        } else if (c == '\'' || c == '"') {
            size_t q = i + 1;
            Span body;
            while (q < e && s[q] != c) {
                if (s[q] == '\\') backslash = 1;
                q++;
            }
            body.start = i + 1;
            body.end = q;
            if (P->macro_depth > 0) parameter_tokens(P, body);
            if (q >= e && check && !backslash) {
                Span q_span;
                q_span.start = i;
                q_span.end = i + 1;
                diag_span(P, SCOUT_SEVERITY_WARNING, q_span, "string is not closed");
            }
            i = q < e ? q + 1 : e;
        } else if (c == '(') {
            depth++;
            i++;
        } else if (c == ')') {
            if (depth > 0) depth--;
            else if (check) {
                Span p;
                p.start = i;
                p.end = i + 1;
                diag_span(P, SCOUT_SEVERITY_ERROR, p, "')' without '('");
            }
            i++;
        } else if (c == '$') {
            i++;
            while (i < e && is_name_char(s[i])) i++;
        } else if (c == '%') {
            if (i + 1 < e && s[i + 1] != '\0' && strchr("adsfp", lower((unsigned char)s[i + 1])) != NULL
                && (i == L->operands.start || !is_name_char(s[i - 1]))) P->gnu = 1;
            i++;
            while (i < e && (s[i] == '0' || s[i] == '1')) i++;
        } else if (c == '@') {
            i++;
            while (i < e && s[i] >= '0' && s[i] <= '7') i++;
        } else if (c >= '0' && c <= '9') {
            size_t k = i;
            while (k < e && is_name_char(s[k])) k++;
            if (s[k - 1] == '$') use_name(P, i, k, check, 0);
            i = k;
        } else if (c == '.' && i > L->operands.start && (is_name_char(s[i - 1]) || s[i - 1] == ')')
                   && i + 1 < e && s[i + 1] != '\0' && strchr("bwlsdxpqBWLSDXPQ", s[i + 1]) != NULL
                   && (i + 2 >= e || !is_name_char(s[i + 2]))) {
            i += 2;     /* the size of an operand */
        } else if (c == '.' || c == '_' || (unsigned char)c >= 0x80 || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) {
            size_t k = i + 1;
            while (k < e && is_name_char(s[k])) k++;
            if (c != '.' || k > i + 1) use_name(P, i, k, check, exporting);
            i = k;
        } else {
            i++;
        }
    }
    if (check && depth > 0) diag_span(P, SCOUT_SEVERITY_ERROR, L->operands, "')' is missing in '%s'");
}

/**
 * @brief Report a size the instruction does not have.
 * @param P The parser.
 * @param L The line.
 */
static void check_size(Parser *P, const Line *L)
{
    const char *s = P->src;
    int bit = 0;
    if (!L->word || !(L->word->flags & F_INSN) || !L->word->sizes || L->size.end <= L->size.start) return;
    if (has_parameter(P, L->size)) return;
    if (L->size.end - L->size.start == 1) {
        switch (lower((unsigned char)s[L->size.start])) {
        case 'b': bit = S_B; break;
        case 'w': bit = S_W; break;
        case 'l': bit = S_L; break;
        case 's': bit = S_S; break;
        default: break;
        }
    }
    if (!(bit & L->word->sizes)) diag_span(P, SCOUT_SEVERITY_ERROR, L->size, "this instruction has no size '%s'");
}

/**
 * @brief The second pass over the lines: tokens, and what depends on all
 *        names being known.
 * @param P The parser.
 */
static void second_pass(Parser *P)
{
    long lines = 0;
    Line L;

    rewind_source(P);
    P->second_pass = 1;
    while (!P->ended && next_line(P)) {
        int op, in_body;
        if ((++lines % STOP_INTERVAL) == 0 && asm_parser_should_stop && asm_parser_should_stop()) {
            P->res->stopped = 1;
            return;
        }
        split_line(P, &L);
        op = L.word ? L.word->op : OP_NONE;
        if (P->in_rem) {
            if (op == OP_EREM) P->in_rem = 0;
            continue;
        }
        if (P->macro_depth > 0) {
            if (op == OP_MACRO) P->macro_depth++;
            else if (op == OP_ENDM && --P->macro_depth == 0) continue;
        }
        in_body = P->macro_depth > 0;
        if (op == OP_REM && !in_body) {
            P->in_rem = 1;
            continue;
        }
        if (!in_body) track_blocks(P, &L);

        /* the label */
        if (L.label.end > L.label.start) {
            if (in_body) {
                parameter_tokens(P, L.label);
            } else {
                int local = is_local(P, L.label.start, L.label.end);
                const Def *d;
                if (!local && op != OP_EQU && op != OP_SET && op != OP_RS && op != OP_MACRO && starts_scope(P))
                    P->scope++;
                d = find_def(P, P->src + L.label.start, L.label.end - L.label.start, local ? P->scope : 0);
                if (d) add_token(P, L.label.start, L.label.end, token_of(d));
            }
        }

        /* the mnemonic */
        if (L.mnemonic.end > L.mnemonic.start) {
            if (in_body) {
                parameter_tokens(P, L.mnemonic);
                parameter_tokens(P, L.size);
            } else if (L.word) {
                check_size(P, &L);
            } else if (!has_parameter(P, L.mnemonic)) {
                const Def *d = find_def(P, P->src + L.mnemonic.start, L.mnemonic.end - L.mnemonic.start, 0);
                if ((d && d->kind == DEF_MACRO) || find_ndk_macro(P, L.mnemonic))
                    add_token(P, L.mnemonic.start, L.mnemonic.end, SCOUT_TOKEN_MACRO);
                else if (!P->includes) diag_span(P, SCOUT_SEVERITY_HINT, L.mnemonic, "unknown instruction or macro '%s'");
            }
        }

        /* the operands */
        if (op == OP_MACRO && !in_body) {
            if (L.label.end <= L.label.start) {
                Span name = first_operand_name(P, &L);
                add_token(P, name.start, name.end, SCOUT_TOKEN_MACRO);
            }
            P->macro_depth = 1;
        } else if (L.operands.end > L.operands.start && op != OP_INCLUDE) {
            int free_text = L.word && (L.word->flags & F_FREE);
            if (!free_text || in_body || op == OP_XDEF)
                scan_operands(P, &L, !in_body && !free_text, op == OP_XDEF);
        }
        if (op == OP_END && !in_body && P->if_depth == 0 && P->rept_depth == 0) P->ended = 1;
    }
}

/**
 * @brief Order two diagnostics by place.
 * @param a The first.
 * @param b The second.
 * @return Negative, zero or positive as for qsort().
 */
static int compare_diagnostics(const void *a, const void *b)
{
    const AsmDiagnostic *x = (const AsmDiagnostic *)a, *y = (const AsmDiagnostic *)b;
    if (x->range.start_line != y->range.start_line) return x->range.start_line < y->range.start_line ? -1 : 1;
    if (x->range.start_col != y->range.start_col) return x->range.start_col < y->range.start_col ? -1 : 1;
    return 0;
}

AsmResult *asm_parser_analyze(const char *source, size_t length, int is_include)
{
    Parser P;
    unsigned long size = 256, lines = 1;
    size_t i;
    long k;

    make_word_table();
    memset(&P, 0, sizeof(P));
    P.res = (AsmResult *)calloc(1, sizeof(AsmResult));
    if (!P.res) return NULL;
    P.src = source;
    P.length = length;
    P.is_include = is_include;
    for (i = 0; i < length; i++)
        if (source[i] == '\n') lines++;
    while (size < lines && size < 65536UL) size *= 2;
    P.mask = size - 1;
    P.slots = (long *)malloc(size * sizeof(long));
    if (!P.slots) {
        free(P.res);
        return NULL;
    }
    for (k = 0; k < (long)size; k++) P.slots[k] = -1;

    first_pass(&P);
    if (!P.res->stopped) second_pass(&P);

    free(P.slots);
    free(P.defs);
    free(P.ifs);
    free(P.repts);
    if (P.gnu) {
        for (k = 0; k < P.res->diagnostic_count; k++) free(P.res->diagnostics[k].message);
        P.res->diagnostic_count = 0;
    }
    if (P.res->diagnostic_count > 1)
        qsort(P.res->diagnostics, (size_t)P.res->diagnostic_count, sizeof(AsmDiagnostic), compare_diagnostics);
    return P.res;
}

void asm_result_free(AsmResult *res)
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
