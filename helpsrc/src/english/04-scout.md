# Analysis Functions

`TinyDev-Scout` is a commodity that analyses source code for editors. It
runs language parsers, small standalone programs (one per language, in
`Parsers/`), keeps their results and answers questions through its ARexx
port `TINYDEV_SCOUT`, in JSON: symbols, diagnostics, "go to definition",
completion and highlighting. Its ideas come from the Language Server
Protocol, but it is not LSP.

Tool types of its icon (or Shell arguments of the same names):

| Tool type | Default | Meaning |
|---|---|---|
| `TEMPDIR` | `T:` | drawer for temporary files |
| `PARSERS` | `PROGDIR:Parsers` | drawer of the parsers |
| `TIMEOUT` | `10` | seconds a parser may take per file |
| `MINFREE` | `1024` | free memory (KB) below which idle sessions are closed, 0: never |
| `CX_POPKEY` | `ctrl alt s` | hotkey that shows Scout's window |
| `CX_POPUP` | `NO` | `YES` shows the window at the start |
| `CX_PRIORITY` | `0` | commodity priority |

`scripts/tdscout_query.rexx` sends a command and shows the result, e.g.
`rx scripts/tdscout_query.rexx STATUS`; `rx scripts/test_scout.rexx` tests
the commands with the sample sources of `tests/data/c_parser/` (it starts
`bin/TinyDev-Scout` if needed).

TinyDev starts Scout (from its own drawer) at its own start, opens a
session for the project and has the open documents analysed (status
line, symbol list, problems). Scout notices files of the project that
change on disk. A lamp at the right of the status line shows whether it
is still working (yellow), done (green) or could not be started (red). Its window (Exchange's Show, or its hotkey) shows what
it does, its sessions and parsers.

