#ifndef _DSR_DEV_H
#define _DSR_DEV_H

#include <linux/netdevice.h>

int dsr_pkt_send(struct sk_buff *skb, struct sockaddr *dest, 
		 struct net_device *dev);

int __init dsr_dev_init(char *ifname);
void __exit dsr_dev_cleanup(void);

#endif
