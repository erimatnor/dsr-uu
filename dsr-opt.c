#include <net/ip.h>

#include "debug.h"
#include "dsr.h"
#include "dsr-opt.h"
#include "dsr-rreq.h"
#include "dsr-rrep.h"
#include "dsr-srt.h"
#include "dsr-ack.h"
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

struct iphdr *dsr_build_ip(char *buf, int buflen, int totlen, 
			   struct in_addr src, struct in_addr dst, int ttl)
{
	struct iphdr *iph;
	
	if (buflen < sizeof(struct iphdr))
		return NULL;
	
	iph = (struct iphdr *)buf;
	
	iph->version = IPVERSION;
	iph->ihl = 5;
	iph->tos = 0;
	iph->tot_len = htons(totlen);
	iph->id = 0;
	iph->frag_off = 0;
	iph->ttl = (ttl ? ttl : IPDEFTTL);
	iph->protocol = IPPROTO_DSR;
	iph->saddr = src.s_addr;		
	iph->daddr = dst.s_addr;
	
	ip_send_check(iph);

	return iph;
}


int dsr_opts_remove(struct dsr_pkt *dp)
{
	int ip_len, len;
	char *off; /* Should point to the byte after the IP header */
	
	if (!dp)
		return -1;

	DEBUG("Removing DSR opts len=%d\n", dp->dsr_opts_len);
	/* Update IP header */
	ip_len = (dp->nh.iph->ihl << 2);

	if (dp->dh.opth->nh == 0 && dp->data_len == 0)		
		dp->nh.iph->protocol = IPPROTO_DSR;
	else
		dp->nh.iph->protocol = dp->dh.opth->nh;

	dp->nh.iph->tot_len = htons(ip_len + dp->data_len);

	ip_send_check(dp->nh.iph);

	off = (char *)dp->dh.opth;
	
	/* Move data */
	memmove(off, off + dp->dsr_opts_len, dp->data_len);
	
	len = dp->dsr_opts_len;
	dp->data = off;
	dp->dsr_opts_len = 0;
	dp->dh.opth = NULL;
	dp->srt_opt = NULL;
	dp->rreq_opt = NULL;
	memset(dp->rrep_opt, 0, sizeof(struct dsr_rrep_opt *) * MAX_RREP_OPTS);
	memset(dp->rerr_opt, 0, sizeof(struct dsr_rerr_opt *) * MAX_RERR_OPTS);
	memset(dp->ack_opt, 0, sizeof(struct dsr_ack_opt *) * MAX_ACK_OPTS);
	dp->num_rrep_opts = dp->num_rerr_opts = dp->num_ack_opts = 0;

	/* Return bytes removed */
	return len;
}
int dsr_opt_recv(struct dsr_pkt *dp)
{	
	int dsr_len, l;
	int action = DSR_PKT_NONE;
	int num_rreq_opts = 0;
	struct dsr_opt *dopt;
	struct in_addr myaddr;

	if (!dp)
		return DSR_PKT_ERROR;
	
	myaddr = my_addr();
	
	/* Packet for us */
	if (dp->dst.s_addr == myaddr.s_addr && dp->data_len != 0)
		action |= DSR_PKT_DELIVER;
	
	dsr_len = dp->dsr_opts_len;
	
	l = DSR_OPT_HDR_LEN;
	dopt = DSR_GET_OPT(dp->dh.opth);
	
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
			if (dp->num_rrep_opts < MAX_RREP_OPTS) {
				dp->rrep_opt[dp->num_rrep_opts++] = (struct dsr_rrep_opt *)dopt;
				action |= dsr_rrep_opt_recv(dp, (struct dsr_rrep_opt *)dopt);
			}
			break;
		case DSR_OPT_RERR:
			DEBUG("RERR opt:");
			if (dp->num_rerr_opts < MAX_RERR_OPTS) {
				dp->rerr_opt[dp->num_rerr_opts++] = (struct dsr_rerr_opt *)dopt;
			}
			break;
		case DSR_OPT_PREV_HOP:
			break;
		case DSR_OPT_ACK:
			DEBUG("ACK opt:\n");
			if (dp->num_ack_opts < MAX_ACK_OPTS) {
				dp->ack_opt[dp->num_ack_opts++] = (struct dsr_ack_opt *)dopt;
				action |= dsr_ack_opt_recv((struct dsr_ack_opt *)dopt);
			}
			break;
		case DSR_OPT_SRT:
			DEBUG("SRT opt:\n");
			dp->srt_opt = (struct dsr_srt_opt *)dopt;
			action |= dsr_srt_opt_recv(dp);
			break;
		case DSR_OPT_TIMEOUT:	
			break;
		case DSR_OPT_FLOWID:
			break;
		case DSR_OPT_ACK_REQ:
			DEBUG("ACK REQ opt:\n");
			action |= dsr_ack_req_opt_recv(dp, (struct dsr_ack_req_opt *)dopt);
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


