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

int xnd_exe_dir(char *out, size_t outlen)
{
        char path[PATH_MAX];

        if (xnd_exe_path(path, sizeof(path)) < 0 ||
            xnd_path_dirname(path, out, outlen) < 0)
                return -1;

        return 0;
}

int xnd_exe_path_of(const char *exe, char *out, size_t outlen)
{
        char dir[PATH_MAX];

        if (xnd_exe_dir(dir, sizeof(dir)) < 0 ||
            xnd_path_join(out, outlen, dir, exe) < 0)
                return -1;

        return 0;
}
