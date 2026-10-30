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
  which reads only the changed directories. Project/Refresh Folder Tree
  reads all shown directories, for file systems without notification.
- **minimap.gadget** reads the editor and starts a render job only when
  `minimap.c` sets `MINIMAP_Update`: after every batch of input events and
  when its signal arrives. Clicks and drags come back as notifications
  (`MINIMAP_Top`, `MINIMAP_Dragging`) and TinyDev scrolls the editor. The
  colours come from a per-line hook that runs TinyDev's syntax scanner on
  the document, so a document is disconnected
  (`minimap_forget_document()`) before its editor is disposed.

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

## Editor colours and texteditor.gadget

TinyDev's syntax colours are real screen pens (`ObtainBestPen`), handed to
the highlighter hook (`syntax.c`) and to the minimap's colour hook. The
OS 3.2 `texteditor.gadget` paints its text area with the screen's
`BACKGROUNDPEN` and ignores `GA_BackFill` and `GA_DrawInfo` there;
`GA_TEXTEDITOR_Pen`/`GA_TEXTEDITOR_ColorMap` are not implemented and
`GA_TEXTEDITOR_Transparent` is OS4 only (details in `ui.c`). The minimap
uses the same background pen so both look alike.

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
  (Build/Project Settings...): lists of Shell commands for the actions Build, Run and
  Debug, each with a default (format in `include/projconf.h`). The file is
  meant to be shared (checked in with the project), so nothing personal of
  a session goes into it; writing keeps the keys TinyDev does not know,
  also those of the entries; `build.c` runs the default commands (Build in
  a process of its own with its output in the output window of
  `outwin.c`, Run and Debug detached; details in its file comment);
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
