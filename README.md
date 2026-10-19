# TinyDev

TinyDev is a compact native AmigaOS 3.2 multi-document text editor. Each
closable tab owns a ReAction `texteditor.gadget`, providing native selection,
clipboard, undo/redo, scrolling and line numbers. Built-in highlighting covers
C/C headers (`.c`, `.h`) and AmigaDOS scripts (`.script`, `.dos`). Other files
remain plain text.

A simple minimap and a file browser are included.

In the future, TinyDev aims to be a simple but complete development environment
for AmigaOS 3.2.
It should support multiple languages via AmigaOS catalogues,
and multiple syntax parsers. It will also include features such as template system,
running build tools, and other external tools.

As you can guess, much of the code is AI-generated (using different models).
I will try to manually check and improve the generated code.

![Screenshot](screenshot.png)

## Build and test

The shared tools in `amiga-devkit/` are a git submodule
([Amiga-Devkit](https://github.com/andregewert/Amiga-Devkit)), and so is
Henry Spencer's regex library in `external/regex/` (used by the search).
Clone with `git clone --recursive` or run `git submodule update --init` once.

The default toolchain prefix is `/opt/amiga/bin/m68k-amigaos-`. Build with:

    make clean && make
    make test
    /opt/amiga/bin/m68k-amigaos-objdump -f bin/TinyDev
    /opt/amiga/bin/m68k-amigaos-objdump -f bin/TinyDev-Scout
    /opt/amiga/bin/m68k-amigaos-objdump -f bin/parsers/c_parser

The Makefile explicitly selects `-m68000 -msoft-float -noixemul` and creates:
- `bin/TinyDev`: Main ReAction GUI editor.
- `bin/TinyDev-Scout`: Scout, the source analysis commodity, with ARexx host port `TINYDEV_SCOUT`.
- `bin/parsers/c_parser`: Standalone C parser communicating via STDIN/STDOUT (JSON-RPC 2.0).
- `bin/Classes/Gadgets/dirtree.gadget` and `minimap.gadget`: the folder
  tree and the minimap as reusable ReAction classes (see below).

`bin/` holds only these finished programs and classes, laid out as they are
used on the Amiga; everything produced on the way (objects, dependency files,
`.debug` files with symbols, host test binaries) goes to `build/`. `make clean`
removes both. `make` first runs
`make charcheck`, which refuses UTF-8 in the Amiga sources, scripts, the Makefile
and `LICENSE`; these files are ISO-8859-1.

## TinyDev-Scout Commodity & ARexx Interface

`TinyDev-Scout` runs in the background as a standard AmigaOS Commodity (`commodities.library`) managed via Exchange (Enable, Disable, Kill) and exposes an ARexx host port named `TINYDEV_SCOUT`.

Language parsers are standalone executables communicating over STDIN/STDOUT using JSON-RPC 2.0 / NDJSON, making them interchangeable.
This is the *Scout protocol*. Its ideas (and the numbering of symbol, diagnostic
and completion kinds) are borrowed from the Language Server Protocol, but it is
not LSP and LSP clients or servers cannot talk to it.

### Supported ARexx Commands

| Command | Parameters | Description |
|---|---|---|
| `PARSE` | `<filepath>` | Executes parser and caches symbols & diagnostics |
| `SYMBOLS` | `<filepath>` | Returns symbol list (`<kind> <line> <col> <name>`) |
| `DIAGNOSTICS` | `<filepath>` | Returns diagnostic list (`<severity> <line> <col> <message>`) |
| `DEFINITION` | `<filepath> <line> <col> [symbol]` | Finds symbol definition location (`<file> <line> <col>`) |
| `COMPLETE` | `<filepath> <line> <col> [prefix]` | Returns space-separated completion candidates |
| `REGISTER_PARSER` | `<ext> <executable_path>` | Configures or overrides parser for a file extension |
| `STATUS` | None | Returns broker status, port, and request count |
| `QUIT` | None | Shuts down the Scout commodity cleanly |

An example ARexx query script is provided in `scripts/tdscout_query.rexx`.
An AmigaDOS shell script to test/demonstrate the C parser directly is provided in `scripts/test_c_parser.script`.

## Gadget classes

TinyDev's own UI components are built as standalone ReAction class
libraries in `src/classes/` so other programs can use them, too. They can be
installed globally in `SYS:Classes/Gadgets/`. TinyDev opens an installed
class first (`gadgets/<name>.gadget`) and otherwise uses the copy in
`PROGDIR:Classes/Gadgets/`, where the build puts it. An older installed
version therefore hides a fresh build; check with
`Version gadgets/dirtree.gadget FULL`.

| Class | Superclass | Purpose |
|---|---|---|
| `dirtree.gadget` | `listbrowser.gadget` | Lazily loaded, bounded directory tree with the directory name as column title, built-in entry symbols and hidden `.info` files |
| `minimap.gadget` | `gadgetclass` | Scaled, coloured overview of a `texteditor.gadget`, rendered in its own process |

The public interface (tags, methods, usage) is documented in
`include/gadgets/<name>.h`; `sfd/` holds the library descriptions from which
`include/proto`, `include/inline` and `include/clib` are generated with
`sfdc`. `src/classes/common/classlib.c` is the library frame shared by all
classes.

Because `dirtree.gadget` must not use DOS while Intuition handles input, it
reports expand, collapse and double-click with a `GADGETUP`; the application
then sets `DIRTREE_HandleEvent` with `SetGadgetAttrs()` to let the gadget read
the directory from the application's process (and redraw itself).
`minimap.gadget` likewise reads the editor and starts render jobs only when
the application sets `MINIMAP_Update` (after event batches and when its
signal arrives); clicks and drags are reported as notifications
(`MINIMAP_Top`, `MINIMAP_Dragging`) and the application scrolls the editor.
Colours come from an optional per-line hook; TinyDev plugs in its syntax
scanner there.

## Icons

`make icons` draws sample Workbench icons in three styles (classic 4 colour,
GlowIcons, NewIcons) into `icons/<Style>/` and a preview `icons/preview.png`.
The motifs are in `tools/mkicons.py`, the icon writer comes from
`amiga-devkit/tools/amigaicons.py`:

![Icon preview: classic, GlowIcons and NewIcons, each normal and selected](icons/preview.png)

| Icon | Motif |
|---|---|
| `TinyDev` | Editor window with line numbers, coloured code and the minimap |
| `TinyDev-Scout` | A page of code under a magnifying glass |
| `c_parser` | A chip with the label of the parser's language ("C") |

A new Scout parser gets its icon with `parser('<letter>')` in
`tools/mkicons.py` (the letter needs a glyph in `GLYPHS` and `SMALL`).

## Settings (tool types)

Settings are tool types of the program icon (`PROGDIR:TinyDev.info`), also
when TinyDev is started from the Shell; when it is started from the
Workbench, the tool types of project icons are applied afterwards.

Project/Settings... (Amiga+,) opens the settings window with the categories
Window, Colours and Toolbar. "Save" writes the settings into the program
icon (other tool types and the NewIcons block stay; a setting at its default
stays as a disabled entry such as `(TOOLBARFRAMES)`), "Use" takes them for
this session only. The highlighting colours take effect at once, the other
settings at the next start.

| Tool type | Meaning |
|---|---|
| `TOOLBAR=IMAGES\|BOTH\|TEXT` | Toolbar buttons show images (default), images with text below, or text only (no image is loaded) |
| `TOOLBARFRAMES` | Frames around the toolbar buttons (otherwise they are flat and only look recessed while pressed) |
| `NOSPLASH` | No splash window while TinyDev starts |
| `WIDTH=n`, `HEIGHT=n` | Size of the main window at the start (default: 90 % x 80 % of the screen) |
| `LEFT=n`, `TOP=n` | Position of the main window at the start (default: centred) |
| `COLOR_TEXT`, `COLOR_KEYWORD`, `COLOR_STRING`, `COLOR_COMMENT`, `COLOR_PREPROCESSOR` | Colours of the syntax highlighting as `RRGGBB` |

## Target requirements

- AmigaOS 3.2 and V47 `window.class`, `layout.gadget`, `clicktab.gadget`, and
  `texteditor.gadget`; editor scrollbars use `scroller.gadget`.
- `dirtree.gadget` and `minimap.gadget` (installed or in
  `PROGDIR:Classes/Gadgets/`); `dirtree.gadget` needs `listbrowser.gadget`
  and, for its expand arrows, `glyph.image`.
- `asl.library` and normal ReAction dependencies.
- `speedbar.gadget` and `label.image` for the toolbar; `chooser.gadget` for
  its overflow list (optional).
- `string.gadget` and `checkbox.gadget` for the find and replace window
  (loaded when it is first opened).
- AISS with its standard `TBImages:` assignment for toolbar imagery. If an
  individual image cannot be created, the corresponding toolbar button shows
  its text instead.
- The editor area always uses the default screen font; there is no font
  selection.

While TinyDev starts (classes, toolbar images), a splash window shows the
program icon, name, version and a progress bar; it uses only Intuition and
graphics.library, so it appears before any class is loaded. Project/About
opens a window with the same head plus the website, licence and the versions
of TinyDev's gadget classes. Both follow Kickdown and the ReAction style
guide of amiga-devkit.

Project supports New/Open/Save/Save As/Close/About/Quit, native edit commands,
horizontal scrolling without automatic line wrapping, line-number toggling,
safe temporary-file saves, duplicate-path tab activation,
and LF/CR/CRLF preservation. A document whose changes are all undone (or
redone back to the saved text) is marked unchanged again: after Undo and
Redo and whenever editing pauses (for texts up to 256 KB), TinyDev compares
the text with a checksum of the text as loaded or saved (line-end style and
line ends at the end of the text do not count). A status line spanning the full window width at
the bottom shows the number of open documents and the line count of the
currently displayed document. The AISS toolbar provides New, Open, Save,
Undo, Redo, Cut, Copy, Paste, and Find, each with a help bubble. Buttons that would do
nothing are ghosted (Save for an unchanged document, Undo/Redo without steps,
Cut/Copy without a selection); a button's ghosted image (`<name>_g`) is only
loaded when it is ghosted for the first time. Buttons that do not fit into a
narrow window are listed in a drop-down chooser right of the bar.
Edit/Find (Amiga+F) opens a find and replace window that stays open next to
the main window (as in Kickdown): search text, replacement, case sensitive,
whole words, regular expression, backwards and wrap around; Find, Replace,
Replace all. "Search in" chooses the document in the visible tab or all open
documents: then the search goes on in the next tab (in tab order, and round
to the first one with "Wrap around"), switching to the tab with the hit, and
Replace all replaces in every open document, one undo step per document.
"Project files" searches the files below the directory opened in the folder
tree (the project) whose names match the "File pattern" (an AmigaDOS pattern
such as `#?.(c|h)`; icon files, links and files larger than 1 MB or with NUL
bytes are skipped, at most 16 levels deep). The search runs in the
background of the event loop and can be stopped; files that are open are
searched in their tab. The hits (at most 500) are listed with file, line and
text; the first one is shown at once, Find and Find Next go through the list
and a click in the list shows a hit (opening its file in a tab). Replace all
opens every file with hits in a tab and replaces there, one undo step per
file; nothing is saved.
Regular
expressions are POSIX extended ones (`[[:digit:]]` instead of `\d`, no
lazy quantifiers or lookaround); `\0` to `\9` in the replacement insert the
match and its groups, `\\` a backslash. `^` and `$` match at the start and
end of a line. The character classes (`[:alpha:]`, `[:upper:]`, `[:lower:]`,
`[:alnum:]`, `[:punct:]`, `[:graph:]`, `[:print:]`) include the Latin-1
characters (Ä, ß, ...); without "Case sensitive", `[:upper:]` and
`[:lower:]` stand for all letters. Edit/Find Next (Amiga+G) repeats the last
search without the window. Case-insensitive search also folds the Latin-1
letters (Ä/ä ...), and they count as word characters. Replace only replaces
the hit that is still marked as it was found; Replace all is one undo step.
A match never spans a line break.
C highlighting recognizes standard C99 keywords,
strings/character constants, preprocessor lines, `//` comments, and block
comments. AmigaDOS keyword matching is case-insensitive and `;` begins a comment.
To keep scrollbar dragging as smooth as the gadget's own mouse-wheel scrolling,
syntax highlighting is suspended while a scrollbar is actively dragged and
restored (with a full redraw) once the drag ends or the scrollbar has not
moved for about a fifth of a second.
An optional colored minimap can be toggled from the View menu. It appears as a
column on the right of the window (below the toolbar, beside the editor) and
renders the active document using the same syntax colors on the same grey
background the editor shows (the screen's `BACKGROUNDPEN`). It also draws a
simple frame with a slightly darker background that marks the vertically
visible portion of the editor; only the vertical range matters, so the box
always spans the full minimap width. This viewport is refreshed after a
scrolling operation completes (not while a scrollbar is being dragged) and
after a window layout change. The viewport overlay can also be dragged with the
mouse to scroll the editor: pressing and dragging inside the minimap moves the
visible region so it is centred on the cursor line, and (as with scrollbar
dragging) syntax highlighting is suspended while the drag is in progress and
restored, together with a viewport redraw, once the mouse button is released.
Rendering runs in a
separate process of `minimap.gadget` so the editor stays responsive; nothing is
rendered while the minimap is hidden, updates are coalesced to the minimum (a
single job is ever outstanding), and the process is stopped cleanly on exit. Like the folder
tree, the minimap column is resizable: a WeightBar between the editor and the
minimap lets its width be dragged, and the render scales to whatever width the
column is given. The minimap
drawing area is framed by a raised bevel (drawn
with the screen's `SHINEPEN` and `SHADOWPEN`) so it reads as a distinct panel.
Every editor installs a white `GA_BackFill` hook plus an editor-local
`DrawInfo` whose background pen is white, and reserves black for normal text
with dark screen pens for keywords, strings, comments, and preprocessor lines.
Note that the AmigaOS 3.2 `texteditor.gadget` renders its text area with the
screen's `BACKGROUNDPEN` and ignores `GA_BackFill`/`GA_DrawInfo` there, while
its `GA_TEXTEDITOR_Pen`/`GA_TEXTEDITOR_ColorMap` attributes are unimplemented
and `GA_TEXTEDITOR_Transparent` is OS4-only; the text area therefore keeps the
Workbench background colour until a future gadget subclass overrides rendering.
Project/Open Directory populates a hierarchical folder tree; the directory's
name is shown as a fixed title above it, and every entry has a small symbol
(drawer, open drawer, file, source code, script, picture; drawn by the gadget,
no files needed). There are no expand arrows: a double-click opens or closes a drawer. Icon files (`.info`)
are not listed. Double-clicking a
file opens it in a tab. The tree is resizable and can be hidden from the View
menu. A selected base directory opens its first level immediately; child
directories start collapsed. Opening a child reads only its direct children;
closing it releases that subtree, so deeper levels are never scanned eagerly.
The visible tree is bounded to 16 directory levels and 2048 entries; a
directory that does not fit is shown incompletely and reported.

Version one intentionally treats contents as 8-bit text. UTF-8 decoding,
Unicode shaping, user-configurable colors, sessions, font
selection, and arbitrary raw TrueType loading are not promised. A missing V47
class prevents startup. The minimap is a scaled color overview with
drag-to-scroll navigation via its viewport overlay.

## Credits

Regular expressions: the regex library by Henry Spencer (alpha3.8 patch 1,
<https://github.com/garyhouston/regex>), Copyright 1992, 1993, 1994, 1997
Henry Spencer, used unchanged as a submodule.
