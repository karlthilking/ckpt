/* xnd_command.c */
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <sys/types.h>
#include <sys/time.h>

#include "xnd.h"
#include "util/io.h"
#include "coordinator/xnd_coord_api.h"
#include "coordinator/xnd_coord_common.h"

static const char *help =
"OVERVIEW: xnd_command\n\n"
"DESCRIPTION: Send a command to a computation under the control of XND\n\n"
"USAGE: ./xnd_command <command>\n\n"
"OPTIONS:\n"
" -c, --checkpoint\n"
"    Send a checkpoint request to a computation\n"
" -k, --kill\n"
"    Kill a computation running under XND\n"
" -t, --timeout SECONDS\n"
"   Specify timeout for sending/receiving coordinator messages\n"
"   A timeout value of 0 will omit a timeout (default: 10)\n"
" -i, --ckpt-interval SECONDS\n"
"   Change current checkpoint interval to SECONDS. If the checkpoint\n"
"   interval parameter is set to 0, automatic checkpoints will be\n"
"   disabled.\n"
" -h, --help\n"
"    Display this help message\n\n";

#define XND_COMMAND_DEFAULT_TIMEOUT 10

#define CHECKPOINT(arg) \
	(strncmp(arg, "-c", 2) == 0 || \
	 strncmp(arg, "--checkpoint", strlen("--checkpoint")) == 0)
#define KILL(arg) \
	(strncmp(arg, "-k", 2) == 0 || \
	 strncmp(arg, "--kill", strlen("--kill")) == 0)
#define TIMEOUT(arg) \
	(strncmp(arg, "-t", 2) == 0 || \
	 strncmp(arg, "--timeout", strlen("--timeout")) == 0)
#define CKPT_INTERVAL(arg) \
	(strncmp(arg, "-i", 2) == 0 || \
	 strncmp(arg, "--ckpt-interval", strlen("--ckpt-interval")) == 0)
#define HELP(arg) \
	(strncmp(arg, "-h", 2) == 0 || \
	 strncmp(arg, "--help", strlen("--help")) == 0)

static void usage_and_exit(int);

int
main(int argc, char *argv[])
{
	bool exited;
	int ret, interval, timeout = XND_COMMAND_DEFAULT_TIMEOUT;
	struct xnd_msg msg = {0};
	enum xnd_cmd cmd = XND_NULL_CMD;
	const char *cmd_str = NULL;

	if (argc < 2)
		usage_and_exit(XND_EXIT_SUCCESS);

#define shift argc--; argv++
	shift;
	while (argc) {
		if (CHECKPOINT(argv[0])) {
			cmd = XND_CKPT_CMD;
			shift;
		} else if (KILL(argv[0])) {
			cmd = XND_KILL_CMD;
			shift;
		} else if (TIMEOUT(argv[0])) {
			if (argv[1] == NULL) {
				xnd_error("timeout value is missing\n");
				usage_and_exit(XND_EXIT_FAILURE);
			}
			timeout = atoi(argv[1]);
			shift; shift;
		} else if (CKPT_INTERVAL(argv[0])) {
			cmd = XND_CKPT_INTERVAL_CMD;
			if (argv[1] == NULL) {
				xnd_error("ckpt interval is missing\n");
				usage_and_exit(XND_EXIT_FAILURE);
			}
			interval = atoi(argv[1]);
			shift; shift;
		} else if (HELP(argv[0])) {
			usage_and_exit(XND_EXIT_SUCCESS);
		} else {
			xnd_printf("unrecognized argument: %s\n", argv[0]);
			usage_and_exit(XND_EXIT_FAILURE);
		}
	}

	if (cmd == XND_NULL_CMD) {
		xnd_error("command argument is missing\n");
		usage_and_exit(XND_EXIT_FAILURE);
	}

	if (!coord_socket_exists()) {
		xnd_error("coordinator is not running\n");
		exit(XND_EXIT_FAILURE);
	}

	msg.hdr = XND_COMMAND;
	msg.cmd = cmd;
	if (msg.cmd == XND_CKPT_INTERVAL_CMD)
		msg.ckpt_interval = interval;

	cmd_str = xnd_cmd_string(cmd);
	ret = send_command_to_coord(&msg, timeout, &exited);
	if (ret != 0 && exited) {
		xnd_warn("command timed out: %s\n", cmd_str);
		exit(XND_EXIT_FAILURE);
	} else if (ret != 0) {
		xnd_error("command failed: %s\n", cmd_str);
		exit(XND_EXIT_FAILURE);
	}

	exit(XND_EXIT_SUCCESS);
}

static void
usage_and_exit(int status)
{
	xnd_printf("%s", help);
	exit(status);
}
