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
#include <net/icmp.h>
#include <net/xfrm.h>

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
	dsr_pkt_t *dp;
	struct iphdr *iph;
	int verdict, len;

	DEBUG("Received DSR packet\n");
		
	iph = skb->nh.iph;
		
	if ((skb->len + (iph->ihl << 2)) < ntohs(iph->tot_len)) {
		DEBUG("data to short according to IP header len=%d tot_len=%d!\n", skb->len + (iph->ihl << 2), ntohs(iph->tot_len));
		return -1;
	}
	
	/* Allocate a new packet for internal representation */
	dp = dsr_pkt_alloc();
	dp->skb = skb;
	dp->len = skb->len;
	dp->data = skb->data;
	
	/* Get IP stuff that we need */
	dp->src.s_addr = iph->saddr;
	dp->dst.s_addr = iph->daddr;

	/* Process packet */
	verdict = dsr_recv(dp);

	/* Check verdict... */
	switch (verdict) {
	case DSR_PKT_FORWARD:
		DEBUG("Forwarding %s", print_ip(dp->src.s_addr));
		printk(" %s", print_ip(dp->dst.s_addr));		
		printk(" nh %s\n", print_ip(dp->nh.s_addr));

		if (iph->ttl < 1) {
			DEBUG("ttl=0, dropping!\n");
			break;
		}
		dsr_dev_queue_xmit(dp);
		
		break;
	case DSR_PKT_DELIVER:
		len = dsr_opts_remove(dp);
		
		if (len) {
			
			DEBUG("Deliver to DSR device\n");
			
			/* Update skb length and deliver to DSR interface. */
			/* if (!pskb_may_pull(skb, len)) */
/* 				return -1; */

			
			/* memmove(skb->mac.raw + len, skb->mac.raw, skb->dev->hard_header_len); */
/* 			skb->nh.raw = skb->data = dp->data; */
/* 			skb->mac.raw = skb->mac.raw + len; */

/* 			memcpy(skb->mac.ethernet->h_dest, dsr_dev->dev_addr, dsr_dev->addr_len); */

			skb_trim(skb, skb->len - len);
			
			dsr_dev_deliver(skb);
			
		} else
			kfree_skb(skb);
		break;
	case DSR_PKT_SEND_ICMP:
	case DSR_PKT_SRT_REMOVE:
	case DSR_PKT_ERROR:
		DEBUG("DSR_PKT_ERROR\n");
	case DSR_PKT_DROP:
	default:
		kfree_skb(skb);
		break;
	}
	
	dsr_pkt_free(dp);
	return 0;
};

static void kdsr_recv_err(struct sk_buff *skb, u32 info)
{
	DEBUG("received error, info=%u\n", info);
	
	kfree_skb(skb);
}

/* This is kind of a mess */
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,7)
static struct inet_protocol dsr_inet_prot = {
#else
static struct net_protocol dsr_inet_prot = {
#endif
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

int kdsr_arpset(struct in_addr addr, struct sockaddr *hw_addr, 
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
	
	/* We align to 16 bytes, for ethernet: 2 bytes + 14 bytes header */
       	skb_reserve(skb, (dev->hard_header_len+15)&~15); 
	skb->nh.raw = skb->data;
	skb->protocol = htons(ETH_P_IP);
	skb->dev = dev;
	
	skb_put(skb, size);
	
	return skb;
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
