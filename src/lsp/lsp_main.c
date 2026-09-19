#include "lsp_server.h"

#include <proto/exec.h>
#include <proto/dos.h>
#include <dos/dos.h>
#include <stdio.h>

LspServer g_lsp_server;

int main(int argc, char **argv)
{
    memset(&g_lsp_server, 0, sizeof(LspServer));

    char prog_dir[256];
    if (app_get_program_directory(argc, argv, prog_dir, sizeof(prog_dir))) {
        lsp_server_add_search_path(&g_lsp_server, prog_dir);
    }
    lsp_server_add_search_path(&g_lsp_server, "PROGDIR:");

    if (!lsp_broker_init(&g_lsp_server)) {
        fprintf(stderr, "Failed to initialize Commodity broker.\n");
        lsp_server_clear_search_paths(&g_lsp_server);
        return 20;
    }

    if (!lsp_rexx_init(&g_lsp_server)) {
        fprintf(stderr, "Failed to initialize ARexx host port.\n");
        lsp_broker_cleanup(&g_lsp_server);
        lsp_server_clear_search_paths(&g_lsp_server);
        return 20;
    }

    g_lsp_server.running = 1;

    ULONG wait_mask = SIGBREAKF_CTRL_C | g_lsp_server.broker_sigbit | g_lsp_server.rexx_sigbit;

    while (g_lsp_server.running) {
        ULONG received = Wait(wait_mask);

        if (received & SIGBREAKF_CTRL_C) {
            g_lsp_server.running = 0;
            break;
        }

        if (received & g_lsp_server.broker_sigbit) {
            lsp_broker_handle_msg(&g_lsp_server);
        }

        if (received & g_lsp_server.rexx_sigbit) {
            lsp_rexx_handle_msg(&g_lsp_server);
        }
    }

    lsp_rexx_cleanup(&g_lsp_server);
    lsp_broker_cleanup(&g_lsp_server);
    lsp_parser_registry_cleanup(&g_lsp_server);
    lsp_server_clear_search_paths(&g_lsp_server);
    lsp_cache_clear(&g_lsp_server);

    return 0;
}
