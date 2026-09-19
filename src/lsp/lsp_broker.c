#include "lsp_server.h"

#include <proto/exec.h>
#include <proto/commodities.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <clib/alib_protos.h>
#include <stdio.h>

int lsp_broker_init(LspServer *srv)
{
    if (!srv) return 0;

    srv->CommoditiesBase = OpenLibrary("commodities.library", 37);
    if (!srv->CommoditiesBase) {
        fprintf(stderr, "Failed to open commodities.library v37+\n");
        return 0;
    }

    srv->broker_port = CreateMsgPort();
    if (!srv->broker_port) {
        fprintf(stderr, "Failed to create Commodity broker MsgPort\n");
        CloseLibrary(srv->CommoditiesBase);
        srv->CommoditiesBase = NULL;
        return 0;
    }
    srv->broker_sigbit = 1UL << srv->broker_port->mp_SigBit;

    struct NewBroker nb;
    nb.nb_Version = NB_VERSION;
    nb.nb_Name = (char *)LSP_BROKER_NAME;
    nb.nb_Title = (char *)LSP_BROKER_TITLE;
    nb.nb_Descr = (char *)LSP_BROKER_DESCR;
    nb.nb_Unique = NBU_UNIQUE | NBU_NOTIFY;
    nb.nb_Flags = 0;
    nb.nb_Pri = 0;
    nb.nb_Port = srv->broker_port;
    nb.nb_ReservedChannel = 0;

    LONG error = 0;
    srv->broker = CxBroker(&nb, &error);
    if (!srv->broker) {
        if (error == CBERR_DUP) {
            fprintf(stderr, "TinyDev-LSP commodity is already running.\n");
        } else {
            fprintf(stderr, "Failed to create CxBroker (error %ld)\n", (long)error);
        }
        DeleteMsgPort(srv->broker_port);
        srv->broker_port = NULL;
        CloseLibrary(srv->CommoditiesBase);
        srv->CommoditiesBase = NULL;
        return 0;
    }

    ActivateCxObj(srv->broker, TRUE);
    srv->enabled = 1;
    return 1;
}

void lsp_broker_handle_msg(LspServer *srv)
{
    if (!srv || !srv->broker_port) return;

    struct Message *msg;
    while ((msg = GetMsg(srv->broker_port)) != NULL) {
        LONG msg_id = CxMsgID((CxMsg *)msg);
        ULONG msg_type = CxMsgType((CxMsg *)msg);

        if (msg_type == CXM_COMMAND) {
            switch (msg_id) {
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
                    /* Informational notification or broker ping */
                    break;
                default:
                    break;
            }
        }
        ReplyMsg(msg);
    }
}

void lsp_broker_cleanup(LspServer *srv)
{
    if (!srv) return;

    if (srv->broker) {
        ActivateCxObj(srv->broker, FALSE);
        DeleteCxObjAll(srv->broker);
        srv->broker = NULL;
    }

    if (srv->broker_port) {
        struct Message *msg;
        while ((msg = GetMsg(srv->broker_port)) != NULL) {
            ReplyMsg(msg);
        }
        DeleteMsgPort(srv->broker_port);
        srv->broker_port = NULL;
    }

    if (srv->CommoditiesBase) {
        CloseLibrary(srv->CommoditiesBase);
        srv->CommoditiesBase = NULL;
    }
}
