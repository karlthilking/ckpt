/* pid_table_common.h */
#ifndef PID_TABLE_COMMON_H
#define PID_TABLE_COMMON_H

#include "xnd/xnd.h"
#include "xnd/virtual_id_table.h"
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

void pid_table_init(void);
void pid_table_acquire(void);
void pid_table_release(void);
void pid_table_reset(void);
void pid_table_update(pid_t, pid_t);
void pid_table_erase(pid_t);
size_t pid_table_size(void);

bool pid_table_virtual_pid_exists(pid_t);
bool pid_table_real_pid_exists(pid_t);

pid_t pid_table_virtual_to_real(pid_t);
pid_t pid_table_real_to_virtual(pid_t);

#ifdef __cplusplus
}
#endif /* __cplusplus */
#endif /* PID_TABLE_COMMON_H */
