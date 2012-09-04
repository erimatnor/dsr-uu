/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */
/* Copyright (C) Uppsala University
 *
 * This file is distributed under the terms of the GNU general Public
 * License (GPL), see the file LICENSE
 *
 * Author: Erik Nordström, <erik.nordstrom@gmail.com>
 */
#include "platform.h"
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
#include "neigh.h"
#include "dsr-rrep.h"
#include "debug.h"

struct in_addr dsr_srt_next_hop(struct dsr_srt *srt, int sleft)
{
	int n = srt->laddrs / sizeof(struct in_addr);
	struct in_addr nxt_hop;

	if (sleft <= 0)
		nxt_hop = srt->dst;
	else
		nxt_hop = srt->addrs[n - sleft];

	return nxt_hop;
}

struct in_addr dsr_srt_prev_hop(struct dsr_srt *srt, int sleft)
{
	struct in_addr prev_hop;
	int n = srt->laddrs / sizeof(u_int32_t);

	if (n - 1 == sleft)
		prev_hop = srt->src;
	else
		prev_hop = srt->addrs[n - 2 - (sleft)];

	return prev_hop;
}

static int dsr_srt_find_addr(struct dsr_srt *srt, struct in_addr addr, 
			      int sleft)
{
	int n = srt->laddrs / sizeof(struct in_addr);

	if (n == 0 || sleft > n || sleft < 1)
		return 0;

	for (; sleft > 0; sleft--)
		if (srt->addrs[n - sleft].s_addr == addr.s_addr)
			return 1;

	if (addr.s_addr == srt->dst.s_addr)
		return 1;

	return 0;
}

struct dsr_srt *dsr_srt_new(struct in_addr src, struct in_addr dst,
			    unsigned int length, char *addrs)
{
	struct dsr_srt *sr;

	sr = (struct dsr_srt *)kmalloc(sizeof(struct dsr_srt) + length,
				       GFP_ATOMIC);

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

	srt_rev = (struct dsr_srt *)kmalloc(sizeof(struct dsr_srt) +
					    srt->laddrs, GFP_ATOMIC);

	if (!srt_rev)
		return NULL;

	srt_rev->src.s_addr = srt->dst.s_addr;
	srt_rev->dst.s_addr = srt->src.s_addr;
	srt_rev->laddrs = srt->laddrs;

	n = srt->laddrs / sizeof(struct in_addr);

	for (i = 0; i < n; i++)
		srt_rev->addrs[i].s_addr = srt->addrs[n - 1 - i].s_addr;

	return srt_rev;
}

/* Split source route add specified address. */
struct dsr_srt *dsr_srt_new_split(struct dsr_srt *srt, struct in_addr addr)
{
	struct dsr_srt *srt_split;
	int i, n;

	if (!srt)
		return NULL;

	n = srt->laddrs / sizeof(struct in_addr);

	if (n == 0)
		return NULL;

	for (i = 0; i < n; i++) {
		if (addr.s_addr == srt->addrs[i].s_addr)
			goto split;
	}
	/* Split address not found - Nothing to split */
	return NULL;

      split:
	srt_split = (struct dsr_srt *)kmalloc(sizeof(struct dsr_srt) +
					      (i * sizeof(struct in_addr)),
					      GFP_ATOMIC);
	
	if (!srt_split)
		return NULL;

	srt_split->src.s_addr = srt->src.s_addr;
	srt_split->dst.s_addr = srt->addrs[i].s_addr;
	srt_split->laddrs = sizeof(struct in_addr) * i;

	memcpy(srt_split->addrs, srt->addrs, sizeof(struct in_addr) * i);

	return srt_split;
}

struct dsr_srt *dsr_srt_new_split_rev(struct dsr_srt *srt, struct in_addr addr)
{
	struct dsr_srt *srt_split, *srt_split_rev;

	srt_split = dsr_srt_new_split(srt, addr);

	if (!srt_split)
		return NULL;

	srt_split_rev = dsr_srt_new_rev(srt_split);

	kfree(srt_split);

	return srt_split_rev;
}

struct dsr_srt *dsr_srt_shortcut(struct dsr_srt *srt, struct in_addr a1,
				 struct in_addr a2)
{
	struct dsr_srt *srt_cut;
	int i, j, n, n_cut, a1_num, a2_num;

	if (!srt)
		return NULL;

	a1_num = a2_num = -1;

	n = srt->laddrs / sizeof(struct in_addr);

	if (srt->src.s_addr == a1.s_addr)
		a1_num = 0;

	/* Find out how between which node indexes to shortcut */
	for (i = 0; i < n; i++) {
		if (srt->addrs[i].s_addr == a1.s_addr)
			a1_num = i + 1;
		if (srt->addrs[i].s_addr == a2.s_addr)
			a2_num = i + 1;
	}

	if (srt->dst.s_addr == a2.s_addr)
		a2_num = i + 1;

	n_cut = n - (a2_num - a1_num - 1);

	srt_cut = (struct dsr_srt *)kmalloc(sizeof(struct dsr_srt) +
					    (n_cut*sizeof(struct in_addr)),
					    GFP_ATOMIC);
	
	if (!srt_cut)
		return NULL;

	srt_cut->src = srt->src;
	srt_cut->dst = srt->dst;
	srt_cut->laddrs = n_cut * sizeof(struct in_addr);

	if (srt_cut->laddrs == 0)
		return srt_cut;

	j = 0;

	for (i = 0; i < n; i++) {
		if (i + 1 > a1_num && i + 1 < a2_num)
			continue;
		srt_cut->addrs[j++] = srt->addrs[i];
	}

	return srt_cut;
}

struct dsr_srt *dsr_srt_concatenate(struct dsr_srt *srt1, struct dsr_srt *srt2)
{
	struct dsr_srt *srt_cat;
	int n, n1, n2;
	
	if (!srt1 || !srt2)
		return NULL;
	
	n1 = srt1->laddrs / sizeof(struct in_addr);
	n2 = srt2->laddrs / sizeof(struct in_addr);
	
	/* We assume that the end node of the first srt is the same as the start
	 * of the second. We therefore only count that node once. */
	n = n1 + n2 + 1;
	
	srt_cat = (struct dsr_srt *)kmalloc(sizeof(struct dsr_srt) +
					    (n * sizeof(struct in_addr)),
					    GFP_ATOMIC);
	
	if (!srt_cat)
		return NULL;
	
	srt_cat->src = srt1->src;
	srt_cat->dst = srt2->dst;
	srt_cat->laddrs = n * sizeof(struct in_addr);

	memcpy(srt_cat->addrs, srt1->addrs, n1 * sizeof(struct in_addr));
	memcpy(srt_cat->addrs + n1, &srt2->src, sizeof(struct in_addr));
	memcpy(srt_cat->addrs + n1 + 1, srt2->addrs, n2 * sizeof(struct in_addr));

	return srt_cat;
}


int dsr_srt_check_duplicate(struct dsr_srt *srt)
{
	struct in_addr *buf;
	int n, i, res = 0;
	
	n = srt->laddrs / sizeof(struct in_addr);

	buf = (struct in_addr *)kmalloc(sizeof(struct in_addr) * (n + 1), 
					GFP_ATOMIC);
	
	if (!buf) 
		return -1;

	buf[0] = srt->src;
		
	for (i = 0; i < n; i++) {
		int j;
		
		for (j = 0; j < i + 1; j++)
			if (buf[j].s_addr == srt->addrs[i].s_addr) {
				res = 1;
				goto out;
			}		
		buf[i+1] = srt->addrs[i];
	}
	
	for (i = 0; i < n + 1; i++)
		if (buf[i].s_addr == srt->dst.s_addr) {
			res = 1;
			goto out;
		}
 out:
	kfree(buf);

	return res;
}
struct dsr_srt_opt *dsr_srt_opt_add(char *buf, int len, int flags, 
				    int salvage, struct dsr_srt *srt)
{
	struct dsr_srt_opt *srt_opt;

	if (len < (int)DSR_SRT_OPT_LEN(srt))
		return NULL;

	srt_opt = (struct dsr_srt_opt *)buf;

	srt_opt->type = DSR_OPT_SRT;
	srt_opt->length = srt->laddrs + 2;
	srt_opt->f = (flags & SRT_FIRST_HOP_EXT) ? 1 : 0;
	srt_opt->l = (flags & SRT_LAST_HOP_EXT) ? 1 : 0;
	srt_opt->res = 0;
	srt_opt->salv = salvage;
	srt_opt->sleft = (srt->laddrs / sizeof(struct in_addr));

	memcpy(srt_opt->addrs, srt->addrs, srt->laddrs);

	return srt_opt;
}

int NSCLASS dsr_srt_add(struct dsr_pkt *dp)
{
	char *buf;
	int n, len, ttl, tot_len, ip_len;
	int prot = 0;

	if (!dp || !dp->srt)
		return -1;

	n = dp->srt->laddrs / sizeof(struct in_addr);

	dp->nxt_hop = dsr_srt_next_hop(dp->srt, n);

	/* Calculate extra space needed */

	len = DSR_OPT_HDR_LEN + DSR_SRT_OPT_LEN(dp->srt);

	LOG_DBG("SR: %s\n", print_srt(dp->srt));

	buf = dsr_pkt_alloc_opts(dp, len);

	if (!buf) {
/* 		LOG_DBG("Could allocate memory\n"); */
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
	tot_len = ntohs(dp->nh.iph->tot_len) + len;
	ttl = dp->nh.iph->ttl;
#endif
	dp->nh.iph = dsr_build_ip(dp, dp->src, dp->dst, ip_len, tot_len,
				  IPPROTO_DSR, ttl);

	if (!dp->nh.iph)
		return -1;

	dp->dh.opth = dsr_opt_hdr_add(buf, len, prot);

	if (!dp->dh.opth) {
/* 		LOG_DBG("Could not create DSR opts header!\n"); */
		return -1;
	}

	buf += DSR_OPT_HDR_LEN;
	len -= DSR_OPT_HDR_LEN;

	dp->srt_opt = dsr_srt_opt_add(buf, len, 0, dp->salvage, dp->srt);

	if (!dp->srt_opt) {
/* 		LOG_DBG("Could not create Source Route option header!\n"); */
		return -1;
	}

	buf += DSR_SRT_OPT_LEN(dp->srt);
	len -= DSR_SRT_OPT_LEN(dp->srt);

	return 0;
}

int NSCLASS dsr_srt_opt_recv(struct dsr_pkt *dp, struct dsr_srt_opt *srt_opt)
{
	struct in_addr next_hop_intended;
	struct in_addr myaddr = my_addr();
	int n;

	if (!dp || !srt_opt)
		return DSR_PKT_ERROR;
	
	dp->srt_opt = srt_opt;

	/* We should add this source route info to the cache... */
	dp->srt = dsr_srt_new(dp->src, dp->dst, srt_opt->length,
			      (char *)srt_opt->addrs);

	if (!dp->srt) {
		LOG_DBG("Create source route failed\n");
		return DSR_PKT_ERROR;
	}
	n = dp->srt->laddrs / sizeof(struct in_addr);

	LOG_DBG("SR: %s sleft=%d\n", print_srt(dp->srt), srt_opt->sleft);

	/* Copy salvage field */
	dp->salvage = dp->srt_opt->salv;

	next_hop_intended = dsr_srt_next_hop(dp->srt, srt_opt->sleft);
	dp->prv_hop = dsr_srt_prev_hop(dp->srt, srt_opt->sleft - 1);
	dp->nxt_hop = dsr_srt_next_hop(dp->srt, srt_opt->sleft - 1);

	LOG_DBG("next_hop=%s prev_hop=%s next_hop_intended=%s\n",
                print_ip(dp->nxt_hop),
                print_ip(dp->prv_hop), print_ip(next_hop_intended));

	neigh_tbl_add(dp->prv_hop, dp->mac.ethh);
	
	/* Do not add a link based on a packet that was overheard */
	if (!(dp->flags & PKT_PROMISC_RECV))
		lc_link_add(myaddr, dp->prv_hop,
			    ConfValToUsecs(RouteCacheTimeout), 0, 1);

	/* Only add the links that this message has already traversed
	 * (i.e., those that are certain to be bidirectional). This is
	 * not in the RFC. */
/* 	dsr_rtc_add(dp->srt, ConfValToUsecs(RouteCacheTimeout), 0); */
	if (dp->dst.s_addr == myaddr.s_addr)
		dsr_rtc_add(dp->srt, ConfValToUsecs(RouteCacheTimeout), 0);
	else {
		/* Split the source route at us or at the previous hop
		 * if the message was overheard. */
		struct dsr_srt *srt_split = NULL;

		/* If the RREP was promiscuously overheard, we can
		 * only assume that the hops prior to the one that we
		 * overheard it from are functional. Otherwise, we
		 * count all hops prior to ourselves as functional. */
		if (dp->flags & PKT_PROMISC_RECV)
			srt_split = dsr_srt_new_split(dp->srt, dp->prv_hop);
		else
			srt_split = dsr_srt_new_split(dp->srt, myaddr);
		
		if (srt_split) {
			LOG_DBG("Adding split SRT to cache: %s\n", print_srt(srt_split));
			dsr_rtc_add(srt_split, ConfValToUsecs(RouteCacheTimeout), 0);
			kfree(srt_split);
		}
	}
	/* Automatic route shortening - Check if this node is the
	 * intended next hop. If not, is it part of the remaining
	 * source route? */
	if (get_confval(AutomaticRouteShortening) && 
	    next_hop_intended.s_addr != myaddr.s_addr &&
	    dsr_srt_find_addr(dp->srt, myaddr, srt_opt->sleft) &&
	    !grat_rrep_tbl_find(dp->src, dp->prv_hop)) {
		struct dsr_srt *srt, *srt_cut;

		/* Send Grat RREP */
		LOG_DBG("Send Gratuitous RREP to %s\n", print_ip(dp->src));

		srt_cut = dsr_srt_shortcut(dp->srt, dp->prv_hop, myaddr);

		if (!srt_cut)
			return DSR_PKT_DROP;

		LOG_DBG("shortcut: %s\n", print_srt(srt_cut));

		/* srt = dsr_rtc_find(myaddr, dp->src); */
		if (srt_cut->laddrs / sizeof(struct in_addr) == 0)
			srt = dsr_srt_new_rev(srt_cut);
		else
			srt = dsr_srt_new_split_rev(srt_cut, myaddr);

		if (!srt) {
			LOG_DBG("No route to %s\n", print_ip(dp->src));
			kfree(srt_cut);
			return DSR_PKT_DROP;
		}
		LOG_DBG("my srt: %s\n", print_srt(srt));

		grat_rrep_tbl_add(dp->src, dp->prv_hop);

		dsr_rrep_send(srt, srt_cut);

		kfree(srt_cut);
		kfree(srt);
	}

	if (dp->flags & PKT_PROMISC_RECV)
		return DSR_PKT_DROP;

	if (srt_opt->sleft == 0)
		return DSR_PKT_SRT_REMOVE;

	if (srt_opt->sleft > n) {
		// Send ICMP parameter error
		return DSR_PKT_SEND_ICMP;
	}

	srt_opt->sleft--;

	/* TODO: check for multicast address in next hop or dst */
	/* TODO: check MTU and compare to pkt size */

	return DSR_PKT_FORWARD;
}
