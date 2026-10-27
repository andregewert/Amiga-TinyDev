/**
 * @file projconf.c
 * @brief Portable reader and writer of a project's settings.
 *
 * Reads and writes \<project\>/.tinydev/project.json (with json.c): the
 * commands of the actions Build, Run and Debug; replaces the placeholders
 * of a command. No Amiga headers, so the host tests can use it.
 */
#include "projconf.h"
#include "json.h"

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
    free(cmd->extra);
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
static int read_command(const JsonValue *e, ProjCommand *cmd)
{
    const JsonValue *command;
    const char *name, *dir;
    long n = 1, i;

    memset(cmd, 0, sizeof(*cmd));
    if (e == NULL || e->type != JSON_OBJECT) return 0;
    command = json_obj_get(e, "command");
    if (command == NULL) return 0;
    if (command->type == JSON_ARRAY) {
        n = (long)json_array_length(command);
        for (i = 0; i < n; ++i) {
            const JsonValue *line = json_array_get(command, (size_t)i);
            if (line == NULL || line->type != JSON_STRING) return 0;
        }
    } else if (command->type != JSON_STRING) return 0;
    if (n == 0 || (command->type == JSON_STRING && command->u.str_val[0] == '\0'))
        return 0;

    cmd->lines = calloc((size_t)n, sizeof(*cmd->lines));
    if (cmd->lines == NULL) return -1;
    for (i = 0; i < n; ++i) {
        const JsonValue *line = command->type == JSON_ARRAY ?
            json_array_get(command, (size_t)i) : command;
        if ((cmd->lines[i] = copy(line->u.str_val)) == NULL) break;
        cmd->line_count = i + 1;
    }
    name = json_obj_get_string(e, "name", "");
    dir = json_obj_get_string(e, "dir", "");
    cmd->name = copy(name[0] != '\0' ? name : cmd->lines[0] != NULL ? cmd->lines[0] : "");
    if (dir[0] != '\0') cmd->dir = copy(dir);
    cmd->is_default = json_obj_get_bool(e, "default", 0);
    {
        JsonWriter w;
        json_writer_init_alloc(&w, 128);
        json_write_value(&w, e, -1);
        if (!w.error) cmd->extra = copy(json_writer_get(&w));
        json_writer_free(&w);
    }
    if (cmd->line_count < n || cmd->name == NULL || cmd->extra == NULL ||
        (dir[0] != '\0' && cmd->dir == NULL)) {
        free_command(cmd);
        memset(cmd, 0, sizeof(*cmd));
        return -1;
    }
    return 1;
}

int projconf_parse(ProjConf *c, const char *json)
{
    JsonValue *root = json_parse(json, NULL);
    int a;

    if (root == NULL || root->type != JSON_OBJECT) {
        json_value_free(root);
        return 0;
    }
    projconf_clear(c);
    for (a = 0; a < PROJ_ACTIONS; ++a) {
        const JsonValue *list = json_obj_get_array(root, action_keys[a]);
        size_t i, n = json_array_length(list);
        if (n == 0) continue;
        c->commands[a] = malloc(n * sizeof(*c->commands[a]));
        if (c->commands[a] == NULL) goto nomem;
        for (i = 0; i < n; ++i) {
            int r = read_command(json_array_get(list, i), &c->commands[a][c->count[a]]);
            if (r < 0) goto nomem;
            if (r > 0) ++c->count[a];
        }
    }
    json_value_free(root);
    return 1;

nomem:
    json_value_free(root);
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

/**
 * @brief Copy a string that may be NULL.
 * @param text The string, or NULL.
 * @param[out] ok Cleared if out of memory.
 * @return The copy (free() it), or NULL.
 */
static char *copy_or_null(const char *text, int *ok)
{
    char *c;
    if (text == NULL) return NULL;
    if ((c = copy(text)) == NULL) *ok = 0;
    return c;
}

int projconf_copy(ProjConf *dst, const ProjConf *src)
{
    int a, ok = 1;
    long i, k;
    projconf_init(dst);
    for (a = 0; a < PROJ_ACTIONS && ok; ++a) {
        if (src->count[a] == 0) continue;
        dst->commands[a] = calloc((size_t)src->count[a], sizeof(ProjCommand));
        if (dst->commands[a] == NULL) break;
        for (i = 0; i < src->count[a] && ok; ++i) {
            const ProjCommand *s = &src->commands[a][i];
            ProjCommand *d = &dst->commands[a][i];
            dst->count[a] = i + 1;
            d->is_default = s->is_default;
            d->name = copy_or_null(s->name, &ok);
            d->dir = copy_or_null(s->dir, &ok);
            d->extra = copy_or_null(s->extra, &ok);
            d->lines = calloc((size_t)s->line_count, sizeof(char *));
            if (d->lines == NULL) ok = 0;
            for (k = 0; k < s->line_count && ok; ++k) {
                d->lines[k] = copy_or_null(s->lines[k], &ok);
                d->line_count = k + 1;
            }
        }
    }
    if (a < PROJ_ACTIONS || !ok) {
        projconf_clear(dst);
        return 0;
    }
    return 1;
}

/**
 * @brief Tell whether a line holds nothing but spaces and tabs.
 * @param line The line.
 * @param length Its length.
 * @return Non-zero if it is blank.
 */
static int blank(const char *line, size_t length)
{
    size_t i;
    for (i = 0; i < length; ++i)
        if (line[i] != ' ' && line[i] != '\t') return 0;
    return 1;
}

int projconf_set(ProjConf *c, ProjAction action, long index, const char *name,
                 const char *command, const char *dir, int is_default)
{
    ProjCommand cmd;
    const char *p, *end;
    long n = 0;

    memset(&cmd, 0, sizeof(cmd));
    /* count the lines that are not blank, then copy them */
    for (p = command; p != NULL && *p != '\0'; p = *end != '\0' ? end + 1 : end) {
        size_t length;
        end = strchr(p, '\n');
        if (end == NULL) end = p + strlen(p);
        length = (size_t)(end - p);
        if (length > 0 && p[length - 1] == '\r') --length;
        if (blank(p, length)) continue;
        if (cmd.lines == NULL) {
            /* first pass: count; at most one line per LF plus one */
            const char *q;
            long lines = 1;
            for (q = command; *q != '\0'; ++q) if (*q == '\n') ++lines;
            if ((cmd.lines = calloc((size_t)lines, sizeof(char *))) == NULL) return -1;
        }
        if ((cmd.lines[n] = malloc(length + 1)) == NULL) {
            cmd.line_count = n;
            free_command(&cmd);
            return -1;
        }
        memcpy(cmd.lines[n], p, length);
        cmd.lines[n][length] = '\0';
        cmd.line_count = ++n;
    }
    if (n == 0) return 0;

    cmd.name = copy(name != NULL && name[0] != '\0' ? name : cmd.lines[0]);
    if (dir != NULL && dir[0] != '\0') cmd.dir = copy(dir);
    if (index >= 0 && c->commands[action][index].extra != NULL)
        cmd.extra = copy(c->commands[action][index].extra);
    if (cmd.name == NULL || (dir != NULL && dir[0] != '\0' && cmd.dir == NULL) ||
        (index >= 0 && c->commands[action][index].extra != NULL && cmd.extra == NULL)) {
        free_command(&cmd);
        return -1;
    }
    cmd.is_default = is_default != 0;

    if (index < 0) {
        ProjCommand *grown = realloc(c->commands[action],
                                     (size_t)(c->count[action] + 1) * sizeof(*grown));
        if (grown == NULL) {
            free_command(&cmd);
            return -1;
        }
        c->commands[action] = grown;
        index = c->count[action]++;
    } else free_command(&c->commands[action][index]);
    c->commands[action][index] = cmd;
    if (is_default) projconf_set_default(c, action, index);
    return 1;
}

void projconf_remove(ProjConf *c, ProjAction action, long index)
{
    long i;
    free_command(&c->commands[action][index]);
    for (i = index + 1; i < c->count[action]; ++i)
        c->commands[action][i - 1] = c->commands[action][i];
    if (--c->count[action] == 0) {
        free(c->commands[action]);
        c->commands[action] = NULL;
    }
}

long projconf_move(ProjConf *c, ProjAction action, long index, int delta)
{
    long to = index + delta;
    ProjCommand t;
    if (to < 0 || to >= c->count[action]) return index;
    t = c->commands[action][index];
    c->commands[action][index] = c->commands[action][to];
    c->commands[action][to] = t;
    return to;
}

void projconf_set_default(ProjConf *c, ProjAction action, long index)
{
    long i;
    for (i = 0; i < c->count[action]; ++i)
        c->commands[action][i].is_default = i == index;
}

char *projconf_command_text(const ProjCommand *cmd)
{
    size_t length = 0;
    long i;
    char *text, *p;
    for (i = 0; i < cmd->line_count; ++i) length += strlen(cmd->lines[i]) + 1;
    if ((text = p = malloc(length + 1)) == NULL) return NULL;
    for (i = 0; i < cmd->line_count; ++i) {
        size_t n = strlen(cmd->lines[i]);
        if (i > 0) *p++ = '\n';
        memcpy(p, cmd->lines[i], n);
        p += n;
    }
    *p = '\0';
    return text;
}

/**
 * @brief Make the JSON entry of a command, starting from the entry as it
 *        was read.
 * @param cmd The command.
 * @return The entry (an object), or NULL if out of memory.
 */
static JsonValue *make_entry(const ProjCommand *cmd)
{
    JsonValue *e = cmd->extra != NULL ? json_parse(cmd->extra, NULL) : NULL;
    JsonValue *command;
    long i;
    int ok;

    if (e != NULL && e->type != JSON_OBJECT) {
        json_value_free(e);
        e = NULL;
    }
    if (e == NULL && (e = json_new_object()) == NULL) return NULL;
    if (cmd->line_count == 1) command = json_new_string(cmd->lines[0]);
    else {
        command = json_new_array();
        for (i = 0; command != NULL && i < cmd->line_count; ++i)
            if (!json_array_append(command, json_new_string(cmd->lines[i]))) {
                json_value_free(command);
                command = NULL;
            }
    }
    /* the order of a new entry: name, command, dir, default */
    if (strcmp(cmd->name, cmd->lines[0]) == 0) {
        json_obj_remove(e, "name");
        ok = 1;
    } else ok = json_obj_set(e, "name", json_new_string(cmd->name));
    if (ok) ok = json_obj_set(e, "command", command);
    else json_value_free(command);
    if (cmd->dir != NULL) ok = ok && json_obj_set(e, "dir", json_new_string(cmd->dir));
    else json_obj_remove(e, "dir");
    if (cmd->is_default) ok = ok && json_obj_set(e, "default", json_new_bool(1));
    else json_obj_remove(e, "default");
    if (!ok) {
        json_value_free(e);
        return NULL;
    }
    return e;
}

char *projconf_format(const ProjConf *c, const char *old_json)
{
    JsonValue *root;
    JsonWriter w;
    char *text = NULL;
    int a, ok = 1;
    long i;

    if (old_json != NULL && old_json[0] != '\0') {
        root = json_parse(old_json, NULL);
        if (root != NULL && root->type != JSON_OBJECT) {
            json_value_free(root);
            return NULL;
        }
    } else {
        root = json_new_object();
        if (root != NULL && !json_obj_set(root, "version", json_new_int(1))) {
            json_value_free(root);
            root = NULL;
        }
    }
    if (root == NULL) return NULL;

    for (a = 0; a < PROJ_ACTIONS && ok; ++a) {
        JsonValue *list = json_new_array();
        for (i = 0; list != NULL && i < c->count[a]; ++i)
            if (!json_array_append(list, make_entry(&c->commands[a][i]))) {
                json_value_free(list);
                list = NULL;
            }
        ok = json_obj_set(root, action_keys[a], list);
    }
    if (ok) {
        json_writer_init_alloc(&w, 512);
        json_write_value(&w, root, 0);
        json_write_raw(&w, "\n");
        if (!w.error) text = copy(json_writer_get(&w));
        json_writer_free(&w);
    }
    json_value_free(root);
    return text;
}
