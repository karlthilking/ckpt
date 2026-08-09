/* exe.c */
#include "xnd/xnd.h"
#include "xnd/platform/exe.h"
#include "xnd/util/path.h"
#include <limits.h>
#include <mach-o/dyld.h>

int xnd_exe_path(char *out, size_t outlen)
{
        u32 size = (u32)outlen;

        if (_NSGetExecutablePath(out, &size) < 0)
                return -1;

        return 0;
}

int xnd_exe_dir(char *dst, size_t dstlen)
{
	char path[PATH_MAX];

	if (xnd_exe_path(path, sizeof(path)) < 0 ||
	    xnd_path_dirname(dst, dstlen, path) < 0)
		return -1;

	return 0;
}

int xnd_exe_path_of(char *dst, size_t dstlen, const char *exe)
{
	char dir[PATH_MAX];

	if (xnd_exe_dir(dir, sizeof(dir)) < 0 ||
	    xnd_path_join(dst, dstlen, dir, exe) < 0)
		return -1;

	return 0;
}
