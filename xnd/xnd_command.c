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

static void send_command(enum xnd_command);

int main(int argc, char *argv[])
{
        xnd_log_setup();
        if (argc < 2) {
                xnd_error("Usage: ./xnd_command <command>");
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

        exit(0);
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
