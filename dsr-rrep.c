#include <linux/string.h>
#include <net/ip.h>

#include "dsr.h"
#include "debug.h"
#include "dsr-rrep.h"
#include "dsr-srt.h"

static inline int dsr_rrep_add_srt(dsr_rrep_opt_t *rrep, dsr_srt_t *srt)
{
	int n;

	if (!rrep | !srt)
		return -1;

	n = srt->laddrs / sizeof(struct in_addr);

	memcpy(rrep->addrs, srt->addrs, srt->laddrs);
	memcpy(&rrep->addrs[n], &srt->dst, sizeof(struct in_addr));
	
	return 0;
}

static dsr_rrep_opt_t *dsr_rrep_opt_add(char *buf, int len, dsr_srt_t *srt)
{
	dsr_rrep_opt_t *rrep;
	
	if (!buf || !srt || len < DSR_RREP_OPT_LEN(srt))
		return NULL;

	rrep = (dsr_rrep_opt_t *)buf;
	
	rrep->type = DSR_OPT_RREP;
	rrep->length = srt->laddrs + sizeof(struct in_addr) + 1;
	rrep->l = 0;
	rrep->res = 0;

	/* Add source route to RREP */
	dsr_rrep_add_srt(rrep, srt);
	
	return rrep;	
}

int dsr_rrep_create(char *buf, int len, dsr_srt_t *srt)
{
	struct in_addr dst;
	dsr_srt_t *srt_rev;
	dsr_rrep_opt_t *rrep;
	char *off;
	int l;
	
	l = IP_HDR_LEN + DSR_OPT_HDR_LEN + 
		DSR_SRT_OPT_LEN(srt) + DSR_RREP_OPT_LEN(srt);
	
	if (!buf || len < l)
		return -1;

	dst.s_addr = srt->src.s_addr;
	off = buf;
	
	dsr_build_ip(off, l, ldev_info.ifaddr, dst, 1);

	off += IP_HDR_LEN;
	l -= IP_HDR_LEN;
	
	dsr_hdr_add(off, l, 0);
	     
	off += DSR_OPT_HDR_LEN;
	l -= DSR_OPT_HDR_LEN;

	/* Reverse the source route */
	srt_rev = dsr_srt_new_rev(srt);

	/* Add the source route option to the packet */
	dsr_srt_opt_add(off, l, srt_rev);
	
	off += DSR_SRT_OPT_LEN(srt_rev);
	l -= DSR_SRT_OPT_LEN(srt_rev);

	/* Free new reversed source route */
	kfree(srt_rev);

	rrep = dsr_rrep_opt_add(off, l, srt);

	if (!rrep) {
		DEBUG("Could not create RREQ\n");
		return -1;
	}
	return 0;
}
