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
- `src/settings.c`: the global settings in `PROGDIR:TinyDev.json` (the key
  "settings"), read at the start and written by the settings window and
  the tool window snapshot; the icon's tool types are not read, so an
  update may replace the icon. A file that cannot be read is never written.
- `src/appprefs.c`, `include/appprefs.h`: the portable reader and writer
  of those settings (the `Settings` structure, defaults, the JSON layout),
  keeping keys it does not know; no Amiga headers.
- `src/prefswin.c`: the settings window (categories General with the groups
  Window and View, Colours, Toolbar, Project Defaults (drawer of new
  projects, author and e-mail address for the templates), Advanced with Scout, the drawer of
  the temporary files, the Shell window and the output window's font, Git
  (am-git on or off, its command, and am-git's environment variables with
  a window of its own for the lines of `AM_GIT_NETRC`), and File Types: Standard and
  Scout's languages in a hierarchical list sharing one page),
  built like Kickdown's.
- `src/gitenv.c`: am-git's environment variables (`AM_GIT_USER_NAME`,
  `AM_GIT_USER_EMAIL`, `AM_GIT_USERNAME`, `AM_GIT_PASSWORD`, `AM_GIT_SSH`,
  `AM_GIT_NETRC`), read from `ENV:` (else `ENVARC:`) and written by the
  settings window: "Use" to `ENV:`, "Save" also to `ENVARC:`, only what
  changed. TinyDev keeps none of them in `TinyDev.json`.
- `src/git.c`: git through am-git, only with the setting "git"/"enabled"
  (the Git menu is hidden otherwise, `ui_update_git_menu()`): `git_run()`
  runs am-git synchronously through a script with `Stack 100000` in the
  temporary drawer (output and messages into one file, read afterwards);
  Git/Create Repository... and the New Project wizard's check box run
  `am-git init` with the drawer's full name (am-git needs it). The status
  (`status --porcelain`) runs in a process of its own (as in `build.c`)
  when a repository is opened, after saving, on the tree's changes (not
  within 2 s after a run: am-git writes into `.git`) and on Git/Refresh
  Status; it colours the tree through `DIRTREE_PenHook` (colours of the
  setting "git"/"colours") and puts the branch (`.git/HEAD`) into the
  status line.
- `src/gitwin.c`: the modal windows of the Git menu: Git/Commit... (the
  changed files of `status --porcelain` with check boxes ("selected"): a
  click stages with `add` (`rm --cached` for a deleted file) or takes back
  with `unstage` at once and reads the status again; "Select All" is
  `add .`; a one-line message, `commit -m`) and the changes of a file
  since the last commit (Git/Show Changes of the File... or from the
  commit window: HEAD's file from `show --text-blob` or `show` against
  the file on disk, compared with `linediff.c`, coloured lines in the
  system default text font).
- `src/linediff.c`, `include/linediff.h`: the portable line diff (Myers
  in linear space) written as unified hunks; no Amiga headers.
- `src/gitstatus.c`, `include/gitstatus.h`: the portable reader of
  am-git's status lines, a file's or drawer's state (ignoring case), the
  branch of `.git/HEAD` and the kinds of a diff's lines; no Amiga headers.
- `src/filetypes.c`, `include/filetypes.h`: the portable settings per
  file type (tab size, indentation, automatic indentation, Tab key, line
  numbers, cutting lines): reading and writing the "filetypes" of
  `PROGDIR:TinyDev.json`, keeping keys it does not know; no Amiga headers.
- `src/ftprefs.c`: loads and saves `PROGDIR:TinyDev.json` and gives each
  editor the settings of its document's language (`ftprefs_apply()`,
  called with the highlighting rules from `scoutlink.c`).
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
  the placeholders `{project}`, `{tinydev}`, `{file}`, `{base}`, the include drawers
  and the languages chosen for files; writing keeps the keys it does not
  know; no Amiga headers.
- `src/projdata.c`: reading and writing the files of
  `<project>/.tinydev/` (whole files; writes through a temporary file,
  making the directory when needed), for bookmarks and project settings;
  `datafile_read()`/`datafile_write()` do the same for any file.
- `src/projwin.c`: the modal project settings window
  (Project/Project Settings...), built like the settings window: a page per
  action with a listbrowser of its commands (checkbox = default), buttons
  beside the page, and a modal window to edit one command; "Save" writes
  `project.json` with `projconf_format()`.
- `src/newproj.c`: the modal wizard Project/New Project... (and, for
  file templates, Project/New File from Template...): the templates and
  their icons from `Sprout LIST ICON`, a page gadget with the template
  page (categories, templates, details, project name and drawer) and a
  form page per template (fields from the placeholders, defaults from
  `sprout_tmpl.c`, linked into TinyDev); Create writes the values to
  TEMPDIR, runs Sprout synchronously, shows its messages, and opens the
  new drawer as the project with the template's files of `open`.
- `src/tmplsym.c`: the built-in symbols of the template icon types and
  of the folder tree's buttons (`tmplsym_draw()` and a small private image
  class, which also shows a template's own icon).
- `src/treeops.c`: the folder tree's file operations: a row of small
  buttons above the tree (symbols of `tmplsym.c`, no AISS images) and
  Project/Folder Tree: New File from Template, New File, New Drawer, Rename, Delete on the entry
  selected in the tree (icons taken along; Delete asks, walks a drawer
  without following links and deletes nothing if something is protected;
  open documents follow a rename or are marked changed), and the small
  modal window asking for a name.
- `src/build.c`: the Build menu's actions: the default command of Build,
  Run or Debug from `project.json`, written as a script to `T:`
  (placeholders replaced, "Stack 65536" first). Build runs in a process
  of its own (synchronous `SystemTags()`, return code by message) with
  its output in a file in `T:` read every 0.25 s (timer.device) into the
  output window; Stop sends Ctrl-C to its Shell. Run and Debug start
  detached in a console window. A submenu per action in the Build menu
  (slots of the table in `ui.c`, filled by `build_commands_poll()`) chooses
  the default command and saves it in `project.json`.
- `src/scoutlink.c`: TinyDev as a client of TinyDev-Scout (ARexx
  messages answered to a port of the event loop): starts Scout when
  needed, a session per project (the tree's root), analyses documents
  when loaded or saved and, as a snapshot in the temporary drawer, after a
  pause in typing; the counts go to the status line. A document's
  language comes from Scout (LANGUAGE) or from the File Type menu
  (`scoutlink_choose_language()`, kept in `project.json`).
- `src/symlist.c`: the symbol list, a tool window (View/Symbol List):
  the symbols of the active document or of the project from Scout
  (through `scoutlink.c`), filter, Go to (`document_go_to()`).
- `src/statuslamp.c`: the lamp right of the status line (a space.gadget
  with a render hook): Scout's state from `scoutlink_activity()` as a
  traffic light (green ready, yellow working, red failed, empty off).
- `src/diag.c`: the diagnostics of the documents from Scout: the list per
  document and the marks in the editor's left bar (drawn by the left bar
  hook of `bookmarks.c`; a fixed array changed under `Forbid()`).
- `src/diaglist.c`: the problem list, a tool window (View/Problem List):
  the diagnostics of the active document or of the project, Go to.
- `src/navigate.c`: Edit/Go to Definition (DEFINITION with the name at
  the cursor, cycling through several), Edit/Open Included File
  (REFERENCE at the cursor: Scout resolves the include or link) and
  Edit/Go Back (a history of places).
- `src/complete.c`: Edit/Complete: COMPLETE with the prefix at the
  cursor, a borderless list window at the cursor that takes the keys
  (choose, insert with MarkText/Replace, narrow while typing).
- `src/tempfile.c`: unique names of temporary files in the drawer of the
  setting TEMPDIR (`temp_name()`), used by the Build menu and Scout's
  snapshots.
- `src/cmdpath.c`: the command path for the Shells TinyDev starts
  (Build, Run, Debug, New Shell), passed with `NP_Path`: a copy of
  TinyDev's CLI path or, started from the Workbench, of the Workbench's
  (so commands added with `Path ... ADD` are found).
- `src/outwin.c`: the output window, a tool window (View/Output Window,
  first placed below the main window, as wide as it): the build's lines
  (in the system default text font or the setting "outputfont"; kept
  also while it is closed, at most 2000), status line, Stop, Clear,
  copying the selected or all lines to the clipboard.
- `src/toolwin.c`: placement of tool windows (remembered box pushed into
  the screen, otherwise docked right of the main window or over its right
  edge). Their boxes are kept for the session (`EditorApp`); only
  View/Snapshot Tool Windows writes them, with the open state, into
  `TinyDev.json` (`settings_save_toolwindows()`), not the settings window;
  View/Unsnapshot Tool Windows writes the defaults back.
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
- `src/shell.c`: Project/New Shell: `NewShell` started asynchronously in
  the project drawer, its window from the setting SHELLWINDOW (a console
  handler, placed below the main window, or a whole window).
- `src/fileio.c`: loading, line-ending preservation, requesters, and safe saves.
- `src/tree.c`: connects `dirtree.gadget` to the editor (root selection,
  opening files, column visibility; the column is the tree with the buttons
  of `treeops.c`).
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
  name hook (shown names), pen hook (a text colour per entry, asked when
  an entry is read and on `DIRTREE_UpdatePens`), sort order, hiding
  directories without shown files. It watches the root and the expanded
  directories with `StartNotify()` (port and signal of the creating task,
  `DIRTREE_SignalMask`); the application answers the signal with
  `DIRTREE_Refresh`, which reconciles only the changed directories.
- `src/classes/minimap/`: `minimap.gadget`, the scaled overview of a
  `texteditor.gadget` with its own render process (gadgetclass subclass).
- `src/minimap.c`: connects `minimap.gadget` to the editor (active document,
  syntax colour hook, re-render requests, scrolling the editor on minimap
  clicks and drags).
- `src/syntax.c`: the combined scan (`syntax_scan()`: Scout's lexical
  rules, then Scout's tokens) and the TextEditor highlighting hook.
  TinyDev has no scanners of its own: without Scout nothing is
  highlighted.
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
- `src/sprout/`: Sprout (`bin/Sprout`), the Shell command that creates a
  project from a template (an LhA archive or a drawer in
  `PROGDIR:Templates/`) and lists or describes templates as JSON (`LIST`,
  `INFO`): `sprout_main.c` (arguments, templates as drawers or archives,
  copying, protection bits and file notes, removing what it made on an
  error, the JSON output), `sprout_tmpl.c` with `include/sprout_tmpl.h`
  (the portable core: `template.json` as text or tree, with its texts
  translated as `"name[deutsch]"` for the locale's languages, values,
  placeholders `{$KEY}`, safe paths; host-tested in
  `tests/test_sprout_tmpl.c`) and `sprout_lha.c` with
  `include/sprout_lha.h` (the portable LhA reader: header levels 0-2,
  -lh0-/-lh4- to -lh7-, CRCs; host-tested in `tests/test_sprout_lha.c`).
  Its design is `src/sprout/docs/Sprout.md`. Like Scout it must not
  depend on TinyDev.
- `templates/`: the templates that come with TinyDev (HelpProject, help
  pages in Markdown built with `mdtohtml`; HelloWorld, a
  project; the starter projects Assembler, AmigaE, AceBasic, FreePascal,
  ARexxScript, ReActionApp, Commodity, SharedLibrary and GadgetClass;
  TemplateProject, a template for making templates; the file templates
  CModule, AsmModule, EModule, PascalUnit, ARexxFile, DosScript,
  MakefileFile, HtmlPage, MarkdownPage, AmigaGuide and AminetReadme) (`templates/<Id>/`
  with `template.json` and `files/`, ISO-8859-1); `make` copies them to
  `bin/Templates/` with `cp` and checks them with `charcheck-templates`,
  as their names may contain `{$...}`, which make and the Shell would
  expand; `make dist` packs each into `Templates/<Id>.lha`.
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
  (`scout/describe`, `scout/analyze`, the program); the AmigaDOS script
  parser is `dos_parser.c` (labels, variables, .KEY parameters, aliases,
  IF/SKIP checks, tokens) and `dos_parser_rpc.c`; the Makefile parser
  is `make_parser.c` (logical lines, variables, targets, define blocks,
  conditionals, includes, recipe checks, tokens; GNU make syntax, BSD
  directives left alone) and `make_parser_rpc.c`; the Markdown parser is
  `markdown_parser.c` (headings as symbols of kind heading, dead links and
  anchors, undefined references, links to files as references of kind
  `link`, tokens; the file system comes from
  `MarkdownFiles`, set in the rpc part) and `markdown_parser_rpc.c`;
  the JSON parser is `json_parser.c` (a scanner with positions of its
  own, the syntax up to the first error, keys defined twice, the keys of
  the first two levels as symbols, every key as a token) and
  `json_parser_rpc.c`; the markup parser is `markup_parser.c` (one program
  for HTML and XML: a pass with a stack of the open elements, XML
  well-formedness, tolerant HTML with optional end tags and void elements,
  the headings of HTML as symbols of kind heading, dead links and anchors
  and links as references of kind `link` as in Markdown (`MarkupFiles`, set
  in the rpc part), tags and attributes as tokens; its rules only know comments and have
  `"numbers": false`) and `markup_parser_rpc.c`;
  the ARexx parser is `arexx_parser.c` (clauses as ARexx reads them, labels,
  procedures, variables, block structure, jumps, tokens; expressions are
  not parsed) and `arexx_parser_rpc.c`;
  the assembler parser is `asm_parser.c` (68k in Motorola syntax as vasm and
  AsmPro read it: two passes over the lines, labels, local labels by the
  part between global labels, equates, macros, conditionals, sizes of
  instructions, tokens) and `asm_parser_rpc.c`;
  the AmigaE parser is `e_parser.c` (E as E-VO reads it: statements in two
  passes, PROCs, methods, OBJECTs, DEFs, CONST/ENUM/SET, labels, `#define`,
  block structure, MODULE as references; expressions are not parsed; a
  compiled `.m` is walked for its symbols, which Scout offers only for
  completion, see `"binary"` in the protocol) and `e_parser_rpc.c`;
  the ACE BASIC parser is `ace_parser.c` (statements in two passes, `~` and
  a backslash continuing a line, SUBs, METHODs, STRUCT and CLASS, DECLARE
  and EXTERNAL, CONST, the declared variables, labels, block structure, the
  `#` lines; expressions are not parsed; its includes end in `.h` and say
  so with `"language"` in their references) and `ace_parser_rpc.c`;
  the Free Pascal parser is `pascal_parser.c` (the source read into tokens,
  then two passes over sections and blocks: program or unit, uses, const,
  type, var, labels, routines and methods, records and classes, directives;
  expressions are not parsed; a unit's `.ppu` is not read) and
  `pascal_parser_rpc.c`;
  the AmigaGuide parser is `guide_parser.c` (two passes over the lines, nodes
  and macros first, then the checks: @database, nodes, commands in their
  place, arguments, dead links; the OS 3.2 command `@{IMG}`; commands as
  tokens, the nodes as symbols of kind heading, the macros of kind macro) and `guide_parser_rpc.c`;
  `include/scout_protocol.h` holds the protocol's numbers (kinds,
  severities, error codes) for Scout and all parsers.
- `include/editor.h`: shared application types and cross-module declarations.
- `include/syntax.h`: portable interface of the highlighting (styles,
  spans, `syntax_scan()`).
- `tests/test_syntax.c`: host-native tests of the combined scan.
- `tests/test_search.c`: host-native tests of the search engine.
- `tests/test_bmstore.c`: host-native tests of the bookmark store.
- `tests/test_projconf.c`: host-native tests of the project settings.
- `tests/test_filetypes.c`: host-native tests of the settings per file type.
- `tests/test_appprefs.c`: host-native tests of the global settings.
- `tests/test_gitstatus.c`: host-native tests of the reading of am-git's status.
- `tests/test_linediff.c`: host-native tests of the line diff.
- `tests/test_sprout_tmpl.c`: host-native tests of Sprout's core (also
  reads the `template.json` of the templates that come with TinyDev).
- `tests/test_sprout_lha.c`: host-native tests of the LhA reader with the
  archives of `tests/data/sprout/` (binary, left out of `charcheck`),
  made by `tools/mksprouttests.py` with jlha.
- `tests/test_dirtree_sort.c`: host-native tests of dirtree.gadget's natural order.
- `tests/test_htmltitle.c`: host-native tests of the HelpViewer's title and link helpers.
- `tests/test_json.c`, `tests/test_scout_rpc.c`, `tests/test_c_parser.c`:
  host-native tests for the JSON code, Scout's JSON-RPC helpers and the C
  parser (with the sample sources in `tests/data/c_parser/`, ISO-8859-1).
- `tests/test_dos_parser.c`: host-native tests of the AmigaDOS script
  parser (sample in `tests/data/dos_parser/`, ISO-8859-1).
- `tests/test_make_parser.c`: host-native tests of the Makefile parser
  (sample in `tests/data/make_parser/`, ISO-8859-1).
- `tests/test_markdown_parser.c`: host-native tests of the Markdown parser
  (samples in `tests/data/markdown_parser/`, ISO-8859-1).
- `tests/test_json_parser.c`: host-native tests of the JSON parser
  (sample in `tests/data/json_parser/`, ISO-8859-1).
- `tests/test_markup_parser.c`: host-native tests of the HTML and XML
  parser (samples in `tests/data/markup_parser/`, ISO-8859-1).
- `tests/test_arexx_parser.c`: host-native tests of the ARexx parser
  (sample in `tests/data/arexx_parser/`, ISO-8859-1).
- `tests/test_asm_parser.c`: host-native tests of the assembler parser
  (sample in `tests/data/asm_parser/`, ISO-8859-1).
- `tests/test_e_parser.c`: host-native tests of the AmigaE parser, with
  modules built byte by byte (sample in `tests/data/e_parser/`, ISO-8859-1).
- `tests/test_ace_parser.c`: host-native tests of the ACE BASIC parser
  (sample in `tests/data/ace_parser/`, ISO-8859-1).
- `tests/test_pascal_parser.c`: host-native tests of the Free Pascal parser
  (samples in `tests/data/pascal_parser/`, ISO-8859-1).
- `tests/test_guide_parser.c`: host-native tests of the AmigaGuide parser
  (sample in `tests/data/guide_parser/`, ISO-8859-1).
- `amiga-devkit/`: git submodule (pinned tag) with shared Amiga tools and AI
  guidelines; see `CLAUDE.md`. Do not edit files inside the submodule.
- `external/html_gadget/`: git submodule (tag `v1.2`), html.gadget, for
  the headers the HelpViewer is built with (the class itself is installed
  separately). Do not edit it.
- `external/openurl/`: git submodule (tag `7.18`), for the headers of
  `openurl.library` (`developer/C/include`), with which the HelpViewer opens
  `http:` and `https:` links. The library is optional and opened at the
  first such link. Do not edit it.
- `external/mdtools/`: git submodule (tag `v1.2`; the Kickdown project,
  repository Amiga-Kickdown) for `bin/mdtohtml`, which turns a tree of Markdown files
  into HTML pages. The Makefile compiles only its converter, unchanged and
  without our warning flags, with the nested submodule `md4c`; `make dist`
  ships those sources only. Do not edit it: changes belong in
  `~/Dokumente/Amiga-MDTools`.
- `external/regex/`: git submodule (tag `alpha3.8p1`), Henry Spencer's POSIX
  regex library for `src/search.c`. Its `regex.h` and `*.ih` headers are
  generated with its `mkh` script into `build/regex/`; it is compiled
  unchanged and without our warning flags. Do not edit it; its licence
  requires the credit in `README.md`.
- `scripts/`: `tdscout_query.rexx` sends a command to TinyDev-Scout and
  shows the result (`rx scripts/tdscout_query.rexx STATUS`);
  `test_scout.rexx` tests Scout's commands with the samples of
  `tests/data/c_parser/` (it starts `bin/TinyDev-Scout` if needed).
- `tools/mkicons.py`: motifs of the Workbench icons (TinyDev, Scout, parsers,
  HelpViewer) for `amiga-devkit/tools/amigaicons.py`; `make icons` writes
  `icons/`. These files are generated, never edited by hand.
- `package/`: what the Aminet archive needs besides the build:
  `TinyDev.readme` (the Aminet readme, ISO-8859-1, at most 78 columns),
  `Install` (the Installer script, ISO-8859-1: everything into one drawer,
  an update keeps the icons holding settings) and
  `help/<language>/` (TinyDev's help pages, HTML in ISO-8859-1 with
  entities; language drawers named like the Amiga locales, the same file
  names in every language; `make` copies them to `bin/Help/`).
- `fonts/<Name>/`: the monospaced bitmap fonts that come with TinyDev,
  each with its `LICENSE`; their sources are glyph text files, every glyph
  as rows of `#` and `.` (format in `fonts/Holstein/glyphs.txt`), edited by
  hand. `tools/mkfont.py` builds `bin/Fonts/<Name>.font`; the Installer
  copies them to `FONTS:` if chosen. Holstein (`glyphs.txt`, cell 8 x 16,
  sizes 16 and 17, where 17 adds an empty row below) is derived from DejaVu
  Sans Mono (Bitstream Vera licence: the name may not contain "Bitstream"
  or "Vera"); Tamzen (`glyphs15.txt`, `glyphs16.txt`, sizes 15 and 16) is
  Tamzen 8x15r/8x16r by Suraj N. Kurapati (a fork of Tamsyn), limited to
  ISO-8859-1, with the glyphs it lacks there drawn for TinyDev.
- `tools/mksprouttests.py`: makes the LhA archives of
  `tests/test_sprout_lha.c` (`tests/data/sprout/`) with jlha.
- `tools/check_templates.py`, `tests/tmpl_generate.c`: `make check-templates`
  makes the C templates with the portable core of Sprout and builds them
  with vbcc (`/opt/vbcc`, if installed) and the gcc cross compiler.
- `tools/mkdist.py`: `make dist` builds `dist/TinyDev.lha` from `bin/`,
  the README, the sources (built without git) and icons, with
  `amiga-devkit/tools/distlib.py`.
- `README.md`: for users, short, maintained by the user (see `CLAUDE.md`).
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

- The submodules are nested: after cloning, run
  `git submodule update --init --recursive`.
- Run `make test` for all changes that affect the portable syntax scanner.
- Run `make check-templates` for changes below `templates/`; it is optional
  and skips a compiler that is not installed. It shows that a template
  compiles and links, not that it runs on the Amiga.
- Run `make docs` (and `make docs-<class>` for a class's public header) when
  comments or interfaces change; it must finish without Doxygen warnings
  (rules: `amiga-devkit/docs/Dokumentation.md`).
- Run `make` for changes to target code when the Amiga cross-toolchain is
  available.
- A successful host test does not validate ReAction or AmigaOS integration.
- Do not claim target-build verification if the cross-toolchain or SDK is
  unavailable; report that limitation explicitly.
- Finished Amiga binaries go to `bin/` (`bin/TinyDev`, `bin/TinyDev-Scout`,
  `bin/parsers/c_parser`, `bin/parsers/dos_parser`,
  `bin/parsers/make_parser`, `bin/parsers/markdown_parser`,
  `bin/parsers/json_parser`, `bin/parsers/markup_parser`,
  `bin/parsers/arexx_parser`,
  `bin/parsers/asm_parser`,
  `bin/parsers/e_parser`,
  `bin/parsers/ace_parser`,
  `bin/parsers/pascal_parser`,
  `bin/parsers/guide_parser`,
  `bin/Classes/Gadgets/<name>.gadget`,
  `bin/Catalogs/<language>/TinyDev.catalog`, `bin/HelpViewer`,
  `bin/Sprout`, `bin/mdtohtml`, `bin/Templates/`,
  `bin/Fonts/Holstein.font` (with `Holstein/16`, `Holstein/17` and
  `Holstein.license`), `bin/Fonts/Tamzen.font` (`Tamzen/15`, `Tamzen/16`,
  `Tamzen.license`),
  `bin/Help/<language>/`), laid out as on
  the Amiga; nothing else goes there. All intermediate files (objects,
  dependency files, `.debug` files, host test binaries) go to `build/`; gadget
  class objects to `build/classes/<name>/`.
- `bin/` is used on the Amiga (case-insensitive file system): never create
  paths in it that differ only in case.
- `make clean` removes `build/` and `bin/`. Do not run it when unrelated
  generated files need to be preserved (e.g. the user's `bin/TinyDev.json`
  with the settings).

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
- work that updates gadgets in the background (timers, messages, build):
  "Background updates and the mouse";
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
  "Wohin welches Wissen gehört"): `README.md` only when asked (see `CLAUDE.md`);
  module details in the
  module's Doxygen comments; what spans modules in `TECHNICAL.md`. No
  implementation details in the README.
- Add or extend host tests for portable scanner behavior and edge cases.
- Do not overwrite unrelated working-tree changes or commit generated binaries
  and object files unless the task explicitly requires them.
