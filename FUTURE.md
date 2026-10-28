# Future

## IDE functions
- [x] Support for multiple languages (AmigaOS catalogues)
- TinyDev editor GUI integration with TinyDev-Scout commodity via ARexx (see "Scout" below)
- Template support (including parameters and initialization wizards)
- [x] Support for running build tools (fpcmake, make, smake, ... ?)
- Support for running external tools (linters, formatters, debuggers ... ?)
- [x] Optimized UI code (event handling, toolbar layout)
- [x] Settings dialog
- [x] Colors (for syntax highlighting) should be configurable

## Scout

Design: `src/scout/docs/Scout.md`, protocol: `src/scout/docs/Scout-Protocol.md`.

1. Parser protocol v1 in the C parser (`scout/describe` with lexical rules,
   `scout/analyze`, several requests per run), host tests with sample files.
2. Scout rebuilt: parsers from `PROGDIR:Parsers/`, sessions, cache, project
   index, parser runs in a worker process, JSON results with `TO`, unique
   temporary files in `TEMPDIR`, tool types.
3. TinyDev as client: start Scout when needed, asynchronous ARexx in the
   event loop, snapshots after a pause in typing, a setting for the
   temporary drawer (also for the Build menu's files, whose names in `T:`
   are not yet unique per process).
4. Features: symbol list, diagnostics (list and marks in the left bar), go to
   definition, highlighting from the lexical rules and tokens (replacing the
   built-in scanners of `syntax.c`), completion.
5. Later: a Scout window with statistics (running commands, open sessions,
   cache use), shown with Exchange's Show.

## Additional Scout / syntax parsers
- Standalone C Parser (`bin/parsers/c_parser` with JSON-RPC 2.0) - *Implemented*
- ACE Amiga BASIC parser
- Free Pascal (FPC) parser
- Amiga E parser
- Markdown parser
