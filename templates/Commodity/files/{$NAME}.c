/*
 * {$NAME} - {$PURPOSE}
 *
 * Copyright (c) {$YEAR} {$AUTHOR}
 * {$AUTHORMAIL}
 * Created {$DATE}
 */
#include <exec/types.h>
#include <dos/dos.h>
#include <libraries/commodities.h>
#include <clib/alib_protos.h>
#include <proto/commodities.h>
#include <proto/exec.h>
#include <proto/intuition.h>

#define BROKER_NAME  "{$NAME}"
#define BROKER_TITLE "{$PROJECT}"
#define BROKER_DESCR "{$PURPOSE}"
#define HOTKEY       "{$HOTKEY}"

/* CxMsgID() of the hotkey's messages. */
#define HOTKEY_ID 1

/* The stack the C startup code gives the program. */
unsigned long __stack = 16384;

static const char version[] = "$VER: {$NAME} 1.0";

struct IntuitionBase *IntuitionBase = NULL;
struct Library *CxBase = NULL;

static struct MsgPort *port = NULL;
static CxObj *broker = NULL;

/* Make the broker with its hotkey and activate it; returns 0 on failure,
 * also when the program runs already (it was told by NBU_NOTIFY). */
static int create_broker(void)
{
    struct NewBroker nb;
    CxObj *key;

    nb.nb_Version = NB_VERSION;
    nb.nb_Name = (STRPTR)BROKER_NAME;
    nb.nb_Title = (STRPTR)BROKER_TITLE;
    nb.nb_Descr = (STRPTR)BROKER_DESCR;
    nb.nb_Unique = NBU_UNIQUE | NBU_NOTIFY;
    nb.nb_Flags = 0;
    nb.nb_Pri = 0;
    nb.nb_Port = port;
    nb.nb_ReservedChannel = 0;

    if ((broker = CxBroker(&nb, NULL)) == NULL) return 0;
    if ((key = HotKey((STRPTR)HOTKEY, port, HOTKEY_ID)) == NULL) return 0;
    AttachCxObj(broker, key);
    if (CxObjError(broker) != 0) return 0;
    ActivateCxObj(broker, TRUE);
    return 1;
}

/* What the hotkey does. */
static void hotkey_pressed(void)
{
    DisplayBeep(NULL);
}

/* Answer the messages of the port; returns 0 to end the program. */
static int handle_messages(void)
{
    struct Message *msg;
    int running = 1;

    while ((msg = GetMsg(port)) != NULL) {
        ULONG type = CxMsgType((CxMsg *)msg);
        LONG id = CxMsgID((CxMsg *)msg);

        ReplyMsg(msg);
        if (type == CXM_IEVENT && id == HOTKEY_ID) {
            hotkey_pressed();
        } else if (type == CXM_COMMAND) {
            switch (id) {
            case CXCMD_DISABLE:
                ActivateCxObj(broker, FALSE);
                break;
            case CXCMD_ENABLE:
                ActivateCxObj(broker, TRUE);
                break;
            case CXCMD_KILL:
            case CXCMD_UNIQUE:      /* started a second time: end */
                running = 0;
                break;
            default:
                break;
            }
        }
    }
    return running;
}

int main(void)
{
    int rc = RETURN_FAIL;

    (void)version;
    IntuitionBase = (struct IntuitionBase *)OpenLibrary((STRPTR)"intuition.library", 37);
    CxBase = OpenLibrary((STRPTR)"commodities.library", 37);
    port = CreateMsgPort();

    if (IntuitionBase != NULL && CxBase != NULL && port != NULL && create_broker()) {
        ULONG port_signal = 1UL << port->mp_SigBit;
        int running = 1;

        while (running) {
            ULONG signals = Wait(port_signal | SIGBREAKF_CTRL_C);

            if (signals & SIGBREAKF_CTRL_C) running = 0;
            if ((signals & port_signal) && !handle_messages()) running = 0;
        }
        rc = RETURN_OK;
    }

    if (broker != NULL) DeleteCxObjAll(broker);
    if (port != NULL) {
        struct Message *msg;

        while ((msg = GetMsg(port)) != NULL) ReplyMsg(msg);
        DeleteMsgPort(port);
    }
    CloseLibrary(CxBase);
    CloseLibrary((struct Library *)IntuitionBase);
    return rc;
}
