## 1. Goal

Prevent the bebbo runtime from trying to open the nonexistent `window.library` before `main()), while retaining the application’s correct explicit opening of the installed `window.class`.

## 2. Approach

Make every library/class base managed by the application a strong, explicitly zero-initialized definition in [main.c](air-file://rqrbrek0ib860onkrlsi/home/agewert/Dokumente/AmiTest/src/main.c?type=file&root=%252F), and compile with `-fno-common` in [Makefile](air-file://rqrbrek0ib860onkrlsi/home/agewert/Dokumente/AmiTest/Makefile?type=file&root=%252F). With the current tentative/common definitions, the linker selects the strong WindowBase object from `libstubs.a`; that object registers `window.library` in the runtime library list even though [main.c](air-file://rqrbrek0ib860onkrlsi/home/agewert/Dokumente/AmiTest/src/main.c?type=file&root=%252F) correctly calls `OpenLibrary("window.class", 47)`.

This fix preserves the existing manual open/close lifecycle and avoids replacing the ReAction APIs or lowering their required versions. A binary-content check will make the exact startup regression detectable without needing an Amiga installation.

## 3. File Changes

- **Modify — [main.c](air-file://rqrbrek0ib860onkrlsi/home/agewert/Dokumente/AmiTest/src/main.c?type=file&root=%252F):** change AslBase, DiskfontBase, GadToolsBase, IconBase, UtilityBase, WindowBase, LayoutBase, ClickTabBase, and TextFieldBase from tentative common definitions to explicit strong null-initialized definitions; retain `window.class` as the explicit class name and the existing reverse-order cleanup.
- **Modify — [Makefile](air-file://rqrbrek0ib860onkrlsi/home/agewert/Dokumente/AmiTest/Makefile?type=file&root=%252F):** add `-fno-common` and a read-only `check-binary` target that verifies the output contains `window.class` but not the erroneous exact string `window.library`.
- **Modify — [README.md](air-file://rqrbrek0ib860onkrlsi/home/agewert/Dokumente/AmiTest/README.md?type=file&root=%252F):** document the bebbo `libstubs.a` class-base issue, the new verification target, and how to distinguish this startup-linkage problem from a genuinely missing or too-old ReAction class.

No files are created or deleted.

## 4. Implementation Steps

### Task 1: Remove the accidental runtime dependency

1. In [main.c](air-file://rqrbrek0ib860onkrlsi/home/agewert/Dokumente/AmiTest/src/main.c?type=file&root=%252F), initialize both groups of application-owned base pointers with `= NULL`. This emits strong definitions instead of GCC 6 common symbols, so the archive members in `libstubs.a` are not selected to define those bases.
2. Keep the explicit `open_one(&WindowBase, "window.class", 47)` call in [main.c](air-file://rqrbrek0ib860onkrlsi/home/agewert/Dokumente/AmiTest/src/main.c?type=file&root=%252F). Keep opening `gadgets/layout.gadget`, `gadgets/clicktab.gadget`, and `gadgets/texteditor.gadget` manually as well.
3. Preserve [main.c](air-file://rqrbrek0ib860onkrlsi/home/agewert/Dokumente/AmiTest/src/main.c?type=file&root=%252F)’s null checks and reverse close order. The change must not introduce a second owner or let runtime destructors close a manually managed class base.

### Task 2: Make the linker behavior explicit and testable

4. Add `-fno-common` to target `CFLAGS` in [Makefile](air-file://rqrbrek0ib860onkrlsi/home/agewert/Dokumente/AmiTest/Makefile?type=file&root=%252F). This turns any future accidental tentative library-base definition into a normal strong BSS definition rather than silently allowing a stub archive object to override it.
5. Add a `check-binary` target in [Makefile](air-file://rqrbrek0ib860onkrlsi/home/agewert/Dokumente/AmiTest/Makefile?type=file&root=%252F) that depends on AmiEditor, confirms an exact `window.class` string exists, and fails if an exact `window.library` line exists in `strings` output.
6. Make the default verification workflow run both the existing host syntax tests and the new binary dependency check, without executing the target binary on the host.
7. Inspect the rebuilt main object and final binary with the cross-toolchain `nm`: the application-managed base symbols must be defined by [main.c](air-file://rqrbrek0ib860onkrlsi/home/agewert/Dokumente/AmiTest/src/main.c?type=file&root=%252F), and no `window.o` class stub dependency may contribute `window.library`.

### Task 3: Document diagnosis and target validation

8. Update [README.md](air-file://rqrbrek0ib860onkrlsi/home/agewert/Dokumente/AmiTest/README.md?type=file&root=%252F) with `make check-binary` and explain that the prior error happened before `main()), because bebbo’s default `libstubs.a` maps WindowBase to `window.library`.
9. In [README.md](air-file://rqrbrek0ib860onkrlsi/home/agewert/Dokumente/AmiTest/README.md?type=file&root=%252F), retain the actual AmigaOS requirement: V47 `window.class` must be reachable through the target’s library/class search path. Explain that after this fix, a reported `window.class` failure reflects the explicit `OpenLibrary` version/path check rather than the phantom `window.library` dependency.

## 5. Acceptance Criteria

1. A clean cross-build succeeds with `-fno-common` and produces AmiEditor without new compiler or linker warnings.
2. `strings AmiEditor` contains an exact `window.class` line and contains no exact `window.library` line.
3. The rebuilt [main.o](air-file://rqrbrek0ib860onkrlsi/home/agewert/Dokumente/AmiTest/src/main.o?type=file&root=%252F) reports each application-owned class/library base as a defined BSS/data symbol rather than a common symbol.
4. On AmigaOS 3.2 with V47 `window.class` installed and reachable, AmiEditor reaches its own UI initialization instead of being aborted by runtime startup with a request for `window.library`.
5. [main.c](air-file://rqrbrek0ib860onkrlsi/home/agewert/Dokumente/AmiTest/src/main.c?type=file&root=%252F) still requests `window.class` version 47, and the layout, clicktab, and texteditor class names remain unchanged.
6. Closing the application still closes every manually opened base exactly once and leaves all base pointers null.
7. The existing syntax test suite continues to pass unchanged.

## 6. Verification Steps

1. Run `make clean && make` and verify all target source files compile with `-fno-common`.
2. Run `make test` and confirm the existing syntax tests pass.
3. Run `make check-binary`; confirm it succeeds only when `window.class` exists and `window.library` is absent.
4. Run `/opt/amiga/bin/m68k-amigaos-nm src/main.o` and verify WindowBase, LayoutBase, ClickTabBase, and TextFieldBase are strong BSS/data definitions, not `C` common symbols.
5. Run `strings AmiEditor | sort -u` and manually confirm the intended ReAction names: `window.class`, `gadgets/layout.gadget`, `gadgets/clicktab.gadget`, and `gadgets/texteditor.gadget`.
6. Copy the rebuilt binary to the affected AmigaOS 3.2 system and launch it from Shell and Workbench. Confirm that no `window.library` startup requester appears and the editor window opens.
7. Temporarily make `window.class` unavailable on a test installation and confirm the application’s explicit version-47 diagnostic is now the observed failure; restore it before normal use.

## 7. Risks & Mitigations

- **Other app-owned bases currently have the same common-symbol pattern:** their stub names happen to be valid, so the bug is less visible, but they can still be auto-opened before [main.c](air-file://rqrbrek0ib860onkrlsi/home/agewert/Dokumente/AmiTest/src/main.c?type=file&root=%252F) opens them manually. Mitigate by strongly defining all nine manually managed bases together, not WindowBase alone.
- **A real class search-path/version problem may remain on a particular target:** removing `window.library` does not make an inaccessible or pre-V47 `window.class` compatible. Mitigate by retaining the explicit class name/version diagnostic and documenting the distinction.
- **A future new Base global could recreate the collision:** GCC 6 defaults to common tentative definitions. Mitigate with project-wide `-fno-common` and the exact binary string regression check in [Makefile](air-file://rqrbrek0ib860onkrlsi/home/agewert/Dokumente/AmiTest/Makefile?type=file&root=%252F).
