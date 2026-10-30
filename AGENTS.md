# AGENTS.md

## Project overview

TinyDev is a native AmigaOS 3.2 multi-document text editor written in C. It
uses ReAction classes and is compiled for the 68000 with soft-float and no
ixemul, but its users run it on larger Amigas (at least a 68030 at 50 MHz,
more likely a 68040 or 68060); weigh run-time costs against those.
The codebase intentionally handles file contents as 8-bit text.

The directory opened in the folder tree is the project: features that refer
to "the project" (for example searching all project files) work on the files
below the tree's root.

## Repository layout

- `src/main.c`: library lifecycle, CLI/Workbench startup, and application entry.
- `src/ui.c`: ReAction window, menus, status line, event loop, iconifying,
  and editor
  colors.
- `src/toolbar.c`: the speedbar toolbar (modes IMAGES/BOTH/TEXT, ghosted
  buttons with lazily loaded `_g` images, overflow chooser, help bubbles),
  built like Kickdown's.
- `src/splash.c`: splash window during start-up (Intuition/graphics only, opened
  before the classes) and the About window, both as in Kickdown.
- `src/locale.c`: `locale.library` and the catalog; `S(MSG_...)` returns a
  text in the user's language. The built-in English texts are
  `catalogs/TinyDev.cd`, translations `catalogs/<language>.ct` (German:
  `deutsch.ct`); `amiga-devkit/tools/catcomp.py` makes
  `include/tinydev_strings.h` (generated, in git) and
  `bin/Catalogs/<language>/TinyDev.catalog`.
- `src/settings.c`: settings from the tool types of the program icon (and of
  project icons on a Workbench start); writes them back into the program icon.
- `src/prefswin.c`: the settings window (categories General with the groups
  Window and View, Colours, Toolbar, Advanced with Scout and the drawer of
  the temporary files),
  built like Kickdown's.
- `src/find.c`: the find and replace window (non-modal, built like
  Kickdown's; scope: current document or all open documents); it searches
  the text exported from the editor with `src/search.c` and marks/replaces
  hits with `GM_TEXTEDITOR_MarkText` and `GM_TEXTEDITOR_Replace`. Editors on
  hidden pages get these methods without a GadgetInfo (no drawing). The
  project-file search runs step by step in the event loop (the window
  signals itself), lists its hits in a listbrowser and replaces in tabs.
- `src/bookmarks.c`: bookmarks, using those of `texteditor.gadget` (ARexx
  `SETBOOKMARK TOGGLE`, `CLEARBOOKMARKS`, `LISTBOOKMARKS`: they move with
  the text, and the editor draws their marks); TinyDev caches the lines per
  document, moves the cursor to the next/previous one itself (`GOTOBOOKMARK`
  leaves the cursor out of sight) and uses an empty left bar
  (`GA_TEXTEDITOR_LeftBarHook`) as the area where a click toggles a
  bookmark. Commands go to the editor with the window (it draws the change
  itself). After a menu pick, `ActivateLayoutGadget()` gives the editor the
  input focus back; it takes the gadget object, not its ID.
- `src/bmstore.c`, `include/bmstore.h`: the portable store of a project's
  bookmarks: reads and writes `<project>/.tinydev/bookmarks.json` (with
  `src/json.c`) and finds a bookmark's line again in a changed
  text by its stored line text; no Amiga headers.
- `src/projconf.c`, `include/projconf.h`: the portable reader and writer
  of a project's settings (`<project>/.tinydev/project.json`): the Shell
  commands of the actions Build, Run and Debug, the default of each, and
  the placeholders `{project}`, `{file}`, `{base}`; writing keeps the keys
  it does not know; no Amiga headers.
- `src/projdata.c`: reading and writing the files of
  `<project>/.tinydev/` (whole files; writes through a temporary file,
  making the directory when needed), for bookmarks and project settings.
- `src/projwin.c`: the modal project settings window
  (Build/Project Settings...), built like the settings window: a page per
  action with a listbrowser of its commands (checkbox = default), buttons
  beside the page, and a modal window to edit one command; "Save" writes
  `project.json` with `projconf_format()`.
- `src/build.c`: the Build menu's actions: the default command of Build,
  Run or Debug from `project.json`, written as a script to `T:`
  (placeholders replaced, "Stack 65536" first). Build runs in a process
  of its own (synchronous `SystemTags()`, return code by message) with
  its output in a file in `T:` read every 0.25 s (timer.device) into the
  output window; Stop sends Ctrl-C to its Shell. Run and Debug start
  detached in a console window.
- `src/scoutlink.c`: TinyDev as a client of TinyDev-Scout (ARexx
  messages answered to a port of the event loop): starts Scout when
  needed, a session per project (the tree's root), analyses documents
  when loaded or saved and, as a snapshot in the temporary drawer, after a
  pause in typing; the counts go to the status line.
- `src/symlist.c`: the symbol list, a tool window (View/Symbol List):
  the symbols of the active document or of the project from Scout
  (through `scoutlink.c`), filter, Go to (`document_go_to()`).
- `src/diag.c`: the diagnostics of the documents from Scout: the list per
  document and the marks in the editor's left bar (drawn by the left bar
  hook of `bookmarks.c`; a fixed array changed under `Forbid()`).
- `src/diaglist.c`: the problem list, a tool window (View/Problem List):
  the diagnostics of the active document or of the project, Go to.
- `src/navigate.c`: Edit/Go to Definition (DEFINITION with the name at
  the cursor, cycling through several) and Edit/Go Back (a history of
  places).
- `src/complete.c`: Edit/Complete: COMPLETE with the prefix at the
  cursor, a borderless list window at the cursor that takes the keys
  (choose, insert with MarkText/Replace, narrow while typing).
- `src/tempfile.c`: unique names of temporary files in the drawer of the
  setting TEMPDIR (`temp_name()`), used by the Build menu and Scout's
  snapshots.
- `src/outwin.c`: the output window, a tool window (View/Output Window,
  first placed below the main window, as wide as it): the build's lines
  (kept also while it is closed, at most 2000), status line, Stop, Clear.
- `src/toolwin.c`: placement of tool windows (remembered box pushed into
  the screen, otherwise docked right of the main window or over its right
  edge). Their boxes are kept for the session (`EditorApp`); only
  View/Snapshot Tool Windows writes them, with the open state, into the
  program icon (`settings_save_toolwindows()`), not the settings window.
- `src/bmlist.c`: the bookmark list, a tool window (scope chooser,
  listbrowser with a render hook drawing a document or ribbon symbol per
  row); rebuilt when `bookmarks_generation()` changes.
- `src/filescan.c`: step-by-step walk through the files of a directory tree
  (bounded depth, no links, no icons, AmigaDOS name pattern) for the
  project-file search.
- `src/search.c`, `include/search.h`: the portable search engine (plain
  text and POSIX extended regular expressions, Latin-1 case folding, whole
  words, line by line, replacement expansion with `\0`-`\9`); no Amiga
  headers.
- `src/document.c`: document/tab lifecycle, scrollbar synchronization, and
  scroll-time highlighting suspension.
- `src/help.c`: Project/Help... and the toolbar's Help button: starts
  `PROGDIR:HelpViewer` on `PROGDIR:Help` with `PORTNAME TinyDev`,
  asynchronously with `SystemTags()` (`SYS_Asynch`), so the HelpViewer is
  independent of TinyDev and there is one viewer for TinyDev
  (`OpenWorkbenchObjectA()` cannot pass options).
- `src/fileio.c`: loading, line-ending preservation, requesters, and safe saves.
- `src/tree.c`: connects `dirtree.gadget` to the editor (root selection,
  opening files, column visibility).
- `src/classes/`: TinyDev's own ReAction gadget classes, each a standalone class
  library for `SYS:Classes/Gadgets/` (`src/classes/<name>/`, with
  `class_config.h` for name and version). `src/classes/common/classlib.c` is the
  shared library frame. Public headers are `include/gadgets/<name>.h`;
  `sfd/<name>_lib.sfd` generates `include/proto`, `include/inline` and
  `include/clib` with `sfdc`.
- `src/classes/dirtree/`: `dirtree.gadget`, the lazy, bounded directory tree
  (listbrowser subclass); `dirtree_icons.c` is its private image class with
  the built-in entry symbols; `dirtree_sort.c` the portable natural order
  (host-tested in `tests/test_dirtree_sort.c`). Options: file pattern,
  name hook (shown names), sort order, hiding directories without shown
  files. It watches the root and the expanded
  directories with `StartNotify()` (port and signal of the creating task,
  `DIRTREE_SignalMask`); the application answers the signal with
  `DIRTREE_Refresh`, which reconciles only the changed directories.
- `src/classes/minimap/`: `minimap.gadget`, the scaled overview of a
  `texteditor.gadget` with its own render process (gadgetclass subclass).
- `src/minimap.c`: connects `minimap.gadget` to the editor (active document,
  syntax colour hook, re-render requests, scrolling the editor on minimap
  clicks and drags).
- `src/syntax.c`: language detection, the built-in scanners, the
  combined scan (`syntax_scan()`: Scout's lexical rules or the built-in
  scanner, then Scout's tokens) and the TextEditor highlighting hook.
- `src/rules.c`, `include/rules.h`: the lexical rules of a language from
  Scout (SYNTAX) and the generic scanner with them; portable,
  host-tested in `tests/test_rules.c`.
- `src/tokenmap.c`, `include/tokenmap.h`: Scout's highlighting tokens of
  a document, found again by the text of a line; portable, host-tested.
- `src/scout/`: Scout, the source analysis commodity (`TinyDev-Scout`, ARexx
  port `TINYDEV_SCOUT`): `scout_main.c` (settings, main loop),
  `scout_broker.c` (Exchange), `scout_rexx.c` (the commands, ReadArgs
  templates, JSON results), `scout_jobs.c` (parser runs: queue, a process
  per run, temporary files, timeout), `scout_walk.c` (project walk,
  includes), `scout_watch.c` (the project's drawers watched with
  `StartNotify()`, the index brought up to date), `scout_window.c` (its
  window: state, sessions, parsers; texts from its own catalog
  `src/scout/catalogs/`, `scout_strings.h` generated and checked in),
  `scout_store.c` with `include/scout_store.h` (the portable
  core: parsers, languages, sessions, results, the answers;
  host-tested in `tests/test_scout_store.c`), `scout_rpc.c` with
  `include/scout_rpc.h` (JSON-RPC framing). Scout talks to the parsers
  with the Scout protocol, which is modeled on LSP but is not LSP. Its
  design is `src/scout/docs/Scout.md`, the parser protocol
  `src/scout/docs/Scout-Protocol.md`. Scout and the parsers must not
  depend on TinyDev, so they can move to a repository of their own.
- `src/json.c`, `include/json.h`: the portable JSON code (values, reader,
  accessors, building and changing trees, writer, indented output of a
  tree) of TinyDev, Scout and the parsers; strings stay 8-bit as read
  (ISO-8859-1), numbers are written back as read; no stdio, no Amiga
  headers.
- `src/helpviewer/`: HelpViewer (`bin/HelpViewer`), a small viewer for a
  directory of HTML help pages: dirtree.gadget on the left (HTML files
  only, shown by their titles, natural order, index pages only via their
  directory, directories without pages hidden, the home page's title as
  header), html.gadget/htmlttf.gadget on the right, a text toolbar (Home,
  Back, Forward, Open, Print), the status line at the bottom. With `PORTNAME`
  one viewer runs per name: a later start with that name passes its
  location through the public port `HELPVIEWER.<NAME>` (`OpenMessage`) and
  ends; without it there is no port and every start is a viewer of its
  own. `printwin.c` prints the page shown (taken over from Kickdown:
  printer.device as graphics, PostScript or PDF through html.gadget 1.2,
  options as tool types); `helpviewer.h` is what its modules share.
  `htmltitle.c` is
  portable (titles, links; host-tested in `tests/test_htmltitle.c`); its
  catalogs are `src/helpviewer/catalogs/` (`helpviewer_strings.h` is
  generated and checked in); `sample/` is a small help directory for
  testing. It is kept self-contained so it can move to its own repository.
- `src/parsers/`: standalone language parsers speaking the Scout protocol over
  STDIN/STDOUT (to move to `src/scout/parsers/`). The C parser is
  `c_parser.c` (the portable, heuristic analysis: lexer, preprocessor
  lines, declarations, highlighting tokens) and `c_parser_rpc.c`
  (`scout/describe`, `scout/analyze`, the program);
  `include/scout_protocol.h` holds the protocol's numbers (kinds,
  severities, error codes) for Scout and all parsers.
- `include/editor.h`: shared application types and cross-module declarations.
- `include/syntax.h`: portable syntax-scanner interface.
- `tests/test_syntax.c`: host-native syntax scanner tests.
- `tests/test_search.c`: host-native tests of the search engine.
- `tests/test_bmstore.c`: host-native tests of the bookmark store.
- `tests/test_projconf.c`: host-native tests of the project settings.
- `tests/test_dirtree_sort.c`: host-native tests of dirtree.gadget's natural order.
- `tests/test_htmltitle.c`: host-native tests of the HelpViewer's title and link helpers.
- `tests/test_json.c`, `tests/test_scout_rpc.c`, `tests/test_c_parser.c`:
  host-native tests for the JSON code, Scout's JSON-RPC helpers and the C
  parser (with the sample sources in `tests/data/c_parser/`, ISO-8859-1).
- `amiga-devkit/`: git submodule (pinned tag) with shared Amiga tools and AI
  guidelines; see `CLAUDE.md`. Do not edit files inside the submodule.
- `external/html_gadget/`: git submodule (tag `v1.2`), html.gadget, for
  the headers the HelpViewer is built with (the class itself is installed
  separately). Do not edit it.
- `external/regex/`: git submodule (tag `alpha3.8p1`), Henry Spencer's POSIX
  regex library for `src/search.c`. Its `regex.h` and `*.ih` headers are
  generated with its `mkh` script into `build/regex/`; it is compiled
  unchanged and without our warning flags. Do not edit it; its licence
  requires the credit in `README.md`.
- `tools/mkicons.py`: motifs of the Workbench icons (TinyDev, Scout, parsers,
  HelpViewer)
  for `amiga-devkit/tools/amigaicons.py`; `make icons` writes `icons/`. The
  files in `icons/` are generated, never edited by hand.
- `package/`: what the Aminet archive needs besides the build:
  `TinyDev.readme` (the Aminet readme, ISO-8859-1, at most 78 columns) and
  `help/<language>/` (TinyDev's help pages, HTML in ISO-8859-1 with
  entities; language drawers named like the Amiga locales, the same file
  names in every language; `make` copies them to `bin/Help/`).
- `tools/mkdist.py`: `make dist` builds `dist/TinyDev.lha` from `bin/`,
  the README, the sources (built without git) and icons, with
  `amiga-devkit/tools/distlib.py`.
- `examples/HelloWorld/`: a demo project for the Build menu (hello.c, a
  Makefile for vbcc, error.c that fails on purpose, and its
  `.tinydev/project.json`); ISO-8859-1 like the sources.
- `README.md`: for users: features, requirements, usage, tool types, limits
  and building, kept short.
- `TECHNICAL.md`: notes that concern several modules (event flow between
  TinyDev and its classes, scrolling and highlighting, editor colours and
  texteditor.gadget, the project, help and HelpViewer, class loading, build
  layout). Not loaded every session: read the section named in "Technical
  notes" below before such a change.

## Build and verification

The target compiler defaults to `/opt/amiga/bin/m68k-amigaos-gcc`.

```sh
make            # includes make charcheck (no UTF-8 in ISO-8859-1 files)
make test
/opt/amiga/bin/m68k-amigaos-objdump -f bin/TinyDev
```

- Run `make test` for all changes that affect the portable syntax scanner.
- Run `make docs` (and `make docs-<class>` for a class's public header) when
  comments or interfaces change; it must finish without Doxygen warnings
  (rules: `amiga-devkit/docs/Dokumentation.md`).
- Run `make` for changes to target code when the Amiga cross-toolchain is
  available.
- A successful host test does not validate ReAction or AmigaOS integration.
- Do not claim target-build verification if the cross-toolchain or SDK is
  unavailable; report that limitation explicitly.
- Finished Amiga binaries go to `bin/` (`bin/TinyDev`, `bin/TinyDev-Scout`,
  `bin/parsers/c_parser`, `bin/Classes/Gadgets/<name>.gadget`,
  `bin/Catalogs/<language>/TinyDev.catalog`, `bin/HelpViewer`,
  `bin/Help/<language>/`), laid out as on
  the Amiga; nothing else goes there. All intermediate files (objects,
  dependency files, `.debug` files, host test binaries) go to `build/`; gadget
  class objects to `build/classes/<name>/`.
- `bin/` is used on the Amiga (case-insensitive file system): never create
  paths in it that differ only in case.
- `make clean` removes `build/` and `bin/`. Do not run it when unrelated
  generated files need to be preserved (e.g. the user's icon `bin/TinyDev.info`
  with its tool types).

## Coding conventions

- Keep code compatible with the Makefile's GNU C99 target and warning flags.
- Match the existing style: four-space indentation, braces on the next line for
  functions, braces on the same line for control flow, and `snake_case` names.
- Keep module-private helpers `static`; declare shared APIs in the appropriate
  header.
- Use AmigaOS types where required by platform APIs and standard C types in the
  portable scanner.
- Check allocations, library/class availability, DOS I/O results, and object
  creation failures. Preserve established cleanup ordering on error paths.
- Avoid adding Unix/POSIX runtime assumptions to target code. Host-only code
  must remain guarded (for example, with `SYNTAX_HOST_TEST`).
- Maintain the current 68000, 8-bit-text, and AmigaOS 3.2 compatibility unless
  a task explicitly changes those requirements.
- Document every file (`@file`), function (static ones too), type, field,
  enumerator and interface macro with Doxygen comments in Javadoc style
  (`/** @brief ... @param ... @return ... */`, `/**< ... */` for fields).
  Functions declared in a header are documented there, not again at the
  definition. Public class interfaces are documented in
  `include/gadgets/<name>.h` (attributes with type, applicability and
  default, grouped with `@name`); there are no hand-written Autodocs.

## Behavioral constraints

- Every text TinyDev shows comes from the catalog: add it to
  `catalogs/TinyDev.cd` and translate it in `catalogs/deutsch.ct`, use it as
  `S(MSG_...)`. Keep the format specifiers in translations and the `_`
  shortcuts unique per window in every language. Strings may be reordered
  until the first release (no catalog version yet).

- Project data goes to the visible directory `<project>/.tinydev/` (made
  when first needed), as files the user may edit by hand: JSON in
  ISO-8859-1, indented, written through a temporary file. A file that
  cannot be read is reported and never overwritten.
- Preserve LF, CR, and CRLF line endings when loading and saving documents.
- Keep saves temporary-file based so a failed write does not destroy the
  original file.
- Keep duplicate-path detection and tab activation behavior intact.
- Directory traversal must remain lazy and bounded to 16 levels and 2048
  visible entries (the `dirtree.gadget` defaults); do not eagerly scan complete
  subtrees.
- Gadget classes in `src/classes/` must stay usable by other applications: no
  TinyDev-specific code or headers, no DOS calls from input handling
  (GM_HANDLEINPUT, notifications), only the library frame's C runtime
  (libnix string functions, no stdio), and a documented public header.
  Version numbers (`LIBVERSION`/`LIBREVISION`/`LIBDATE` in `class_config.h`,
  `$VER` in the public header) change only deliberately for a release, never
  for ordinary changes: during development TinyDev loads the class from the
  build directory. The first release is 1.0.
- A subclass of a ReAction gadget must redraw itself after OM_SET/OM_UPDATE
  when the superclass returns non-zero (the superclass only renders for
  objects of its own class).
- Toolbar images may be unavailable and must retain their text-button fallback.
- The editor control always uses the default screen font; there is no font
  selection.
- Syntax scanning and the search engine (`src/search.c`) must remain usable
  independently of Amiga headers so the host test binaries can compile.
- Syntax highlighting is suspended while a scrollbar is actively dragged and
  must be restored (with a redraw) when the drag ends. The end is the
  scroller's GADGETUP or two IntuiTicks without a scroll signal (the scroller
  sends no GADGETUP when the button is released outside it).
- `minimap.gadget` renders in its own process and only while it has a size;
  no rendering happens when the column is hidden. Updates must stay coalesced
  (a single outstanding job); the gadget stops and joins its process in
  OM_DISPOSE. Its colour hook reads the document, so TinyDev disconnects a
  document (`minimap_forget_document()`) before the document's editor is
  disposed.

## Technical notes

Read the section of `TECHNICAL.md` before changing:

- tree.c, minimap.c or the event loop's handling of the classes' signals:
  "Event flow between TinyDev and its classes";
- scrolling, the highlighter hook or the minimap viewport: "Scrolling,
  highlighting and the minimap viewport";
- editor pens, backfill or colour settings: "Editor colours and
  texteditor.gadget";
- the find window's project search, bookmarks or `.tinydev/`: "The
  project";
- help.c, the HelpViewer's start options or its port: "Help: TinyDev,
  HelpViewer and the help pages";
- how classes are opened, or `bin/`, `make dist`: "Classes" and "Build
  layout";
- iconifying, or a new tool window: "Iconifying and the tool windows".

## Change discipline

- Read the relevant module and shared headers before editing cross-module data
  structures or ReAction object ownership.
- Documentation goes where it is read (`amiga-devkit/docs/Dokumentation.md`,
  "Wohin welches Wissen gehört"): a short entry in `README.md` for what
  users see (feature bullet, tool type row, limit); module details in the
  module's Doxygen comments; what spans modules in `TECHNICAL.md`. No
  implementation details in the README.
- Add or extend host tests for portable scanner behavior and edge cases.
- Do not overwrite unrelated working-tree changes or commit generated binaries
  and object files unless the task explicitly requires them.
