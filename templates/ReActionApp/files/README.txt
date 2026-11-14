{$PROJECT}
{$PURPOSE}

  Build/Build Project (Amiga+E)  builds the program with {$CC}
  Build/Run (Amiga+R)            starts "{$NAME}" in a console window
  Build/Debug (Amiga+D)          starts {$NAME}.debug in BDebug

{$NAME}.debug is the program with debug information. Choose "Debug build"
in Build/Build Command and build it before debugging; choose "Build"
again for the normal program.

It needs AmigaOS 3.2 to run (window.class V47 builds its menu).

The texts are in Catalogs/{$NAME}.cd (English, built in) and
Catalogs/deutsch/{$NAME}.ct. Building needs CatComp from the NDK 3.2
(Tools/Catcomp) in the command path. The program finds its catalog in
PROGDIR:Catalogs/.

BDebug is part of Barfly, available on Aminet.

The commands are in .tinydev/project.json (Project/Project Settings...).
