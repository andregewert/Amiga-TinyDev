#ifndef GADGETS_MINIMAP_H
#define GADGETS_MINIMAP_H
/*
**  $VER: minimap.h 1.0 (18.10.2026)
**  Copyright (c) 2026 André Gewert <agewert@ubergeek.de>, MIT License
**
**  Definitions for the minimap.gadget ReAction class (AmigaOS 3.2)
**
**  minimap.gadget shows a scaled overview of the text of a connected
**  texteditor.gadget: one pixel per non-blank character, the visible
**  part of the editor as a tinted, framed box. Clicking or dragging in
**  the minimap reports the line the editor should scroll to.
**
**  Public class name: "minimap.gadget"   (superclass: gadgetclass)
**  Library:           SYS:Classes/Gadgets/minimap.gadget
**
**  Rendering runs in a process of the gadget, one job at a time; the
**  gadget shows the last finished picture until the next one is ready.
**  Nothing is rendered while the gadget has no size (e.g. a collapsed
**  layout column).
**
**  Usage
**  -----
**  The gadget does not notice changes of the editor by itself, and it
**  needs the application's process to read the editor and to start a
**  render job. Create and use the object from one process:
**
**  - Connect the editor (and optionally a colour hook):
**        SetGadgetAttrs(mm, win, NULL, MINIMAP_TextEditor, editor, TAG_END);
**  - When the text changed:
**        SetGadgetAttrs(mm, win, NULL, MINIMAP_TextChanged, TRUE, TAG_END);
**  - After every batch of input events and whenever a signal of
**    MINIMAP_SignalMask arrives:
**        SetGadgetAttrs(mm, win, NULL, MINIMAP_Update, TRUE, TAG_END);
**    This shows a finished picture and starts a new job if the text, the
**    editor's scroll position or the gadget's size changed.
**  - Scrolling: give the gadget ICA_TARGET (e.g. ICTARGET_IDCMP) and
**    scroll the editor to MINIMAP_Top when a notification arrives
**    (GA_TEXTEDITOR_Prop_First). MINIMAP_Dragging tells when a drag
**    starts and ends; the drag ends with a GADGETUP as well.
**  - Before the connected editor is disposed, set MINIMAP_TextEditor to
**    NULL (or another editor). This waits for a running job.
*/

#ifndef UTILITY_TAGITEM_H
#include <utility/tagitem.h>
#endif

#define MINIMAP_CLASSNAME   "minimap.gadget"
#define MINIMAP_VERSION     1

/*****************************************************************************/

#define MINIMAP_Dummy       (TAG_USER + 0x04A90000)

/* The texteditor.gadget whose text is shown. NULL shows an empty
 * minimap. Changing it waits for a running render job.             (ISG) */
#define MINIMAP_TextEditor  (MINIMAP_Dummy + 1)     /* Object * */

/* Hook that colours one line, called from the render process with the
 * minimap object and a struct MinimapLineMsg. It returns the state for
 * the next line (e.g. inside a block comment). It must not use DOS or
 * Intuition and must stay valid until it is replaced (changing it waits
 * for a running job). NULL draws everything with MINIMAP_TextPen.   (IS) */
#define MINIMAP_ColorHook   (MINIMAP_Dummy + 2)     /* struct Hook * */

/* Number of text columns mapped onto the gadget's width. Default 128. (I) */
#define MINIMAP_Columns     (MINIMAP_Dummy + 3)     /* ULONG */

/* Pens; -1 (the default) uses the screen's pen given in brackets.  (IS)
 * TextPen [TEXTPEN], BackgroundPen [BACKGROUNDPEN], ViewFillPen (tint of
 * the visible part) [BackgroundPen], ViewBorderPen [SHADOWPEN].           */
#define MINIMAP_TextPen         (MINIMAP_Dummy + 4) /* LONG */
#define MINIMAP_BackgroundPen   (MINIMAP_Dummy + 5) /* LONG */
#define MINIMAP_ViewFillPen     (MINIMAP_Dummy + 6) /* LONG */
#define MINIMAP_ViewBorderPen   (MINIMAP_Dummy + 7) /* LONG */

/* The editor's text changed: render it again at the next update.     (S) */
#define MINIMAP_TextChanged (MINIMAP_Dummy + 8)     /* BOOL */

/* Show a finished picture and start a render job if needed. Must be set
 * with SetGadgetAttrs() from the application's process.               (S) */
#define MINIMAP_Update      (MINIMAP_Dummy + 9)     /* BOOL */

/* Signal(s) of the application's process that a render job finished;
 * then set MINIMAP_Update.                                            (G) */
#define MINIMAP_SignalMask  (MINIMAP_Dummy + 10)    /* ULONG */

/* Line the editor should show at its top, chosen with the mouse so that
 * the visible part is centred on the pointer.                     (GN) */
#define MINIMAP_Top         (MINIMAP_Dummy + 11)    /* ULONG */

/* TRUE while the user drags in the minimap.                       (GN) */
#define MINIMAP_Dragging    (MINIMAP_Dummy + 12)    /* BOOL */

/*****************************************************************************/

/* Message of MINIMAP_ColorHook */
struct MinimapLineMsg {
    STRPTR mlm_Text;        /* the line, NUL-terminated, without line end */
    ULONG  mlm_Length;      /* its length */
    UBYTE *mlm_Pens;        /* one pen per character, preset to TextPen */
    ULONG  mlm_State;       /* returned by the hook for the previous line,
                               0 for the first line */
};

#endif /* GADGETS_MINIMAP_H */
