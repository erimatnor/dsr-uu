#ifndef _KDSR_H
#define _KDSR_H

#include <linux/skbuff.h>
#include <linux/netdevice.h>

struct sk_buff *kdsr_pkt_alloc(unsigned int size, struct net_device *dev);

struct sk_buff *dsr_rreq_create(__u32 taddr, struct net_device *dev);

#endif /* _KDSR_H */
