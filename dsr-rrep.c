#include <linux/string.h>
#include <net/ip.h>

#include "dsr.h"
#include "dsr-rrep.h"

static inline int dsr_rrep_add_src_rte(dsr_rrep_opt_t *rrep, dsr_src_rte_t *sr)
{
	int n;

	if (!rrep | !sr)
		return -1;

	n = sr->length / sizeof(struct in_addr);

	memcpy(rrep->addrs, sr->addrs, sr->length);
	memcpy(&rrep->addrs[n], &sr->target, sizeof(struct in_addr));
	
	return 0;
}

dsr_rrep_opt_t *dsr_rrep_hdr_add(char *buf, int len, dsr_src_rte_t *sr)
{
	struct iphdr *iph;
	dsr_hdr_t *dsr_hdr;
	dsr_rrep_opt_t *rrep;

	if (buf == NULL || sr == NULL ||
	    len < (DSR_RREP_HDR_LEN + sr->length + sizeof(u_int32_t)))		return NULL;

	iph = (struct iphdr *)buf;
	
	iph->version = IPVERSION;
	iph->ihl = 5;
	iph->tos = 0;
	iph->tot_len = htons(len);
	iph->id = 0;
	iph->frag_off = 0;
	iph->ttl = 1; /* Should probably change dynamically */
	iph->protocol = IPPROTO_DSR;
	iph->saddr = ldev_info.ifaddr.s_addr;		
	iph->daddr = sr->initiator.s_addr;
	
	ip_send_check(iph);

	dsr_hdr = dsr_hdr_add(buf + IP_HDR_LEN, len - IP_HDR_LEN, 0);

	if (!dsr_hdr)
		return NULL;

	rrep = (dsr_rrep_opt_t *)DSR_OPT_HDR(dsr_hdr);
	
	rrep->type = DSR_OPT_RREP;
	rrep->length = DSR_RREP_HDR_LEN + sr->length + sizeof(u_int32_t);
	rrep->l = 0;
	rrep->res = 0;

	/* Add source route to RREP */
	dsr_rrep_add_src_rte(rrep, sr);
	
	return rrep;	
}
