/**
 * @file scout_server.h
 * @brief Shared declarations of TinyDev-Scout (broker, ARexx port, parser runner).
 */
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

/** @name Names of the commodity and its ARexx port */
/** @{ */
#define SCOUT_BROKER_NAME        "TinyDev-Scout"    /**< name of the commodity (unique) */
#define SCOUT_BROKER_TITLE       "TinyDev Scout Broker" /**< its title in Exchange */
#define SCOUT_BROKER_DESCR       "Source code analysis via ARexx" /**< its description in Exchange */
#define SCOUT_AREXX_PORT_NAME    "TINYDEV_SCOUT"    /**< name of the public ARexx port */
/** @} */

/** @name ARexx return codes */
/** @{ */
#define SCOUT_RC_OK              0      /**< command done */
#define SCOUT_RC_WARN            5      /**< done with a warning */
#define SCOUT_RC_ERROR           10     /**< command failed */
/** @} */

/** Number of directories searched for parsers. */
#define SCOUT_MAX_SEARCH_PATHS   8

/** A parser registered for a file extension (a node of a singly linked list). */
typedef struct ParserEntry {
    char *extension;            /**< the extension with its dot, e.g. ".c" (allocated) */
    char *executable_path;      /**< the parser program (allocated) */
    struct ParserEntry *next;   /**< the next entry, NULL at the end */
} ParserEntry;

/** The last results of a file, as ARexx result strings (a node of a singly linked list). */
typedef struct CachedFileResult {
    char *filepath;             /**< the file (allocated) */
    char *symbols_rexx;         /**< its symbols, one "kind line col name" per line (allocated, may be NULL) */
    char *diagnostics_rexx;     /**< its diagnostics, one "severity line col message" per line (allocated, may be NULL) */
    struct CachedFileResult *next;  /**< the next entry, NULL at the end */
} CachedFileResult;

/** The state of the Scout commodity. */
typedef struct ScoutServer {
    struct Library *CommoditiesBase;    /**< commodities.library */
    struct RxsLib *RexxSysBase;         /**< rexxsyslib.library */
    struct Library *DOSBase;            /**< dos.library */
    struct Library *IntuitionBase;      /**< intuition.library */

    struct MsgPort *broker_port;        /**< port of the broker's messages */
    CxObj *broker;                      /**< the commodity's broker */
    ULONG broker_sigbit;                /**< signal mask of broker_port */

    struct MsgPort *rexx_port;          /**< the public ARexx port */
    ULONG rexx_sigbit;                  /**< signal mask of rexx_port */

    ParserEntry *parsers;               /**< the registered parsers */
    CachedFileResult *cache;            /**< the cached results */

    char *search_paths[SCOUT_MAX_SEARCH_PATHS]; /**< directories searched for parsers (allocated) */
    size_t search_path_count;           /**< number of them */

    int running;                        /**< the main loop runs (cleared by CXCMD_KILL or QUIT) */
    int enabled;                        /**< the commodity is enabled */
    int request_count;                  /**< number of requests sent to parsers (their IDs) */
} ScoutServer;

/** The one Scout server of the program. */
extern ScoutServer g_scout_server;

/**
 * @brief Open commodities.library and create the unique broker.
 * @param srv The server.
 * @return 1 on success, 0 if it failed (or Scout already runs).
 */
int scout_broker_init(ScoutServer *srv);
/**
 * @brief Delete the broker and close commodities.library.
 * @param srv The server.
 */
void scout_broker_cleanup(ScoutServer *srv);
/**
 * @brief Handle the broker's messages (enable, disable, kill).
 * @param srv The server.
 */
void scout_broker_handle_msg(ScoutServer *srv);

/**
 * @brief Open rexxsyslib.library and add the public ARexx port.
 * @param srv The server.
 * @return 1 on success, 0 if it failed (or the port exists already).
 */
int scout_rexx_init(ScoutServer *srv);
/**
 * @brief Answer waiting messages, remove the ARexx port and close
 *        rexxsyslib.library.
 * @param srv The server.
 */
void scout_rexx_cleanup(ScoutServer *srv);
/**
 * @brief Carry out the ARexx commands that arrived (PARSE, SYMBOLS,
 *        DIAGNOSTICS, DEFINITION, COMPLETE, REGISTER_PARSER, STATUS, QUIT).
 * @param srv The server.
 */
void scout_rexx_handle_msg(ScoutServer *srv);

/**
 * @brief Register (or replace) the parser of a file extension.
 * @param srv The server.
 * @param ext The extension with its dot, e.g. ".c".
 * @param exec_path The parser program.
 * @return 1 on success, 0 without memory.
 */
int scout_parser_register(ScoutServer *srv, const char *ext, const char *exec_path);
/**
 * @brief Find the parser of a file: a registered one, otherwise for C
 *        sources c_parser in the search paths, the current directory or
 *        the path.
 * @param srv The server.
 * @param filepath The file.
 * @return The parser program (owned by the server, or a static buffer
 *         until the next call), or NULL if there is none.
 */
const char *scout_parser_find(ScoutServer *srv, const char *filepath);
/**
 * @brief Free the registered parsers.
 * @param srv The server.
 */
void scout_parser_registry_cleanup(ScoutServer *srv);

/**
 * @brief Add a directory searched for parsers (once).
 * @param srv The server.
 * @param path The directory.
 * @return 1 on success (or if it is there already), 0 if the list is full
 *         or memory ran out.
 */
int scout_server_add_search_path(ScoutServer *srv, const char *path);
/**
 * @brief Remove all search paths.
 * @param srv The server.
 */
void scout_server_clear_search_paths(ScoutServer *srv);
/**
 * @brief Resolves the directory containing the running executable.
 *
 * Supports both CLI launches (via GetProgramDir() / GetProgramName() / argv[0])
 * and Workbench launches (via WBStartup argument lock).
 *
 * @param argc CLI argument count (0 if started from Workbench).
 * @param argv CLI argument array or pointer to struct WBStartup.
 * @param out_dir Buffer to receive the directory path.
 * @param out_size Size of out_dir buffer in bytes.
 * @return 1 on success, 0 on failure.
 */
int app_get_program_directory(int argc, char **argv, char *out_dir, size_t out_size);

/**
 * @brief Run the parser of a file with one JSON-RPC request and read its
 *        response (the parser runs as a separate program; request and
 *        response go through STDIN/STDOUT).
 * @param srv The server.
 * @param filepath The file (selects the parser).
 * @param method The method.
 * @param params_json Its parameters as JSON text (may be NULL).
 * @param[out] out_result Receives the response's result as JSON text.
 * @param[out] out_error_msg Set to an allocated error message on failure
 *             (free() it; may be NULL).
 * @return 1 on success, 0 on failure.
 */
int scout_parser_run_request(ScoutServer *srv, const char *filepath,
                           const char *method, const char *params_json,
                           ScoutJsonWriter *out_result, char **out_error_msg);

/**
 * @brief Free all cached results.
 * @param srv The server.
 */
void scout_cache_clear(ScoutServer *srv);
/**
 * @brief Keep the results of a file (replacing what was kept).
 * @param srv The server.
 * @param filepath The file.
 * @param symbols Its symbols as ARexx result string (NULL keeps the old ones).
 * @param diags Its diagnostics as ARexx result string (NULL keeps the old ones).
 */
void scout_cache_put(ScoutServer *srv, const char *filepath, const char *symbols, const char *diags);
/**
 * @brief Return the kept results of a file.
 * @param srv The server.
 * @param filepath The file.
 * @return The results (owned by the server), or NULL.
 */
const CachedFileResult *scout_cache_get(ScoutServer *srv, const char *filepath);

#ifdef __cplusplus
}
#endif

#endif /* TINYDEV_SCOUT_SERVER_H */
