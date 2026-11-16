# Sprout

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
