#include <linux/proc_fs.h>

#include "tbl.h"
#include "dsr.h"
#include "debug.h"
#include "dsr-opt.h"
#include "dsr-ack.h"
#include "dsr-dev.h"

unsigned short ack_id = 0;

#define ACK_TBL_MAX_LEN 24
#define MAX_AREQ_TX 2

static TBL(neigh_tbl, ACK_TBL_MAX_LEN);

#define ACK_REQ_TIMEOUT_EVENT 0 
#define ACK_SEND_ACK_EVENT 1
#define ACK_DEL_EVENT      2

#define NEIGH_DEL_TIMEOUT 1000 
#define ACK_SEND_REQ_INTERVAL 1000
#define RTT_DEF 10

#define NEIGH_TBL_PROC_NAME "dsr_neigh_tbl"

struct neighbor {
	struct list_head l;
	struct in_addr addr;
        int id, id_ack;
	struct timer_list areq_timer;
	struct timer_list ack_timer;
	unsigned long time_req_tx;
	unsigned long time_rep_rx; /* Time when last ACK REP was received */
	int rtt, reqs, jitter;
};

int dsr_send_ack_req(struct in_addr, unsigned short id);
static int dsr_ack_req_send(struct neighbor *neigh);
static int dsr_ack_send(struct neighbor *neigh);

static inline int crit_addr(void *pos, void *addr)
{
	struct in_addr *a = addr; 
	struct neighbor *e = pos;
	
	if (e->addr.s_addr == a->s_addr)
		return 1;
	return 0;
}

static inline int crit_ack(void *pos, void *ack)
{
	struct dsr_ack_opt *a = ack; 
	struct neighbor *e = pos;
	struct in_addr myaddr = my_addr();

	if (e->addr.s_addr == a->src && 
	    e->id == a->id && 
	    a->dst == myaddr.s_addr)
		return 1;
	return 0;
}

static inline int timer_remove(void *entry, void *data)
{
	struct neighbor *n = entry;

	if (timer_pending(&n->areq_timer))
		del_timer(&n->areq_timer);
		
	if (timer_pending(&n->ack_timer))
		del_timer(&n->ack_timer);
	
	return 0;
}

static inline int crit_not_send_req(void *pos, void *addr)
{
	struct in_addr *a = addr; 
	struct neighbor *neigh = pos;
	
	if (neigh->addr.s_addr == a->s_addr &&
	    neigh->reqs >= MAX_AREQ_TX &&
	    jiffies < neigh->time_req_tx + neigh->rtt + (HZ/2))
		return 1;
	return 0;
}

static void neigh_tbl_del_timeout(unsigned long data)
{
	DEBUG("Neighbor delete timeout\n");

	tbl_del(&neigh_tbl, (struct list_head *)data);
}

static void neigh_tbl_areq_timeout(unsigned long data)
{
	struct neighbor *neigh;
	
	DEBUG("Neighbor timeout\n");

	write_lock_bh(&neigh_tbl.lock);
	
	neigh = (struct neighbor *)data;
	
	if (!neigh)
		goto out;
	
	DEBUG("ACK REQ Timeout\n");
	if (neigh->reqs >= MAX_AREQ_TX) {
		/* TODO: Send RERR */
		DEBUG("Send RERR!!!\n");
		if (!timer_pending(&neigh->ack_timer)) {
			neigh->ack_timer.function = neigh_tbl_del_timeout;
			neigh->ack_timer.data = (unsigned long)neigh;
			neigh->areq_timer.expires = time_add_msec(NEIGH_DEL_TIMEOUT);
			add_timer(&neigh->areq_timer);
		}
	} else {
		dsr_ack_req_send(neigh);
	}
 out:
	write_unlock_bh(&neigh_tbl.lock);
}

static void neigh_tbl_ack_timeout(unsigned long data)
{
	struct neighbor *neigh;
	
	DEBUG("ACK timeout\n");

	write_lock_bh(&neigh_tbl.lock);
	
	neigh = (struct neighbor *)data;
	
	if (!neigh)
		goto out;
	
	/* Time to send ACK REP */
	dsr_ack_send(neigh);
	
	neigh->id_ack = -1;
	if (!timer_pending(&neigh->areq_timer)) {
		neigh->ack_timer.function = neigh_tbl_del_timeout;
		neigh->ack_timer.data = (unsigned long)neigh;
		neigh->ack_timer.expires = time_add_msec(NEIGH_DEL_TIMEOUT);
		add_timer(&neigh->ack_timer);
	}
out:
	write_unlock_bh(&neigh_tbl.lock);	
}


static int neigh_tbl_print(char *buf)
{
	struct list_head *pos;
	int len = 0;
    
	read_lock_bh(&neigh_tbl.lock);
    
	len += sprintf(buf, "# %-15s %-6s %-6s TxTime\n", "Addr", "Reqs", "RTT");

	list_for_each(pos, &neigh_tbl.head) {
		struct neighbor *neigh =(struct neighbor *)pos;
		
		len += sprintf(buf+len, "  %-15s %-6d %-6u %lu\n", 
			       print_ip(neigh->addr.s_addr), neigh->reqs,
			       neigh->rtt,
			       (jiffies - neigh->time_req_tx) * 1000 / HZ);
	}
    
	read_unlock_bh(&neigh_tbl.lock);
	return len;

}

static int neigh_tbl_proc_info(char *buffer, char **start, off_t offset, int length)
{
	int len;

	len = neigh_tbl_print(buffer);
    
	*start = buffer + offset;
	len -= offset;
	if (len > length)
		len = length;
	else if (len < 0)
		len = 0;
	return len;    
}


int __init neigh_tbl_init(void)
{
	proc_net_create(NEIGH_TBL_PROC_NAME, 0, neigh_tbl_proc_info);
	return 0;
}


void __exit neigh_tbl_cleanup(void)
{
	tbl_flush(&neigh_tbl, timer_remove);
	proc_net_remove(NEIGH_TBL_PROC_NAME);
}

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
	int len = IP_HDR_LEN + DSR_OPT_HDR_LEN + DSR_ACK_HDR_LEN;
	char buffer[len];
	char *buf = buffer;

	if (!neigh)
		return -1;

	memset(&dp, 0, sizeof(dp));
	memset(buf, 0, len);
	
	dp.data = NULL; /* No data in this packet */
	dp.data_len = 0;
	dp.dst = neigh->addr;
	dp.nxt_hop = neigh->addr;
	
	dp.src = my_addr();

	dp.iph = dsr_build_ip(buf, len, dp.src, dp.dst, 1);
	
	if (!dp.iph) {
		DEBUG("Could not create IP header\n");
		return -1;
	}
	
	buf += IP_HDR_LEN;
	len -= IP_HDR_LEN;

	dp.dsr_opts_len = len;

	dp.opt_hdr = dsr_opt_hdr_add(buf, len, 0);
	
	if (!dp.opt_hdr) {
		DEBUG("Could not create DSR opt header\n");
		return -1;
	}
	
	buf += DSR_OPT_HDR_LEN;
	len -= DSR_OPT_HDR_LEN;
	
	ack_opt = dsr_ack_opt_add(buf, len, neigh->addr, neigh->id_ack);

	if (!ack_opt) {
		DEBUG("Could not create DSR ACK opt header\n");
		return -1;
	}
	
	dp.iph->ttl = 1;

	DEBUG("Sending ACK for %s\n", print_ip(neigh->addr.s_addr));

	dsr_dev_xmit(&dp);

	return 1;
}

struct dsr_ack_req_opt *dsr_ack_req_opt_add(char *buf, int len, struct in_addr addr)
{
	struct dsr_ack_req_opt *areq = (struct dsr_ack_req_opt *)buf;
	struct neighbor *neigh;
	int rtt = RTT_DEF;
	
	if (len < DSR_AREQ_HDR_LEN)
		return NULL;
	
	DEBUG("Adding ACK REQ opt\n");
	areq->type = DSR_OPT_AREQ;
	areq->length = DSR_AREQ_OPT_LEN;

	write_lock_bh(&neigh_tbl.lock);
	
	neigh = __tbl_find(&neigh_tbl, &addr, crit_addr);

	if (neigh) {
		if (timer_pending(&neigh->areq_timer))
			del_timer(&neigh->areq_timer);
		
		areq->id = htons(ack_id++);

		neigh->reqs++;
		neigh->time_req_tx = jiffies;
		neigh->areq_timer.function = neigh_tbl_areq_timeout;
		neigh->areq_timer.data = (unsigned long)neigh;
		neigh->areq_timer.expires = time_add_msec(rtt) + HZ;
				
		add_timer(&neigh->areq_timer);

		write_unlock_bh(&neigh_tbl.lock);
		return areq;
	}
	write_unlock_bh(&neigh_tbl.lock);
	
	neigh = kmalloc(sizeof(struct neighbor), GFP_ATOMIC);
	
	if (!neigh)
		return NULL;
	
	areq->id = htons(neigh->id);

	neigh->addr = addr;
	neigh->rtt = rtt;
	neigh->time_req_tx = jiffies;
	neigh->reqs = 1;
	
	init_timer(&neigh->areq_timer);
	neigh->areq_timer.function = neigh_tbl_areq_timeout;
	neigh->areq_timer.data = (unsigned long)neigh;
	neigh->areq_timer.expires = time_add_msec(rtt) + HZ;
	add_timer(&neigh->areq_timer);

	tbl_add(&neigh_tbl, &neigh->l, crit_none);
	
	return areq;
}

static int dsr_ack_req_send(struct neighbor *neigh)
{
	struct dsr_pkt dp;
	struct dsr_ack_req_opt *areq;
	int len = IP_HDR_LEN + DSR_OPT_HDR_LEN + DSR_AREQ_HDR_LEN;
	char buffer[len];
	char *buf = buffer;
	
	if (!neigh)
		return -1;
	
	memset(&dp, 0, sizeof(dp));
	memset(buf, 0, len);
	
	dp.data = NULL; /* No data in this packet */
	dp.data_len = 0;
	dp.dst = neigh->addr;
	dp.nxt_hop = neigh->addr;
	
	dp.src = my_addr();

	dp.iph = dsr_build_ip(buf, len, dp.src, dp.dst, 1);
	
	if (!dp.iph) {
		DEBUG("Could not create IP header\n");
		return -1;
	}
	
	buf += IP_HDR_LEN;
	len -= IP_HDR_LEN;

	dp.dsr_opts_len = len;

	dp.opt_hdr = dsr_opt_hdr_add(buf, len, 0);
	
	if (!dp.opt_hdr) {
		DEBUG("Could not create DSR opt header\n");
		return -1;
	}
	
	buf += DSR_OPT_HDR_LEN;
	len -= DSR_OPT_HDR_LEN;
	
	areq = dsr_ack_req_opt_add(buf, len, neigh->addr);

	if (!areq) {
		DEBUG("Could not create ACK REQ opt\n");
		return -1;
	}
	
	dp.iph->ttl = 1;
	
	DEBUG("Sending ACK REQ for %s\n", print_ip(neigh->addr.s_addr));

	dsr_dev_xmit(&dp);

	return 1;
}

int dsr_ack_add_ack_req(struct in_addr addr)
{
	return !in_tbl(&neigh_tbl, &addr, crit_not_send_req);
}

int dsr_ack_opt_recv(struct dsr_ack_opt *ack)
{
	struct neighbor *neigh;

	if (!ack)
		return -1;

	write_lock_bh(&neigh_tbl.lock);
	
	neigh = __tbl_find(&neigh_tbl, ack, crit_ack);

	if (!neigh) {
		DEBUG("No ACK REQ sent for this ACK??\n");
		write_unlock_bh(&neigh_tbl.lock);
		return DSR_PKT_NONE;
	}
	
	neigh->time_rep_rx = jiffies;
	neigh->id = ++ack_id;
	neigh->reqs = 0;
	
	write_unlock_bh(&neigh_tbl.lock);

	return DSR_PKT_NONE;
}

int dsr_ack_req_opt_recv(struct dsr_pkt *dp, struct dsr_ack_req_opt *areq)
{
	struct neighbor *neigh;

	if (!areq || !dp)
		return -1;

	write_lock_bh(&neigh_tbl.lock);
	
	neigh = __tbl_find(&neigh_tbl, &dp->src, crit_addr);

	if (neigh) {
		if (timer_pending(&neigh->ack_timer))
			del_timer(&neigh->ack_timer);
		
		neigh->id_ack = ntohs(areq->id);
		neigh->ack_timer.function = neigh_tbl_ack_timeout;
		neigh->ack_timer.data = (unsigned long)neigh;
		neigh->ack_timer.expires = time_add_msec(100);
		
		add_timer(&neigh->ack_timer);

		write_unlock_bh(&neigh_tbl.lock);
		return 1;
	}

	write_unlock_bh(&neigh_tbl.lock);
	
	neigh = kmalloc(sizeof(struct neighbor), GFP_ATOMIC);
	
	if (!neigh)
		return -1;
	
	neigh->addr = dp->src;
	neigh->id_ack = ntohs(areq->id);

	init_timer(&neigh->areq_timer);
	neigh->ack_timer.function = neigh_tbl_ack_timeout;
	neigh->ack_timer.data = (unsigned long)neigh;
	neigh->ack_timer.expires = time_add_msec(100);
	add_timer(&neigh->ack_timer);

	tbl_add(&neigh_tbl, &neigh->l, crit_none);
	
	return 1;
}

