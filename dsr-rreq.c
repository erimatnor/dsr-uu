#include <net/ip.h>

#include "debug.h"
#include "dsr.h"
#include "kdsr.h"
#include "dsr-rreq.h"

static unsigned int rreq_seqno = 1;

dsr_rreq_opt_t *dsr_rreq_hdr_add(char *buf, int buflen, struct in_addr target)
{
	struct iphdr *iph;
	dsr_hdr_t *dsr_hdr;
	dsr_rreq_opt_t *rreq;

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
	iph->saddr = ldev_info.ifaddr.s_addr;		
	iph->daddr = DSR_BROADCAST;
	
	ip_send_check(iph);

	dsr_hdr = dsr_hdr_add(buf + IP_HDR_LEN, buflen - IP_HDR_LEN, 0);

	if (!dsr_hdr)
		return NULL;

	rreq = (dsr_rreq_opt_t *)DSR_OPT_HDR(dsr_hdr);
	
	rreq->type = DSR_OPT_RREQ;
	rreq->length = DSR_RREQ_HDR_LEN;
	rreq->id = htons(rreq_seqno++);
	rreq->target = target.s_addr;
	
	return rreq;
}

void dsr_rreq_recv(struct in_addr initiator, dsr_rreq_opt_t *rreq)
{
	if (!rreq)
		return;


	//dsr_parse_source_route(initiator, sr);
	
	
	if (rreq->target == ldev_info.ifaddr.s_addr) {
		dsr_src_rte_t *sr;
		DEBUG("I am RREQ target\n");
		sr = dsr_src_rte_new(initiator, ldev_info.ifaddr, 
			     rreq->length - DSR_RREQ_HDR_LEN, rreq->addrs);
		
		/* send rrep.... */

	} else {
		/* Forward RREQ */
		//	dsr_send_rrep(initiator);

	}
	
	return;
}
