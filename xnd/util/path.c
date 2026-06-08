/* path.c */
#include "xnd/xnd.h"
#include "xnd/util/path.h"
#include <string.h>

/**
 * /path/to/file -> /path/to
 */
int xnd_path_dirname(const char *path, char *out, size_t outlen)
{
        size_t  dirlen;
        char    *rslash;

        rslash = strrchr(path, '/');
        if (!rslash) {
                if (!out || !outlen)
                        return -1;
                *out = '\0';
                return 0;
        }

        dirlen = rslash - path;
        if (outlen < dirlen + 1)
                return -1;

        strncpy(out, path, dirlen);
        out[dirlen] = '\0';
        return 0;
}

/**
 * /path/to/file -> file
 */
int xnd_path_basename(const char *path, char *out, size_t outlen)
{
        char *base, *rslash;
        
        rslash = strrchr(path, '/');
        if (rslash)
                base = rslash + 1;
        else
                base = (char *)path;

        if (outlen < strlen(base) + 1)
                return -1;

        strncpy(out, base, strlen(base));
        out[strlen(base)] = '\0';
        return 0;
}

/**
 * file.txt -> file 
 */
int xnd_path_stem(const char *path, char *out, size_t outlen)
{
        size_t  stemlen;
        char    *dot;
        
        dot = strrchr(path, '.');
        if (dot)
                stemlen = dot - path;
        else
                stemlen = strlen(path);

        if (outlen < stemlen + 1)
                return -1;

        strncpy(out, path, stemlen);
        out[stemlen] = '\0';
        return 0;
}

/**
 * file.txt -> txt
 */
int xnd_path_ext(const char *path, char *out, size_t outlen)
{
        char *ext, *dot;

        dot = strrchr(path, '.');
        if (!dot) {
                if (!out || !outlen)
                        return -1;
                *out = '\0';
                return 0;
        }

        ext = dot + 1;
        if (outlen < strlen(ext) + 1)
                return -1;

        strncpy(out, ext, strlen(ext));
        out[strlen(ext)] = '\0';
        return 0;
}

/**
 * xnd_path_join('/usr', 'bin/') -> /usr/bin/
 */
int xnd_path_join(char *out, size_t outlen, const char *a, const char *b)
{
        size_t len;
        
        bzero(out, outlen);
        if (a[strlen(a)] == '/' && b[0] == '/') {
                len = strlen(a) + strlen(b + 1) + sizeof('\0');
                if (outlen < len)
                        return -1;
                strncpy(out, a, strlen(a));
                strncpy(out + strlen(a), b + 1, strlen(b) - 1);
                out[len - 1] = '\0';
        } else if (a[strlen(a)] == '/' || b[0] == '/') {
                len = strlen(a) + strlen(b) + sizeof('\0');
                if (outlen < len)
                        return -1;
                strncpy(out, a, strlen(a));
                strncpy(out + strlen(a), b, strlen(b));
                out[len - 1] = '\0';
        } else {
                len = strlen(a) + sizeof('/') + strlen(b) + sizeof('\0');
                if (outlen < len)
                        return -1;
                strncpy(out, a, strlen(a));
                out[strlen(a)] = '/';
                strncat(out, b, strlen(b));
                out[len - 1] = '\0';
        }

        return 0;
}
