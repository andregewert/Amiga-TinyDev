/**
 * @file scout_parser_runner.c
 * @brief Runs the language parsers of Scout and talks to them with the Scout protocol over STDIN/STDOUT.
 */
#include "scout_server.h"
#include "scout_json.h"

#include <proto/exec.h>
#include <proto/dos.h>
#include <dos/dostags.h>
#include <workbench/startup.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * @brief Copy a string with malloc().
 * @param s The string (may be NULL).
 * @return The copy, or NULL.
 */
static char *scout_strdup(const char *s)
{
    if (!s) return NULL;
    size_t len = strlen(s);
    char *copy = (char *)malloc(len + 1);
    if (copy) {
        memcpy(copy, s, len + 1);
    }
    return copy;
}

int scout_server_add_search_path(ScoutServer *srv, const char *path)
{
    if (!srv || !path || !*path) return 0;
    if (srv->search_path_count >= SCOUT_MAX_SEARCH_PATHS) return 0;

    for (size_t i = 0; i < srv->search_path_count; i++) {
        if (srv->search_paths[i] && strcmp(srv->search_paths[i], path) == 0) {
            return 1;
        }
    }

    char *copy = scout_strdup(path);
    if (!copy) return 0;

    srv->search_paths[srv->search_path_count++] = copy;
    return 1;
}

void scout_server_clear_search_paths(ScoutServer *srv)
{
    if (!srv) return;
    for (size_t i = 0; i < srv->search_path_count; i++) {
        if (srv->search_paths[i]) {
            free(srv->search_paths[i]);
            srv->search_paths[i] = NULL;
        }
    }
    srv->search_path_count = 0;
}

int app_get_program_directory(int argc, char **argv, char *out_dir, size_t out_size)
{
    if (!out_dir || out_size == 0) return 0;
    out_dir[0] = '\0';

    if (argc == 0) {
        /* Workbench startup: first element of sm_ArgList contains lock to program directory */
        struct WBStartup *wb_msg = (struct WBStartup *)argv;
        if (wb_msg && wb_msg->sm_NumArgs > 0) {
            struct WBArg *app_arg = &wb_msg->sm_ArgList[0];
            if (app_arg->wa_Lock) {
                return NameFromLock(app_arg->wa_Lock, out_dir, (LONG)out_size) != 0;
            }
        }
    } else {
        /* CLI startup: GetProgramDir() returns lock to program directory if available */
        BPTR prog_lock = GetProgramDir();
        if (prog_lock) {
            if (NameFromLock(prog_lock, out_dir, (LONG)out_size)) {
                return 1;
            }
        }

        /* Fallback when GetProgramDir() lock is unavailable */
        char prog_name[256];
        prog_name[0] = '\0';
        if (GetProgramName(prog_name, sizeof(prog_name)) || (argv && argv[0])) {
            const char *src = (prog_name[0] != '\0') ? prog_name : argv[0];
            const char *last_sep = strrchr(src, '/');
            const char *colon = strrchr(src, ':');
            const char *split = (last_sep > colon) ? last_sep : colon;

            if (split) {
                size_t len = (size_t)(split - src) + (split == colon ? 1 : 0);
                if (len < out_size) {
                    strncpy(out_dir, src, len);
                    out_dir[len] = '\0';
                    return 1;
                }
            }
        }
    }

    return 0;
}

int scout_parser_register(ScoutServer *srv, const char *ext, const char *exec_path)
{
    if (!srv || !ext || !exec_path) return 0;

    /* Check if extension is already registered -> update path */
    for (ParserEntry *p = srv->parsers; p; p = p->next) {
        if (p->extension && strcmp(p->extension, ext) == 0) {
            free(p->executable_path);
            p->executable_path = scout_strdup(exec_path);
            return 1;
        }
    }

    ParserEntry *entry = (ParserEntry *)malloc(sizeof(ParserEntry));
    if (!entry) return 0;

    entry->extension = scout_strdup(ext);
    entry->executable_path = scout_strdup(exec_path);
    entry->next = srv->parsers;
    srv->parsers = entry;
    return 1;
}

const char *scout_parser_find(ScoutServer *srv, const char *filepath)
{
    if (!srv || !filepath) return NULL;

    const char *dot = strrchr(filepath, '.');
    if (dot) {
        for (ParserEntry *p = srv->parsers; p; p = p->next) {
            if (p->extension && strcasecmp(p->extension, dot) == 0) {
                return p->executable_path;
            }
        }
    }

    /* Fallback search for C parser (.c / .h) */
    if (dot && (strcasecmp(dot, ".c") == 0 || strcasecmp(dot, ".h") == 0)) {
        static char resolved_path[256];

        /* 1. Prioritized search paths configured in ScoutServer (e.g. program directory) */
        for (size_t i = 0; i < srv->search_path_count; i++) {
            if (!srv->search_paths[i]) continue;

            /* Check <SearchPath>/parsers/c_parser */
            strncpy(resolved_path, srv->search_paths[i], sizeof(resolved_path) - 1);
            resolved_path[sizeof(resolved_path) - 1] = '\0';
            if (AddPart(resolved_path, "parsers/c_parser", sizeof(resolved_path))) {
                BPTR lock = Lock(resolved_path, SHARED_LOCK);
                if (lock) {
                    UnLock(lock);
                    return resolved_path;
                }
            }

            /* Check <SearchPath>/c_parser */
            strncpy(resolved_path, srv->search_paths[i], sizeof(resolved_path) - 1);
            resolved_path[sizeof(resolved_path) - 1] = '\0';
            if (AddPart(resolved_path, "c_parser", sizeof(resolved_path))) {
                BPTR lock = Lock(resolved_path, SHARED_LOCK);
                if (lock) {
                    UnLock(lock);
                    return resolved_path;
                }
            }
        }

        /* 2. Relative fallbacks from current working directory */
        static const char *local_fallbacks[] = {
            "bin/parsers/c_parser",
            "parsers/c_parser",
            "c_parser",
            NULL
        };
        for (size_t i = 0; local_fallbacks[i]; i++) {
            BPTR lock = Lock(local_fallbacks[i], SHARED_LOCK);
            if (lock) {
                UnLock(lock);
                return local_fallbacks[i];
            }
        }

        /* 3. Final fallback: system PATH / assign */
        return "c_parser";
    }

    return NULL;
}

void scout_parser_registry_cleanup(ScoutServer *srv)
{
    if (!srv) return;
    ParserEntry *p = srv->parsers;
    while (p) {
        ParserEntry *next = p->next;
        if (p->extension) free(p->extension);
        if (p->executable_path) free(p->executable_path);
        free(p);
        p = next;
    }
    srv->parsers = NULL;
}

int scout_parser_run_request(ScoutServer *srv, const char *filepath,
                           const char *method, const char *params_json,
                           ScoutJsonWriter *out_result, char **out_error_msg)
{
    if (!srv || !filepath || !method || !out_result) return 0;
    if (out_error_msg) *out_error_msg = NULL;

    const char *parser_path = scout_parser_find(srv, filepath);
    if (!parser_path) {
        if (out_error_msg) {
            *out_error_msg = scout_strdup("No parser registered for file extension");
        }
        return 0;
    }

    /* Build JSON-RPC request line */
    ScoutJsonWriter req_writer;
    scout_json_writer_init_alloc(&req_writer, 512);

    long req_id = ++srv->request_count;
    if (!scout_json_format_request(&req_writer, req_id, method, params_json)) {
        scout_json_writer_free(&req_writer);
        if (out_error_msg) *out_error_msg = scout_strdup("Failed to construct JSON-RPC request");
        return 0;
    }

    /* Create unique temp files in T: */
    char in_temp[64];
    char out_temp[64];
    snprintf(in_temp, sizeof(in_temp), "T:tdscout_in_%ld.json", req_id);
    snprintf(out_temp, sizeof(out_temp), "T:tdscout_out_%ld.json", req_id);

    BPTR in_write_fh = Open(in_temp, MODE_NEWFILE);
    if (!in_write_fh) {
        scout_json_writer_free(&req_writer);
        if (out_error_msg) *out_error_msg = scout_strdup("Failed to open temp input file");
        return 0;
    }

    const char *req_str = scout_json_writer_get(&req_writer);
    size_t req_len = scout_json_writer_length(&req_writer);
    Write(in_write_fh, (CONST APTR)req_str, (LONG)req_len);
    Write(in_write_fh, (CONST APTR)"\n", 1);
    Close(in_write_fh);
    scout_json_writer_free(&req_writer);

    /* Open I/O handles for child process redirection */
    BPTR in_read_fh = Open(in_temp, MODE_OLDFILE);
    BPTR out_write_fh = Open(out_temp, MODE_NEWFILE);

    if (!in_read_fh || !out_write_fh) {
        if (in_read_fh) Close(in_read_fh);
        if (out_write_fh) Close(out_write_fh);
        DeleteFile(in_temp);
        DeleteFile(out_temp);
        if (out_error_msg) *out_error_msg = scout_strdup("Failed to open child process I/O streams");
        return 0;
    }

    LONG sys_rc = SystemTags(parser_path,
                             SYS_Input, (ULONG)in_read_fh,
                             SYS_Output, (ULONG)out_write_fh,
                             SYS_UserShell, FALSE,
                             TAG_DONE);

    Close(in_read_fh);
    Close(out_write_fh);
    DeleteFile(in_temp);

    if (sys_rc != 0) {
        DeleteFile(out_temp);
        if (out_error_msg) {
            char err_buf[128];
            snprintf(err_buf, sizeof(err_buf), "Parser process failed with code %ld", (long)sys_rc);
            *out_error_msg = scout_strdup(err_buf);
        }
        return 0;
    }

    /* Read response file */
    BPTR out_read_fh = Open(out_temp, MODE_OLDFILE);
    if (!out_read_fh) {
        DeleteFile(out_temp);
        if (out_error_msg) *out_error_msg = scout_strdup("Failed to open parser output file");
        return 0;
    }

    Seek(out_read_fh, 0, OFFSET_END);
    LONG file_size = Seek(out_read_fh, 0, OFFSET_BEGINNING);

    if (file_size <= 0) {
        Close(out_read_fh);
        DeleteFile(out_temp);
        if (out_error_msg) *out_error_msg = scout_strdup("Parser returned empty output");
        return 0;
    }

    char *resp_buf = (char *)malloc((size_t)file_size + 1);
    if (!resp_buf) {
        Close(out_read_fh);
        DeleteFile(out_temp);
        if (out_error_msg) *out_error_msg = scout_strdup("Out of memory reading parser output");
        return 0;
    }

    LONG bytes_read = Read(out_read_fh, resp_buf, file_size);
    Close(out_read_fh);
    DeleteFile(out_temp);

    if (bytes_read > 0) {
        resp_buf[bytes_read] = '\0';
    } else {
        resp_buf[0] = '\0';
    }

    /* Parse JSON-RPC response */
    ScoutJsonRpcMessage resp_msg;
    int parse_ok = scout_json_rpc_parse(resp_buf, &resp_msg);
    free(resp_buf);

    if (!parse_ok) {
        if (out_error_msg) *out_error_msg = scout_strdup("Malformed JSON-RPC response from parser");
        return 0;
    }

    if (resp_msg.has_error) {
        if (out_error_msg) {
            *out_error_msg = scout_strdup(resp_msg.error_msg ? resp_msg.error_msg : "Scout error");
        }
        scout_json_rpc_free(&resp_msg);
        return 0;
    }

    /* Format raw result to JSON writer */
    if (resp_msg.result) {
        /* Re-serialize root result or copy DOM */
        const ScoutJsonValue *res_val = resp_msg.result;
        if (res_val->type == SCOUT_JSON_ARRAY) {
            scout_json_write_arr_start(out_result);
            size_t count = scout_json_array_length(res_val);
            for (size_t i = 0; i < count; i++) {
                if (i > 0) scout_json_write_raw(out_result, ",");
                const ScoutJsonValue *item = scout_json_array_get(res_val, i);
                if (item && item->type == SCOUT_JSON_OBJECT) {
                    scout_json_write_obj_start(out_result);
                    const ScoutJsonMember *m = item->u.obj_val.first;
                    int f = 1;
                    while (m) {
                        if (!f) scout_json_write_raw(out_result, ",");
                        f = 0;
                        scout_json_write_obj_key(out_result, m->key);
                        if (m->value->type == SCOUT_JSON_STRING) {
                            scout_json_write_string(out_result, m->value->u.str_val);
                        } else if (m->value->type == SCOUT_JSON_NUMBER) {
                            scout_json_write_int(out_result, m->value->u.num_val.int_val);
                        } else if (m->value->type == SCOUT_JSON_BOOL) {
                            scout_json_write_bool(out_result, m->value->u.bool_val);
                        } else if (m->value->type == SCOUT_JSON_OBJECT) {
                            /* nested range object */
                            scout_json_write_obj_start(out_result);
                            const ScoutJsonMember *sub_m = m->value->u.obj_val.first;
                            int sub_f = 1;
                            while (sub_m) {
                                if (!sub_f) scout_json_write_raw(out_result, ",");
                                sub_f = 0;
                                scout_json_write_obj_key(out_result, sub_m->key);
                                if (sub_m->value->type == SCOUT_JSON_OBJECT) {
                                    scout_json_write_obj_start(out_result);
                                    const ScoutJsonMember *pos_m = sub_m->value->u.obj_val.first;
                                    int pos_f = 1;
                                    while (pos_m) {
                                        if (!pos_f) scout_json_write_raw(out_result, ",");
                                        pos_f = 0;
                                        scout_json_write_obj_key(out_result, pos_m->key);
                                        scout_json_write_int(out_result, pos_m->value->u.num_val.int_val);
                                        pos_m = pos_m->next;
                                    }
                                    scout_json_write_obj_end(out_result);
                                } else {
                                    scout_json_write_int(out_result, sub_m->value->u.num_val.int_val);
                                }
                                sub_m = sub_m->next;
                            }
                            scout_json_write_obj_end(out_result);
                        }
                        m = m->next;
                    }
                    scout_json_write_obj_end(out_result);
                }
            }
            scout_json_write_arr_end(out_result);
        } else if (res_val->type == SCOUT_JSON_OBJECT) {
            scout_json_write_obj_start(out_result);
            const ScoutJsonMember *m = res_val->u.obj_val.first;
            int f = 1;
            while (m) {
                if (!f) scout_json_write_raw(out_result, ",");
                f = 0;
                scout_json_write_obj_key(out_result, m->key);
                if (m->value->type == SCOUT_JSON_STRING) {
                    scout_json_write_string(out_result, m->value->u.str_val);
                } else if (m->value->type == SCOUT_JSON_NUMBER) {
                    scout_json_write_int(out_result, m->value->u.num_val.int_val);
                }
                m = m->next;
            }
            scout_json_write_obj_end(out_result);
        }
    }

    scout_json_rpc_free(&resp_msg);
    return 1;
}

void scout_cache_clear(ScoutServer *srv)
{
    if (!srv) return;
    CachedFileResult *c = srv->cache;
    while (c) {
        CachedFileResult *next = c->next;
        if (c->filepath) free(c->filepath);
        if (c->symbols_rexx) free(c->symbols_rexx);
        if (c->diagnostics_rexx) free(c->diagnostics_rexx);
        free(c);
        c = next;
    }
    srv->cache = NULL;
}

void scout_cache_put(ScoutServer *srv, const char *filepath, const char *symbols, const char *diags)
{
    if (!srv || !filepath) return;

    for (CachedFileResult *c = srv->cache; c; c = c->next) {
        if (c->filepath && strcmp(c->filepath, filepath) == 0) {
            if (symbols) {
                if (c->symbols_rexx) free(c->symbols_rexx);
                c->symbols_rexx = scout_strdup(symbols);
            }
            if (diags) {
                if (c->diagnostics_rexx) free(c->diagnostics_rexx);
                c->diagnostics_rexx = scout_strdup(diags);
            }
            return;
        }
    }

    CachedFileResult *entry = (CachedFileResult *)malloc(sizeof(CachedFileResult));
    if (!entry) return;

    entry->filepath = scout_strdup(filepath);
    entry->symbols_rexx = symbols ? scout_strdup(symbols) : NULL;
    entry->diagnostics_rexx = diags ? scout_strdup(diags) : NULL;
    entry->next = srv->cache;
    srv->cache = entry;
}

const CachedFileResult *scout_cache_get(ScoutServer *srv, const char *filepath)
{
    if (!srv || !filepath) return NULL;
    for (const CachedFileResult *c = srv->cache; c; c = c->next) {
        if (c->filepath && strcmp(c->filepath, filepath) == 0) {
            return c;
        }
    }
    return NULL;
}
