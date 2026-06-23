/* xnd_coord_client.h */
#ifndef XND_COORD_CLIENT_H
#define XND_COORD_CLIENT_H

#include "xnd/xnd.h"

void register_with_coord_on_launch(void);
void register_with_coord_on_restart(void);
void send_exit_to_coord(void);

void wait_for_coord_msg(void);
void notify_coord_before_checkpoint(void);
void notify_coord_after_checkpoint(void);
void enter_coord_barrier(enum xnd_msghdr);

#endif /* XND_COORD_CLIENT_H */
