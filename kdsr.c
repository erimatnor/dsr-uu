#include <linux/config.h>
#include <linux/version.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/skbuff.h>
#include <net/protocol.h>
#include <linux/netdevice.h>
#include <linux/if_ether.h>
#include <linux/socket.h>
#include <net/arp.h>
#include <net/ip.h>
#include <net/dst.h>
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


static int kdsr_arpset(struct in_addr addr, struct sockaddr *hw_addr, 
		       struct net_device *dev)
{
	struct neighbour *neigh;

	DEBUG("Setting arp for %s %s\n", print_ip(addr.s_addr), 
	      print_eth(hw_addr->sa_data));

	neigh = __neigh_lookup_errno(&arp_tbl, &(addr.s_addr), dev);
	//        err = PTR_ERR(neigh);
        if (!IS_ERR(neigh)) {
		neigh->parms->delay_probe_time = 0;
                neigh_update(neigh, hw_addr->sa_data, NUD_REACHABLE, 1, 0);
                neigh_release(neigh);
        }
	return 0;
}

static int kdsr_recv(struct sk_buff *skb)
{
	struct iphdr *iph;
	struct in_addr src, dst;
	struct sockaddr hw_addr;
	int ret, newlen;

	DEBUG("received dsr packet\n");
	
	iph = skb->nh.iph;
		
	if ((skb->len + (iph->ihl << 2)) < ntohs(iph->tot_len)) {
		DEBUG("data to short according to IP header len=%d tot_len=%d!\n", skb->len + (iph->ihl << 2), ntohs(iph->tot_len));
		return -1;
	}
	
	/* Get IP stuff that we need */
	src.s_addr = iph->saddr;
	dst.s_addr = iph->daddr;

	/* FIXME: This should probably be put in a function. But how do we call
	 * it from dsr-rreq without access to the skb? */
	if (skb->mac.ethernet) {
		/* struct net_device *dev = skb->dev; */
		
		memcpy(hw_addr.sa_data, skb->mac.ethernet->h_source, ETH_ALEN);
		kdsr_arpset(src, &hw_addr, skb->dev);
	/* 	dev_put(dev); */
	}

	/* Process packet */
	ret = dsr_recv(skb->data, skb->len, src, dst);

	switch (ret) {
	case DSR_SRT_FORWARD:
		break;
	case DSR_SRT_DELIVER:
		newlen = dsr_opts_remove(skb->nh.iph, skb->len);
		
		if (newlen) {
			DEBUG("Deliver to IP\n");
			/* Trim skb and deliver to IP layer again. */
			skb_trim(skb, newlen);
			ip_rcv(skb, skb->dev, NULL);
			return 0;
		}
		break;
	case DSR_SRT_REMOVE:
		break;
	case DSR_SRT_ERROR:
		break;
	}
	
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

int kdsr_get_hwaddr(struct in_addr addr, struct sockaddr *hwaddr, 
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
	
	skb->dev = dev;

	t.s_addr = target;
	
	res = dsr_rreq_create(skb->data, skb->len, t);

	if (res < 0) {
		DEBUG("Could not create RREQ\n");
		goto out_err;
	}
	
	res = dsr_dev_build_hw_hdr(skb, &broadcast);
	
	if (res < 0) {
		DEBUG("RREQ transmission failed...\n");
		dev_kfree_skb(skb);
		goto out_err;
	}

	dev_queue_xmit(skb);

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

	DEBUG("Sending RREP\n");

	len = IP_HDR_LEN + DSR_OPT_HDR_LEN + DSR_SRT_OPT_LEN(srt) + DSR_RREP_OPT_LEN(srt);
	
	skb = kdsr_pkt_alloc(len, dev);
	
	if (!skb) {
		res = -1;
		goto out_err;
	}

	skb->dev = dev;

	res = dsr_rrep_create(skb->data, skb->len, srt);

	if (res < 0) {
		DEBUG("Could not create RREP\n");
		goto out_err;
	}

	if (srt->laddrs == 0) {
		DEBUG("source route is one hop\n");
		addr.s_addr = srt->src.s_addr;
	} else		
		addr.s_addr = srt->addrs->s_addr;
	
	if (kdsr_get_hwaddr(addr, &dest, dev) < 0) {
		DEBUG("Could not get hardware address for %s\n", 
		      print_ip(addr.s_addr));
		goto out_err;
	}
	
	res = dsr_dev_build_hw_hdr(skb, &dest);
	
	if (res < 0) {
		DEBUG("RREP transmission failed...\n");
		dev_kfree_skb(skb);
		goto out_err;
	}
	
	
	dev_queue_xmit(skb);
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
