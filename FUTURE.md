# Future

## IDE functions
- [x] Support for multiple languages (AmigaOS catalogues)
- [x] TinyDev editor GUI integration with TinyDev-Scout commodity via ARexx (see "Scout" below)
- Template support (including parameters and initialization wizards)
- [x] Support for running build tools (fpcmake, make, smake, ... ?)
- Support for running external tools (linters, formatters, debuggers ... ?)
- [x] Optimized UI code (event handling, toolbar layout)
- [x] Settings dialog
- [x] Colors (for syntax highlighting) should be configurable

## Scout

Design: `src/scout/docs/Scout.md`, protocol: `src/scout/docs/Scout-Protocol.md`.

1. [x] Parser protocol v1 in the C parser (`scout/describe` with lexical rules,
   `scout/analyze`, several requests per run), host tests with sample files.
2. [x] Scout rebuilt: parsers from `PROGDIR:Parsers/`, sessions, cache, project
   index, parser runs in a worker process, JSON results with `TO`, unique
   temporary files in `TEMPDIR`, tool types.
3. [x] TinyDev as client: start Scout when needed, asynchronous ARexx in the
   event loop, snapshots after a pause in typing, a setting for the
   temporary drawer (also for the Build menu's files).
4. [x] Features: symbol list, diagnostics (list and marks in the left bar), go
   to definition and back, highlighting from the lexical rules and tokens
   (the built-in scanners of `syntax.c` stay for languages without a
   parser and without Scout), completion.

Next:
- A clearer default colour scheme: the colours must differ more and be
  brighter.
- Include paths of a project (e.g. the NDK's or the compiler's headers) in
  the project settings, passed to Scout (`OPEN ... INCLUDE`), so system
  headers are indexed for definition and completion.
- Speed of the rules scanner while scrolling (check delimiters only at
  characters that can start one).
- Scout: watch the project with `StartNotify()`; drop sessions when memory
  runs low.
- A Scout window with statistics (running commands, open sessions, cache
  use), shown with Exchange's Show.
- An AmigaDOS script parser (so `syntax.c` needs no built-in scanners).

## Additional Scout / syntax parsers
- Standalone C Parser (`bin/parsers/c_parser` with JSON-RPC 2.0) - *Implemented*
- ACE Amiga BASIC parser
- Free Pascal (FPC) parser
- Amiga E parser
- Markdown parser
