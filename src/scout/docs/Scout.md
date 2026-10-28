# Scout

Scout (`TinyDev-Scout`) analyses source code for editors on AmigaOS 3.2. It
is a commodity with an ARexx port; the analysis itself is done by small
language parsers, one program per language, which Scout runs.

This document describes what Scout is for, how its parts work together and
its ARexx interface. The protocol between Scout and its parsers is in
[Scout-Protocol.md](Scout-Protocol.md).

> **Status:** Scout and the C parser follow this design, version 1 of the
> protocol. Not done yet: watching the project with `StartNotify()`,
> dropping sessions when memory runs low, a window. TinyDev does not use
> Scout yet.

## Contents

1. [Goals](#1-goals)
2. [Parts and roles](#2-parts-and-roles)
3. [Languages and parsers](#3-languages-and-parsers)
4. [Sessions](#4-sessions)
5. [Documents and snapshots](#5-documents-and-snapshots)
6. [Analysis, cache and project index](#6-analysis-cache-and-project-index)
7. [Syntax highlighting](#7-syntax-highlighting)
8. [Concurrency and robustness](#8-concurrency-and-robustness)
9. [Temporary files and settings](#9-temporary-files-and-settings)
10. [ARexx interface](#10-arexx-interface)
11. [Writing a client](#11-writing-a-client)
12. [Limits and non-goals](#12-limits-and-non-goals)
13. [Code layout](#13-code-layout)

## 1. Goals

An editor should get what an IDE needs from source code without knowing
the language:

- syntax highlighting,
- a list of the symbols of a file or project,
- "go to definition",
- diagnostics (syntax errors and the like),
- completion.

To get there:

- **One parser per language.** A parser is a plain CLI program that talks
  over STDIN and STDOUT. It is not a library, module or plugin. It knows
  nothing about the editor or Scout's internals and normally looks at one
  source file per request. Everything about one language, including the
  rules for highlighting, comes from its parser. Adding a language means
  adding a parser; neither Scout nor the editor changes.
- **Scout is the one place for everything else.** It finds the right
  parser for a file, runs it, keeps the results, combines them into an
  index of the project and answers the editor's questions. Parsers need
  no sessions or caches of their own.
- **ARexx is the interface.** Every Amiga program can use it, and so can
  scripts. Scout answers in JSON, like the parsers.
- **Modelled on LSP, but not LSP.** Ideas and the numbering of symbol and
  diagnostic kinds come from the Language Server Protocol. Transport,
  positions, methods and the split of work differ, and LSP clients or
  servers cannot talk to Scout.

## 2. Parts and roles

```
 +--------------+   ARexx (JSON results)  +---------------+  STDIN/STDOUT  +-------------+
 | Client       | ----------------------> | Scout         | -------------> | Parser      |
 | (TinyDev,    | <---------------------- | (commodity,   | <------------- | (one per    |
 |  a script,   |                         |  TINYDEV_SCOUT)|  Scout protocol |  language)  |
 |  an editor)  |                         +---------------+                +-------------+
 +--------------+                           sessions, cache,                 reads one file,
   asks questions,                          project index,                   returns facts
   shows answers                            answers questions
```

**Parsers deliver facts; Scout answers questions.** A parser run takes one
file and returns everything about it at once: symbols (kind, place, scope),
diagnostics, references to other files (includes) and highlighting tokens.
Scout keeps these results per file and combines them into the project
index. It answers "where is X defined?" and "what could follow here?" from
that index, also across files, without running a parser again. So a parser
runs once per change of a file, and it only has to understand one file.

| | Client | Scout | Parser |
|---|---|---|---|
| Knows the project | yes | yes (from the session) | no |
| Knows the editor | yes | no | no |
| Knows the language | no | no (only the parser's description) | yes |
| Keeps state | its documents | sessions, results, index | nothing between runs |
| Runs | always | as a commodity | per request |

## 3. Languages and parsers

- Parsers live in `PROGDIR:Parsers/` (beside Scout). At start, and on
  `RESCAN`, Scout asks each program found there for its description
  (`scout/describe`, see the protocol): its name, its languages with
  their file extensions, what it can deliver, and the lexical rules for
  highlighting. Installing a parser means copying it into the drawer.
- A language has an identifier (`"c"`, `"amigados"`, `"pascal"`, ...,
  lower case). Scout picks the language of a file by its extension; a
  client may name it instead (`LANG`). If two parsers claim a language or
  an extension, the first in alphabetical order wins, and `STATUS` reports
  the conflict.
- `REGISTER` adds or overrides the parser of an extension at run time,
  e.g. to try a parser that is not in the drawer.

## 4. Sessions

A session is one client working on one project.

- `OPEN <project drawer>` starts a session and returns its number. All
  further commands name it. Several clients (or several TinyDev windows)
  have sessions of their own, also on the same project.
- The session holds the project drawer, the paths searched for included
  files (`INCLUDE`), the results of its files and the project index.
- `CLOSE` ends it and frees everything. Sessions of a client that quits
  without `CLOSE` stay until `QUIT` (later also until Scout runs low on
  memory, the least recently used first).
- Commands without a session work on single files without a project:
  results are kept, but there is no project index. ARexx scripts use this.

## 5. Documents and snapshots

An editor's text and the file on disk differ while the user types. A
request therefore names two things:

- `FILE`: the document's real path. It is the key of its results, appears
  in the answers, and is what relative includes are resolved against.
- `TEXT` (optional): a file with the text as the editor has it now,
  usually a snapshot the client writes to `T:`. Without it, the parser
  reads `FILE`.

Parsers always read from a file; they never get text through ARexx or a
pipe. The snapshot is the client's file: it names it, writes it and
deletes it (see [Temporary files](#9-temporary-files-and-settings)). A
client sends `VERSION` (a number it raises with every change) with
the snapshot, so it can tell which state an answer belongs to.

## 6. Analysis, cache and project index

- **Analysis:** `ANALYZE` runs the parser of a file once (`scout/analyze`)
  and keeps the result: symbols, diagnostics, references and tokens.
- **Cache:** A result is valid as long as its key matches: the file's date
  and size, or the client's version for a snapshot. Queries on a file
  without a valid result analyse it first.
- **Project index:** On `OPEN`, Scout walks the project drawer (bounded
  depth, the extensions of known languages) and analyses its files in the
  background, a few at a time, while no client request waits. Files a
  result references (includes found through `INCLUDE`) are added the same
  way. The index maps names to symbols with scope `global`, plus the
  `file` scope symbols of each file.
- **Changes on disk:** A file's result is checked against the file's
  date and size when a query uses it, and analysed again if they changed.
  Files of the index are checked when the project is opened again (later:
  watching the drawer with `StartNotify()`).
- **Queries:** `SYMBOLS`, `DIAGNOSTICS`, `TOKENS` read the results of a
  file (or the index); `DEFINITION` and `COMPLETE` combine the symbol at
  the position, the scopes in the file and the index. Context-sensitive
  completion (the fields after `->`) needs more than the index; the
  protocol keeps room for an optional parser method for that.

## 7. Syntax highlighting

Highlighting has to follow every keystroke at once, and an editor's
highlighter usually runs while the text is drawn (in TinyDev a hook of
texteditor.gadget, called in input.device context: no DOS, no waiting, no
allocation). A round trip through ARexx and a parser run takes far too
long for that. Highlighting therefore has two layers, and both come from
the language's parser:

1. **Lexical rules** (`scout/describe`, per language): comments, strings,
   numbers, keyword lists by class, preprocessor lines, identifier
   characters, case sensitivity. The client fetches them once
   (`SYNTAX <language>`) and scans lines itself with a generic scanner.
   This layer is instant and decides the structure of a line: what is
   comment, string, number or keyword.
2. **Semantic tokens** (`scout/analyze`, per file): ranges of identifiers
   with what they are (type, function, macro, constant, variable, field,
   ...), things a lexical scanner cannot know, such as the names of the
   project's own types. The client asks for them (`TOKENS`) after a
   generous pause in typing (e.g. one second) and keeps them in a buffer
   per document that the highlighter reads.

Rules for the two layers:

- **Tokens only refine normal text.** Where the lexical layer found a
  comment, string, number or keyword, a token does not apply. So a `/*`
  just typed turns the following lines into a comment at once, even though
  the tokens of those lines are not new yet.
- **Tokens belong to a line's text.** A token range never spans lines. The
  client keeps, for each analysed line, a checksum of its text. When the
  highlighter draws a line, the client uses the tokens of the analysed
  line with the same checksum: at the same line number, or a few lines
  above or below after lines were inserted or deleted. A line edited since
  shows the lexical layer only until the next update.
- **Fixed kinds.** Lexical classes and token kinds are fixed lists (see
  the protocol), so a client can give each a colour without knowing the
  language.
- **Without Scout** the client highlights nothing or falls back to scanners
  of its own.

A language whose highlighting is not lexical at all (Markdown, for
example) cannot be described by the rules. Such a parser can deliver tokens
for everything; how clients treat that is left open for a later version.

## 8. Concurrency and robustness

- **No waiting in the client.** An ARexx message does not block its sender
  unless it waits for the reply. A client sends the message, carries on,
  and gets the reply at its reply port in its event loop.
- **No waiting in Scout.** Scout runs parsers from a worker process and
  keeps taking commands meanwhile. A command that needs a parser run is
  replied to when the result is there; queries that the cache can answer
  are replied to at once.
- **Coalescing.** A newer `ANALYZE` of the same file and session replaces
  one that has not started yet; the replaced one is replied to with the
  "superseded" error.
- **Order.** Client requests come before background indexing.
- **Batching.** For background indexing, Scout sends one parser run
  several requests in a row, which saves starting the program per file.
- **Timeout.** A parser run that does not answer in time (default 10
  seconds per request) gets Ctrl-C; the request ends with an error.
  Scout cannot remove a process that ignores Ctrl-C, so it stops using that
  parser until `RESCAN` and reports it in `STATUS`.
- **Failures stay local.** A parser that crashes, exits or writes garbage
  costs the request, not Scout. The last valid result of the file stays
  in the cache.

## 9. Temporary files and settings

Parsers read files, and results larger than an ARexx string go through
files, so Scout and its clients work with temporary files. These rules hold
for all of them:

- **Every program uses a drawer of its own choice.** The default is `T:`,
  in the RAM disk and the fastest; a user short of memory can name a
  drawer on a hard disk instead. Each program has its own setting and
  writes only there: Scout's files go to Scout's drawer, a client's files
  (snapshots, `TO` files) to the client's. Scout writes a `TO` file
  exactly where the client asks and never makes up names in the client's
  drawer.
- **Names are unique among running processes.** A name starts with the
  program, then the address of its process in hexadecimal (unique while
  it runs) and a counter of the process, e.g.
  `T:Scout-0812A3C0-17.in` or `T:TinyDev-07F5E218-3.snap`. Two Scouts,
  several TinyDevs and the parser runs of one Scout therefore never share
  a name. A file left over by an earlier, crashed process may be
  overwritten; it carries that process's prefix and nobody else uses it.
- **Who makes a file deletes it**, at the latest when quitting.

Scout's own files are the requests for a parser run and its answers.

Scout's settings are tool types of its icon (or Shell arguments of the
same names), as for commodities:

| Tool type | Default | Meaning |
|---|---|---|
| `TEMPDIR` | `T:` | drawer of Scout's temporary files |
| `PARSERS` | `PROGDIR:Parsers` | drawer searched for parsers |
| `TIMEOUT` | `10` | seconds a parser may take per request |
| `CX_PRIORITY` | `0` | the commodity's priority |

## 10. ARexx interface

Port: `TINYDEV_SCOUT`. Commands take their arguments like AmigaDOS
commands (`ReadArgs()` templates below; quote paths with spaces). Upper
and lower case do not matter.

### Results

- **Success:** `RC` 0, `RESULT` a JSON value (the caller needs `OPTIONS
  RESULTS`). Text is ISO-8859-1. Positions are 1-based lines and columns
  counted in bytes; ranges are `{"start":[line,col],"end":[line,col]}` with
  the end exclusive, as in the protocol.
- **Failure:** `RC` 10 and `RC2` an error number (`RESULT` cannot carry
  text when `RC` is not 0). `ERROR` returns the number and text of the
  last error of the session, and without `SESSION` the last error of all.
- **Large results:** An ARexx string holds at most 65535 bytes. Commands
  that can return more take `TO/K`: Scout writes the JSON to that file and
  returns `{"file":"<path>","size":<bytes>}`. Without `TO`, a result that
  does not fit fails with `RC2` 3. Clients like TinyDev always use `TO`
  for symbols and tokens.

| `RC2` | Meaning |
|---|---|
| 1 | bad arguments |
| 2 | unknown session |
| 3 | result too large for ARexx (use `TO`) |
| 4 | no parser for this file or language |
| 5 | the parser failed (crash, timeout, invalid answer) |
| 6 | file not found or not readable |
| 7 | superseded by a newer request |
| 8 | out of memory |
| 9 | the parser cannot do this (capability missing) |

### Commands

Sessions:

| Command | Template | Result |
|---|---|---|
| `OPEN` | `PROJECT/A,INCLUDE/M` | `{"session":3}` |
| `CLOSE` | `SESSION/N/A` | `{}` |
| `INCLUDE` | `SESSION/N/A,PATHS/M,CLEAR/S` | `{"include":["Work:SDK/Include", ...]}` |
| `ERROR` | `SESSION/K/N` | `{"code":5,"message":"..."}` |

Documents (`SESSION` is a keyword, `ANALYZE SESSION 3 FILE x.c`; without it, session 0, see [Sessions](#4-sessions)):

| Command | Template | Result |
|---|---|---|
| `ANALYZE` | `SESSION/K/N,FILE/A,TEXT/K,VERSION/K/N,LANG/K` | `{"file":...,"version":7,"language":"c","symbols":42,"diagnostics":1}`, after the run |
| `FORGET` | `SESSION/K/N,FILE/A` | `{}`; drops a snapshot's results, so the file on disk counts again |
| `SYMBOLS` | `SESSION/K/N,FILE/K,NAME/K,TO/K` | `{"file":...,"version":...,"symbols":[...]}` of a file; without `FILE`, `{"symbols":[...]}` of the project files, without locals and with `"file"` in each; optionally only those matching the AmigaDOS pattern `NAME` |
| `DIAGNOSTICS` | `SESSION/K/N,FILE/K,TO/K` | `{"file":...,"version":...,"diagnostics":[...]}` of a file; without `FILE`, `{"files":[...]}` with the project files that have any |
| `TOKENS` | `SESSION/K/N,FILE/A,TO/K` | `{"file":...,"version":7,"tokens":[...]}` as in the protocol |
| `DEFINITION` | `SESSION/K/N,FILE/A,LINE/N/A,COLUMN/N/A,NAME/K` | `{"name":...,"locations":[{"file":...,"name":...,"kind":12,"range":...,"selection":...,"detail":...}]}`, definitions before declarations, empty if unknown; without `NAME`, the word at the position |
| `COMPLETE` | `SESSION/K/N,FILE/A,LINE/N/A,COLUMN/N/A,PREFIX/K,MAX/K/N` | `{"prefix":...,"items":[{"label":...,"kind":12,"detail":...}]}`, locals first, then the file's, the project's and keywords (`{"label":...,"keyword":true}`); without `PREFIX`, the part of the word before the position; `MAX` default 50 |

Languages and parsers:

| Command | Template | Result |
|---|---|---|
| `LANGUAGES` | | `{"languages":[{"id":"c","name":"C","extensions":[".c",".h"],"parser":"c_parser"}]}` |
| `LANGUAGE` | `FILE/A` | `{"language":"c"}` or `{"language":null}` |
| `SYNTAX` | `LANG/A,TO/K` | the lexical rules of a language, as in `scout/describe` |
| `REGISTER` | `EXTENSION/A,PARSER/A` | `{}` |
| `RESCAN` | | `LANGUAGES`' result after reading `PROGDIR:Parsers/` again |

Scout:

| Command | Template | Result |
|---|---|---|
| `STATUS` | | `{"version":"1.0","protocol":1,"enabled":true,"scanning":false,"tempdir":"T:","sessions":2,"queue":0,"walks":0,"parsers":[...],"problems":[...]}` |
| `QUIT` | | `{}`; Scout ends after replying to waiting messages |

Exchange enables and disables Scout and removes it. Show and Hide do
nothing yet; a window with statistics (running commands, open sessions,
cache use) is planned. While disabled, commands fail with `RC2` 5 except `STATUS` and
`QUIT`.

## 11. Writing a client

What a client such as TinyDev does:

1. Start Scout if the port `TINYDEV_SCOUT` does not exist
   (`PROGDIR:TinyDev-Scout` or the path, asynchronously), and work on
   without Scout's functions if it does not come up.
2. `OPEN` the project, with the include paths of the project's settings.
3. `LANGUAGES` once; for each language shown, `SYNTAX` once, kept for the
   session.
4. After a document is loaded or saved, and after a pause in typing:
   write a snapshot to `T:` if the document is changed, `ANALYZE` it
   with the snapshot and a new version, then `TOKENS` and `DIAGNOSTICS`
   with `TO`. Results for an old version are dropped.
5. `SYMBOLS`, `DEFINITION`, `COMPLETE` when the user asks for them.
6. `FORGET` for a document closed without saving; `CLOSE` the session
   when the project changes or the client quits. Delete the snapshots
   and `TO` files.

The client names its files after the rules in
[Temporary files](#9-temporary-files-and-settings), in a drawer the user
can set.

Never wait for a reply in the event loop, and keep at most one request per
document in flight.

## 12. Limits and non-goals

- Text is 8-bit (ISO-8859-1 or whatever the file has). There is no UTF-8
  or UTF-16 handling, and columns are bytes. A tab is one column.
- Parsers see one file per request. Whatever needs more (types across
  files, macro expansion with included definitions) is combined by Scout
  from the per-file results and stays approximate.
- No refactoring, formatting or code actions.
- No LSP compatibility.
- Scout runs on AmigaOS 3.2 with a 68000; analyses are sized for 68030
  and better.

## 13. Code layout

Scout and its parsers are kept so they can move to a repository of their
own; nothing of them may depend on TinyDev.

- `src/scout/`: the commodity; `src/scout/docs/`: these documents.
- `src/parsers/`: the parsers, each a standalone program; planned to move
  to `src/scout/parsers/`.
- `include/scout_*.h`, `src/scout/scout_rpc.c`: shared by Scout and the
  parsers (JSON-RPC framing of the protocol).
- `src/json.c`, `include/json.h`: portable JSON code, shared with
  TinyDev. On a split it goes with Scout and TinyDev takes a copy (or it
  moves to `amiga-devkit`).
- TinyDev's side (the client, the generic lexical scanner, the token
  buffer) lives in TinyDev and talks to Scout only through ARexx.
