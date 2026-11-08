/*
 * {$NAME}.c - {$PURPOSE}.
 *
 * Copyright (c) {$YEAR} {$AUTHOR}
 * {$AUTHORMAIL}
 * Created {$DATE}
 */
#include <stddef.h>
#include <exec/types.h>
#include <dos/dos.h>
#include <intuition/intuition.h>
#include <libraries/gadtools.h>
#include <libraries/locale.h>
#include <reaction/reaction.h>
#include <reaction/reaction_macros.h>
#include <classes/window.h>
#include <gadgets/button.h>
#include <gadgets/layout.h>
#include <gadgets/string.h>
#include <images/label.h>
#include <clib/alib_protos.h>
#include <proto/button.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/label.h>
#include <proto/layout.h>
#include <proto/locale.h>
#include <proto/string.h>
#include <proto/window.h>

/* Made from Catalogs/{$NAME}.cd by CatComp: the numbers of the texts and,
 * with CATCOMP_ARRAY, the built-in English ones. */
#define CATCOMP_ARRAY
#include "{$NAME}_strings.h"

#define PROGRAM_NAME "{$PROJECT}"
#define NAME_LENGTH  64

/* The stack the C startup code gives the program. */
unsigned long __stack = 32768;

static const char version[] = "$VER: {$NAME} 1.0";

/* The commands of the gadgets (GA_ID) and of the menu (user data). */
enum {
    CMD_NAME = 1,
    CMD_GREET,
    CMD_STATUS,
    CMD_ABOUT,
    CMD_QUIT
};

struct IntuitionBase *IntuitionBase = NULL;
struct LocaleBase *LocaleBase = NULL;
struct Library *WindowBase = NULL;
struct Library *LayoutBase = NULL;
struct Library *ButtonBase = NULL;
struct Library *StringBase = NULL;
struct Library *LabelBase = NULL;

/* window.class keeps a pointer to this and uses it at every opening. The
 * labels hold the numbers of their texts until localize_menu() has run. */
static struct NewMenu menu[] = {
    { NM_TITLE, (STRPTR)MSG_MENU_PROJECT, NULL, 0, 0, NULL },
    { NM_ITEM,  (STRPTR)MSG_MENU_ABOUT, (STRPTR)"?", 0, 0, (APTR)CMD_ABOUT },
    { NM_ITEM,  NM_BARLABEL, NULL, 0, 0, NULL },
    { NM_ITEM,  (STRPTR)MSG_MENU_QUIT, (STRPTR)"Q", 0, 0, (APTR)CMD_QUIT },
    { NM_END,   NULL,        NULL, 0, 0, NULL }
};

static Object *window_object = NULL;
static Object *layout_object = NULL;
static Object *name_gadget = NULL;
static Object *status_gadget = NULL;
static struct Window *window = NULL;
static struct Catalog *catalog = NULL;
static struct MsgPort *app_port = NULL;
static char status_text[NAME_LENGTH + 96];

/* The text with a number: from the catalog, otherwise the built-in one. */
static const char *S(LONG id)
{
    const char *text = "";
    size_t i;

    for (i = 0; i < sizeof(CatCompArray) / sizeof(CatCompArray[0]); i++) {
        if (CatCompArray[i].cca_ID == id) {
            text = (const char *)CatCompArray[i].cca_Str;
            break;
        }
    }
    /* GetCatalogStr() accepts a NULL catalog. */
    if (LocaleBase != NULL) text = (const char *)GetCatalogStr(catalog, id, (STRPTR)text);
    return text;
}

/* Replace the numbers in the menu's labels by their texts. */
static void localize_menu(void)
{
    struct NewMenu *m;

    for (m = menu; m->nm_Type != NM_END; m++) {
        if (m->nm_Label != NM_BARLABEL) m->nm_Label = (STRPTR)S((LONG)m->nm_Label);
    }
}

/* Copy a text, with name in place of its %s, into a buffer of a size. */
static void format_name(char *buffer, size_t size, const char *format, const char *name)
{
    size_t n = 0;

    while (*format != '\0' && n + 1 < size) {
        if (format[0] == '%' && format[1] == 's') {
            const char *p = name;
            while (*p != '\0' && n + 1 < size) buffer[n++] = *p++;
            format += 2;
        } else {
            buffer[n++] = *format++;
        }
    }
    buffer[n] = '\0';
}

/* Show a message with one text in it (%s); window may be NULL. */
static void show_message(const char *format, const char *text)
{
    struct EasyStruct es;
    ULONG args[1];

    es.es_StructSize = sizeof(es);
    es.es_Flags = 0;
    es.es_Title = (UBYTE *)PROGRAM_NAME;
    es.es_TextFormat = (UBYTE *)format;
    es.es_GadgetFormat = (UBYTE *)"OK";
    args[0] = (ULONG)text;
    EasyRequestArgs(window, &es, NULL, args);
}

/* Open the libraries and classes; say which one is missing if one is. */
static int open_libraries(void)
{
    static struct {
        struct Library **base;
        const char *name;
    } classes[] = {
        { &WindowBase, "window.class" },
        { &LayoutBase, "gadgets/layout.gadget" },
        { &ButtonBase, "gadgets/button.gadget" },
        { &StringBase, "gadgets/string.gadget" },
        { &LabelBase,  "images/label.image" }
    };
    int i;

    /* Without locale.library or a catalog the texts stay English. */
    LocaleBase = (struct LocaleBase *)OpenLibrary((STRPTR)"locale.library", 38);
    if (LocaleBase != NULL) {
        catalog = OpenCatalog(NULL, (STRPTR)"{$NAME}.catalog",
                              OC_BuiltInLanguage, (ULONG)"english", TAG_DONE);
    }
    localize_menu();

    IntuitionBase = (struct IntuitionBase *)OpenLibrary((STRPTR)"intuition.library", 47);
    if (IntuitionBase == NULL) return 0;
    for (i = 0; i < (int)(sizeof(classes) / sizeof(classes[0])); i++) {
        *classes[i].base = OpenLibrary((STRPTR)classes[i].name, 47);
        if (*classes[i].base == NULL) {
            show_message(S(MSG_ERR_CLASS), classes[i].name);
            return 0;
        }
    }
    return 1;
}

/* Close what open_libraries() opened. */
static void close_libraries(void)
{
    /* CloseLibrary() accepts NULL. */
    CloseLibrary(LabelBase);
    CloseLibrary(StringBase);
    CloseLibrary(ButtonBase);
    CloseLibrary(LayoutBase);
    CloseLibrary(WindowBase);
    CloseLibrary((struct Library *)IntuitionBase);
    if (LocaleBase != NULL) CloseCatalog(catalog);
    CloseLibrary((struct Library *)LocaleBase);
}

/* Build the window object with its gadgets; returns 0 on failure. */
static int create_window(void)
{
    if ((app_port = CreateMsgPort()) == NULL) return 0;

    window_object = NewObject(WINDOW_GetClass(), NULL,
        WA_Title, (ULONG)PROGRAM_NAME,
        WA_Activate, TRUE,
        WA_DepthGadget, TRUE,
        WA_DragBar, TRUE,
        WA_CloseGadget, TRUE,
        WA_SizeGadget, TRUE,
        WINDOW_IconifyGadget, TRUE,
        WINDOW_IconTitle, (ULONG)PROGRAM_NAME,
        WINDOW_AppPort, (ULONG)app_port,
        WINDOW_NewMenu, (ULONG)menu,
        WINDOW_Position, WPOS_CENTERSCREEN,
        WINDOW_Layout, (ULONG)(layout_object = NewObject(LAYOUT_GetClass(), NULL,
            LAYOUT_Orientation, LAYOUT_ORIENT_VERT,
            LAYOUT_SpaceOuter, TRUE,
            LAYOUT_DeferLayout, TRUE,

            LAYOUT_AddChild, (ULONG)(name_gadget = NewObject(STRING_GetClass(), NULL,
                GA_ID, CMD_NAME,
                GA_RelVerify, TRUE,
                GA_TabCycle, TRUE,
                STRINGA_MaxChars, NAME_LENGTH,
                STRINGA_TextVal, (ULONG)"World",
            TAG_END)),
            CHILD_Label, (ULONG)NewObject(LABEL_GetClass(), NULL,
                LABEL_Text, (ULONG)S(MSG_NAME_GAD),
            TAG_END),
            CHILD_WeightedHeight, 0,

            LAYOUT_AddChild, (ULONG)NewObject(BUTTON_GetClass(), NULL,
                GA_ID, CMD_GREET,
                GA_RelVerify, TRUE,
                GA_Text, (ULONG)S(MSG_GREET_GAD),
            TAG_END),
            CHILD_WeightedHeight, 0,

            /* A read-only button as the status line. GA_Underscore goes
             * before GA_Text: the class looks for the shortcut at once. */
            LAYOUT_AddChild, (ULONG)(status_gadget = NewObject(BUTTON_GetClass(), NULL,
                GA_ID, CMD_STATUS,
                GA_ReadOnly, TRUE,
                GA_Underscore, 0,
                GA_Text, (ULONG)"",
            TAG_END)),
            CHILD_WeightedHeight, 0,
        TAG_END)),
    TAG_END);

    return window_object != NULL;
}

/* Open the window and put the cursor into the string gadget. */
static int open_window(void)
{
    if ((window = RA_OpenWindow(window_object)) == NULL) return 0;
    ActivateLayoutGadget((struct Gadget *)layout_object, window, NULL,
                         (ULONG)name_gadget);
    return 1;
}

/* Show the greeting for the name in the string gadget. */
static void greet(void)
{
    STRPTR name = NULL;

    GetAttr(STRINGA_TextVal, name_gadget, (ULONG *)&name);
    format_name(status_text, sizeof(status_text), S(MSG_GREETING),
                name != NULL ? (const char *)name : "");
    /* The gadget keeps the pointer; status_text stays. */
    SetGadgetAttrs((struct Gadget *)status_gadget, window, NULL,
                   GA_Text, (ULONG)status_text, TAG_DONE);
}

/* Carry out a command; returns 0 to end the program. */
static int do_command(ULONG command)
{
    switch (command) {
    case CMD_NAME:
    case CMD_GREET:
        greet();
        break;
    case CMD_ABOUT:
        show_message("%s", PROGRAM_NAME "\n{$PURPOSE}");
        break;
    case CMD_QUIT:
        return 0;
    default:
        break;
    }
    return 1;
}

/* Find the command of a menu item; 0 if there is none. */
static ULONG menu_command(ULONG result)
{
    struct Menu *strip = NULL;
    struct MenuItem *item;

    GetAttr(WINDOW_MenuStrip, window_object, (ULONG *)&strip);
    if (strip == NULL) return 0;
    item = ItemAddress(strip, (UWORD)(result & WMHI_MENUMASK));
    return item != NULL ? (ULONG)GTMENUITEM_USERDATA(item) : 0;
}

/* The event loop; returns when the program is to end. */
static void event_loop(void)
{
    int running = 1;

    while (running) {
        ULONG window_signal = 0, signals, result;
        WORD code;

        /* Read again every time: the signal changes with iconifying. */
        GetAttr(WINDOW_SigMask, window_object, &window_signal);
        signals = Wait(window_signal | SIGBREAKF_CTRL_C);
        if (signals & SIGBREAKF_CTRL_C) running = 0;

        while ((result = RA_HandleInput(window_object, &code)) != WMHI_LASTMSG) {
            switch (result & WMHI_CLASSMASK) {
            case WMHI_CLOSEWINDOW:
                running = 0;
                break;
            case WMHI_GADGETUP:
                if (!do_command(result & WMHI_GADGETMASK)) running = 0;
                break;
            case WMHI_MENUPICK:
                if (!do_command(menu_command(result))) running = 0;
                break;
            case WMHI_ICONIFY:
                if (RA_Iconify(window_object)) window = NULL;
                break;
            case WMHI_UNICONIFY:
                if (!open_window()) running = 0;
                break;
            default:
                break;
            }
        }
    }
}

int main(void)
{
    int rc = RETURN_FAIL;

    (void)version;
    if (open_libraries() && create_window() && open_window()) {
        event_loop();
        rc = RETURN_OK;
    }
    if (window_object != NULL) DisposeObject(window_object);
    if (app_port != NULL) DeleteMsgPort(app_port);
    close_libraries();
    return rc;
}
