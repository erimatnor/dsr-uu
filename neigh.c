#include <linux/proc_fs.h>
#include <linux/timer.h>

#include "tbl.h"
#include "neigh.h"
#include "debug.h"
#include "dsr-rtc.h"
#include "dsr-ack.h"
#include "maint-buf.h"

#define NEIGH_TBL_MAX_LEN 50
#define MAX_AREQ_TX 2

static TBL(neigh_tbl, NEIGH_TBL_MAX_LEN);

#define NEIGH_TBL_GARBAGE_COLLECT_TIMEOUT 3000 
#define NEIGH_TBL_TIMEOUT 2000
#define RTT_DEF 10

#define NEIGH_TBL_PROC_NAME "dsr_neigh_tbl"


struct neighbor {
	struct list_head l;
	struct in_addr addr;
	struct sockaddr hw_addr;
        unsigned short id_req, id_ack;
	struct timer_list ack_req_timer;
	struct timer_list ack_timer;
	unsigned long last_ack_tx_time;
	unsigned long last_ack_rx_time;
	unsigned long last_ack_req_tx_time;
	int rtt, reqs, jitter;
};

static struct timer_list garbage_timer;

static inline int crit_addr(void *pos, void *addr)
{
	struct in_addr *a = addr; 
	struct neighbor *e = pos;
	
	if (e->addr.s_addr == a->s_addr)
		return 1;
	return 0;
}
static inline int crit_addr_get_hwaddr(void *pos, void *data)
{
	struct {
		struct in_addr *a;
		struct sockaddr *hw;
	} *d;
	
	struct neighbor *e = pos;
	d = data;

	if (e->addr.s_addr == d->a->s_addr) {
		memcpy(d->hw, &e->hw_addr, sizeof(struct sockaddr));
		return 1;
	}
	return 0;
}
static inline int crit_addr_get_id(void *pos, void *data)
{
	struct {
		struct in_addr *a;
		unsigned short *id;
	} *d;
	
	struct neighbor *e = pos;
	d = data;

	if (e->addr.s_addr == d->a->s_addr) {
		*(d->id) = ++e->id_req;
		return 1;
	}
	return 0;
}

static inline int crit_addr_id_reset(void *entry, void *data)
{
	struct {
		struct in_addr *a;
		unsigned short *id;
	} *d;
	
	struct neighbor *n = entry;
	d = data;

	if (d) {

		DEBUG("neigh=%s id=%u ent->neigh=%s, ent->id=%u\n", print_ip(d->a->s_addr), *(d->id), print_ip(n->addr.s_addr), n->id_req);

		if (n->addr.s_addr == d->a->s_addr &&
		    n->id_req == *(d->id)) {
			
			del_timer_sync(&n->ack_req_timer);
			
			n->reqs = 0;
			n->rtt = JIFFIES_TO_MSEC(jiffies - n->last_ack_req_tx_time);
			if (n->rtt < 2)
				n->rtt = 2;
			DEBUG("New RTT=%u\n", n->rtt);
		}
		
	} else {
		del_timer_sync(&n->ack_req_timer);
		n->reqs = 0;
	}
	
	return 0;
}

static inline int crit_not_send_req(void *pos, void *addr)
{
	struct in_addr *a = addr; 
	struct neighbor *neigh = pos;
	
	if (neigh->addr.s_addr == a->s_addr &&
	    neigh->reqs >= MAX_AREQ_TX &&
	    jiffies < neigh->last_ack_req_tx_time + neigh->rtt + (HZ/2))
		return 1;
	return 0;
}

static inline int crit_garbage(void *pos, void *foo)
{
	struct neighbor *neigh = pos;

	if (neigh->reqs >= MAX_AREQ_TX ||
	    jiffies - neigh->last_ack_req_tx_time > NEIGH_TBL_TIMEOUT/1000*HZ 
	    /* jiffies - neigh->last_ack_rx_time > NEIGH_TBL_TIMEOUT/1000*HZ */) {
		crit_addr_id_reset(neigh, NULL);		
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
			MSEC_TO_JIFFIES(NEIGH_TBL_GARBAGE_COLLECT_TIMEOUT);
		add_timer(&garbage_timer);	
	}

	read_unlock_bh(&neigh_tbl.lock);
}


static inline void __neigh_tbl_set_ack_req_timer(struct neighbor *neigh)
{
	neigh->ack_req_timer.data = (unsigned long)neigh;

	if (timer_pending(&neigh->ack_req_timer))
		return;
	else {
		neigh->ack_req_timer.expires = jiffies + MSEC_TO_JIFFIES(neigh->rtt + 5);
		add_timer(&neigh->ack_req_timer);

	}
}

void neigh_tbl_set_ack_req_timer(struct in_addr neigh_addr)
{
	struct neighbor *neigh;

	write_lock_bh(&neigh_tbl.lock);
	
	neigh = __tbl_find(&neigh_tbl, &neigh_addr, crit_addr);
	neigh->last_ack_req_tx_time = jiffies;

	__neigh_tbl_set_ack_req_timer(neigh);

	write_unlock_bh(&neigh_tbl.lock);
}

static void neigh_tbl_ack_req_timeout(unsigned long data)
{
	struct neighbor *neigh;
	
	write_lock_bh(&neigh_tbl.lock);
	
	neigh = (struct neighbor *)data;
	
	if (!neigh)
		goto out;
	
	DEBUG("ACK REQ Timeout %s\n", print_ip(neigh->addr.s_addr));
	
	neigh->reqs++;
	

	if (neigh->reqs >= MAX_AREQ_TX) {
		lc_link_del(my_addr(), neigh->addr);
		maint_buf_del(neigh->addr);
		DEBUG("Send RERR!!!\n");
		/* TODO: Salvage */
	} else {
		DEBUG("Resending ACK to %s\n", print_ip(neigh->addr.s_addr));
		__neigh_tbl_set_ack_req_timer(neigh);
		dsr_ack_req_send(neigh->addr, ++neigh->id_req);
	}
 out:
	write_unlock_bh(&neigh_tbl.lock);
}
void neigh_tbl_update_ack_req_tx_time(struct in_addr neigh_addr)
{
	struct neighbor *neigh;

	write_lock_bh(&neigh_tbl.lock);
	
	neigh = __tbl_find(&neigh_tbl, &neigh_addr, crit_addr);
	
	if (neigh)
		neigh->last_ack_req_tx_time = jiffies;

	write_unlock_bh(&neigh_tbl.lock);

}
void neigh_tbl_reset_ack_req_timer(struct in_addr neigh_addr, unsigned short id)
{
	struct {
		struct in_addr *a;
		unsigned short *id;
	} data;

	data.a = &neigh_addr;
	data.id = &id;

	tbl_do_for_each(&neigh_tbl, &data, crit_addr_id_reset);
}


static struct neighbor *neigh_tbl_create(struct in_addr addr, 
					 struct sockaddr *hw_addr,
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
	memcpy(&neigh->hw_addr, hw_addr, sizeof(struct sockaddr));

	init_timer(&neigh->ack_req_timer);
	init_timer(&neigh->ack_timer);
	
	neigh->ack_req_timer.function = neigh_tbl_ack_req_timeout;
	neigh->ack_req_timer.data = (unsigned long)neigh;

/* 	neigh->ack_timer.function = neigh_tbl_ack_timeout; */
/* 	neigh->ack_timer.data = (unsigned long)neigh; */
	
	
/* 	garbage_timer.expires = jiffies + NEIGH_TBL_GARBAGE_COLLECT_TIMEOUT / 1000*HZ; */
/* 	add_timer(&garbage_timer); */

	return neigh;
}

int neigh_tbl_add(struct in_addr neigh_addr, struct sockaddr *hw_addr)
{
	struct neighbor *neigh;
	
	if (in_tbl(&neigh_tbl, &neigh_addr, crit_addr))
		return 0;

	neigh = neigh_tbl_create(neigh_addr, hw_addr, 0, 0);

	if (!neigh) {
		DEBUG("Could not create new neighbor entry\n");
		return -1;
	}
	
	tbl_add(&neigh_tbl, &neigh->l, crit_none);

	return 1;
}

int neigh_tbl_get_hwaddr(struct in_addr neigh_addr, struct sockaddr *hw_addr)
{
	struct {
		struct in_addr *a;
		struct sockaddr *hw;
	} data;
	
	data.a = &neigh_addr;
	data.hw = hw_addr;
	
	return in_tbl(&neigh_tbl, &data, crit_addr_get_hwaddr);
}

unsigned short neigh_tbl_get_id(struct in_addr neigh_addr)
{
	unsigned short id = 0;

	struct {
		struct in_addr *a;
		unsigned short *id;
	} data;
	
	data.a = &neigh_addr;
	data.id = &id;
	
	in_tbl(&neigh_tbl, &data, crit_addr_get_id);
	
	return id;
}

static int neigh_tbl_print(char *buf)
{
	struct list_head *pos;
	int len = 0;
    
	read_lock_bh(&neigh_tbl.lock);
    
	len += sprintf(buf, "# %-15s %-17s %-6s %-6s %-6s %-12s\n", "Addr", "HwAddr", "Reqs", "RTT", "Id", "AckReqTxTime" /*, "AckRxTime","AckTxTime" */);

	list_for_each(pos, &neigh_tbl.head) {
		struct neighbor *neigh =(struct neighbor *)pos;
		
		len += sprintf(buf+len, "  %-15s %-17s %-6d %-6u %-6u %-12lu\n", 
			       print_ip(neigh->addr.s_addr), 
			       print_eth(neigh->hw_addr.sa_data), neigh->reqs,
			       neigh->rtt, neigh->id_req,
			       neigh->last_ack_req_tx_time ? JIFFIES_TO_MSEC(jiffies - neigh->last_ack_req_tx_time) : 0
/* 			       neigh->last_ack_rx_time ? (jiffies - neigh->last_ack_rx_time) * 1000 / HZ : 0, */
			       /* neigh->last_ack_tx_time ? (jiffies - neigh->last_ack_tx_time) * 1000 / HZ : 0 */);
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
	tbl_flush(&neigh_tbl, crit_addr_id_reset);
	proc_net_remove(NEIGH_TBL_PROC_NAME);
}
