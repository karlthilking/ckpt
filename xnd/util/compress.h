/* compress.h */
#ifndef XND_COMPRESS_H
#define XND_COMPRESS_H

#include "xnd/xnd.h"

#define CHUNK 16384

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

int xnd_compress_ckpt(int, char *);
int xnd_decompress_ckpt(int, char *);

#ifdef __cplusplus
}
#endif /* __cplusplus */
#endif /* XND_COMPRESS_H */
