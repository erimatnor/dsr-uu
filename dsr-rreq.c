#include <net/ip.h>

#include "debug.h"
#include "dsr.h"
#include "kdsr.h"
#include "dsr-rrep.h"
#include "dsr-rreq.h"
#include "dsr-rtc.h"
#include "dsr-dev.h"
#include "p-queue.h"

static unsigned int rreq_seqno = 1;

static dsr_rreq_opt_t *dsr_rreq_opt_add(char *buf, int len, 
					struct in_addr target)
{
	dsr_rreq_opt_t *rreq;

	if (!buf || len < DSR_RREQ_HDR_LEN)
		return NULL;

	rreq = (dsr_rreq_opt_t *)buf;
	
	rreq->type = DSR_OPT_RREQ;
	rreq->length = 6;
	rreq->id = htons(rreq_seqno++);
	rreq->target = target.s_addr;
	
	return rreq;
}

int __dsr_rreq_create(dsr_pkt_t *dp)
{
	struct in_addr dst;
	dsr_rreq_opt_t *rreq;
	char *off;
	int l;
	
	l = IP_HDR_LEN + DSR_OPT_HDR_LEN + DSR_RREQ_HDR_LEN;
	
	if (!dp->data || dp->len < l)
		return -1;

	dst.s_addr = DSR_BROADCAST;
	off = dp->skb->data;
	
	dsr_build_ip(dp->skb->data, l, dsr_node->ifaddr, dst, 1);

	off += IP_HDR_LEN;
	l -= IP_HDR_LEN;
	
	dsr_hdr_add(off, l, 0);
	     
	off += DSR_OPT_HDR_LEN;
	l -= DSR_OPT_HDR_LEN;

	rreq = dsr_rreq_opt_add(off, l, dp->dst);

	if (!rreq) {
		DEBUG("Could not create RREQ\n");
		return -1;
	}
	return 0;
}
int dsr_rreq_send(struct in_addr target)
{
	int res = 0;
	struct sockaddr broadcast = {AF_UNSPEC, {0xff, 0xff, 0xff, 0xff, 0xff, 0xff}};
	dsr_pkt_t *dp;
	
	dp = dsr_pkt_alloc();
	
	if (!dp) {
		return -1;
	}
	
	dp->len = IP_HDR_LEN + DSR_OPT_HDR_LEN + DSR_RREQ_HDR_LEN;
	
	dsr_node_lock(dsr_node);
	
	dp->skb = kdsr_skb_alloc(dp->len, dsr_node->slave_dev);
	
	if (!dp->skb) {
		res = -1;
		goto out_err;
	}
	
/* 	dp->skb->dev = dsr_node->slave_dev; */
	dp->data = dp->skb->data;
	dp->dst.s_addr = target.s_addr;
	
	res = __dsr_rreq_create(dp);
	
	dsr_node_unlock(dsr_node);

	if (res < 0) {
		DEBUG("Could not create RREQ\n");
		dev_kfree_skb(dp->skb);
		goto out_err;
	}
	
	res = dsr_dev_build_hw_hdr(dp->skb, &broadcast);
	
	if (res < 0) {
		DEBUG("RREQ transmission failed...\n");
		dev_kfree_skb(dp->skb);
		goto out_err;
	}

	DEBUG("Sending RREQ for %s headroom=%d len=%d tailroom=%d\n", print_ip(dp->dst.s_addr), skb_headroom(dp->skb), dp->skb->len, skb_tailroom(dp->skb));
	dev_queue_xmit(dp->skb);
	
 out_err:	
	dsr_pkt_free(dp);
	return res;
}

int dsr_rreq_recv(dsr_pkt_t *dp)
{
	dsr_rreq_opt_t *rreq;
	int ret = 0;
	
	if (!dp || !dp->rreq)
		return DSR_PKT_DROP;

	rreq = dp->rreq;

	dsr_node_lock(dsr_node);
	
	if (dp->src.s_addr == dsr_node->ifaddr.s_addr) {
		ret = DSR_PKT_DROP;
		goto out;
	}
	dp->srt = dsr_srt_new(dp->src, dsr_node->ifaddr, 
			      DSR_RREQ_ADDRS_LEN(rreq), (char *)rreq->addrs);
#ifdef __KERNEL__
	/* Add mac address of previous hop to the arp table */
	if (dp->skb->mac.raw) {
		struct sockaddr hw_addr;
		struct in_addr ph;
		struct ethhdr *eth;
		int n;
		/* struct net_device *dev = skb->dev; */
		
		eth = (struct ethhdr *)dp->skb->mac.raw;
		
		memcpy(hw_addr.sa_data, eth->h_source, ETH_ALEN);
		n = dp->srt->laddrs / sizeof(u_int32_t);
		/* Find the previous hop */
		if (n == 0)
			ph.s_addr = dp->srt->src.s_addr;
		else
			ph.s_addr = dp->srt->addrs[n-1].s_addr;
		
		kdsr_arpset(ph, &hw_addr, dp->skb->dev);
	/* 	dev_put(dev); */
	}
#endif
	DEBUG("RREQ target=%s\n", print_ip(rreq->target));
	DEBUG("my addr %s\n", print_ip(dsr_node->ifaddr.s_addr));

	if (rreq->target == dsr_node->ifaddr.s_addr) {
		dsr_srt_t *srt_rev;

		DEBUG("RREQ for me\n");
		
	
		DEBUG("srt: %s\n", print_srt(dp->srt));

		/* Add reversed source route */
		srt_rev = dsr_srt_new_rev(dp->srt);
		
		//DEBUG("srt_rev: %s\n", print_srt(srt_rev));

		dsr_rtc_add(srt_rev, 60000, 0);
		
		/* send rrep.... */
		dsr_rrep_send(dp->srt);
		
		/* Send buffered packets */
		p_queue_set_verdict(P_QUEUE_SEND, srt_rev->dst.s_addr);

		kfree(srt_rev);

	} else {
		int i, n;
		
		n = DSR_RREQ_ADDRS_LEN(rreq) / sizeof(struct in_addr);
		
		/* Examine source route if this node already exists in it */
		for (i = 0; i < n; i++)
			if (dp->srt->addrs[i].s_addr == dsr_node->ifaddr.s_addr) {
				ret = DSR_PKT_DROP;
				goto out;
			}		

		/* FIXME: Add myself to source route.... */
		
		/* Forward RREQ */
		ret = DSR_PKT_FORWARD;
	}

 out:
	dsr_node_unlock(dsr_node);
	       
	return ret;
}

