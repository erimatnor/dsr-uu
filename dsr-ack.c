#include <linux/proc_fs.h>

#include "tbl.h"
#include "dsr.h"
#include "debug.h"
#include "dsr-opt.h"
#include "dsr-ack.h"
#include "dsr-dev.h"
#include "dsr-rtc.h"
#include "neigh.h"
#include "maint-buf.h"

unsigned short ID = 0;

#define ACK_TBL_MAX_LEN 24
#define MAX_AREQ_TX 2

/* int dsr_send_ack_req(struct in_addr, unsigned short id); */

struct dsr_ack_opt *dsr_ack_opt_add(char *buf, int len, struct in_addr addr, unsigned short id)
{
	struct dsr_ack_opt *ack = (struct dsr_ack_opt *)buf;
	struct in_addr myaddr = my_addr();
	
	if (len < DSR_ACK_HDR_LEN)
		return NULL;
	
	ack->type = DSR_OPT_ACK;
	ack->length = DSR_ACK_OPT_LEN;
	ack->id = htons(id);
	ack->dst = addr.s_addr;
	ack->src = myaddr.s_addr;

	return ack;
}


int dsr_ack_send(struct in_addr dst, unsigned short id)
{
	struct dsr_pkt *dp;
	struct dsr_ack_opt *ack_opt;
	struct dsr_srt *srt;
	int len;
	char *buf;
	
	srt = dsr_rtc_find(my_addr(), dst);
	
	if (!srt) {
		DEBUG("No source route to %s\n", print_ip(dst.s_addr));
		return -1;
	}

	len = IP_HDR_LEN + DSR_OPT_HDR_LEN + DSR_SRT_OPT_LEN(srt) + DSR_ACK_HDR_LEN;

	dp = dsr_pkt_alloc(NULL, len);
	
	dp->data = NULL; /* No data in this packet */
	dp->data_len = 0;
	dp->dst = dst;
	dp->srt = srt;
	dp->nxt_hop = dsr_srt_next_hop(dp->srt, 0);
	dp->dsr_opts_len = len - IP_HDR_LEN;
	dp->src = my_addr();
	
	buf = dp->dsr_data;	

	dp->nh.iph = dsr_build_ip(buf, IP_HDR_LEN, len, dp->src, dp->dst, IPDEFTTL);
	
	if (!dp->nh.iph) {
		DEBUG("Could not create IP header\n");
		goto out_err;
	}

	buf += IP_HDR_LEN;
	len -= IP_HDR_LEN;

	dp->dh.opth = dsr_opt_hdr_add(buf, len, 0);
	
	if (!dp->dh.opth) {
		DEBUG("Could not create DSR opt header\n");
		goto out_err;
	}
	
	buf += DSR_OPT_HDR_LEN;
	len -= DSR_OPT_HDR_LEN;

	dp->srt_opt = dsr_srt_opt_add(buf, len, dp->srt);

	if (!dp->srt_opt) {
		DEBUG("Could not create Source Route option header\n");
		goto out_err;
	}

	buf += DSR_SRT_OPT_LEN(dp->srt);
	len -= DSR_SRT_OPT_LEN(dp->srt);

	ack_opt = dsr_ack_opt_add(buf, len, dst, id);

	if (!ack_opt) {
		DEBUG("Could not create DSR ACK opt header\n");
		goto out_err;
	}
	
	DEBUG("Sending ACK for %s\n", print_ip(dst.s_addr));
	
	dsr_dev_xmit(dp);
	
	return 1;

 out_err:
	dsr_pkt_free(dp);
	return -1;
}

static struct dsr_ack_req_opt *dsr_ack_req_opt_create(char *buf, int len,
						      unsigned short id)
{
	struct dsr_ack_req_opt *ack_req = (struct dsr_ack_req_opt *)buf;
	
	if (len < DSR_ACK_REQ_HDR_LEN)
		return NULL;
	
	DEBUG("Adding ACK REQ opt\n");
	
	/* Build option */
	ack_req->type = DSR_OPT_ACK_REQ;
	ack_req->length = DSR_ACK_REQ_OPT_LEN;
	ack_req->id = htons(id);
	

	return ack_req;
}

struct dsr_ack_req_opt *dsr_ack_req_opt_add(struct dsr_pkt *dp)
{
	char *buf = NULL;

	if (!dp)
		return NULL;

	if (dp->dsr_data_len) {
		buf = dsr_pkt_alloc_data_expand(dp, DSR_ACK_REQ_HDR_LEN);
	
		if (!buf)
			return NULL;
		
		dp->dh.raw = dp->dsr_data;

		dp->dh.opth->p_len = htons(ntohs(dp->dh.opth->p_len) + DSR_ACK_REQ_HDR_LEN);
		dp->dsr_opts_len += DSR_ACK_REQ_HDR_LEN;
	} else {
		buf = dsr_pkt_alloc_data(dp, DSR_OPT_HDR_LEN + DSR_ACK_REQ_HDR_LEN);

		if (!buf)
			return NULL;
		
		dp->dh.opth = dsr_opt_hdr_add(buf, DSR_OPT_HDR_LEN + DSR_ACK_REQ_HDR_LEN, 0);
		
		if (!dp->dh.opth) {
			DEBUG("Could not create DSR opt header\n");
			return NULL;
		}
		
		buf += DSR_OPT_HDR_LEN;
		dp->dsr_opts_len = DSR_OPT_HDR_LEN + DSR_ACK_REQ_HDR_LEN;
	}
	
	neigh_tbl_set_ack_req_timer(dp->nxt_hop);

	return dsr_ack_req_opt_create(buf, DSR_ACK_REQ_HDR_LEN, neigh_tbl_get_id(dp->nxt_hop));
}

int dsr_ack_req_send(struct in_addr neigh_addr, unsigned short id)
{
	struct dsr_pkt *dp;
	struct dsr_ack_req_opt *ack_req;
	int len = IP_HDR_LEN + DSR_OPT_HDR_LEN + DSR_ACK_REQ_HDR_LEN;
	char *buf;
	
	dp = dsr_pkt_alloc(NULL, len);

	dp->data = NULL; /* No data in this packet */
	dp->data_len = 0;
	dp->dst = neigh_addr;
	dp->nxt_hop = neigh_addr;
	dp->dsr_opts_len = len - IP_HDR_LEN;
	dp->src = my_addr();

	buf = dp->dsr_data;

	dp->nh.iph = dsr_build_ip(buf, IP_HDR_LEN, len, dp->src, dp->dst, 1);
	
	if (!dp->nh.iph) {
		DEBUG("Could not create IP header\n");
		goto out_err;
	}

	buf += IP_HDR_LEN;
	len -= IP_HDR_LEN;

	dp->dh.opth = dsr_opt_hdr_add(buf, len, 0);
	
	if (!dp->dh.opth) {
		DEBUG("Could not create DSR opt header\n");
		goto out_err;
	}
	
	buf += DSR_OPT_HDR_LEN;
	len -= DSR_OPT_HDR_LEN;
	
	ack_req = dsr_ack_req_opt_create(buf, len, id);

	if (!ack_req) {
		DEBUG("Could not create ACK REQ opt\n");
		goto out_err;
	}
	
	DEBUG("Sending ACK REQ for %s id=%u\n", print_ip(neigh_addr.s_addr), id);
	
	neigh_tbl_set_ack_req_timer(neigh_addr);

	dsr_dev_xmit(dp);
	
	return 1;

 out_err:
	dsr_pkt_free(dp);
	return -1;
}


int dsr_ack_req_opt_recv(struct dsr_pkt *dp, struct dsr_ack_req_opt *ack_req)
{
	if (!ack_req || !dp)
		return DSR_PKT_ERROR;
	
	dp->ack_req_opt = ack_req;
	
	return DSR_PKT_SEND_ACK;
}


int dsr_ack_opt_recv(struct dsr_ack_opt *ack)
{
	unsigned short id;
	struct in_addr dst, src, myaddr;
	
	if (!ack)
		return DSR_PKT_ERROR;

	myaddr = my_addr();
	
	dst.s_addr = ack->dst;
	src.s_addr = ack->src;
	id = ntohs(ack->id);
	
	DEBUG("ACK dst=%s src=%s id=%u\n", print_ip(ack->dst), print_ip(ack->src), id);

	if (dst.s_addr != myaddr.s_addr)
		return DSR_PKT_ERROR;

	/* Purge packets buffered for this next hop */
	maint_buf_del(src);

	neigh_tbl_reset_ack_req_timer(src, id);

	return DSR_PKT_NONE;
}

