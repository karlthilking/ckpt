/* xnd_io.h */
#ifndef XND_IO_H
#define XND_IO_H

#include "xnd/xnd.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

ssize_t writeall(int, const void *, size_t);
ssize_t readall(int, void *, size_t);

#ifdef __cplusplus
}
#endif /* __cplusplus */
#endif /* XND_IO_H */
