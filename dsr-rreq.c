#include <linux/ip.h>
#include <linux/skbuff.h>
#include <net/ip.h>
#include "debug.h"

#include "dsr.h"
#include "dsr-rreq.h"

static unsigned int rreq_seqno = 0;

#define DSR_RREQ_TOT_LEN IP_HDR_LEN + sizeof(dsr_hdr_t) + sizeof(dsr_rreq_opt_t)

static dsr_rreq_opt_t *dsr_rreq_hdr_add(char *buf, int buflen, struct in_addr taddr)
{
	struct iphdr *iph;
	dsr_hdr_t *dsr_hdr;
	dsr_rreq_opt_t *dsr_rreq;

	if (buflen < DSR_RREQ_TOT_LEN)
		return NULL;

	iph = (struct iphdr *)buf;
	
	iph->version = IPVERSION;
	iph->ihl = 5;
	iph->tos = 0;
	iph->tot_len = htons(DSR_RREQ_TOT_LEN);
	iph->id = 0;
	iph->frag_off = 0;
	iph->ttl = IPDEFTTL; /* Should probably change dynamically */
	iph->protocol = IPPROTO_DSR;
	iph->saddr = ldev_info.ip_addr;		
	iph->daddr = DSR_BROADCAST;
	
	ip_send_check(iph);

	dsr_hdr = dsr_hdr_add(buf, buflen - sizeof(dsr_rreq_opt_t) - IP_HDR_LEN, 0);

	if (!dsr_hdr)
		return NULL;

	dsr_rreq = (dsr_rreq_opt_t *)dsr_hdr->options;
	
	dsr_rreq->type = DSR_OPT_RREQ;
	dsr_rreq->length = DSR_RREQ_HDR_LEN;
	dsr_rreq->id = rreq_seqno++;
	dsr_rreq->taddr = taddr.s_addr;
	
	return dsr_rreq;
}

struct sk_buff *dsr_rreq_create(__u32 taddr)
{
	struct sk_buff *skb;
	struct in_addr target;
	dsr_rreq_opt_t *dsr_rreq;
	
	skb = dsr_pkt_alloc(DSR_RREQ_TOT_LEN);
	
	if (!skb)
		return NULL;

	target.s_addr = taddr;

	dsr_rreq = dsr_rreq_hdr_add(skb->data, skb->len, target);

	if (!dsr_rreq)
		return NULL;

	return skb;
}
