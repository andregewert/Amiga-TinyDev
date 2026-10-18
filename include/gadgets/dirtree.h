#ifndef GADGETS_DIRTREE_H
#define GADGETS_DIRTREE_H
/*
**  $VER: dirtree.h 1.0 (18.10.2026)
**  Copyright (c) 2026 André Gewert <agewert@ubergeek.de>, MIT License
**
**  Definitions for the dirtree.gadget ReAction class (AmigaOS 3.2)
**
**  dirtree.gadget shows a directory as a hierarchical list. Directories
**  are read lazily: only the children of an expanded directory are read,
**  collapsing a directory frees them again. Depth and the number of
**  entries are bounded.
**
**  Public class name: "dirtree.gadget"   (superclass: listbrowser.gadget)
**  Library:           SYS:Classes/Gadgets/dirtree.gadget
**
**  All listbrowser.gadget attributes except LISTBROWSER_Labels and
**  LISTBROWSER_Hierarchical may be used; the class owns its node list.
**  Without LISTBROWSER_ShowImage/HideImage the class uses glyph.image
**  arrows (when images/glyph.image is available). LISTBROWSER_ShowSelected
**  defaults to TRUE, so a clicked entry stays highlighted; the selection
**  is kept when directories are expanded or collapsed (unless the
**  selected entry disappears with a collapsed directory).
**
**  Usage
**  -----
**  Expanding, collapsing and activating happen while Intuition handles
**  the input, where DOS must not be used. The gadget therefore only
**  reports them with IDCMP_GADGETUP (WMHI_GADGETUP); the application then
**  lets the gadget do the work from its own process. SetGadgetAttrs()
**  passes the GadgetInfo the gadget needs to redraw itself:
**
**      ULONG ev = DIRTREE_EV_NONE;
**      SetGadgetAttrs((struct Gadget *)tree, window, NULL,
**                     DIRTREE_HandleEvent, TRUE, TAG_END);
**      GetAttr(DIRTREE_Event, tree, &ev);
**      if (ev == DIRTREE_EV_ACTIVATE) {
**          STRPTR path = NULL;
**          GetAttr(DIRTREE_ActivePath, tree, (ULONG *)&path);
**          ... open path ...
**      }
**
**  DIRTREE_Root and DIRTREE_HandleEvent must be set from a process
**  (SetGadgetAttrs()), not through a notification (OM_UPDATE).
*/

#ifndef UTILITY_TAGITEM_H
#include <utility/tagitem.h>
#endif
#ifndef INTUITION_CGHOOKS_H
#include <intuition/cghooks.h>
#endif

#define DIRTREE_CLASSNAME   "dirtree.gadget"
#define DIRTREE_VERSION     1

/*****************************************************************************/

#define DIRTREE_Dummy       (TAG_USER + 0x04A80000)

/* Directory shown as the root of the tree. The path is resolved to its
 * full name and copied. Setting it reads the directory's first level;
 * NULL empties the tree. If the directory cannot be read, the old tree
 * stays and DIRTREE_Status is DIRTREE_ST_FAILED.                    (ISG) */
#define DIRTREE_Root        (DIRTREE_Dummy + 1)     /* STRPTR */

/* Deepest level that is read below the root (root = 0). Default 16. (IG) */
#define DIRTREE_MaxDepth    (DIRTREE_Dummy + 2)     /* ULONG */

/* Maximum number of list entries, including placeholders. A directory
 * that would exceed it is shown incompletely. Default 2048.          (IG) */
#define DIRTREE_MaxEntries  (DIRTREE_Dummy + 3)     /* ULONG */

/* Result of the last DIRTREE_Root or DIRTREE_HandleEvent.             (G) */
#define DIRTREE_Status      (DIRTREE_Dummy + 4)     /* ULONG, DIRTREE_ST_# */

/* Full path of the file activated (double-clicked) at the last
 * DIRTREE_HandleEvent, NULL otherwise. Valid until the next
 * DIRTREE_HandleEvent, DIRTREE_Root change or disposal.               (G) */
#define DIRTREE_ActivePath  (DIRTREE_Dummy + 5)     /* STRPTR */

/* Full path of the selected entry, NULL if none or a placeholder.
 * Valid until the tree changes.                                       (G) */
#define DIRTREE_SelectedPath (DIRTREE_Dummy + 6)    /* STRPTR */

/* Texts of the placeholder entries, for localisation. They are not
 * copied and must stay valid while the object exists.
 * Defaults: "Loading...", "(empty)", "(unavailable)".                 (I) */
#define DIRTREE_LoadingText     (DIRTREE_Dummy + 7) /* STRPTR */
#define DIRTREE_EmptyText       (DIRTREE_Dummy + 8) /* STRPTR */
#define DIRTREE_UnavailableText (DIRTREE_Dummy + 9) /* STRPTR */

/* DIRTREE_Status values */
#define DIRTREE_ST_OK           0   /* read completely */
#define DIRTREE_ST_INCOMPLETE   1   /* partly read (error, limits, memory) */
#define DIRTREE_ST_FAILED       2   /* not read at all */

/*****************************************************************************/

/* Set to TRUE (with SetGadgetAttrs(), from the application's process)
 * after each GADGETUP: expands or collapses the directory or activates
 * the file the listbrowser reported. The result is DIRTREE_Event.      (S) */
#define DIRTREE_HandleEvent     (DIRTREE_Dummy + 10)    /* BOOL */

/* Result of the last DIRTREE_HandleEvent.                              (G) */
#define DIRTREE_Event           (DIRTREE_Dummy + 11)    /* ULONG, DIRTREE_EV_# */

/* DIRTREE_Event values */
#define DIRTREE_EV_NONE         0   /* nothing to do (selection, cursor) */
#define DIRTREE_EV_EXPANDED     1   /* a directory was read and opened */
#define DIRTREE_EV_COLLAPSED    2   /* a directory was closed */
#define DIRTREE_EV_ACTIVATE     3   /* a file was double-clicked, see
                                       DIRTREE_ActivePath */

#endif /* GADGETS_DIRTREE_H */
