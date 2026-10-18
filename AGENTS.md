# AGENTS.md

## Project overview

TinyDev is a native AmigaOS 3.2 multi-document text editor written in C. It
uses ReAction classes and targets 68000 systems with soft-float and no ixemul.
The codebase intentionally handles file contents as 8-bit text.

## Repository layout

- `src/main.c`: library lifecycle, CLI/Workbench startup, and application entry.
- `src/ui.c`: ReAction window, menus, status line, event loop, and editor
  colors.
- `src/toolbar.c`: the speedbar toolbar (modes IMAGES/BOTH/TEXT, ghosted
  buttons with lazily loaded `_g` images, overflow chooser, help bubbles),
  built like Kickdown's.
- `src/splash.c`: splash window during start-up (Intuition/graphics only, opened
  before the classes) and the About window, both as in Kickdown.
- `src/settings.c`: settings from the tool types of the program icon (and of
  project icons on a Workbench start).
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
  (listbrowser subclass).
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
- `tests/test_scout_json.c`, `tests/test_c_parser.c`: host-native tests for the
  Scout JSON code and the C parser.
- `amiga-devkit/`: git submodule (pinned tag) with shared Amiga tools and AI
  guidelines; see `CLAUDE.md`. Do not edit files inside the submodule.
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
  `bin/parsers/c_parser`, `bin/Classes/Gadgets/<name>.gadget`), laid out as on
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
- Syntax scanning must remain usable independently of Amiga headers so the host
  test binary can compile.
- Syntax highlighting is suspended while a scrollbar is actively dragged and
  must be restored (with a redraw) when the drag ends.
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
