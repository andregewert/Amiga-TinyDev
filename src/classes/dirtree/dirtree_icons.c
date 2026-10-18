/*
 * dirtree_icons.c - the small entry symbols of dirtree.gadget
 *
 * A private subclass of imageclass that draws one of a few built-in pixel
 * motifs (12 x 11): drawer, open drawer, file, source code, script and
 * picture. No files are loaded. The colours are obtained on the screen of
 * the tree with ObtainBestPen() the first time it is rendered; until then
 * (or if that fails) the DrawInfo pens of the screen are used.
 *
 * Copyright (c) 2026 André Gewert <agewert@ubergeek.de>
 * Released under the MIT License, see LICENSE.
 */
#include <dos/dos.h>
#include <graphics/rastport.h>
#include <intuition/classes.h>
#include <intuition/classusr.h>
#include <intuition/imageclass.h>
#include <intuition/screens.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/utility.h>

#include <string.h>

#include "classlib.h"
#include "dirtree_icons.h"

#define ICON_W 12
#define ICON_H 11

/* Tags of the private class (not public). */
#define DTIA_Kind (TAG_USER + 0x04A80F01)
#define DTIA_Pens (TAG_USER + 0x04A80F02)

/* Colours: K outline, D dark, W white, G grey, Y/O folder light/dark,
 * B blue, R red, N green, C light blue. '.' is transparent. */
static const char letters[DTI_COLOURS] = { 'K', 'D', 'W', 'G', 'Y', 'O', 'B', 'R', 'N', 'C' };
static const ULONG rgb[DTI_COLOURS] = {
    0x282830, 0x484854, 0xFFFFFF, 0x9696A2, 0xFFD460,
    0xD29628, 0x3060D0, 0xD03828, 0x28963C, 0x96C4FF
};
/* DrawInfo pens used before (or without) obtained pens. */
static const UWORD fallback[DTI_COLOURS] = {
    TEXTPEN, SHADOWPEN, SHINEPEN, SHADOWPEN, FILLPEN,
    SHADOWPEN, FILLPEN, TEXTPEN, TEXTPEN, BACKGROUNDPEN
};

static const char *const motifs[DTI_COUNT][ICON_H] = {
    {   /* DTI_DRAWER */
        "............",
        ".KKKK.......",
        "KYYYYK......",
        "KYYYYKKKKKK.",
        "KOOOOOOOOOOK",
        "KYYYYYYYYYYK",
        "KYYYYYYYYYYK",
        "KYYYYYYYYYYK",
        "KYYYYYYYYYYK",
        "KOOOOOOOOOOK",
        ".KKKKKKKKKK.",
    },
    {   /* DTI_DRAWER_OPEN */
        "............",
        ".KKKK.......",
        "KOOOOK......",
        "KOOOOKKKKK..",
        "KOWWWWWWWK..",
        "KKKKKKKKKKKK",
        "KYYYYYYYYYYK",
        ".KYYYYYYYYYK",
        ".KYYYYYYYYK.",
        "..KYYYYYYYK.",
        "..KKKKKKKKK.",
    },
    {   /* DTI_FILE */
        ".KKKKKKK....",
        ".KWWWWWKK...",
        ".KWGGGWKWK..",
        ".KWWWWWKKKK.",
        ".KWGGGGGGWK.",
        ".KWWWWWWWWK.",
        ".KWGGGGGWWK.",
        ".KWWWWWWWWK.",
        ".KWGGGGGGWK.",
        ".KWWWWWWWWK.",
        ".KKKKKKKKKK.",
    },
    {   /* DTI_SOURCE */
        ".KKKKKKK....",
        ".KWWWWWKK...",
        ".KWRRRWKWK..",
        ".KWWWWWKKKK.",
        ".KWBBGGGGWK.",
        ".KWWWWWWWWK.",
        ".KWWWBBGNWK.",
        ".KWWWWWWWWK.",
        ".KWWWGGNNWK.",
        ".KWWWWWWWWK.",
        ".KKKKKKKKKK.",
    },
    {   /* DTI_SCRIPT */
        "............",
        ".KKKKKKKKKK.",
        ".KBBBBBBBBK.",
        ".KKKKKKKKKK.",
        ".KDDDDDDDDK.",
        ".KDNDDDDDDK.",
        ".KDDNDDDDDK.",
        ".KDNDDNNNDK.",
        ".KDDDDDDDDK.",
        ".KKKKKKKKKK.",
        "............",
    },
    {   /* DTI_PICTURE */
        "............",
        ".KKKKKKKKKK.",
        ".KCCCCCCCCK.",
        ".KCCCCCYYCK.",
        ".KCCCCCYYCK.",
        ".KCCNCCCCCK.",
        ".KCNNNCCCCK.",
        ".KNNNNNCNCK.",
        ".KNNNNNNNNK.",
        ".KKKKKKKKKK.",
        "............",
    },
};

/** Instance data of a symbol. */
typedef struct IconData {
    int kind;
    IconPens *pens;
} IconData;

static Class *icon_class = NULL;

/**
 * @brief Index of a motif letter in the colour tables.
 * @param c The letter.
 * @return Its index, or -1 for transparent.
 */
static int colour_index(char c)
{
    int i;
    for (i = 0; i < DTI_COLOURS; ++i)
        if (letters[i] == c) return i;
    return -1;
}

/**
 * @brief Draw a symbol: one RectFill() per run of equal pixels.
 * @param d The symbol.
 * @param rp The RastPort.
 * @param x Left edge.
 * @param y Top edge.
 * @param dri DrawInfo for the fallback pens, may be NULL.
 */
static void draw_icon(IconData *d, struct RastPort *rp, WORD x, WORD y, struct DrawInfo *dri)
{
    const char *const *rows = motifs[d->kind];
    int row, col;
    for (row = 0; row < ICON_H; ++row) {
        for (col = 0; col < ICON_W; ) {
            int ci = colour_index(rows[row][col]), end = col + 1;
            LONG pen;
            while (end < ICON_W && rows[row][end] == rows[row][col]) ++end;
            if (ci >= 0) {
                if (d->pens != NULL && d->pens->ok && d->pens->pens[ci] >= 0)
                    pen = d->pens->pens[ci];
                else if (dri != NULL && fallback[ci] < dri->dri_NumPens)
                    pen = dri->dri_Pens[fallback[ci]];
                else
                    pen = ci == 2 ? 2 : 1;
                SetAPen(rp, (UBYTE)pen);
                RectFill(rp, (WORD)(x + col), (WORD)(y + row), (WORD)(x + end - 1), (WORD)(y + row));
            }
            col = end;
        }
    }
}

/**
 * @brief Dispatcher of the symbol class.
 * @param cl The class.
 * @param o The object (the class for OM_NEW).
 * @param msg The message.
 * @return The method's result.
 */
static ULONG icon_dispatcher(Class *cl __asm("a0"), Object *o __asm("a2"), Msg msg __asm("a1"))
{
    switch (msg->MethodID) {
        case OM_NEW: {
            struct opSet *ops = (struct opSet *)msg;
            Object *obj = (Object *)class_dsm(cl, o, msg);
            if (obj != NULL) {
                IconData *d = INST_DATA(cl, obj);
                d->kind = (int)GetTagData(DTIA_Kind, DTI_FILE, ops->ops_AttrList);
                if (d->kind >= DTI_COUNT) d->kind = DTI_FILE;
                d->pens = (IconPens *)GetTagData(DTIA_Pens, 0, ops->ops_AttrList);
            }
            return (ULONG)obj;
        }
        case IM_DRAW:
        case IM_DRAWFRAME: {
            struct impDraw *imp = (struct impDraw *)msg;
            struct Image *im = (struct Image *)o;
            if (((IconData *)INST_DATA(cl, o))->kind < 0) return 0;     /* blank */
            draw_icon(INST_DATA(cl, o), imp->imp_RPort,
                      (WORD)(im->LeftEdge + imp->imp_Offset.X),
                      (WORD)(im->TopEdge + imp->imp_Offset.Y), imp->imp_DrInfo);
            return 0;
        }
        default:
            return class_dsm(cl, o, msg);
    }
}

/**
 * @brief Create the private image class of the symbols.
 * @return TRUE on success.
 */
BOOL dirtree_icons_init(void)
{
    icon_class = MakeClass(NULL, (STRPTR)"imageclass", NULL, sizeof(IconData), 0);
    if (icon_class == NULL) return FALSE;
    icon_class->cl_Dispatcher.h_Entry = (ULONG (*)())icon_dispatcher;
    return TRUE;
}

/** @brief Free the private image class. */
void dirtree_icons_exit(void)
{
    if (icon_class != NULL) FreeClass(icon_class);
    icon_class = NULL;
}

/**
 * @brief Create a symbol image.
 * @param kind A DTI_ kind.
 * @param pens The shared pens.
 * @return The image object, or NULL.
 */
Object *dirtree_icon_new(int kind, IconPens *pens)
{
    if (icon_class == NULL) return NULL;
    return NewObject(icon_class, NULL,
        IA_Width, ICON_W,
        IA_Height, ICON_H,
        DTIA_Kind, (ULONG)kind,
        DTIA_Pens, (ULONG)pens,
        TAG_END);
}

/**
 * @brief Create an invisible image (kind -1, draws nothing).
 * @param width Its width.
 * @param height Its height.
 * @return The image object, or NULL.
 */
Object *dirtree_blank_new(WORD width, WORD height)
{
    if (icon_class == NULL) return NULL;
    return NewObject(icon_class, NULL,
        IA_Width, (ULONG)width,
        IA_Height, (ULONG)height,
        DTIA_Kind, (ULONG)-1,
        TAG_END);
}

/**
 * @brief Obtain the symbols' colours on a screen (once).
 * @param pens The shared pens.
 * @param screen The screen.
 * @param dri The screen's DrawInfo (unused, for later colours), may be NULL.
 */
void dirtree_icons_obtain(IconPens *pens, struct Screen *screen, struct DrawInfo *dri)
{
    int i;
    if (pens->ok || screen == NULL) return;
    pens->cm = screen->ViewPort.ColorMap;
    (void)dri;
    for (i = 0; i < DTI_COLOURS; ++i) {
        ULONG c = rgb[i];
        pens->pens[i] = ObtainBestPen(pens->cm,
            ((c >> 16) & 0xFF) * 0x01010101UL, ((c >> 8) & 0xFF) * 0x01010101UL,
            (c & 0xFF) * 0x01010101UL, OBP_Precision, PRECISION_IMAGE, TAG_END);
    }
    pens->ok = 1;
}

/**
 * @brief Release pens obtained by dirtree_icons_obtain().
 * @param pens The shared pens.
 */
void dirtree_icons_release(IconPens *pens)
{
    int i;
    if (!pens->ok) return;
    for (i = 0; i < DTI_COLOURS; ++i)
        if (pens->pens[i] >= 0) ReleasePen(pens->cm, (ULONG)pens->pens[i]);
    pens->ok = 0;
}

/** @brief Width of the symbols. @return The width in pixels. */
WORD dirtree_icon_width(void)
{
    return ICON_W;
}

/** @brief Height of the symbols. @return The height in pixels. */
WORD dirtree_icon_height(void)
{
    return ICON_H;
}

/**
 * @brief Report whether a name ends with one of the given extensions.
 * @param name The file name.
 * @param list Extensions without dot, separated by '|'.
 * @return Non-zero on a match (case-insensitive).
 */
static int has_extension(const char *name, const char *list)
{
    const char *dot = strrchr(name, '.');
    const char *p = list;
    if (dot == NULL || dot[1] == '\0') return 0;
    ++dot;
    while (*p != '\0') {
        const char *e = p;
        size_t n;
        while (*e != '\0' && *e != '|') ++e;
        n = (size_t)(e - p);
        if (strlen(dot) == n && Strnicmp((STRPTR)dot, (STRPTR)p, (LONG)n) == 0) return 1;
        p = *e != '\0' ? e + 1 : e;
    }
    return 0;
}

/**
 * @brief Choose the symbol of a file.
 * @param name The file name.
 * @param protection fib_Protection of the file.
 * @return A DTI_ kind.
 */
int dirtree_icon_kind(const char *name, ULONG protection)
{
    if (has_extension(name, "c|h|cc|cpp|hpp|s|asm|i|e|py|rexx|rx|lua|sfd|fd|mk"))
        return DTI_SOURCE;
    if ((protection & FIBF_SCRIPT) != 0 || has_extension(name, "script|dos|sh"))
        return DTI_SCRIPT;
    if (has_extension(name, "iff|ilbm|png|jpg|jpeg|gif|bmp|lbm|pic"))
        return DTI_PICTURE;
    return DTI_FILE;
}
