/* env.c */
#include "xnd/xnd.h"
#include "env.h"
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

static __always_inline void env_set_pid(const char *var, pid_t pid)
{
        char buf[11];
        
        snprintf(buf, sizeof(buf), "%d", pid);
        xnd_assert(setenv(var, buf, 1) == 0);
}

static __always_inline pid_t env_get_pid(const char *var)
{
        char    *value;
        pid_t   pid;
        
        xnd_assert((value = getenv(var)) != NULL);
        pid = atoi(value);

        return pid;
}

void env_set_pid_info(pid_t virt_pid, pid_t real_pid, 
                      pid_t virt_ppid, pid_t real_ppid)
{
        if (virt_pid != -1) {
                env_set_pid(XND_VIRTUAL_PID_ENV, virt_pid);
        }
        
        if (real_pid != -1) {
                env_set_pid(XND_REAL_PID_ENV, real_pid);
        }

        if (virt_ppid != -1) {
                env_set_pid(XND_VIRTUAL_PPID_ENV, virt_ppid);
        }
        
        if (real_ppid != -1) {
                env_set_pid(XND_REAL_PPID_ENV, real_ppid);
        }
}

void env_get_pid_info(pid_t *virt_pid, pid_t *real_pid,
                      pid_t *virt_ppid, pid_t *real_ppid)
{
        if (virt_pid) {
                *virt_pid = env_get_pid(XND_VIRTUAL_PID_ENV);
        }

        if (real_pid) {
                *real_pid = env_get_pid(XND_REAL_PID_ENV);
        }

        if (virt_ppid) {
                *virt_ppid = env_get_pid(XND_VIRTUAL_PPID_ENV);
        }

        if (real_ppid) {
                *real_ppid = env_get_pid(XND_REAL_PPID_ENV); 
        }
}

void env_set_program_name(char *value)
{
        xnd_assert(setenv(XND_PROGRAM_ENV, value, 1) == 0);
        xnd_trace("%s=%s\n", XND_PROGRAM_ENV, value);
}

char *env_get_program_name(void)
{
        char *value;

        if ((value = getenv(XND_PROGRAM_ENV)) != NULL) {
                xnd_trace("%s=%s\n", XND_PROGRAM_ENV, value);
        }

        return value;
}

void env_set_ckpt_signal(char *sig)
{
        xnd_assert(setenv(XND_CKPT_SIGNAL_ENV, sig, 1) == 0);
        xnd_trace("%s=%s\n", XND_CKPT_SIGNAL_ENV, sig);
}

int env_get_ckpt_signal(void)
{
        char            *value;
        static int      sig = -1;

        if (unlikely(sig == -1)) {
                if ((value = getenv(XND_CKPT_SIGNAL_ENV)) != NULL) {
                        sig = atoi(value);
                } else {
                        sig = XND_DEFAULT_CKPT_SIGNAL;
                }
        }

        return sig;
}

void env_set_dyld_shared_region_private(void)
{
        xnd_assert(setenv("DYLD_SHARED_REGION", "private", 1) == 0);
}

void env_unset_dyld_shared_region(void)
{
        xnd_assert(unsetenv("DYLD_SHARED_REGION") == 0);
}

char *env_get_dyld_shared_region(void)
{
        return getenv("DYLD_SHARED_REGION");
}

bool env_dyld_shared_region_is_private(void)
{
        char *value;
        
        value = getenv("DYLD_SHARED_REGION");
        if (value == NULL || strcmp(value, "private")) {
                return false;
        }

        return true;
}

void env_set_zlib_compression(char *value)
{
        xnd_assert(setenv(XND_USE_ZLIB_ENV, value, 1) == 0);
}

bool env_use_zlib_compression(void)
{
        char *value;

        value = getenv(XND_USE_ZLIB_ENV);
        if (value && atoi(value) != 0) {
                return true;
        }

        return false;
}

void env_set_ckpt_interval(char *interval)
{
        xnd_assert(setenv(XND_CKPT_INTERVAL_ENV, interval, 1) == 0);
}

int env_get_ckpt_interval(void)
{
        char *value;

        if ((value = getenv(XND_CKPT_INTERVAL_ENV))) {
                return atoi(value);
        }

        return 0;
}
