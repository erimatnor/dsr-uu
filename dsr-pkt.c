#include <linux/skbuff.h>
#include <linux/if_ether.h>

#include "dsr.h"
#include "dsr-opt.h"

char *dsr_pkt_alloc_opts(struct dsr_pkt *dp, int len)
{	
	if (!dp && len)
		return NULL;
	
	dp->dsr_opts = kmalloc(len + DEFAULT_TAILROOM, GFP_ATOMIC);

	if (!dp->dsr_opts)
		return NULL;
	
	dp->end = dp->dsr_opts + len + DEFAULT_TAILROOM;
	dp->tail = dp->dsr_opts + len;

	return dp->dsr_opts;
}

char *dsr_pkt_alloc_opts_expand(struct dsr_pkt *dp, int len)
{
	char *tmp;
	int old_len;
	
	if (!dp || !dp->dsr_opts)
		return NULL;

	if (dsr_pkt_tailroom(dp) > len) {
		tmp = dp->tail;
		dp->tail = dp->tail + len;
		return tmp;
	} 
		
	tmp = dp->dsr_opts;
	old_len = dsr_pkt_opts_len(dp);

	if (!dsr_pkt_alloc_opts(dp, old_len + len))
		return NULL;
	
	memcpy(dp->dsr_opts, tmp, old_len);

	kfree(tmp);

	return (dp->dsr_opts + old_len);
}

int dsr_pkt_free_opts(struct dsr_pkt *dp)
{
	int len;
	
	if (!dp->dsr_opts)
		return -1;
	
	len = dsr_pkt_opts_len(dp);

	kfree(dp->dsr_opts);
	
	dp->dh.raw = dp->dsr_opts = dp->end = dp->tail = NULL;
	dp->srt_opt = NULL;
	dp->rreq_opt = NULL;
	memset(dp->rrep_opt, 0, sizeof(struct dsr_rrep_opt *) * MAX_RREP_OPTS);
	memset(dp->rerr_opt, 0, sizeof(struct dsr_rerr_opt *) * MAX_RERR_OPTS);
	memset(dp->ack_opt, 0, sizeof(struct dsr_ack_opt *) * MAX_ACK_OPTS);
	dp->num_rrep_opts = dp->num_rerr_opts = dp->num_ack_opts = 0;

	return len;
}

struct dsr_pkt *dsr_pkt_alloc(struct sk_buff *skb)
{
	struct dsr_pkt *dp;
	int dsr_opts_len = 0;

	dp = kmalloc(sizeof(struct dsr_pkt), GFP_ATOMIC);

	if (!dp)
		return NULL;
	
	memset(dp, 0, sizeof(struct dsr_pkt));
		
	if (skb) {		
		dp->skb = skb;
		dp->nh.iph = skb->nh.iph;
		
		dp->src.s_addr = skb->nh.iph->saddr;
		dp->dst.s_addr = skb->nh.iph->daddr;

		if (dp->nh.iph->protocol == IPPROTO_DSR) {
			dp->dh.raw = dp->nh.raw + (dp->nh.iph->ihl << 2);
			dsr_opts_len = ntohs(dp->dh.opth->p_len) + DSR_OPT_HDR_LEN;

			if (!dsr_pkt_alloc_opts(dp, dsr_opts_len)) {
				kfree(dp);
				return NULL;
			}
			
			memcpy(dp->dsr_opts, dp->dh.raw, dsr_opts_len);
			dp->dh.raw = dp->dsr_opts;
		} 		

		dp->payload = dp->nh.raw + 
			(dp->nh.iph->ihl << 2) + dsr_opts_len;
		
		dp->payload_len = ntohs(dp->nh.iph->tot_len) - 
			(dp->nh.iph->ihl << 2) - dsr_opts_len;
	}
       	
	return dp;
}


void dsr_pkt_free(struct dsr_pkt *dp)
{
	
	if (!dp)
		return;
	
	if (dp->skb)
		kfree_skb(dp->skb);

	dsr_pkt_free_opts(dp);

	if (dp->srt)
		kfree(dp->srt);

	kfree(dp);
	
	return;
}
