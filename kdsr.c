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
#include "dsr-rtc.h"

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

static int kdsr_ip_recv(struct sk_buff *skb)
{
	struct dsr_pkt dp;
	int action;

	DEBUG("Received DSR packet\n");
		
	memset(&dp, 0, sizeof(dp));
	
	dp.iph = skb->nh.iph;
		
	if ((skb->len + (dp.iph->ihl << 2)) < ntohs(dp.iph->tot_len)) {
		DEBUG("data to short according to IP header len=%d tot_len=%d!\n", skb->len + (dp.iph->ihl << 2), ntohs(dp.iph->tot_len));
		kfree_skb(skb);
		return -1;
	}
	
	dp.opt_hdr = (struct dsr_opt_hdr *)skb->data;
	dp.dsr_opts_len = ntohs(dp.opt_hdr->p_len) + DSR_OPT_HDR_LEN;

	dp.data = skb->data + dp.dsr_opts_len;
	dp.data_len = skb->len - dp.dsr_opts_len;

	/* Get IP stuff that we need */
	dp.src.s_addr = dp.iph->saddr;
	dp.dst.s_addr = dp.iph->daddr;
	
	DEBUG("iph_len=%d iph_totlen=%d dsr_opts_len=%d data_len=%d\n", 
	      (dp.iph->ihl << 2), ntohs(dp.iph->tot_len), dp.dsr_opts_len, dp.data_len);
	/* Process packet */
	action = dsr_opt_recv(&dp);  /* Kernel panics here!!! */

	/* Add mac address of previous hop to the arp table */
	if (dp.srt && skb->mac.raw) {
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

	/* Check action... */

	if (action & DSR_PKT_SRT_REMOVE) {
		DEBUG("DSR options remove!\n");
		dsr_opts_remove(&dp);
		
	}
	if (action & DSR_PKT_FORWARD) {
		DEBUG("Forwarding %s", print_ip(dp.src.s_addr));
		printk(" %s", print_ip(dp.dst.s_addr));		
		printk(" nh %s\n", print_ip(dp.nxt_hop.s_addr));

		if (dp.iph->ttl < 1) {
			DEBUG("ttl=0, dropping!\n");
			action = DSR_PKT_DROP;
		} else {
			DEBUG("Forwarding (dev_queue_xmit)\n");
			dsr_dev_xmit(&dp);
		}
	}
	if (action & DSR_PKT_SEND_RREP) {
		struct dsr_srt *srt_rev;

		DEBUG("Send RREP\n");
		
		if (dp.srt) {
			srt_rev = dsr_srt_new_rev(dp.srt);
			
			DEBUG("srt_rev: %s\n", print_srt(srt_rev));
			/* send rrep.... */
			dsr_rrep_send(srt_rev);
			kfree(srt_rev);
		}
	}

	if (action & DSR_PKT_SEND_ICMP) {
		DEBUG("Send ICMP\n");
	}
	if (action & DSR_PKT_SEND_BUFFERED) {
		/* Send buffered packets */
		DEBUG("Sending buffered packets\n");
		if (dp.srt) {
			p_queue_set_verdict(P_QUEUE_SEND, dp.srt->src.s_addr);
		}
	}

	/* Free source route. Should probably think of a better way to handle
	 * source routes that are dynamically allocated. */
	if (dp.srt) {
		DEBUG("Freeing source route\n");
		kfree(dp.srt);
	}
	if (action & DSR_PKT_DROP || action & DSR_PKT_ERROR) {
		DEBUG("DSR_PKT_DROP or DSR_PKT_ERROR\n");
		kfree_skb(skb);
		return 0;
	}
	if (action & DSR_PKT_DELIVER) {
		DEBUG("Deliver to DSR device\n");
		dsr_dev_deliver(&dp);
		kfree_skb(skb);
		return 0;
	}

	return 0;
};

static void kdsr_ip_recv_err(struct sk_buff *skb, u32 info)
{
	DEBUG("received error, info=%u\n", info);
	
	kfree_skb(skb);
}



static int kdsr_get_hwaddr(struct in_addr addr, struct sockaddr *hwaddr, 
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

struct sk_buff *kdsr_skb_create(struct dsr_pkt *dp,
				struct net_device *dev)
{
	struct sk_buff *skb;
	char *buf;
	int ip_len;
	int tot_len;
	
	ip_len = dp->iph->ihl << 2;
	
	tot_len = ip_len + dp->dsr_opts_len + dp->data_len;
	
	DEBUG("iph_len=%d dp->data_len=%d dp->dsr_opts_len=%d TOT len=%d\n", 
	      ip_len, dp->data_len, dp->dsr_opts_len, tot_len);
	
	skb = alloc_skb(dev->hard_header_len + 15 + tot_len, GFP_ATOMIC);
	
	if (!skb) {
		DEBUG("alloc_skb failed\n");
		return NULL;
	}
	
	/* We align to 16 bytes, for ethernet: 2 bytes + 14 bytes header */
       	skb_reserve(skb, (dev->hard_header_len+15)&~15); 
	skb->nh.raw = skb->data;
	skb->dev = dev;
	skb->protocol = htons(ETH_P_IP);

	/* Copy in all the headers in the right order */
	buf = skb_put(skb, tot_len);

	memcpy(buf, dp->iph, ip_len);
	
	buf += ip_len;
	
	if (dp->dsr_opts_len && dp->opt_hdr) {
		memcpy(buf, dp->opt_hdr, dp->dsr_opts_len);
		buf += dp->dsr_opts_len;
	}

	if (dp->data_len && dp->data)
		memcpy(buf, dp->data, dp->data_len);
	
	return skb;
}

int kdsr_hw_header_create(struct dsr_pkt *dp, struct sk_buff *skb) 
{

		
	struct sockaddr broadcast = {AF_UNSPEC, {0xff, 0xff, 0xff, 0xff, 0xff, 0xff}};
	struct sockaddr dest;
	
	if (dp->dst.s_addr == DSR_BROADCAST)
		memcpy(dest.sa_data , broadcast.sa_data, ETH_ALEN);
	else {
		/* Get hardware destination address */
		if (kdsr_get_hwaddr(dp->nxt_hop, &dest, skb->dev) < 0) {
			DEBUG("Could not get hardware address for next hop %s\n", print_ip(dp->nxt_hop.s_addr));
			return -1;
		}
	}
	
	if (skb->dev->hard_header) {
		skb->dev->hard_header(skb, skb->dev, ETH_P_IP,
				      dest.sa_data, 0, skb->len);
	} else {
		DEBUG("Missing hard_header\n");
		return -1;
	}
	return 0;
}

/* This is kind of a mess */
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,7)
static struct inet_protocol dsr_inet_prot = {
#else
static struct net_protocol dsr_inet_prot = {
#endif
	.handler = kdsr_ip_recv,
	.err_handler = kdsr_ip_recv_err,
#ifdef KERNEL26
	.no_policy = 1,
#else
	.protocol    = IPPROTO_DSR,
	.name        = "DSR"
#endif
};

static int __init kdsr_init(void)
{
	int res = -EAGAIN;;

	res = dsr_dev_init(ifname);

	if (res < 0) {
		DEBUG("dsr-dev init failed\n");
		return -EAGAIN;
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
	
	if (inet_add_protocol(&dsr_inet_prot, IPPROTO_DSR) < 0) {
		DEBUG("Could not register inet protocol\n");
		goto cleanup_p_queue;
	}
	rreq_tbl_init();

	DEBUG("Setup finished res=%d\n", res);
	return 0;
	
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
	rreq_tbl_cleanup();
}

module_init(kdsr_init);
module_exit(kdsr_cleanup);
