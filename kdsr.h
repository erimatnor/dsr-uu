#ifndef _KDSR_H
#define _KDSR_H

#include <linux/skbuff.h>
#include <linux/netdevice.h>

//struct sk_buff *kdsr_pkt_alloc(unsigned int size, struct net_device *dev);
int kdsr_get_hwaddr(struct in_addr addr, struct sockaddr *hwaddr, 
	       struct net_device *dev);

#endif /* _KDSR_H */
