#include <net/ip.h>

#include "debug.h"
#include "dsr.h"
#include "kdsr.h"
#include "dsr-rrep.h"
#include "dsr-rreq.h"
#include "dsr-rtc.h"
#include "p-queue.h"

static unsigned int rreq_seqno = 1;

static dsr_rreq_opt_t *dsr_rreq_opt_add(char *buf, int len, 
					struct in_addr target)
{
	dsr_rreq_opt_t *rreq;

	if (!buf || len < DSR_RREQ_HDR_LEN)
		return NULL;

	rreq = (dsr_rreq_opt_t *)buf;
	
	rreq->type = DSR_OPT_RREQ;
	rreq->length = 6;
	rreq->id = htons(rreq_seqno++);
	rreq->target = target.s_addr;
	
	return rreq;
}

int dsr_rreq_create(char *buf, int len, struct in_addr target)
{
	struct in_addr dst;
	dsr_rreq_opt_t *rreq;
	char *off;
	int l;
	
	l = IP_HDR_LEN + DSR_OPT_HDR_LEN + DSR_RREQ_HDR_LEN;
	
	if (!buf || len < l)
		return -1;

	dst.s_addr = DSR_BROADCAST;
	off = buf;
	
	dsr_build_ip(off, l, ldev_info.ifaddr, dst, 1);

	off += IP_HDR_LEN;
	l -= IP_HDR_LEN;
	
	dsr_hdr_add(off, l, 0);
	     
	off += DSR_OPT_HDR_LEN;
	l -= DSR_OPT_HDR_LEN;

	rreq = dsr_rreq_opt_add(off, l, target);

	if (!rreq) {
		DEBUG("Could not create RREQ\n");
		return -1;
	}
	return 0;
}

void dsr_rreq_recv(dsr_rreq_opt_t *rreq, struct in_addr initiator)
{
	dsr_srt_t *srt;
	
	if (!rreq)
		return;
	
	if (initiator.s_addr == ldev_info.ifaddr.s_addr)
		return;

	srt = dsr_srt_new(initiator, ldev_info.ifaddr, 
			  DSR_RREQ_ADDRS_LEN(rreq), rreq->addrs);
		
	/* Examine source route if this node already exists in it */

	    
	if (rreq->target == ldev_info.ifaddr.s_addr) {
		dsr_srt_t *srt_rev;
		
		DEBUG("I am RREQ target\n");
		
		DEBUG("srt: %s\n", print_srt(srt));

		srt_rev = dsr_srt_new_rev(srt);
		
		dsr_rtc_add(srt_rev, 60000, 0);

		/* Send buffered packets */
		p_queue_set_verdict(P_QUEUE_SEND, srt_rev->dst.s_addr);

		/* send rrep.... */
		dsr_rrep_send(srt_rev);

		kfree(srt_rev);

	} else {
		int i, n;
		
		n = DSR_RREQ_ADDRS_LEN(rreq) / sizeof(struct in_addr);
		
		for (i = 0; i < n; i++)
			if (srt->addrs[i].s_addr == ldev_info.ifaddr.s_addr)
				goto out;
		
		/* Forward RREQ */
		//	dsr_send_rrep(initiator);
	}
 out:
	kfree(srt);
	return;
}
