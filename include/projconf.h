/**
 * @file projconf.h
 * @brief Portable reader and writer of a project's settings
 *        (.tinydev/project.json): the commands of the actions Build, Run
 *        and Debug.
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
 * grow; projconf_format() keeps them.
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
    char *extra;        /**< the entry as read (JSON on one line), so projconf_format() keeps its other keys; NULL for a new one */
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
 * @brief Copy settings.
 * @param[out] dst The copy (initialised here; projconf_clear() it).
 * @param src The settings.
 * @return 1 on success, 0 if out of memory (@p dst is empty then).
 */
int projconf_copy(ProjConf *dst, const ProjConf *src);

/**
 * @brief Add or change a command of an action.
 *
 * Blank lines of the command are left out. A changed command keeps its
 * place and the other keys of its entry. Marking it as default takes the
 * mark from the others.
 *
 * @param c The settings.
 * @param action The action.
 * @param index The command to change, or -1 to add one at the end.
 * @param name Its name ("" or NULL: the first line).
 * @param command Its lines, separated by LF (a CR before it is dropped).
 * @param dir Its drawer ("" or NULL: the project directory).
 * @param is_default Whether the action runs it.
 * @return 1 on success; 0 if the command has no line (nothing changes);
 *         -1 if out of memory (nothing changes).
 */
int projconf_set(ProjConf *c, ProjAction action, long index, const char *name,
                 const char *command, const char *dir, int is_default);

/**
 * @brief Remove a command of an action.
 * @param c The settings.
 * @param action The action.
 * @param index The command.
 */
void projconf_remove(ProjConf *c, ProjAction action, long index);

/**
 * @brief Move a command of an action one place up or down.
 * @param c The settings.
 * @param action The action.
 * @param index The command.
 * @param delta -1 for up, 1 for down.
 * @return Its new index (the old one if it cannot move).
 */
long projconf_move(ProjConf *c, ProjAction action, long index, int delta);

/**
 * @brief Make a command the one its action runs (the others lose the mark).
 * @param c The settings.
 * @param action The action.
 * @param index The command.
 */
void projconf_set_default(ProjConf *c, ProjAction action, long index);

/**
 * @brief Return the lines of a command as one text, separated by LF.
 * @param cmd The command.
 * @return The text (free() it), or NULL if out of memory.
 */
char *projconf_command_text(const ProjCommand *cmd);

/**
 * @brief Write settings as the JSON text of a project file.
 *
 * The lists of the actions replace those of @p old_json; everything else
 * in it stays, in its place, and so do the other keys of the entries
 * (ProjCommand::extra). "name" is left out when it is the first line,
 * "dir" and "default" when they are not set. A new file starts with
 * "version": 1.
 *
 * @param c The settings.
 * @param old_json The text of the file as it is now (NULL or "" for a new
 *                 file); it must be a text projconf_parse() accepts.
 * @return The text (free() it), or NULL if out of memory or @p old_json
 *         is not a JSON object.
 */
char *projconf_format(const ProjConf *c, const char *old_json);

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
