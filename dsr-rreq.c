#include <linux/ip.h>
#include <linux/skbuff.h>
#include <net/ip.h>

#include "debug.h"
#include "dsr.h"
#include "kdsr.h"
#include "dsr-rreq.h"

static unsigned int rreq_seqno = 1;

dsr_rreq_opt_t *dsr_rreq_hdr_add(char *buf, int buflen, struct in_addr taddr)
{
	struct iphdr *iph;
	dsr_hdr_t *dsr_hdr;
	dsr_rreq_opt_t *dsr_rreq;

	if (buflen < DSR_RREQ_TOT_LEN || buf == NULL)
		return NULL;

	iph = (struct iphdr *)buf;
	
	iph->version = IPVERSION;
	iph->ihl = 5;
	iph->tos = 0;
	iph->tot_len = htons(DSR_RREQ_TOT_LEN);
	iph->id = 0;
	iph->frag_off = 0;
	iph->ttl = 1; /* Should probably change dynamically */
	iph->protocol = IPPROTO_DSR;
	iph->saddr = ldev_info.ip_addr;		
	iph->daddr = DSR_BROADCAST;
	
	ip_send_check(iph);

	dsr_hdr = dsr_hdr_add(buf + IP_HDR_LEN, sizeof(dsr_rreq_opt_t), 0);

	if (!dsr_hdr)
		return NULL;

	dsr_rreq = (dsr_rreq_opt_t *)DSR_OPT_HDR(dsr_hdr);
	
	dsr_rreq->type = DSR_OPT_RREQ;
	dsr_rreq->length = DSR_RREQ_HDR_LEN;
	dsr_rreq->id = htons(rreq_seqno++);
	dsr_rreq->target = taddr.s_addr;
	
	return dsr_rreq;
}

void dsr_rreq_recv(struct in_addr initiator, dsr_rreq_opt_t *rreq)
{
	if (!rreq)
		return;

	dsr_parse_source_route(initiator, rreq->addrs);
	
	
	if (rreq->target == ldev_info.ip_addr) {
		DEBUG("I am RREQ target\n");
		
		/* send rrep.... */

	} else {
		/* Forward RREQ */
		dsr_send_rrep(initiator);

	}
	
	return;
}
