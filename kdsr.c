#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/skbuff.h>
#include <net/protocol.h>
#include <linux/netdevice.h>
#include <linux/if_ether.h>
#ifdef KERNEL26
#include <linux/moduleparam.h>
#endif

#include "dsr.h"
#include "dsr-dev.h"
#include "p-queue.h"
#include "debug.h"

static char *ifname = NULL;

static int kdsr_recv(struct sk_buff *skb)
{
	DEBUG("received dsr packet\n");

	// dsr_recv();

	kfree_skb(skb);

	return 0;
};

static void kdsr_recv_err(struct sk_buff *skb, u32 info)
{
	DEBUG("received error, info=%u\n", info);
	
	kfree_skb(skb);
}

static struct inet_protocol dsr_inet_prot = {
	.handler = kdsr_recv,
	.err_handler = kdsr_recv_err,
#ifdef KERNEL26
	.no_policy = 1,
#else
	.protocol    = IPPROTO_DSR,
	.name        = "DSR"
#endif
};


/* Allocate DSR packet, dev is send device */
struct sk_buff *kdsr_pkt_alloc(unsigned int size, struct net_device *dev)
{
	struct sk_buff *skb;
	
	if (size < DSR_PKT_MIN_LEN)
		return NULL;
	
	if (!dev) 
		return NULL;

	/* skb = dev_alloc_skb(size); */
	skb = alloc_skb(dev->hard_header_len + 15 + size, GFP_ATOMIC);
	
	if (!skb)
		return NULL;
		
       	skb_reserve(skb, (dev->hard_header_len+15)&~15); 
	skb->nh.raw = skb->data;
	skb->protocol = htons(ETH_P_IP);
	skb->dev = dev;
	
	/* skb_put(skb, size); */

	//skb->nh.iph = (struct iphdr *)skb->data;
	
	return skb;
}

#ifdef KERNEL26
module_param(ifname, charp, 0);
#else
MODULE_PARM(ifname, "s");
#endif

static int __init kdsr_init(void)
{
	int res = 0;

	res = dsr_dev_init(ifname);

	if (res < 0) {
		DEBUG("dsr-dev init failed\n");
		return -1;
	}

	res = p_queue_init();

	if (res < 0)
		goto cleanup_dsr_dev;

#ifndef KERNEL26
	inet_add_protocol(&dsr_inet_prot);
	return res;
#else
	res = inet_add_protocol(&dsr_inet_prot, IPPROTO_DSR);
	
	if (res < 0) {
		DEBUG("Could not register inet protocol\n");
		goto cleanup_p_queue;
	}
	return res;
	
 cleanup_p_queue:
	p_queue_cleanup();
#endif
	

 cleanup_dsr_dev:
	dsr_dev_cleanup();
	
	return res;
}

static void __exit kdsr_cleanup(void)
{
#ifdef KERNEL26
	inet_del_protocol(&dsr_inet_prot, IPPROTO_DSR);
#else
	inet_del_protocol(&dsr_inet_prot);
#endif
	dsr_dev_cleanup();
	p_queue_cleanup();
}

module_init(kdsr_init);
module_exit(kdsr_cleanup);
MODULE_LICENSE("GPL");
