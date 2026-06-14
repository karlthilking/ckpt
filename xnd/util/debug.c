/* debug.c */
#include "xnd/xnd.h"
#include "xnd/platform/exe.h"
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <limits.h>
#include <mach-o/dyld.h>

static __always_inline s32 main_image_index(void)
{
        char *program;

        if ((program = getenv("XND_PROGRAM")) == NULL) {
                xnd_trace("getenv: %s\n", strerror(errno));
                return -1;
        }

        for (s32 i = 0; i < _dyld_image_count(); i++) {
                const char *name = _dyld_get_image_name(i);
                if (name && strstr(name, program))
                        return i;
        }

        return -1;
}

static __always_inline s32 libxnd_image_index(void)
{
        for (s32 i = 0; i < _dyld_image_count(); i++) {
                const char *name = _dyld_get_image_name(i);
                if (name && strstr(name, "libxnd.dylib"))
                        return i;
        }

        return -1;
}

void dump_debug_info(void)
{
        intptr_t        image_slide, libxnd_slide;
        s32             image_idx, libxnd_idx;
        
        if ((image_idx = main_image_index()) < 0) {
                xnd_trace("Failed to generate debug script\n");
                return;
        }

        const char *image_path = _dyld_get_image_name(image_idx);
        image_slide = _dyld_get_image_vmaddr_slide(image_idx);

        if ((libxnd_idx = libxnd_image_index()) < 0) {
                xnd_trace("Failed to generate debug script\n");
                return;
        }

        const char *libxnd_path = _dyld_get_image_name(libxnd_idx);
        libxnd_slide = _dyld_get_image_vmaddr_slide(libxnd_idx);

        xnd_trace("Debug info:\n"
                  "  Main image path:   %s\n"
                  "  Main image slide:  %lx\n"
                  "  libxnd path:       %s\n"
                  "  libxnd slide:      %lx\n",
                  image_path, image_slide, libxnd_path, libxnd_slide);
}
