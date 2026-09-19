#ifndef TINYDEV_LSP_SERVER_H
#define TINYDEV_LSP_SERVER_H

#include <exec/types.h>
#include <exec/ports.h>
#include <libraries/commodities.h>
#include <rexx/storage.h>
#include <rexx/rxslib.h>

#include "lsp_json.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LSP_BROKER_NAME        "TinyDev-LSP"
#define LSP_BROKER_TITLE       "TinyDev LSP Broker"
#define LSP_BROKER_DESCR       "ARexx Language Server Protocol Broker"
#define LSP_AREXX_PORT_NAME    "TINYDEV_LSP"

#define LSP_RC_OK              0
#define LSP_RC_WARN            5
#define LSP_RC_ERROR           10

#define LSP_MAX_SEARCH_PATHS   8

typedef struct ParserEntry {
    char *extension;
    char *executable_path;
    struct ParserEntry *next;
} ParserEntry;

typedef struct CachedFileResult {
    char *filepath;
    char *symbols_rexx;
    char *diagnostics_rexx;
    struct CachedFileResult *next;
} CachedFileResult;

typedef struct LspServer {
    struct Library *CommoditiesBase;
    struct RxsLib *RexxSysBase;
    struct Library *DOSBase;
    struct Library *IntuitionBase;

    struct MsgPort *broker_port;
    CxObj *broker;
    ULONG broker_sigbit;

    struct MsgPort *rexx_port;
    ULONG rexx_sigbit;

    ParserEntry *parsers;
    CachedFileResult *cache;

    char *search_paths[LSP_MAX_SEARCH_PATHS];
    size_t search_path_count;

    int running;
    int enabled;
    int request_count;
} LspServer;

extern LspServer g_lsp_server;

/* Library and broker lifecycle (lsp_broker.c) */
int lsp_broker_init(LspServer *srv);
void lsp_broker_cleanup(LspServer *srv);
void lsp_broker_handle_msg(LspServer *srv);

/* ARexx host lifecycle and command dispatch (lsp_rexx.c) */
int lsp_rexx_init(LspServer *srv);
void lsp_rexx_cleanup(LspServer *srv);
void lsp_rexx_handle_msg(LspServer *srv);

/* Parser runner, registry, and search paths (lsp_parser_runner.c) */
int lsp_parser_register(LspServer *srv, const char *ext, const char *exec_path);
const char *lsp_parser_find(LspServer *srv, const char *filepath);
void lsp_parser_registry_cleanup(LspServer *srv);

int lsp_server_add_search_path(LspServer *srv, const char *path);
void lsp_server_clear_search_paths(LspServer *srv);
int app_get_program_directory(int argc, char **argv, char *out_dir, size_t out_size);

int lsp_parser_run_request(LspServer *srv, const char *filepath,
                           const char *method, const char *params_json,
                           LspJsonWriter *out_result, char **out_error_msg);

void lsp_cache_clear(LspServer *srv);
void lsp_cache_put(LspServer *srv, const char *filepath, const char *symbols, const char *diags);
const CachedFileResult *lsp_cache_get(LspServer *srv, const char *filepath);

#ifdef __cplusplus
}
#endif

#endif /* TINYDEV_LSP_SERVER_H */
