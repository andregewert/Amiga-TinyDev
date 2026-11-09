/*
 * {$NAME} - {$PURPOSE}
 *
 * Copyright (c) {$YEAR} {$AUTHOR}
 * {$AUTHORMAIL}
 * Created {$DATE}
 */
#include <exec/types.h>
#include <intuition/intuition.h>
#include <intuition/classes.h>
#include <intuition/classusr.h>
#include <intuition/cghooks.h>
#include <intuition/gadgetclass.h>
#include <intuition/imageclass.h>
#include <intuition/screens.h>
#include <utility/hooks.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/utility.h>

#include <gadgets/{$NAME.lower}.h>
#include "{$NAME}_private.h"

/* The data of one object. */
struct Data {
    BOOL on;    /* {$NAME}_On */
};

/* Send a message on to the superclass. */
static ULONG super_method(Class *cl, Object *o, Msg msg)
{
    return CallHookPkt(&cl->cl_Super->cl_Dispatcher, o, msg);
}

/* Draw the gadget. */
static void render(Class *cl, struct Gadget *g, struct RastPort *rp, struct GadgetInfo *gi)
{
    struct Data *data = INST_DATA(cl, g);
    UWORD *pens = gi->gi_DrInfo->dri_Pens;
    WORD x0 = g->LeftEdge, y0 = g->TopEdge;
    WORD x1 = x0 + g->Width - 1, y1 = y0 + g->Height - 1;

    SetAPen(rp, (ULONG)pens[data->on ? FILLPEN : BACKGROUNDPEN]);
    RectFill(rp, x0, y0, x1, y1);
    SetAPen(rp, (ULONG)pens[SHADOWPEN]);
    Move(rp, x0, y0);
    Draw(rp, x1, y0);
    Draw(rp, x1, y1);
    Draw(rp, x0, y1);
    Draw(rp, x0, y0);
}

/* Draw the gadget outside of GM_RENDER. */
static void redraw(Class *cl, struct Gadget *g, struct GadgetInfo *gi)
{
    struct RastPort *rp;

    if (gi != NULL && (rp = ObtainGIRPort(gi)) != NULL) {
        render(cl, g, rp, gi);
        ReleaseGIRPort(rp);
    }
}

/* OM_NEW and OM_SET: take over the attributes; returns non-zero on a change. */
static int set_attrs(Class *cl, Object *o, struct TagItem *tags)
{
    struct Data *data = INST_DATA(cl, o);
    BOOL on = GetTagData({$NAME}_On, data->on, tags) ? TRUE : FALSE;
    int changed = on != data->on;

    data->on = on;
    return changed;
}

/* The class's dispatcher, called with its parameters in registers. */
static ULONG dispatcher(REG(a0, Class *cl), REG(a2, Object *o), REG(a1, Msg msg))
{
    ULONG result;

    switch (msg->MethodID) {
    case OM_NEW:
        result = super_method(cl, o, msg);
        if (result != 0) {
            ((struct Data *)INST_DATA(cl, result))->on = FALSE;
            set_attrs(cl, (Object *)result, ((struct opSet *)msg)->ops_AttrList);
        }
        return result;

    case OM_SET:
    case OM_UPDATE:
        result = super_method(cl, o, msg);
        if (set_attrs(cl, o, ((struct opSet *)msg)->ops_AttrList)) {
            redraw(cl, (struct Gadget *)o, ((struct opSet *)msg)->ops_GInfo);
            result = 1;
        }
        return result;

    case OM_GET:
        if (((struct opGet *)msg)->opg_AttrID == {$NAME}_On) {
            *((struct opGet *)msg)->opg_Storage = ((struct Data *)INST_DATA(cl, o))->on;
            return 1;
        }
        return super_method(cl, o, msg);

    case GM_RENDER:
        render(cl, (struct Gadget *)o, ((struct gpRender *)msg)->gpr_RPort,
               ((struct gpRender *)msg)->gpr_GInfo);
        return 1;

    case GM_HITTEST:
        return GMR_GADGETHIT;

    case GM_GOACTIVE: {
        struct Data *data = INST_DATA(cl, o);

        data->on = data->on ? FALSE : TRUE;
        redraw(cl, (struct Gadget *)o, ((struct gpInput *)msg)->gpi_GInfo);
        *((struct gpInput *)msg)->gpi_Termination = data->on;
        return GMR_NOREUSE | GMR_VERIFY;
    }

    default:
        return super_method(cl, o, msg);
    }
}

Class *{$NAME}_create_class(const char *name)
{
    Class *cl = MakeClass((STRPTR)name, (STRPTR)"gadgetclass", NULL, sizeof(struct Data), 0);

    if (cl != NULL) cl->cl_Dispatcher.h_Entry = (ULONG (*)())dispatcher;
    return cl;
}
