#ifndef _NEIGH_H
#define _NEIGH_H

#ifdef __KERNEL__
#include <linux/if_ether.h>
#endif

#include "dsr.h"

#ifndef NO_GLOBALS

struct neighbor_info {
	struct sockaddr hw_addr;
	unsigned short id;
	usecs_t rto; /* Round Trip Timeout */
	struct timeval last_ack_req;
};

#endif /* NO_GLOBALS */

#ifndef NO_DECLS
#ifdef NS2 
int neigh_tbl_add(struct in_addr neigh_addr, struct hdr_mac *mac);
#else
int neigh_tbl_add(struct in_addr neigh_addr, struct ethhdr *ethh);
#endif
int neigh_tbl_del(struct in_addr neigh_addr);
int neigh_tbl_query(struct in_addr neigh_addr, struct neighbor_info *neigh_info);int neigh_tbl_id_inc(struct in_addr neigh_addr);
void neigh_tbl_garbage_timeout(unsigned long data);

int neigh_tbl_init(void);
void neigh_tbl_cleanup(void);

#endif /* NO_DECLS */

#endif /* _NEIGH_H */
