/* xnd_coord_client.h */
#ifndef XND_COORD_CLIENT_H
#define XND_COORD_CLIENT_H

#include "xnd/xnd.h"
#include "xnd_coord_api.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

void send_recv_coord_handshake(enum xnd_msghdr);
void connect_to_coord_on_launch(void);
void connect_to_coord_on_restart(void);
void disconnect_from_coord(void);

void coord_client_atfork_prepare(void);
void coord_client_atfork_child(void);
void coord_client_atfork_parent(void);
void coord_client_atfork_failed(void);

int wait_for_ckpt_request_from_coord(void);
void enter_coord_barrier(enum coord_barrier_type);

pid_t virt_to_real_pid_from_coord(pid_t);
pid_t real_to_virt_pid_from_coord(pid_t);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* XND_COORD_CLIENT_H */
