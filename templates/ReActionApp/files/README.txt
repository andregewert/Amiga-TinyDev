{$PROJECT}
{$PURPOSE}

  Build/Build Project (Amiga+E)  builds the program with {$CC}
  Build/Run (Amiga+R)            starts "{$NAME}" in a console window

It needs AmigaOS 3.2 to run (window.class V47 builds its menu).

The texts are in Catalogs/{$NAME}.cd (English, built in) and
Catalogs/deutsch/{$NAME}.ct. Building needs CatComp from the NDK 3.2
(Tools/Catcomp) in the command path. The program finds its catalog in
PROGDIR:Catalogs/.

The commands are in .tinydev/project.json (Project/Project Settings...).
