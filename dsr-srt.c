#include <linux/slab.h>

#include "dsr.h"
#include "dsr-srt.h"
#include "debug.h"

dsr_srt_t *dsr_srt_new(struct in_addr src, struct in_addr dst,
		       unsigned int length, u_int32_t *addrs)
{
	dsr_srt_t *sr;

	sr = kmalloc(sizeof(dsr_srt_t) + length, GFP_ATOMIC);

	sr->src.s_addr = src.s_addr;
	sr->dst.s_addr = dst.s_addr;
	sr->laddrs = length;
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
	sopt->sleft = srt->laddrs / sizeof(struct in_addr);
	
	memcpy(sopt->addrs, srt->addrs, srt->laddrs);
	
	return sopt;
}

void dsr_parse_srt(struct in_addr initiator, dsr_srt_t *srt)
{
	DEBUG("Parse source route\n");
	return;
}
