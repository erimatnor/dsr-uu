#include <linux/ip.h>

#include "debug.h"
#include "dsr.h"
#include "dsr-rreq.h"
#include "kdsr.h"

dsr_hdr_t *dsr_hdr_add(char *buf, int len, unsigned int protocol)
{
	dsr_hdr_t *dh;
	
	if (len < sizeof(dsr_hdr_t))
		return NULL;
	
	dh = (dsr_hdr_t *)buf;

	dh->nh = protocol;
	dh->f = 0;
	dh->res = 0;
      	dh->length = htons(len);

	return dh;
}

void dsr_parse_source_route(struct in_addr initiator, u_int32_t *addrs)
{
	DEBUG("Parse source route\n");
	return;
}


void dsr_recv(char *buf, int len)
{	
	struct iphdr *iph;
	struct in_addr src;
	dsr_hdr_t *dh;
	dsr_opt_t *dopt;

	iph = (struct iphdr *)buf;

	src.s_addr = iph->saddr;

	if (len < ntohs(iph->tot_len)) {
		DEBUG("received data to short according to IP header!\n");
		return;
	}

	dh = DSR_FIXED_HDR(iph);

	if (len < ntohs(dh->length)) {
		DEBUG("received data to short according to DSR header!\n");
		return;
	}

	dopt = DSR_OPT_HDR(dh);
	
	switch (dopt->type) {
	case DSR_OPT_PADN:
		break;
	case DSR_OPT_RREQ:
		DEBUG("Received RREQ\n");
		dsr_rreq_recv(src, (dsr_rreq_opt_t *)dopt);
		break;
	case DSR_OPT_RREP:
		DEBUG("Received RREP\n");
		break;
	case DSR_OPT_ERR:
		DEBUG("Received RERR\n");
		break;
	case DSR_OPT_PREV_HOP:
		break;
	case DSR_OPT_ACK:
		DEBUG("Received ACK\n");
		break;
	case DSR_OPT_SRC_RTE:
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
}
