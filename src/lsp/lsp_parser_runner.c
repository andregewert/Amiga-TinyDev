#include "lsp_server.h"
#include "lsp_json.h"

#include <proto/exec.h>
#include <proto/dos.h>
#include <dos/dostags.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *lsp_strdup(const char *s)
{
    if (!s) return NULL;
    size_t len = strlen(s);
    char *copy = (char *)malloc(len + 1);
    if (copy) {
        memcpy(copy, s, len + 1);
    }
    return copy;
}

int lsp_parser_register(LspServer *srv, const char *ext, const char *exec_path)
{
    if (!srv || !ext || !exec_path) return 0;

    /* Check if extension is already registered -> update path */
    for (ParserEntry *p = srv->parsers; p; p = p->next) {
        if (p->extension && strcmp(p->extension, ext) == 0) {
            free(p->executable_path);
            p->executable_path = lsp_strdup(exec_path);
            return 1;
        }
    }

    ParserEntry *entry = (ParserEntry *)malloc(sizeof(ParserEntry));
    if (!entry) return 0;

    entry->extension = lsp_strdup(ext);
    entry->executable_path = lsp_strdup(exec_path);
    entry->next = srv->parsers;
    srv->parsers = entry;
    return 1;
}

const char *lsp_parser_find(LspServer *srv, const char *filepath)
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

    /* Default fallbacks for C */
    if (dot && (strcasecmp(dot, ".c") == 0 || strcasecmp(dot, ".h") == 0)) {
        static const char *default_c_paths[] = {
            "PROGDIR:parsers/c_parser",
            "build/parsers/c_parser",
            "parsers/c_parser",
            "c_parser",
            NULL
        };
        for (size_t i = 0; default_c_paths[i]; i++) {
            BPTR lock = Lock(default_c_paths[i], SHARED_LOCK);
            if (lock) {
                UnLock(lock);
                return default_c_paths[i];
            }
        }
        return "PROGDIR:parsers/c_parser";
    }

    return NULL;
}

void lsp_parser_registry_cleanup(LspServer *srv)
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

int lsp_parser_run_request(LspServer *srv, const char *filepath,
                           const char *method, const char *params_json,
                           LspJsonWriter *out_result, char **out_error_msg)
{
    if (!srv || !filepath || !method || !out_result) return 0;
    if (out_error_msg) *out_error_msg = NULL;

    const char *parser_path = lsp_parser_find(srv, filepath);
    if (!parser_path) {
        if (out_error_msg) {
            *out_error_msg = lsp_strdup("No parser registered for file extension");
        }
        return 0;
    }

    /* Build JSON-RPC request line */
    LspJsonWriter req_writer;
    lsp_json_writer_init_alloc(&req_writer, 512);

    long req_id = ++srv->request_count;
    if (!lsp_json_format_request(&req_writer, req_id, method, params_json)) {
        lsp_json_writer_free(&req_writer);
        if (out_error_msg) *out_error_msg = lsp_strdup("Failed to construct JSON-RPC request");
        return 0;
    }

    /* Create unique temp files in T: */
    char in_temp[64];
    char out_temp[64];
    snprintf(in_temp, sizeof(in_temp), "T:tdlsp_in_%ld.json", req_id);
    snprintf(out_temp, sizeof(out_temp), "T:tdlsp_out_%ld.json", req_id);

    BPTR in_write_fh = Open(in_temp, MODE_NEWFILE);
    if (!in_write_fh) {
        lsp_json_writer_free(&req_writer);
        if (out_error_msg) *out_error_msg = lsp_strdup("Failed to open temp input file");
        return 0;
    }

    const char *req_str = lsp_json_writer_get(&req_writer);
    size_t req_len = lsp_json_writer_length(&req_writer);
    Write(in_write_fh, (CONST APTR)req_str, (LONG)req_len);
    Write(in_write_fh, (CONST APTR)"\n", 1);
    Close(in_write_fh);
    lsp_json_writer_free(&req_writer);

    /* Open I/O handles for child process redirection */
    BPTR in_read_fh = Open(in_temp, MODE_OLDFILE);
    BPTR out_write_fh = Open(out_temp, MODE_NEWFILE);

    if (!in_read_fh || !out_write_fh) {
        if (in_read_fh) Close(in_read_fh);
        if (out_write_fh) Close(out_write_fh);
        DeleteFile(in_temp);
        DeleteFile(out_temp);
        if (out_error_msg) *out_error_msg = lsp_strdup("Failed to open child process I/O streams");
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
            *out_error_msg = lsp_strdup(err_buf);
        }
        return 0;
    }

    /* Read response file */
    BPTR out_read_fh = Open(out_temp, MODE_OLDFILE);
    if (!out_read_fh) {
        DeleteFile(out_temp);
        if (out_error_msg) *out_error_msg = lsp_strdup("Failed to open parser output file");
        return 0;
    }

    Seek(out_read_fh, 0, OFFSET_END);
    LONG file_size = Seek(out_read_fh, 0, OFFSET_BEGINNING);

    if (file_size <= 0) {
        Close(out_read_fh);
        DeleteFile(out_temp);
        if (out_error_msg) *out_error_msg = lsp_strdup("Parser returned empty output");
        return 0;
    }

    char *resp_buf = (char *)malloc((size_t)file_size + 1);
    if (!resp_buf) {
        Close(out_read_fh);
        DeleteFile(out_temp);
        if (out_error_msg) *out_error_msg = lsp_strdup("Out of memory reading parser output");
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
    LspJsonRpcMessage resp_msg;
    int parse_ok = lsp_json_rpc_parse(resp_buf, &resp_msg);
    free(resp_buf);

    if (!parse_ok) {
        if (out_error_msg) *out_error_msg = lsp_strdup("Malformed JSON-RPC response from parser");
        return 0;
    }

    if (resp_msg.has_error) {
        if (out_error_msg) {
            *out_error_msg = lsp_strdup(resp_msg.error_msg ? resp_msg.error_msg : "LSP error");
        }
        lsp_json_rpc_free(&resp_msg);
        return 0;
    }

    /* Format raw result to JSON writer */
    if (resp_msg.result) {
        /* Re-serialize root result or copy DOM */
        const LspJsonValue *res_val = resp_msg.result;
        if (res_val->type == LSP_JSON_ARRAY) {
            lsp_json_write_arr_start(out_result);
            size_t count = lsp_json_array_length(res_val);
            for (size_t i = 0; i < count; i++) {
                if (i > 0) lsp_json_write_raw(out_result, ",");
                const LspJsonValue *item = lsp_json_array_get(res_val, i);
                if (item && item->type == LSP_JSON_OBJECT) {
                    lsp_json_write_obj_start(out_result);
                    const LspJsonMember *m = item->u.obj_val.first;
                    int f = 1;
                    while (m) {
                        if (!f) lsp_json_write_raw(out_result, ",");
                        f = 0;
                        lsp_json_write_obj_key(out_result, m->key);
                        if (m->value->type == LSP_JSON_STRING) {
                            lsp_json_write_string(out_result, m->value->u.str_val);
                        } else if (m->value->type == LSP_JSON_NUMBER) {
                            lsp_json_write_int(out_result, m->value->u.num_val.int_val);
                        } else if (m->value->type == LSP_JSON_BOOL) {
                            lsp_json_write_bool(out_result, m->value->u.bool_val);
                        } else if (m->value->type == LSP_JSON_OBJECT) {
                            /* nested range object */
                            lsp_json_write_obj_start(out_result);
                            const LspJsonMember *sub_m = m->value->u.obj_val.first;
                            int sub_f = 1;
                            while (sub_m) {
                                if (!sub_f) lsp_json_write_raw(out_result, ",");
                                sub_f = 0;
                                lsp_json_write_obj_key(out_result, sub_m->key);
                                if (sub_m->value->type == LSP_JSON_OBJECT) {
                                    lsp_json_write_obj_start(out_result);
                                    const LspJsonMember *pos_m = sub_m->value->u.obj_val.first;
                                    int pos_f = 1;
                                    while (pos_m) {
                                        if (!pos_f) lsp_json_write_raw(out_result, ",");
                                        pos_f = 0;
                                        lsp_json_write_obj_key(out_result, pos_m->key);
                                        lsp_json_write_int(out_result, pos_m->value->u.num_val.int_val);
                                        pos_m = pos_m->next;
                                    }
                                    lsp_json_write_obj_end(out_result);
                                } else {
                                    lsp_json_write_int(out_result, sub_m->value->u.num_val.int_val);
                                }
                                sub_m = sub_m->next;
                            }
                            lsp_json_write_obj_end(out_result);
                        }
                        m = m->next;
                    }
                    lsp_json_write_obj_end(out_result);
                }
            }
            lsp_json_write_arr_end(out_result);
        } else if (res_val->type == LSP_JSON_OBJECT) {
            lsp_json_write_obj_start(out_result);
            const LspJsonMember *m = res_val->u.obj_val.first;
            int f = 1;
            while (m) {
                if (!f) lsp_json_write_raw(out_result, ",");
                f = 0;
                lsp_json_write_obj_key(out_result, m->key);
                if (m->value->type == LSP_JSON_STRING) {
                    lsp_json_write_string(out_result, m->value->u.str_val);
                } else if (m->value->type == LSP_JSON_NUMBER) {
                    lsp_json_write_int(out_result, m->value->u.num_val.int_val);
                }
                m = m->next;
            }
            lsp_json_write_obj_end(out_result);
        }
    }

    lsp_json_rpc_free(&resp_msg);
    return 1;
}

void lsp_cache_clear(LspServer *srv)
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

void lsp_cache_put(LspServer *srv, const char *filepath, const char *symbols, const char *diags)
{
    if (!srv || !filepath) return;

    for (CachedFileResult *c = srv->cache; c; c = c->next) {
        if (c->filepath && strcmp(c->filepath, filepath) == 0) {
            if (symbols) {
                if (c->symbols_rexx) free(c->symbols_rexx);
                c->symbols_rexx = lsp_strdup(symbols);
            }
            if (diags) {
                if (c->diagnostics_rexx) free(c->diagnostics_rexx);
                c->diagnostics_rexx = lsp_strdup(diags);
            }
            return;
        }
    }

    CachedFileResult *entry = (CachedFileResult *)malloc(sizeof(CachedFileResult));
    if (!entry) return;

    entry->filepath = lsp_strdup(filepath);
    entry->symbols_rexx = symbols ? lsp_strdup(symbols) : NULL;
    entry->diagnostics_rexx = diags ? lsp_strdup(diags) : NULL;
    entry->next = srv->cache;
    srv->cache = entry;
}

const CachedFileResult *lsp_cache_get(LspServer *srv, const char *filepath)
{
    if (!srv || !filepath) return NULL;
    for (const CachedFileResult *c = srv->cache; c; c = c->next) {
        if (c->filepath && strcmp(c->filepath, filepath) == 0) {
            return c;
        }
    }
    return NULL;
}
