#ifdef __KERNEL__
#include <linux/slab.h>
#include <net/ip.h>
#endif

#ifdef NS2
#include "ns-agent.h"
#endif

#include "dsr.h"
#include "dsr-srt.h"
#include "dsr-opt.h"
#include "dsr-ack.h"
#include "link-cache.h"
#include "debug.h"

struct in_addr dsr_srt_next_hop(struct dsr_srt *srt, struct in_addr myaddr, int index)
{
	int n = srt->laddrs / sizeof(struct in_addr);
	struct in_addr nxt_hop;
	
	if (srt->src.s_addr == myaddr.s_addr) {
		if (srt->laddrs == 0)
			nxt_hop = srt->dst;
		else
			nxt_hop = srt->addrs[0];
	} else {
		
		/* The draft's usage of indexes into the source route is a bit
		 * confusing since they use arrays that start at position 1 */
		
		if ((index - n) == 0)
			nxt_hop = srt->dst;
		else
			nxt_hop = srt->addrs[index-1];
	}
	
/* 	DEBUG("Next hop for %s is %s\n",  */
/* 	      print_ip(srt->dst), print_ip(nxt_hop)); */

	return nxt_hop;
}

struct in_addr dsr_srt_prev_hop(struct dsr_srt *srt, struct in_addr myaddr)
{
	struct in_addr prev_hop;
	int n = srt->laddrs / sizeof(u_int32_t);
		
	/* Find the previous hop */
	if (n == 0)
		prev_hop = srt->src;
	else {
		int i;
		
		if (srt->dst.s_addr == myaddr.s_addr) {
			prev_hop = srt->addrs[n-1];
			goto out;
		}
		for (i = 0; i < n; i++) {
			if (srt->addrs[i].s_addr == myaddr.s_addr) {
				if (i == 0)
					prev_hop = srt->src;
				else 
					prev_hop = srt->addrs[i-1];
				goto out;
			}
/* 			DEBUG("Error! Previous hop not found!\n"); */
			return prev_hop;
		}
	}
 out:
/* 	DEBUG("Previous hop=%s\n", print_ip(prev_hop)); */
	return prev_hop;
}


struct dsr_srt *dsr_srt_new(struct in_addr src, struct in_addr dst,
		       unsigned int length, char *addrs)
{
	struct dsr_srt *sr;

	sr = (struct dsr_srt *)MALLOC(sizeof(struct dsr_srt) + length, GFP_ATOMIC);
	
	if (!sr)
		return NULL;

	sr->src.s_addr = src.s_addr;
	sr->dst.s_addr = dst.s_addr;
	sr->laddrs = length;
/* 	sr->index = index; */
	
	if (length != 0 && addrs)
		memcpy(sr->addrs, addrs, length);
	
	return sr;
}

struct dsr_srt *dsr_srt_new_rev(struct dsr_srt *srt)
{
	struct dsr_srt *srt_rev;
	int i, n;

	if (!srt)
		return NULL;
	
	srt_rev = (struct dsr_srt *)MALLOC(sizeof(struct dsr_srt) + 
					   srt->laddrs, GFP_ATOMIC);
	
	srt_rev->src.s_addr = srt->dst.s_addr;
	srt_rev->dst.s_addr = srt->src.s_addr;
	srt_rev->laddrs = srt->laddrs;

	n = srt->laddrs / sizeof(struct in_addr);

	for (i = 0; i < n; i++)
		srt_rev->addrs[i].s_addr = srt->addrs[n-1-i].s_addr;

	return srt_rev;
}

void dsr_srt_del(struct dsr_srt *srt)
{
	FREE(srt);
}


struct dsr_srt_opt *dsr_srt_opt_add(char *buf, int len, struct dsr_srt *srt)
{
	struct dsr_srt_opt *srt_opt;
	
	if (len < DSR_SRT_OPT_LEN(srt))
		return NULL;

	srt_opt = (struct dsr_srt_opt *)buf;

	srt_opt->type = DSR_OPT_SRT;
	srt_opt->length = srt->laddrs + 2;
	srt_opt->f = 0;
	srt_opt->l = 0;
	srt_opt->res = 0;
	SET_SALVAGE(srt_opt, 0);
	srt_opt->sleft = (srt->laddrs / sizeof(struct in_addr));
	
	memcpy(srt_opt->addrs, srt->addrs, srt->laddrs);
	
	return srt_opt;
}


int NSCLASS dsr_srt_add(struct dsr_pkt *dp)
{
	char *buf;
	int len, ttl, tot_len, ip_len;
	int prot = 0;
		
	if (!dp || !dp->srt)
		return -1;

	dp->nxt_hop = dsr_srt_next_hop(dp->srt, dp->src, 0);

	/* Calculate extra space needed */

	len = DSR_OPT_HDR_LEN + DSR_SRT_OPT_LEN(dp->srt);

/* 	DEBUG("dsr_opts_len=%d\n", len); */
	
	buf = dsr_pkt_alloc_opts(dp, len);

	if (!buf) {
/* 		DEBUG("Could allocate memory\n"); */
		return -1;
	}

#ifdef NS2
	if (dp->p) {
		hdr_cmn *cmh = HDR_CMN(dp->p);
		prot = cmh->ptype();
	} else 
		prot = PT_NTYPE;
	
	ip_len = IP_HDR_LEN;
	tot_len = dp->payload_len + ip_len + len;
	ttl = dp->nh.iph->ttl();
#else
	prot = dp->nh.iph->protocol;
	ip_len = (dp->nh.iph->ihl << 2);
	tot_len =  ntohs(dp->nh.iph->tot_len) + len;
	ttl = dp->nh.iph->ttl;
#endif	
	dp->nh.iph = dsr_build_ip(dp, dp->src, dp->dst, ip_len, tot_len, 
				  IPPROTO_DSR, ttl);
	
	if (!dp->nh.iph) 
		return -1;

	dp->dh.opth = dsr_opt_hdr_add(buf, len, prot);

	if (!dp->dh.opth) {
/* 		DEBUG("Could not create DSR opts header!\n"); */
		return -1;
	}

	buf += DSR_OPT_HDR_LEN;
	len -= DSR_OPT_HDR_LEN;

	dp->srt_opt = dsr_srt_opt_add(buf, len, dp->srt);

	if (!dp->srt_opt) {
/* 		DEBUG("Could not create Source Route option header!\n"); */
		return -1;
	}

	buf += DSR_SRT_OPT_LEN(dp->srt);
	len -= DSR_SRT_OPT_LEN(dp->srt);
	
	return 0;
}

int NSCLASS dsr_srt_opt_recv(struct dsr_pkt *dp)
{
	int n;	
	
	if (!dp || !dp->srt_opt)
		return DSR_PKT_ERROR;
	
	/* We should add this source route info to the cache... */
	n = (dp->srt_opt->length - 2) / sizeof(struct in_addr);
	
	dp->srt = dsr_srt_new(dp->src, dp->dst, dp->srt_opt->length, 
			      (char *)dp->srt_opt->addrs);
	
	if (!dp->srt) {
		DEBUG("Create source route failed\n");
		return DSR_PKT_ERROR;
	}
	
	DEBUG("Source route: %s\n", print_srt(dp->srt));


	dsr_rtc_add(dp->srt, 60000, 0);
	
	if (dp->srt_opt->sleft == 0) {
	/* 	DEBUG("Remove source route...\n"); */
		return DSR_PKT_SRT_REMOVE;
	}
	
	if (dp->srt_opt->sleft > n) {
		// Send ICMP parameter error
		return DSR_PKT_SEND_ICMP;
	} else {
		int i;
		
		if (dp->srt_opt->sleft > n) {
			DEBUG("segments left=%d larger than n=%d\n", 
			      dp->srt_opt->sleft, n);
			return DSR_PKT_ERROR;
		}
		
		dp->srt_opt->sleft--;	
		i = n - dp->srt_opt->sleft;
		

		/* Fill in next hop */
		dp->nxt_hop = dsr_srt_next_hop(dp->srt, my_addr(), i);

		DEBUG("Setting next hop %s and forward n=%d i=%d\n", 
		      print_ip(dp->nxt_hop), n, i);
		/* TODO: check for multicast address in next hop or dst */
		/* TODO: check MTU and compare to pkt size */
	
		return DSR_PKT_FORWARD;
	}
	return DSR_PKT_ERROR;
}
