{$PROJECT}
{$PURPOSE}

  Build/Build Project (Amiga+E)  builds {$NAME.lower}.library and test{$NAME}
  Build/Run (Amiga+R)            starts test{$NAME} in a console window

The test program loads the library from its own drawer. A new function
goes into {$NAME}.c, the jump table of libframe.c, {$NAME}_lib.sfd and
include/proto/{$NAME.lower}.h, always at the end.

After a change, flush the old library from memory with Avail FLUSH.

The commands are in .tinydev/project.json (Project/Project Settings...).
