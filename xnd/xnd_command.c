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
" --exit\n"
"    Request for a computation to exit\n"
" --kill\n"
"    Kill a computation running under XND\n";

static void send_command(enum xnd_command);
static void usage(void);

int main(int argc, char *argv[])
{
        xnd_log_setup();
        if (argc < 2) {
                usage();
                xnd_log_cleanup();
                exit(0);
        }
        
        connect_to_coord();
        if (strcmp(argv[1], "--checkpoint") == 0) {
                send_command(XND_CKPT_CMD);
        } else if (strcmp(argv[1], "--kill") == 0) {
                send_command(XND_KILL_CMD);
        } else if (strcmp(argv[1], "--exit") == 0) {
                send_command(XND_EXIT_CMD);
        } else {
                xnd_error("Unrecognized command: %s\n", argv[1]);
        }
        
        xnd_log_cleanup();
        disconnect_from_coord();
        exit(0);
}

static void usage(void)
{
        xnd_error("%s", help);
}

static void send_command(enum xnd_command cmd)
{
        int             err;
        struct xnd_msg  msg;

        msg.hdr = XND_COMMAND;
        msg.cmd = cmd;
        
        err = send_msg_to_coord(&msg);
        if (err != 0) {
                xnd_error("Failed to send command to coordinator!\n");
        }
}
