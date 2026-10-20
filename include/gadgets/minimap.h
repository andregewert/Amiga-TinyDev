/**
 * @file minimap.h
 * @brief Public interface of minimap.gadget, a scaled overview of a
 *        texteditor.gadget (ReAction class for AmigaOS 3.2).
 *
 * minimap.gadget shows a scaled overview of the text of a connected
 * texteditor.gadget: one pixel per non-blank character, the visible part
 * of the editor as a tinted, framed box. Clicking or dragging in the
 * minimap reports the line the editor should scroll to.
 *
 * - Public class name: "minimap.gadget" (superclass: gadgetclass)
 * - Library: SYS:Classes/Gadgets/minimap.gadget
 *
 * Rendering runs in a process of the gadget, one job at a time; the gadget
 * shows the last finished picture until the next one is ready. Nothing is
 * rendered while the gadget has no size (e.g. a collapsed layout column).
 *
 * @par Usage
 * The gadget does not notice changes of the editor by itself, and it needs
 * the application's process to read the editor and to start a render job.
 * Create and use the object from one process:
 * - Connect the editor (and optionally a colour hook):
 *   @code
 *   SetGadgetAttrs(mm, win, NULL, MINIMAP_TextEditor, editor, TAG_END);
 *   @endcode
 * - When the text changed:
 *   @code
 *   SetGadgetAttrs(mm, win, NULL, MINIMAP_TextChanged, TRUE, TAG_END);
 *   @endcode
 * - After every batch of input events and whenever a signal of
 *   #MINIMAP_SignalMask arrives:
 *   @code
 *   SetGadgetAttrs(mm, win, NULL, MINIMAP_Update, TRUE, TAG_END);
 *   @endcode
 *   This shows a finished picture and starts a new job if the text, the
 *   editor's scroll position or the gadget's size changed.
 * - Scrolling: give the gadget ICA_TARGET (e.g. ICTARGET_IDCMP) and scroll
 *   the editor to #MINIMAP_Top when a notification arrives
 *   (GA_TEXTEDITOR_Prop_First). #MINIMAP_Dragging tells when a drag starts
 *   and ends; the drag ends with a GADGETUP as well.
 * - Before the connected editor is disposed, set #MINIMAP_TextEditor to
 *   NULL (or another editor). This waits for a running job.
 */
/*
**  $VER: minimap.h 1.0 (18.10.2026)
**  Copyright (c) 2026 André Gewert <agewert@ubergeek.de>, MIT License
*/
#ifndef GADGETS_MINIMAP_H
#define GADGETS_MINIMAP_H

#ifndef UTILITY_TAGITEM_H
#include <utility/tagitem.h>
#endif

#define MINIMAP_CLASSNAME   "minimap.gadget"    /**< public name of the class */
#define MINIMAP_VERSION     1                   /**< library version this header describes */

#define MINIMAP_Dummy       (TAG_USER + 0x04A90000) /**< base of the class's tags */

/** @name Attributes */
/** @{ */

/**
 * @brief The texteditor.gadget whose text is shown.
 *
 * NULL shows an empty minimap. Changing it waits for a running render job.
 *
 * Type: Object *. Applicability: I S G. Default: NULL.
 */
#define MINIMAP_TextEditor  (MINIMAP_Dummy + 1)

/**
 * @brief Hook that colours one line.
 *
 * Called from the render process with the minimap object and a struct
 * MinimapLineMsg. It returns the state for the next line (e.g. inside a
 * block comment). It must not use DOS or Intuition and must stay valid
 * until it is replaced (changing it waits for a running job). NULL draws
 * everything with #MINIMAP_TextPen.
 *
 * Type: struct Hook *. Applicability: I S. Default: NULL.
 */
#define MINIMAP_ColorHook   (MINIMAP_Dummy + 2)

/**
 * @brief Number of text columns mapped onto the gadget's width.
 *
 * Type: ULONG. Applicability: I. Default: 128.
 */
#define MINIMAP_Columns     (MINIMAP_Dummy + 3)

/**
 * @brief Pen of the text.
 *
 * Type: LONG. Applicability: I S. Default: -1, the screen's TEXTPEN.
 */
#define MINIMAP_TextPen         (MINIMAP_Dummy + 4)

/**
 * @brief Pen of the background.
 *
 * Type: LONG. Applicability: I S. Default: -1, the screen's BACKGROUNDPEN.
 */
#define MINIMAP_BackgroundPen   (MINIMAP_Dummy + 5)

/**
 * @brief Pen that tints the part the editor shows.
 *
 * Type: LONG. Applicability: I S. Default: -1, #MINIMAP_BackgroundPen.
 */
#define MINIMAP_ViewFillPen     (MINIMAP_Dummy + 6)

/**
 * @brief Pen of the frame around the part the editor shows.
 *
 * Type: LONG. Applicability: I S. Default: -1, the screen's SHADOWPEN.
 */
#define MINIMAP_ViewBorderPen   (MINIMAP_Dummy + 7)

/**
 * @brief The editor's text changed: render it again at the next update.
 *
 * Type: BOOL. Applicability: S.
 */
#define MINIMAP_TextChanged (MINIMAP_Dummy + 8)

/**
 * @brief Show a finished picture and start a render job if needed.
 *
 * Must be set with SetGadgetAttrs() from the application's process.
 *
 * Type: BOOL. Applicability: S.
 */
#define MINIMAP_Update      (MINIMAP_Dummy + 9)

/**
 * @brief Signal(s) of the application's process that a render job
 *        finished; then set #MINIMAP_Update.
 *
 * Type: ULONG. Applicability: G.
 */
#define MINIMAP_SignalMask  (MINIMAP_Dummy + 10)

/**
 * @brief Line the editor should show at its top, chosen with the mouse so
 *        that the visible part is centred on the pointer.
 *
 * Type: ULONG. Applicability: G N.
 */
#define MINIMAP_Top         (MINIMAP_Dummy + 11)

/**
 * @brief TRUE while the user drags in the minimap.
 *
 * Type: BOOL. Applicability: G N.
 */
#define MINIMAP_Dragging    (MINIMAP_Dummy + 12)

/** @} */

/** Message of #MINIMAP_ColorHook: one line to colour. */
struct MinimapLineMsg {
    STRPTR mlm_Text;        /**< the line, NUL-terminated, without line end */
    ULONG  mlm_Length;      /**< its length */
    UBYTE *mlm_Pens;        /**< one pen per character, preset to TextPen */
    ULONG  mlm_State;       /**< returned by the hook for the previous line,
                               0 for the first line */
};

#endif /* GADGETS_MINIMAP_H */
