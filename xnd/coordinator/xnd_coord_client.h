/* xnd_coord_client.h */
#ifndef XND_COORD_CLIENT_H
#define XND_COORD_CLIENT_H

#include "xnd/xnd.h"

void register_with_coord_on_launch(void);
void register_with_coord_on_restart(void);
void send_exit_to_coord(void);

enum xnd_msghdr wait_for_coord_msg(void);
void preckpt_coord_barrier(void);
void postckpt_coord_barrier(void);
void postrestart_coord_barrier(void);

#endif /* XND_COORD_CLIENT_H */
