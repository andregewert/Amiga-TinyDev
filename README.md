# AmiEditor

AmiEditor is a compact native AmigaOS 3.2 multi-document text editor. Each
closable tab owns a ReAction `texteditor.gadget`, providing native selection,
clipboard, undo/redo, scrolling and line numbers. Built-in highlighting covers
C/C headers (`.c`, `.h`) and AmigaDOS scripts (`.script`, `.dos`). Other files
remain plain text.

## Build and test

The default toolchain prefix is `/opt/amiga/bin/m68k-amigaos-`. Build with:

    make clean && make
    make test
    /opt/amiga/bin/m68k-amigaos-objdump -f AmiEditor

The Makefile explicitly selects `-m68000 -msoft-float -noixemul` and creates the
Hunk executable `AmiEditor`. Copy that file to the target and run it from Shell
with optional file arguments, or launch it from Workbench with project icons.

## Target requirements

- AmigaOS 3.2 and V47 `window.class`, `layout.gadget`, `clicktab.gadget`, and
  `texteditor.gadget`; editor scrollbars use `scroller.gadget`, and the folder
  tree additionally uses `listbrowser.gadget`.
- `asl.library` and normal ReAction dependencies.
- AISS with its standard `TBImages:` assignment for toolbar imagery. If an
  individual image cannot be created, the corresponding toolbar action falls
  back to a text button.
- The editor area always uses the default screen font; there is no font
  selection.

Project supports New/Open/Save/Save As/Close/Quit, native edit commands,
horizontal scrolling without automatic line wrapping, line-number toggling,
safe temporary-file saves, duplicate-path tab activation,
and LF/CR/CRLF preservation. A status line spanning the full window width at
the bottom shows the number of open documents and the line count of the
currently displayed document. The AISS toolbar provides New, Open, Save,
Undo, Redo, Cut, Copy, and Paste. C highlighting recognizes standard C99 keywords,
strings/character constants, preprocessor lines, `//` comments, and block
comments. AmigaDOS keyword matching is case-insensitive and `;` begins a comment.
Every editor installs a white `GA_BackFill` hook plus an editor-local
`DrawInfo` whose background pen is white, and reserves black for normal text
with dark screen pens for keywords, strings, comments, and preprocessor lines.
Note that the AmigaOS 3.2 `texteditor.gadget` renders its text area with the
screen's `BACKGROUNDPEN` and ignores `GA_BackFill`/`GA_DrawInfo` there, while
its `GA_TEXTEDITOR_Pen`/`GA_TEXTEDITOR_ColorMap` attributes are unimplemented
and `GA_TEXTEDITOR_Transparent` is OS4-only; the text area therefore keeps the
Workbench background colour until a future gadget subclass overrides rendering.
Project/Open Directory populates a hierarchical folder tree; double-clicking a
file opens it in a tab. The tree is resizable and can be hidden from the View
menu. A selected base directory opens its first level immediately; child
directories start collapsed. Opening a child reads only its direct children;
closing it releases that subtree, so deeper levels are never scanned eagerly.
The visible tree is bounded to 16 directory levels and 2048 entries.

Version one intentionally treats contents as 8-bit text. UTF-8 decoding,
Unicode shaping, search/replace UI, user-configurable colors, sessions, font
selection, and arbitrary raw TrueType loading are not promised. A missing V47
class prevents startup.
