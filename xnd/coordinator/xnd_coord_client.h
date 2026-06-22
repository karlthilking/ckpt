/* xnd_coord_client.h */
#ifndef XND_COORD_CLIENT_H
#define XND_COORD_CLIENT_H

#include "xnd/xnd.h"

void register_with_coord_on_launch(void);
void register_with_coord_on_restart(void);
void send_exit_to_coord(void);

#endif /* XND_COORD_CLIENT_H */
