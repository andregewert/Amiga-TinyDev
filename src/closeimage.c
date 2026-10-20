/**
 * @file closeimage.c
 * @brief The close symbol of the editor tabs: a built-in cross, drawn by a small private image class.
 *
 * The close symbol of the editor tabs: a simple black cross, drawn by a
 * small private image class instead of loading an AISS image. The cross is
 * 6 x 6 pixels with strokes 2 pixels wide (symmetric, no thin end rows),
 * centred in an 8 x 8 image so it keeps a pixel of room on every side; it
 * is drawn in TEXTPEN (black on the Workbench), while pressed (state
 * IDS_SELECTED) in SHINEPEN (white).
 */
#include "editor.h"

#include <graphics/rastport.h>
#include <intuition/classes.h>
#include <intuition/classusr.h>
#include <intuition/imageclass.h>
#include <intuition/screens.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <clib/alib_protos.h>

#define CLOSE_SIZE 8                    /**< the image */

/**
 * The cross, 6 x 6 pixels ('#' is drawn).
 */
static const char *const cross[6] = {
    "##..##",
    ".####.",
    "..##..",
    "..##..",
    ".####.",
    "##..##",
};

/**
 * The private image class of the close symbol.
 */
static Class *close_class = NULL;

/**
 * @brief Draw the cross into the image with its upper left corner at (x, y).
 * @param rp The RastPort.
 * @param x Left edge of the image.
 * @param y Top edge of the image.
 * @param dri DrawInfo for the pen, may be NULL.
 * @param selected Non-zero while the close gadget is pressed.
 */
static void draw_cross(struct RastPort *rp, WORD x, WORD y, struct DrawInfo *dri, int selected)
{
    WORD row, col;
    UWORD pen = (UWORD)(selected ? 2 : 1);
    if (dri != NULL) pen = dri->dri_Pens[selected ? SHINEPEN : TEXTPEN];
    SetAPen(rp, (UBYTE)pen);
    for (row = 0; row < 6; ++row)
        for (col = 0; col < 6; ++col)
            if (cross[row][col] == '#')
                RectFill(rp, (WORD)(x + 1 + col), (WORD)(y + 1 + row),
                         (WORD)(x + 1 + col), (WORD)(y + 1 + row));
}

/**
 * @brief Dispatcher of the close symbol class (h_SubEntry of HookEntry).
 * @param cl The class.
 * @param o The object.
 * @param msg The message.
 * @return The method's result.
 */
static ULONG close_dispatch(Class *cl, Object *o, Msg msg)
{
    if (msg->MethodID == IM_DRAW || msg->MethodID == IM_DRAWFRAME) {
        struct impDraw *imp = (struct impDraw *)msg;
        struct Image *im = (struct Image *)o;
        draw_cross(imp->imp_RPort, (WORD)(im->LeftEdge + imp->imp_Offset.X),
                   (WORD)(im->TopEdge + imp->imp_Offset.Y), imp->imp_DrInfo,
                   imp->imp_State == IDS_SELECTED || imp->imp_State == IDS_INACTIVESELECTED);
        return 0;
    }
    return DoSuperMethodA(cl, o, msg);
}

Object *close_image_new(void)
{
    if (close_class == NULL) {
        close_class = MakeClass(NULL, (STRPTR)"imageclass", NULL, 0, 0);
        if (close_class == NULL) return NULL;
        close_class->cl_Dispatcher.h_Entry = (ULONG (*)())HookEntry;
        close_class->cl_Dispatcher.h_SubEntry = (ULONG (*)())close_dispatch;
    }
    return NewObject(close_class, NULL, IA_Width, CLOSE_SIZE, IA_Height, CLOSE_SIZE, TAG_END);
}

void close_image_free_class(void)
{
    if (close_class != NULL && FreeClass(close_class)) close_class = NULL;
}
