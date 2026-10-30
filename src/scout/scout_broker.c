/**
 * @file scout_broker.c
 * @brief Scout's commodity: the broker, its hotkey (CX_POPKEY) and its
 *        messages from Exchange (Show and Hide work the window).
 */
#include "scout_server.h"

#include <proto/exec.h>
#include <proto/commodities.h>
#include <clib/alib_protos.h>

/** CxMsgID() of the hotkey. */
#define POPKEY_ID 1

int scout_broker_init(ScoutServer *srv)
{
    struct NewBroker nb;
    LONG error = 0;

    srv->broker_port = CreateMsgPort();
    if (!srv->broker_port) return 0;

    nb.nb_Version = NB_VERSION;
    nb.nb_Name = (STRPTR)SCOUT_BROKER_NAME;
    nb.nb_Title = (STRPTR)SCOUT_BROKER_TITLE;
    nb.nb_Descr = (STRPTR)SCOUT_BROKER_DESCR;
    nb.nb_Unique = NBU_UNIQUE | NBU_NOTIFY;
    nb.nb_Flags = COF_SHOW_HIDE;    /* Exchange's Show and Hide work the window */
    nb.nb_Pri = (BYTE)srv->priority;
    nb.nb_Port = srv->broker_port;
    nb.nb_ReservedChannel = 0;

    srv->broker = CxBroker(&nb, &error);
    if (!srv->broker) {
        DeleteMsgPort(srv->broker_port);
        srv->broker_port = NULL;
        /* Scout runs already: it got CXCMD_UNIQUE and shows its window */
        return error == CBERR_DUP ? -1 : 0;
    }
    if (srv->popkey[0]) {
        /* a bad description only means no hotkey */
        CxObj *key = HotKey((STRPTR)srv->popkey, srv->broker_port, POPKEY_ID);
        if (key) AttachCxObj(srv->broker, key);
        ClearCxObjError(srv->broker);
    }
    ActivateCxObj(srv->broker, TRUE);
    srv->enabled = 1;
    return 1;
}

void scout_broker_handle(ScoutServer *srv)
{
    struct Message *msg;
    if (!srv->broker_port) return;
    while ((msg = GetMsg(srv->broker_port)) != NULL) {
        ULONG type = CxMsgType((CxMsg *)msg);
        LONG id = CxMsgID((CxMsg *)msg);
        ReplyMsg(msg);
        if (type == CXM_IEVENT && id == POPKEY_ID) scout_window_show(srv);
        if (type != CXM_COMMAND) continue;
        switch (id) {
        case CXCMD_DISABLE:
            srv->enabled = 0;
            ActivateCxObj(srv->broker, FALSE);
            break;
        case CXCMD_ENABLE:
            srv->enabled = 1;
            ActivateCxObj(srv->broker, TRUE);
            break;
        case CXCMD_KILL:
            srv->running = 0;
            break;
        case CXCMD_APPEAR:
        case CXCMD_UNIQUE:      /* Scout was started again */
            scout_window_show(srv);
            break;
        case CXCMD_DISAPPEAR:
            scout_window_hide(srv);
            break;
        default:
            break;
        }
    }
}

void scout_broker_cleanup(ScoutServer *srv)
{
    struct Message *msg;
    if (srv->broker) {
        DeleteCxObjAll(srv->broker);
        srv->broker = NULL;
    }
    if (srv->broker_port) {
        while ((msg = GetMsg(srv->broker_port)) != NULL) ReplyMsg(msg);
        DeleteMsgPort(srv->broker_port);
        srv->broker_port = NULL;
    }
}
