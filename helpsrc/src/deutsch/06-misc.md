# Sonstige Funktionen

## HelpViewer

`HelpViewer` shows a directory of HTML help pages: the pages by their
titles in a tree on the left, the page on the right, with Home, Back and
Forward; Project/Print prints the page, also as PostScript or PDF. TinyDev
uses it for its own help, but it works with any directory of HTML pages.

    HelpViewer [DIR] <directory or page> [TTF] [FONTSET Vera|DejaVu|Noto] [SIZE n]
               [PORTNAME <name>]

| Option / tool type | Meaning |
|---|---|
| `DIR` | Directory or page to show; without it, a welcome text |
| `TTF` | Use htmlttf.gadget (TrueType fonts) instead of html.gadget |
| `FONTSET`, `SIZE` | Font family and text size for `TTF` (default size 10) |
| `PORTNAME` | Only one viewer per name: a later start with the same name shows its page in the running viewer |
| `WINDOW=l,t,w,h` | Window position and size (tool type, written by Project/Snapshot Window) |
| `PRINT_MODE=PRINTER|PS|PDF`, `PS_LEVEL=1|2`, `PRINT_TO=FILE|PRT|PS|DEVICE`, `PRINT_DEVICE`, `PRINT_IMAGEDPI` | Print options: kind of output, PostScript level and destination, picture resolution (tool types, written by the print window) |
| `PAPER=A4|A5|LETTER|LEGAL`, `MARGINS=l,t,r,b`, `PRINT_SERIF`, `PRINT_SIZE`, `NOPAGENUMBERS`, `NOPRINTBACKGROUNDS` | Page options: paper, margins in mm, font and size for PostScript/PDF, footer, background colours |

- The tree lists the HTML files of the directory in natural order of their
  names (`2-...` before `10-...`); a drawer is shown by the title of its
  `index.html`. The `index.html` of the directory itself is the home page,
  its title the header of the tree.
- A help directory may hold one drawer per language, named like the Amiga
  locales (`english`, `deutsch` ...); the viewer opens the one of your
  language.
- Links between the pages are followed; `http:` and `https:` links open in
  a browser with OpenURL (without `openurl.library` the status line says so),
  other external links are only shown there.
- Project/Iconify (Amiga+I) or the iconify gadget puts the viewer on the
  Workbench as its icon, titled with the help's home page; a later start
  with the same `PORTNAME` opens it again.
- Printing needs html.gadget 1.2. It prints to the default printer as
  graphics, or writes PostScript (to a file, `PRT:`, TurboPrint's `PS:` or
  a device) or PDF; with TurboPrint only through PostScript.

`src/helpviewer/sample/` is a small help directory to try it with.
