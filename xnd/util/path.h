/* path.h */
#ifndef XND_PATH_UTIL_H
#define XND_PATH_UTIL_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

int xnd_path_find(const char *, char *, size_t);
int xnd_path_dirname(const char *, char *, size_t);
int xnd_path_basename(const char *, char *, size_t);
int xnd_path_stem(const char *, char *, size_t);
int xnd_path_ext(const char *, char *, size_t);
int xnd_path_join(char *, size_t, const char *, const char *);

#ifdef __cplusplus
}
#endif /* __cplusplus */
#endif /* XND_PATH_UTIL_H */
