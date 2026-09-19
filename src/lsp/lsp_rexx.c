#include "lsp_server.h"
#include "lsp_json.h"

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/rexxsyslib.h>
#include <clib/alib_protos.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

int lsp_rexx_init(LspServer *srv)
{
    if (!srv) return 0;

    srv->RexxSysBase = (struct RxsLib *)OpenLibrary("rexxsyslib.library", 0);
    if (!srv->RexxSysBase) {
        fprintf(stderr, "Failed to open rexxsyslib.library\n");
        return 0;
    }

    /* Check if default port already exists */
    Forbid();
    struct MsgPort *existing = FindPort(LSP_AREXX_PORT_NAME);
    if (existing) {
        Permit();
        fprintf(stderr, "ARexx port %s already exists\n", LSP_AREXX_PORT_NAME);
        CloseLibrary((struct Library *)srv->RexxSysBase);
        srv->RexxSysBase = NULL;
        return 0;
    }

    srv->rexx_port = CreateMsgPort();
    if (!srv->rexx_port) {
        Permit();
        fprintf(stderr, "Failed to create ARexx MsgPort\n");
        CloseLibrary((struct Library *)srv->RexxSysBase);
        srv->RexxSysBase = NULL;
        return 0;
    }

    srv->rexx_port->mp_Node.ln_Name = (char *)LSP_AREXX_PORT_NAME;
    srv->rexx_port->mp_Node.ln_Pri = 0;
    srv->rexx_port->mp_Node.ln_Type = NT_MSGPORT;
    AddPort(srv->rexx_port);
    Permit();

    srv->rexx_sigbit = 1UL << srv->rexx_port->mp_SigBit;
    return 1;
}

static void reply_rexx(LspServer *srv, struct RexxMsg *rx_msg, LONG rc, const char *result_str)
{
    if (!rx_msg) return;

    rx_msg->rm_Result1 = rc;
    rx_msg->rm_Result2 = 0;

    if (result_str && srv->RexxSysBase) {
        rx_msg->rm_Result2 = (LONG)CreateArgstring((char *)result_str, (ULONG)strlen(result_str));
    }

    ReplyMsg((struct Message *)rx_msg);
}

static void skip_spaces(const char **p)
{
    while (**p && ((unsigned char)**p <= ' ' || **p == '\t')) {
        (*p)++;
    }
}

static int parse_next_arg(const char **p, char *out, size_t out_max)
{
    skip_spaces(p);
    if (!**p) return 0;

    size_t len = 0;
    if (**p == '"') {
        (*p)++;
        while (**p && **p != '"' && len + 1 < out_max) {
            out[len++] = *(*p)++;
        }
        if (**p == '"') (*p)++;
    } else {
        while (**p && (unsigned char)**p > ' ' && len + 1 < out_max) {
            out[len++] = *(*p)++;
        }
    }
    out[len] = '\0';
    return len > 0;
}

static void format_symbols_to_rexx(const LspJsonValue *sym_array, LspJsonWriter *w)
{
    if (!sym_array || sym_array->type != LSP_JSON_ARRAY || !w) return;

    size_t count = lsp_json_array_length(sym_array);
    for (size_t i = 0; i < count; i++) {
        const LspJsonValue *item = lsp_json_array_get(sym_array, i);
        if (!item || item->type != LSP_JSON_OBJECT) continue;

        const char *name = lsp_json_obj_get_string(item, "name", "");
        long kind = lsp_json_obj_get_int(item, "kind", 0);
        const LspJsonValue *range = lsp_json_obj_get_obj(item, "range");
        const LspJsonValue *start = range ? lsp_json_obj_get_obj(range, "start") : NULL;
        long line = start ? lsp_json_obj_get_int(start, "line", 0) + 1 : 1;
        long col = start ? lsp_json_obj_get_int(start, "character", 0) + 1 : 1;

        char buf[256];
        snprintf(buf, sizeof(buf), "%ld %ld %ld %s\n", kind, line, col, name);
        lsp_json_write_raw(w, buf);
    }
}

static void format_diags_to_rexx(const LspJsonValue *diag_array, LspJsonWriter *w)
{
    if (!diag_array || diag_array->type != LSP_JSON_ARRAY || !w) return;

    size_t count = lsp_json_array_length(diag_array);
    for (size_t i = 0; i < count; i++) {
        const LspJsonValue *item = lsp_json_array_get(diag_array, i);
        if (!item || item->type != LSP_JSON_OBJECT) continue;

        const char *msg = lsp_json_obj_get_string(item, "message", "");
        long sev = lsp_json_obj_get_int(item, "severity", 1);
        const LspJsonValue *range = lsp_json_obj_get_obj(item, "range");
        const LspJsonValue *start = range ? lsp_json_obj_get_obj(range, "start") : NULL;
        long line = start ? lsp_json_obj_get_int(start, "line", 0) + 1 : 1;
        long col = start ? lsp_json_obj_get_int(start, "character", 0) + 1 : 1;

        char buf[256];
        snprintf(buf, sizeof(buf), "%ld %ld %ld %s\n", sev, line, col, msg);
        lsp_json_write_raw(w, buf);
    }
}

void lsp_rexx_handle_msg(LspServer *srv)
{
    if (!srv || !srv->rexx_port) return;

    struct RexxMsg *rx_msg;
    while ((rx_msg = (struct RexxMsg *)GetMsg(srv->rexx_port)) != NULL) {
        if (!rx_msg->rm_Args[0]) {
            reply_rexx(srv, rx_msg, LSP_RC_WARN, "Empty command");
            continue;
        }

        const char *cmd_line = (const char *)rx_msg->rm_Args[0];
        const char *p = cmd_line;

        char command[64];
        if (!parse_next_arg(&p, command, sizeof(command))) {
            reply_rexx(srv, rx_msg, LSP_RC_WARN, "Empty command");
            continue;
        }

        if (strcasecmp(command, "PARSE") == 0) {
            char filepath[512];
            if (!parse_next_arg(&p, filepath, sizeof(filepath))) {
                reply_rexx(srv, rx_msg, LSP_RC_ERROR, "Usage: PARSE <filepath>");
                continue;
            }

            char params[1024];
            snprintf(params, sizeof(params), "{\"textDocument\":{\"uri\":\"file://%s\"}}", filepath);

            LspJsonWriter sym_out, diag_out;
            lsp_json_writer_init_alloc(&sym_out, 512);
            lsp_json_writer_init_alloc(&diag_out, 512);

            char *err_msg = NULL;
            int ok1 = lsp_parser_run_request(srv, filepath, "textDocument/documentSymbol", params, &sym_out, &err_msg);
            if (!ok1) {
                reply_rexx(srv, rx_msg, LSP_RC_ERROR, err_msg ? err_msg : "Parser error");
                if (err_msg) free(err_msg);
                lsp_json_writer_free(&sym_out);
                lsp_json_writer_free(&diag_out);
                continue;
            }

            int ok2 = lsp_parser_run_request(srv, filepath, "textDocument/publishDiagnostics", params, &diag_out, &err_msg);
            if (!ok2) {
                reply_rexx(srv, rx_msg, LSP_RC_ERROR, err_msg ? err_msg : "Parser error");
                if (err_msg) free(err_msg);
                lsp_json_writer_free(&sym_out);
                lsp_json_writer_free(&diag_out);
                continue;
            }

            /* Convert JSON arrays to ARexx formatted lines */
            LspJsonValue *sym_root = lsp_json_parse(lsp_json_writer_get(&sym_out), NULL);
            LspJsonValue *diag_root = lsp_json_parse(lsp_json_writer_get(&diag_out), NULL);

            LspJsonWriter sym_rexx_w, diag_rexx_w;
            lsp_json_writer_init_alloc(&sym_rexx_w, 256);
            lsp_json_writer_init_alloc(&diag_rexx_w, 256);

            format_symbols_to_rexx(sym_root, &sym_rexx_w);
            format_diags_to_rexx(diag_root, &diag_rexx_w);

            lsp_cache_put(srv, filepath, lsp_json_writer_get(&sym_rexx_w), lsp_json_writer_get(&diag_rexx_w));

            lsp_json_writer_free(&sym_rexx_w);
            lsp_json_writer_free(&diag_rexx_w);
            if (sym_root) lsp_json_value_free(sym_root);
            if (diag_root) lsp_json_value_free(diag_root);
            lsp_json_writer_free(&sym_out);
            lsp_json_writer_free(&diag_out);

            reply_rexx(srv, rx_msg, LSP_RC_OK, "OK");
        }
        else if (strcasecmp(command, "SYMBOLS") == 0) {
            char filepath[512];
            if (!parse_next_arg(&p, filepath, sizeof(filepath))) {
                reply_rexx(srv, rx_msg, LSP_RC_ERROR, "Usage: SYMBOLS <filepath>");
                continue;
            }

            const CachedFileResult *c = lsp_cache_get(srv, filepath);
            if (c && c->symbols_rexx) {
                reply_rexx(srv, rx_msg, LSP_RC_OK, c->symbols_rexx);
            } else {
                /* Run on-demand */
                char params[1024];
                snprintf(params, sizeof(params), "{\"textDocument\":{\"uri\":\"file://%s\"}}", filepath);
                LspJsonWriter sym_out;
                lsp_json_writer_init_alloc(&sym_out, 512);
                char *err_msg = NULL;
                if (lsp_parser_run_request(srv, filepath, "textDocument/documentSymbol", params, &sym_out, &err_msg)) {
                    LspJsonValue *sym_root = lsp_json_parse(lsp_json_writer_get(&sym_out), NULL);
                    LspJsonWriter sym_rexx_w;
                    lsp_json_writer_init_alloc(&sym_rexx_w, 256);
                    format_symbols_to_rexx(sym_root, &sym_rexx_w);

                    reply_rexx(srv, rx_msg, LSP_RC_OK, lsp_json_writer_get(&sym_rexx_w));

                    lsp_json_writer_free(&sym_rexx_w);
                    if (sym_root) lsp_json_value_free(sym_root);
                } else {
                    reply_rexx(srv, rx_msg, LSP_RC_ERROR, err_msg ? err_msg : "Symbol query failed");
                    if (err_msg) free(err_msg);
                }
                lsp_json_writer_free(&sym_out);
            }
        }
        else if (strcasecmp(command, "DIAGNOSTICS") == 0) {
            char filepath[512];
            if (!parse_next_arg(&p, filepath, sizeof(filepath))) {
                reply_rexx(srv, rx_msg, LSP_RC_ERROR, "Usage: DIAGNOSTICS <filepath>");
                continue;
            }

            const CachedFileResult *c = lsp_cache_get(srv, filepath);
            if (c && c->diagnostics_rexx) {
                reply_rexx(srv, rx_msg, LSP_RC_OK, c->diagnostics_rexx);
            } else {
                char params[1024];
                snprintf(params, sizeof(params), "{\"textDocument\":{\"uri\":\"file://%s\"}}", filepath);
                LspJsonWriter diag_out;
                lsp_json_writer_init_alloc(&diag_out, 512);
                char *err_msg = NULL;
                if (lsp_parser_run_request(srv, filepath, "textDocument/publishDiagnostics", params, &diag_out, &err_msg)) {
                    LspJsonValue *diag_root = lsp_json_parse(lsp_json_writer_get(&diag_out), NULL);
                    LspJsonWriter diag_rexx_w;
                    lsp_json_writer_init_alloc(&diag_rexx_w, 256);
                    format_diags_to_rexx(diag_root, &diag_rexx_w);

                    reply_rexx(srv, rx_msg, LSP_RC_OK, lsp_json_writer_get(&diag_rexx_w));

                    lsp_json_writer_free(&diag_rexx_w);
                    if (diag_root) lsp_json_value_free(diag_root);
                } else {
                    reply_rexx(srv, rx_msg, LSP_RC_ERROR, err_msg ? err_msg : "Diagnostic query failed");
                    if (err_msg) free(err_msg);
                }
                lsp_json_writer_free(&diag_out);
            }
        }
        else if (strcasecmp(command, "DEFINITION") == 0) {
            char filepath[512];
            char s_line[32];
            char s_col[32];
            if (!parse_next_arg(&p, filepath, sizeof(filepath)) ||
                !parse_next_arg(&p, s_line, sizeof(s_line)) ||
                !parse_next_arg(&p, s_col, sizeof(s_col))) {
                reply_rexx(srv, rx_msg, LSP_RC_ERROR, "Usage: DEFINITION <filepath> <line> <column>");
                continue;
            }

            long l = strtol(s_line, NULL, 10);
            long c = strtol(s_col, NULL, 10);
            char sym_name[128];
            sym_name[0] = '\0';
            parse_next_arg(&p, sym_name, sizeof(sym_name));

            char params[1024];
            if (sym_name[0]) {
                snprintf(params, sizeof(params),
                         "{\"textDocument\":{\"uri\":\"file://%s\"},\"position\":{\"line\":%ld,\"character\":%ld},\"symbol\":\"%s\"}",
                         filepath, l > 0 ? l - 1 : 0, c > 0 ? c - 1 : 0, sym_name);
            } else {
                snprintf(params, sizeof(params),
                         "{\"textDocument\":{\"uri\":\"file://%s\"},\"position\":{\"line\":%ld,\"character\":%ld}}",
                         filepath, l > 0 ? l - 1 : 0, c > 0 ? c - 1 : 0);
            }

            LspJsonWriter loc_out;
            lsp_json_writer_init_alloc(&loc_out, 256);
            char *err_msg = NULL;
            if (lsp_parser_run_request(srv, filepath, "textDocument/definition", params, &loc_out, &err_msg)) {
                LspJsonValue *loc_val = lsp_json_parse(lsp_json_writer_get(&loc_out), NULL);
                if (loc_val && loc_val->type == LSP_JSON_OBJECT) {
                    const char *target_uri = lsp_json_obj_get_string(loc_val, "uri", filepath);
                    if (strncmp(target_uri, "file://", 7) == 0) target_uri += 7;
                    const LspJsonValue *range = lsp_json_obj_get_obj(loc_val, "range");
                    const LspJsonValue *start = range ? lsp_json_obj_get_obj(range, "start") : NULL;
                    long t_line = start ? lsp_json_obj_get_int(start, "line", 0) + 1 : 1;
                    long t_col = start ? lsp_json_obj_get_int(start, "character", 0) + 1 : 1;

                    char res_buf[512];
                    snprintf(res_buf, sizeof(res_buf), "%s %ld %ld", target_uri, t_line, t_col);
                    reply_rexx(srv, rx_msg, LSP_RC_OK, res_buf);
                } else {
                    reply_rexx(srv, rx_msg, LSP_RC_WARN, "Symbol definition not found");
                }
                if (loc_val) lsp_json_value_free(loc_val);
            } else {
                reply_rexx(srv, rx_msg, LSP_RC_ERROR, err_msg ? err_msg : "Definition query failed");
                if (err_msg) free(err_msg);
            }
            lsp_json_writer_free(&loc_out);
        }
        else if (strcasecmp(command, "COMPLETE") == 0) {
            char filepath[512];
            char s_line[32];
            char s_col[32];
            char pfx[128];
            pfx[0] = '\0';

            if (!parse_next_arg(&p, filepath, sizeof(filepath)) ||
                !parse_next_arg(&p, s_line, sizeof(s_line)) ||
                !parse_next_arg(&p, s_col, sizeof(s_col))) {
                reply_rexx(srv, rx_msg, LSP_RC_ERROR, "Usage: COMPLETE <filepath> <line> <col> [prefix]");
                continue;
            }
            parse_next_arg(&p, pfx, sizeof(pfx));

            long l = strtol(s_line, NULL, 10);
            long c = strtol(s_col, NULL, 10);

            char params[1024];
            snprintf(params, sizeof(params),
                     "{\"textDocument\":{\"uri\":\"file://%s\"},\"position\":{\"line\":%ld,\"character\":%ld},\"context\":{\"prefix\":\"%s\"}}",
                     filepath, l > 0 ? l - 1 : 0, c > 0 ? c - 1 : 0, pfx);

            LspJsonWriter comp_out;
            lsp_json_writer_init_alloc(&comp_out, 512);
            char *err_msg = NULL;
            if (lsp_parser_run_request(srv, filepath, "textDocument/completion", params, &comp_out, &err_msg)) {
                LspJsonValue *comp_val = lsp_json_parse(lsp_json_writer_get(&comp_out), NULL);
                if (comp_val && comp_val->type == LSP_JSON_ARRAY) {
                    LspJsonWriter list_w;
                    lsp_json_writer_init_alloc(&list_w, 256);
                    size_t count = lsp_json_array_length(comp_val);
                    for (size_t i = 0; i < count; i++) {
                        const LspJsonValue *item = lsp_json_array_get(comp_val, i);
                        const char *label = item ? lsp_json_obj_get_string(item, "label", "") : "";
                        if (*label) {
                            if (i > 0) lsp_json_write_raw(&list_w, " ");
                            lsp_json_write_raw(&list_w, label);
                        }
                    }
                    reply_rexx(srv, rx_msg, LSP_RC_OK, lsp_json_writer_get(&list_w));
                    lsp_json_writer_free(&list_w);
                } else {
                    reply_rexx(srv, rx_msg, LSP_RC_OK, "");
                }
                if (comp_val) lsp_json_value_free(comp_val);
            } else {
                reply_rexx(srv, rx_msg, LSP_RC_ERROR, err_msg ? err_msg : "Completion query failed");
                if (err_msg) free(err_msg);
            }
            lsp_json_writer_free(&comp_out);
        }
        else if (strcasecmp(command, "REGISTER_PARSER") == 0) {
            char ext[32];
            char path[512];
            if (!parse_next_arg(&p, ext, sizeof(ext)) || !parse_next_arg(&p, path, sizeof(path))) {
                reply_rexx(srv, rx_msg, LSP_RC_ERROR, "Usage: REGISTER_PARSER <extension> <executable_path>");
                continue;
            }

            if (lsp_parser_register(srv, ext, path)) {
                reply_rexx(srv, rx_msg, LSP_RC_OK, "OK");
            } else {
                reply_rexx(srv, rx_msg, LSP_RC_ERROR, "Failed to register parser");
            }
        }
        else if (strcasecmp(command, "STATUS") == 0) {
            char status_buf[512];
            snprintf(status_buf, sizeof(status_buf),
                     "Broker: %s | Port: %s | Enabled: %d | Requests: %d",
                     LSP_BROKER_NAME, LSP_AREXX_PORT_NAME, srv->enabled, srv->request_count);
            reply_rexx(srv, rx_msg, LSP_RC_OK, status_buf);
        }
        else if (strcasecmp(command, "QUIT") == 0) {
            srv->running = 0;
            reply_rexx(srv, rx_msg, LSP_RC_OK, "OK");
        }
        else {
            reply_rexx(srv, rx_msg, LSP_RC_ERROR, "Unknown command");
        }
    }
}

void lsp_rexx_cleanup(LspServer *srv)
{
    if (!srv) return;

    if (srv->rexx_port) {
        Forbid();
        struct Message *msg;
        while ((msg = GetMsg(srv->rexx_port)) != NULL) {
            reply_rexx(srv, (struct RexxMsg *)msg, LSP_RC_WARN, "Server shutting down");
        }
        RemPort(srv->rexx_port);
        DeleteMsgPort(srv->rexx_port);
        srv->rexx_port = NULL;
        Permit();
    }

    if (srv->RexxSysBase) {
        CloseLibrary((struct Library *)srv->RexxSysBase);
        srv->RexxSysBase = NULL;
    }
}
