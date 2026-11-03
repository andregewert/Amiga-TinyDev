# Sprout

Sprout creates projects from templates on AmigaOS 3.2. It is a Shell
command: given a template and a target drawer, it makes the drawer, copies
the template's files into it and replaces placeholders in file names and
in text files. TinyDev's wizard Project/New Project... is a front end to
it; the command works just as well on its own.

> **Status:** phases 1 to 5 are implemented: templates as drawers and
> LhA archives, the placeholders, creating a project, `LIST`, `INFO` and
> `ICON`, TinyDev's wizard with the templates' icons, file templates and
> conditional files.

## Contents

1. [Goals](#1-goals)
2. [Parts and roles](#2-parts-and-roles)
3. [Templates](#3-templates)
4. [The metadata: template.json](#4-the-metadata-templatejson)
5. [Placeholders](#5-placeholders)
6. [Creating a project](#6-creating-a-project)
7. [Writing a template](#7-writing-a-template)
8. [Command line](#8-command-line)
9. [LhA archives](#9-lha-archives)
10. [TinyDev's wizard](#10-tinydevs-wizard)
11. [File templates](#11-file-templates)
12. [Limits and non-goals](#12-limits-and-non-goals)
13. [Code layout](#13-code-layout)
14. [Phases](#14-phases)

## 1. Goals

- A new project starts from a template: drawers, sources, Makefile,
  `.tinydev/project.json`, icons, with its names filled in.
- Templates are single LhA archives, installed by copying them into
  `PROGDIR:Templates/`, and easy to make: pack a drawer with `lha a`.
- No external programs or libraries: Sprout reads LhA archives itself
  (OS 3.2 has no unpacker, and `lha` or xadmaster.library may be missing).
- Usable from the Shell and from scripts, not only from TinyDev.
- Independent of TinyDev, like Scout, so it can move to a repository of
  its own.

## 2. Parts and roles

| Part | Role |
|---|---|
| Sprout (`bin/Sprout`) | the command: lists templates, describes one, creates a project |
| templates | `.lha` archives or plain drawers in a templates drawer |
| TinyDev | the wizard: lets the user choose a template and fill in its placeholders, runs Sprout, opens the new project |

TinyDev never reads templates itself: it asks Sprout (`LIST`, `INFO`) and
gets JSON back. Only Sprout knows archives and the metadata format. The
portable placeholder code (section 5) is shared, so the wizard shows the
same defaults that Sprout would use.

## 3. Templates

The templates drawer is `PROGDIR:Templates/` of Sprout (with TinyDev in
the same drawer, `bin/Templates/`); `DIR` names another.

A template is either

- an archive `<Id>.lha`, or
- a drawer `<Id>/` (handy while making a template).

`<Id>` is the template's identifier, used on the command line. Both forms
hold the same layout:

```
template.json       the metadata (required)
template.info       its icon (optional, see "icon")
files/              what is copied into the new project
```

An archive may also hold all of it in a single top-level drawer
(`Name/template.json`, `Name/files/...`), as `lha a Name.lha Name/` packs
it; Sprout strips that drawer.

## 4. The metadata: template.json

JSON in ISO-8859-1, read with `json.c`. Unknown keys are ignored, so later
versions can add some.

```json
{
  "version": 1,
  "kind": "project",
  "name": "ReAction program",
  "description": "A window with a menu and a Makefile for gcc.",
  "category": "C",
  "icon": { "type": "program" },
  "placeholders": [
    { "key": "NAME", "label": "Program name", "default": "{$PROJECT}" },
    { "key": "AUTHOR", "label": "Author", "required": true },
    { "key": "CC", "label": "Compiler", "choices": ["gcc", "vbcc"] }
  ],
  "open": ["src/{$NAME}.c"],
  "verbatim": ["#?.iff"]
}
```

| Key | Meaning |
|---|---|
| `version` | format version, 1 |
| `kind` | `"project"` (default) or `"file"` for a file template (section 11) |
| `name` | shown name (required) |
| `description` | one or a few sentences |
| `category` | the group in the wizard, e.g. `"C"`, `"Pascal"`, `"Scripts"`; without it `"Other"` |
| `icon` | `{ "type": "<type>" }`: a symbol TinyDev brings along (`generic`, `program`, `library`, `class`, `script`, `document`; unknown types are `generic`), or `{ "file": "template.info" }`: an icon in the template; both may be given, the file wins where it can be shown |
| `placeholders` | the values asked for, in this order (section 5) |
| `open` | files (names after replacement, relative to the project) TinyDev opens after creating it; a string or a list |
| `verbatim` | AmigaDOS patterns of files whose contents are copied unchanged although they look like text (their names are still replaced) |

A placeholder entry:

| Key | Meaning |
|---|---|
| `key` | its name, `A`-`Z`, `0`-`9`, `_`, starting with a letter (required) |
| `label` | the label in the wizard; without it the key |
| `description` | help bubble in the wizard |
| `default` | the value when none is given; may contain other placeholders (built-in ones and those defined before it) |
| `required` | `true`: the value must not be empty |
| `choices` | the allowed values; the wizard shows a chooser, the first is the default |

## 5. Placeholders

**Syntax:** `{$KEY}`, optionally with a modifier: `{$KEY:upper}`.

- Only defined keys are replaced: a `{$...}` whose key is not defined is
  left as it is. So are Free Pascal's directives (`{$MODE OBJFPC}`,
  `{$I file}`, `{$H+}`): only the exact form `{$KEY}` or `{$KEY:modifier}`
  is a placeholder, and keys are upper case only.
- It does not clash with TinyDev's build placeholders (`{project}`,
  `{tinydev}`, `{file}`, `{base}` in `project.json`), which a template's `project.json`
  must keep, nor with `${VAR}` in Makefiles or `$VAR` in AmigaDOS scripts.

**Escaping:** a placeholder written with one more `$` is not replaced but
loses that `$`: `{$$NAME}` becomes `{$NAME}`, `{$$NAME:upper}` becomes
`{$NAME:upper}`, `{$$$NAME}` becomes `{$$NAME}`. This holds whether the
key is defined or not, so a template can show placeholders literally,
e.g. in a template for templates or in its documentation. Nothing else
with `$$` is touched (`$$` in a Shell line, `{$$}` stay). For a file with
many such texts, `verbatim` copies it unchanged instead.

**Built-in keys** (a template cannot define them):

| Key | Value |
|---|---|
| `PROJECT` | the project's name: given with `PROJECT=...` (`SET` or `VALUES`), else the name of the project drawer |
| `DRAWER` | the name of the project drawer (last part of `TO`), always |
| `DATE` | today, `YYYY-MM-DD` |
| `YEAR` | the year, `YYYY` |

**Standard keys:** `AUTHOR` and `AUTHORMAIL` (the author's e-mail
address) are always defined: given with `AUTHOR=...` (`SET` or
`VALUES`), else empty, so a template may use them without asking. A
template declares them as placeholders to ask for them in the wizard
(with a label, `required`, a default); then they work like its other
placeholders. A given value of a standard key is never a warning.
TinyDev's wizard gives them from its settings (Project Defaults).

**Modifiers:**

| Modifier | Result |
|---|---|
| `upper` | upper case (ISO-8859-1) |
| `lower` | lower case |
| `ident` | a C identifier: other characters become `_`, a leading digit gets a `_` before it |

`{$NAME:upper:ident}` is not supported; `ident` keeps the case, so a
header guard is written `{$NAME:upper}_H` only for names that are already
identifiers. (Chaining may come later if templates need it.)

**Where they are replaced:**

- in every path below `files/` (each part of it; see section 6 for what
  a value may contain there),
- in the contents of text files: files without a NUL byte, at most 1 MB,
  not matching `verbatim`. Others are copied byte by byte.

Contents stay 8-bit; line endings are kept as they are.

**Conditions:** a part of a path below `files/` may hold conditions that
decide whether the file (or the drawer with all below it) is copied:

| Condition | Holds if the value of `KEY` |
|---|---|
| `{?KEY=a}` | is `a` |
| `{?KEY=a\|b}` | is one of `a`, `b` |
| `{?KEY!=a\|b}` | is none of them |

Values are compared exactly (case matters); an undefined key counts as
empty (`{?KEY=}` holds then). Several conditions in a name must all hold.
They are taken out of the name before the placeholders are replaced:
`Makefile{?CC=gcc}` becomes `Makefile` if `CC` is `gcc` and is left out
otherwise. A malformed condition (no `=`, no `}`, a key that is not
valid) is an error; so is a name that is empty without its conditions.
The `verbatim` patterns are matched against the name as it is in the
template, conditions included. Only one variant of a file should hold for
a given value; if two do, the later one copied replaces the earlier.

## 6. Creating a project

1. Read `template.json`; check the values: every `required` one given,
   `choices` respected, keys known (an unknown key in the values is a
   warning, not an error).
2. Work out the values: given ones, then defaults in the order of
   definition, with built-in keys first. `PROJECT` may be given, the
   other built-in keys not (a warning).
3. Make the project drawer `TO`. Its parent must exist. If `TO` exists, it
   must be empty, unless `FORCE` is given: then files are written into it
   and files of the same name are replaced.
4. Copy `files/` in archive (or drawer) order, making drawers as needed,
   replacing placeholders in names and text contents. Protection bits and
   file notes are taken over (section 9).
5. On an error, everything Sprout created is deleted again (files, then
   drawers, in reverse order), so no half project is left. Files that
   existed before (`FORCE`) are not restored.

**Safe paths:** a path from the template, before and after replacement,
must not start with `/` (the parent drawer in AmigaDOS), contain an empty
part (`a//b`) or the parts `.`/`..`; after replacement it must not
contain `:` either (before, a `:` may belong to a modifier). Such an
entry is an error. A value that contains `/` or `:` cannot be used in a
name. Links in a template drawer are left out with a warning.

## 7. Writing a template

**Which placeholder where:**

- **File names and identifiers** (source files, program names, Makefile
  targets, C identifiers) get a placeholder of the template, e.g. `NAME`,
  with the default `{$PROJECT:ident}`. A project's name may contain
  spaces and other characters that Makefiles and the Shell handle badly
  (`Hello World` becomes `Hello_World`); the wizard shows the derived
  value, and the user can change it.
- **Shown texts** (the `"name"` of `.tinydev/project.json`, headings,
  comments, a README) use `{$PROJECT}`.
- **`{$DRAWER}`** only where the drawer itself is meant, e.g. in a path.

`templates/HelloWorld/` follows this: `files/{$NAME}.c`, the Makefile's
target and the Run command use `NAME`, the README's heading and the
project's name use `PROJECT`. With
`Sprout HelloWorld RAM:hello project="Hello World" author=Me`:

| Placeholder | Value |
|---|---|
| `PROJECT` | `Hello World` |
| `DRAWER` | `hello` |
| `NAME` | `Hello_World` (from its default `{$PROJECT:ident}`) |

**Further hints:**

- Keep the build placeholders of `project.json` (`{file}`, `{base}`,
  `{project}`) as they are; Sprout leaves them alone.
- Mark values without a sensible default as `required` (e.g. `AUTHOR`).
- Text files are ISO-8859-1, like everything that goes to the Amiga;
  files that must not be changed although they are text go into
  `verbatim`.
- A placeholder that must stay in the new project as it is is written
  with one more `$` (`{$$NAME}`).
- Files for different choices (a compiler, a GUI toolkit) get
  conditions in their names: `templates/HelloWorld/` has a placeholder
  `CC` with the choices `vbcc`, `gcc` and `sasc` and the files
  `Makefile{?CC=vbcc}`, `Makefile{?CC=gcc}`, `smakefile{?CC=sasc}` and a
  `.tinydev/project.json` for each.
- Try a template in the Shell before packing it:
  `Sprout <drawer of the template> RAM:Test AUTHOR=x`.

## 8. Command line

```
Sprout TEMPLATE,TO,SET/M,VALUES/K,DIR/K,ICON/K,LIST/S,INFO/S,FORCE/S,QUIET/S
```

| Argument | Meaning |
|---|---|
| `TEMPLATE` | an identifier (looked up as the drawer `<Id>/`, then as `<Id>.lha` in the templates drawer, so a template being worked on wins over its archive) or a path to an archive or drawer; not needed for `LIST` |
| `TO` | the project drawer to create; for a file template the existing drawer the files go into (not for `LIST` and `INFO`) |
| `SET` | values as `KEY=value`, e.g. `NAME=Hello AUTHOR="André Gewert"` (ReadArgs splits them at the `=`; Sprout joins a key and the item after it again, so `AUTHOR "André Gewert"` works too); keys may be written in any case (`author=Me`); `PROJECT=...` gives the project a name other than its drawer's |
| `VALUES` | a JSON file with values: `{ "NAME": "Hello", "AUTHOR": "..." }` (keys in upper case, as written by a program); `SET` wins over it |
| `DIR` | another templates drawer |
| `LIST` | write all templates of the drawer as JSON to standard output |
| `INFO` | write the metadata of `TEMPLATE` as JSON |
| `ICON` | with `INFO`: also extract the template's icon to this file; with `LIST`: extract the icons of all templates to this (existing) drawer as `<Id>.info` (for the wizard) |
| `FORCE` | write into an existing, non-empty `TO` |
| `QUIET` | no progress lines |

Examples:

```
Sprout ReAction Work:Projects/Hello NAME=Hello AUTHOR=Me
Sprout ReAction Work:Projects/hello project="Hello World" author=Me
Sprout LIST
Sprout ReAction INFO ICON T:sprout.info
Sprout LIST ICON T:icons
```

`LIST` writes every template of the drawer (drawers with a
`template.json` and `*.lha`; a drawer wins over an archive of the same
identifier), sorted by identifier, indented:

```json
{
  "version": 1,
  "templates": [
    { "version": 1, "kind": "project", "name": "...", "description": "...",
      "category": "C", "icon": { "type": "program" }, "placeholders": [ ... ],
      "id": "ReAction", "path": "PROGDIR:Templates/ReAction.lha" }
  ]
}
```

Each entry is the template's metadata in the form of `template.json`
(checked, with the defaults filled in: `kind`, `category`), plus `id` and
`path`; a program reads it with `sprout_template_from_json()`. A template
that cannot be read is listed with `"error": "<text>"` instead of its
metadata, so the wizard can say why it is missing. `INFO` writes
`{ "version": 1, "template": { ... } }` with one such entry; with `ICON`
it also copies the file named by `"icon": { "file": ... }` (a warning if
there is none). `LIST ICON <drawer>` does the same for every template
that names an icon file, as `<drawer>/<Id>.info`; a missing icon is a
warning (return code 5) and the list is complete all the same.

Messages go to standard error, in English. Return codes: 0 done,
5 (WARN) done with warnings, 10 (ERROR) failed (nothing left behind),
20 (FAIL) bad arguments or out of memory.

## 9. LhA archives

Sprout reads LhA archives itself, sequentially (no seeking back), with a
portable decoder that has no Amiga headers and is host-tested.

- **Header levels** 0, 1 and 2; level 3 is reported as unsupported.
- **Methods** `-lh0-` and `-lz4-` (stored), `-lh4-` (4 KB dictionary),
  `-lh5-` (8 KB), `-lh6-` (32 KB), `-lh7-` (64 KB), `-lhd-` (drawer
  entry). Other methods are an error naming the method.
- **Checks:** the header checksum (levels 0/1), the header CRC where
  present and the CRC-16 of every file's data.
- **Paths:** level 0/1 names separate drawers with `\`, `/` or 0xFF;
  level 2 has the drawer name in extended header 2 (0xFF separated).
  All become `/`.
- **Amiga extras:** a file note stored after a NUL in the name (as Amiga
  LhA does) is taken over; so are the protection bits from the attribute
  byte when the archive was made on an Amiga (OS id `A`, header level 0
  or 1); otherwise files get the default bits.
- **Two passes:** `template.json` may come after the files, so an archive
  is read once for it (finding the drawer that holds it) and once for
  `files/`. Drawers above a file are made even if the archive has no
  entries for them.
- **Memory:** the dictionary (at most 64 KB) plus small tables; a text
  file to be changed is held whole (at most 1 MB), binary files are
  written as they are decoded.
- For `LIST` and `INFO` only `template.json` (and the icon) are decoded;
  the other entries are skipped by their sizes.

## 10. TinyDev's wizard

Project/New Project... opens a modal window, built like the settings
windows. Its texts come from TinyDev's catalog.

**Page 1, template:** the categories on the left (listbrowser), the
templates of the chosen category in the middle (listbrowser, a small
symbol per row), on the right the chosen template: its icon large, name
and description. Below: the project's name and the project drawer
(string with an ASL button); an empty name means the drawer's.

**Page 2, values:** a form generated from the placeholders: a label and a
string gadget per value, a chooser where `choices` are given, the
description as help bubble, defaults filled in (expanded with the shared
placeholder code; `PROJECT` and `DRAWER` from page 1, `AUTHOR` and
`AUTHORMAIL` from TinyDev's settings before a template's default; a field
keeps a value the user typed). The project drawer is suggested in the
setting Project Defaults/Project drawer, if set. `required` values are marked with `*` and must be
filled in before Create. Back, Next and Create switch the pages; Create
also works from page 1. The forms of all templates are built with the
window, as pages of one page gadget.

**Create:** TinyDev writes the values (with `PROJECT` if a name was
given, `AUTHOR` and `AUTHORMAIL` from the settings also when the template
does not ask for them) to a file in the temporary drawer,
runs `Sprout <template> <drawer> VALUES <file>`, shows its messages if it
fails, and on success opens the drawer as the project (the tree's root),
then the files of `open`.

The templates come from `Sprout LIST ICON <drawer>` once per opening of
the window, the icons into a drawer in the temporary drawer; TinyDev loads
them with icon.library and deletes the files. The list shows the symbols
per icon type, which are built into TinyDev; the details show the
template's icon where there is one (the box is as large as the largest
icon, at most 128 x 128 pixels; a larger icon gets the symbol), else the
symbol enlarged.

## 11. File templates

`"kind": "file"` marks a template for a single file or a few, e.g. a new
C module with its header (`templates/CModule`). Same layout, same
placeholders; `TO` is then an existing drawer, usually inside a project,
that the files are copied into: Sprout makes no drawer of its own (only
the drawers inside `files/`), refuses files that exist (unless `FORCE`),
and on an error removes only what it made. `PROJECT` is the project's name
when it is given (TinyDev gives the open project's), else the drawer's.

In TinyDev: Project/New File from Template..., the same wizard with the
file templates; instead of the project's name and drawer it asks for the
drawer of the files (suggested: the active document's, else the
project's). After creating, the files of `open` are opened; the project
stays as it is.

## 12. Limits and non-goals

- No writing of archives: templates are packed with LhA.
- No scripts or programs run by a template (no hooks after creation).
- Conditions compare values only: no `and`/`or` across keys beyond
  several conditions in one name, no patterns.
- 8-bit text only; no conversion of character sets or line endings.

## 13. Code layout

| File | Contents |
|---|---|
| `src/sprout/sprout_main.c` | ReadArgs, finding templates, DOS: drawers, files, protection bits, notes, rollback, `LIST`/`INFO` output |
| `src/sprout/sprout_tmpl.c`, `include/sprout_tmpl.h` | portable: metadata, values, placeholder expansion, safe paths, text detection |
| `src/sprout/sprout_lha.c`, `include/sprout_lha.h` | portable: LhA headers and decoding over read/write callbacks |
| `src/sprout/docs/Sprout.md` | this document |
| `tests/test_sprout_tmpl.c`, `tests/test_sprout_lha.c` | host tests; the archives in `tests/data/sprout/` (every header level and method jlha writes) are made by `tools/mksprouttests.py` and left out of `charcheck` as they are binary |
| `templates/<Id>/` | the templates that come with TinyDev: `make` copies them as drawers to `bin/Templates/<Id>/` (with `cp`, as names with `{$...}` cannot go through make), `make dist` packs them into `Templates/<Id>.lha` of the archive (with jlha, which `make dist` needs anyway) |

`json.c` is shared as with Scout. TinyDev links `sprout_tmpl.c` for the
wizard's defaults; Sprout uses nothing of TinyDev.

## 14. Phases

1. (done) Sprout's core: metadata, placeholders, copying from template
   drawers, rollback; host tests; a first template made from
   `examples/HelloWorld`.
2. (done) LhA reading, `LIST`, `INFO` and `ICON`, archives in `make dist`.
3. (done) The wizard in TinyDev with the built-in symbols per icon type.
4. (done) Icon files from the template in the wizard; then file templates.
5. (done) Conditional files (`{?KEY=value}` in names), used by
   `templates/HelloWorld/` for its compilers.
