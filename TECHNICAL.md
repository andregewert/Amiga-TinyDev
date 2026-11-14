# TinyDev – technical notes

Notes that concern several modules: how the parts work together, and
findings about the OS 3.2 classes that more than one place depends on.
What belongs to one module is in that module's Doxygen comments
(`@file` head or function); the public interfaces of the gadget classes
are in `include/gadgets/<name>.h`. `AGENTS.md` says which section to read
before which change.

## Event flow between TinyDev and its classes

`dirtree.gadget` and `minimap.gadget` must not use DOS or read the editor
while Intuition handles input (GM_HANDLEINPUT runs on input.device's task).
Both therefore only report, and TinyDev does the work from its own
process:

- **dirtree.gadget** reports expand, collapse and double-click with a
  `GADGETUP`; `tree.c` answers with `DIRTREE_HandleEvent` (through
  `SetGadgetAttrs()`), which reads the directory and redraws. Changes on
  disk arrive as DOS notifications on the gadget's signal
  (`DIRTREE_SignalMask`); the event loop answers with `DIRTREE_Refresh`,
  which reads only the changed directories. Project/Folder Tree/Refresh
  reads all shown directories, for file systems without notification.
- **minimap.gadget** reads the editor and starts a render job only when
  `minimap.c` sets `MINIMAP_Update`: after every batch of input events and
  when its signal arrives. Clicks and drags come back as notifications
  (`MINIMAP_Top`, `MINIMAP_Dragging`) and TinyDev scrolls the editor. The
  colours come from a per-line hook that runs TinyDev's syntax scanner on
  the document, so a document is disconnected
  (`minimap_forget_document()`) before its editor is disposed.

## Background updates and the mouse

While the user moves a window or uses a menu, Intuition holds the layers,
and a gadget method called from TinyDev's task can then hang the system
(seen: moving the output window during a build). So, during a build, the
event loop does not update gadgets from the background while a mouse button
is held (`ui_mouse_held()`, with `PeekQualifier()`, which also works with
mice that do not go through the CIA): the build's output and its end, the
answers of Scout and the tree's refresh wait for a later turn. The build's
timer guarantees that turn; without a build nothing would wake the loop, so
nothing waits then.

Do not protect such calls with `LockLayerInfo()`: the autodoc of
`intuition.library/BeginRefresh` forbids Intuition functions dealing with
gadgets while that lock is held.

## Scrolling, highlighting and the minimap viewport

While a scrollbar is dragged, the syntax highlighting is suspended and the
minimap's viewport box is not updated; both come back (with a full redraw)
when the drag ends. The end is the scroller's `GADGETUP` or two IntuiTicks
without a scroll signal (`document.c`, `ui.c`). A drag in the minimap
suspends the highlighting in the same way until the button is released.

Two layers make the highlighting (`syntax_scan()`): the lexical rules of
the language from Scout's parser (`rules.c`; TinyDev has no scanners of
its own, so without Scout nothing is highlighted) decide comments,
strings, numbers and keywords at once; Scout's tokens (`tokenmap.c`,
fetched by `scoutlink.c` after each analysis) add the project's types,
functions, macros and constants where a line is normal text, parameters
and variables also in strings and preprocessor lines (scripts substitute
them there). The hook gets a line's text but not its number,
so tokens are found by a checksum of the line's text: a line changed
since the analysis shows the first layer only until the next one. Rules
and tokens are read by the editor's hook and by the minimap's render
process: rules stay until TinyDev quits, token maps are swapped under
`Forbid()` and the old one is freed only at the next swap.

The main window is always a smart refresh window, whatever the user's
ReAction preference says: `texteditor.gadget` scrolls with `ScrollRasterBF()`
and draws only the rows that are new, so in a simple refresh window the part
coming up from under another window is left to a later refresh, which does
not fit the text any more when scrolling goes on. `listbrowser.gadget`
scrolls the same way, so every window with a list (the tool windows, find,
completion, the settings, project settings and template windows, and the
HelpViewer) asks for smart refresh too (`WA_SmartRefresh`); a new window
with a list or another scrolled area needs it as well.

## Editor colours and texteditor.gadget

TinyDev's syntax colours are real screen pens (`ObtainBestPen`), handed to
the highlighter hook (`syntax.c`) and to the minimap's colour hook. The
OS 3.2 `texteditor.gadget` paints its text area with the screen's
`BACKGROUNDPEN` and ignores `GA_BackFill` and `GA_DrawInfo` there;
`GA_TEXTEDITOR_Pen`/`GA_TEXTEDITOR_ColorMap` are not implemented and
`GA_TEXTEDITOR_Transparent` is OS4 only (details in `ui.c`). The minimap
uses the same background pen so both look alike.

The editors always export LF (`GA_TEXTEDITOR_LineEndingExport`,
`LINEENDING_LF`), and `file_save()` writes the file's own line ends. Never
let `texteditor.gadget` export CRLF (or `LINEENDING_ASIMPORT` for a CRLF
file): in 47.69, `GM_TEXTEDITOR_ExportText` counts one byte per line end but
writes two, so every export overruns the heap by nearly a byte per line.

## The project

The directory opened in the folder tree is the project. What refers to it:

- the project-file search of the find window (`find.c`, `filescan.c`),
  bounded like the tree (16 levels, no links, no icons);
- the bookmarks of the project's files in `<project>/.tinydev/bookmarks.json`
  (`bookmarks.c`, `bmstore.c`); the files of `.tinydev/` are read and
  written by `projdata.c`; the directory is visible and made when first
  needed, the file is JSON in ISO-8859-1 that may be edited by hand and is
  never overwritten when it cannot be read;
- the project's settings in `<project>/.tinydev/project.json`, read and
  written by `projconf.c`, edited in the window of `projwin.c`
  (Project/Project Settings...): lists of Shell commands for the actions Build, Run and
  Debug, each with a default, the include drawers and the languages
  chosen in the File Type menu (format in `include/projconf.h`;
  `scoutlink.c` writes the languages and sends them to Scout's session).
  The file is
  meant to be shared (checked in with the project), so nothing personal of
  a session goes into it; writing keeps the keys TinyDev does not know,
  also those of the entries; `build.c` runs the default commands (Build in
  a process of its own with its output in the output window of
  `outwin.c`, Run and Debug detached; details in its file comment);
- new projects (`newproj.c`, Project/New Project...): Sprout creates the
  drawer from a template, then `tree_open_project()` makes it the project
  (closing the old project's documents) and the template's files of
  `open` are opened; the wizard links Sprout's portable core
  (`sprout_tmpl.c`) only to show the defaults Sprout will use; file
  templates (Project/New File from Template...) go into a drawer of the
  open project, whose name (`ui_project_name()`) is their PROJECT;
- the start drawer of the file requesters (`fileio.c`);
- Scout's session (`scoutlink.c`): `tree.c` reports a new root
  (`scoutlink_project_opened()`), which closes the old session and opens
  one for the new project; `document.c` reports changes and closed
  documents, `fileio.c` loaded and saved ones. The analyses are sent from
  the event loop (IntuiTicks for the pause in typing, the answers' port in
  the main `Wait()`); a changed document goes as a snapshot written to the
  drawer of the temporary files (`temp_name()`, `tempfile.c`).

## Help: TinyDev, HelpViewer and the help pages

- The pages are `package/help/<language>/` (Amiga locale names, the same
  file names in every language); `make` copies them to `bin/Help/`.
- `help.c` starts `PROGDIR:HelpViewer "PROGDIR:Help" PORTNAME TinyDev` with
  `SystemTags()` and `SYS_Asynch`, so the viewer outlives TinyDev.
  `OpenWorkbenchObjectA()` would be the Workbench way, but it cannot pass
  options.
- The HelpViewer opens the language drawer of the user's locale when a
  directory has no index page but language drawers.
- With `PORTNAME` the HelpViewer adds the public port
  `HELPVIEWER.<NAME>`; a later start with that name sends an
  `OpenMessage` with the location as full path and ends. Without
  `PORTNAME` there is no port, every start is a viewer of its own.
- On a Shell start the HelpViewer reads the tool types of its icon first,
  then the arguments; a switch argument (`TTF`) may only switch on, never
  off what the tool type set.
- Printing (`printwin.c`) is Kickdown's print window with the same
  options and tool type names; what was found there about printer.device
  and TurboPrint is in its `@file` comment. A fix in one should go to the
  other as well.

## Iconifying and the tool windows

The main window has an AppPort, so window.class can iconify it (gadget in
the title bar, Project/Iconify); the program icon is the AppIcon.
`ui_iconify()` and `ui_uniconify()` (`ui.c`) take the tool windows along:

- Before iconifying, the open tool windows are noted and closed:
  `find_hide()` keeps the find window's box and pauses a project search
  (it goes on after `find_show()`), `bmlist_close()` keeps the bookmark
  list's box for the session, and so does `outwin_close()` for the output
  window. None of them changes the settings. A running build goes on.
- window.class builds the menu strip anew from `menus[]` at every
  `WM_OPEN`, so `set_menu_checks()` writes the current check marks into
  the table first.
- The window's port changes with iconifying: the event loop reads
  `WINDOW_SigMask` on every turn. While iconified `app->window` is NULL;
  code that draws or opens windows must check it.
- After `WM_OPEN` the window is put back into its box, the toolbar gets
  its window again, the tool windows reopen and the tree is refreshed
  (notifications that came meanwhile).

A new tool window must be added to both functions.

## Classes: installed or next to the program

TinyDev and the HelpViewer open an installed class first
(`gadgets/<name>.gadget`) and otherwise the copy in
`PROGDIR:Classes/Gadgets/`, where the build puts it. An installed older
version therefore hides a fresh build; check with
`Version gadgets/dirtree.gadget FULL` before looking for a bug.

## Build layout and the archive

- `bin/` is laid out as on the Amiga (programs, `Classes/Gadgets/`,
  `Catalogs/<language>/`, `Help/<language>/`), `build/` holds everything in
  between, `dist/` the archive.
- `tools/mkdist.py` copies `bin/` as it is, so the archive runs without
  installing; the sources in it must build without git (submodule
  contents and the devkit are copied).

## Ideas not done yet

- Soft line wrapping as a file type setting (texteditor.gadget wraps
  within the window when `GA_TEXTEDITOR_HorizontalScroll` is off). Low
  priority: the minimap viewport, the completion list's position, the
  status line's line count and the left bar's marks assume one screen
  row per line, and how the class counts wrapped rows must be tried on
  the Amiga first.
- Running external tools such as linters and formatters.
- Scout parsers for ACE BASIC, Free Pascal and Amiga E.
- Git through am-git (<https://github.com/anderskjeldsen/am-git>, a native
  client written in AmLang, on Aminet; https needs AmiSSL, ssh an external
  client). For 1.0, strictly optional: TinyDev only runs
  the command if it is there and does not link or ship it (the repository
  has no licence file). The steps, each useful alone:
  1. Done (`git.c`): `am-git init` from the New Project wizard and from
     Git/Create Repository...; no `.gitignore` is written (whether `.info`
     files belong in the repository is the user's choice, and templates
     may bring one).
  2. Done (`git.c`, `gitstatus.c`, `DIRTREE_PenHook`): the status in a
     process of its own, coloured entries in the tree, the branch from
     `.git/HEAD` in the status line.
  3. Done (`gitwin.c`, `linediff.c`): a modal commit window with the
     changed files and check boxes (`add`, for a deleted file
     `rm --cached`; taken back with `unstage`, at once, then the status
     anew), a one-line message, `commit -m`; a diff window with the
     changes since the last commit, made by TinyDev itself.
  4. Fetch, pull and push with their output in the output window; remotes
     through `am-git config` (it has no `remote` command).

  Tried on the Amiga with am-git 0.12.1 (14.11.2026):
  - `status --porcelain` prints only `XY path` lines (index, work tree:
    `A ` added, ` M` changed, `??` untracked), with no `## branch` line;
    the branch is the first line of plain `status` ("On branch main").
  - File names come through as the bytes on disk (ISO-8859-1 `ö` as
    0xF6), not as UTF-8: TinyDev needs no conversion, but such names
    look wrong in other git clients.
  - `diff` shows the work tree against the index only (a staged new file
    is missing) and has no path argument; an untracked file may show a
    header without a hunk.
  - `commit -m` takes one line: neither `*N` nor `\n` makes a line
    break, so the commit window asks for a single line.
  - From am-git's sources (0.12.3): `diff` reads the work tree under
    `"./" + path`, which AmigaOS does not know, so every working file
    looks empty (a changed file shows only removed lines, a new one only
    its header). TinyDev takes HEAD's file from `show --text-blob
    HEAD:path` (0.12.3: a first line `text`, `binary` or `missing`) or
    `show HEAD:path` (0.12.1: the bare contents) and diffs it with the
    file on disk. A file of the index cannot be printed, so staged and
    not staged changes are not told apart.
  - `rm --cached` drops the index entry: for a tracked file that stages
    its deletion. `unstage <path>` (0.12.1) puts back HEAD's entry, or
    drops a new one. `add` does not stage a deletion. am-git ends with
    return code 0 even when a command fails ("add: failed"), so TinyDev
    reads the status afterwards instead of trusting the code.

  Still to try: how long `status` takes on a real project on a 68030
  (0.12.3 made it much faster, but it walks the tree). am-git wants a stack of
  100000 bytes, more than the 65536 of the Build scripts. Credentials stay
  with am-git (`ENV:AM_GIT_NETRC`); TinyDev stores none. Pull and checkout
  change files that may be open, so documents must be compared with the
  disk afterwards.
- Claude in TinyDev, after the first release and strictly optional:
  TinyDev talks to the Claude API itself (HTTPS over `bsdsocket.library` and
  AmiSSL, `src/json.c`), as a separate part like Scout (a process of its
  own, an ARexx port) with a tool window. A simple first stage takes the
  selection or the file as context; tools can come later. A bridge running
  `claude -p` on a host was the alternative considered. Every user needs an
  API key of their own, which costs extra. Between ISO-8859-1 and UTF-8 it
  would use `codesets.library`
  (<https://aminet.net/package/util/libs/codesets-6.22>, not looked at yet)
  instead of a conversion of its own.
