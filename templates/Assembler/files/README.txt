{$PROJECT}
{$PURPOSE}

  Build/Build Project (Amiga+E)  assembles the program with {$ASM}
  Build/Run (Amiga+R)            starts "{$NAME}" in a console window
  Build/Debug (Amiga+D)          starts {$NAME}.debug in BDebug

{$NAME}.debug is the program with debug information. Choose "Debug build"
in Build/Build Command and build it before debugging; choose "Build"
again for the normal program.

The assembler needs the assembler includes of the NDK 3.2 (Include_I) in
{$INCDIR}; lvo/exec_lib.i and lvo/dos_lib.i must be found there.

BDebug is part of Barfly, available on Aminet.

The commands are in .tinydev/project.json (Project/Project Settings...).
