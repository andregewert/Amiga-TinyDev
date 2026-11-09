{$PROJECT}
{$PURPOSE}

  Build/Build Project (Amiga+E)  builds {$NAME.lower}.gadget and test{$NAME}
  Build/Run (Amiga+R)            starts test{$NAME} in a console window

The class draws a box; a click switches it. The test program loads the
class from its own drawer. The public interface is
include/gadgets/{$NAME.lower}.h.

After a change, flush the old class from memory with Avail FLUSH.

The commands are in .tinydev/project.json (Project/Project Settings...).
