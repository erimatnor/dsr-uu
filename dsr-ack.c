#include <linux/proc_fs.h>

#include "tbl.h"
#include "dsr.h"
#include "debug.h"
#include "dsr-opt.h"
#include "dsr-ack.h"
#include "dsr-dev.h"
#include "dsr-rtc.h"

unsigned short ID = 0;

#define ACK_TBL_MAX_LEN 24
#define MAX_AREQ_TX 2

static TBL(neigh_tbl, ACK_TBL_MAX_LEN);


#define NEIGH_TBL_GARBAGE_COLLECT_TIMEOUT 3000 
#define NEIGH_TBL_TIMEOUT 2000

#define ACK_SEND_REQ_INTERVAL 1000
#define RTT_DEF 10

#define NEIGH_TBL_PROC_NAME "dsr_neigh_tbl"

struct neighbor {
	struct list_head l;
	struct in_addr addr;
        int id_req, id_ack;
	struct timer_list ack_req_timer;
	struct timer_list ack_timer;
	unsigned long last_ack_tx_time;
	unsigned long last_ack_rx_time;
	unsigned long last_req_tx_time;
	int rtt, reqs, jitter;
};

static struct timer_list garbage_timer;

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
	    e->id_req == ntohs(a->id) && 
	    a->dst == myaddr.s_addr)
		return 1;
	return 0;
}

static inline int timer_remove(void *entry, void *data)
{
	struct neighbor *n = entry;

	if (timer_pending(&n->ack_req_timer))
		del_timer(&n->ack_req_timer);
		
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
	    jiffies < neigh->last_req_tx_time + neigh->rtt + (HZ/2))
		return 1;
	return 0;
}

static inline int crit_garbage(void *pos, void *foo)
{
	struct neighbor *neigh = pos;

	if (neigh->reqs >= MAX_AREQ_TX ||
	    jiffies - neigh->last_ack_tx_time > NEIGH_TBL_TIMEOUT/1000*HZ 
	    /* jiffies - neigh->last_ack_rx_time > NEIGH_TBL_TIMEOUT/1000*HZ */) {
		timer_remove(neigh, foo);		
		return 1;
	}
	return 0;
}

static void neigh_tbl_garbage_timeout(unsigned long data)
{
	tbl_for_each_del(&neigh_tbl, NULL, crit_garbage);
	
	read_lock_bh(&neigh_tbl.lock);
	
	if (!TBL_EMPTY(&neigh_tbl)) {
		garbage_timer.expires = jiffies + 
			NEIGH_TBL_GARBAGE_COLLECT_TIMEOUT / 1000*HZ;
		add_timer(&garbage_timer);	
	}

	read_unlock_bh(&neigh_tbl.lock);
}

static void neigh_tbl_ack_req_timeout(unsigned long data)
{
	struct neighbor *neigh;
	
	write_lock_bh(&neigh_tbl.lock);
	
	neigh = (struct neighbor *)data;
	
	if (!neigh)
		goto out;
	
	DEBUG("ACK REQ Timeout %s\n", print_ip(neigh->addr.s_addr));
	
	if (neigh->reqs >= MAX_AREQ_TX) {
		lc_link_del(my_addr(), neigh->addr);
		DEBUG("Send RERR!!!\n");
	} else
		dsr_ack_req_send(neigh);
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

out:
	write_unlock_bh(&neigh_tbl.lock);	
}

static struct neighbor *__neigh_create(struct in_addr addr, 
				       unsigned short id_req, 
				       unsigned short id_ack)
{
	struct neighbor *neigh;
	
	neigh = kmalloc(sizeof(struct neighbor), GFP_ATOMIC);
	
	if (!neigh)
		return NULL;
	
	memset(neigh, 0, sizeof(struct neighbor));

	neigh->id_req = id_req;
	neigh->id_ack = id_ack;
	neigh->addr = addr;
	neigh->rtt = RTT_DEF;
	neigh->reqs = 0;
	
	init_timer(&neigh->ack_req_timer);
	init_timer(&neigh->ack_timer);
	
	neigh->ack_req_timer.function = neigh_tbl_ack_req_timeout;
	neigh->ack_req_timer.data = (unsigned long)neigh;

	neigh->ack_timer.function = neigh_tbl_ack_timeout;
	neigh->ack_timer.data = (unsigned long)neigh;
	
	__tbl_add(&neigh_tbl, &neigh->l, crit_none);
	
	garbage_timer.expires = jiffies + NEIGH_TBL_GARBAGE_COLLECT_TIMEOUT / 1000*HZ;
	add_timer(&garbage_timer);

	return neigh;
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
	
	neigh->last_ack_tx_time = jiffies;

	dsr_dev_xmit(&dp);

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
	int len = IP_HDR_LEN + DSR_OPT_HDR_LEN + DSR_ACK_REQ_HDR_LEN;
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
	
	ack_req = dsr_ack_req_opt_add(buf, len, neigh->addr);

	if (!ack_req) {
		DEBUG("Could not create ACK REQ opt\n");
		return -1;
	}
	
	dp.iph->ttl = 1;
	
	DEBUG("Sending ACK REQ for %s id=%u\n", print_ip(neigh->addr.s_addr), neigh->id_req);

	dsr_dev_xmit(&dp);

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

static int neigh_tbl_print(char *buf)
{
	struct list_head *pos;
	int len = 0;
    
	read_lock_bh(&neigh_tbl.lock);
    
	len += sprintf(buf, "# %-15s %-6s %-6s %-6s %-12s %-12s %-12s\n", "Addr", "Reqs", "RTT", "Id", "AckReqTxTime", "AckRxTime", "AckTxTime");

	list_for_each(pos, &neigh_tbl.head) {
		struct neighbor *neigh =(struct neighbor *)pos;
		
		len += sprintf(buf+len, "  %-15s %-6d %-6u %-6u %-12lu %-12lu %-12lu\n", 
			       print_ip(neigh->addr.s_addr), neigh->reqs,
			       neigh->rtt, neigh->id_req,
			       neigh->last_req_tx_time ? (jiffies - neigh->last_req_tx_time) * 1000 / HZ : 0,
			       neigh->last_ack_rx_time ? (jiffies - neigh->last_ack_rx_time) * 1000 / HZ : 0,
			       neigh->last_ack_tx_time ? (jiffies - neigh->last_ack_tx_time) * 1000 / HZ : 0);
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
	init_timer(&garbage_timer);
	
	garbage_timer.function = neigh_tbl_garbage_timeout;

	proc_net_create(NEIGH_TBL_PROC_NAME, 0, neigh_tbl_proc_info);
	return 0;
}


void __exit neigh_tbl_cleanup(void)
{
	tbl_flush(&neigh_tbl, timer_remove);
	proc_net_remove(NEIGH_TBL_PROC_NAME);
}
