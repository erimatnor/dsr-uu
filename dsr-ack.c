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


static int dsr_ack_send(struct in_addr neigh_addr, unsigned short id)
{
	struct dsr_pkt *dp;
	struct dsr_ack_opt *ack_opt;
	int len = IP_HDR_LEN + DSR_OPT_HDR_LEN + DSR_ACK_HDR_LEN;
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
	
	ack_opt = dsr_ack_opt_add(buf, len, neigh_addr, id);

	if (!ack_opt) {
		DEBUG("Could not create DSR ACK opt header\n");
		goto out_err;
	}
	
	dp->nh.iph->ttl = 1;

	DEBUG("Sending ACK for %s\n", print_ip(neigh_addr.s_addr));
	
/* 	neigh->last_ack_tx_time = jiffies; */

	dsr_dev_xmit(dp);
	
	return 1;

 out_err:
	dsr_pkt_free(dp);
	return -1;
}

struct dsr_ack_req_opt *dsr_ack_req_opt_add(char *buf, int len, 
					    struct in_addr addr,
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
	
	ack_req = dsr_ack_req_opt_add(buf, len, neigh_addr, id);

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
	unsigned short id;

	if (!ack_req || !dp)
		return DSR_PKT_ERROR;
	
	id = ntohs(ack_req->id);

	DEBUG("ACK REQ: src=%s id=%u\n", print_ip(dp->src.s_addr), id);
	
	dsr_ack_send(dp->src, id);

	return DSR_PKT_NONE;
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

