/**
 * @file printwin.c
 * @brief HelpViewer: printing the page shown (window "Print"), also as
 *        PostScript or PDF; taken over from Kickdown's print window.
 *
 * Three kinds of output, all through the page's gadget (html.gadget or
 * htmlttf.gadget, V1.2):
 *  - printer: HTMLM_PrintBegin/Render, the pages as pixels through
 *    printer.device PRD_DUMPRPORTTAGS with DRPA_SourceHook (the driver
 *    asks for strips on its own task). Each page is sent with SendIO()
 *    while a modal progress window shows the pages and can stop the
 *    print; the main window waits.
 *    The whole sheet is sent in exactly the driver's dots, the driver
 *    puts it at the start of its printable area (its hardware margins
 *    add to ours, so the margins can be set).
 *  - PostScript: HTMLM_Export to a file, PRT:, PS: (the PostScript
 *    handler of TurboPrint) or any device.
 *  - PDF: HTMLM_Export to a file.
 * The options are tool types of the program icon (the names of
 * Kickdown's), written back when the print window changed them; page
 * range and copies are not kept.
 *
 * Only the default printer (unit 0) is used, at the density of its
 * settings (Prefs/PrinterGfx); the window shows its driver from
 * ENV:Sys/Printer.prefs and does not open printer.device. htmlttf.gadget
 * renders the page at the printer's resolution up to MAXRENDERDPI,
 * html.gadget uses its bitmap fonts.
 *
 * TurboPrint replaces printer.device. It does not know PRD_DUMPRPORTTAGS,
 * and its own 24 bit dump, queries with SPECIAL_NOPRINT and even opening
 * and closing its printer.device damaged memory (TurboPrint 7, OS 3.2;
 * found with Kickdown). With TurboPrint printer.device is therefore never
 * opened (turboprint_running() reads the device list) and only PostScript
 * is offered, by default to PS:, TurboPrint's PostScript handler.
 *
 * No stdio: texts with numbers are made with RawDoFmt() (fmt()).
 */
#include <exec/types.h>
#include <exec/memory.h>
#include <exec/execbase.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <devices/printer.h>
#include <devices/prtbase.h>
#include <graphics/rastport.h>
#include <intuition/intuition.h>
#include <intuition/gadgetclass.h>
#include <utility/hooks.h>
#include <classes/window.h>
#include <gadgets/layout.h>
#include <gadgets/button.h>
#include <gadgets/chooser.h>
#include <gadgets/checkbox.h>
#include <gadgets/integer.h>
#include <gadgets/string.h>
#include <gadgets/getfile.h>
#include <gadgets/fuelgauge.h>
#include <gadgets/html.h>
#include <images/label.h>
#include <images/bevel.h>

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/utility.h>
#include <proto/window.h>
#include <proto/layout.h>
#include <proto/button.h>
#include <proto/chooser.h>
#include <proto/checkbox.h>
#include <proto/integer.h>
#include <proto/string.h>
#include <proto/getfile.h>
#include <proto/fuelgauge.h>
#include <proto/label.h>
#include <proto/icon.h>
#include <clib/alib_protos.h>

#include <stdarg.h>
#include <string.h>

#include "helpviewer.h"
#include "helpviewer_strings.h"

extern struct Library *HTMLBase;        /* helpviewer.c */

/** @name Classes opened for the print window (closed by print_cleanup()) */
/** @{ */
struct Library *ChooserBase = NULL;     /**< chooser.gadget */
struct Library *CheckBoxBase = NULL;    /**< checkbox.gadget */
struct Library *IntegerBase = NULL;     /**< integer.gadget */
struct Library *StringBase = NULL;      /**< string.gadget */
struct Library *GetFileBase = NULL;     /**< getfile.gadget */
struct Library *FuelGaugeBase = NULL;   /**< fuelgauge.gadget, only while printing */
/** @} */

/** Gadget IDs of the print and the progress window. */
enum {
    PW_MODE = 400, PW_DEST, PW_FILE, PW_DEVICE, PW_IMAGEDPI, PW_FROM, PW_TO, PW_COPIES,
    PW_PAPER, PW_ML, PW_MT, PW_MR, PW_MB, PW_SERIF, PW_SIZE, PW_PAGENUMBERS, PW_BACKGROUNDS,
    PW_PRINT, PW_CANCEL, PW_STOP
};

/** Kinds of output. */
enum { PRMODE_PRINTER, PRMODE_PS, PRMODE_PDF, NUMPRMODES };
/** Destinations of PostScript. */
enum { PRDEST_FILE, PRDEST_PRT, PRDEST_PS, PRDEST_DEVICE, NUMPRDESTS };
/** Paper sizes. */
enum { PAPER_A4, PAPER_A5, PAPER_LETTER, PAPER_LEGAL, NUMPAPERS };

/** The gadget renders the pages not finer than this; above, the source
 *  hook enlarges them to the printer's dots. */
#define MAXRENDERDPI 600

/** TurboPrint's mark in its printer.device (tp_devel.lha of IrseeSoft). */
#define TPMATCHWORD 0xf10a57efUL

/** Bytes of the bitmap plane of printer_dpi() (32 would do). */
#define QUERYPLANE 4096

/** The print options (tool types of the program icon). */
typedef struct PrintSettings {
    LONG prmode;            /**< PRMODE_, -1 = automatic: with TurboPrint PostScript level 2 to PS:, else the printer */
    LONG pslevel;           /**< PostScript level 1 or 2 */
    LONG primagedpi;        /**< PS/PDF: pictures at most this many dpi, 0 = as they are */
    LONG prdest;            /**< PostScript to: PRDEST_ */
    char prdevice[40];      /**< PRDEST_DEVICE: "PAR:" etc. */
    LONG paper;             /**< PAPER_ */
    LONG margins[4];        /**< left, top, right, bottom in mm */
    BOOL prserif;           /**< PostScript/PDF text in Times */
    LONG prsize;            /**< normal text in points (PostScript/PDF) */
    BOOL prpagenumbers;     /**< page numbers in the footer */
    BOOL prbackgrounds;     /**< background colours */
} PrintSettings;

/** Page range and copies of a print (not kept in the icon). */
typedef struct PrintJob {
    LONG first;             /**< first page, from 1 */
    LONG last;              /**< last page, 0 = to the end */
    LONG copies;            /**< copies (printer only) */
} PrintJob;

/** The options of the session; the defaults if the icon has none. */
static PrintSettings set = {
    -1, 2, 0, PRDEST_FILE, "", PAPER_A4, { 20, 20, 20, 20 }, FALSE, 10, TRUE, TRUE
};

/** Tool type values of PrintSettings.prmode. */
static const char *const prmode_names[NUMPRMODES] = { "PRINTER", "PS", "PDF" };
/** Tool type values of PrintSettings.prdest. */
static const char *const prdest_names[NUMPRDESTS] = { "FILE", "PRT", "PS", "DEVICE" };
/** Tool type values of PrintSettings.paper. */
static const char *const paper_names[NUMPAPERS] = { "A4", "A5", "LETTER", "LEGAL" };
/** Paper width and height in points. */
static const short paper_sizes[NUMPAPERS][2] = { { 595, 842 }, { 420, 595 }, { 612, 792 }, { 612, 1008 } };

/** PostScript/PDF: the most dpi of a picture, 0 = as it is. */
static const LONG imagedpis[] = { 0, 600, 300, 150, 75 };
#define NUMIMAGEDPIS 5          /**< entries of imagedpis[] */
/** Labels of the picture resolution chooser (the first from the catalog). */
static STRPTR imagedpi_labels[NUMIMAGEDPIS + 1] = {
    NULL, (STRPTR)"600 dpi", (STRPTR)"300 dpi", (STRPTR)"150 dpi", (STRPTR)"75 dpi", NULL
};
/** Labels of the paper chooser. */
static STRPTR paper_labels[] = { (STRPTR)"A4", (STRPTR)"A5", (STRPTR)"Letter", (STRPTR)"Legal", NULL };
/** The entries of the kind chooser: PostScript in two levels. */
enum { MI_PRINTER, MI_PS2, MI_PS1, MI_PDF, NUMMI };
/** Labels of the kind chooser. */
static STRPTR mode_labels[NUMMI + 1];
/** Labels of the font chooser. */
static STRPTR font_labels[3];

/** The main window and its page while printing. */
static struct {
    struct Screen *screen;      /**< its screen */
    Object *winobj;             /**< the main window (window.class) */
    struct Window *win;         /**< its Intuition window */
    Object *html;               /**< the page's gadget */
} mw;

/** The print window. */
static struct {
    Object *winobj;             /**< window.class object */
    Object *root;               /**< its layout */
    Object *mode;               /**< kind of output */
    Object *dest;               /**< destination of PostScript */
    Object *file;               /**< output file */
    Object *device;             /**< output device */
    Object *imagedpi;           /**< picture resolution */
    Object *printer;            /**< driver of the default printer (shown only) */
    Object *from;               /**< first page */
    Object *to;                 /**< last page */
    Object *copies;             /**< copies */
    Object *paper;              /**< paper size */
    Object *margin[4];          /**< margins left, top, right, bottom */
    Object *serif;              /**< font */
    Object *size;               /**< font size */
    Object *pagenumbers;        /**< page numbers */
    Object *backgrounds;        /**< background colours */
    Object *print;              /**< the Print button */
    struct Window *win;         /**< the open window */
    struct List destlist;       /**< chooser nodes of the destinations */
    struct List modelist;       /**< chooser nodes of the kinds */
    char printername[32];       /**< driver of the default printer */
    BOOL tp;                    /**< printer.device is TurboPrint's */
} pw;

/*****************************************************************************/
/* helpers                                                                   */

/** Output of fmt(): where the next character goes, and the last byte. */
typedef struct FmtOut {
    char *p;                    /**< next character */
    char *end;                  /**< the last byte (for the terminator) */
} FmtOut;

/**
 * @brief RawDoFmt() put-character function: store a character if there
 *        is room.
 * @param c The character.
 * @param o The output.
 */
static void fmt_put(UBYTE c __asm("d0"), FmtOut *o __asm("a3"))
{
    if (o->p < o->end) *o->p++ = (char)c;
}

/**
 * @brief Format a text like sprintf() with RawDoFmt(), cut to the buffer.
 *
 * Numbers must be passed as long and formatted with %ld.
 *
 * @param[out] buf The buffer.
 * @param size Its size.
 * @param f The format.
 * @param ... Its arguments.
 */
static void fmt(char *buf, LONG size, const char *f, ...)
{
    FmtOut o;
    va_list ap;
    o.p = buf;
    o.end = buf + size - 1;
    va_start(ap, f);
    RawDoFmt((STRPTR)f, (APTR)ap, (void (*)())fmt_put, &o);
    va_end(ap);
    *o.p = '\0';
}

/**
 * @brief Ask a question with two answers.
 * @param text The question.
 * @param buttons The answers ("Replace|Cancel").
 * @return Non-zero for the first answer.
 */
static int ask(const char *text, const char *buttons)
{
    struct EasyStruct es;
    es.es_StructSize = sizeof(es);
    es.es_Flags = 0;
    es.es_Title = (STRPTR)"HelpViewer";
    es.es_TextFormat = (STRPTR)"%s";
    es.es_GadgetFormat = (STRPTR)buttons;
    return EasyRequest(mw.win, &es, NULL, (ULONG)text) == 1;
}

/**
 * @brief Report a DOS error with the text of IoErr().
 * @param what What failed.
 * @param name The file or device.
 */
static void dos_error(const char *what, const char *name)
{
    char err[100], text[PATHMAX + 200];
    Fault(IoErr(), NULL, (STRPTR)err, sizeof(err));
    fmt(text, sizeof(text), "%s\n%s\n%s", what, name, err);
    message(text);
}

/**
 * @brief Ask before a file is replaced.
 * @param name The file.
 * @return Non-zero if it does not exist or may be replaced.
 */
static int confirm_overwrite(const char *name)
{
    char text[PATHMAX + 100];
    BPTR lock = Lock((STRPTR)name, ACCESS_READ);
    if (lock == 0) return 1;
    UnLock(lock);
    fmt(text, sizeof(text), S(MSG_FILE_EXISTS), name);
    return ask(text, S(MSG_REPLACE_CANCEL));
}

/**
 * @brief Show or hide the busy pointer of the main window.
 * @param on Non-zero to show it.
 */
static void busy(BOOL on)
{
    SetAttrs(mw.winobj, WA_BusyPointer, (ULONG)on, TAG_DONE);
}

/** @brief Drop the input of the main window, which waits. */
static void drop_main_input(void)
{
    UWORD code;
    while (DoMethod(mw.winobj, WM_HANDLEINPUT, &code) != WMHI_LASTMSG) ;
}

/**
 * @brief Create a label.
 * @param text Its text.
 * @return The label.image, or NULL.
 */
static Object *label(const char *text)
{
    return NewObject(LABEL_GetClass(), NULL, LABEL_Text, (ULONG)text, TAG_DONE);
}

/**
 * @brief Create a checkbox.
 * @param id Its ID.
 * @param text Its text.
 * @param on Its state.
 * @return The checkbox.gadget, or NULL.
 */
static Object *checkbox(ULONG id, const char *text, BOOL on)
{
    return NewObject(CHECKBOX_GetClass(), NULL,
        GA_ID, id, GA_RelVerify, TRUE, GA_Text, (ULONG)text, CHECKBOX_Checked, (ULONG)on, TAG_DONE);
}

/**
 * @brief Create a pop-up chooser from a label array.
 * @param id Its ID.
 * @param labels The labels.
 * @param selected The entry selected.
 * @return The chooser.gadget, or NULL.
 */
static Object *chooser(ULONG id, STRPTR *labels, ULONG selected)
{
    return NewObject(CHOOSER_GetClass(), NULL,
        GA_ID, id, GA_RelVerify, TRUE, CHOOSER_PopUp, TRUE,
        CHOOSER_LabelArray, (ULONG)labels, CHOOSER_Selected, selected, TAG_DONE);
}

/**
 * @brief Create a number field.
 * @param id Its ID.
 * @param value Its number.
 * @param min The smallest number.
 * @param max The largest number.
 * @param chars The most digits.
 * @return The integer.gadget, or NULL.
 */
static Object *integer(ULONG id, LONG value, LONG min, LONG max, LONG chars)
{
    return NewObject(INTEGER_GetClass(), NULL,
        GA_ID, id, GA_RelVerify, TRUE, GA_TabCycle, TRUE,
        INTEGER_Number, (ULONG)value, INTEGER_Minimum, (ULONG)min, INTEGER_Maximum, (ULONG)max,
        INTEGER_MaxChars, (ULONG)chars, TAG_DONE);
}

/**
 * @brief Create a button as wide as its text.
 * @param id Its ID.
 * @param text Its text.
 * @return The button.gadget, or NULL.
 */
static Object *button(ULONG id, const char *text)
{
    return NewObject(BUTTON_GetClass(), NULL,
        GA_ID, id, GA_RelVerify, TRUE, GA_Text, (ULONG)text,
        BUTTON_TextPadding, TRUE, TAG_DONE);
}

/**
 * @brief Create an empty group that takes the space left.
 * @return The layout, or NULL.
 */
static Object *filler(void)
{
    return NewObject(LAYOUT_GetClass(), NULL, TAG_DONE);
}

/**
 * @brief Read an attribute.
 * @param o The object.
 * @param attr The attribute.
 * @return Its value (numbers are signed), 0 if it has none.
 */
static LONG get(Object *o, ULONG attr)
{
    ULONG v = 0;
    GetAttr(attr, o, &v);
    return (LONG)v;
}

/**
 * @brief Tell whether PS:, the PostScript handler of TurboPrint, is there.
 * @return TRUE if it is mounted.
 */
static BOOL have_ps(void)
{
    struct DosList *dl = LockDosList(LDF_DEVICES | LDF_READ);
    BOOL found = FindDosEntry(dl, (STRPTR)"PS", LDF_DEVICES) != NULL;
    UnLockDosList(LDF_DEVICES | LDF_READ);
    return found;
}

/**
 * @brief Give a file name the extension of the output (.ps or .pdf).
 * @param[in,out] file The name.
 * @param size Its buffer's size.
 * @param mode The kind of output (PRMODE_).
 */
static void set_extension(char *file, ULONG size, LONG mode)
{
    char *dot = strrchr((char *)FilePart((STRPTR)file), '.');
    if (dot) *dot = 0;
    strncat(file, mode == PRMODE_PDF ? ".pdf" : ".ps", size - strlen(file) - 1);
}

/**
 * @brief Free the nodes of a chooser list.
 * @param l The list (may be uninitialised: all zero).
 */
static void free_nodes(struct List *l)
{
    struct Node *n, *next;
    if (!l->lh_Head) return;
    for (n = l->lh_Head; (next = n->ln_Succ); n = next) FreeChooserNode(n);
    NewList(l);
}

/**
 * @brief Read the driver of the default printer from
 *        ENV:Sys/Printer.prefs (chunk PTXT), so that printer.device needs
 *        not be opened for its name.
 * @param[out] driver Its name (31 bytes), "" if unknown.
 */
static void prefs_driver(char *driver)
{
    UBYTE buf[512];
    LONG len, i;
    BPTR fh;

    driver[0] = 0;
    if (!(fh = Open((STRPTR)"ENV:Sys/Printer.prefs", MODE_OLDFILE))) return;
    len = Read(fh, buf, sizeof(buf));
    Close(fh);
    if (len < 12 || memcmp(buf, "FORM", 4) || memcmp(buf + 8, "PREF", 4)) return;
    for (i = 12; i + 8 <= len; ) {
        LONG size = (LONG)buf[i + 4] << 24 | (LONG)buf[i + 5] << 16 | (LONG)buf[i + 6] << 8 | buf[i + 7];
        UBYTE *d = buf + i + 8;
        if (size < 0 || i + 8 + size > len) break;
        if (!memcmp(buf + i, "PTXT", 4) && size >= 16 + 30) {
            memcpy(driver, d + 16, 30);
            driver[30] = 0;
        }
        i += 8 + size + (size & 1);
    }
}

/**
 * @brief Recognise TurboPrint's printer.device without opening it.
 *
 * Opening and closing it damaged memory (units above 0, which it does not
 * know, were opened then). printer.device is device and unit at once
 * (struct PrinterData), so the match word can be read in the device list.
 *
 * @param[out] driver The name of its driver if it is loaded (31 bytes),
 *             or NULL.
 * @return TRUE if printer.device is TurboPrint's.
 */
static BOOL turboprint_running(char *driver)
{
    struct PrinterData *pd;
    BOOL tp = FALSE;

    Forbid();
    if ((pd = (struct PrinterData *)FindName(&SysBase->DeviceList, (STRPTR)"printer.device"))) {
        tp = ((struct Library *)pd)->lib_Version >= 39 && ((ULONG *)pd->pd_OldStk)[2] == TPMATCHWORD;
        if (tp && driver && pd->pd_SegmentData) {
            STRPTR name = pd->pd_SegmentData->ps_PED.ped_PrinterName;
            if (name && name[0]) {
                strncpy(driver, (const char *)name, 30);
                driver[30] = 0;
            }
        }
    }
    Permit();
    return tp;
}

/**
 * @brief Return the kind of output of an entry of the kind chooser.
 * @param item The entry (MI_).
 * @return The kind (PRMODE_).
 */
static LONG mode_of(LONG item)
{
    return item == MI_PDF ? PRMODE_PDF : item == MI_PRINTER ? PRMODE_PRINTER : PRMODE_PS;
}

/**
 * @brief Return the entry of the kind chooser for a kind of output.
 * @param mode The kind (PRMODE_).
 * @param level The PostScript level.
 * @return The entry (MI_).
 */
static LONG item_of(LONG mode, LONG level)
{
    return mode == PRMODE_PDF ? MI_PDF : mode == PRMODE_PS ? (level == 1 ? MI_PS1 : MI_PS2) : MI_PRINTER;
}

/**
 * @brief Convert millimetres to points.
 * @param mm Millimetres.
 * @return Points, rounded.
 */
static LONG mm_pt(LONG mm)
{
    return (mm * 720 + 127) / 254;
}

/** Tags of a group of the print window: frame, title, spacing. */
#define PAGE_GROUP(title) \
    LAYOUT_Orientation, LAYOUT_ORIENT_VERT, LAYOUT_BevelStyle, BVS_GROUP, \
    LAYOUT_Label, (ULONG)(title), LAYOUT_SpaceOuter, TRUE, \
    LAYOUT_LeftSpacing, 8, LAYOUT_RightSpacing, 8, \
    LAYOUT_TopSpacing, 6, LAYOUT_BottomSpacing, 6, LAYOUT_InnerSpacing, 4

/** A row that keeps its height. */
#define FIXED CHILD_WeightedHeight, 0

/*****************************************************************************/
/* options                                                                   */

void print_options(CONST_STRPTR *tt)
{
    STRPTR v;
    LONG n;
    int i;

    if (!tt) return;
    if ((v = FindToolType(tt, (STRPTR)"PRINT_MODE")))
        for (i = 0; i < NUMPRMODES; i++) if (!Stricmp(v, (STRPTR)prmode_names[i])) set.prmode = i;
    if ((v = FindToolType(tt, (STRPTR)"PRINT_IMAGEDPI")) && StrToLong(v, &n) > 0 && n >= 0 && n <= 2400)
        set.primagedpi = n;
    if ((v = FindToolType(tt, (STRPTR)"PS_LEVEL")) && StrToLong(v, &n) > 0 && (n == 1 || n == 2))
        set.pslevel = n;
    if ((v = FindToolType(tt, (STRPTR)"PRINT_TO")))
        for (i = 0; i < NUMPRDESTS; i++) if (!Stricmp(v, (STRPTR)prdest_names[i])) set.prdest = i;
    if ((v = FindToolType(tt, (STRPTR)"PRINT_DEVICE"))) {
        strncpy(set.prdevice, (const char *)v, sizeof(set.prdevice) - 1);
        set.prdevice[sizeof(set.prdevice) - 1] = 0;
    }
    if ((v = FindToolType(tt, (STRPTR)"PAPER")))
        for (i = 0; i < NUMPAPERS; i++) if (!Stricmp(v, (STRPTR)paper_names[i])) set.paper = i;
    if ((v = FindToolType(tt, (STRPTR)"MARGINS"))) {
        /* "left,top,right,bottom" in mm; one value for all */
        LONG m[4], k = 0, len;
        while (k < 4 && (len = StrToLong(v, &n)) > 0 && n >= 0 && n <= 100) {
            m[k++] = n;
            v += len;
            if (*v != ',') break;
            v++;
        }
        if (k == 1) m[1] = m[2] = m[3] = m[0];
        if (k == 1 || k == 4) for (k = 0; k < 4; k++) set.margins[k] = m[k];
    }
    if (FindToolType(tt, (STRPTR)"PRINT_SERIF")) set.prserif = TRUE;
    if ((v = FindToolType(tt, (STRPTR)"PRINT_SIZE")) && StrToLong(v, &n) > 0 && n >= 4 && n <= 36)
        set.prsize = n;
    if (FindToolType(tt, (STRPTR)"NOPAGENUMBERS")) set.prpagenumbers = FALSE;
    if (FindToolType(tt, (STRPTR)"NOPRINTBACKGROUNDS")) set.prbackgrounds = FALSE;
}

/**
 * @brief Tell whether two sets of options are the same.
 * @param a The one.
 * @param b The other.
 * @return Non-zero if they are.
 */
static int same_options(const PrintSettings *a, const PrintSettings *b)
{
    int i;
    for (i = 0; i < 4; i++) if (a->margins[i] != b->margins[i]) return 0;
    return a->prmode == b->prmode && a->pslevel == b->pslevel && a->primagedpi == b->primagedpi &&
           a->prdest == b->prdest && !strcmp(a->prdevice, b->prdevice) && a->paper == b->paper &&
           !a->prserif == !b->prserif && a->prsize == b->prsize &&
           !a->prpagenumbers == !b->prpagenumbers && !a->prbackgrounds == !b->prbackgrounds;
}

/**
 * @brief Fill a tool type entry.
 * @param[out] e The entry.
 * @param key Its name.
 * @param active FALSE for the default value.
 * @param value Its value, NULL for a switch.
 */
static void entry(ToolTypeEntry *e, const char *key, BOOL active, const char *value)
{
    e->key = key;
    e->active = active;
    if (value) fmt(e->text, sizeof(e->text), "%s=%s", key, value);
    else fmt(e->text, sizeof(e->text), "%s", key);
}

/** @brief Write the print options into the program icon. */
static void save_options(void)
{
    ToolTypeEntry e[11];
    char num[12];
    int k = 0;

    entry(&e[k++], "PRINT_MODE", set.prmode >= 0,
          prmode_names[set.prmode >= 0 && set.prmode < NUMPRMODES ? set.prmode : 0]);
    entry(&e[k++], "PS_LEVEL", set.pslevel != 2, set.pslevel == 1 ? "1" : "2");
    fmt(num, sizeof(num), "%ld", (long)set.primagedpi);
    entry(&e[k++], "PRINT_IMAGEDPI", set.primagedpi != 0, num);
    entry(&e[k++], "PRINT_TO", set.prdest != PRDEST_FILE,
          prdest_names[set.prdest >= 0 && set.prdest < NUMPRDESTS ? set.prdest : 0]);
    entry(&e[k++], "PRINT_DEVICE", set.prdevice[0] != 0, set.prdevice);
    entry(&e[k++], "PAPER", set.paper != PAPER_A4,
          paper_names[set.paper >= 0 && set.paper < NUMPAPERS ? set.paper : 0]);
    entry(&e[k], "MARGINS", set.margins[0] != 20 || set.margins[1] != 20 || set.margins[2] != 20 ||
          set.margins[3] != 20, NULL);
    fmt(e[k].text, sizeof(e[k].text), "MARGINS=%ld,%ld,%ld,%ld", (long)set.margins[0],
        (long)set.margins[1], (long)set.margins[2], (long)set.margins[3]);
    k++;
    entry(&e[k++], "PRINT_SERIF", set.prserif, NULL);
    fmt(num, sizeof(num), "%ld", (long)set.prsize);
    entry(&e[k++], "PRINT_SIZE", set.prsize != 10, num);
    entry(&e[k++], "NOPAGENUMBERS", !set.prpagenumbers, NULL);
    entry(&e[k++], "NOPRINTBACKGROUNDS", !set.prbackgrounds, NULL);
    write_tooltypes(e, k);
}

/*****************************************************************************/
/* the window                                                                */

/**
 * @brief Open the classes of the print window.
 * @return TRUE if all are there.
 */
static BOOL open_classes(void)
{
    static const struct { struct Library **base; const char *name; } cl[] = {
        { &ChooserBase,     "gadgets/chooser.gadget" },
        { &CheckBoxBase,    "gadgets/checkbox.gadget" },
        { &IntegerBase,     "gadgets/integer.gadget" },
        { &StringBase,      "gadgets/string.gadget" },
        { &GetFileBase,     "gadgets/getfile.gadget" },
    };
    ULONG i;
    char text[100];

    for (i = 0; i < sizeof(cl) / sizeof(cl[0]); i++)
        if (!*cl[i].base && !(*cl[i].base = OpenLibrary((STRPTR)cl[i].name, 44))) {
            fmt(text, sizeof(text), S(MSG_ERR_CLASS), cl[i].name);
            message(text);
            return FALSE;
        }
    return TRUE;
}

void print_cleanup(void)
{
    struct Library **b[] = { &ChooserBase, &CheckBoxBase, &IntegerBase, &StringBase, &GetFileBase };
    ULONG i;
    for (i = 0; i < sizeof(b) / sizeof(b[0]); i++)
        if (*b[i]) {
            CloseLibrary(*b[i]);
            *b[i] = NULL;
        }
}

/** @brief Make only the gadgets usable that matter for the kind of output. */
static void update_gadgets(void)
{
    LONG mode = mode_of(get(pw.mode, CHOOSER_Selected)), dest = get(pw.dest, CHOOSER_Selected);
    BOOL ps = mode == PRMODE_PS, printer = mode == PRMODE_PRINTER;
    struct Window *w = pw.win;

    SetGadgetAttrs((struct Gadget *)pw.dest, w, NULL, GA_Disabled, (ULONG)!ps, TAG_DONE);
    SetGadgetAttrs((struct Gadget *)pw.file, w, NULL, GA_Disabled,
                   (ULONG)!(mode == PRMODE_PDF || (ps && dest == PRDEST_FILE)), TAG_DONE);
    SetGadgetAttrs((struct Gadget *)pw.device, w, NULL, GA_Disabled, (ULONG)!(ps && dest == PRDEST_DEVICE), TAG_DONE);
    SetGadgetAttrs((struct Gadget *)pw.imagedpi, w, NULL, GA_Disabled, (ULONG)printer, TAG_DONE);
    SetGadgetAttrs((struct Gadget *)pw.copies, w, NULL, GA_Disabled, (ULONG)!printer, TAG_DONE);
    SetGadgetAttrs((struct Gadget *)pw.serif, w, NULL, GA_Disabled, (ULONG)printer, TAG_DONE);
    SetGadgetAttrs((struct Gadget *)pw.size, w, NULL, GA_Disabled, (ULONG)printer, TAG_DONE);
}

/**
 * @brief Create the gadgets of the print window.
 * @param s The options shown.
 * @param file The output file.
 * @param from The first page.
 * @param to The last page, 0 = to the end.
 * @return The root layout, or NULL.
 */
static Object *build(const PrintSettings *s, const char *file, LONG from, LONG to)
{
    struct Node *n;
    LONG i, mode, dest;
    Object *output, *page;

    mode_labels[MI_PRINTER] = (STRPTR)S(MSG_PR_MODE_PRINTER);
    mode_labels[MI_PS2] = (STRPTR)"PostScript Level 2";
    mode_labels[MI_PS1] = (STRPTR)"PostScript Level 1";
    mode_labels[MI_PDF] = (STRPTR)"PDF";
    font_labels[0] = (STRPTR)S(MSG_PR_SANS);
    font_labels[1] = (STRPTR)S(MSG_PR_SERIF);

    /* destinations of PostScript; PS: only if TurboPrint provides it */
    NewList(&pw.destlist);
    for (i = 0; i < NUMPRDESTS; i++) {
        static const LONG msgs[NUMPRDESTS] = { MSG_PR_DEST_FILE, MSG_PR_DEST_PRT, MSG_PR_DEST_PS, MSG_PR_DEST_DEVICE };
        if ((n = AllocChooserNode(CNA_Text, (ULONG)S(msgs[i]),
                                  CNA_Disabled, i == PRDEST_PS && !have_ps(), TAG_DONE)))
            AddTail(&pw.destlist, n);
    }

    /* the default printer (unit 0), shown with its driver; resolution
     * and everything else come from its settings */
    pw.tp = turboprint_running(pw.printername);
    if (!pw.tp) prefs_driver(pw.printername);
    if (!pw.printername[0]) strcpy(pw.printername, "generic");

    /* no kind chosen yet: with TurboPrint PostScript level 2 to PS: (its
     * Ghostscript renders better than a graphics dump), else the printer */
    mode = s->prmode;
    dest = s->prdest;
    if (mode < 0 || (pw.tp && mode == PRMODE_PRINTER)) {
        mode = pw.tp ? PRMODE_PS : PRMODE_PRINTER;
        if (pw.tp && have_ps()) dest = PRDEST_PS;
    }
    /* with TurboPrint no graphics printing */
    NewList(&pw.modelist);
    for (i = 0; i < NUMMI; i++)
        if ((n = AllocChooserNode(CNA_Text, (ULONG)mode_labels[i],
                                  CNA_Disabled, i == MI_PRINTER && pw.tp, TAG_DONE)))
            AddTail(&pw.modelist, n);
    pw.mode = NewObject(CHOOSER_GetClass(), NULL,
        GA_ID, PW_MODE, GA_RelVerify, TRUE, CHOOSER_PopUp, TRUE,
        CHOOSER_Labels, (ULONG)&pw.modelist,
        CHOOSER_Selected, (ULONG)item_of(mode, s->prmode < 0 || s->prmode == PRMODE_PRINTER ? 2 : s->pslevel), TAG_DONE);
    pw.dest = NewObject(CHOOSER_GetClass(), NULL,
        GA_ID, PW_DEST, GA_RelVerify, TRUE, CHOOSER_PopUp, TRUE,
        CHOOSER_Labels, (ULONG)&pw.destlist,
        CHOOSER_Selected, (ULONG)(dest >= 0 && dest < NUMPRDESTS ? dest : 0), TAG_DONE);
    pw.file = NewObject(GETFILE_GetClass(), NULL,
        GA_ID, PW_FILE, GA_RelVerify, TRUE,
        GETFILE_TitleText, (ULONG)S(MSG_PR_FILE_TITLE), GETFILE_FullFile, (ULONG)file,
        GETFILE_DoSaveMode, TRUE, GETFILE_RejectIcons, TRUE, TAG_DONE);
    pw.device = NewObject(STRING_GetClass(), NULL,
        GA_ID, PW_DEVICE, GA_RelVerify, TRUE, GA_TabCycle, TRUE,
        STRINGA_TextVal, (ULONG)(s->prdevice[0] ? s->prdevice : "PAR:"),
        STRINGA_MaxChars, sizeof(s->prdevice) - 1, TAG_DONE);
    /* only shown, like a label (style guide: read-only button.gadget);
     * driver names may contain '_'. GA_Underscore before GA_Text: the
     * class looks for the shortcut when it takes the text. */
    pw.printer = NewObject(BUTTON_GetClass(), NULL,
        GA_ReadOnly, TRUE, GA_Underscore, 0, GA_Text, (ULONG)pw.printername, BUTTON_Justification, BCJ_LEFT,
        BUTTON_BevelStyle, BVS_NONE, BUTTON_Transparent, TRUE, TAG_DONE);
    pw.from = integer(PW_FROM, from, 1, 9999, 4);
    pw.to = integer(PW_TO, to, 0, 9999, 4);
    pw.copies = integer(PW_COPIES, 1, 1, 99, 2);
    imagedpi_labels[0] = (STRPTR)S(MSG_PR_IMAGEDPI_ORIGINAL);
    for (i = 0; i < NUMIMAGEDPIS - 1 && imagedpis[i] != s->primagedpi; i++) ;
    if (imagedpis[i] != s->primagedpi) i = 0;
    pw.imagedpi = chooser(PW_IMAGEDPI, imagedpi_labels, (ULONG)i);
    output = NewObject(LAYOUT_GetClass(), NULL, PAGE_GROUP(S(MSG_PR_OUTPUT)),
        LAYOUT_AddChild, (ULONG)pw.mode,   FIXED, CHILD_Label, (ULONG)label(S(MSG_PR_MODE)),
        LAYOUT_AddChild, (ULONG)pw.dest,   FIXED, CHILD_Label, (ULONG)label(S(MSG_PR_DEST)),
        LAYOUT_AddChild, (ULONG)pw.file,   FIXED, CHILD_Label, (ULONG)label(S(MSG_PR_FILE)),
        LAYOUT_AddChild, (ULONG)pw.device, FIXED, CHILD_Label, (ULONG)label(S(MSG_PR_DEVICE)),
        LAYOUT_AddChild, (ULONG)pw.imagedpi, FIXED, CHILD_Label, (ULONG)label(S(MSG_PR_IMAGEDPI)),
        LAYOUT_AddChild, (ULONG)pw.printer, FIXED, CHILD_Label, (ULONG)label(S(MSG_PR_PRINTER)),
        LAYOUT_AddChild, (ULONG)NewObject(LAYOUT_GetClass(), NULL,
            LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ,
            LAYOUT_AddChild, (ULONG)pw.from,
            LAYOUT_AddChild, (ULONG)pw.to,     CHILD_Label, (ULONG)label(S(MSG_PR_TO)),
            LAYOUT_AddChild, (ULONG)pw.copies, CHILD_Label, (ULONG)label(S(MSG_PR_COPIES)),
            TAG_DONE),
        FIXED, CHILD_Label, (ULONG)label(S(MSG_PR_FROM)),
        LAYOUT_AddImage, (ULONG)label(S(MSG_PR_RANGE_NOTE)), FIXED,
        TAG_DONE);

    pw.paper = chooser(PW_PAPER, paper_labels, (ULONG)(s->paper >= 0 && s->paper < NUMPAPERS ? s->paper : 0));
    for (i = 0; i < 4; i++) pw.margin[i] = integer(PW_ML + (ULONG)i, s->margins[i], 0, 100, 3);
    pw.serif = chooser(PW_SERIF, font_labels, s->prserif ? 1 : 0);
    pw.size = integer(PW_SIZE, s->prsize, 4, 36, 2);
    pw.pagenumbers = checkbox(PW_PAGENUMBERS, S(MSG_PR_PAGENUMBERS), s->prpagenumbers);
    pw.backgrounds = checkbox(PW_BACKGROUNDS, S(MSG_PR_BACKGROUNDS), s->prbackgrounds);
    page = NewObject(LAYOUT_GetClass(), NULL, PAGE_GROUP(S(MSG_PR_PAGE)),
        LAYOUT_AddChild, (ULONG)pw.paper, FIXED, CHILD_Label, (ULONG)label(S(MSG_PR_PAPER)),
        /* the margins in two rows: left/right, top/bottom */
        LAYOUT_AddChild, (ULONG)NewObject(LAYOUT_GetClass(), NULL,
            LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ,
            LAYOUT_AddChild, (ULONG)pw.margin[0], CHILD_Label, (ULONG)label(S(MSG_PR_LEFT)),
            LAYOUT_AddChild, (ULONG)pw.margin[2], CHILD_Label, (ULONG)label(S(MSG_PR_RIGHT)),
            TAG_DONE),
        FIXED, CHILD_Label, (ULONG)label(S(MSG_PR_MARGINS)),
        LAYOUT_AddChild, (ULONG)NewObject(LAYOUT_GetClass(), NULL,
            LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ,
            LAYOUT_AddChild, (ULONG)pw.margin[1], CHILD_Label, (ULONG)label(S(MSG_PR_TOP)),
            LAYOUT_AddChild, (ULONG)pw.margin[3], CHILD_Label, (ULONG)label(S(MSG_PR_BOTTOM)),
            TAG_DONE),
        FIXED, CHILD_Label, (ULONG)label(" "),
        LAYOUT_AddChild, (ULONG)NewObject(LAYOUT_GetClass(), NULL,
            LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ,
            LAYOUT_AddChild, (ULONG)pw.serif,
            LAYOUT_AddChild, (ULONG)pw.size, CHILD_Label, (ULONG)label(S(MSG_PR_SIZE)),
            CHILD_WeightedWidth, 0,
            TAG_DONE),
        FIXED, CHILD_Label, (ULONG)label(S(MSG_PR_FONT)),
        LAYOUT_AddChild, (ULONG)pw.pagenumbers, FIXED,
        LAYOUT_AddChild, (ULONG)pw.backgrounds, FIXED,
        LAYOUT_AddImage, (ULONG)label(S(MSG_PR_MARGIN_NOTE)), FIXED,
        TAG_DONE);

    pw.print = button(PW_PRINT, S(MSG_PR_PRINT));
    return NewObject(LAYOUT_GetClass(), NULL,
        LAYOUT_Orientation,   LAYOUT_ORIENT_VERT,
        LAYOUT_SpaceOuter,    TRUE,
        LAYOUT_LeftSpacing,   6,
        LAYOUT_RightSpacing,  6,
        LAYOUT_TopSpacing,    6,
        LAYOUT_BottomSpacing, 6,
        LAYOUT_InnerSpacing,  6,
        LAYOUT_DeferLayout,   TRUE,
        LAYOUT_AddChild, (ULONG)output, CHILD_WeightedHeight, 0,
        LAYOUT_AddChild, (ULONG)page,   CHILD_WeightedHeight, 0,
        LAYOUT_AddChild, (ULONG)filler(),
        /* positive action left, negative right */
        LAYOUT_AddChild, (ULONG)NewObject(LAYOUT_GetClass(), NULL,
            LAYOUT_Orientation,  LAYOUT_ORIENT_HORIZ,
            LAYOUT_AddChild,     (ULONG)pw.print,
            CHILD_WeightedWidth, 0,
            LAYOUT_AddChild,     (ULONG)filler(),
            LAYOUT_AddChild,     (ULONG)button(PW_CANCEL, S(MSG_PR_CANCEL)),
            CHILD_WeightedWidth, 0,
            TAG_DONE),
        CHILD_WeightedHeight, 0,
        TAG_DONE);
}

/**
 * @brief Take the options, the output file and the page range from the
 *        gadgets.
 * @param[out] s The options.
 * @param[out] file The output file.
 * @param filesize Its buffer's size.
 * @param[out] job Page range and copies.
 */
static void read_gadgets(PrintSettings *s, char *file, ULONG filesize, PrintJob *job)
{
    STRPTR str;
    LONG i;

    s->prmode = mode_of(get(pw.mode, CHOOSER_Selected));
    if (s->prmode == PRMODE_PS) s->pslevel = get(pw.mode, CHOOSER_Selected) == MI_PS1 ? 1 : 2;
    i = get(pw.imagedpi, CHOOSER_Selected);
    s->primagedpi = i >= 0 && i < NUMIMAGEDPIS ? imagedpis[i] : 0;
    s->prdest = get(pw.dest, CHOOSER_Selected);
    s->prdevice[0] = 0;
    if ((str = (STRPTR)get(pw.device, STRINGA_TextVal))) strncat(s->prdevice, (const char *)str, sizeof(s->prdevice) - 1);
    s->paper = get(pw.paper, CHOOSER_Selected);
    for (i = 0; i < 4; i++) s->margins[i] = get(pw.margin[i], INTEGER_Number);
    s->prserif = get(pw.serif, CHOOSER_Selected) == 1;
    s->prsize = get(pw.size, INTEGER_Number);
    s->prpagenumbers = get(pw.pagenumbers, CHECKBOX_Checked) != 0;
    s->prbackgrounds = get(pw.backgrounds, CHECKBOX_Checked) != 0;
    file[0] = 0;
    if ((str = (STRPTR)get(pw.file, GETFILE_FullFile))) strncat(file, (const char *)str, filesize - 1);
    job->first = get(pw.from, INTEGER_Number);
    job->last = get(pw.to, INTEGER_Number);
    job->copies = get(pw.copies, INTEGER_Number);
}

/**
 * @brief Show the print window (modal: the main window waits).
 * @param[in,out] s The options; changed on Print.
 * @param[in,out] file The output file; changed on Print.
 * @param filesize Its buffer's size.
 * @param[in,out] job Page range and copies; changed on Print.
 * @return TRUE for Print, FALSE for Cancel.
 */
static int print_dialog(PrintSettings *s, char *file, ULONG filesize, PrintJob *job)
{
    ULONG sig = 0, mainsig = 0, result;
    UWORD code;
    BOOL done = FALSE;
    int rc = FALSE;

    if (!open_classes()) return FALSE;
    memset(&pw, 0, sizeof(pw));
    set_extension(file, filesize, s->prmode);
    if (!(pw.root = build(s, file, job->first > 0 ? job->first : 1, job->last))) goto out;
    pw.winobj = NewObject(WINDOW_GetClass(), NULL,
        WA_Title,           (ULONG)S(MSG_PR_TITLE),
        WA_PubScreen,       (ULONG)mw.screen,
        WA_Activate,        TRUE,
        WA_DepthGadget,     TRUE,
        WA_DragBar,         TRUE,
        WA_CloseGadget,     TRUE,
        WA_SizeGadget,      TRUE,
        WINDOW_RefWindow,   (ULONG)mw.win,
        WINDOW_Position,    WPOS_CENTERWINDOW,
        WINDOW_ParentGroup, (ULONG)pw.root,
        TAG_DONE);
    if (!pw.winobj) {
        DisposeObject(pw.root);
        goto out;
    }
    if (!(pw.win = (struct Window *)DoMethod(pw.winobj, WM_OPEN))) goto out;
    update_gadgets();

    busy(TRUE);
    GetAttr(WINDOW_SigMask, mw.winobj, &mainsig);
    GetAttr(WINDOW_SigMask, pw.winobj, &sig);
    while (!done) {
        ULONG got = Wait(sig | mainsig);

        /* the main window waits: its input is dropped */
        if (got & mainsig) drop_main_input();

        while ((result = DoMethod(pw.winobj, WM_HANDLEINPUT, &code)) != WMHI_LASTMSG) {
            if ((result & WMHI_CLASSMASK) == WMHI_CLOSEWINDOW) {
                done = TRUE;
                continue;
            }
            if ((result & WMHI_CLASSMASK) != WMHI_GADGETUP) continue;
            switch (result & WMHI_GADGETMASK) {
            case PW_MODE: {
                char f[PATHMAX];
                STRPTR str = (STRPTR)get(pw.file, GETFILE_FullFile);
                f[0] = 0;
                if (str) strncat(f, (const char *)str, sizeof(f) - 1);
                if (f[0]) {
                    set_extension(f, sizeof(f), mode_of(get(pw.mode, CHOOSER_Selected)));
                    SetGadgetAttrs((struct Gadget *)pw.file, pw.win, NULL, GETFILE_FullFile, (ULONG)f, TAG_DONE);
                }
                update_gadgets();
                break;
            }
            case PW_DEST:
                update_gadgets();
                break;
            case PW_FILE:
                gfRequestFile(pw.file, pw.win);
                break;
            case PW_PRINT:
                read_gadgets(s, file, filesize, job);
                rc = TRUE;
                done = TRUE;
                break;
            case PW_CANCEL:
                done = TRUE;
                break;
            }
        }
    }
    busy(FALSE);
out:
    if (pw.winobj) DisposeObject(pw.winobj);
    free_nodes(&pw.destlist);
    free_nodes(&pw.modelist);
    memset(&pw, 0, sizeof(pw));
    return rc;
}

/*****************************************************************************/
/* running                                                                   */

/**
 * @brief Tell whether the page's gadget can print (version 1.2 or newer).
 * @return TRUE if it can.
 */
static BOOL gadget_can_print(void)
{
    return HTMLBase && (HTMLBase->lib_Version > 1 || (HTMLBase->lib_Version == 1 && HTMLBase->lib_Revision >= 2));
}

/** The source of a page for printer.device's source hook. */
struct PrintSrc {
    Object        *gadget;          /**< the page's gadget */
    LONG           page;            /**< the page printed */
    struct Task   *task;            /**< told about each strip, for the progress */
    ULONG          sigmask;         /**< the signal for it, 0 = none */
    volatile LONG  row;             /**< rows of the page delivered so far */
    LONG           sw;              /**< page width as the gadget renders it */
    LONG           sh;              /**< page height as the gadget renders it */
    LONG           dw;              /**< page width in printer dots */
    LONG           dh;              /**< page height in printer dots */
    ULONG         *rowbuf;          /**< one row of the gadget's page, sw pixels */
    LONG           cached;          /**< the row in rowbuf, -1: none */
};

/**
 * @brief DRPA_SourceHook, called by printer.device on its task: the
 *        pixels of a strip, in printer dots.
 *
 * printer.device gets the page in exactly its dots and scales nothing (its
 * scaling squeezed the page vertically); if the gadget's page has another
 * size, it is scaled here, nearest pixel.
 *
 * @param h The hook (h_Data: struct PrintSrc).
 * @param o Unused.
 * @param m The strip wanted.
 * @return TRUE if the pixels were delivered.
 */
static ULONG print_source(struct Hook *h __asm("a0"), APTR o __asm("a2"), struct DRPSourceMsg *m __asm("a1"))
{
    struct PrintSrc *ps = h->h_Data;
    ULONG rc = TRUE;

    (void)o;
    if (ps->sw == ps->dw && ps->sh == ps->dh)
        rc = DoMethod(ps->gadget, HTMLM_PrintRender, ps->page, m->x, m->y, m->width, m->height, (ULONG)m->buf);
    else {
        ULONG step = ((ULONG)ps->sw << 16) / (ULONG)ps->dw, pos, *out = m->buf;
        LONG r, i, sy;
        for (r = 0; r < m->height; r++) {
            sy = ((2 * (m->y + r) + 1) * ps->sh) / (2 * ps->dh);
            if (sy >= ps->sh) sy = ps->sh - 1;
            if (sy != ps->cached) {
                if (!DoMethod(ps->gadget, HTMLM_PrintRender, ps->page, 0, sy, ps->sw, 1, (ULONG)ps->rowbuf))
                    rc = FALSE;
                ps->cached = sy;
            }
            pos = (ULONG)m->x * step + step / 2;
            for (i = 0; i < m->width; i++, pos += step) {
                ULONG sx = pos >> 16;
                *out++ = ps->rowbuf[sx < (ULONG)ps->sw ? sx : (ULONG)ps->sw - 1];
            }
        }
    }
    if ((LONG)(m->y + m->height) > ps->row) {
        ps->row = m->y + m->height;
        if (ps->sigmask) Signal(ps->task, ps->sigmask);
    }
    return rc;
}

/** A print to printer.device. */
struct BitmapPrint {
    struct MsgPort      *port;      /**< reply port of the request */
    struct IODRPTagsReq *io;        /**< the request */
    struct Hook          hook;      /**< DRPA_SourceHook */
    struct PrintSrc      src;       /**< its data */
    struct RastPort      rp;        /**< an empty rastport (the hook delivers the pixels) */
    struct ColorMap     *cmap;      /**< the screen's colours */
    struct TagItem       drtags[4]; /**< tags of the request */
    LONG                 paper_w;   /**< paper width in points */
    LONG                 paper_h;   /**< paper height in points */
    LONG                 sw;        /**< page width as the gadget renders it */
    LONG                 sh;        /**< page height as the gadget renders it */
    LONG                 dw;        /**< page width in printer dots */
    LONG                 dh;        /**< page height in printer dots */
    LONG                 density;   /**< 1-7, that of the printer settings */
    BOOL                 opened;    /**< printer.device is open */
};

/**
 * @brief Send a page to printer.device (SendIO()).
 * @param bp The print.
 * @param page The page.
 */
static void print_send(struct BitmapPrint *bp, LONG page)
{
    struct IODRPTagsReq *io = bp->io;

    bp->src.page = page;
    bp->src.row = 0;
    bp->src.cached = -1;
    io->io_Command = PRD_DUMPRPORTTAGS;
    io->io_RastPort = &bp->rp;
    io->io_ColorMap = bp->cmap;
    io->io_Modes = 0;
    io->io_SrcX = 0;
    io->io_SrcY = 0;
    /* the whole sheet, one source pixel per printer dot */
    io->io_SrcWidth = (UWORD)bp->dw;
    io->io_SrcHeight = (UWORD)bp->dh;
    io->io_DestCols = bp->dw;
    io->io_DestRows = bp->dh;
    io->io_Special = (UWORD)(bp->density << 8);     /* SPECIAL_DENSITY1-7 */
    io->io_TagList = bp->drtags;
    SendIO((struct IORequest *)io);
}

/** The modal progress window of printing and export. */
struct Progress {
    Object        *winobj;          /**< window.class object */
    Object        *text;            /**< text line */
    Object        *gauge;           /**< fuel gauge */
    Object        *stop;            /**< Stop button */
    struct Window *win;             /**< the open window, NULL if none */
    ULONG          sig;             /**< its signals */
    ULONG          mainsig;         /**< the main window's signals */
    LONG           shown;           /**< the gauge's level */
    BOOL           stopped;         /**< Stop was chosen */
    LONG           first;           /**< export: the first page */
    LONG           last;            /**< export: the last page, 0 = to the end */
    LONG           max;             /**< the gauge's maximum */
    char           buf[100];        /**< the text */
};

static void progress_set(struct Progress *p, const char *text, LONG level);

/**
 * @brief Open the progress window.
 * @param[out] p The progress.
 * @param widest A text as wide as the longest one to come.
 * @param max The gauge's maximum.
 */
static void progress_open(struct Progress *p, const char *widest, LONG max)
{
    memset(p, 0, sizeof(*p));
    p->shown = -1;
    p->max = max > 0 ? max : 1;
    /* sized for the longer of the page text and "Preparing" */
    if (strlen(S(MSG_PR_PREPARING)) > strlen(widest)) widest = S(MSG_PR_PREPARING);
    strncpy(p->buf, widest, sizeof(p->buf) - 1);
    p->text = NewObject(BUTTON_GetClass(), NULL,
        GA_ReadOnly, TRUE, GA_Underscore, 0, GA_Text, (ULONG)p->buf, BUTTON_BevelStyle, BVS_NONE,
        BUTTON_Justification, BCJ_LEFT, TAG_DONE);
    p->gauge = NewObject(FUELGAUGE_GetClass(), NULL,
        FUELGAUGE_Min, 0, FUELGAUGE_Max, (ULONG)p->max, FUELGAUGE_Level, 0,
        FUELGAUGE_Percent, TRUE, FUELGAUGE_Ticks, 0, TAG_DONE);
    p->stop = button(PW_STOP, S(MSG_PR_STOP));
    p->winobj = NewObject(WINDOW_GetClass(), NULL,
        WA_Title,           (ULONG)S(MSG_PR_PROGRESS_TITLE),
        WA_PubScreen,       (ULONG)mw.screen,
        WA_Activate,        TRUE,
        WA_DepthGadget,     TRUE,
        WA_DragBar,         TRUE,
        WA_CloseGadget,     TRUE,
        WA_IDCMP,           IDCMP_RAWKEY,
        WINDOW_RefWindow,   (ULONG)mw.win,
        WINDOW_Position,    WPOS_CENTERWINDOW,
        WINDOW_ParentGroup, (ULONG)NewObject(LAYOUT_GetClass(), NULL,
            LAYOUT_Orientation,  LAYOUT_ORIENT_VERT,
            LAYOUT_SpaceOuter,   TRUE,
            LAYOUT_DeferLayout,  TRUE,
            LAYOUT_AddChild,     (ULONG)p->text,
            LAYOUT_AddChild,     (ULONG)p->gauge,
            CHILD_MinWidth,      240,
            LAYOUT_AddChild,     (ULONG)NewObject(LAYOUT_GetClass(), NULL,
                LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ,
                LAYOUT_AddChild,    (ULONG)filler(),
                LAYOUT_AddChild,    (ULONG)p->stop,
                CHILD_WeightedWidth, 0,
                LAYOUT_AddChild,    (ULONG)filler(),
                TAG_DONE),
            CHILD_WeightedHeight, 0,
            TAG_DONE),
        TAG_DONE);
    if (p->winobj) p->win = (struct Window *)DoMethod(p->winobj, WM_OPEN);
    if (p->win) GetAttr(WINDOW_SigMask, p->winobj, &p->sig);
    GetAttr(WINDOW_SigMask, mw.winobj, &p->mainsig);
    busy(TRUE);
    /* until the first page: the export writes PDF pictures before it */
    progress_set(p, S(MSG_PR_PREPARING), 0);
}

/**
 * @brief Show a new text (also in the status line) and the gauge.
 * @param p The progress.
 * @param text The text, or NULL to keep it.
 * @param level The gauge's level.
 */
static void progress_set(struct Progress *p, const char *text, LONG level)
{
    if (text) {
        strncpy(p->buf, text, sizeof(p->buf) - 1);
        set_status(p->buf);
        if (p->win) SetGadgetAttrs((struct Gadget *)p->text, p->win, NULL, GA_Text, (ULONG)p->buf, TAG_DONE);
    }
    if (p->win && level != p->shown)
        SetGadgetAttrs((struct Gadget *)p->gauge, p->win, NULL, FUELGAUGE_Level, (ULONG)(p->shown = level), TAG_DONE);
}

/**
 * @brief Handle the progress window's input.
 *
 * The main window's input is dropped if 'main' (not while the gadget
 * works on our stack: the page must not be drawn then).
 *
 * @param p The progress.
 * @param main Non-zero to drop the main window's input as well.
 * @return TRUE once Stop, the close gadget or Esc came.
 */
static BOOL progress_input(struct Progress *p, BOOL main)
{
    ULONG result;
    UWORD code;
    BOOL stop = FALSE;

    if (main) drop_main_input();
    if (p->win)
        while ((result = DoMethod(p->winobj, WM_HANDLEINPUT, &code)) != WMHI_LASTMSG)
            switch (result & WMHI_CLASSMASK) {
            case WMHI_CLOSEWINDOW: stop = TRUE; break;
            case WMHI_GADGETUP: if ((result & WMHI_GADGETMASK) == PW_STOP) stop = TRUE; break;
            case WMHI_RAWKEY: if ((result & WMHI_KEYMASK) == 0x45) stop = TRUE; break;     /* Esc */
            }
    if (stop && !p->stopped) {
        p->stopped = TRUE;
        if (p->win) SetGadgetAttrs((struct Gadget *)p->stop, p->win, NULL, GA_Disabled, TRUE, TAG_DONE);
        return TRUE;
    }
    return FALSE;
}

/**
 * @brief Close the progress window.
 * @param p The progress.
 */
static void progress_close(struct Progress *p)
{
    busy(FALSE);
    if (p->winobj) DisposeObject(p->winobj);
    p->winobj = NULL;
    p->win = NULL;
}

/**
 * @brief Wait until printer.device is done with a page (also when it is
 *        stopped), moving the gauge with the strips delivered.
 * @param bp The print.
 * @param pr The progress.
 * @param printed Pages printed before this one.
 */
static void wait_io(struct BitmapPrint *bp, struct Progress *pr, LONG printed)
{
    ULONG portsig = 1UL << bp->port->mp_SigBit;

    for (;;) {
        ULONG got = Wait(pr->sig | pr->mainsig | portsig | bp->src.sigmask);

        if ((got & bp->src.sigmask) && bp->dh > 0) {
            LONG row = bp->src.row;
            if (row > bp->dh) row = bp->dh;
            progress_set(pr, NULL, printed * 100 + row * 100 / bp->dh);
        }
        if (progress_input(pr, TRUE)) AbortIO((struct IORequest *)bp->io);
        if (CheckIO((struct IORequest *)bp->io)) {
            WaitIO((struct IORequest *)bp->io);
            return;
        }
    }
}

/**
 * @brief Print the pages one after the other with the progress window:
 *        the page being printed, a gauge over all pages and copies, Stop.
 * @param bp The print.
 * @param first The first page.
 * @param last The last page.
 * @param copies The copies.
 * @return The pages printed, -1 when stopped, or the negative printer
 *         error - 1.
 */
static LONG print_pages(struct BitmapPrint *bp, LONG first, LONG last, LONG copies)
{
    struct Progress pr;
    LONG page = first, copy = 0, printed = 0, total = (last - first + 1) * copies, err = 0, stripbit;
    char buf[100];

    fmt(buf, sizeof(buf), S(MSG_PR_PRINTING), (long)last, (long)last);
    progress_open(&pr, buf, total * 100);
    /* the source hook signals each strip: the gauge moves within a page,
     * in 1/100 page */
    bp->src.task = FindTask(NULL);
    if ((stripbit = AllocSignal(-1)) >= 0) bp->src.sigmask = 1UL << stripbit;

    for (;;) {
        fmt(buf, sizeof(buf), S(MSG_PR_PRINTING), (long)page, (long)last);
        progress_set(&pr, buf, printed * 100);
        print_send(bp, page);
        wait_io(bp, &pr, printed);
        err = bp->io->io_Error;
        if (pr.stopped || err) break;
        printed++;
        if (++page > last) {
            page = first;
            if (++copy >= copies) break;
        }
    }

    bp->src.sigmask = 0;
    if (stripbit >= 0) FreeSignal((BYTE)stripbit);
    progress_close(&pr);
    if (pr.stopped || err == PDERR_CANCEL) return -1;
    if (err) return -err - 1;
    return printed;
}

/**
 * @brief Ask the driver for the resolution of a density.
 *
 * A dump with SPECIAL_NOPRINT makes the driver switch to it and updates
 * XDotsInch/YDotsInch (before, they may be those of the last print): the
 * classic PRD_DUMPRPORT with a small empty bitmap. Not for TurboPrint.
 *
 * @param io The open request.
 * @param density The density (1-7).
 * @param[out] x Dots per inch across.
 * @param[out] y Dots per inch down.
 * @return FALSE if it fails.
 */
static BOOL printer_dpi(struct IODRPTagsReq *io, LONG density, LONG *x, LONG *y)
{
    struct PrinterExtendedData *ped = &((struct PrinterData *)io->io_Device)->pd_SegmentData->ps_PED;
    struct RastPort rp;
    struct BitMap bm;
    PLANEPTR plane;
    LONG err;

    *x = *y = 0;
    /* 32 bytes would do; the reserve is for printer.devices that take
     * more than the bitmap says: it must not hit other chip memory */
    if (!(plane = AllocVec(QUERYPLANE, MEMF_CHIP | MEMF_CLEAR))) return FALSE;
    InitBitMap(&bm, 1, 16, 16);
    bm.Planes[0] = plane;
    InitRastPort(&rp);
    rp.BitMap = &bm;
    io->io_Command = PRD_DUMPRPORT;
    io->io_RastPort = &rp;
    io->io_ColorMap = mw.screen->ViewPort.ColorMap;
    io->io_Modes = 0;
    io->io_SrcX = io->io_SrcY = 0;
    io->io_SrcWidth = io->io_SrcHeight = 16;
    io->io_DestCols = io->io_DestRows = 1000;
    io->io_Special = (UWORD)(SPECIAL_MILCOLS | SPECIAL_MILROWS | SPECIAL_NOPRINT | (density << 8));
    io->io_TagList = NULL;
    err = DoIO((struct IORequest *)io);
    FreeVec(plane);
    if (err || !ped->ped_XDotsInch || !ped->ped_YDotsInch) return FALSE;
    *x = ped->ped_XDotsInch;
    *y = ped->ped_YDotsInch;
    return TRUE;
}

/**
 * @brief Print the page to the default printer as graphics.
 * @param s The options.
 * @param job Page range and copies.
 */
static void print_bitmap(const PrintSettings *s, const PrintJob *job)
{
    /* the paper in 1/10 mm: the sizes in points are rounded, A4 would
     * miss a column at 300 dpi */
    static const LONG tenthmm[NUMPAPERS][2] = { { 2100, 2970 }, { 1480, 2100 }, { 2159, 2794 }, { 2159, 3556 } };
    struct BitmapPrint bp;
    struct PrinterExtendedData *ped;
    LONG pages = 0, first, last, n, xdpi, ydpi, dpi;
    char buf[100];
    struct TagItem tags[] = {
        { HTMLEX_DPI, 0 }, { HTMLEX_PaperWidth, 0 }, { HTMLEX_PaperHeight, 0 },
        { HTMLEX_MarginLeft, 0 }, { HTMLEX_MarginTop, 0 }, { HTMLEX_MarginRight, 0 }, { HTMLEX_MarginBottom, 0 },
        { HTMLEX_FontSize, 100 }, { HTMLEX_Backgrounds, 0 }, { HTMLEX_Footer, 0 },
        { HTMLEX_Pages, 0 }, { HTMLEX_PageWidth, 0 }, { HTMLEX_PageHeight, 0 }, { TAG_DONE, 0 }
    };

    memset(&bp, 0, sizeof(bp));
    if (!FuelGaugeBase && !(FuelGaugeBase = OpenLibrary((STRPTR)"gadgets/fuelgauge.gadget", 44))) {
        fmt(buf, sizeof(buf), S(MSG_ERR_CLASS), "fuelgauge.gadget");
        message(buf);
        return;
    }
    bp.paper_w = paper_sizes[s->paper][0];
    bp.paper_h = paper_sizes[s->paper][1];
    if (!(bp.port = CreateMsgPort()) || !(bp.io = (struct IODRPTagsReq *)CreateIORequest(bp.port, sizeof(*bp.io)))) {
        message(S(MSG_PR_NOMEM));
        goto out;
    }
    /* TurboPrint: graphics are not printed through it */
    if (turboprint_running(NULL)) {
        message(S(MSG_PR_TURBOPRINT));
        goto out;
    }
    if (OpenDevice((STRPTR)"printer.device", 0, (struct IORequest *)bp.io, 0)) {
        DeleteIORequest((struct IORequest *)bp.io);
        bp.io = NULL;
        message(S(MSG_PR_NODEVICE));
        goto out;
    }
    bp.opened = TRUE;

    /* The density is that of the printer settings; the sheet goes in the
     * driver's dots at it. The gadget renders not finer than
     * MAXRENDERDPI. */
    bp.density = ((struct PrinterData *)bp.io->io_Device)->pd_Preferences.PrintDensity;
    if (bp.density < 1 || bp.density > 7) bp.density = 1;
    ped = &((struct PrinterData *)bp.io->io_Device)->pd_SegmentData->ps_PED;
    if (!printer_dpi(bp.io, bp.density, &xdpi, &ydpi)) {
        xdpi = ped->ped_XDotsInch ? ped->ped_XDotsInch : 300;
        ydpi = ped->ped_YDotsInch ? ped->ped_YDotsInch : xdpi;
    }
    bp.dw = (tenthmm[s->paper][0] * xdpi + 127) / 254;
    bp.dh = (tenthmm[s->paper][1] * ydpi + 127) / 254;
    if (ped->ped_MaxXDots && bp.dw > (LONG)ped->ped_MaxXDots) bp.dw = (LONG)ped->ped_MaxXDots;
    if (ped->ped_MaxYDots && bp.dh > (LONG)ped->ped_MaxYDots) bp.dh = (LONG)ped->ped_MaxYDots;
    dpi = xdpi < MAXRENDERDPI ? xdpi : MAXRENDERDPI;

    tags[0].ti_Data = (ULONG)dpi;
    tags[1].ti_Data = (ULONG)bp.paper_w;
    tags[2].ti_Data = (ULONG)bp.paper_h;
    tags[3].ti_Data = (ULONG)mm_pt(s->margins[0]);
    tags[4].ti_Data = (ULONG)mm_pt(s->margins[1]);
    tags[5].ti_Data = (ULONG)mm_pt(s->margins[2]);
    tags[6].ti_Data = (ULONG)mm_pt(s->margins[3]);
    /* FontSize stays 10 pt: the fonts of the page view are printed */
    tags[8].ti_Data = (ULONG)s->prbackgrounds;
    tags[9].ti_Data = s->prpagenumbers ? (ULONG)S(MSG_PR_FOOTER) : 0;
    tags[10].ti_Data = (ULONG)&pages;
    tags[11].ti_Data = (ULONG)&bp.sw;
    tags[12].ti_Data = (ULONG)&bp.sh;
    busy(TRUE);
    n = (LONG)DoMethod(mw.html, HTMLM_PrintBegin, (ULONG)tags);
    busy(FALSE);
    if (n <= 0 || bp.sw <= 0 || bp.sh <= 0) {
        message(S(MSG_PR_NOPAGES));
        goto out;
    }
    first = job->first > 0 ? job->first : 1;
    last = job->last > 0 && job->last < pages ? job->last : pages;
    if (first > last) goto out;
    if (!(bp.src.rowbuf = AllocVec((ULONG)bp.sw * 4, MEMF_ANY))) {
        message(S(MSG_PR_NOMEM));
        goto out;
    }
    bp.src.sw = bp.sw;
    bp.src.sh = bp.sh;
    bp.src.dw = bp.dw;
    bp.src.dh = bp.dh;
    InitRastPort(&bp.rp);
    bp.cmap = mw.screen->ViewPort.ColorMap;
    bp.hook.h_Entry = (ULONG (*)())print_source;
    bp.hook.h_Data = &bp.src;
    bp.src.gadget = mw.html;
    bp.drtags[0].ti_Tag = DRPA_SourceHook;
    bp.drtags[0].ti_Data = (ULONG)&bp.hook;
    bp.drtags[1].ti_Tag = DRPA_AspectX;
    bp.drtags[1].ti_Data = 1;
    bp.drtags[2].ti_Tag = DRPA_AspectY;
    bp.drtags[2].ti_Data = 1;
    bp.drtags[3].ti_Tag = TAG_DONE;

    n = print_pages(&bp, first, last, job->copies > 0 ? job->copies : 1);
    if (n == -1) set_status(S(MSG_PR_STOPPED));
    else if (n < 0) {
        set_status("");
        fmt(buf, sizeof(buf), S(MSG_PR_ERROR), (long)(-n - 1));
        message(buf);
    } else {
        fmt(buf, sizeof(buf), S(n == 1 ? MSG_PR_PRINTED1 : MSG_PR_PRINTED), (long)n);
        set_status(buf);
    }
out:
    if (bp.opened) CloseDevice((struct IORequest *)bp.io);
    if (bp.io) DeleteIORequest((struct IORequest *)bp.io);
    if (bp.src.rowbuf) FreeVec(bp.src.rowbuf);
    if (bp.port) DeleteMsgPort(bp.port);
    DoMethod(mw.html, HTMLM_PrintEnd);
    CloseLibrary(FuelGaugeBase);
    FuelGaugeBase = NULL;
}

/**
 * @brief HTMLEX_ProgressHook: called by HTMLM_Export before each page, on
 *        our task but on the gadget's stack: only the progress window is
 *        served (see progress_input()).
 * @param h The hook (h_Data: struct Progress).
 * @param o The gadget.
 * @param m The page about to be written.
 * @return Non-zero to stop the export.
 */
static ULONG export_progress(struct Hook *h __asm("a0"), Object *o __asm("a2"),
                             struct HTMLExportProgress *m __asm("a1"))
{
    struct Progress *p = h->h_Data;
    LONG last = p->last > 0 && p->last < m->Pages ? p->last : m->Pages;
    char buf[100];

    (void)o;
    if (p->win && last - p->first + 1 != p->max && last >= p->first) {
        p->max = last - p->first + 1;
        SetGadgetAttrs((struct Gadget *)p->gauge, p->win, NULL, FUELGAUGE_Max, (ULONG)p->max, TAG_DONE);
    }
    fmt(buf, sizeof(buf), S(MSG_PR_WRITING), (long)m->Page, (long)last);
    progress_set(p, buf, m->Page - p->first);
    progress_input(p, FALSE);
    return (ULONG)p->stopped;
}

/**
 * @brief Write the page as PostScript or PDF.
 * @param s The options.
 * @param file The output file (PDF, PostScript to a file).
 * @param job The page range.
 */
static void export_ps_pdf(const PrintSettings *s, const char *file, const PrintJob *job)
{
    const char *name;
    LONG n, pages = 0;
    BPTR fh;
    char buf[PATHMAX + 100];
    struct Hook hook;
    struct Progress pr;
    BOOL tofile = s->prmode == PRMODE_PDF || s->prdest == PRDEST_FILE;
    struct TagItem tags[] = {
        { HTMLEX_File, 0 }, { HTMLEX_Format, 0 }, { HTMLEX_PaperWidth, 0 }, { HTMLEX_PaperHeight, 0 },
        { HTMLEX_MarginLeft, 0 }, { HTMLEX_MarginTop, 0 }, { HTMLEX_MarginRight, 0 }, { HTMLEX_MarginBottom, 0 },
        { HTMLEX_FontSize, 0 }, { HTMLEX_Serif, 0 }, { HTMLEX_Backgrounds, 0 }, { HTMLEX_Footer, 0 },
        { HTMLEX_FirstPage, 0 }, { HTMLEX_LastPage, 0 }, { HTMLEX_ProgressHook, 0 }, { HTMLEX_Pages, 0 },
        { HTMLEX_PSLevel, 2 }, { HTMLEX_ImageDPI, 0 }, { TAG_DONE, 0 }
    };

    if (tofile) {
        name = file;
        if (!name[0]) return;
        if (!confirm_overwrite(name)) return;
    } else if (s->prdest == PRDEST_PRT) name = "PRT:";
    else if (s->prdest == PRDEST_PS) {
        if (!have_ps()) {
            message(S(MSG_PR_NOPS));
            return;
        }
        name = "PS:";
    } else name = s->prdevice[0] ? s->prdevice : "PAR:";

    if (!(fh = Open((STRPTR)name, MODE_NEWFILE))) {
        dos_error(S(MSG_PR_CANNOT_OPEN), name);
        return;
    }
    memset(&hook, 0, sizeof(hook));
    hook.h_Entry = (ULONG (*)())export_progress;
    hook.h_Data = &pr;
    tags[0].ti_Data = (ULONG)fh;
    tags[1].ti_Data = (ULONG)(s->prmode == PRMODE_PDF ? HTMLEXF_PDF : HTMLEXF_PS);
    tags[2].ti_Data = (ULONG)paper_sizes[s->paper][0];
    tags[3].ti_Data = (ULONG)paper_sizes[s->paper][1];
    tags[4].ti_Data = (ULONG)mm_pt(s->margins[0]);
    tags[5].ti_Data = (ULONG)mm_pt(s->margins[1]);
    tags[6].ti_Data = (ULONG)mm_pt(s->margins[2]);
    tags[7].ti_Data = (ULONG)mm_pt(s->margins[3]);
    tags[8].ti_Data = (ULONG)(s->prsize * 10);
    tags[9].ti_Data = (ULONG)s->prserif;
    tags[10].ti_Data = (ULONG)s->prbackgrounds;
    tags[11].ti_Data = s->prpagenumbers ? (ULONG)S(MSG_PR_FOOTER) : 0;
    tags[12].ti_Data = (ULONG)(job->first > 0 ? job->first : 1);
    tags[13].ti_Data = (ULONG)job->last;
    tags[14].ti_Data = (ULONG)&hook;
    tags[15].ti_Data = (ULONG)&pages;
    tags[16].ti_Data = (ULONG)(s->pslevel == 1 ? 1 : 2);
    tags[17].ti_Data = (ULONG)s->primagedpi;
    /* the progress window, without fuelgauge.gadget only the status line */
    fmt(buf, sizeof(buf), S(MSG_PR_WRITING), 999L, 999L);
    if (!FuelGaugeBase) FuelGaugeBase = OpenLibrary((STRPTR)"gadgets/fuelgauge.gadget", 44);
    if (FuelGaugeBase) progress_open(&pr, buf, 1);
    else {
        memset(&pr, 0, sizeof(pr));
        busy(TRUE);
    }
    pr.first = job->first > 0 ? job->first : 1;
    pr.last = job->last;
    n = (LONG)DoMethod(mw.html, HTMLM_Export, (ULONG)tags);
    if (FuelGaugeBase) {
        progress_close(&pr);
        CloseLibrary(FuelGaugeBase);
        FuelGaugeBase = NULL;
    } else busy(FALSE);
    if (n == -2) {                  /* stopped: no half file */
        Close(fh);
        if (tofile) DeleteFile((STRPTR)name);
        set_status(S(MSG_PR_STOPPED));
        return;
    }
    if (n < 0) {
        LONG err = IoErr();
        Close(fh);
        SetIoErr(err);
        dos_error(S(MSG_PR_WRITE_FAILED), name);
        set_status("");
        return;
    }
    if (!Close(fh)) {
        dos_error(S(MSG_PR_WRITE_FAILED), name);
        return;
    }
    fmt(buf, sizeof(buf), S(n == 1 ? MSG_PR_WRITTEN1 : MSG_PR_WRITTEN), (long)n, name);
    set_status(buf);
}

void print_page(struct Screen *screen, Object *winobj, Object *html, const char *page)
{
    static PrintJob job = { 1, 0, 1 };
    static char file[PATHMAX], forpage[PATHMAX] = "\1";
    PrintSettings n = set;
    char *dot;

    mw.screen = screen;
    mw.winobj = winobj;
    mw.html = html;
    mw.win = NULL;
    GetAttr(WINDOW_Window, winobj, (ULONG *)&mw.win);
    if (!gadget_can_print()) {
        message(S(MSG_PR_NEEDGADGET));
        return;
    }
    if (page == NULL) page = "";
    /* another page: its name in the drawer of the last output file (at
     * first RAM:, help drawers are often write-protected), from page 1 */
    if (strcmp(forpage, page)) {
        strncpy(forpage, page, sizeof(forpage) - 1);
        forpage[sizeof(forpage) - 1] = 0;
        if (file[0]) *PathPart((STRPTR)file) = 0;
        else strcpy(file, "RAM:");
        AddPart((STRPTR)file, page[0] ? FilePart((STRPTR)page) : (STRPTR)"HelpViewer", sizeof(file));
        if ((dot = strrchr((char *)FilePart((STRPTR)file), '#'))) *dot = 0;
        if ((dot = strrchr((char *)FilePart((STRPTR)file), '.'))) *dot = 0;
        job.first = 1;
        job.last = 0;
    }
    if (!print_dialog(&n, file, sizeof(file), &job)) return;
    if (!same_options(&n, &set)) {
        set = n;
        save_options();
    }
    if (set.prmode == PRMODE_PRINTER) print_bitmap(&set, &job);
    else export_ps_pdf(&set, file, &job);
}
