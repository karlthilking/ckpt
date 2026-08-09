/* path.h */
#ifndef XND_PATH_UTIL_H
#define XND_PATH_UTIL_H

#define xnd_path_basename_inplace(path) \
	(__builtin_strrchr(path, '/') ? \
	 __builtin_strrchr(path, '/') + 1 : path)

#define xnd_path_ext_inplace(path) \
	(__builtin_strrchr(path, '.') ? \
	 __builtin_strrchr(path, '.') + 1 : "")

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

int xnd_path_find(const char *, char *, size_t);
int xnd_path_dirname(char *, size_t, const char *);
int xnd_path_basename(char *, size_t, const char *);
int xnd_path_stem(char *, size_t, const char *);
int xnd_path_ext(char *, size_t, const char *);
int xnd_path_join(char *, size_t, const char *, const char *);

#ifdef __cplusplus
}
#endif /* __cplusplus */
#endif /* XND_PATH_UTIL_H */
