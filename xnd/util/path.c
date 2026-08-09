/* path.c */
#include "xnd/xnd.h"
#include "xnd/util/path.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

int xnd_path_find(const char *file, char *out, size_t outlen)
{
        char    *path_env, *endp, buf[PATH_MAX];
        size_t   len, namelen = strlen(file);
        bool     next = true, found = false;

        if ((path_env = getenv("PATH")) == NULL)
                return -1;

        while (next) {
                bzero(buf, sizeof(buf));
                if ((endp = strchr(path_env, ':')) == NULL) {
                        next = false;
                        endp = path_env + strlen(path_env);
                }

                len = endp - path_env;
                strncpy(buf, path_env, len);
                buf[len] = '/';
                strncat(buf, file, namelen);
                buf[len + 1 + namelen] = '\0';

                /* Found file path */
                if (access(buf, F_OK) == 0) {
                        found = true;
                        break;
                }

                /* Advance to next path in PATH */
                path_env = endp + 1;
        }

        if (found) {
                strncpy(out, buf, outlen);
                return 0;
        }

        return -1;
}

/*
 * xnd_path_dirname:
 *  Extract directory path from file path.
 *  (e.g. /home/user/xnd/libxnd.dylib -> /home/usr/xnd)
 */
int xnd_path_dirname(char *dst, size_t dstlen, const char *path)
{
	const char *delim, *src = path;
	size_t srclen;

	if (dst == NULL || dstlen == 0)
		return -1;

	delim = strrchr(path, '/');
	if (delim == NULL) {
		*dst = '\0';
		return 0;
	}

	srclen = delim - src;
	if (dstlen <= srclen)
		return -1;

	strlcpy(dst, src, srclen + 1);
	return 0;
}

/*
 * xnd_path_basename:
 *  Extract basename from file path.
 *  (e.g. /usr/bin/uname -> uname)
 */
int xnd_path_basename(char *dst, size_t dstlen, const char *path)
{
	const char *src;
	size_t srclen;

	src = xnd_path_basename_inplace(path);
	srclen = strlen(src);
	if (dstlen <= srclen)
		return -1;

	strlcpy(dst, src, srclen + 1);
	return 0;
}

/*
 * xnd_path_stem:
 *  Extract filename stem from filename or pathname.
 *  (e.g. /usr/lib/libhello.dylib -> libhello)
 */
int xnd_path_stem(char *out, size_t outlen, const char *path)
{
	const char *src;
	char *dot;
	size_t len;

	dot = strrchr(path, '.');
	src = (strrchr(path, '/') ? strrchr(path, '/') + 1 : path);
	len = (dot != NULL ? dot - src : strlen(src));

	if (len >= outlen)
		return -1;

	strlcpy(out, src, len + 1);
	return 0;
}

/*
 * xnd_path_ext:
 *  Extract the extension from a pathname.
 *  (e.g. /path/to/file.txt -> txt)
 */
int xnd_path_ext(char *out, size_t outlen, const char *path)
{
	char *ext, *dot;

	if (out == NULL || outlen == 0)
		return -1;

	dot = strrchr(path, '.');
	if (dot == NULL) {
		*out = '\0';
		return 0;
	}

	ext = dot + 1;
	if (strlcpy(out, ext, outlen) >= outlen)
		return -1;

	return 0;
}

/*
 * xnd_path_join:
 *  Concatenate left hand side, l, with right hand side, r, and store
 *  path string in out.
 */
int xnd_path_join(char *out, size_t outlen, const char *l, const char *r)
{
	size_t len, llen, rlen;

	if (l == NULL || r == NULL)
		return -1;

	llen = strlen(l);
	rlen = strlen(r);

	if (l[llen - 1] == '/' && r[0] == '/') {
		/*
		 * Truncate path delimiter from l when copying l to out,
		 * then concatenate out with r.
		 */
		len = llen - sizeof('/') + rlen + sizeof('\0');
		if (outlen < len)
			return -1;
		strlcpy(out, l, llen);
	} else if (l[llen - 1] == '/' || r[0] == '/') {
		len = llen + rlen + sizeof('\0');
		if (outlen < len)
			return -1;
		strlcpy(out, l, llen + 1);
	} else {
		len = llen + sizeof('/') + rlen + sizeof('\0');
		if (outlen < len)
			return -1;
		strlcpy(out, l, llen + 1);
		strlcat(out, "/", len);
	}

	strlcat(out, r, len);
	return 0;
}
