/* exe.h */
#ifndef XND_EXE_H
#define XND_EXE_H

#include "xnd/xnd.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

int xnd_exe_path(char *, size_t);
int xnd_exe_dir(char *, size_t);
int xnd_exe_path_of(char *, size_t, const char *);

#ifdef __cplusplus
}
#endif /* __cplusplus */
#endif /* XND_EXE_H */
