/* xnd_coord_common.c */
#include "xnd_coord_api.h"

const char *
xnd_msghdr_string_nonconst(enum xnd_msghdr hdr)
{
	static const char *xnd_msghdr_list[] = {
		[XND_CONNECT_LAUNCH] = "XND_CONNECT_LAUNCH",
		[XND_CONNECT_RESTART] = "XND_CONNECT_RESTART",
		[XND_EXIT] = "XND_EXIT",
		[XND_ATFORK_PREPARE] = "XND_ATFORK_PREPARE",
		[XND_ATFORK_CHILD] = "XND_ATFORK_CHILD",
		[XND_ATFORK_FAILED] = "XND_ATFORK_FAILED",
		[XND_COMMAND] = "XND_COMMAND",
		[XND_COORD_ACK] = "XND_COORD_ACK",
		[XND_CLIENT_ACK] = "XND_CLIENT_ACK",
		[XND_VIRT_TO_REAL] = "XND_VIRT_TO_REAL",
		[XND_REAL_TO_VIRT] = "XND_REAL_TO_VIRT",
		[XND_CKPT_REQUEST] = "XND_CKPT_REQUEST",
		[XND_CKPT_READY] = "XND_CKPT_READY",
		[XND_CKPT_START] = "XND_CKPT_START",
		[XND_CKPT_DONE] = "XND_CKPT_DONE",
		[XND_RESUME_AFTER_CKPT] = "XND_RESUME_AFTER_CKPT",
		[XND_RESTART] = "XND_RESTART",
		[XND_RESUME_AFTER_RESTART] = "XND_RESUME_AFTER_RESTART",
	};

	if (hdr >= XND_MSGHDR_MIN && hdr <= XND_MSGHDR_MAX)
		return xnd_msghdr_list[hdr];

	return "";
}

const char *
xnd_cmd_string_nonconst(enum xnd_cmd cmd)
{
	static const char *xnd_cmd_list[] = {
		[XND_NULL_CMD] = "XND_NULL_CMD",
		[XND_CKPT_CMD] = "XND_CKPT_CMD",
		[XND_EXIT_CMD] = "XND_EXIT_CMD",
		[XND_KILL_CMD] = "XND_KILL_CMD",
	};

	if (cmd >= XND_CMD_MIN && cmd <= XND_CMD_MAX)
		return xnd_cmd_list[cmd];

	return "";
}
