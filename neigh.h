#ifndef _NEIGH_H
#define _NEIGH_H

#ifdef __KERNEL__
#include <linux/if_ether.h>
#endif

#include "dsr.h"

#ifndef NO_DECLS

int neigh_tbl_add(struct in_addr neigh_addr, struct sockaddr *hw_addr);
int neigh_tbl_del(struct in_addr neigh_addr);
int neigh_tbl_get_hwaddr(struct in_addr neigh_addr, struct sockaddr *hw_addr);
unsigned short neigh_tbl_get_id(struct in_addr neigh_addr);
void neigh_tbl_reset_ack_req_timer(struct in_addr neigh_addr, unsigned short id);
void neigh_tbl_update_ack_req_tx_time(struct in_addr neigh_addr);
void neigh_tbl_set_ack_req_timer(struct in_addr neigh_addr);
int neigh_tbl_init(void);
void neigh_tbl_cleanup(void);
int neigh_tbl_rtt_update(struct in_addr nxt_hop, int rtt);
long neigh_tbl_get_rto(struct in_addr nxt_hop);
void neigh_tbl_garbage_timeout(unsigned long data);
int neigh_tbl_get_rtt(struct in_addr neigh_addr);

#endif /* NO_DECLS */

#endif /* _NEIGH_H */
