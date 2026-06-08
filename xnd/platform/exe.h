/* exe.h */
#ifndef XND_EXE_H
#define XND_EXE_H

#include "xnd/xnd.h"

int xnd_exe_path(char *, size_t);
int xnd_exe_dir(char *, size_t);
int xnd_exe_path_of(const char *, char *, size_t);

#endif /* XND_EXE_H */
