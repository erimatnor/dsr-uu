#ifndef _NEIGH_H
#define _NEIGH_H

#include <linux/if_ether.h>

#include "dsr.h"


int neigh_tbl_add(struct in_addr neigh_addr, struct sockaddr *hw_addr);
int neigh_tbl_get_hwaddr(struct in_addr neigh_addr, struct sockaddr *hw_addr);
unsigned short neigh_tbl_get_id(struct in_addr neigh_addr);
void neigh_tbl_reset_ack_req_timer(struct in_addr neigh_addr, unsigned short id);
void neigh_tbl_update_ack_req_tx_time(struct in_addr neigh_addr);
void neigh_tbl_set_ack_req_timer(struct in_addr neigh_addr);
int neigh_tbl_init(void);
void neigh_tbl_cleanup(void);

#endif /* _NEIGH_H */
