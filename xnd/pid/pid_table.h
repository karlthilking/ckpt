/* pid_table.h */
#ifndef VIRTUAL_PID_TABLE_H
#define VIRTUAL_PID_TABLE_H

#include "xnd/xnd.h"
#include "xnd/virtual_id_table.h"
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

void pid_table_init_pid_info(void);
void pid_table_postrestart(void);
void pid_table_atfork_prepare(void);
void pid_table_atfork_child(void);
void pid_table_atfork_parent(void);
void pid_table_atfork_failed(void);

#ifdef __cplusplus
}
#endif /* __cplusplus */
#endif /* VIRTUAL_PID_TABLE_H */
