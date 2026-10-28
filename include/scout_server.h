/**
 * @file scout_server.h
 * @brief Shared declarations of the Scout commodity: its state, the jobs
 *        that run parsers, and the modules (main loop, broker, ARexx port,
 *        jobs, project walk).
 *
 * What Scout knows (parsers, sessions, results) is the portable store
 * (scout_store.h); this header adds what runs on the Amiga. The design is
 * described in src/scout/docs/Scout.md.
 */
#ifndef TINYDEV_SCOUT_SERVER_H
#define TINYDEV_SCOUT_SERVER_H

#include <exec/types.h>
#include <exec/ports.h>
#include <dos/dos.h>
#include <devices/timer.h>
#include <libraries/commodities.h>
#include <rexx/storage.h>

#include "scout_store.h"

/** @name Names of the commodity and its ARexx port */
/** @{ */
#define SCOUT_BROKER_NAME        "TinyDev-Scout"    /**< name of the commodity (unique) */
#define SCOUT_BROKER_TITLE       "TinyDev Scout"    /**< its title in Exchange */
#define SCOUT_BROKER_DESCR       "Source code analysis via ARexx" /**< its description in Exchange */
#define SCOUT_AREXX_PORT_NAME    "TINYDEV_SCOUT"    /**< name of the public ARexx port */
#define SCOUT_VERSION            "1.0"              /**< Scout's version (STATUS) */
/** @} */

/** @name Error numbers (RC2 of a failed command) */
/** @{ */
#define SCOUT_E_ARGS        1   /**< bad arguments */
#define SCOUT_E_SESSION     2   /**< unknown session */
#define SCOUT_E_TOO_LARGE   3   /**< result too large for ARexx (use TO) */
#define SCOUT_E_NO_PARSER   4   /**< no parser for the file or language */
#define SCOUT_E_PARSER      5   /**< the parser failed (or Scout is disabled) */
#define SCOUT_E_FILE        6   /**< file not found or not readable */
#define SCOUT_E_SUPERSEDED  7   /**< superseded by a newer request */
#define SCOUT_E_MEMORY      8   /**< out of memory */
#define SCOUT_E_CAPABILITY  9   /**< the parser cannot do this */
/** @} */

/** Files analysed in one parser run while indexing. */
#define SCOUT_BATCH         8

/** Longest path Scout handles. */
#define SCOUT_PATH_MAX      512

/** Most files of a session (project files and included ones). */
#define SCOUT_MAX_FILES     4096

/** One file of a job. */
typedef struct ScoutItem {
    char *path;             /**< the file (allocated) */
    char *text;             /**< the client's snapshot (allocated, NULL: the file) */
    char *language;         /**< the language (allocated) */
    long version;           /**< the client's version, -1 for the file on disk */
    ScoutStamp stamp;       /**< the file on disk when the job was made */
    int in_project;         /**< belongs to the project (not only included) */
    struct RexxMsg *rx;     /**< the client's message waiting for it, NULL */
    int requery;            /**< carry out @p rx again when done (a query that needed the analysis) */
} ScoutItem;

/** What a job does. */
typedef enum ScoutJobType {
    SCOUT_JOB_DESCRIBE,     /**< scout/describe of a parser */
    SCOUT_JOB_ANALYZE       /**< scout/analyze of one or more files */
} ScoutJobType;

/** A parser run: requests written to a file, answers read from another. */
typedef struct ScoutJob {
    struct Message msg;     /**< sent back by the job's process when the parser has ended */
    struct ScoutJob *next;  /**< the next job in the queue */
    ScoutJobType type;      /**< what it does */
    int background;         /**< indexing (runs after the clients' jobs) */
    char *program;          /**< the parser (allocated) */
    long parser;            /**< its index in the store, -1 for a describe */
    char *extension;        /**< describe: the extension to take over (REGISTER; allocated, NULL) */
    struct RexxMsg *rx;     /**< describe: the message waiting (REGISTER), NULL */
    long session;           /**< analyze: the session */
    ScoutItem items[SCOUT_BATCH];   /**< analyze: the files */
    int count;              /**< number of files */
    char in_name[SCOUT_PATH_MAX];   /**< the requests' file */
    char out_name[SCOUT_PATH_MAX];  /**< the answers' file */
    BPTR in;                /**< the requests, the parser's input */
    BPTR out;               /**< the answers, the parser's output */
    LONG rc;                /**< the parser's return code */
    int timed_out;          /**< Ctrl-C was sent after the timeout */
    int scan;               /**< describe: part of the scan, still counted in ScoutServer::scanning */
    char command[SCOUT_PATH_MAX + 4];   /**< the command line run (the quoted parser) */
    char arg[16];           /**< the job's address in hex, the process's argument */
} ScoutJob;

/** A directory being read while walking a project. */
typedef struct ScoutWalkDir {
    BPTR lock;                      /**< the directory */
    struct FileInfoBlock *fib;      /**< its entry being read */
    char path[SCOUT_PATH_MAX];      /**< its path */
} ScoutWalkDir;

/** The walk through a session's project drawer. */
typedef struct ScoutWalk {
    long session;                   /**< the session */
    ScoutWalkDir dirs[16];          /**< the directories being read (at most 16 deep) */
    int depth;                      /**< number of them */
    long found;                     /**< files queued */
    struct ScoutWalk *next;         /**< the next walk */
} ScoutWalk;

/** A message waiting until the parsers are read. */
typedef struct ScoutDeferred {
    struct RexxMsg *rx;             /**< the message */
    int languages;                  /**< answer with LANGUAGES' result (RESCAN) */
    struct ScoutDeferred *next;     /**< the next one */
} ScoutDeferred;

/** The state of the Scout commodity. */
typedef struct ScoutServer {
    ScoutStore store;               /**< parsers, sessions, results */

    struct MsgPort *broker_port;    /**< port of the broker's messages */
    CxObj *broker;                  /**< the commodity's broker */
    struct MsgPort *rexx_port;      /**< the public ARexx port */
    struct MsgPort *job_port;       /**< where the jobs come back */
    struct MsgPort *timer_port;     /**< port of the timeout timer */
    struct timerequest *timer;      /**< the timeout timer */
    int timer_open;                 /**< timer.device is open */
    int timer_pending;              /**< the timer runs */

    ScoutJob *queue;                /**< jobs waiting */
    ScoutJob *job;                  /**< the job running, NULL */
    ScoutJob *abandoned;            /**< jobs whose parser hung (still running) */
    int scanning;                   /**< describe jobs of the parser drawer outstanding */
    ScoutDeferred *deferred;        /**< messages waiting for the scan */
    ScoutWalk *walks;               /**< project walks going on */

    char tempdir[SCOUT_PATH_MAX];   /**< drawer of the temporary files (TEMPDIR) */
    char parsers[SCOUT_PATH_MAX];   /**< drawer of the parsers (PARSERS) */
    long timeout;                   /**< seconds a parser may take per request (TIMEOUT) */
    long priority;                  /**< the commodity's priority (CX_PRIORITY) */
    unsigned long temp_count;       /**< counter of the temporary files */

    int running;                    /**< the main loop runs */
    int enabled;                    /**< the commodity is enabled */
} ScoutServer;

/** The one Scout server of the program. */
extern ScoutServer g_scout;

/**
 * @brief Create the commodity's broker.
 * @param srv The server.
 * @return 1 on success, 0 if it failed (or Scout runs already).
 */
int scout_broker_init(ScoutServer *srv);

/**
 * @brief Handle the broker's messages (enable, disable, kill).
 * @param srv The server.
 */
void scout_broker_handle(ScoutServer *srv);

/**
 * @brief Delete the broker.
 * @param srv The server.
 */
void scout_broker_cleanup(ScoutServer *srv);

/**
 * @brief Add the public ARexx port.
 * @param srv The server.
 * @return 1 on success, 0 if it failed (or the port exists already).
 */
int scout_rexx_init(ScoutServer *srv);

/**
 * @brief Carry out the commands that arrived at the ARexx port.
 * @param srv The server.
 */
void scout_rexx_handle(ScoutServer *srv);

/**
 * @brief Carry out one command (again, after the analysis it waited for,
 *        or after the parser scan).
 * @param srv The server.
 * @param rx The message.
 * @param languages Answer with LANGUAGES' result (a RESCAN waiting for its
 *                  scan).
 */
void scout_rexx_command(ScoutServer *srv, struct RexxMsg *rx, int languages);

/**
 * @brief Answer a message with success and a result.
 * @param rx The message.
 * @param result The result (only given if the caller asked for one).
 */
void scout_reply(struct RexxMsg *rx, const char *result);

/**
 * @brief Answer a message with failure (RC 10, RC2 the error number) and
 *        note the error for ERROR (in the session and in session 0).
 * @param srv The server.
 * @param rx The message.
 * @param session The session the command was for (-1: session 0).
 * @param code The error number (SCOUT_E_).
 * @param text The error's text.
 */
void scout_fail(ScoutServer *srv, struct RexxMsg *rx, long session, long code, const char *text);

/**
 * @brief Answer the waiting messages and remove the ARexx port.
 * @param srv The server.
 */
void scout_rexx_cleanup(ScoutServer *srv);

/**
 * @brief Open what the jobs need (port, timer).
 * @param srv The server.
 * @return 1 on success, 0 if it failed.
 */
int scout_jobs_init(ScoutServer *srv);

/**
 * @brief Read the parsers of the parser drawer again (describe jobs);
 *        commands that need them wait until all are answered.
 * @param srv The server.
 */
void scout_jobs_scan(ScoutServer *srv);

/**
 * @brief Queue a describe job for one parser (REGISTER).
 * @param srv The server.
 * @param program The parser.
 * @param extension The extension it is to take over.
 * @param rx The message to answer when done.
 * @return 1 if queued, 0 if out of memory.
 */
int scout_jobs_register(ScoutServer *srv, const char *program, const char *extension,
                        struct RexxMsg *rx);

/**
 * @brief Queue the analysis of a file.
 *
 * A client's analysis replaces one of the same file and session that has
 * not started yet (its message is answered with SCOUT_E_SUPERSEDED);
 * background analyses are batched per parser.
 *
 * @param srv The server.
 * @param session The session.
 * @param item The file (its strings are copied; @p rx is taken over).
 * @param background Indexing rather than a client's request.
 * @return 1 if queued, 0 if out of memory (the caller answers @p rx).
 */
int scout_jobs_analyze(ScoutServer *srv, long session, const ScoutItem *item, int background);

/**
 * @brief Drop the queued jobs of a session (CLOSE); waiting messages are
 *        answered with SCOUT_E_SESSION.
 * @param srv The server.
 * @param session The session.
 */
void scout_jobs_drop_session(ScoutServer *srv, long session);

/**
 * @brief Return the number of queued and running jobs.
 * @param srv The server.
 * @return The number.
 */
long scout_jobs_count(const ScoutServer *srv);

/**
 * @brief Start the next job if none runs.
 * @param srv The server.
 */
void scout_jobs_next(ScoutServer *srv);

/**
 * @brief Handle a job that came back and the timeout timer.
 * @param srv The server.
 * @param signals The signals received.
 */
void scout_jobs_handle(ScoutServer *srv, ULONG signals);

/**
 * @brief Return the signals of the jobs (port, timer).
 * @param srv The server.
 * @return The mask.
 */
ULONG scout_jobs_sigmask(const ScoutServer *srv);

/**
 * @brief Answer all waiting messages, stop the parsers that run (Ctrl-C)
 *        and wait for them, close the timer.
 * @param srv The server.
 */
void scout_jobs_cleanup(ScoutServer *srv);

/**
 * @brief Make a temporary file's name: TEMPDIR/Scout-PROCESS-N plus the
 *        suffix (PROCESS: Scout's process in hex, N: a counter).
 * @param srv The server.
 * @param suffix The end of the name, e.g. ".in".
 * @param[out] out The name.
 * @param size Size of @p out.
 */
void scout_temp_name(ScoutServer *srv, const char *suffix, char *out, size_t size);

/**
 * @brief Read the state of a file on disk.
 * @param path The file.
 * @param[out] stamp Its date and size.
 * @return 1 if it exists (and is a file), 0 otherwise.
 */
int scout_file_stamp(const char *path, ScoutStamp *stamp);

/**
 * @brief Queue the background analysis of a file of a session, unless it
 *        is known and unchanged, has no parser, or the session is full.
 * @param srv The server.
 * @param s The session.
 * @param path The file.
 * @param in_project It is a project file (not only included).
 * @return 1 if queued.
 */
int scout_index_file(ScoutServer *srv, ScoutSession *s, const char *path, int in_project);

/**
 * @brief Start walking a session's project drawer.
 * @param srv The server.
 * @param s The session.
 */
void scout_walk_start(ScoutServer *srv, ScoutSession *s);

/**
 * @brief Stop walking a session's project drawer.
 * @param srv The server.
 * @param session The session.
 */
void scout_walk_stop(ScoutServer *srv, long session);

/**
 * @brief Walk on a little (a few directory entries).
 * @param srv The server.
 * @return Non-zero while walks go on.
 */
int scout_walk_step(ScoutServer *srv);

/**
 * @brief Queue the included files of a file of a session that are not
 *        known yet (resolved next to the file and along the include paths).
 * @param srv The server.
 * @param s The session.
 * @param f The file.
 */
void scout_walk_references(ScoutServer *srv, ScoutSession *s, const ScoutFile *f);

#endif /* TINYDEV_SCOUT_SERVER_H */
