/**
 * @file projconf.c
 * @brief Portable reader of a project's settings.
 *
 * Reads \<project\>/.tinydev/project.json (with Scout's JSON code) into the
 * commands of the actions Build, Run and Debug, and replaces the
 * placeholders of a command. No Amiga headers, so the host tests can use
 * it.
 */
#include "projconf.h"
#include "scout_json.h"

#include <stdlib.h>
#include <string.h>

/** The keys of the actions in the file, in the order of ProjAction. */
static const char *const action_keys[PROJ_ACTIONS] = { "build", "run", "debug" };

const char *projconf_action_key(ProjAction action)
{
    return action_keys[action];
}

void projconf_init(ProjConf *c)
{
    int a;
    for (a = 0; a < PROJ_ACTIONS; ++a) {
        c->commands[a] = NULL;
        c->count[a] = 0;
    }
}

/**
 * @brief Free what a command holds.
 * @param cmd The command.
 */
static void free_command(ProjCommand *cmd)
{
    long i;
    for (i = 0; i < cmd->line_count; ++i) free(cmd->lines[i]);
    free(cmd->lines);
    free(cmd->name);
    free(cmd->dir);
}

void projconf_clear(ProjConf *c)
{
    int a;
    long i;
    for (a = 0; a < PROJ_ACTIONS; ++a) {
        for (i = 0; i < c->count[a]; ++i) free_command(&c->commands[a][i]);
        free(c->commands[a]);
    }
    projconf_init(c);
}

/**
 * @brief Copy a string.
 * @param text The string.
 * @return The copy (free() it), or NULL.
 */
static char *copy(const char *text)
{
    size_t n = strlen(text) + 1;
    char *c = malloc(n);
    if (c != NULL) memcpy(c, text, n);
    return c;
}

/**
 * @brief Read one entry of an action's list.
 * @param e The entry.
 * @param[out] cmd The command (zeroed if the entry is skipped).
 * @return 1 if it is a command, 0 if it is skipped, -1 if out of memory.
 */
static int read_command(const ScoutJsonValue *e, ProjCommand *cmd)
{
    const ScoutJsonValue *command;
    const char *name, *dir;
    long n = 1, i;

    memset(cmd, 0, sizeof(*cmd));
    if (e == NULL || e->type != SCOUT_JSON_OBJECT) return 0;
    command = scout_json_obj_get(e, "command");
    if (command == NULL) return 0;
    if (command->type == SCOUT_JSON_ARRAY) {
        n = (long)scout_json_array_length(command);
        for (i = 0; i < n; ++i) {
            const ScoutJsonValue *line = scout_json_array_get(command, (size_t)i);
            if (line == NULL || line->type != SCOUT_JSON_STRING) return 0;
        }
    } else if (command->type != SCOUT_JSON_STRING) return 0;
    if (n == 0 || (command->type == SCOUT_JSON_STRING && command->u.str_val[0] == '\0'))
        return 0;

    cmd->lines = calloc((size_t)n, sizeof(*cmd->lines));
    if (cmd->lines == NULL) return -1;
    for (i = 0; i < n; ++i) {
        const ScoutJsonValue *line = command->type == SCOUT_JSON_ARRAY ?
            scout_json_array_get(command, (size_t)i) : command;
        if ((cmd->lines[i] = copy(line->u.str_val)) == NULL) break;
        cmd->line_count = i + 1;
    }
    name = scout_json_obj_get_string(e, "name", "");
    dir = scout_json_obj_get_string(e, "dir", "");
    cmd->name = copy(name[0] != '\0' ? name : cmd->lines[0] != NULL ? cmd->lines[0] : "");
    if (dir[0] != '\0') cmd->dir = copy(dir);
    cmd->is_default = scout_json_obj_get_bool(e, "default", 0);
    if (cmd->line_count < n || cmd->name == NULL || (dir[0] != '\0' && cmd->dir == NULL)) {
        free_command(cmd);
        memset(cmd, 0, sizeof(*cmd));
        return -1;
    }
    return 1;
}

int projconf_parse(ProjConf *c, const char *json)
{
    ScoutJsonValue *root = scout_json_parse(json, NULL);
    int a;

    if (root == NULL || root->type != SCOUT_JSON_OBJECT) {
        scout_json_value_free(root);
        return 0;
    }
    projconf_clear(c);
    for (a = 0; a < PROJ_ACTIONS; ++a) {
        const ScoutJsonValue *list = scout_json_obj_get_array(root, action_keys[a]);
        size_t i, n = scout_json_array_length(list);
        if (n == 0) continue;
        c->commands[a] = malloc(n * sizeof(*c->commands[a]));
        if (c->commands[a] == NULL) goto nomem;
        for (i = 0; i < n; ++i) {
            int r = read_command(scout_json_array_get(list, i), &c->commands[a][c->count[a]]);
            if (r < 0) goto nomem;
            if (r > 0) ++c->count[a];
        }
    }
    scout_json_value_free(root);
    return 1;

nomem:
    scout_json_value_free(root);
    projconf_clear(c);
    return -1;
}

long projconf_default(const ProjConf *c, ProjAction action)
{
    long i;
    for (i = 0; i < c->count[action]; ++i)
        if (c->commands[action][i].is_default) return i;
    return c->count[action] > 0 ? 0 : -1;
}

/**
 * @brief Return the length of a path without its extension: up to the last
 *        dot of its last part, if that part has one (not at its start).
 * @param path The path.
 * @return The length.
 */
static size_t base_length(const char *path)
{
    size_t n = strlen(path), i = n;
    while (i > 0 && path[i - 1] != '/' && path[i - 1] != ':') {
        --i;
        if (path[i] == '.' && i > 0 && path[i - 1] != '/' && path[i - 1] != ':') return i;
    }
    return n;
}

char *projconf_expand(const char *text, const char *project, const char *file, int *missing)
{
    /* Two passes: the first measures, the second writes. */
    char *out = NULL;
    size_t length = 0;
    int pass;

    if (missing != NULL) *missing = 0;
    for (pass = 0; pass < 2; ++pass) {
        const char *p = text;
        length = 0;
        while (*p != '\0') {
            const char *value = NULL;
            size_t value_length = 0, skip = 0;
            if (*p == '{') {
                if (strncmp(p, "{project}", 9) == 0) {
                    value = project;
                    value_length = strlen(project);
                    skip = 9;
                } else if (strncmp(p, "{file}", 6) == 0 || strncmp(p, "{base}", 6) == 0) {
                    skip = 6;
                    if (file == NULL) {
                        if (missing != NULL) *missing = 1;
                        value = "";
                    } else {
                        value = file;
                        value_length = p[1] == 'b' ? base_length(file) : strlen(file);
                    }
                }
            }
            if (value != NULL) {
                if (out != NULL) memcpy(out + length, value, value_length);
                length += value_length;
                p += skip;
            } else {
                if (out != NULL) out[length] = *p;
                ++length;
                ++p;
            }
        }
        if (pass == 0 && (out = malloc(length + 1)) == NULL) return NULL;
    }
    out[length] = '\0';
    return out;
}
