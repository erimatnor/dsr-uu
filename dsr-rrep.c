#include <linux/string.h>
#include <linux/if_ether.h>
#include <net/ip.h>

#include "dsr.h"
#include "debug.h"
#include "dsr-rrep.h"
#include "dsr-opt.h"
#include "dsr-srt.h"
#include "dsr-rtc.h"
#include "dsr-dev.h"
#include "p-queue.h"
#include "kdsr.h"

static inline int dsr_rrep_add_srt(struct dsr_rrep_opt *rrep_opt, struct dsr_srt *srt)
{
	int n;

	if (!rrep_opt | !srt)
		return -1;

	n = srt->laddrs / sizeof(struct in_addr);

	memcpy(rrep_opt->addrs, srt->addrs, srt->laddrs);
	memcpy(&rrep_opt->addrs[n], &srt->dst, sizeof(struct in_addr));
	
	return 0;
}

static struct dsr_rrep_opt *dsr_rrep_opt_add(char *buf, int len, struct dsr_srt *srt)
{
	struct dsr_rrep_opt *rrep_opt;
	
	if (!buf || !srt || len < DSR_RREP_OPT_LEN(srt))
		return NULL;

	rrep_opt = (struct dsr_rrep_opt *)buf;
	
	rrep_opt->type = DSR_OPT_RREP;
	rrep_opt->length = srt->laddrs + sizeof(struct in_addr) + 1;
	rrep_opt->l = 0;
	rrep_opt->res = 0;

	/* Add source route to RREP */
	dsr_rrep_add_srt(rrep_opt, srt);
       
	return rrep_opt;	
}

int dsr_rrep_create(struct dsr_pkt *dp, char *buf, int len)
{
	struct dsr_srt *srt_rev;
	struct in_addr my_addr;
	
	if (!dp || !dp->srt)
		return -1;
	
	dp->dst.s_addr = dp->srt->src.s_addr;

	dsr_node_lock(dsr_node);
	my_addr = dsr_node->ifaddr;
	dsr_node_unlock(dsr_node);

	dp->iph = dsr_build_ip(buf, len, my_addr, dp->dst, 1);
	
	if (!dp->iph) {
		DEBUG("Could not create IP header\n");
		return -1;
	}

	buf += IP_HDR_LEN;
	len -= IP_HDR_LEN;
	
	dp->dsr_opts_len = len;

	dp->opt_hdr = dsr_opt_hdr_add(buf, len, 0);
	
	if (!dp->opt_hdr) {
		DEBUG("Could not create DSR options header\n");
		return -1;
	}

	buf += DSR_OPT_HDR_LEN;
	len -= DSR_OPT_HDR_LEN;

/* /\* 	srt_rev = dsr_srt_new_rev(dp->srt); *\/ */
	
	srt_rev = dsr_rtc_find(dp->dst);
	
	if (!srt_rev)
		return -1;
	
	if (srt_rev->laddrs == 0) {
		DEBUG("source route is one hop\n");
		dp->nxt_hop.s_addr = srt_rev->dst.s_addr;
	} else
		dp->nxt_hop.s_addr = srt_rev->addrs[0].s_addr;
	
	/* Add the source route option to the packet */
	dp->srt_opt = dsr_srt_opt_add(buf, len, srt_rev);

	kfree(srt_rev);

	if (!dp->srt_opt) {
		DEBUG("Could not create Source Route option header\n");
		return -1;
	}

	buf += DSR_SRT_OPT_LEN(dp->srt);
	len -= DSR_SRT_OPT_LEN(dp->srt);

	dp->rrep_opt = dsr_rrep_opt_add(buf, len, dp->srt);

	if (!dp->rrep_opt) {
		DEBUG("Could not create RREP option header\n");
		return -1;
	}
	return 0;
}


int dsr_rrep_send(struct dsr_srt *srt)
{
	struct dsr_pkt dp;
	int len, res;
	char *buf;

	if (!srt) {
		DEBUG("no source route!\n");
		return -1;
	}
	
	
	len = IP_HDR_LEN + DSR_OPT_HDR_LEN + DSR_SRT_OPT_LEN(srt) + DSR_RREP_OPT_LEN(srt);
	
	buf = kmalloc(len, GFP_ATOMIC);
	
	if (!buf)
		return -1;
	
	DEBUG("RREP len=%d\n", len);
	
	/* Make a copy of the source route */
	dp.srt = srt;
	
	DEBUG("srt: %s\n", print_srt(srt));

	dp.data = NULL;
	dp.data_len = 0;
	
	res = dsr_rrep_create(&dp, buf, len);


	if (res < 0) {
		DEBUG("Could not create RREP\n");
	} else 
		dsr_dev_xmit(&dp);
	
	kfree(buf);
	return 0;
}

int dsr_rrep_opt_recv(struct dsr_pkt *dp)
{
	struct in_addr my_addr;
	
	if (!dp || !dp->rrep_opt)
		return DSR_PKT_DROP;

	dsr_node_lock(dsr_node);
	my_addr = dsr_node->ifaddr;
	dsr_node_unlock(dsr_node);
	
	dp->srt = dsr_srt_new(dp->dst, dp->src, 
			      DSR_RREP_ADDRS_LEN(dp->rrep_opt), 
			      (char *)dp->rrep_opt->addrs);
	
	if (!dp->srt)
		return DSR_PKT_DROP;
	
	DEBUG("Adding srt to cache\n");
	dsr_rtc_add(dp->srt, 60000, 0);
	
	if (dp->dst.s_addr == my_addr.s_addr) {
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
