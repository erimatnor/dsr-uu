#include <net/ip.h>

#include "debug.h"
#include "dsr.h"
#include "dsr-opt.h"
#include "dsr-rreq.h"
#include "dsr-rrep.h"
#include "dsr-srt.h"
#include "kdsr.h"


struct dsr_opt_hdr *dsr_opt_hdr_add(char *buf, int len, unsigned int protocol)
{
	struct dsr_opt_hdr *opt_hdr;
	
	if (len < DSR_OPT_HDR_LEN)
		return NULL;
	
	opt_hdr = (struct dsr_opt_hdr *)buf;

	opt_hdr->nh = protocol;
	opt_hdr->f = 0;
	opt_hdr->res = 0;
      	opt_hdr->p_len = htons(len - DSR_OPT_HDR_LEN);

	return opt_hdr;
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

int dsr_opts_create(struct dsr_pkt *dp)
{

	return 0;
}


int dsr_opts_remove(struct dsr_pkt *dp)
{
	int ip_len, len;
	char *off; /* Should point to the byte after the IP header */
	
	if (!dp)
		return -1;

	/* Update IP header */
	ip_len = (dp->iph->ihl << 2);

	dp->iph->protocol = dp->opt_hdr->nh;
	dp->iph->tot_len = htons(ip_len + dp->data_len);

	ip_send_check(dp->iph);

	off = (char *)dp->opt_hdr;
	
	/* Move data */
	memmove(off, off + dp->dsr_opts_len, dp->data_len);
	
	len = dp->dsr_opts_len;
	dp->dsr_opts_len = 0;
	dp->opt_hdr = NULL;
	dp->srt_opt = NULL;
	dp->rreq_opt = NULL;
	dp->rrep_opt = NULL;
	
	/* Return bytes removed */
	return len;
}
int dsr_opt_recv(struct dsr_pkt *dp)
{	
	int dsr_len, l;
	int action = 0;
	int num_rreq_opts = 0;
	struct dsr_opt *dopt;
	struct in_addr my_addr;

	if (!dp)
		return DSR_PKT_ERROR;
	
	dsr_node_lock(dsr_node);
	my_addr = dsr_node->ifaddr;
	dsr_node_unlock(dsr_node);
	
	/* Packet for us */
	if (dp->dst.s_addr == my_addr.s_addr)
		action |= DSR_PKT_DELIVER;
	
	dsr_len = dp->dsr_opts_len;
	
	l = DSR_OPT_HDR_LEN;
	dopt = DSR_GET_OPT(dp->opt_hdr);
	
	DEBUG("Parsing DSR packet l=%d dsr_len=%d\n", l, dsr_len);
		
	while (l < dsr_len && (dsr_len - l) > 2) {
		DEBUG("dsr_len=%d l=%d\n", dsr_len, l);
		switch (dopt->type) {
		case DSR_OPT_PADN:
			break;
		case DSR_OPT_RREQ:
			num_rreq_opts++;
			DEBUG("RREQ opt:\n");
			if (num_rreq_opts > 1) {
				DEBUG("More than one RREQ opt!!! - Ignoring\n");
				return DSR_PKT_ERROR;
			}
			dp->rreq_opt = (struct dsr_rreq_opt *)dopt;
			action |= dsr_rreq_opt_recv(dp);
			break;
		case DSR_OPT_RREP:
			DEBUG("RREP opt:\n");
			dp->rrep_opt = (struct dsr_rrep_opt *)dopt;
			action |= dsr_rrep_opt_recv(dp);
			break;
		case DSR_OPT_ERR:
			DEBUG("RERR opt:");
			break;
		case DSR_OPT_PREV_HOP:
			break;
		case DSR_OPT_ACK:
			DEBUG("ACK opt:\n");
			break;
		case DSR_OPT_SRT:
			DEBUG("SRT opt:\n");
			dp->srt_opt = (dsr_srt_opt_t *)dopt;
			action |= dsr_srt_opt_recv(dp);
			break;
		case DSR_OPT_TIMEOUT:	
			break;
		case DSR_OPT_FLOWID:
			break;
		case DSR_OPT_AREQ:
			break;
		case DSR_OPT_PAD1:
			DEBUG("PAD1 opt\n");
			l++;
			dopt++;
			continue;
		default:
			DEBUG("Unknown DSR option type=%d\n", dopt->type);
		}
		l = l + dopt->length + 2;
		dopt = DSR_GET_NEXT_OPT(dopt);
	}
	return action;
}


