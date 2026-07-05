/* env.h */
#ifndef XND_ENV_H
#define XND_ENV_H

#include "xnd/xnd.h"
#include <sys/types.h>

#define XND_VIRTUAL_PID_ENV     "XND_VIRTUAL_PID"
#define XND_REAL_PID_ENV        "XND_REAL_PID"
#define XND_VIRTUAL_PPID_ENV    "XND_VIRTUAL_PPID"
#define XND_REAL_PPID_ENV       "XND_REAL_PPID"

#define XND_PROGRAM_ENV         "XND_PROGRAM"
#define XND_CKPT_SIGNAL_ENV     "XND_CKPT_SIGNAL"
#define XND_USE_ZLIB_ENV        "XND_USE_ZLIB"
#define XND_CKPT_INTERVAL_ENV   "XND_CKPT_INTERVAL"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

void env_set_pid_info(pid_t, pid_t, pid_t, pid_t);
void env_get_pid_info(pid_t *, pid_t *, pid_t *, pid_t *);

void env_set_program_name(char *);
char *env_get_program_name(void);

void env_set_ckpt_signal(char *);
int env_get_ckpt_signal(void);

void env_set_dyld_shared_region_private(void);
void env_unset_dyld_shared_region(void);
char *env_get_dyld_shared_region(void);
bool env_dyld_shared_region_is_private(void);

void env_set_zlib_compression(char *);
bool env_use_zlib_compression(void);

void env_set_ckpt_interval(char *);
int env_get_ckpt_interval(void);

#ifdef __cplusplus
}
#endif /* __cplusplus */
#endif /* XND_ENV_H */
