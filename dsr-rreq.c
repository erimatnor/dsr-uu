#include <net/ip.h>

#include "debug.h"
#include "dsr.h"
#include "kdsr.h"
#include "dsr-rrep.h"
#include "dsr-rreq.h"
#include "dsr-opt.h"
#include "dsr-rtc.h"
#include "dsr-dev.h"
#include "p-queue.h"

static unsigned int rreq_seqno = 1;

static struct dsr_rreq_opt *dsr_rreq_opt_add(char *buf, int len, 
					struct in_addr target)
{
	struct dsr_rreq_opt *rreq_opt;

	if (!buf || len < DSR_RREQ_HDR_LEN)
		return NULL;

	rreq_opt = (struct dsr_rreq_opt *)buf;
	
	rreq_opt->type = DSR_OPT_RREQ;
	rreq_opt->length = 6;
	rreq_opt->id = htons(rreq_seqno++);
	rreq_opt->target = target.s_addr;
	
	return rreq_opt;
}

int dsr_rreq_create(struct dsr_pkt *dp, struct in_addr target, char *buf, int len)
{
	
	dp->iph = dsr_build_ip(buf, len, dp->src, dp->dst, 1);
	
	if (!dp->iph) {
		DEBUG("Could not create IP header\n");
		return -1;
	}
	
	buf += IP_HDR_LEN;
	len -= IP_HDR_LEN;

	dp->dsr_opts_len = len;

	dp->opt_hdr = dsr_opt_hdr_add(buf, len, 0);
	
	if (!dp->opt_hdr) {
		DEBUG("Could not create DSR opt header\n");
		return -1;
	}
	
	buf += DSR_OPT_HDR_LEN;
	len -= DSR_OPT_HDR_LEN;
	
	dp->rreq_opt = dsr_rreq_opt_add(buf, len, target);

	if (!dp->rreq_opt) {
		DEBUG("Could not create RREQ opt\n");
		return -1;
	}
	
	return 0;
}
int dsr_rreq_send(struct in_addr target)
{
	struct dsr_pkt dp;
	char buf[IP_HDR_LEN + DSR_OPT_HDR_LEN + DSR_RREQ_HDR_LEN];
	int len = IP_HDR_LEN + DSR_OPT_HDR_LEN + DSR_RREQ_HDR_LEN;
	int res = 0;
	
	dp.data = NULL; /* No data in this packet */
	dp.dst.s_addr = DSR_BROADCAST;
	dp.nxt_hop.s_addr = DSR_BROADCAST;
	
	dsr_node_lock(dsr_node);
	dp.src = dsr_node->ifaddr;
	dsr_node_unlock(dsr_node);

	res = dsr_rreq_create(&dp, target, buf, len);
	
	if (res < 0) {
		DEBUG("Could not create RREQ\n");
		return -1;
	}
	
	dsr_dev_xmit(&dp);
	
	return 0;
}

int dsr_rreq_opt_recv(struct dsr_pkt *dp)
{
	struct in_addr my_addr;

	if (!dp || !dp->rreq_opt)
		return DSR_PKT_DROP;


	dsr_node_lock(dsr_node);
	my_addr = dsr_node->ifaddr;
	dsr_node_unlock(dsr_node);
	
	if (dp->src.s_addr == my_addr.s_addr)
		return DSR_PKT_DROP;
	
	dp->srt = dsr_srt_new(dp->src, my_addr, 
			      DSR_RREQ_ADDRS_LEN(dp->rreq_opt), 
			      (char *)dp->rreq_opt->addrs);

	DEBUG("RREQ target=%s\n", print_ip(dp->rreq_opt->target));
	DEBUG("my addr %s\n", print_ip(my_addr.s_addr));

	if (dp->rreq_opt->target == my_addr.s_addr) {
		struct dsr_srt *srt_rev;

		DEBUG("RREQ_OPT for me\n");
		
	
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
		
		n = DSR_RREQ_ADDRS_LEN(dp->rreq_opt) / sizeof(struct in_addr);
		
		/* Examine source route if this node already exists in it */
		for (i = 0; i < n; i++)
			if (dp->srt->addrs[i].s_addr == my_addr.s_addr) {
				return DSR_PKT_DROP;
			}

		/* FIXME: Add myself to source route.... */
		
		/* Forward RREQ */
		return DSR_PKT_FORWARD;
	}

	return DSR_PKT_DROP;
}

