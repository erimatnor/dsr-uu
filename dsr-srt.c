#include <linux/slab.h>
#include <net/ip.h>

#include "dsr.h"
#include "dsr-srt.h"
#include "dsr-opt.h"
#include "dsr-ack.h"
#include "dsr-rtc.h"
#include "debug.h"


struct in_addr dsr_srt_next_hop(struct dsr_srt *srt, int index)
{
	int n = srt->laddrs / sizeof(struct in_addr);
	struct in_addr nxt_hop, myaddr = my_addr();
	
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
	
	DEBUG("Next hop for %s is %s\n", print_ip(srt->dst.s_addr), 
	      print_ip(nxt_hop.s_addr));

	return nxt_hop;
}

struct in_addr dsr_srt_prev_hop(struct dsr_srt *srt)
{
	struct in_addr prev_hop;
	int n = srt->laddrs / sizeof(u_int32_t);
		
	/* Find the previous hop */
	if (n == 0)
		prev_hop = srt->src;
	else {
		int i;
		struct in_addr myaddr = my_addr();
		
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
			DEBUG("Error! Previous hop not found!\n");
			return prev_hop;
		}
	}
 out:
	DEBUG("Previous hop=%s\n", print_ip(prev_hop.s_addr));
	return prev_hop;
}

char *print_srt(struct dsr_srt *srt)
{
#define BUFLEN 256
	static char buf[BUFLEN];
	int i, len;

	if (!srt)
		return NULL;
	
	len = sprintf(buf, "%s<->", print_ip(srt->src.s_addr));
	
	for (i = 0; i < (srt->laddrs / sizeof(u_int32_t)) && 
		     (len + 16) < BUFLEN; i++)
		len += sprintf(buf+len, "%s<->", print_ip(srt->addrs[i].s_addr));
	
	if ((len + 16) < BUFLEN)
		len = sprintf(buf+len, "%s", print_ip(srt->dst.s_addr));
	return buf;
}

struct dsr_srt *dsr_srt_new(struct in_addr src, struct in_addr dst,
		       unsigned int length, char *addrs)
{
	struct dsr_srt *sr;

	sr = kmalloc(sizeof(struct dsr_srt) + length, GFP_ATOMIC);
	
	if (!sr)
		return NULL;

	sr->src.s_addr = src.s_addr;
	sr->dst.s_addr = dst.s_addr;
	sr->laddrs = length;
	
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
	
	srt_rev = kmalloc(sizeof(struct dsr_srt) + srt->laddrs, GFP_ATOMIC);
	
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
	kfree(srt);
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


int dsr_srt_add(struct dsr_pkt *dp)
{
	char *buf;
	int len;
	int add_ack_req = 0;
	
	if (!dp || !dp->srt || !dp->nh.iph)
		return -1;

	dp->nxt_hop = dsr_srt_next_hop(dp->srt, 0);

	/* Calculate extra space needed */
	add_ack_req = dsr_ack_add_ack_req(dp->nxt_hop);

	if (add_ack_req)
		
		len = DSR_OPT_HDR_LEN + DSR_SRT_OPT_LEN(dp->srt) + DSR_ACK_REQ_HDR_LEN;
	else
		len = DSR_OPT_HDR_LEN + DSR_SRT_OPT_LEN(dp->srt);

	dp->dsr_opts_len = len;
	
	DEBUG("dsr_opts_len=%d\n", dp->dsr_opts_len);
	

	buf = kmalloc(len, GFP_ATOMIC);

	if (!buf) {
		DEBUG("Could allocate memory\n");
		return -1;
	}
	
	dp->dh.opth = dsr_opt_hdr_add(buf, len, dp->nh.iph->protocol);

	if (!dp->dh.opth) {
		DEBUG("Could not create DSR opts header!\n");
		kfree(buf);
		return -1;
	}

	buf += DSR_OPT_HDR_LEN;
	len -= DSR_OPT_HDR_LEN;
	
	dp->srt_opt = dsr_srt_opt_add(buf, len, dp->srt);

	if (!dp->srt_opt) {
		DEBUG("Could not create Source Route option header!\n");
		kfree(dp->dh.raw);
		return -1;
	}

	buf += DSR_SRT_OPT_LEN(dp->srt);
	len -= DSR_SRT_OPT_LEN(dp->srt);
	
	if (add_ack_req) {
		struct dsr_ack_req_opt *areq;
		
		areq = dsr_ack_req_opt_add(buf, len, dp->nxt_hop);

		if (!areq) {
			DEBUG("Could not create ACK REQ option header!\n");
			kfree(dp->dh.raw);
			return -1;
		}
		buf += DSR_ACK_REQ_HDR_LEN;
		len -= DSR_ACK_REQ_HDR_LEN;
	}

	dp->nh.iph->tot_len = 
		htons(ntohs(dp->nh.iph->tot_len) + dp->dsr_opts_len);
	
	DEBUG("New iph->tot_len=%d\n", ntohs(dp->nh.iph->tot_len));

	dp->nh.iph->protocol = IPPROTO_DSR;
	
	ip_send_check(dp->nh.iph);

	return 0;
}

int dsr_srt_opt_recv(struct dsr_pkt *dp)
{
	int n;	
	
	if (!dp || !dp->srt_opt)
		return DSR_PKT_ERROR;
	
	dp->srt = dsr_srt_new(dp->src, dp->dst, dp->srt_opt->length, 
			      (char *)dp->srt_opt->addrs);
	
	if (!dp->srt) {
		DEBUG("Create source route failed\n");
		return DSR_PKT_ERROR;
	}
	
	DEBUG("Source route: %s\n", print_srt(dp->srt));

	/* We should add this source route info to the cache... */
	n = (dp->srt_opt->length - 2) / sizeof(struct in_addr);

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
		dp->nxt_hop = dsr_srt_next_hop(dp->srt, i);

		DEBUG("Setting next hop %s and forward n=%d i=%d\n", 
		      print_ip(dp->nxt_hop.s_addr), n, i);
		/* TODO: check for multicast address in next hop or dst */
		/* TODO: check MTU and compare to pkt size */
	
		return DSR_PKT_FORWARD;
	}
	return DSR_PKT_ERROR;
}
