#include <linux/string.h>
#include <linux/if_ether.h>
#include <net/ip.h>

#include "dsr.h"
#include "debug.h"
#include "dsr-rrep.h"
#include "dsr-srt.h"
#include "dsr-rtc.h"
#include "dsr-dev.h"
#include "p-queue.h"
#include "kdsr.h"

static inline int dsr_rrep_add_srt(dsr_rrep_opt_t *rrep, dsr_srt_t *srt)
{
	int n;

	if (!rrep | !srt)
		return -1;

	n = srt->laddrs / sizeof(struct in_addr);

	memcpy(rrep->addrs, srt->addrs, srt->laddrs);
	memcpy(&rrep->addrs[n], &srt->dst, sizeof(struct in_addr));
	
	return 0;
}

static dsr_rrep_opt_t *dsr_rrep_opt_add(char *buf, int len, dsr_srt_t *srt)
{
	dsr_rrep_opt_t *rrep;
	
	if (!buf || !srt || len < DSR_RREP_OPT_LEN(srt))
		return NULL;

	rrep = (dsr_rrep_opt_t *)buf;
	
	rrep->type = DSR_OPT_RREP;
	rrep->length = srt->laddrs + sizeof(struct in_addr) + 1;
	rrep->l = 0;
	rrep->res = 0;

	/* Add source route to RREP */
	dsr_rrep_add_srt(rrep, srt);
       
	return rrep;	
}

int dsr_rrep_create(dsr_pkt_t *dp)
{
	dsr_srt_t *srt_rev;
	dsr_rrep_opt_t *rrep;
	char *off;
	int l;
	
	if (!dp || !dp->srt)
		return -1;
	
	l = IP_HDR_LEN + DSR_OPT_HDR_LEN + 
		DSR_SRT_OPT_LEN(dp->srt) + DSR_RREP_OPT_LEN(dp->srt);
	
	if (dp->len < l)
		return -1;
	
	dp->dst.s_addr = dp->srt->src.s_addr;
	off = dp->data;
	
	dsr_build_ip(off, l, dsr_node->ifaddr, dp->dst, 1);
	
	off += IP_HDR_LEN;
	l -= IP_HDR_LEN;
	
	dsr_hdr_add(off, l, 0);
	
	off += DSR_OPT_HDR_LEN;
	l -= DSR_OPT_HDR_LEN;

	srt_rev = dsr_srt_new_rev(dp->srt);

	if (!srt_rev)
		return -1;
	
	if (srt_rev->laddrs == 0) {
		DEBUG("source route is one hop\n");
		dp->nh.s_addr = srt_rev->dst.s_addr;
	} else
		dp->nh.s_addr = srt_rev->addrs[0].s_addr;
	
	/* Add the source route option to the packet */
	dsr_srt_opt_add(off, l, srt_rev);
	
	kfree(srt_rev);
	
	off += DSR_SRT_OPT_LEN(dp->srt);
	l -= DSR_SRT_OPT_LEN(dp->srt);

	rrep = dsr_rrep_opt_add(off, l, dp->srt);

	if (!rrep) {
		DEBUG("Could not create RREQ\n");
		return -1;
	}
	return 0;
}

int dsr_rrep_recv(dsr_pkt_t *dp)
{
	dsr_rrep_opt_t *rrep;
	
	if (!dp || !dp->rrep)
		return DSR_PKT_DROP;

	rrep = dp->rrep;
	
	dp->srt = dsr_srt_new(dp->dst, dp->src, DSR_RREP_ADDRS_LEN(rrep), 
			  (char *)rrep->addrs);
	
	if (!dp->srt)
		return DSR_PKT_DROP;
	
	DEBUG("Adding srt to cache\n");
	dsr_rtc_add(dp->srt, 60000, 0);
	
#ifdef __KERNEL__
	/* Add mac address of previous hop to the arp table */
	if (dp->skb->mac.raw) {
		struct sockaddr hw_addr;
		struct in_addr ph;
		/* struct net_device *dev = skb->dev; */
		struct ethhdr *eth;
		
		eth = (struct ethhdr *)dp->skb->mac.raw;
		
		memcpy(hw_addr.sa_data, eth->h_source, ETH_ALEN);
		/* Find the previous hop */
		if (dp->srt->laddrs == 0)
			ph.s_addr = dp->srt->dst.s_addr;
		else {
			/* FIXME: this is not right */
			ph.s_addr = dp->srt->addrs[0].s_addr;
		
		}
		kdsr_arpset(ph, &hw_addr, dp->skb->dev);
	/* 	dev_put(dev); */
	}
#endif
	if (dp->dst.s_addr == dsr_node->ifaddr.s_addr) {
		/*RREP for this node */
		
		DEBUG("RREP for me!\n");
				
		/* Send buffered packets */
		p_queue_set_verdict(P_QUEUE_SEND, dp->srt->dst.s_addr);
				
	} else {
		DEBUG("I am not RREP destination\n");
		/* Forward */
		return DSR_PKT_FORWARD;
	}
	
	return DSR_PKT_DROP;
}

int dsr_rrep_send(dsr_srt_t *srt)
{
	int res = 0;
	dsr_pkt_t *dp;
	struct sockaddr dest;
	
	if (!srt) {
		DEBUG("no source route!\n");
		return -1;
	}

	dp = dsr_pkt_alloc();
	
	if (!dp) {
		return -1;
	}
	
	dp->len = IP_HDR_LEN + DSR_OPT_HDR_LEN + DSR_SRT_OPT_LEN(srt) + DSR_RREP_OPT_LEN(srt);

	DEBUG("RREP len=%d\n", dp->len);
		
	dp->skb = kdsr_pkt_alloc(dp->len, dsr_node->slave_dev);
	
	if (dp->skb == NULL)
		goto out_err;

	/* Make a copy of the source route */
	dp->srt = dsr_srt_new(srt->src, dsr_node->ifaddr, 
			      srt->laddrs, (char *)srt->addrs);
	
	DEBUG("srt copy: %s\n", print_srt(dp->srt));

	dp->data = dp->skb->data; 

	res = dsr_rrep_create(dp);

	if (res < 0) {
		DEBUG("Could not create RREP\n");
		kfree_skb(dp->skb);
		goto out_err;
	}
	
	if (kdsr_get_hwaddr(dp->nh, &dest, dsr_node->slave_dev) < 0) {
		DEBUG("Could not get hardware address for %s\n",
		      print_ip(dp->nh.s_addr));
		kfree_skb(dp->skb);
		goto out_err;
	}
	
	res = dsr_dev_build_hw_hdr(dp->skb, &dest);
	
	if (res < 0) {
		DEBUG("RREP transmission failed...\n");
		kfree_skb(dp->skb);
		goto out_err;
	}
	
	DEBUG("Sending RREP\n");
		
	dev_queue_xmit(dp->skb);

 out_err:
	dsr_pkt_free(dp);
	/* dev_put(dev); */
	return res;
}
