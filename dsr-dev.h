#ifndef _DSR_DEV_H
#define _DSR_DEV_H

#include <linux/netdevice.h>

/* int dsr_dev_build_hw_hdr(struct sk_buff *skb, struct sockaddr *dest); */
int dsr_dev_xmit(struct dsr_pkt *dp);
int dsr_dev_deliver(struct sk_buff *skb);

int __init dsr_dev_init(char *ifname);
void __exit dsr_dev_cleanup(void);

#endif
