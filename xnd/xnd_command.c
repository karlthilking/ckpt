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
#include <sys/types.h>
#include <sys/time.h>

static const char *help =
"OVERVIEW: xnd_command\n\n"
"DESCRIPTION: Send a command to a computation under the control of XND\n\n"
"USAGE: ./xnd_command <command>\n\n"
"OPTIONS:\n"
" --checkpoint\n"
"    Send a checkpoint request to a computation\n"
" --kill\n"
"    Kill a computation running under XND\n"
" --help\n"
"    Display this help message\n\n";

#define CHECKPOINT(arg) \
	(strncmp(arg, "--checkpoint", strlen("--checkpoint")) == 0)
#define KILL(arg) \
	(strncmp(arg, "--kill", strlen("--kill")) == 0)
#define HELP(arg) \
	(strncmp(arg, "--help", strlen("--help")) == 0)

static void usage(void);

int
main(int argc, char *argv[])
{
	int ret;
	enum xnd_cmd cmd;

	if (argc < 2 || HELP(argv[1])) {
		usage();
		exit(0);
	}

	if (CHECKPOINT(argv[1])) {
		cmd = XND_CKPT_CMD;
	} else if (KILL(argv[1])) {
		 cmd = XND_KILL_CMD;
	} else {
		usage();
		exit(-1);
	}

	ret = send_command_to_coord(cmd);
	if (ret != 0)
		xnd_error("%s failed\n", xnd_cmd_string(cmd));

	exit(ret);
}

static void
usage(void)
{
        xnd_error("%s", help);
}
