#include "scout_server.h"

#include <proto/exec.h>
#include <proto/dos.h>
#include <dos/dos.h>
#include <stdio.h>

ScoutServer g_scout_server;

int main(int argc, char **argv)
{
    memset(&g_scout_server, 0, sizeof(ScoutServer));

    char prog_dir[256];
    if (app_get_program_directory(argc, argv, prog_dir, sizeof(prog_dir))) {
        scout_server_add_search_path(&g_scout_server, prog_dir);
    }
    scout_server_add_search_path(&g_scout_server, "PROGDIR:");

    if (!scout_broker_init(&g_scout_server)) {
        fprintf(stderr, "Failed to initialize Commodity broker.\n");
        scout_server_clear_search_paths(&g_scout_server);
        return 20;
    }

    if (!scout_rexx_init(&g_scout_server)) {
        fprintf(stderr, "Failed to initialize ARexx host port.\n");
        scout_broker_cleanup(&g_scout_server);
        scout_server_clear_search_paths(&g_scout_server);
        return 20;
    }

    g_scout_server.running = 1;

    ULONG wait_mask = SIGBREAKF_CTRL_C | g_scout_server.broker_sigbit | g_scout_server.rexx_sigbit;

    while (g_scout_server.running) {
        ULONG received = Wait(wait_mask);

        if (received & SIGBREAKF_CTRL_C) {
            g_scout_server.running = 0;
            break;
        }

        if (received & g_scout_server.broker_sigbit) {
            scout_broker_handle_msg(&g_scout_server);
        }

        if (received & g_scout_server.rexx_sigbit) {
            scout_rexx_handle_msg(&g_scout_server);
        }
    }

    scout_rexx_cleanup(&g_scout_server);
    scout_broker_cleanup(&g_scout_server);
    scout_parser_registry_cleanup(&g_scout_server);
    scout_server_clear_search_paths(&g_scout_server);
    scout_cache_clear(&g_scout_server);

    return 0;
}
