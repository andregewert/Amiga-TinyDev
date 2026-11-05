/**
 * @file statuslamp.c
 * @brief The lamp at the right end of the status line: whether Scout is
 *        still working for the project, as a traffic light.
 *
 * Green: Scout is ready and has nothing to do; yellow: it works (it
 * starts, opens the project's session, indexes the project or analyses a
 * document); red: it could not be started; an empty lamp: Scout is
 * switched off in the settings. The state comes from scoutlink_activity()
 * and is looked at whenever the status line is (ui_update_status()); the
 * lamp is drawn again only when it changes.
 *
 * Its help bubble tells the state in words, with the number of jobs
 * waiting while the project is indexed. It takes no clicks: space.gadget
 * sends no GADGETUP, and the window's root layout, not the lamp, gets
 * the GADGETDOWN of a GA_Immediate child (tried for a double-click that
 * was to open Scout's window).
 *
 * The lamp is a space.gadget with a render hook. space.gadget 47 does not
 * clear its area before calling the hook again, so the hook does.
 */
#include "editor.h"

#include <intuition/gadgetclass.h>
#include <gadgets/space.h>
#include <images/bevel.h>
#include <graphics/view.h>

#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/space.h>
#include <clib/alib_protos.h>

#include <stdio.h>

/** Colours of the lamp (RRGGBB): ready, working, failed. */
static const unsigned long lamp_colours[3] = { 0x2E9E3A, 0xE8B000, 0xC8281E };

/** The state of the lamp. */
static struct {
    Object *gadget;             /**< the space.gadget (owned by the window's layout) */
    struct Hook hook;           /**< its render hook */
    struct ColorMap *cm;        /**< where the pens come from, NULL without them */
    LONG pens[3];               /**< pens of lamp_colours, -1 if none */
    int shown;                  /**< the SCOUT_ACT_ value drawn, -1 before the first */
    char hint[120];             /**< the text of its help bubble */
} lamp = { NULL, {{NULL, NULL}, NULL, NULL, NULL}, NULL, {-1, -1, -1}, -1, "" };

/**
 * @brief Write the help bubble's text for a state.
 * @param state The SCOUT_ACT_ value.
 */
static void make_hint(int state)
{
    long waiting;
    switch (state) {
    case SCOUT_ACT_IDLE: snprintf(lamp.hint, sizeof(lamp.hint), "%s", S(MSG_LAMP_IDLE)); break;
    case SCOUT_ACT_BUSY:
        if ((waiting = scoutlink_indexing()) > 0)
            snprintf(lamp.hint, sizeof(lamp.hint), S(MSG_LAMP_JOBS), waiting);
        else snprintf(lamp.hint, sizeof(lamp.hint), "%s", S(MSG_LAMP_BUSY));
        break;
    case SCOUT_ACT_FAILED: snprintf(lamp.hint, sizeof(lamp.hint), "%s", S(MSG_LAMP_FAILED)); break;
    default: snprintf(lamp.hint, sizeof(lamp.hint), "%s", S(MSG_LAMP_OFF)); break;
    }
}

/**
 * @brief Render hook: the lamp, a small square with rounded corners in
 *        the middle of the gadget. Graphics only, it may run during input
 *        handling.
 * @param hook The hook.
 * @param obj The space.gadget.
 * @param msg The render message.
 * @return 0.
 */
static ULONG render_lamp(struct Hook *hook, Object *obj, struct gpRender *msg)
{
    struct RastPort *rp = msg->gpr_RPort;
    struct IBox *box = NULL;
    UWORD *dpens;
    WORD size, x, y, right, bottom;
    UWORD fill;
    (void)hook;
    if (rp == NULL || msg->gpr_GInfo == NULL || msg->gpr_GInfo->gi_DrInfo == NULL) return 0;
    GetAttr(SPACE_AreaBox, obj, (ULONG *)&box);
    if (box == NULL || box->Width <= 0 || box->Height <= 0) return 0;
    dpens = msg->gpr_GInfo->gi_DrInfo->dri_Pens;
    EraseRect(rp, box->Left, box->Top, (WORD)(box->Left + box->Width - 1),
              (WORD)(box->Top + box->Height - 1));
    size = (WORD)((box->Width < box->Height ? box->Width : box->Height) - 4);
    if (size < 5) return 0;
    x = (WORD)(box->Left + (box->Width - size) / 2);
    y = (WORD)(box->Top + (box->Height - size) / 2);
    right = (WORD)(x + size - 1);
    bottom = (WORD)(y + size - 1);
    switch (lamp.shown) {
    case SCOUT_ACT_IDLE: fill = lamp.pens[0] >= 0 ? (UWORD)lamp.pens[0] : dpens[FILLPEN]; break;
    case SCOUT_ACT_BUSY: fill = lamp.pens[1] >= 0 ? (UWORD)lamp.pens[1] : dpens[HIGHLIGHTTEXTPEN]; break;
    case SCOUT_ACT_FAILED: fill = lamp.pens[2] >= 0 ? (UWORD)lamp.pens[2] : dpens[SHADOWPEN]; break;
    default: fill = dpens[BACKGROUNDPEN]; break;     /* off: an empty lamp */
    }
    SetDrMd(rp, JAM1);
    SetAPen(rp, (UBYTE)fill);
    RectFill(rp, (WORD)(x + 1), (WORD)(y + 1), (WORD)(right - 1), (WORD)(bottom - 1));
    /* the edge without its corners */
    SetAPen(rp, (UBYTE)dpens[SHADOWPEN]);
    Move(rp, (WORD)(x + 1), y); Draw(rp, (WORD)(right - 1), y);
    Move(rp, (WORD)(x + 1), bottom); Draw(rp, (WORD)(right - 1), bottom);
    Move(rp, x, (WORD)(y + 1)); Draw(rp, x, (WORD)(bottom - 1));
    Move(rp, right, (WORD)(y + 1)); Draw(rp, right, (WORD)(bottom - 1));
    /* a glint in the upper left of a lit lamp */
    if (lamp.shown != SCOUT_ACT_OFF && size >= 8) {
        SetAPen(rp, (UBYTE)dpens[SHINEPEN]);
        Move(rp, (WORD)(x + 2), (WORD)(y + 3)); Draw(rp, (WORD)(x + 2), (WORD)(y + 2));
        Draw(rp, (WORD)(x + 3), (WORD)(y + 2));
    }
    return 0;
}

Object *statuslamp_new(EditorApp *app)
{
    WORD h = app->screen->Font != NULL ? (WORD)app->screen->Font->ta_YSize : 8;
    int i;
    if (lamp.cm == NULL) {
        lamp.cm = app->screen->ViewPort.ColorMap;
        for (i = 0; i < 3; ++i) {
            unsigned long c = lamp_colours[i];
            lamp.pens[i] = ObtainBestPen(lamp.cm, ((c >> 16) & 0xFF) * 0x01010101UL,
                                         ((c >> 8) & 0xFF) * 0x01010101UL, (c & 0xFF) * 0x01010101UL,
                                         OBP_Precision, PRECISION_GUI, OBP_FailIfBad, FALSE, TAG_END);
        }
    }
    lamp.hook.h_Entry = (ULONG (*)())HookEntry;
    lamp.hook.h_SubEntry = (ULONG (*)())render_lamp;
    lamp.shown = scoutlink_activity(app);
    make_hint(lamp.shown);
    lamp.gadget = NewObject(SPACE_GetClass(), NULL,
        GA_ID, GID_STATUSLAMP,
        SPACE_MinWidth, (ULONG)(h + 8), SPACE_MinHeight, (ULONG)(h + 2),
        SPACE_BevelStyle, BVS_DISPLAY,
        SPACE_RenderHook, (ULONG)&lamp.hook, TAG_END);
    return lamp.gadget;
}

void statuslamp_update(EditorApp *app)
{
    int now;
    if (lamp.gadget == NULL) return;
    now = scoutlink_activity(app);
    make_hint(now);                     /* the jobs waiting change while it is yellow */
    if (now == lamp.shown) return;
    lamp.shown = now;
    if (app->window != NULL) RefreshGList((struct Gadget *)lamp.gadget, app->window, NULL, 1);
}

const char *statuslamp_hint(void)
{
    return lamp.hint;
}

void statuslamp_cleanup(EditorApp *app)
{
    int i;
    (void)app;
    lamp.gadget = NULL;                 /* the window disposed it */
    lamp.shown = -1;
    if (lamp.cm == NULL) return;
    for (i = 0; i < 3; ++i) {
        if (lamp.pens[i] >= 0) ReleasePen(lamp.cm, (ULONG)lamp.pens[i]);
        lamp.pens[i] = -1;
    }
    lamp.cm = NULL;
}
