/* xnd_command.c */
#include "xnd/xnd.h"
#include "xnd/util/io.h"
#include "xnd/util/log.h"
#include "xnd/coordinator/xnd_coord_api.h"
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>

static const char *help =
"OVERVIEW: xnd_command\n\n"
"DESCRIPTION: Send a command to a computation under the control of XND\n\n"
"USAGE: ./xnd_command <command>\n\n"
"OPTIONS:\n"
" --checkpoint\n"
"    Send a checkpoint request to a computation\n"
" --kill\n"
"    Kill a computation running under XND\n";

static void usage(void);

int main(int argc, char *argv[])
{
        int             err;
        enum xnd_cmd    cmd;

        xnd_log_setup();
        if (argc < 2) {
                usage();
                goto out;
        }

        if (strcmp(argv[1], "--checkpoint") == 0) {
                cmd = XND_CKPT_CMD;
        } else if (strcmp(argv[1], "--kill") == 0) {
                cmd = XND_KILL_CMD;
        } else {
                usage();
                goto out;
        }
        
        if ((err = send_command_to_coord(cmd)) != 0) {
                xnd_error("Command failed: %s\n", xnd_cmd_string(cmd));
                goto out;
        }

out:
        xnd_log_cleanup();
        exit(XND_EXIT_SUCCESS);
}

static void usage(void)
{
        xnd_error("%s", help);
}
