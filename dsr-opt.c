#include <net/ip.h>

#include "debug.h"
#include "dsr.h"
#include "dsr-rreq.h"
#include "dsr-rrep.h"
#include "dsr-srt.h"
#include "kdsr.h"

dsr_hdr_t *dsr_hdr_add(char *buf, int len, unsigned int protocol)
{
	dsr_hdr_t *dh;
	
	if (len < DSR_OPT_HDR_LEN)
		return NULL;
	
	dh = (dsr_hdr_t *)buf;

	dh->nh = protocol;
	dh->f = 0;
	dh->res = 0;
      	dh->length = htons(len - DSR_OPT_HDR_LEN);

	return dh;
}

struct iphdr *dsr_build_ip(char *buf, int len, struct in_addr src, 
			   struct in_addr dst, int ttl)
{
	struct iphdr *iph;
	
	if (len < sizeof(struct iphdr))
		return NULL;
	
	iph = (struct iphdr *)buf;
	
	iph->version = IPVERSION;
	iph->ihl = 5;
	iph->tos = 0;
	iph->tot_len = htons(len);
	iph->id = 0;
	iph->frag_off = 0;
	iph->ttl = (ttl ? ttl : IPDEFTTL);
	iph->protocol = IPPROTO_DSR;
	iph->saddr = src.s_addr;		
	iph->daddr = dst.s_addr;
	
	ip_send_check(iph);

	return iph;
}

int dsr_opts_remove(struct iphdr *iph, int len)
{
	dsr_hdr_t *dh;
	int dsr_len;
	
	dh = (dsr_hdr_t *)((char *)iph + (iph->ihl << 2));

	dsr_len = ntohs(dh->length) + DSR_OPT_HDR_LEN;
	
	if (dsr_len > len) {
		DEBUG("data to short according to DSR header len=%d dh->length=%d!\n", len, dsr_len);
		return -1;
	}
	/* Update IP header */
	iph->protocol = dh->nh;
	iph->tot_len = htons((iph->ihl << 2) + len - dsr_len);
	ip_send_check(iph);

	memcpy((char *)dh, (char *)dh + dsr_len, len - dsr_len);
	
	/* Return new length */
	return (len - dsr_len);
}
int dsr_recv(char *buf, int len, struct in_addr src, struct in_addr dst)
{	
	dsr_hdr_t *dh;
	dsr_opt_t *dopt;
	int dsr_len, l, res = 0;
	
	dh = (dsr_hdr_t *)buf;

	dsr_len = ntohs(dh->length);
	
	if (dsr_len > len) {
		DEBUG("data to short according to DSR header len=%d dh->length=%d!\n", len, dsr_len);
		return -1;
	}
	
	l = DSR_OPT_HDR_LEN;
	dopt = DSR_OPT_HDR(dh);
	
	while (l < dsr_len) {
		DEBUG("len=%d dsr_len=%d l=%d\n", len, dsr_len, l);
		switch (dopt->type) {
		case DSR_OPT_PADN:
			break;
		case DSR_OPT_RREQ:
			DEBUG("Received RREQ\n");
			dsr_rreq_recv((dsr_rreq_opt_t *)dopt, src);
			break;
		case DSR_OPT_RREP:
			DEBUG("Received RREP\n");
			dsr_rrep_recv((dsr_rrep_opt_t *)dopt, src, dst);
			break;
		case DSR_OPT_ERR:
			DEBUG("Received RERR\n");
			break;
		case DSR_OPT_PREV_HOP:
			break;
		case DSR_OPT_ACK:
			DEBUG("Received ACK\n");
			break;
		case DSR_OPT_SRT:
			DEBUG("Received SRT\n");
			res = dsr_srt_recv((dsr_srt_opt_t *)dopt, src, dst);

			switch (res) {
			case DSR_SRT_FORWARD:
			case DSR_SRT_DELIVER:
			case DSR_SRT_ERROR:
				return res;
				break;
			case DSR_SRT_REMOVE:
				break;
			}
			break;
		case DSR_OPT_TIMEOUT:	
			break;
		case DSR_OPT_FLOWID:
			break;
		case DSR_OPT_AREQ:
			break;
		case DSR_OPT_PAD1:
			break;
		default:
			DEBUG("Unknown DSR option type=%d\n", dopt->type);
		}
		l = l + dopt->length + 2;
		dopt = DSR_NEXT_OPT(dopt);
	}
	return res;
}
