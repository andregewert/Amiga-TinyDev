/**
 * @file projconf.h
 * @brief Portable reader of a project's settings (.tinydev/project.json):
 *        the commands of the actions Build, Run and Debug.
 */
#ifndef TINYDEV_PROJCONF_H
#define TINYDEV_PROJCONF_H

/*
 * The settings of a project, as kept in <project>/.tinydev/project.json.
 * No Amiga headers, so the host tests can use it.
 *
 * The file (ISO-8859-1, like all of TinyDev's files):
 *
 *   {
 *     "version": 1,
 *     "build": [
 *       { "name": "Build", "command": "make", "default": true },
 *       { "name": "Clean", "command": ["Delete build ALL QUIET", "Delete bin/TinyDev QUIET"] },
 *       { "name": "Compile file", "command": "vc -c \"{file}\" -o \"{base}.o\"" }
 *     ],
 *     "run": [
 *       { "name": "TinyDev", "command": "TinyDev", "dir": "bin" }
 *     ],
 *     "debug": [
 *       { "name": "Debugger", "command": "BDebug bin/TinyDev" }
 *     ]
 *   }
 *
 * Each action has a list of commands:
 * - "command" is a Shell command line, or an array of lines that run as a
 *   script (the AmigaShell has no separator between commands);
 * - "name" is shown in menus; without it, the first line is the name;
 * - "dir" is the drawer the command runs in, relative to the project
 *   directory (or absolute); without it, the project directory;
 * - "default": true marks the command the action runs; the first marked
 *   one counts, and without a mark the first command.
 * Entries without a command are skipped, as are lists that are no arrays.
 * Other keys, in the file and in the entries, are ignored, so the file can
 * grow; whoever writes it later must keep them.
 *
 * In command lines and "dir", {project} stands for the project directory,
 * {file} for the active document and {base} for the document without its
 * extension (projconf_expand()). Braces, unlike "$", mean nothing to the
 * AmigaShell. A path with spaces needs quotes around the placeholder.
 */

/** The actions with commands. */
typedef enum ProjAction {
    PROJ_BUILD,     /**< build the project ("build") */
    PROJ_RUN,       /**< run it ("run") */
    PROJ_DEBUG,     /**< debug it ("debug") */
    PROJ_ACTIONS    /**< number of actions */
} ProjAction;

/** A command of an action. */
typedef struct ProjCommand {
    char *name;         /**< shown in menus (never NULL) */
    char **lines;       /**< the command lines (at least one) */
    long line_count;    /**< number of lines */
    char *dir;          /**< drawer to run in, NULL for the project directory */
    int is_default;     /**< marked "default": true */
} ProjCommand;

/** The settings of a project. */
typedef struct ProjConf {
    ProjCommand *commands[PROJ_ACTIONS];    /**< the commands of each action (malloc()) */
    long count[PROJ_ACTIONS];               /**< number of commands of each action */
} ProjConf;

/**
 * @brief Initialise empty settings.
 * @param c The settings.
 */
void projconf_init(ProjConf *c);

/**
 * @brief Remove all commands and free the memory.
 * @param c The settings.
 */
void projconf_clear(ProjConf *c);

/**
 * @brief Return the key of an action in the file.
 * @param action The action.
 * @return "build", "run" or "debug".
 */
const char *projconf_action_key(ProjAction action);

/**
 * @brief Read settings from the JSON text of a project file, replacing
 *        those in @p c.
 * @param c The settings.
 * @param json The text.
 * @return 1 on success; 0 if the text is not a JSON object (@p c is left
 *         unchanged then); -1 if out of memory (@p c is empty then).
 */
int projconf_parse(ProjConf *c, const char *json);

/**
 * @brief Return the command an action runs: the first marked as default,
 *        otherwise the first.
 * @param c The settings.
 * @param action The action.
 * @return Its index in ProjConf::commands, or -1 if the action has none.
 */
long projconf_default(const ProjConf *c, ProjAction action);

/**
 * @brief Replace the placeholders {project}, {file} and {base} in a text.
 *
 * Other braces stay as they are.
 *
 * @param text The text (a command line or a drawer).
 * @param project The project directory.
 * @param file The active document's full path, NULL if there is none
 *             (or it was never saved).
 * @param[out] missing Set to non-zero if the text needs a document but
 *                     @p file is NULL ({file} and {base} are then left
 *                     out); may be NULL.
 * @return The text (free() it), or NULL if out of memory.
 */
char *projconf_expand(const char *text, const char *project, const char *file, int *missing);

#endif
