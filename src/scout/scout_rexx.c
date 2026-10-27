/**
 * @file scout_rexx.c
 * @brief Scout's ARexx port TINYDEV_SCOUT and its commands.
 */
#include "scout_server.h"
#include "json.h"

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/rexxsyslib.h>
#include <clib/alib_protos.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

int scout_rexx_init(ScoutServer *srv)
{
    if (!srv) return 0;

    srv->RexxSysBase = (struct RxsLib *)OpenLibrary("rexxsyslib.library", 0);
    if (!srv->RexxSysBase) {
        fprintf(stderr, "Failed to open rexxsyslib.library\n");
        return 0;
    }

    /* Check if default port already exists */
    Forbid();
    struct MsgPort *existing = FindPort(SCOUT_AREXX_PORT_NAME);
    if (existing) {
        Permit();
        fprintf(stderr, "ARexx port %s already exists\n", SCOUT_AREXX_PORT_NAME);
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

    srv->rexx_port->mp_Node.ln_Name = (char *)SCOUT_AREXX_PORT_NAME;
    srv->rexx_port->mp_Node.ln_Pri = 0;
    srv->rexx_port->mp_Node.ln_Type = NT_MSGPORT;
    AddPort(srv->rexx_port);
    Permit();

    srv->rexx_sigbit = 1UL << srv->rexx_port->mp_SigBit;
    return 1;
}

/**
 * @brief Answer an ARexx message.
 * @param srv The server.
 * @param rx_msg The message.
 * @param rc The return code (SCOUT_RC_).
 * @param result_str The result string (only with RESULT; may be NULL).
 */
static void reply_rexx(ScoutServer *srv, struct RexxMsg *rx_msg, LONG rc, const char *result_str)
{
    if (!rx_msg) return;

    rx_msg->rm_Result1 = rc;
    rx_msg->rm_Result2 = 0;

    if (result_str && srv->RexxSysBase) {
        rx_msg->rm_Result2 = (LONG)CreateArgstring((char *)result_str, (ULONG)strlen(result_str));
    }

    ReplyMsg((struct Message *)rx_msg);
}

/**
 * @brief Skip white space.
 * @param p The position, moved on.
 */
static void skip_spaces(const char **p)
{
    while (**p && ((unsigned char)**p <= ' ' || **p == '\t')) {
        (*p)++;
    }
}

/**
 * @brief Read the next argument of a command (in quotes if it has spaces).
 * @param p The position, moved behind the argument.
 * @param out The buffer for it.
 * @param out_max Its size.
 * @return Non-zero if there was an argument.
 */
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

/**
 * @brief Write symbols of a parser response as ARexx result, one
 *        "kind line col name" per line (1-based).
 * @param sym_array The array of symbols.
 * @param w The writer.
 */
static void format_symbols_to_rexx(const JsonValue *sym_array, JsonWriter *w)
{
    if (!sym_array || sym_array->type != JSON_ARRAY || !w) return;

    size_t count = json_array_length(sym_array);
    for (size_t i = 0; i < count; i++) {
        const JsonValue *item = json_array_get(sym_array, i);
        if (!item || item->type != JSON_OBJECT) continue;

        const char *name = json_obj_get_string(item, "name", "");
        long kind = json_obj_get_int(item, "kind", 0);
        const JsonValue *range = json_obj_get_obj(item, "range");
        const JsonValue *start = range ? json_obj_get_obj(range, "start") : NULL;
        long line = start ? json_obj_get_int(start, "line", 0) + 1 : 1;
        long col = start ? json_obj_get_int(start, "character", 0) + 1 : 1;

        char buf[256];
        snprintf(buf, sizeof(buf), "%ld %ld %ld %s\n", kind, line, col, name);
        json_write_raw(w, buf);
    }
}

/**
 * @brief Write diagnostics of a parser response as ARexx result, one
 *        "severity line col message" per line (1-based).
 * @param diag_array The array of diagnostics.
 * @param w The writer.
 */
static void format_diags_to_rexx(const JsonValue *diag_array, JsonWriter *w)
{
    if (!diag_array || diag_array->type != JSON_ARRAY || !w) return;

    size_t count = json_array_length(diag_array);
    for (size_t i = 0; i < count; i++) {
        const JsonValue *item = json_array_get(diag_array, i);
        if (!item || item->type != JSON_OBJECT) continue;

        const char *msg = json_obj_get_string(item, "message", "");
        long sev = json_obj_get_int(item, "severity", 1);
        const JsonValue *range = json_obj_get_obj(item, "range");
        const JsonValue *start = range ? json_obj_get_obj(range, "start") : NULL;
        long line = start ? json_obj_get_int(start, "line", 0) + 1 : 1;
        long col = start ? json_obj_get_int(start, "character", 0) + 1 : 1;

        char buf[256];
        snprintf(buf, sizeof(buf), "%ld %ld %ld %s\n", sev, line, col, msg);
        json_write_raw(w, buf);
    }
}

void scout_rexx_handle_msg(ScoutServer *srv)
{
    if (!srv || !srv->rexx_port) return;

    struct RexxMsg *rx_msg;
    while ((rx_msg = (struct RexxMsg *)GetMsg(srv->rexx_port)) != NULL) {
        if (!rx_msg->rm_Args[0]) {
            reply_rexx(srv, rx_msg, SCOUT_RC_WARN, "Empty command");
            continue;
        }

        const char *cmd_line = (const char *)rx_msg->rm_Args[0];
        const char *p = cmd_line;

        char command[64];
        if (!parse_next_arg(&p, command, sizeof(command))) {
            reply_rexx(srv, rx_msg, SCOUT_RC_WARN, "Empty command");
            continue;
        }

        if (strcasecmp(command, "PARSE") == 0) {
            char filepath[512];
            if (!parse_next_arg(&p, filepath, sizeof(filepath))) {
                reply_rexx(srv, rx_msg, SCOUT_RC_ERROR, "Usage: PARSE <filepath>");
                continue;
            }

            char params[1024];
            snprintf(params, sizeof(params), "{\"textDocument\":{\"uri\":\"file://%s\"}}", filepath);

            JsonWriter sym_out, diag_out;
            json_writer_init_alloc(&sym_out, 512);
            json_writer_init_alloc(&diag_out, 512);

            char *err_msg = NULL;
            int ok1 = scout_parser_run_request(srv, filepath, "textDocument/documentSymbol", params, &sym_out, &err_msg);
            if (!ok1) {
                reply_rexx(srv, rx_msg, SCOUT_RC_ERROR, err_msg ? err_msg : "Parser error");
                if (err_msg) free(err_msg);
                json_writer_free(&sym_out);
                json_writer_free(&diag_out);
                continue;
            }

            int ok2 = scout_parser_run_request(srv, filepath, "textDocument/publishDiagnostics", params, &diag_out, &err_msg);
            if (!ok2) {
                reply_rexx(srv, rx_msg, SCOUT_RC_ERROR, err_msg ? err_msg : "Parser error");
                if (err_msg) free(err_msg);
                json_writer_free(&sym_out);
                json_writer_free(&diag_out);
                continue;
            }

            /* Convert JSON arrays to ARexx formatted lines */
            JsonValue *sym_root = json_parse(json_writer_get(&sym_out), NULL);
            JsonValue *diag_root = json_parse(json_writer_get(&diag_out), NULL);

            JsonWriter sym_rexx_w, diag_rexx_w;
            json_writer_init_alloc(&sym_rexx_w, 256);
            json_writer_init_alloc(&diag_rexx_w, 256);

            format_symbols_to_rexx(sym_root, &sym_rexx_w);
            format_diags_to_rexx(diag_root, &diag_rexx_w);

            scout_cache_put(srv, filepath, json_writer_get(&sym_rexx_w), json_writer_get(&diag_rexx_w));

            json_writer_free(&sym_rexx_w);
            json_writer_free(&diag_rexx_w);
            if (sym_root) json_value_free(sym_root);
            if (diag_root) json_value_free(diag_root);
            json_writer_free(&sym_out);
            json_writer_free(&diag_out);

            reply_rexx(srv, rx_msg, SCOUT_RC_OK, "OK");
        }
        else if (strcasecmp(command, "SYMBOLS") == 0) {
            char filepath[512];
            if (!parse_next_arg(&p, filepath, sizeof(filepath))) {
                reply_rexx(srv, rx_msg, SCOUT_RC_ERROR, "Usage: SYMBOLS <filepath>");
                continue;
            }

            const CachedFileResult *c = scout_cache_get(srv, filepath);
            if (c && c->symbols_rexx) {
                reply_rexx(srv, rx_msg, SCOUT_RC_OK, c->symbols_rexx);
            } else {
                /* Run on-demand */
                char params[1024];
                snprintf(params, sizeof(params), "{\"textDocument\":{\"uri\":\"file://%s\"}}", filepath);
                JsonWriter sym_out;
                json_writer_init_alloc(&sym_out, 512);
                char *err_msg = NULL;
                if (scout_parser_run_request(srv, filepath, "textDocument/documentSymbol", params, &sym_out, &err_msg)) {
                    JsonValue *sym_root = json_parse(json_writer_get(&sym_out), NULL);
                    JsonWriter sym_rexx_w;
                    json_writer_init_alloc(&sym_rexx_w, 256);
                    format_symbols_to_rexx(sym_root, &sym_rexx_w);

                    reply_rexx(srv, rx_msg, SCOUT_RC_OK, json_writer_get(&sym_rexx_w));

                    json_writer_free(&sym_rexx_w);
                    if (sym_root) json_value_free(sym_root);
                } else {
                    reply_rexx(srv, rx_msg, SCOUT_RC_ERROR, err_msg ? err_msg : "Symbol query failed");
                    if (err_msg) free(err_msg);
                }
                json_writer_free(&sym_out);
            }
        }
        else if (strcasecmp(command, "DIAGNOSTICS") == 0) {
            char filepath[512];
            if (!parse_next_arg(&p, filepath, sizeof(filepath))) {
                reply_rexx(srv, rx_msg, SCOUT_RC_ERROR, "Usage: DIAGNOSTICS <filepath>");
                continue;
            }

            const CachedFileResult *c = scout_cache_get(srv, filepath);
            if (c && c->diagnostics_rexx) {
                reply_rexx(srv, rx_msg, SCOUT_RC_OK, c->diagnostics_rexx);
            } else {
                char params[1024];
                snprintf(params, sizeof(params), "{\"textDocument\":{\"uri\":\"file://%s\"}}", filepath);
                JsonWriter diag_out;
                json_writer_init_alloc(&diag_out, 512);
                char *err_msg = NULL;
                if (scout_parser_run_request(srv, filepath, "textDocument/publishDiagnostics", params, &diag_out, &err_msg)) {
                    JsonValue *diag_root = json_parse(json_writer_get(&diag_out), NULL);
                    JsonWriter diag_rexx_w;
                    json_writer_init_alloc(&diag_rexx_w, 256);
                    format_diags_to_rexx(diag_root, &diag_rexx_w);

                    reply_rexx(srv, rx_msg, SCOUT_RC_OK, json_writer_get(&diag_rexx_w));

                    json_writer_free(&diag_rexx_w);
                    if (diag_root) json_value_free(diag_root);
                } else {
                    reply_rexx(srv, rx_msg, SCOUT_RC_ERROR, err_msg ? err_msg : "Diagnostic query failed");
                    if (err_msg) free(err_msg);
                }
                json_writer_free(&diag_out);
            }
        }
        else if (strcasecmp(command, "DEFINITION") == 0) {
            char filepath[512];
            char s_line[32];
            char s_col[32];
            if (!parse_next_arg(&p, filepath, sizeof(filepath)) ||
                !parse_next_arg(&p, s_line, sizeof(s_line)) ||
                !parse_next_arg(&p, s_col, sizeof(s_col))) {
                reply_rexx(srv, rx_msg, SCOUT_RC_ERROR, "Usage: DEFINITION <filepath> <line> <column>");
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

            JsonWriter loc_out;
            json_writer_init_alloc(&loc_out, 256);
            char *err_msg = NULL;
            if (scout_parser_run_request(srv, filepath, "textDocument/definition", params, &loc_out, &err_msg)) {
                JsonValue *loc_val = json_parse(json_writer_get(&loc_out), NULL);
                if (loc_val && loc_val->type == JSON_OBJECT) {
                    const char *target_uri = json_obj_get_string(loc_val, "uri", filepath);
                    if (strncmp(target_uri, "file://", 7) == 0) target_uri += 7;
                    const JsonValue *range = json_obj_get_obj(loc_val, "range");
                    const JsonValue *start = range ? json_obj_get_obj(range, "start") : NULL;
                    long t_line = start ? json_obj_get_int(start, "line", 0) + 1 : 1;
                    long t_col = start ? json_obj_get_int(start, "character", 0) + 1 : 1;

                    char res_buf[512];
                    snprintf(res_buf, sizeof(res_buf), "%s %ld %ld", target_uri, t_line, t_col);
                    reply_rexx(srv, rx_msg, SCOUT_RC_OK, res_buf);
                } else {
                    reply_rexx(srv, rx_msg, SCOUT_RC_WARN, "Symbol definition not found");
                }
                if (loc_val) json_value_free(loc_val);
            } else {
                reply_rexx(srv, rx_msg, SCOUT_RC_ERROR, err_msg ? err_msg : "Definition query failed");
                if (err_msg) free(err_msg);
            }
            json_writer_free(&loc_out);
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
                reply_rexx(srv, rx_msg, SCOUT_RC_ERROR, "Usage: COMPLETE <filepath> <line> <col> [prefix]");
                continue;
            }
            parse_next_arg(&p, pfx, sizeof(pfx));

            long l = strtol(s_line, NULL, 10);
            long c = strtol(s_col, NULL, 10);

            char params[1024];
            snprintf(params, sizeof(params),
                     "{\"textDocument\":{\"uri\":\"file://%s\"},\"position\":{\"line\":%ld,\"character\":%ld},\"context\":{\"prefix\":\"%s\"}}",
                     filepath, l > 0 ? l - 1 : 0, c > 0 ? c - 1 : 0, pfx);

            JsonWriter comp_out;
            json_writer_init_alloc(&comp_out, 512);
            char *err_msg = NULL;
            if (scout_parser_run_request(srv, filepath, "textDocument/completion", params, &comp_out, &err_msg)) {
                JsonValue *comp_val = json_parse(json_writer_get(&comp_out), NULL);
                if (comp_val && comp_val->type == JSON_ARRAY) {
                    JsonWriter list_w;
                    json_writer_init_alloc(&list_w, 256);
                    size_t count = json_array_length(comp_val);
                    for (size_t i = 0; i < count; i++) {
                        const JsonValue *item = json_array_get(comp_val, i);
                        const char *label = item ? json_obj_get_string(item, "label", "") : "";
                        if (*label) {
                            if (i > 0) json_write_raw(&list_w, " ");
                            json_write_raw(&list_w, label);
                        }
                    }
                    reply_rexx(srv, rx_msg, SCOUT_RC_OK, json_writer_get(&list_w));
                    json_writer_free(&list_w);
                } else {
                    reply_rexx(srv, rx_msg, SCOUT_RC_OK, "");
                }
                if (comp_val) json_value_free(comp_val);
            } else {
                reply_rexx(srv, rx_msg, SCOUT_RC_ERROR, err_msg ? err_msg : "Completion query failed");
                if (err_msg) free(err_msg);
            }
            json_writer_free(&comp_out);
        }
        else if (strcasecmp(command, "REGISTER_PARSER") == 0) {
            char ext[32];
            char path[512];
            if (!parse_next_arg(&p, ext, sizeof(ext)) || !parse_next_arg(&p, path, sizeof(path))) {
                reply_rexx(srv, rx_msg, SCOUT_RC_ERROR, "Usage: REGISTER_PARSER <extension> <executable_path>");
                continue;
            }

            if (scout_parser_register(srv, ext, path)) {
                reply_rexx(srv, rx_msg, SCOUT_RC_OK, "OK");
            } else {
                reply_rexx(srv, rx_msg, SCOUT_RC_ERROR, "Failed to register parser");
            }
        }
        else if (strcasecmp(command, "ADD_SEARCH_PATH") == 0) {
            char path[512];
            if (!parse_next_arg(&p, path, sizeof(path))) {
                reply_rexx(srv, rx_msg, SCOUT_RC_ERROR, "Usage: ADD_SEARCH_PATH <path>");
                continue;
            }

            if (scout_server_add_search_path(srv, path)) {
                reply_rexx(srv, rx_msg, SCOUT_RC_OK, "OK");
            } else {
                reply_rexx(srv, rx_msg, SCOUT_RC_ERROR, "Failed to add search path (maximum reached or invalid)");
            }
        }
        else if (strcasecmp(command, "STATUS") == 0) {
            char status_buf[512];
            snprintf(status_buf, sizeof(status_buf),
                     "Broker: %s | Port: %s | Enabled: %d | Requests: %d",
                     SCOUT_BROKER_NAME, SCOUT_AREXX_PORT_NAME, srv->enabled, srv->request_count);
            reply_rexx(srv, rx_msg, SCOUT_RC_OK, status_buf);
        }
        else if (strcasecmp(command, "QUIT") == 0) {
            srv->running = 0;
            reply_rexx(srv, rx_msg, SCOUT_RC_OK, "OK");
        }
        else {
            reply_rexx(srv, rx_msg, SCOUT_RC_ERROR, "Unknown command");
        }
    }
}

void scout_rexx_cleanup(ScoutServer *srv)
{
    if (!srv) return;

    if (srv->rexx_port) {
        Forbid();
        struct Message *msg;
        while ((msg = GetMsg(srv->rexx_port)) != NULL) {
            reply_rexx(srv, (struct RexxMsg *)msg, SCOUT_RC_WARN, "Server shutting down");
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
