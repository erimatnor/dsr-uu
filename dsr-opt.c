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

struct iphdr *dsr_build_ip(struct dsr_pkt *dp, struct in_addr src, struct in_addr dst, int totlen, int ttl)
{
	struct iphdr *iph;
	
	dp->nh.iph = iph = (struct iphdr *)dp->ip_data;
	
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
	int len, ip_len;

	if (!dp)
		return -1;

	/* Update IP header */
	/* ip_len = (dp->skb->nh.iph->ihl << 2); */

/* 	/\* Make sure we point to the headers in the skb *\/ */
/* 	opth = (struct dsr_opt_hdr *)((char *)dp->skb->nh.iph + ip_len); */

/* 	dsr_opts_len = ntohs(opth->p_len) + DSR_OPT_HDR_LEN; */

/* 	DEBUG("Removing DSR opts len=%d\n", dsr_opts_len); */

/* 	if (opth->nh == 0 && dp->payload_len == 0)		 */
/* 		dp->nh.iph->protocol = IPPROTO_DSR; */
/* 	else */
/* 		dp->nh.iph->protocol = opth->nh; */

/* 	dp->skb->nh.iph->tot_len = htons(ip_len + dp->payload_len); */

/* 	ip_send_check(dp->nh.iph); */

/* 	off = (char *)opth; */
	
/* 	/\* Move data *\/ */
/* 	memmove(off, off + dsr_opts_len, dp->payload_len); */
	
/* 	len = dsr_opts_len; */
/* 	dp->payload = off; */
/* 	dp->dh.opth = NULL; */
/* 	dp->srt_opt = NULL; */
/* 	dp->rreq_opt = NULL; */
/* 	memset(dp->rrep_opt, 0, sizeof(struct dsr_rrep_opt *) * MAX_RREP_OPTS); */
/* 	memset(dp->rerr_opt, 0, sizeof(struct dsr_rerr_opt *) * MAX_RERR_OPTS); */
/* 	memset(dp->ack_opt, 0, sizeof(struct dsr_ack_opt *) * MAX_ACK_OPTS); */
/* 	dp->num_rrep_opts = dp->num_rerr_opts = dp->num_ack_opts = 0; */
	
	ip_len = (dp->nh.iph->ihl << 2);

	if (dp->dh.opth->nh == 0 && dp->payload_len == 0)
		dp->nh.iph->protocol = IPPROTO_DSR;
	else
		dp->nh.iph->protocol = dp->dh.opth->nh;
	
	dp->skb->nh.iph->tot_len = htons(ip_len + dp->payload_len);
	
	ip_send_check(dp->nh.iph);
	
	len = dsr_pkt_free_opts(dp);
	
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
	if (dp->dst.s_addr == myaddr.s_addr && dp->payload_len != 0)
		action |= DSR_PKT_DELIVER;
	
	dsr_len = dsr_pkt_opts_len(dp);
	
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
			action |= dsr_rreq_opt_recv(dp, (struct dsr_rreq_opt *)dopt);
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


