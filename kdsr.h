#ifndef _KDSR_H
#define _KDSR_H

#include <linux/skbuff.h>
#include <linux/netdevice.h>

//struct sk_buff *kdsr_pkt_alloc(unsigned int size, struct net_device *dev);
int dsr_get_hwaddr(struct in_addr addr, struct sockaddr *hwaddr, 
	       struct net_device *dev);
int dsr_arpset(struct in_addr addr, struct sockaddr *hw_addr, 
		struct net_device *dev);
/* struct sk_buff *kdsr_skb_alloc(unsigned int size, struct net_device *dev); */
struct sk_buff *dsr_skb_create(struct dsr_pkt *dp, struct net_device *dev);
int dsr_hw_header_create(struct dsr_pkt *dp, struct sk_buff *skb);
#endif /* _KDSR_H */
