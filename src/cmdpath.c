/**
 * @file cmdpath.c
 * @brief The command path for the Shells TinyDev starts (Build, Run,
 *        Debug, New Shell).
 *
 * A Shell started with SystemTags() copies the path of the caller's CLI.
 * TinyDev started from the Workbench has no CLI, nor has the build
 * process, so their Shells would only find commands in C: and the current
 * drawer, not those added with "Path ... ADD" in the User-Startup. So
 * TinyDev passes a copy of a path with NP_Path: that of its own CLI when
 * started from a Shell, otherwise that of the Workbench process, which
 * LoadWB gave the path of the Shell that ran it (the Startup-Sequence's,
 * after the User-Startup).
 *
 * The path list is a chain of nodes { BPTR next; BPTR lock; }, allocated
 * with AllocVec() as dos.library does, so the Shell frees it like its own.
 *
 * SystemTags() passes NP_Path on to the CLI it makes instead of copying
 * the caller's path.
 */
#include "editor.h"

#include <dos/dos.h>
#include <dos/dosextens.h>
#include <exec/memory.h>
#include <proto/dos.h>
#include <proto/exec.h>

/** A node of a CLI's path list (cli_CommandDir). */
typedef struct PathNode {
    BPTR next;      /**< the next node (BPTR), 0 at the end */
    BPTR lock;      /**< a lock on the drawer */
} PathNode;

/**
 * @brief Find the path list of a process's CLI.
 * @param p The process, or NULL.
 * @return The first node (BPTR), or 0 if it has no CLI or no path.
 */
static BPTR cli_path(struct Process *p)
{
    struct CommandLineInterface *cli;
    if (p == NULL || p->pr_Task.tc_Node.ln_Type != NT_PROCESS) return 0;
    if ((cli = (struct CommandLineInterface *)BADDR(p->pr_CLI)) == NULL) return 0;
    return cli->cli_CommandDir;
}

/**
 * @brief Copy a path list.
 * @param src The first node (BPTR) of the list.
 * @return The copy (BPTR), 0 if the list is empty or on failure.
 */
static BPTR copy_list(BPTR src)
{
    BPTR first = 0, *link = &first;
    while (src != 0) {
        PathNode *from = (PathNode *)BADDR(src), *node;
        BPTR lock = DupLock(from->lock);
        if (lock == 0 || (node = (PathNode *)AllocVec(sizeof(PathNode), MEMF_PUBLIC)) == NULL) {
            if (lock != 0) UnLock(lock);
            break;
        }
        node->next = 0;
        node->lock = lock;
        *link = MKBADDR(node);
        link = &node->next;
        src = from->next;
    }
    return first;
}

BPTR cmdpath_copy(void)
{
    BPTR path;
    struct Process *self = (struct Process *)FindTask(NULL);
    if (cli_path(self) != 0) return copy_list(cli_path(self));
    /* DupLock() may break the Forbid(); the Workbench's path list does not
       change while it runs, it is set once by LoadWB */
    Forbid();
    path = copy_list(cli_path((struct Process *)FindTask((STRPTR)"Workbench")));
    Permit();
    return path;
}

void cmdpath_free(BPTR path)
{
    while (path != 0) {
        PathNode *node = (PathNode *)BADDR(path);
        path = node->next;
        UnLock(node->lock);
        FreeVec(node);
    }
}
