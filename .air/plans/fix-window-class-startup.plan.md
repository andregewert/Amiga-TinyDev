## 1. Goal

Prevent the libnix startup code from requesting the nonexistent `window.library`, so AmiEditor reaches its own version-47 `window.class` initialization on AmigaOS 3.2.

## 2. Approach

The executable currently contains both `window.class` and `window.library`: the latter comes from libnix's `window.o` stub because the application-owned library bases are tentative/common symbols. Give every base that AmiEditor explicitly opens and closes a real zero-initialized definition in [main.c](air-file://rqrbrek0ib860onkrlsi/home/agewert/Dokumente/AmiTest/src/main.c?type=file&linesData=%7B%22range%22%3A%7B%22first%22%3A338%2C%22second%22%3A490%7D%2C%22lines%22%3A%7B%22first%22%3A16%2C%22second%22%3A17%7D%7D&root=%252F); this prevents the linker from pulling the auto-open stubs while retaining the existing explicit `OpenLibrary`/`CloseLibrary` lifecycle. This is preferable to lowering version requirements because the UI uses version-47-only ReAction tags, and the installed OS 3.2 component is already the correct class.

## 3. File Changes

- **Modify** [main.c](air-file://rqrbrek0ib860onkrlsi/home/agewert/Dokumente/AmiTest/src/main.c?type=file&linesData=%7B%22range%22%3A%7B%22first%22%3A338%2C%22second%22%3A490%7D%2C%22lines%22%3A%7B%22first%22%3A16%2C%22second%22%3A17%7D%7D&root=%252F): explicitly initialize the nine application-managed library/class base globals to `NULL`, turning them from common symbols into definitions owned by AmiEditor.

No files need to be created or deleted.

## 4. Implementation Steps

### Task 1: Correct library-base ownership

1. In [main.c](air-file://rqrbrek0ib860onkrlsi/home/agewert/Dokumente/AmiTest/src/main.c?type=file&linesData=%7B%22range%22%3A%7B%22first%22%3A338%2C%22second%22%3A490%7D%2C%22lines%22%3A%7B%22first%22%3A16%2C%22second%22%3A17%7D%7D&root=%252F), add explicit `NULL` initializers to `AslBase`, `DiskfontBase`, `GadToolsBase`, `IconBase`, `UtilityBase`, `WindowBase`, `LayoutBase`, `ClickTabBase`, and `TextFieldBase`.
2. Leave `DOSBase`, `IntuitionBase`, and `GfxBase` under the C runtime's existing handling; the startup code relies on `DOSBase`, while the application deliberately saves and restores the runtime-provided Intuition and Graphics bases.
3. Retain the explicit `OpenLibrary("window.class", 47)` call and the existing reverse-order cleanup, since those match the OS 3.2 API level and object lifecycle.

### Task 2: Verify the linked dependency set

1. Rebuild with the existing rules in [Makefile](air-file://rqrbrek0ib860onkrlsi/home/agewert/Dokumente/AmiTest/Makefile?type=file&root=%252F).
2. Inspect the rebuilt executable's strings and symbols to confirm that `window.class` remains present, `window.library` is absent, and the application-managed bases resolve from [main.c](air-file://rqrbrek0ib860onkrlsi/home/agewert/Dokumente/AmiTest/src/main.c?type=file&root=%252F) instead of libnix's class stubs.
3. Run the existing host syntax tests, then smoke-test the rebuilt Hunk executable on AmigaOS 3.2.

## 5. Acceptance Criteria

- The rebuilt `AmiEditor` executable contains `window.class` and does not contain `window.library`.
- The linker no longer pulls libnix auto-open entries for the nine bases explicitly managed by `app_open_libraries()` and `app_close_libraries()`.
- On AmigaOS 3.2 with the standard version-47 ReAction installation, launching AmiEditor does not display the “cannot open window library” startup failure.
- AmiEditor still rejects an actually missing or pre-version-47 `window.class` through its existing explicit error path.
- The ReAction window opens and closes normally, including the tab layout and text editor gadget.
- The existing host syntax test suite passes without regressions.

## 6. Verification Steps

1. Run `make` from the project root and confirm the cross-compiler completes without warnings or link errors.
2. Run `strings AmiEditor | rg 'window\\.(class|library)'`; expect exactly the valid `window.class` dependency and no `window.library`.
3. Run `m68k-amigaos-nm src/main.o` and verify the nine application-managed bases are defined data/BSS symbols rather than common (`C`) symbols.
4. Run `make test`; expect the syntax test executable to exit successfully.
5. Copy the rebuilt executable to an AmigaOS 3.2 system and launch it from Shell and Workbench; confirm no pre-main window-library requester appears and the editor window is usable.
6. As a negative edge check, temporarily make `window.class` unavailable on a test system and verify AmiEditor exits with its own version-47 requirement message rather than crashing.

## 7. Risks & Mitigations

- **Changing symbol strength suppresses runtime auto-opening for all explicitly initialized bases.** This is intentional: [main.c](air-file://rqrbrek0ib860onkrlsi/home/agewert/Dokumente/AmiTest/src/main.c?type=file&root=%252F) already opens each one before any UI/file/font code runs and closes every successfully opened base on the common exit path.
- **A lower class version might appear to solve startup but fail later on version-47-only tags.** Keep the version floor at 47 and fix only the erroneous linker-generated library name.
- **Host tests cannot reproduce Amiga loader behavior.** Pair the automated binary-string/symbol checks with a real AmigaOS 3.2 launch smoke test.