#include "editor.h"

#include <classes/window.h>
#include <diskfont/diskfont.h>
#include <gadgets/button.h>
#include <graphics/text.h>
#include <graphics/view.h>
#include <intuition/gadgetclass.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <workbench/icon.h>
#include <workbench/workbench.h>
#include <proto/button.h>
#include <proto/diskfont.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/icon.h>
#include <proto/intuition.h>
#include <clib/alib_protos.h>

#include <stdio.h>
#include <string.h>

/* The splash window while TinyDev starts, and the About window, both as in
 * Kickdown (ReAction style guide in amiga-devkit, sections 3.5 and 3.6).
 *
 * Opening the ReAction and TinyDev classes and loading the toolbar images
 * takes a while; meanwhile the splash window shows the program icon, name,
 * version and a progress bar. It is drawn with Intuition and
 * graphics.library only, so it opens before any class is loaded. It is
 * borderless, centred and not activated, and closes as soon as the main
 * window is open and before any error requester. The About window has the
 * same head (icon, title, subtitle, version, copyright) with the details and
 * a centred OK button below; it is modal and centred over the main window.
 *
 * Everything is optional: without the icon there is no icon, without a big
 * designed font the title uses the screen font in bold. */

#define MARGIN  14                      /* border to the contents */
#define GAP     12                      /* icon to text, between blocks */
#define BAR_H   6                       /* height of the progress bar */
#define SUBTITLE S(MSG_SUBTITLE)
#define COPYRIGHT "\251 2026 Andr\351 Gewert"

/** What the head of both windows needs. */
typedef struct Card {
    struct Screen *screen;
    struct DrawInfo *dri;
    struct DiskObject *icon;
    struct TextFont *big;               /**< title font, NULL: screen font bold */
    struct TextFont *font;              /**< screen font */
    LONG pen_bg, pen_title;             /**< obtained pens, -1: none */
    UWORD bg, title, text, shine, shadow, fill;
    WORD iconw, iconh;
    WORD textw, texth;                  /**< the text block of the head */
    WORD lineh;
    char version[64];
} Card;

/**
 * @brief Obtain the pen nearest to an 0xRRGGBB colour.
 * @param cm The screen's colour map.
 * @param rgb The colour.
 * @return The pen, or -1.
 */
static LONG obtain(struct ColorMap *cm, ULONG rgb)
{
    return ObtainBestPen(cm, (rgb >> 16 & 0xff) * 0x01010101UL, (rgb >> 8 & 0xff) * 0x01010101UL,
                         (rgb & 0xff) * 0x01010101UL, OBP_Precision, PRECISION_IMAGE, TAG_END);
}

/**
 * @brief The background: halfway between the window background and white.
 * @param c The card.
 * @return The colour as 0xRRGGBB.
 */
static ULONG light_grey(Card *c)
{
    ULONG b[3], s[3];
    struct ColorMap *cm = c->screen->ViewPort.ColorMap;
    GetRGB32(cm, c->dri->dri_Pens[BACKGROUNDPEN], 1, b);
    GetRGB32(cm, c->dri->dri_Pens[SHINEPEN], 1, s);
    return ((b[0] >> 25) + (s[0] >> 25)) << 16 | ((b[1] >> 25) + (s[1] >> 25)) << 8 |
           ((b[2] >> 25) + (s[2] >> 25));
}

/**
 * @brief Open a designed (not scaled) size of the screen font or Helvetica
 *        for the title: the biggest of 24, 18 and 15 pixels.
 * @param c The card.
 * @return The font, or NULL.
 */
static struct TextFont *title_font(Card *c)
{
    static const UWORD sizes[] = { 24, 18, 15 };
    const char *names[2];
    struct TextAttr ta;
    ULONG n, i;

    if (DiskfontBase == NULL &&
        (DiskfontBase = OpenLibrary((STRPTR)"diskfont.library", 39)) == NULL)
        return NULL;
    names[0] = c->screen->Font != NULL ? (const char *)c->screen->Font->ta_Name : "helvetica.font";
    names[1] = "helvetica.font";
    for (n = 0; n < 2; ++n)
        for (i = 0; i < sizeof(sizes) / sizeof(sizes[0]); ++i) {
            struct TextFont *f;
            ta.ta_Name = (STRPTR)names[n];
            ta.ta_YSize = sizes[i];
            ta.ta_Style = FS_NORMAL;
            ta.ta_Flags = FPF_DESIGNED;
            if ((f = OpenDiskFont(&ta)) != NULL) {
                if (f->tf_YSize == sizes[i] && (f->tf_Flags & FPF_DESIGNED) != 0) return f;
                CloseFont(f);
            }
        }
    return NULL;
}

/**
 * @brief Draw text with its top at (x, y).
 * @param rp The RastPort.
 * @param x Left edge.
 * @param y Top edge.
 * @param pen The pen.
 * @param s The text.
 * @param len Its length.
 */
static void text_at(struct RastPort *rp, WORD x, WORD y, UWORD pen, CONST_STRPTR s, ULONG len)
{
    SetAPen(rp, (UBYTE)pen);
    Move(rp, x, (WORD)(y + rp->TxBaseline));
    Text(rp, (STRPTR)s, (UWORD)len);
}

/**
 * @brief Width of a text in the RastPort's font.
 * @param rp The RastPort.
 * @param s The text.
 * @return The width in pixels.
 */
static WORD text_width(struct RastPort *rp, CONST_STRPTR s)
{
    return (WORD)TextLength(rp, (STRPTR)s, (UWORD)strlen((const char *)s));
}

/**
 * @brief Draw a raised (shine top left) or recessed frame.
 * @param c The card.
 * @param rp The RastPort.
 * @param x0 Left.
 * @param y0 Top.
 * @param x1 Right.
 * @param y1 Bottom.
 * @param raised Non-zero for a raised frame.
 */
static void frame(Card *c, struct RastPort *rp, WORD x0, WORD y0, WORD x1, WORD y1, int raised)
{
    SetAPen(rp, (UBYTE)(raised ? c->shine : c->shadow));
    Move(rp, x0, y1); Draw(rp, x0, y0); Draw(rp, x1, y0);
    SetAPen(rp, (UBYTE)(raised ? c->shadow : c->shine));
    Move(rp, x1, (WORD)(y0 + 1)); Draw(rp, x1, y1); Draw(rp, (WORD)(x0 + 1), y1);
}

/**
 * @brief Select the title font (or the screen font in bold).
 * @param c The card.
 * @param rp The RastPort.
 */
static void title_style(Card *c, struct RastPort *rp)
{
    if (c->big != NULL) SetFont(rp, c->big);
    else {
        SetFont(rp, c->font);
        SetSoftStyle(rp, FSF_BOLD, FSF_BOLD);
    }
}

/**
 * @brief Select the plain screen font.
 * @param c The card.
 * @param rp The RastPort.
 */
static void normal_style(Card *c, struct RastPort *rp)
{
    SetFont(rp, c->font);
    SetSoftStyle(rp, FS_NORMAL, FSF_BOLD | FSF_ITALIC | FSF_UNDERLINED);
}

/**
 * @brief Free the card's icon, font, pens and DrawInfo.
 * @param c The card.
 */
static void card_free(Card *c)
{
    struct ColorMap *cm = c->screen != NULL ? c->screen->ViewPort.ColorMap : NULL;
    if (c->icon != NULL) FreeDiskObject(c->icon);
    if (c->big != NULL) CloseFont(c->big);
    if (cm != NULL && c->pen_bg >= 0) ReleasePen(cm, (ULONG)c->pen_bg);
    if (cm != NULL && c->pen_title >= 0) ReleasePen(cm, (ULONG)c->pen_title);
    if (c->dri != NULL) FreeScreenDrawInfo(c->screen, c->dri);
    memset(c, 0, sizeof(*c));
    c->pen_bg = c->pen_title = -1;
}

/**
 * @brief Get colours, icon and fonts on a screen and measure the head.
 * @param c The card.
 * @param screen The screen.
 * @param icon_name The program icon (without .info), may be empty.
 * @return Non-zero on success.
 */
static int card_init(Card *c, struct Screen *screen, const char *icon_name)
{
    struct ColorMap *cm = screen->ViewPort.ColorMap;
    struct Rectangle rect;
    struct RastPort rp;

    memset(c, 0, sizeof(*c));
    c->pen_bg = c->pen_title = -1;
    c->screen = screen;
    if ((c->dri = GetScreenDrawInfo(screen)) == NULL) return 0;
    c->font = c->dri->dri_Font;

    /* colours: light background, navy title */
    c->text = c->dri->dri_Pens[TEXTPEN];
    c->shine = c->dri->dri_Pens[SHINEPEN];
    c->shadow = c->dri->dri_Pens[SHADOWPEN];
    c->fill = c->dri->dri_Pens[FILLPEN];
    c->bg = c->dri->dri_Pens[BACKGROUNDPEN];
    if ((c->pen_bg = obtain(cm, light_grey(c))) >= 0) c->bg = (UWORD)c->pen_bg;
    c->title = c->text;
    if ((c->pen_title = obtain(cm, 0x0a2a8a)) >= 0 && c->pen_title != c->bg)
        c->title = (UWORD)c->pen_title;

    /* the program icon, laid out for this screen (icon.library V44) */
    if (IconBase != NULL && icon_name != NULL && icon_name[0] != '\0' &&
        (c->icon = GetDiskObjectNew((STRPTR)icon_name)) != NULL) {
        LayoutIconA(c->icon, screen, NULL);
        if (GetIconRectangle(NULL, c->icon, NULL, &rect,
                             ICONDRAWA_Frameless, TRUE, ICONDRAWA_Borderless, TRUE, TAG_END)) {
            c->iconw = (WORD)(rect.MaxX - rect.MinX + 1);
            c->iconh = (WORD)(rect.MaxY - rect.MinY + 1);
        }
    }
    c->big = title_font(c);

    snprintf(c->version, sizeof(c->version), S(MSG_VERSION), APP_VERSION);
    InitRastPort(&rp);
    c->lineh = (WORD)(c->font->tf_YSize + 2);
    normal_style(c, &rp);
    c->textw = text_width(&rp, (CONST_STRPTR)SUBTITLE);
    if (text_width(&rp, (CONST_STRPTR)c->version) > c->textw)
        c->textw = text_width(&rp, (CONST_STRPTR)c->version);
    if (text_width(&rp, (CONST_STRPTR)COPYRIGHT) > c->textw)
        c->textw = text_width(&rp, (CONST_STRPTR)COPYRIGHT);
    title_style(c, &rp);
    if (text_width(&rp, (CONST_STRPTR)APP_NAME) > c->textw)
        c->textw = text_width(&rp, (CONST_STRPTR)APP_NAME);
    c->texth = (WORD)((c->big != NULL ? c->big->tf_YSize : c->font->tf_YSize) + 4 + 3 * c->lineh);
    return 1;
}

/** @brief Width of the head. @param c The card. @return The width. */
static WORD head_width(Card *c)
{
    return (WORD)((c->iconw != 0 ? c->iconw + GAP : 0) + c->textw);
}

/** @brief Height of the head. @param c The card. @return The height. */
static WORD head_height(Card *c)
{
    return c->texth > c->iconh ? c->texth : c->iconh;
}

/**
 * @brief Draw the head at (x, y): icon, title, subtitle, version, copyright.
 * @param c The card.
 * @param r The RastPort.
 * @param x Left edge.
 * @param y Top edge.
 */
static void draw_head(Card *c, struct RastPort *r, WORD x, WORD y)
{
    WORD tx = (WORD)(x + (c->iconw != 0 ? c->iconw + GAP : 0)), block = head_height(c);

    SetDrMd(r, JAM1);
    if (c->iconw != 0)
        DrawIconState(r, c->icon, NULL, x, (WORD)(y + (block - c->iconh) / 2), IDS_NORMAL,
                      ICONDRAWA_DrawInfo, (ULONG)c->dri, ICONDRAWA_Frameless, TRUE,
                      ICONDRAWA_Borderless, TRUE, ICONDRAWA_EraseBackground, FALSE, TAG_END);
    y = (WORD)(y + (block - c->texth) / 2);
    title_style(c, r);
    text_at(r, tx, y, c->title, (CONST_STRPTR)APP_NAME, strlen(APP_NAME));
    y = (WORD)(y + (c->big != NULL ? c->big->tf_YSize : c->font->tf_YSize) + 4);
    normal_style(c, r);
    text_at(r, tx, y, c->text, (CONST_STRPTR)SUBTITLE, strlen(SUBTITLE));
    y = (WORD)(y + c->lineh);
    text_at(r, tx, y, c->text, (CONST_STRPTR)c->version, strlen(c->version));
    y = (WORD)(y + c->lineh);
    text_at(r, tx, y, c->text, (CONST_STRPTR)COPYRIGHT, strlen(COPYRIGHT));
}

/**
 * @brief Background and double frame (raised outside, recessed inside).
 * @param c The card.
 * @param r The RastPort.
 * @param x0 Left.
 * @param y0 Top.
 * @param x1 Right.
 * @param y1 Bottom.
 */
static void draw_back(Card *c, struct RastPort *r, WORD x0, WORD y0, WORD x1, WORD y1)
{
    SetAPen(r, (UBYTE)c->bg);
    RectFill(r, x0, y0, x1, y1);
    frame(c, r, x0, y0, x1, y1, 1);
    frame(c, r, (WORD)(x0 + 2), (WORD)(y0 + 2), (WORD)(x1 - 2), (WORD)(y1 - 2), 0);
}

/* -- splash window -------------------------------------------------------- */

/** State of the splash window. */
static struct {
    Card card;
    struct Window *win;
    WORD barx, bary, barw;              /**< inside of the progress bar */
    WORD statusy;
    ULONG steps, done;
} sp;

/** @brief Draw the progress bar. */
static void draw_bar(void)
{
    struct RastPort *rp;
    WORD w;
    if (sp.win == NULL) return;
    rp = sp.win->RPort;
    w = sp.steps != 0
        ? (WORD)((ULONG)(UWORD)sp.barw * (sp.done < sp.steps ? sp.done : sp.steps) / sp.steps)
        : 0;
    if (w > 0) {
        SetAPen(rp, (UBYTE)sp.card.fill);
        RectFill(rp, sp.barx, sp.bary, (WORD)(sp.barx + w - 1), (WORD)(sp.bary + BAR_H - 1));
    }
    if (w < sp.barw) {
        SetAPen(rp, (UBYTE)sp.card.bg);
        RectFill(rp, (WORD)(sp.barx + w), sp.bary, (WORD)(sp.barx + sp.barw - 1),
                 (WORD)(sp.bary + BAR_H - 1));
    }
}

/**
 * @brief Open the splash window on the default public screen.
 * @param icon_name The program icon (without .info).
 * @param steps Number of steps of the progress bar.
 */
void splash_open(const char *icon_name, ULONG steps)
{
    Card *c = &sp.card;
    struct Screen *screen;
    WORD w, h;

    if (sp.win != NULL || (screen = LockPubScreen(NULL)) == NULL) return;
    sp.steps = steps;
    sp.done = 0;
    if (!card_init(c, screen, icon_name)) goto fail;

    w = (WORD)(MARGIN + (head_width(c) > 180 ? head_width(c) : 180) + MARGIN);
    h = (WORD)(MARGIN + head_height(c) + GAP + c->lineh + 4 + BAR_H + 2 + MARGIN);
    if ((sp.win = OpenWindowTags(NULL,
            WA_PubScreen, (ULONG)screen,
            WA_Left, (ULONG)((screen->Width - w) / 2),
            WA_Top, (ULONG)((screen->Height - h) / 2),
            WA_Width, (ULONG)w,
            WA_Height, (ULONG)h,
            WA_Borderless, TRUE,
            WA_SmartRefresh, TRUE,
            WA_RMBTrap, TRUE,
            WA_Activate, FALSE,
            TAG_END)) == NULL)
        goto fail;

    draw_back(c, sp.win->RPort, 0, 0, (WORD)(w - 1), (WORD)(h - 1));
    draw_head(c, sp.win->RPort, MARGIN, MARGIN);
    /* status line and progress bar at the bottom */
    sp.statusy = (WORD)(MARGIN + head_height(c) + GAP);
    sp.barx = MARGIN + 1;
    sp.bary = (WORD)(sp.statusy + c->lineh + 4 + 1);
    sp.barw = (WORD)(w - 2 * MARGIN - 2);
    frame(c, sp.win->RPort, MARGIN, (WORD)(sp.bary - 1), (WORD)(MARGIN + sp.barw + 1),
          (WORD)(sp.bary + BAR_H), 0);
    draw_bar();
    return;

fail:
    card_free(c);
    UnlockPubScreen(NULL, screen);
    memset(&sp, 0, sizeof(sp));
}

/**
 * @brief Show the current step below the texts.
 * @param text The step.
 */
void splash_status(const char *text)
{
    struct RastPort *r;
    if (sp.win == NULL) return;
    r = sp.win->RPort;
    SetAPen(r, (UBYTE)sp.card.bg);
    RectFill(r, MARGIN, sp.statusy, (WORD)(sp.win->Width - MARGIN - 1),
             (WORD)(sp.statusy + r->TxHeight + 1));
    text_at(r, MARGIN, sp.statusy, sp.card.text, (CONST_STRPTR)text, strlen(text));
}

/** @brief Advance the progress bar by one step. */
void splash_step(void)
{
    if (sp.win == NULL) return;
    ++sp.done;
    draw_bar();
}

/** @brief Close the splash window (if it is open). */
void splash_close(void)
{
    struct Screen *screen = sp.card.screen;
    if (sp.win != NULL) CloseWindow(sp.win);
    card_free(&sp.card);
    if (screen != NULL) UnlockPubScreen(NULL, screen);
    memset(&sp, 0, sizeof(sp));
}

/* -- About window --------------------------------------------------------- */

/**
 * @brief Draw the About window's contents into its inner area.
 * @param c The card.
 * @param win The window.
 * @param details The details, lines separated by '\n'.
 * @param sepy Position of the groove below the head.
 */
static void about_draw(Card *c, struct Window *win, const char *details, WORD sepy)
{
    struct RastPort *r = win->RPort;
    WORD x0 = win->BorderLeft, y0 = win->BorderTop;
    WORD x1 = (WORD)(win->Width - win->BorderRight - 1);
    WORD y1 = (WORD)(win->Height - win->BorderBottom - 1);
    WORD y;
    const char *p = details;

    draw_back(c, r, x0, y0, x1, y1);
    draw_head(c, r, (WORD)(x0 + MARGIN), (WORD)(y0 + MARGIN));
    /* a groove between head and details */
    SetAPen(r, (UBYTE)c->shadow);
    Move(r, (WORD)(x0 + MARGIN), (WORD)(y0 + sepy));
    Draw(r, (WORD)(x1 - MARGIN), (WORD)(y0 + sepy));
    SetAPen(r, (UBYTE)c->shine);
    Move(r, (WORD)(x0 + MARGIN), (WORD)(y0 + sepy + 1));
    Draw(r, (WORD)(x1 - MARGIN), (WORD)(y0 + sepy + 1));
    normal_style(c, r);
    for (y = (WORD)(y0 + sepy + 2 + GAP); *p != '\0'; y = (WORD)(y + c->lineh)) {
        const char *e = strchr(p, '\n');
        ULONG len = e != NULL ? (ULONG)(e - p) : strlen(p);
        text_at(r, (WORD)(x0 + MARGIN), y, c->text, (CONST_STRPTR)p, len);
        p += len;
        if (*p != '\0') ++p;
    }
}

/**
 * @brief Show the modal About window centred over the main window.
 *
 * The main window shows the busy pointer and its input is dropped while the
 * About window is open. Return, Esc, the close gadget and OK close it.
 *
 * @param app The application state (main window and screen).
 * @param details The details, lines separated by '\n'.
 * @return Non-zero if the window was shown, zero if it could not be opened
 *         (the caller shows a requester instead).
 */
int about_window(EditorApp *app, const char *details)
{
    Card c;
    struct Window *win;
    struct Gadget *ok;
    struct RastPort rp;
    struct IntuiMessage *msg;
    const char *p;
    WORD w, h, dw = 0, dlines = 0, bw, bh, sepy;
    ULONG mainsig = 0;
    int done = 0;
    WORD code;

    if (app->window == NULL || !card_init(&c, app->screen, app->settings.icon_name)) return 0;

    /* measure the details and the button */
    InitRastPort(&rp);
    normal_style(&c, &rp);
    for (p = details; *p != '\0'; ++dlines) {
        const char *e = strchr(p, '\n');
        ULONG len = e != NULL ? (ULONG)(e - p) : strlen(p);
        WORD tw = (WORD)TextLength(&rp, (STRPTR)p, (UWORD)len);
        if (tw > dw) dw = tw;
        p += len;
        if (*p != '\0') ++p;
    }
    bw = (WORD)(text_width(&rp, (CONST_STRPTR)S(MSG_OK)) + 32);
    bh = (WORD)(c.font->tf_YSize + 6);
    w = head_width(&c) > dw ? head_width(&c) : dw;
    w = (WORD)(w + 2 * MARGIN);
    sepy = (WORD)(MARGIN + head_height(&c) + GAP);
    h = (WORD)(sepy + 2 + GAP + dlines * c.lineh + GAP + bh + MARGIN);

    ok = (struct Gadget *)NewObject(BUTTON_GetClass(), NULL,
        GA_ID, 1,
        GA_RelVerify, TRUE,
        GA_Text, (ULONG)S(MSG_OK),
        GA_Width, (ULONG)bw,
        GA_Height, (ULONG)bh,
        TAG_END);
    win = ok != NULL ? OpenWindowTags(NULL,
        WA_PubScreen, (ULONG)app->screen,
        WA_Title, (ULONG)S(MSG_ABOUT_TITLE),
        WA_InnerWidth, (ULONG)w,
        WA_InnerHeight, (ULONG)h,
        WA_Left, (ULONG)(app->window->LeftEdge + (app->window->Width - w) / 2),
        WA_Top, (ULONG)(app->window->TopEdge + (app->window->Height - h) / 2),
        WA_DragBar, TRUE,
        WA_DepthGadget, TRUE,
        WA_CloseGadget, TRUE,
        WA_Activate, TRUE,
        WA_SmartRefresh, TRUE,
        WA_RMBTrap, TRUE,
        WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_GADGETUP | IDCMP_VANILLAKEY | IDCMP_REFRESHWINDOW,
        TAG_END) : NULL;
    if (win == NULL) {
        if (ok != NULL) DisposeObject((Object *)ok);
        card_free(&c);
        return 0;
    }
    about_draw(&c, win, details, sepy);
    SetAttrs((Object *)ok, GA_Left, (ULONG)(win->BorderLeft + (w - bw) / 2),
             GA_Top, (ULONG)(win->BorderTop + h - MARGIN - bh), TAG_END);
    AddGList(win, ok, (UWORD)~0, 1, NULL);
    RefreshGList(ok, win, NULL, 1);

    SetAttrs(app->window_object, WA_BusyPointer, TRUE, TAG_END);
    GetAttr(WINDOW_SigMask, app->window_object, &mainsig);
    while (!done) {
        ULONG got = Wait(1UL << win->UserPort->mp_SigBit | mainsig);
        /* the main window waits: its input is dropped */
        if ((got & mainsig) != 0)
            while (DoMethod(app->window_object, WM_HANDLEINPUT, &code) != WMHI_LASTMSG) ;
        while ((msg = (struct IntuiMessage *)GetMsg(win->UserPort)) != NULL) {
            ULONG msg_class = msg->Class;
            UWORD key = msg->Code;
            ReplyMsg((struct Message *)msg);
            switch (msg_class) {
                case IDCMP_CLOSEWINDOW:
                case IDCMP_GADGETUP:
                    done = 1;
                    break;
                case IDCMP_VANILLAKEY:
                    if (key == 13 || key == 27) done = 1;      /* Return, Esc */
                    break;
                case IDCMP_REFRESHWINDOW:
                    BeginRefresh(win);
                    EndRefresh(win, TRUE);
                    break;
                default:
                    break;
            }
        }
    }
    SetAttrs(app->window_object, WA_BusyPointer, FALSE, TAG_END);
    RemoveGList(win, ok, 1);
    CloseWindow(win);
    DisposeObject((Object *)ok);
    card_free(&c);
    return 1;
}
