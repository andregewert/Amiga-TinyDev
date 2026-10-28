# The Scout protocol

How Scout talks to its language parsers, version 1. This document is for
anyone who writes a parser: it describes everything a parser has to know,
and a parser needs nothing else from Scout or from an editor. What Scout
does with the results is described in [Scout.md](Scout.md).

> **Status: draft.** The C parser in `src/parsers/` still speaks an
> earlier form (`textDocument/*` methods, one method per run).

## Contents

1. [Overview](#1-overview)
2. [Transport](#2-transport)
3. [Conventions](#3-conventions)
4. [scout/describe](#4-scoutdescribe)
5. [Lexical rules](#5-lexical-rules)
6. [scout/analyze](#6-scoutanalyze)
7. [Kinds](#7-kinds)
8. [Errors](#8-errors)
9. [What a parser must do](#9-what-a-parser-must-do)
10. [Trying a parser by hand](#10-trying-a-parser-by-hand)
11. [Versions and extensions](#11-versions-and-extensions)

## 1. Overview

A parser is a CLI program for one or more languages. Scout starts it,
writes requests to its STDIN and reads the answers from its STDOUT. There
are two methods:

- `scout/describe`: what the parser is, which languages and file
  extensions it handles, what it can deliver, and the lexical rules that
  editors use for highlighting. Scout asks once, when it finds the parser.
- `scout/analyze`: everything about one source file at once: symbols,
  diagnostics, references to other files and highlighting tokens.

A parser knows nothing about projects, editors, caches or other files.
It reads the file it is given and reports what it finds in it.

## 2. Transport

- **Start:** Scout runs the program without arguments, with STDIN and
  STDOUT redirected and no console window. The current directory is
  undefined; all paths in requests are absolute.
- **Messages:** one JSON object per line (newline-delimited JSON), ended
  by LF, in the form of JSON-RPC 2.0. A line holds exactly one message;
  JSON written by a parser must therefore not contain line breaks (inside
  strings they are written as `\n`).
- **Order:** Scout writes one or more requests; the parser answers each,
  in the order received, with one line. A run may get a single request
  (an editor's request) or several (indexing a project in the background).
- **End:** At end of file on STDIN the parser exits with return code 0.
- **No notifications:** every message from Scout has an `id`, and every
  answer carries it back. A parser never sends anything unasked.
- **STDERR** is not part of the protocol; Scout may discard or log it.
- **Character set:** ISO-8859-1. Strings from the source (names, details)
  are passed on byte for byte as they are in the file; only `"`, `\` and
  control characters are escaped.

Request:

```json
{"jsonrpc":"2.0","id":1,"method":"scout/analyze","params":{"path":"Work:Hello/hello.c"}}
```

Answer:

```json
{"jsonrpc":"2.0","id":1,"result":{"language":"c","symbols":[]}}
```

or, on failure (see [Errors](#8-errors)):

```json
{"jsonrpc":"2.0","id":1,"error":{"code":-32001,"message":"cannot open Work:Hello/hello.c"}}
```

## 3. Conventions

- **Positions** are `[line, column]`, both counted from 1. Columns count
  bytes: a tab is one column, as is every Latin-1 character.
- **Ranges** are `{"start":[line,col],"end":[line,col]}`; the end is the
  position after the last character (exclusive). A range at the end of a
  line ends at column length + 1.
- **Line ends:** LF, CR and CRLF each end a line.
- **Paths** are AmigaDOS paths (`Work:Hello/hello.c`), not URIs.
- **Unknown fields** are ignored, by Scout and by parsers. Optional fields
  may be left out; `null` means the same as left out.

## 4. scout/describe

Parameters: none (`{}` or left out).

Result:

```json
{
  "protocol": 1,
  "name": "c_parser",
  "version": "1.0",
  "languages": [
    {
      "id": "c",
      "name": "C",
      "extensions": [".c", ".h"],
      "syntax": { ... }
    }
  ],
  "capabilities": {
    "symbols": true,
    "diagnostics": true,
    "references": true,
    "tokens": true
  }
}
```

| Field | Meaning |
|---|---|
| `protocol` | the protocol version the parser speaks (1) |
| `name`, `version` | shown by Scout's `STATUS` |
| `languages` | at least one |
| `languages[].id` | identifier of the language: lower case letters, digits, `-`; shared by all parsers and clients (`c`, `amigados`, `pascal`, `e`, `basic`, `arexx`, ...) |
| `languages[].name` | name shown to users |
| `languages[].extensions` | file extensions with their dot, lower case; matched without regard to case |
| `languages[].syntax` | the lexical rules (next section); optional, without them editors do not highlight the language |
| `capabilities` | what `scout/analyze` can deliver; a missing entry means `false` |

## 5. Lexical rules

The rules describe how a line is split into comments, strings, numbers,
keywords and normal text. Editors apply them with a generic scanner while
the user types, without asking Scout, so they must be simple enough to
run for every line drawn. Example for C:

```json
{
  "caseSensitive": true,
  "identifier": { "start": "A-Za-z_", "part": "A-Za-z0-9_" },
  "lineComments": ["//"],
  "blockComments": [ { "open": "/*", "close": "*/", "nested": false } ],
  "strings": [
    { "open": "\"", "close": "\"", "escape": "\\", "multiline": false },
    { "open": "'", "close": "'", "escape": "\\", "multiline": false }
  ],
  "numbers": { "prefixes": ["0x", "0X"] },
  "preprocessor": { "prefix": "#", "continuation": "\\" },
  "keywords": {
    "keyword": ["if", "else", "for", "while", "do", "switch", "case", "default",
                "break", "continue", "return", "goto", "sizeof", "typedef",
                "struct", "union", "enum", "static", "extern", "const", "volatile",
                "register", "auto", "inline", "restrict"],
    "type": ["void", "char", "short", "int", "long", "float", "double",
             "signed", "unsigned", "_Bool"],
    "constant": ["NULL", "TRUE", "FALSE"]
  }
}
```

| Field | Meaning |
|---|---|
| `caseSensitive` | whether keywords are matched with case (default `true`); Latin-1 letters fold too |
| `identifier.start`, `.part` | character classes of the first and further characters of a word: characters and ranges `a-z`; `-` at the start or end stands for itself; Latin-1 characters as `ä` in JSON. Default `A-Za-z_` and `A-Za-z0-9_` |
| `lineComments` | strings that start a comment to the end of the line |
| `blockComments` | `open`/`close` pairs; `nested`: whether an `open` inside counts (Pascal, E) |
| `strings` | `open`/`close` pairs; `escape`: the character that makes the next one part of the string (none if left out); `multiline`: whether the string may go on to the next line (otherwise it ends at the line end) |
| `numbers.prefixes` | strings that start a number when a digit or a letter `a-f`/`A-F` follows (`0x`, `$`, `%`); a digit always starts a number unless it is part of a word |
| `preprocessor.prefix` | a line whose first non-blank characters are this string is a preprocessor line as a whole (comments and strings in it still count) |
| `preprocessor.continuation` | a line ending in this character continues the preprocessor line on the next |
| `keywords` | words by class (see [Kinds](#7-kinds)): `keyword`, `type`, `constant`, `builtin` |

How a scanner applies them, from the start of a line:

1. Inside a block comment or multi-line string left open by the previous
   line, look for its end first.
2. Otherwise, at each position try, in this order: line comment, block
   comment, string, number, word. Among the delimiters that match, the
   longest wins.
3. A number runs over the following identifier characters and `.`.
4. A word that is in a keyword list gets that class; others are normal
   text (semantic tokens may refine them).
5. Everything else is normal text.

The state carried from line to line is "inside which block comment (and
how deeply nested) or which string". Rules that need more (heredocs,
context-dependent keywords, indentation) cannot be expressed in version 1;
see [Versions and extensions](#11-versions-and-extensions).

## 6. scout/analyze

Parameters:

```json
{
  "path": "Work:Hello/hello.c",
  "text": "T:TinyDev-Snapshot-3.c",
  "language": "c",
  "want": ["symbols", "diagnostics", "references", "tokens"]
}
```

| Field | Meaning |
|---|---|
| `path` | the source file's real path; used in messages and as the base of relative references |
| `text` | optional: a file with the text to analyse instead of `path` (an editor's unsaved state). The parser reads it, but reports as if it were `path` |
| `language` | the language to analyse as, one of the parser's `languages[].id` |
| `want` | optional: which parts to deliver; default all the parser can. Parts not wanted may be left out of the result, or delivered anyway |

Result:

```json
{
  "language": "c",
  "symbols": [
    {"name": "Point", "kind": 23, "scope": "global",
     "range": {"start": [3, 1], "end": [6, 2]},
     "selection": {"start": [3, 16], "end": [3, 21]}},
    {"name": "x", "kind": 8, "scope": "local", "parent": 0,
     "range": {"start": [4, 5], "end": [4, 11]},
     "selection": {"start": [4, 9], "end": [4, 10]}, "detail": "int"},
    {"name": "main", "kind": 12, "scope": "global",
     "range": {"start": [8, 1], "end": [12, 2]},
     "selection": {"start": [8, 5], "end": [8, 9]}, "detail": "int main(void)"}
  ],
  "diagnostics": [
    {"severity": 1, "message": "missing ';'",
     "range": {"start": [10, 14], "end": [10, 15]}}
  ],
  "references": [
    {"kind": "include", "target": "exec/types.h", "system": true,
     "range": {"start": [1, 10], "end": [1, 24]}}
  ],
  "tokens": [3, 16, 5, 1, 8, 5, 4, 2, 10, 5, 5, 1]
}
```

### Symbols

| Field | Meaning |
|---|---|
| `name` | the name as written |
| `kind` | symbol kind (see [Kinds](#7-kinds)) |
| `scope` | `global` (visible to other files, e.g. a non-static C function), `file` (only in this file), `local` (inside another symbol, e.g. a field or a local variable). Default `global` |
| `parent` | optional: index (from 0) in `symbols` of the symbol it belongs to (the struct of a field, the function of a local variable); it comes before |
| `range` | the whole definition (for a function: from the return type to the closing brace) |
| `selection` | the name within it (where "go to definition" puts the cursor) |
| `detail` | optional: a signature or type, shown in lists and completion |

List definitions, not uses. Declarations without a definition (a C
prototype, an `extern`) may be listed with `"declaration": true`, so that
Scout prefers the definition but can fall back to them.

### Diagnostics

| Field | Meaning |
|---|---|
| `range` | where the problem is; at least one character if possible |
| `severity` | 1 error, 2 warning, 3 information, 4 hint |
| `message` | one line of text |
| `code` | optional: a short identifier of the kind of problem (string) |

A parser reports what it can see in the one file (syntax errors,
unbalanced brackets, unknown directives), not what needs other files.

### References

Other files this one refers to, so Scout can add them to the project
index and resolve them.

| Field | Meaning |
|---|---|
| `kind` | `include` (text included, e.g. `#include`, `INCLUDE`), `import` (a module used, e.g. `uses`, `MODULE`) |
| `target` | the name as written, without quotes or brackets; Scout resolves it (relative to `path`, then along the include paths) |
| `system` | optional: `true` for a reference that is looked up in the include paths only (`<...>` in C) |
| `range` | where the reference is |

### Tokens

Highlighting tokens for identifiers whose meaning a lexical scanner cannot
know: the names of types, functions, macros and constants defined in the
file or by the language's libraries. They refine the normal text of the
lexical rules and are not needed for keywords, comments, strings or
numbers (editors ignore tokens there).

`tokens` is a flat array of integers, four per token: line, column, length
in bytes, token kind (see [Kinds](#7-kinds)). The example above holds three
tokens: `Point` in line 3 (a type), `main` in line 8 (a function) and
`Point` in line 10 (a type). Rules:

- sorted by line, then column;
- no overlaps;
- a token never spans lines (split it if needed);
- uses count, not only definitions: every place a known type name
  appears should be a token.

The flat form keeps the answer small; a file of a few thousand lines can
still produce many tokens, which is why Scout hands them to clients as
files.

## 7. Kinds

### Symbol kinds

The numbers of the Language Server Protocol, so that existing lists and
symbols can be reused:

| | | | | | |
|---|---|---|---|---|---|
| 1 file | 2 module | 3 namespace | 4 package | 5 class | 6 method |
| 7 property | 8 field | 9 constructor | 10 enum | 11 interface | 12 function |
| 13 variable | 14 constant | 15 string | 16 number | 17 boolean | 18 array |
| 19 object | 20 key | 21 null | 22 enum member | 23 struct | 24 event |
| 25 operator | 26 type parameter | | | | |

Scout adds kinds that LSP lacks from 100 on: 100 macro, 101 type alias
(`typedef`), 102 label, 103 procedure (languages that separate procedures
from functions).

### Lexical classes

The classes of the lexical rules: `keyword`, `type`, `constant`,
`builtin`, plus `comment`, `string`, `number` and `preprocessor` from the
other rules. Everything else is normal text.

### Token kinds

| Kind | Meaning |
|---|---|
| 1 | type (struct, typedef, class, ...) |
| 2 | function or procedure |
| 3 | macro |
| 4 | constant (enum member, constant macro, `CONST`) |
| 5 | variable (global or local) |
| 6 | parameter |
| 7 | field |
| 8 | label |
| 9 | module or namespace |

Kinds from 64 on are reserved for later versions; an editor treats unknown
kinds as normal text.

## 8. Errors

The codes of JSON-RPC 2.0, plus Scout's own:

| Code | Meaning |
|---|---|
| -32700 | the request is not valid JSON |
| -32600 | not a valid request |
| -32601 | unknown method |
| -32602 | wrong parameters |
| -32603 | internal error of the parser |
| -32001 | the file cannot be read |
| -32002 | language not handled by this parser |
| -32003 | out of memory |

An error answers one request; the parser carries on with the next line.
Syntax errors in the analysed source are not protocol errors: they are
diagnostics of a successful answer. A parser should deliver what it found
even in a broken file.

## 9. What a parser must do

- **Stay small and standalone:** a plain program, no libraries beyond the
  C runtime and the OS, no ixemul. Set its own stack (`__stack`); Scout
  does not know how much it needs.
- **Answer every request,** in order, with exactly one line, and flush
  STDOUT after each answer (Scout reads while the parser runs).
- **Read the source itself,** as bytes: any byte values, LF/CR/CRLF, files
  of several hundred kilobytes, very long lines, no line end at the end.
- **Stay quiet:** nothing on STDOUT but answers, no windows, no
  requesters (set `pr_WindowPtr` to -1 before opening files).
- **Keep no state between requests** that changes an answer; caching
  within a run is fine.
- **Be quick and stoppable:** answer within a few seconds on a 68030;
  check for Ctrl-C (`SIGBREAKF_CTRL_C`) in long loops and exit when it
  comes. Scout sends it on a timeout.
- **Exit with 0** at the end of STDIN, and with a non-zero code only if it
  cannot go on at all.
- **Prefer portable C** for the analysis, so it can be tested on a host
  computer with the same requests and files.

## 10. Trying a parser by hand

Write the requests into a file, one per line:

```json
{"jsonrpc":"2.0","id":1,"method":"scout/describe"}
{"jsonrpc":"2.0","id":2,"method":"scout/analyze","params":{"path":"Work:Hello/hello.c","language":"c"}}
```

and run the parser on it in a Shell:

```
c_parser <requests.json >answers.json
```

`answers.json` then holds two lines, one per request.

## 11. Versions and extensions

- `protocol` in `scout/describe` is the version a parser speaks. Scout
  uses parsers of the versions it knows and reports others in `STATUS`.
- New optional fields and methods do not change the version; both sides
  ignore what they do not know, and a method a parser lacks is answered
  with -32601.
- Planned for later, as optional methods or fields:
  - `scout/complete`: completion that depends on the context in the file
    (the fields of a struct after `->`), with a capability `complete`;
  - tokens for languages whose highlighting is not lexical (Markdown);
  - richer lexical rules (context-dependent keywords, heredocs).
