#include <linux/skbuff.h>
#include <linux/ip.h>

#include "dsr.h"


dsr_hdr_t *dsr_hdr_add(char *buf, int len, unsigned int protocol)
{
	dsr_hdr_t *dh;
	
	if (len < sizeof(dsr_hdr_t))
		return NULL;
	
	dh = (dsr_hdr_t *)buf;

	dh->nh = protocol;
	dh->f = 0;
	dh->res = 0;
      	dh->length = len;

	return dh;
}

struct sk_buff *dsr_pkt_alloc(int size)
{
	struct sk_buff *skb;
	struct net_device *dev;
	
	if (size < DSR_PKT_MIN_LEN)
		return NULL;
	
	dev = dev_get_by_index(ldev_info.ifindex);
	
	if (!dev) 
		return NULL;

	skb = alloc_skb(dev->hard_header_len + 15 + size, GFP_ATOMIC);
	
	if (!skb)
		return NULL;
	
	skb_reserve(skb, (dev->hard_header_len+15)&~15);
	skb->nh.raw = skb->data;
	skb->protocol = htons(ETH_P_IP);
	skb->dev = dev;
	
	skb_put(skb, size);

	skb->nh.iph = (struct iphdr *)skb->data;

	dev_put(dev);
	
	return skb;
}

//struct sk_buff *dsr_pkt_create(int size)
