/* xnd_coord_common.h */
#ifndef XND_COORD_COMMON_H
#define XND_COORD_COMMON_H

enum xnd_msghdr;
enum xnd_cmd;

#define xnd_msghdr_string(hdr) \
	(__builtin_constant_p(hdr) ? TOSTRING(hdr) : \
	 xnd_msghdr_string_nonconst(hdr))

#define xnd_cmd_string(cmd) \
	(__builtin_constant_p(cmd) ? TOSTRING(cmd) : \
	 xnd_cmd_string_nonconst(cmd))

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

const char *xnd_msghdr_string_nonconst(enum xnd_msghdr);
const char *xnd_cmd_string_nonconst(enum xnd_cmd);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* XND_COORD_COMMON_H */
