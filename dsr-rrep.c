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
	struct dsr_srt *srt_to_me;
	struct dsr_pad1_opt *pad1_opt;

	if (!dp || !dp->srt)
		return -1;
	
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
		DEBUG("Could not create DSR options header\n");
		return -1;
	}

	buf += DSR_OPT_HDR_LEN;
	len -= DSR_OPT_HDR_LEN;

	srt_to_me = dsr_srt_new_rev(dp->srt);
	
	if (!srt_to_me)
		return -1;
	
	
	/* Add the source route option to the packet */
	dp->srt_opt = dsr_srt_opt_add(buf, len, srt_to_me);

	kfree(srt_to_me);

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

	buf += DSR_RREP_OPT_LEN(dp->srt);
	len -= DSR_RREP_OPT_LEN(dp->srt);
	
	pad1_opt = (struct dsr_pad1_opt *)buf;
	pad1_opt->type = DSR_OPT_PAD1;
	
	return 0;
}


int dsr_rrep_send(struct dsr_srt *srt)
{
	struct dsr_pkt dp;
	struct in_addr my_addr;
	int len, res;
	char *buf;

	if (!srt) {
		DEBUG("no source route!\n");
		return -1;
	}	
	
	memset(&dp, 0, sizeof(dp));
	
	dsr_node_lock(dsr_node);
	my_addr = dsr_node->ifaddr;
	dsr_node_unlock(dsr_node);

	dp.dst = srt->dst;
	dp.src = my_addr;
	dp.nxt_hop = dsr_srt_next_hop(srt);
	dp.srt = srt;

	len = IP_HDR_LEN + DSR_OPT_HDR_LEN + DSR_SRT_OPT_LEN(srt) + DSR_RREP_OPT_LEN(srt) + DSR_OPT_PAD1_LEN;
	
	buf = kmalloc(len, GFP_ATOMIC);
	
	if (!buf)
		return -1;
	
	DEBUG("IP_HDR_LEN=%d DSR_OPT_HDR_LEN=%d DSR_SRT_OPT_LEN=%d DSR_RREP_OPT_LEN=%d DSR_OPT_PAD1_LEN=%d RREP len=%d\n", IP_HDR_LEN, DSR_OPT_HDR_LEN, DSR_SRT_OPT_LEN(srt), DSR_RREP_OPT_LEN(srt), DSR_OPT_PAD1_LEN, len);
	
	
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
	struct dsr_srt *rrep_opt_srt;

	if (!dp || !dp->rrep_opt)
		return DSR_PKT_DROP;

	dsr_node_lock(dsr_node);
	my_addr = dsr_node->ifaddr;
	dsr_node_unlock(dsr_node);
	
	rrep_opt_srt = dsr_srt_new(dp->dst, dp->src, 
			      DSR_RREP_ADDRS_LEN(dp->rrep_opt), 
			      (char *)dp->rrep_opt->addrs);
	
	if (!rrep_opt_srt)
		return DSR_PKT_DROP;
	
	DEBUG("Adding srt to cache\n");
	dsr_rtc_add(rrep_opt_srt, 60000, 0);
	
	kfree(rrep_opt_srt);
	
	if (dp->dst.s_addr == my_addr.s_addr) {
		/*RREP for this node */
		
		DEBUG("RREP for me!\n");
				
		return DSR_PKT_DROP | DSR_PKT_SEND_BUFFERED;				
	}
	
	DEBUG("I am not RREP destination\n");
	/* Forward */
	return DSR_PKT_FORWARD;	
}
