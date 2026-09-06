# AGENTS.md

## Project overview

AmiEditor is a native AmigaOS 3.2 multi-document text editor written in C. It
uses ReAction classes and targets 68000 systems with soft-float and no ixemul.
The codebase intentionally handles file contents as 8-bit text.

## Repository layout

- `src/main.c`: library lifecycle, CLI/Workbench startup, and application entry.
- `src/ui.c`: ReAction window, menus, toolbar, status line, event loop, and
  editor colors.
- `src/document.c`: document/tab lifecycle, scrollbar synchronization, and
  scroll-time highlighting suspension.
- `src/fileio.c`: loading, line-ending preservation, requesters, and safe saves.
- `src/tree.c`: lazy, bounded directory tree loading.
- `src/minimap.c`: background render task, message-port handshake, and the
  off-screen colored minimap of the active document.
- `src/syntax.c`: language detection, scanners, and TextEditor highlighting hook.
- `include/editor.h`: shared application types and cross-module declarations.
- `include/syntax.h`: portable syntax-scanner interface.
- `tests/test_syntax.c`: host-native syntax scanner tests.
- `README.md`: supported behavior, runtime dependencies, and known limits.

## Build and verification

The target compiler defaults to `/opt/amiga/bin/m68k-amigaos-gcc`.

```sh
make
make test
/opt/amiga/bin/m68k-amigaos-objdump -f AmiEditor
```

- Run `make test` for all changes that affect the portable syntax scanner.
- Run `make` for changes to target code when the Amiga cross-toolchain is
  available.
- A successful host test does not validate ReAction or AmigaOS integration.
- Do not claim target-build verification if the cross-toolchain or SDK is
  unavailable; report that limitation explicitly.
- `make clean` removes the target binary, target objects/dependency files, and
  the host test binary. Do not run it when unrelated generated-file changes
  need to be preserved.

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
- Add documentation comments to functions, methods and type declarations

## Behavioral constraints

- Preserve LF, CR, and CRLF line endings when loading and saving documents.
- Keep saves temporary-file based so a failed write does not destroy the
  original file.
- Keep duplicate-path detection and tab activation behavior intact.
- Directory traversal must remain lazy and bounded to 16 levels and 2048
  visible entries; do not eagerly scan complete subtrees.
- Toolbar images may be unavailable and must retain their text-button fallback.
- The editor control always uses the default screen font; there is no font
  selection.
- Syntax scanning must remain usable independently of Amiga headers so the host
  test binary can compile.
- Syntax highlighting is suspended while a scrollbar is actively dragged and
  must be restored (with a redraw) when the drag ends.
- The minimap renders in a separate task and only while it is visible; no
  rendering happens when it is hidden. Updates must stay coalesced (a single
  outstanding job) and the render task must be stopped and joined before the
  window (and its `space.gadget`) is disposed.

## Change discipline

- Read the relevant module and shared headers before editing cross-module data
  structures or ReAction object ownership.
- Update `README.md` when supported features, requirements, or intentional
  limitations change.
- Add or extend host tests for portable scanner behavior and edge cases.
- Do not overwrite unrelated working-tree changes or commit generated binaries
  and object files unless the task explicitly requires them.
