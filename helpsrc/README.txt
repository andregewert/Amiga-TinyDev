TinyDev Help

The pages are the Markdown files in src/. Help/ is made from them, so change
the files in src/ and not those in Help/.

  Build/Build Project (Amiga+E)  makes the pages in Help/
  Build/Run (Amiga+R)            shows them in the HelpViewer
  Rebuild (Project Settings)     also removes pages that have no source any
                                 more, which the build only reports

How the files become a help:

  - Names give the order: 1-first.md comes before 2-second.md.
  - The first "# heading" of a file is its title in the HelpViewer's tree.
  - A drawer is a chapter, its index.md is the chapter's page. src/index.md
    is the home page.
  - Link to other files by their Markdown names, with an anchor if you like:
    [Next](2-chapter/1-page.md#a-heading). The build reports dead links.
  - Everything that is not Markdown (pictures, for example) is copied.

template.html is the frame around every page; $title$ and $body$ are filled
in, and $root$ leads from a page to src/ (see the comment in the file).

The commands are in .tinydev/project.json (Project/Project Settings...).
