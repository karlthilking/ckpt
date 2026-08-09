/* debug.c */
#include "xnd/xnd.h"
#include "xnd/xnd_lib.h"
#include "xnd/platform/exe.h"
#include "xnd/util/env.h"
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <limits.h>
#include <mach-o/dyld.h>

static inline s32 main_image_index(void)
{
	const char *name = NULL;
	char *program = env_get_program_name();

	for (s32 i = 0; i < _dyld_image_count(); i++) {
		name = _dyld_get_image_name(i);
		if (name == NULL)
			continue;
		if (strstr(name, program) || strstr(program, name))
			return i;
	}

	return -1;
}

static inline s32 libxnd_image_index(void)
{
	const char *name = NULL;
	const u32 count = _dyld_image_count();

	for (s32 i = 0; i < count; i++) {
		name = _dyld_get_image_name(i);
		if (name != NULL && strstr(name, "libxnd.dylib"))
			return i;
	}

	return -1;
}

void dump_debug_info(void)
{
	s32 image_idx, libxnd_idx;
	intptr_t image_slide = -1, libxnd_slide = -1;
	const char *image_name = NULL, *libxnd_name = NULL;

	image_idx = main_image_index();
	if (image_idx != -1) {
		image_name = _dyld_get_image_name(image_idx);
		image_slide = _dyld_get_image_vmaddr_slide(image_idx);
	}

	libxnd_idx = libxnd_image_index();
	if (libxnd_idx != -1) {
		libxnd_name = _dyld_get_image_name(libxnd_idx);
		libxnd_slide = _dyld_get_image_vmaddr_slide(libxnd_idx);
	}

	xnd_trace("Debug info:\n"
		  " main image index: %d\n"
		  "  main image name: %s\n"
		  " main image slide: 0x%lx\n"
		  "     libxnd index: %d\n"
		  "      libxnd name: %s\n"
		  "     libxnd slide: 0x%lx\n",
		  image_idx, image_name, image_slide,
		  libxnd_idx, libxnd_name, libxnd_slide);
}
