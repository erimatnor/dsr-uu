#include <linux/proc_fs.h>

#include "tbl.h"
#include "dsr.h"
#include "debug.h"
#include "dsr-opt.h"
#include "dsr-ack.h"
#include "dsr-dev.h"
#include "dsr-rtc.h"
#include "dsr-neigh.h"

unsigned short ID = 0;

#define ACK_TBL_MAX_LEN 24
#define MAX_AREQ_TX 2

int dsr_send_ack_req(struct in_addr, unsigned short id);
static int dsr_ack_req_send(struct neighbor *neigh);
static int dsr_ack_send(struct neighbor *neigh);

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


static int dsr_ack_send(struct neighbor *neigh)
{
	struct dsr_pkt dp;
	struct dsr_ack_opt *ack_opt;
	int len = DSR_OPT_HDR_LEN + DSR_ACK_HDR_LEN;
	char ip_buf[IP_HDR_LEN];
	char *buf;

	if (!neigh)
		return -1;

	memset(&dp, 0, sizeof(dp));
	
	dp.data = NULL; /* No data in this packet */
	dp.data_len = 0;
	dp.dst = neigh->addr;
	dp.nxt_hop = neigh->addr;
	dp.dsr_opts_len = len;
	dp.src = my_addr();

	dp.nh.iph = dsr_build_ip(ip_buf, IP_HDR_LEN, IP_HDR_LEN + len, 
				 dp.src, dp.dst, 1);
	
	if (!dp.nh.iph) {
		DEBUG("Could not create IP header\n");
		return -1;
	}
	
	buf = kmalloc(len, GFP_ATOMIC);
	
	if (!buf)
		return -1;

	dp.dh.opth = dsr_opt_hdr_add(buf, len, 0);
	
	if (!dp.dh.opth) {
		DEBUG("Could not create DSR opt header\n");
		kfree(buf);
		return -1;
	}
	
	buf += DSR_OPT_HDR_LEN;
	len -= DSR_OPT_HDR_LEN;
	
	ack_opt = dsr_ack_opt_add(buf, len, neigh->addr, neigh->id_ack);

	if (!ack_opt) {
		DEBUG("Could not create DSR ACK opt header\n");
		kfree(dp.dh.raw);
		return -1;
	}
	
	dp.nh.iph->ttl = 1;

	DEBUG("Sending ACK for %s\n", print_ip(neigh->addr.s_addr));
	
	neigh->last_ack_tx_time = jiffies;

	dsr_dev_xmit(&dp);
	
	kfree(dp.dh.raw);

	return 1;
}

struct dsr_ack_req_opt *dsr_ack_req_opt_add(char *buf, int len, 
					    struct in_addr addr)
{
	struct dsr_ack_req_opt *ack_req = (struct dsr_ack_req_opt *)buf;
	struct neighbor *neigh;
	
	if (len < DSR_ACK_REQ_HDR_LEN)
		return NULL;
	
	DEBUG("Adding ACK REQ opt\n");
	
	write_lock_bh(&neigh_tbl.lock);
	
	neigh = __tbl_find(&neigh_tbl, &addr, crit_addr);

	if (!neigh)
		neigh = __neigh_create(addr, ++ID, -1);

	if (!neigh)
		return NULL;
	
	neigh->ack_req_timer.expires = time_add_msec(neigh->rtt);
	neigh->last_req_tx_time = jiffies;
	neigh->reqs++;

	if (timer_pending(&neigh->ack_req_timer))
		mod_timer(&neigh->ack_req_timer, neigh->ack_req_timer.expires);
	else
		add_timer(&neigh->ack_req_timer);
	
	/* Build option */
	ack_req->type = DSR_OPT_ACK_REQ;
	ack_req->length = DSR_ACK_REQ_OPT_LEN;
	ack_req->id = htons(neigh->id_req);
	
	write_unlock_bh(&neigh_tbl.lock);

	return ack_req;
}

static int dsr_ack_req_send(struct neighbor *neigh)
{
	struct dsr_pkt dp;
	struct dsr_ack_req_opt *ack_req;
	int len = DSR_OPT_HDR_LEN + DSR_ACK_REQ_HDR_LEN;
	char ip_buf[IP_HDR_LEN];
	char *buf;
	
	if (!neigh)
		return -1;
	
	memset(&dp, 0, sizeof(dp));
	
	dp.data = NULL; /* No data in this packet */
	dp.data_len = 0;
	dp.dst = neigh->addr;
	dp.nxt_hop = neigh->addr;
	dp.dsr_opts_len = len;
	dp.src = my_addr();
	

	dp.nh.iph = dsr_build_ip(ip_buf, IP_HDR_LEN, IP_HDR_LEN + len, 
				 dp.src, dp.dst, 1);
	
	if (!dp.nh.iph) {
		DEBUG("Could not create IP header\n");
		return -1;
	}
	
	buf = kmalloc(len, GFP_ATOMIC);
	
	if (!buf)
		return -1;
	
	dp.dh.opth = dsr_opt_hdr_add(buf, len, 0);
	
	if (!dp.dh.opth) {
		DEBUG("Could not create DSR opt header\n");
		kfree(buf);
		return -1;
	}
	
	buf += DSR_OPT_HDR_LEN;
	len -= DSR_OPT_HDR_LEN;
	
	ack_req = dsr_ack_req_opt_add(buf, len, neigh->addr);

	if (!ack_req) {
		DEBUG("Could not create ACK REQ opt\n");
		kfree(dp.dh.raw);
		return -1;
	}
	
	DEBUG("Sending ACK REQ for %s id=%u\n", print_ip(neigh->addr.s_addr), neigh->id_req);

	dsr_dev_xmit(&dp);
	
	kfree(dp.dh.raw);

	return 1;
}

int dsr_ack_add_ack_req(struct in_addr addr)
{
	return !in_tbl(&neigh_tbl, &addr, crit_not_send_req);
}

int dsr_ack_req_opt_recv(struct dsr_pkt *dp, struct dsr_ack_req_opt *ack_req)
{
	struct neighbor *neigh;

	if (!ack_req || !dp)
		return DSR_PKT_ERROR;
	
	DEBUG("ACK REQ: src=%s id=%u\n", print_ip(dp->src.s_addr), ntohs(ack_req->id));
	write_lock_bh(&neigh_tbl.lock);
	
	neigh = __tbl_find(&neigh_tbl, &dp->src, crit_addr);

	if (!neigh)
		neigh = __neigh_create(dp->src, -1, ntohs(ack_req->id));
	else 

	if (!neigh)
		return DSR_PKT_ERROR;

	neigh->ack_timer.expires = time_add_msec(neigh->rtt / 3);
     	neigh->id_ack = ntohs(ack_req->id);

	if (timer_pending(&neigh->ack_timer))
		mod_timer(&neigh->ack_timer, neigh->ack_timer.expires);
	else
		add_timer(&neigh->ack_timer);
		
	write_unlock_bh(&neigh_tbl.lock);

	return DSR_PKT_NONE;
}


int dsr_ack_opt_recv(struct dsr_ack_opt *ack)
{
	struct neighbor *neigh;

	if (!ack)
		return DSR_PKT_ERROR;

	DEBUG("ACK dst=%s src=%s id=%u\n", print_ip(ack->dst), print_ip(ack->src), ntohs(ack->id));

	write_lock_bh(&neigh_tbl.lock);
	
	neigh = __tbl_find(&neigh_tbl, ack, crit_ack);

	if (!neigh) {
		DEBUG("No ACK REQ sent for this ACK??\n");
		write_unlock_bh(&neigh_tbl.lock);
		return DSR_PKT_NONE;
	}
	if (timer_pending(&neigh->ack_req_timer))
		del_timer(&neigh->ack_req_timer);
	
	neigh->last_ack_rx_time = jiffies;
	neigh->id_req = ++ID;
	neigh->reqs = 0;
	
	write_unlock_bh(&neigh_tbl.lock);

	return DSR_PKT_NONE;
}

