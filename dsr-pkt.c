#include <linux/skbuff.h>

#include "dsr.h"


char *dsr_pkt_alloc_data(struct dsr_pkt *dp, int len)
{	
	if (!dp && len)
		return NULL;

	dp->dsr_data = kmalloc(len, GFP_ATOMIC);
		
	dp->dsr_data_len = len;

	return dp->dsr_data;
}

struct dsr_pkt *dsr_pkt_alloc(struct sk_buff *skb, int len)
{
	struct dsr_pkt *dp;

	dp = kmalloc(sizeof(struct dsr_pkt), GFP_ATOMIC);

	if (!dp)
		return NULL;
	
	memset(dp, 0, sizeof(struct dsr_pkt));

	
	if (len && !dsr_pkt_alloc_data(dp, len)) {
		kfree(dp);
		return NULL;
	}
		
	if (skb) {
		dp->skb = skb;
		dp->nh.iph = skb->nh.iph;
		dp->dh.raw = skb->data;
		
		dp->src.s_addr = skb->nh.iph->saddr;
		dp->dst.s_addr = skb->nh.iph->daddr;
	}
	return dp;
}


void dsr_pkt_free(struct dsr_pkt *dp)
{
	
	if (!dp)
		return;
	
	if (dp->skb)
		kfree_skb(dp->skb);
	
/* 	if (dp->dh.raw) */
/* 		kfree(dp->dh.raw); */

	if (dp->dsr_data)
		kfree(dp->dsr_data);

	if (dp->srt)
		kfree(dp->srt);

	kfree(dp);
	
	return;
}
