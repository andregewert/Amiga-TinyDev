# Scout

`TinyDev-Scout` is a commodity that analyses source code for editors. It
runs language parsers, small standalone programs (one per language, in
`Parsers/`), keeps their results and answers questions through its ARexx
port `TINYDEV_SCOUT`, in JSON: symbols, diagnostics, "go to definition",
completion and highlighting. Its ideas come from the Language Server
Protocol, but it is not LSP.

## Tooltypes
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

## ARexx-Befehle
...
