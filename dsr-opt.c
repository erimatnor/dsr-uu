#include <net/ip.h>

#include "debug.h"
#include "dsr.h"
#include "dsr-rreq.h"
#include "dsr-rrep.h"
#include "dsr-srt.h"
#include "kdsr.h"


dsr_pkt_t *dsr_pkt_alloc(void)
{
	dsr_pkt_t *dp;

	dp = kmalloc(sizeof(dsr_pkt_t), GFP_ATOMIC);
	
	memset(dp, 0, sizeof(dsr_pkt_t));
	
	return dp;
}

void dsr_pkt_free(dsr_pkt_t *dp)
{
	if (!dp)
		return;
	
	/* Can't free skb, might be needed further down or up the stack */

	if (dp->srt)
		kfree(dp->srt);
	
	kfree(dp);
}

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

int dsr_opts_remove(dsr_pkt_t *dp)
{
	struct iphdr *iph;
	int dsr_len, ip_len;
	
	if (!dp)
		return -1;

       	dsr_len = ntohs(dp->dh->length) + DSR_OPT_HDR_LEN;
	
	if (dsr_len > dp->len) {
		DEBUG("data to short according to DSR header len=%d dh->length=%d!\n", dp->len, dsr_len);
		return -1;
	}
	
	/* Update IP header */
	iph = dp->skb->nh.iph;

	ip_len = (iph->ihl << 2);

	iph->protocol = dp->dh->nh;
	iph->tot_len = htons(ntohs(iph->tot_len) - dsr_len);

	ip_send_check(iph);

	/* Move data */
	memmove(dp->data, dp->data + dsr_len, dp->len - dsr_len);
		
	dp->len -= dsr_len;
	dp->dh = NULL;
	dp->sopt = NULL;
	dp->rreq = NULL;
	dp->rrep = NULL;
	
	/* Return bytes removed */
	return dsr_len;
}
int dsr_recv(dsr_pkt_t *dp)
{	
	int dsr_len, l;
	int res = DSR_PKT_DROP;
	dsr_opt_t *dopt;
	
	if (!dp)
		return DSR_PKT_ERROR;
	
	dp->dh = (dsr_hdr_t *)dp->data;

	dsr_len = ntohs(dp->dh->length) + DSR_OPT_HDR_LEN;
	
	if (dsr_len > dp->len) {
		DEBUG("data to short, DSR header len=%d dh->length=%d!\n", 
		      dp->len, dsr_len);
		return -1;
	}


	l = DSR_OPT_HDR_LEN;
	dopt = DSR_OPT_HDR(dp->dh);
	
	DEBUG("Parsing DSR packet l=%d dsr_len=%d\n", l, dsr_len);

	while (l < dsr_len && (dsr_len - l) > 2) {
		DEBUG("len=%d dsr_len=%d l=%d\n", dp->len, dsr_len, l);
		switch (dopt->type) {
		case DSR_OPT_PADN:
			break;
		case DSR_OPT_RREQ:
			DEBUG("Received RREQ\n");
			dp->rreq = (dsr_rreq_opt_t *)dopt;
			res = dsr_rreq_recv(dp);
			break;
		case DSR_OPT_RREP:
			DEBUG("Received RREP\n");
			dp->rrep = (dsr_rrep_opt_t *)dopt;
			res = dsr_rrep_recv(dp);
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
			dp->sopt = (dsr_srt_opt_t *)dopt;
			res = dsr_srt_recv(dp);
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


