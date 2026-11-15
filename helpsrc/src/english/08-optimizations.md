# Speed Optimisations

Some functions of TinyDev need comparatively much processing power, fast
mass storage or a lot of RAM.

In general, graphics output on AGA screens is rather slow. If TinyDev is
used on an AGA screen, reducing the number of colours of the Workbench
screen can help to speed up the user interface.

To make TinyDev faster on slow Amigas, the following can be tried:

- **Toolbar**  
  Loading the AISS images for the toolbar takes some time. To make
  TinyDev start faster, the toolbar can be switched to text only under
  *Settings / Toolbar*.
- **Scrolling**  
  If scrolling in the text editor is too slow, first try switching on
  the option &raquo;Pause highlighting while scrolling&laquo; under
  *Settings / General*.
- **Source analysis**  
  With Scout, TinyDev includes a modular and powerful system for source
  analysis. However, this analysis takes time, above all on slow hard
  disks and other mass storage. It can be made faster in three steps:

  - The drawer for temporary files should be on a fast drive. By
    default, `T:` is used. This assign should point to a drawer in the
    RAM Disk.
  - The files in the include drawers are analysed as well. With large
    collections of includes (e.g. the NDK of AmigaOS) it can make sense to
    remove them under *Project Settings / Include Drawers* (if they are
    listed there).
  - The source analysis with Scout can also be switched off completely
    under *Settings / Advanced*. Then, however, syntax highlighting, the
    symbol list, the problem list and other features no longer work.

- **Git integration**  
  Running am-git in the background to keep the file status up to date
  takes quite a lot of time. If this makes TinyDev too slow, switch the
  git integration off again under *Settings / Git*. am-git can still be
  used from the Shell then.
