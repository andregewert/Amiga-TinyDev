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
- `src/ui.c`: ReAction window, menus, status line, event loop, and editor
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
- `src/prefswin.c`: the settings window (categories Window, Colours, Toolbar),
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
  `src/scout/scout_json.c`) and finds a bookmark's line again in a changed
  text by its stored line text; no Amiga headers.
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
  the built-in entry symbols. It watches the root and the expanded
  directories with `StartNotify()` (port and signal of the creating task,
  `DIRTREE_SignalMask`); the application answers the signal with
  `DIRTREE_Refresh`, which reconciles only the changed directories.
- `src/classes/minimap/`: `minimap.gadget`, the scaled overview of a
  `texteditor.gadget` with its own render process (gadgetclass subclass).
- `src/minimap.c`: connects `minimap.gadget` to the editor (active document,
  syntax colour hook, re-render requests, scrolling the editor on minimap
  clicks and drags).
- `src/syntax.c`: language detection, scanners, and TextEditor highlighting hook.
- `src/scout/`: Scout, the source analysis commodity (`TinyDev-Scout`, ARexx
  port `TINYDEV_SCOUT`), and its JSON/JSON-RPC code. Scout talks to the parsers
  with the Scout protocol, which is modeled on LSP but is not LSP.
- `src/parsers/`: standalone language parsers speaking the Scout protocol over
  STDIN/STDOUT.
- `include/editor.h`: shared application types and cross-module declarations.
- `include/syntax.h`: portable syntax-scanner interface.
- `tests/test_syntax.c`: host-native syntax scanner tests.
- `tests/test_search.c`: host-native tests of the search engine.
- `tests/test_bmstore.c`: host-native tests of the bookmark store.
- `tests/test_scout_json.c`, `tests/test_c_parser.c`: host-native tests for the
  Scout JSON code and the C parser.
- `amiga-devkit/`: git submodule (pinned tag) with shared Amiga tools and AI
  guidelines; see `CLAUDE.md`. Do not edit files inside the submodule.
- `external/regex/`: git submodule (tag `alpha3.8p1`), Henry Spencer's POSIX
  regex library for `src/search.c`. Its `regex.h` and `*.ih` headers are
  generated with its `mkh` script into `build/regex/`; it is compiled
  unchanged and without our warning flags. Do not edit it; its licence
  requires the credit in `README.md`.
- `tools/mkicons.py`: motifs of the Workbench icons (TinyDev, Scout, parsers)
  for `amiga-devkit/tools/amigaicons.py`; `make icons` writes `icons/`. The
  files in `icons/` are generated, never edited by hand.
- `README.md`: supported behavior, runtime dependencies, and known limits.

## Build and verification

The target compiler defaults to `/opt/amiga/bin/m68k-amigaos-gcc`.

```sh
make            # includes make charcheck (no UTF-8 in ISO-8859-1 files)
make test
/opt/amiga/bin/m68k-amigaos-objdump -f bin/TinyDev
```

- Run `make test` for all changes that affect the portable syntax scanner.
- Run `make` for changes to target code when the Amiga cross-toolchain is
  available.
- A successful host test does not validate ReAction or AmigaOS integration.
- Do not claim target-build verification if the cross-toolchain or SDK is
  unavailable; report that limitation explicitly.
- Finished Amiga binaries go to `bin/` (`bin/TinyDev`, `bin/TinyDev-Scout`,
  `bin/parsers/c_parser`, `bin/Classes/Gadgets/<name>.gadget`,
  `bin/Catalogs/<language>/TinyDev.catalog`), laid out as on
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
- Add documentation comments to functions, methods, and type declarations

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

## Change discipline

- Read the relevant module and shared headers before editing cross-module data
  structures or ReAction object ownership.
- Update `README.md` when supported features, requirements, or intentional
  limitations change.
- Add or extend host tests for portable scanner behavior and edge cases.
- Do not overwrite unrelated working-tree changes or commit generated binaries
  and object files unless the task explicitly requires them.
