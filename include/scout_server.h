#ifndef TINYDEV_SCOUT_SERVER_H
#define TINYDEV_SCOUT_SERVER_H

#include <exec/types.h>
#include <exec/ports.h>
#include <libraries/commodities.h>
#include <rexx/storage.h>
#include <rexx/rxslib.h>

#include "scout_json.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SCOUT_BROKER_NAME        "TinyDev-Scout"
#define SCOUT_BROKER_TITLE       "TinyDev Scout Broker"
#define SCOUT_BROKER_DESCR       "Source code analysis via ARexx"
#define SCOUT_AREXX_PORT_NAME    "TINYDEV_SCOUT"

#define SCOUT_RC_OK              0
#define SCOUT_RC_WARN            5
#define SCOUT_RC_ERROR           10

#define SCOUT_MAX_SEARCH_PATHS   8

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

typedef struct ScoutServer {
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

    char *search_paths[SCOUT_MAX_SEARCH_PATHS];
    size_t search_path_count;

    int running;
    int enabled;
    int request_count;
} ScoutServer;

extern ScoutServer g_scout_server;

/* Library and broker lifecycle (scout_broker.c) */
int scout_broker_init(ScoutServer *srv);
void scout_broker_cleanup(ScoutServer *srv);
void scout_broker_handle_msg(ScoutServer *srv);

/* ARexx host lifecycle and command dispatch (scout_rexx.c) */
int scout_rexx_init(ScoutServer *srv);
void scout_rexx_cleanup(ScoutServer *srv);
void scout_rexx_handle_msg(ScoutServer *srv);

/* Parser runner, registry, and search paths (scout_parser_runner.c) */
int scout_parser_register(ScoutServer *srv, const char *ext, const char *exec_path);
const char *scout_parser_find(ScoutServer *srv, const char *filepath);
void scout_parser_registry_cleanup(ScoutServer *srv);

int scout_server_add_search_path(ScoutServer *srv, const char *path);
void scout_server_clear_search_paths(ScoutServer *srv);
int app_get_program_directory(int argc, char **argv, char *out_dir, size_t out_size);

int scout_parser_run_request(ScoutServer *srv, const char *filepath,
                           const char *method, const char *params_json,
                           ScoutJsonWriter *out_result, char **out_error_msg);

void scout_cache_clear(ScoutServer *srv);
void scout_cache_put(ScoutServer *srv, const char *filepath, const char *symbols, const char *diags);
const CachedFileResult *scout_cache_get(ScoutServer *srv, const char *filepath);

#ifdef __cplusplus
}
#endif

#endif /* TINYDEV_SCOUT_SERVER_H */
