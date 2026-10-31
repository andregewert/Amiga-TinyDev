/**
 * @file tmplsym.c
 * @brief The symbols of the template icon types (generic, program,
 *        library, class, script, document), drawn by a small private image
 *        class.
 *
 * Templates name an icon type in template.json; the wizard of
 * Project/New Project... shows its symbol small in the list of templates
 * (tmplsym_draw() from the list's render hook) and enlarged next to it (an
 * image of this class). The symbols are 12 x 12 pixels in the screen's
 * pens, drawn scaled by a whole number; an unknown type gets the generic
 * one. Graphics only, so the drawing may run during input handling.
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
#include <proto/utility.h>
#include <clib/alib_protos.h>

#include <string.h>

/**
 * The symbols, TMPLSYM_SIZE pixels square, in the order of TmplSymType:
 * 'o' is drawn in TEXTPEN, '#' in SHINEPEN, '+' in FILLPEN; '.' is
 * transparent.
 */
static const char *const symbols[TMPLSYM_COUNT][TMPLSYM_SIZE] = {
    {   /* generic: a sheet with a folded corner */
        "..oooooo....", "..o####oo...", "..o####o#o..", "..o####oooo.",
        "..o#######o.", "..o#######o.", "..o#######o.", "..o#######o.",
        "..o#######o.", "..o#######o.", "..o#######o.", "..ooooooooo.",
    },
    {   /* program: a window with a title bar */
        "oooooooooooo", "o+++++++o#oo", "oooooooooooo", "o##########o",
        "o#++++++###o", "o##########o", "o#+++++####o", "o##########o",
        "o#++++#####o", "o##########o", "o#########oo", "oooooooooooo",
    },
    {   /* library: books on a shelf */
        "ooo.........", "o#o.ooo.....", "o#o.o+o.ooo.", "ooo.o+o.o#o.",
        "o#o.ooo.o#o.", "o#o.o+o.ooo.", "o#o.o+o.o#o.", "o#o.o+o.o#o.",
        "o#o.o+o.o#o.", "ooo.ooo.ooo.", "o#o.o+o.o#o.", "oooooooooooo",
    },
    {   /* class: a building brick */
        "............", "..oo....oo..", ".o++o..o++o.", "oooooooooooo",
        "o++++++++++o", "o+########+o", "o+++++++++oo", "o++++++++++o",
        "o++++++++++o", "o+++++++++oo", "oooooooooooo", "............",
    },
    {   /* script: a Shell window with a prompt */
        "oooooooooooo", "o++++++++++o", "oooooooooooo", "o##########o",
        "o#o########o", "o##o#######o", "o###o######o", "o##o#######o",
        "o#o##oooo##o", "o##########o", "o##########o", "oooooooooooo",
    },
    {   /* document: a sheet with lines of text */
        "..oooooo....", "..o####oo...", "..o####o#o..", "..o####oooo.",
        "..o#######o.", "..o#+++++#o.", "..o#######o.", "..o#+++++#o.",
        "..o#######o.", "..o#++++##o.", "..o#######o.", "..ooooooooo.",
    },
};

/** Names of the icon types in template.json, in the order of TmplSymType. */
static const char *const type_names[TMPLSYM_COUNT] = {
    "generic", "program", "library", "class", "script", "document"
};

/** Instance data of a symbol image. */
typedef struct SymData {
    ULONG type;         /**< the TmplSymType shown */
    UWORD scale;        /**< pixels per symbol pixel */
} SymData;

/**
 * The private image class of the enlarged symbol.
 */
static Class *sym_class = NULL;

TmplSymType tmplsym_type(const char *name)
{
    int i;
    if (name != NULL)
        for (i = 0; i < TMPLSYM_COUNT; ++i)
            if (Stricmp((STRPTR)name, (STRPTR)type_names[i]) == 0) return (TmplSymType)i;
    return TMPLSYM_GENERIC;
}

void tmplsym_draw(struct RastPort *rp, WORD x, WORD y, struct DrawInfo *dri, TmplSymType type, UWORD scale)
{
    const char *const *rows = symbols[type < TMPLSYM_COUNT ? type : TMPLSYM_GENERIC];
    WORD row, col, end;
    if (scale == 0) scale = 1;
    for (row = 0; row < TMPLSYM_SIZE; ++row)
        for (col = 0; col < TMPLSYM_SIZE; col = end) {
            char c = rows[row][col];
            UWORD pen;
            /* a run of pixels of one pen in one RectFill() */
            for (end = (WORD)(col + 1); end < TMPLSYM_SIZE && rows[row][end] == c; ++end) ;
            if (c == '.') continue;
            pen = (UWORD)(c == 'o' ? 1 : c == '#' ? 2 : 3);
            if (dri != NULL) pen = dri->dri_Pens[c == 'o' ? TEXTPEN : c == '#' ? SHINEPEN : FILLPEN];
            SetAPen(rp, (UBYTE)pen);
            RectFill(rp, (WORD)(x + col * scale), (WORD)(y + row * scale),
                     (WORD)(x + end * scale - 1), (WORD)(y + (row + 1) * scale - 1));
        }
}

/**
 * @brief Dispatcher of the symbol class (h_SubEntry of HookEntry).
 * @param cl The class.
 * @param o The object.
 * @param msg The message.
 * @return The method's result.
 */
static ULONG sym_dispatch(Class *cl, Object *o, Msg msg)
{
    SymData *d;
    struct TagItem *tag;
    ULONG r;
    switch (msg->MethodID) {
        case OM_NEW:
            if ((r = DoSuperMethodA(cl, o, msg)) == 0) return 0;
            d = (SymData *)INST_DATA(cl, (Object *)r);
            d->type = GetTagData(TMPLSYM_Type, TMPLSYM_GENERIC, ((struct opSet *)msg)->ops_AttrList);
            d->scale = (UWORD)GetTagData(TMPLSYM_Scale, 1, ((struct opSet *)msg)->ops_AttrList);
            if (d->scale == 0) d->scale = 1;
            return r;
        case OM_SET:
            d = (SymData *)INST_DATA(cl, o);
            if ((tag = FindTagItem(TMPLSYM_Type, ((struct opSet *)msg)->ops_AttrList)) != NULL)
                d->type = tag->ti_Data < TMPLSYM_COUNT ? tag->ti_Data : TMPLSYM_GENERIC;
            return DoSuperMethodA(cl, o, msg);
        case IM_DOMAINFRAME: {
            /* layout.gadget asks for the size; the same for every domain */
            struct impDomainFrame *dom = (struct impDomainFrame *)msg;
            d = (SymData *)INST_DATA(cl, o);
            dom->imp_Domain.Left = dom->imp_Domain.Top = 0;
            dom->imp_Domain.Width = dom->imp_Domain.Height = (WORD)(TMPLSYM_SIZE * d->scale);
            return 1;
        }
        case IM_DRAW:
        case IM_DRAWFRAME: {
            struct impDraw *imp = (struct impDraw *)msg;
            struct Image *im = (struct Image *)o;
            d = (SymData *)INST_DATA(cl, o);
            /* centred in the image's box */
            tmplsym_draw(imp->imp_RPort,
                         (WORD)(im->LeftEdge + imp->imp_Offset.X + (im->Width - TMPLSYM_SIZE * d->scale) / 2),
                         (WORD)(im->TopEdge + imp->imp_Offset.Y + (im->Height - TMPLSYM_SIZE * d->scale) / 2),
                         imp->imp_DrInfo, (TmplSymType)d->type, d->scale);
            return 0;
        }
    }
    return DoSuperMethodA(cl, o, msg);
}

Object *tmplsym_image_new(UWORD scale)
{
    if (sym_class == NULL) {
        sym_class = MakeClass(NULL, (STRPTR)"imageclass", NULL, sizeof(SymData), 0);
        if (sym_class == NULL) return NULL;
        sym_class->cl_Dispatcher.h_Entry = (ULONG (*)())HookEntry;
        sym_class->cl_Dispatcher.h_SubEntry = (ULONG (*)())sym_dispatch;
    }
    return NewObject(sym_class, NULL, IA_Width, (ULONG)(TMPLSYM_SIZE * scale),
                     IA_Height, (ULONG)(TMPLSYM_SIZE * scale), TMPLSYM_Scale, (ULONG)scale, TAG_END);
}

void tmplsym_free_class(void)
{
    if (sym_class != NULL && FreeClass(sym_class)) sym_class = NULL;
}
