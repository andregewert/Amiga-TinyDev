# Projektverwaltung

## Sprout

`Sprout` creates a project from a template: it makes the project drawer,
copies the template's files and fills in placeholders (`{$NAME}`) in file
names and texts; files can depend on a value (e.g. a Makefile per
compiler). A file template copies its files into an existing
drawer instead. Templates are LhA archives (`<name>.lha`) or drawers in
`Templates/`.

    Sprout <template> <project drawer> [KEY=value ...] [VALUES <file>] [DIR <drawer>] [FORCE] [QUIET]
    Sprout LIST [DIR <drawer>]
    Sprout <template> INFO [ICON <file>]

For example `Sprout HelloWorld Work:Hello AUTHOR=Me`; keys may be written
in any case, and `PROJECT=<name>` names the project (otherwise it is named
after its drawer).

## Bookmarks file

The bookmarks of a project are kept in `.tinydev/bookmarks.json` in the
project directory, a file you may edit by hand (JSON, ISO-8859-1; `line`
counts from 1, `text` finds the line again after changes outside TinyDev):

    {
      "version": 1,
      "bookmarks": [
        { "file": "src/ui.c", "line": 99, "text": "void tree_handle_event(EditorApp *app)" }
      ]
    }

## Project file

The project settings are kept in `.tinydev/project.json` in the project
directory, a file you may edit by hand and keep with the project (JSON,
ISO-8859-1). `name` is the project's name, shown by the icon of the
iconified TinyDev (otherwise the directory's name). Each action has a list of commands; `command` is a Shell
command line or a list of lines run as a script, `name` and `dir` (relative
to the project) are optional, `default` marks the command the action runs.
`include` lists drawers with headers (relative to the project or absolute,
e.g. the NDK's) that TinyDev-Scout indexes for Go to Definition and
completion. `{file}`, `{base}` (without extension) and `{project}` are
replaced with the active document and the project directory, `{tinydev}` with
TinyDev's drawer (`"{tinydev}mdtohtml" ...`, as it is not on the command path):

    {
      "version": 1,
      "name": "TinyDev",
      "build": [
        { "name": "Build", "command": "make", "default": true },
        { "name": "Clean", "command": ["Delete build ALL QUIET", "Delete bin/TinyDev QUIET"] }
      ],
      "run": [ { "command": "TinyDev", "dir": "bin" } ],
      "debug": []
    }
