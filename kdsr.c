#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/skbuff.h>
#include <net/protocol.h>
#include <linux/netdevice.h>
#include <linux/if_ether.h>
#include <linux/socket.h>
#include <net/arp.h>
#include <net/neighbour.h>
#ifdef KERNEL26
#include <linux/moduleparam.h>
#endif

#include "dsr.h"
#include "dsr-dev.h"
#include "dsr-rreq.h"
#include "dsr-rrep.h"
#include "dsr-srt.h"
#include "p-queue.h"
#include "debug.h"

static char *ifname = NULL;

MODULE_AUTHOR("Erik Nordstroem <erikn@it.uu.se>");
MODULE_DESCRIPTION("Dynamic Source Routing (DSR) protocol stack");
MODULE_LICENSE("GPL");

#ifdef KERNEL26
module_param(ifname, charp, 0);
#else
MODULE_PARM(ifname, "s");
#endif

static int kdsr_recv(struct sk_buff *skb)
{
	DEBUG("received dsr packet\n");

	// dsr_recv();
	dsr_recv(skb->data, skb->len);

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

static int get_hwaddr(struct in_addr addr, struct sockaddr *hwaddr, 
		      struct net_device *dev)
{	
	struct neighbour *neigh;

	neigh = neigh_lookup(&arp_tbl, &addr.s_addr, dev);
	
	if (neigh) {
		
		hwaddr->sa_family = AF_UNSPEC;
	      
		read_lock_bh(&neigh->lock);
		memcpy(hwaddr->sa_data, neigh->ha, ETH_ALEN);
		read_unlock_bh(&neigh->lock);

		return 0;
	}
	return -1;
}

/* Allocate DSR packet, dev is send device */
static struct sk_buff *kdsr_pkt_alloc(unsigned int size, struct net_device *dev)
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
	
	/* We align to 16 bytes, for ethernet: 2 bytes + 14 bytes header */
       	skb_reserve(skb, (dev->hard_header_len+15)&~15); 
	skb->nh.raw = skb->data;
	skb->protocol = htons(ETH_P_IP);
	skb->dev = dev;
	
	skb_put(skb, size);
	
	return skb;
}

int dsr_rreq_send(u_int32_t target)
{
	int len, res = 0;
	struct net_device *dev;
	struct sk_buff *skb;
	struct in_addr t;
	struct sockaddr broadcast = {AF_UNSPEC, {0xff, 0xff, 0xff, 0xff, 0xff, 0xff}};

	dev = dev_get_by_index(ldev_info.ifindex);
	
	if (!dev) {
		DEBUG("no send device!\n");
		res = -1;
		goto out_err;
	}

	len = IP_HDR_LEN + DSR_OPT_HDR_LEN + DSR_RREQ_HDR_LEN;
	
	skb = kdsr_pkt_alloc(len, dev);
	
	if (!skb) {
		res = -1;
		goto out_err;
	}
	
	t.s_addr = target;
	
	res = dsr_rreq_create(skb->data, skb->len, t);

	if (res < 0) {
		DEBUG("Could not create RREQ\n");
		goto out_err;
	}
	
	res = dsr_pkt_send(skb, &broadcast, dev);
	
	if (res < 0) {
		DEBUG("RREQ transmission failed...\n");
		dev_kfree_skb(skb);
	}
 out_err:	
	dev_put(dev);
	return res;
}

int dsr_rrep_send(dsr_srt_t *srt)
{
	int len, res = 0;
	struct net_device *dev;
	struct sk_buff *skb;
	struct sockaddr dest;
	struct in_addr addr;
	
	dev = dev_get_by_index(ldev_info.ifindex);
	
	if (!dev) {
		DEBUG("no send device!\n");
		res = -1;
		goto out_err;
	}
	
	if (!srt) {
		DEBUG("no source route!\n");
		res = -1;
		goto out_err;
	}
	len = IP_HDR_LEN + DSR_OPT_HDR_LEN + DSR_RREP_OPT_LEN(srt);
	
	skb = kdsr_pkt_alloc(len, dev);
	
	if (!skb) {
		res = -1;
		goto out_err;
	}

	res = dsr_rrep_create(skb->data, skb->len, srt);

	if (res < 0) {
		DEBUG("Could not create RREP\n");
		goto out_err;
	}

	if (srt->laddrs == 0)
		addr.s_addr = srt->src.s_addr;
	else
		
		addr.s_addr = srt->addrs[0].s_addr;
	
	if (get_hwaddr(addr, &dest, dev) < 0) {
		DEBUG("Could not get hardware address for %s\n", 
		      print_ip(addr.s_addr));
		goto out_err;
	}
	
	res = dsr_pkt_send(skb, &dest, dev);

 out_err:	
	dev_put(dev);
	return res;
}

static int __init kdsr_init(void)
{
	int res = 0;

	res = dsr_dev_init(ifname);

	if (res < 0) {
		DEBUG("dsr-dev init failed\n");
		return -1;
	}
	
	DEBUG("Creating packet queue\n"),
	res = p_queue_init();

	if (res < 0) {
		DEBUG("Could not create packet queue\n");
		goto cleanup_dsr_dev;
	}

#ifndef KERNEL26
	inet_add_protocol(&dsr_inet_prot);
	DEBUG("Setup finished\n");
	return 0;
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
	p_queue_cleanup();
	dsr_dev_cleanup();
}

module_init(kdsr_init);
module_exit(kdsr_cleanup);
