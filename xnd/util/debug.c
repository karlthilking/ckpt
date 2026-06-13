/* debug.c */
#include "xnd/xnd.h"
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <limits.h>
#include <mach-o/dyld.h>

/**
 * dump_debug_script:
 *  Output an lldb startup script to add libxnd.dylib and libxnd's
 *  symbols to debug internals after a restart.
 */
void dump_debug_script(void)
{
        int             fd;
        intptr_t        slide;
        const char      *script_path, *libxnd_path = NULL;
        char            dsym_path[PATH_MAX];

        for (u32 i = 0; i < _dyld_image_count(); i++) {
                const char *name = _dyld_get_image_name(i);
                if (name && strstr(name, "libxnd.dylib")) {
                        slide = _dyld_get_image_vmaddr_slide(i);
                        libxnd_path = name;
                        break;
                }
        }

        if (!libxnd_path) {
                xnd_trace("Failed to find libxnd.dylib image\n");
                return;
        }

        snprintf(dsym_path, sizeof(dsym_path), "%s.dSYM", libxnd_path);
        script_path = "xnd-debug.sh";

        fd = open(script_path, O_WRONLY | O_CREAT | O_TRUNC, 0711);
        if (fd < 0) {
                xnd_error("Couldn't open file to write debug script: %s\n",
                          strerror(errno));
                return;
        }

        dprintf(fd,
                "# script for debugging libxnd:\n"
 		"# usage: lldb -s %s -- ./xnd_run -r <ckpt-file>\n"
 		"target modules add %s\n"
 		"target symbols add %s\n"
 		"target modules load --file libxnd.dylib --slide 0x%lx\n",
		script_path, libxnd_path, dsym_path, slide);
	close(fd);
        xnd_trace("Debug script written to: %s\n", script_path);
}
