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
#include "dsr-opt.h"
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
#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,8)
                neigh_update(neigh, hw_addr->sa_data, NUD_REACHABLE, 1);
#else
		neigh_update(neigh, hw_addr->sa_data, NUD_REACHABLE, 1, 0);
#endif
                neigh_release(neigh);
        }
	return 0;
}

static int kdsr_recv(struct sk_buff *skb)
{
	struct dsr_pkt dp;
	int verdict;

	DEBUG("Received DSR packet\n");
		
	dp.iph = skb->nh.iph;
		
	if ((skb->len + (dp.iph->ihl << 2)) < ntohs(dp.iph->tot_len)) {
		DEBUG("data to short according to IP header len=%d tot_len=%d!\n", skb->len + (dp.iph->ihl << 2), ntohs(dp.iph->tot_len));
		return -1;
	}
	
	dp.opt_hdr = (struct dsr_opt_hdr *)skb->data;
	dp.dsr_opts_len = ntohs(dp.opt_hdr->p_len) + DSR_OPT_HDR_LEN;

	dp.data = skb->data + dp.dsr_opts_len;
	dp.data_len = skb->len - dp.dsr_opts_len;

	/* Get IP stuff that we need */
	dp.src.s_addr = dp.iph->saddr;
	dp.dst.s_addr = dp.iph->daddr;

	/* Process packet */
	verdict = dsr_recv(&dp);

	/* Add mac address of previous hop to the arp table */
	if (dp.srt && dp.srt_opt && skb->mac.raw) {
		struct sockaddr hw_addr;
		struct in_addr prev_hop;
		struct ethhdr *eth;
		int n;
		
		eth = (struct ethhdr *)skb->mac.raw;
			
		memcpy(hw_addr.sa_data, eth->h_source, ETH_ALEN);
		n = dp.srt->laddrs / sizeof(u_int32_t);
		
		/* Find the previous hop */
		if (n == 0)
			prev_hop.s_addr = dp.srt->src.s_addr;
		else
			prev_hop.s_addr = dp.srt->addrs[n-1].s_addr;
		
		kdsr_arpset(prev_hop, &hw_addr, skb->dev);
	}

	/* Check verdict... */

	if (verdict & DSR_PKT_SRT_REMOVE) {
		int len;
		len = dsr_opts_remove(&dp);
		if (len)
			skb_trim(skb, skb->len - len);
		else
			kfree_skb(skb);
	}
	if (verdict & DSR_PKT_FORWARD) {
		DEBUG("Forwarding %s", print_ip(dp.src.s_addr));
		printk(" %s", print_ip(dp.dst.s_addr));		
		printk("nh %s\n", print_ip(dp.nxt_hop.s_addr));

		if (dp.iph->ttl < 1) {
			DEBUG("ttl=0, dropping!\n");
			kfree_skb(skb);
		}
		
		/* dev_queue_xmit(dp.skb); */
	}
	
	if (verdict & DSR_PKT_SEND_ICMP) {
		DEBUG("Send ICMP\n");
		kfree_skb(skb);
	}

	if (verdict & DSR_PKT_DELIVER) {
		DEBUG("Deliver to DSR device\n");
		
	/* 	dsr_dev_deliver(skb); */
		kfree_skb(skb);
	}

	if (verdict & DSR_PKT_DROP) {
		DEBUG("DSR_PKT_DROP\n");
		kfree_skb(skb);
	}
	
	if (verdict & DSR_PKT_ERROR) {
		DEBUG("DSR_PKT_ERROR\n");
		kfree_skb(skb);
	}
		
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
struct sk_buff *kdsr_skb_create(struct dsr_pkt *dp)
{
	struct sk_buff *skb;
	struct ethhdr *ethh;
	struct sockaddr dest = {AF_UNSPEC, {0xff, 0xff, 0xff, 0xff, 0xff, 0xff}};
	struct net_device *dev;
	char *buf;
	int ip_len;
	int len;
	
	dsr_node_lock(dsr_node);
	dev = dsr_node->slave_dev;
	dsr_node_unlock(dsr_node);

	ip_len = dp->iph->ihl << 2;
	
	len = dev->hard_header_len + 15 + ip_len + dp->dsr_opts_len + dp->data_len;
	
	DEBUG("dp->data_len=%d dp->dsr_opts_len=%d len=%d\n", 
	      dp->data_len, dp->dsr_opts_len, len);
	
	skb = alloc_skb(len, GFP_ATOMIC);
	
	if (!skb) 
		return NULL;
	
	
	/* We align to 16 bytes, for ethernet: 2 bytes + 14 bytes header */
       	skb_reserve(skb, (dev->hard_header_len+15)&~15); 
	skb->nh.raw = skb->data;
	skb->dev = dev;
	skb->protocol = htons(ETH_P_IP);
	ethh = (struct ethhdr *)skb->data;

	/* Copy in all the headers in the right order */
	buf = skb_put(skb, len);

	memcpy(buf, dp->iph, ip_len);
	
	buf += ip_len;
	
	if (dp->dsr_opts_len && dp->opt_hdr) {
		memcpy(buf, dp->opt_hdr, dp->dsr_opts_len);
		buf += dp->dsr_opts_len;
	}

	if (dp->data_len && dp->data)
		memcpy(buf, dp->data, dp->data_len);

	/* Get hardware destination address */
 	if (dp->nxt_hop.s_addr != DSR_BROADCAST) {
		
		if (kdsr_get_hwaddr(dp->nxt_hop, &dest, dev) < 0) {
			kfree_skb(skb);
			return NULL;
		}
	}
	dev->rebuild_header(skb);

	memcpy(ethh->h_source, dev->dev_addr, dev->addr_len);	

	if (dev->hard_header) {
		dev->hard_header(skb, dev, ETH_P_IP,
				      dest.sa_data, 0, skb->len);
	} else {
		DEBUG("Missing hard_header\n");
		kfree_skb(skb);
	}
	
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
