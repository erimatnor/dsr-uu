#include <linux/slab.h>
#include <net/ip.h>

#include "dsr.h"
#include "dsr-srt.h"
#include "debug.h"

char *print_srt(dsr_srt_t *srt)
{
#define BUFLEN 256
	static char buf[BUFLEN];
	int i, len;

	if (!srt)
		return NULL;
	
	len = sprintf(buf, "%s-", print_ip(srt->src.s_addr));
	
	for (i = 0; i < (srt->laddrs / sizeof(u_int32_t)) && 
		     (len + 16) < BUFLEN; i++)
		len += sprintf(buf+len, "%s-", print_ip(srt->addrs[i].s_addr));
	
	if ((len + 16) < BUFLEN)
		len = sprintf(buf+len, "%s", print_ip(srt->dst.s_addr));
	return buf;
}

dsr_srt_t *dsr_srt_new(struct in_addr src, struct in_addr dst,
		       unsigned int length, char *addrs)
{
	dsr_srt_t *sr;

	sr = kmalloc(sizeof(dsr_srt_t) + length, GFP_ATOMIC);

	sr->src.s_addr = src.s_addr;
	sr->dst.s_addr = dst.s_addr;
	sr->laddrs = length;
	
	if (length != 0 && addrs)
		memcpy(sr->addrs, addrs, length);
	
	return sr;
}

dsr_srt_t *dsr_srt_new_rev(dsr_srt_t *srt)
{
	dsr_srt_t *srt_rev;
	int i, n;

	if (!srt)
		return NULL;
	
	srt_rev = kmalloc(sizeof(dsr_srt_t) + srt->laddrs, GFP_ATOMIC);
	
	srt_rev->src.s_addr = srt->dst.s_addr;
	srt_rev->dst.s_addr = srt->src.s_addr;
	srt_rev->laddrs = srt->laddrs;

	n = srt->laddrs / sizeof(struct in_addr);

	for (i = 0; i < n; i++)
		srt_rev->addrs[i].s_addr = srt->addrs[n-1-i].s_addr;

	return srt_rev;
}

dsr_srt_opt_t *dsr_srt_opt_add(char *buf, int len, dsr_srt_t *srt)
{
	dsr_srt_opt_t *sopt;
	
	if (len < DSR_SRT_OPT_LEN(srt))
		return NULL;

	sopt = (dsr_srt_opt_t *)buf;

	sopt->type = DSR_OPT_SRT;
	sopt->length = srt->laddrs + 2;
	sopt->f = 0;
	sopt->l = 0;
	sopt->res = 0;
	SET_SALVAGE(sopt, 0);
	sopt->sleft = (srt->laddrs / sizeof(struct in_addr));
	
	memcpy(sopt->addrs, srt->addrs, srt->laddrs);
	
	return sopt;
}

int dsr_srt_add(dsr_pkt_t *dp)
{
	struct iphdr *iph;
	struct sk_buff *nskb;
	int dsr_len;
	
	if (!dp || !dp->skb || !dp->srt)
		return -1;

	/* Calculate extra space needed */
	dsr_len = DSR_OPT_HDR_LEN + DSR_SRT_OPT_LEN(dp->srt);
	
	/* Allocate new data space at head */
	nskb = skb_copy_expand(dp->skb, skb_headroom(dp->skb),
			       skb_tailroom(dp->skb) + dsr_len, 
			       GFP_ATOMIC);
	
	if (nskb == NULL) {
		printk("Could not allocate new skb\n");
		return -1;	
	}
	/* Set old owner */
	if (dp->skb->sk != NULL)
		skb_set_owner_w(nskb, dp->skb->sk);
		
	skb_put(nskb, dsr_len);

	kfree_skb(dp->skb);
	dp->skb = nskb;	
	
	/* Update pointer to IP header */
	iph = dp->skb->nh.iph;
	
	if (!iph) {
		DEBUG("No IP header!\n");
		return -1;
	}
	/* Move data towards tail */
	memmove(dp->skb->h.raw + dsr_len, dp->skb->h.raw, dp->skb->tail - dp->skb->h.raw);
	
	dsr_hdr_add(dp->skb->h.raw, dsr_len, iph->protocol);
	
	iph->tot_len = htons(ntohs(iph->tot_len) + dsr_len);
	iph->protocol = IPPROTO_DSR;
	
	ip_send_check(iph);

	dsr_srt_opt_add(dp->skb->h.raw + DSR_OPT_HDR_LEN, dsr_len - DSR_OPT_HDR_LEN, dp->srt);
	dp->skb->h.raw += dsr_len;
	DEBUG("New skb->len=%d\n", dp->skb->len);

	return 0;
}

int dsr_srt_recv(struct dsr_pkt *dp)
{
	dsr_srt_opt_t *sopt;
	int n;	
	
	if (!dp || !dp->sopt)
		return DSR_PKT_ERROR;
	
	sopt = dp->sopt;
	
	dp->srt = dsr_srt_new(dp->src, dp->dst, sopt->length, (char *)sopt->addrs);
	
	/* We should add this source route info to the cache... */
	n = (sopt->length - 2) / sizeof(struct in_addr);
	
	if (sopt->sleft == 0) {
		if (dp->dst.s_addr == dsr_node->ifaddr.s_addr) {
			DEBUG("Source route remove and deliver\n");
			return DSR_PKT_DELIVER;
		} else {
			DEBUG("Remove source route...\n");
			return DSR_PKT_SRT_REMOVE;
		}
	}
	if (sopt->sleft > n) {
		// Send ICMP parameter error
		return DSR_PKT_SEND_ICMP;
	} else {
		int i;

		sopt->sleft--;		
		i = n - sopt->sleft;

		/* Fill in next hop */
		dp->nh.s_addr = sopt->addrs[i];
		DEBUG("Setting next hop and forward\n");
		/* TODO: check for multicast address in next hop or dst */
		/* TODO: check MTU and compare to pkt size */
	
		return DSR_PKT_FORWARD;
	}
	return DSR_PKT_ERROR;
}
