#ifndef _NEIGH_H
#define _NEIGH_H

#include <linux/if_ether.h>

#include "dsr.h"

struct neighbor {
	struct list_head l;
	struct in_addr addr;
	struct sockaddr hw_addr;
        unsigned int id_req, id_ack;
	struct timer_list ack_req_timer;
	struct timer_list ack_timer;
	unsigned long last_ack_tx_time;
	unsigned long last_ack_rx_time;
	unsigned long last_req_tx_time;
	int rtt, reqs, jitter;
};

int neigh_tbl_add(struct in_addr neigh_addr, struct sockaddr *hw_addr);
int neigh_tbl_get_hwaddr(struct in_addr neigh_addr, struct sockaddr *hw_addr);

#endif /* _NEIGH_H */
