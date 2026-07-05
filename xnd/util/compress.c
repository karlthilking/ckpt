/* compress.c */
#include "xnd/xnd.h"
#include "xnd/ckptfile.h"
#include "compress.h"
#include "io.h"
#include <zlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

static inline int do_compress(int dstfd, int srcfd)
{
        int             err, flush;
        ulong           have, bytes;
        z_stream        stream;
        uchar           inbuf[CHUNK], outbuf[CHUNK];

        stream.zalloc = Z_NULL;
        stream.zfree = Z_NULL;
        stream.opaque = Z_NULL;

        err = deflateInit(&stream, Z_BEST_SPEED);
        if (err != Z_OK) {
                return -1;
        }

        do {
                bytes = read(srcfd, inbuf, CHUNK);
                switch (bytes) {
                case -1:
                        xnd_error("read: %s\n", strerror(errno));
                        goto bad;
                case 0:
                        flush = Z_FINISH;
                        break;
                default:
                        flush = Z_NO_FLUSH;
                        break;
                }

                stream.avail_in = bytes;
                stream.next_in = inbuf;
                do {
                        stream.avail_out = CHUNK;
                        stream.next_out = outbuf;
                        err = deflate(&stream, flush);
                        xnd_assert(err != Z_STREAM_ERROR);

                        have = CHUNK - stream.avail_out;
                        bytes = writeall(dstfd, outbuf, have);
                        if (bytes != have) {
                                goto bad;
                        }
                } while (stream.avail_out == 0);
                xnd_assert(stream.avail_in == 0);
        } while (flush != Z_FINISH);
        xnd_assert(err == Z_STREAM_END);

        deflateEnd(&stream);
        return 0;
bad:
        deflateEnd(&stream);
        return -1;
}

static inline int do_decompress(int dstfd, int srcfd)
{
        int             err;
        ulong           have, bytes;
        z_stream        stream;
        uchar           inbuf[CHUNK], outbuf[CHUNK];

        stream.zalloc = Z_NULL;
        stream.zfree = Z_NULL;
        stream.opaque = Z_NULL;
        stream.avail_in = 0;
        stream.next_in = Z_NULL;

        err = inflateInit(&stream);
        if (err != Z_OK) {
                return -1;
        }

        do {
                bytes = read(srcfd, inbuf, CHUNK);
                switch (bytes) {
                case -1:
                        xnd_error("read: %s\n", strerror(errno));
                        goto bad;
                case 0:
                        goto done;
                default:
                        break;
                }
                
                stream.avail_in = bytes;
                stream.next_in = inbuf;
                do {
                        stream.avail_out = CHUNK;
                        stream.next_out = outbuf;

                        switch ((err = inflate(&stream, Z_NO_FLUSH))) {
                        case Z_STREAM_ERROR:
                        case Z_NEED_DICT:
                                xnd_assert(false);
                        case Z_DATA_ERROR:
                        case Z_MEM_ERROR:
                                goto bad;
                        default:
                                break;
                        }

                        have = CHUNK - stream.avail_out;
                        bytes = writeall(dstfd, outbuf, have);
                        if (bytes != have) {
                                goto bad;
                        }
                } while (stream.avail_out == 0);
        } while (err != Z_STREAM_END);

done:
        inflateEnd(&stream);
        return 0;
bad:
        inflateEnd(&stream);
        return -1;
}

int xnd_compress_ckpt(int dirfd, char *ckptfile)
{
        int     srcfd = -1, dstfd = -1;
        char    buf[XND_CKPTFILE_MAXLEN];

        srcfd = openat(dirfd, ckptfile, O_RDONLY);
        if (srcfd < 0) {
                xnd_error("openat(%s): %s\n", buf, strerror(errno));
                goto bad;
        }

        snprintf(buf, sizeof(buf), "%s" XND_COMPRESSED_SUFFIX, ckptfile);
        dstfd = openat(dirfd, buf, O_WRONLY | O_CREAT, 0666);
        if (dstfd < 0) {
                xnd_error("openat(%s): %s\n", buf, strerror(errno));
                goto bad;
        }

        if (do_compress(dstfd, srcfd) != 0) {
                xnd_error("Failed to compress %s\n", ckptfile);
                goto bad;
        }
        
        if (unlinkat(dirfd, ckptfile, 0) != 0) {
                xnd_warn("unlinkat: %s\n", strerror(errno));
        }
        
        close(srcfd);
        close(dstfd);
        return 0;
bad:
        if (srcfd != -1)
                close(srcfd);
        if (dstfd != -1)
                close(dstfd);
        return -1;
}

int xnd_decompress_ckpt(int dirfd, char *ckptfile)
{
        int     srcfd = -1, dstfd = -1;
        char    buf[XND_CKPTFILE_MAXLEN];

        snprintf(buf, sizeof(buf), "%s" XND_COMPRESSED_SUFFIX, ckptfile);
        srcfd = openat(dirfd, buf, O_RDONLY, 0666);
        if (srcfd < 0) {
                xnd_error("openat(%s): %s\n", buf, strerror(errno));
                goto bad;
        }

        dstfd = openat(dirfd, ckptfile, O_WRONLY | O_CREAT, 0666);
        if (dstfd < 0) {
                xnd_error("openat(%s): %s\n", ckptfile, strerror(errno));
                goto bad;
        }

        if (do_decompress(dstfd, srcfd) != 0) {
                xnd_error("Failed to decompress %s\n", ckptfile);
                goto bad;
        }
        
        close(srcfd);
        close(dstfd);
        return 0;
bad:
        if (srcfd != -1)
                close(srcfd);
        if (dstfd != -1)
                close(dstfd);
        return -1;
}
