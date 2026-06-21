/* xnd_io.h */
#ifndef XND_IO_H
#define XND_IO_H

#include "xnd/xnd.h"

ssize_t writeall(int, const void *, size_t);
ssize_t readall(int, void *, size_t);

#endif /* XND_IO_H */
