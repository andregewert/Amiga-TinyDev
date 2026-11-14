{$PROJECT}
{$PURPOSE}

  Build/Build Project (Amiga+E)  builds the program with {$CC}
  Build/Run (Amiga+R)            starts "{$NAME}" in a console window
  Build/Debug (Amiga+D)          starts {$NAME}.debug in BDebug

{$NAME}.debug is the program with debug information. Choose "Debug build"
in Build/Build Command and build it before debugging; choose "Build"
again for the normal program.

It stays in the background: the hotkey "{$HOTKEY}" makes the screen flash,
Exchange can disable and quit it, Ctrl-C ends it.

BDebug is part of Barfly, available on Aminet.

The commands are in .tinydev/project.json (Project/Project Settings...).
