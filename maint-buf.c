#include <linux/proc_fs.h>
#include <linux/module.h>

#include "dsr.h"
#include "debug.h"
#include "tbl.h"
#include "neigh.h"
#include "dsr-ack.h"
#include "dsr-rtc.h"
#include "dsr-rerr.h"

#define MAINT_BUF_PROC_FS_NAME "maint_buf"

TBL(maint_buf, MAINT_BUF_MAX_LEN);

static struct timer_list ack_timer;

struct maint_entry {
	struct list_head l;
	struct in_addr nxt_hop;
	unsigned int rexmt;
	unsigned short id;
	unsigned long tx_time;
	unsigned long rto;
	int timer_set;
	struct dsr_pkt *dp;
};

static void maint_buf_set_timeout(void);
static void maint_buf_timeout(unsigned long data);

static inline int crit_addr_id_del(void *pos, void *data)
{	
	struct maint_entry *m = pos;
	struct {
		struct in_addr *nxt_hop;
		unsigned short *id;
	} *d;
	
	d = data;

	if (!d)
		return 0;
	
	if (m->nxt_hop.s_addr == d->nxt_hop->s_addr &&
	    m->id == *(d->id)) {
		
		if (m->dp)
			dsr_pkt_free(m->dp);

		if (m->timer_set) 
			del_timer_sync(&ack_timer);
		
		return 1;
	}
	return 0;
}

static inline int crit_addr_del(void *pos, void *data)
{	
	struct maint_entry *m = pos;
	struct in_addr *addr = data;
	
	if (m->nxt_hop.s_addr == addr->s_addr) {
		
		if (m->dp)
			dsr_pkt_free(m->dp);

		if (m->timer_set) 
			del_timer_sync(&ack_timer);
		
		return 1;
	}
	return 0;
}

static inline int crit_free_pkt(void *pos, void *foo)
{
	struct maint_entry *m = pos;
	
	if (m->dp)
		dsr_pkt_free(m->dp);
	return 1;
}

static inline int crit_nxt_hop_rexmt(void *pos, void *nh)
{
	struct in_addr *nxt_hop = nh; 
	struct maint_entry *m = pos;
	
	if (m->nxt_hop.s_addr == nxt_hop->s_addr) {
		m->rexmt++;
		m->tx_time = jiffies;
		return 1;
	}
	return 0;
}

void maint_buf_set_max_len(unsigned int max_len)
{
	maint_buf.max_len = max_len;
}

static struct maint_entry *maint_entry_create(struct dsr_pkt *dp)
{
	struct maint_entry *m;
	unsigned short id;
	unsigned long rto;
	
	id = neigh_tbl_get_id(dp->nxt_hop);
	rto = neigh_tbl_get_rto(dp->nxt_hop);

	if (!id) {
		DEBUG("Could not get request id for %s\n", 
		      print_ip(dp->nxt_hop.s_addr));
		return NULL;		
	}
	
	m = kmalloc(sizeof(struct maint_entry), GFP_ATOMIC);

	if (!m) {
		DEBUG("Could not allocate maintenance buf entry\n");
		return NULL;
	}
	
	m->nxt_hop = dp->nxt_hop;
	m->tx_time = jiffies;
	m->rexmt = 0;
	m->id = id;
	m->rto = rto;
	m->timer_set = 0;
	m->dp = dsr_pkt_alloc(skb_copy(dp->skb, GFP_ATOMIC));
	m->dp->nxt_hop = dp->nxt_hop;

	return m;
}

int maint_buf_add(struct dsr_pkt *dp)
{
	struct maint_entry *m;
	int empty = 0;
	
	if (TBL_EMPTY(&maint_buf))
		empty = 1;

	m = maint_entry_create(dp);
	
	if (!m)
		return -1;
		
	if (tbl_add_tail(&maint_buf, &m->l) < 0) {
		DEBUG("Buffer full, not buffering!\n");
		kfree(m);
		return -1;
	}
	
	dsr_ack_req_opt_add(dp, m->id);

	if (empty)
		maint_buf_set_timeout();

	return 1;
}
static void maint_buf_set_timeout(void)
{
	struct maint_entry *m;
	unsigned long tx_time, rto;
	unsigned long now = jiffies;
	
	write_lock_bh(&maint_buf.lock);
	/* Get first packet in maintenance buffer */
	m = __tbl_find(&maint_buf, NULL, crit_none);
	
	if (!m) {
		DEBUG("No packet to set timeout for\n");
		write_unlock_bh(&maint_buf.lock);
		return;
	}

	tx_time = m->tx_time;
	rto = m->rto;
	m->timer_set = 1;

	write_unlock_bh(&maint_buf.lock);
	
	DEBUG("now=%lu exp=%lu\n", now, tx_time + rto);

	/* Check if this packet has already expired */
	if (now > tx_time + rto) 
		maint_buf_timeout(0);
	else {		
		ack_timer.expires = tx_time + rto;
		add_timer(&ack_timer);
	}
}


static void maint_buf_timeout(unsigned long data)
{
	struct maint_entry *m;
	
	if (timer_pending(&ack_timer))
	    return;

	m = tbl_find_detach(&maint_buf, NULL, crit_none);

	if (!m) {
		DEBUG("Nothing in maint buf\n");
		return;
	}

	DEBUG("nxt_hop=%s id=%u rexmt=%d\n", 
	      print_ip(m->nxt_hop.s_addr), m->id, m->rexmt);
	
	m->timer_set = 0;

/* 	if (m->acked) { */
/* 		DEBUG("Packet was ACK'd, freeing\n"); */
/* 		if (m->dp) */
/* 			dsr_pkt_free(m->dp); */
/* 		kfree(m); */
/* 		goto out; */
/* 	} */

	/* Increase the number of retransmits */	
	if (++m->rexmt >= PARAM(MaxMaintRexmt)) {
		DEBUG("MaxMaintRexmt reached, send RERR\n");
		lc_link_del(my_addr(), m->nxt_hop);
/* 		dsr_rtc_del(my_addr(), m->nxt_hop); */
/* 		neigh_tbl_del(m->nxt_hop); */

		dsr_rerr_send(m->dp, m->nxt_hop);

		if (m->dp)
			dsr_pkt_free(m->dp);
		kfree(m);
		goto out;
	} 
	
	/* Set new Transmit time */
	m->tx_time = jiffies;
		
	/* Send new ACK REQ */
	dsr_ack_req_send(m->nxt_hop, m->id);
	
	/* Add at end of buffer */
	tbl_add_tail(&maint_buf, &m->l);
 out:	
	maint_buf_set_timeout();

	return;
}

int maint_buf_del_all(struct in_addr nxt_hop)
{
      	/* Find the buffered packet to mark as acked */
	if (!tbl_for_each_del(&maint_buf, &nxt_hop, crit_addr_del))
		return 0;

	if (!TBL_EMPTY(&maint_buf) && !timer_pending(&ack_timer))
		maint_buf_set_timeout();
	
	return 1;
}
int maint_buf_del(struct in_addr nxt_hop, unsigned short id)
{
	struct {
		struct in_addr *nxt_hop;
		unsigned short *id;
	} d;
	
	d.id = &id;
	d.nxt_hop = &nxt_hop;

	/* Find the buffered packet to mark as acked */
	if (!tbl_find_del(&maint_buf, &d, crit_addr_id_del))
		return 0;

	if (!TBL_EMPTY(&maint_buf) && !timer_pending(&ack_timer))
		maint_buf_set_timeout();
	
	return 1;
}
static int maint_buf_get_info(char *buffer, char **start, off_t offset, int length)
{
	struct list_head *p;
	int len;

	len = sprintf(buffer, "# %-15s %-6s %-6s %-8s\n", "IPAddr", "Rexmt", "Id", "TxTime");

	read_lock_bh(&maint_buf.lock);

	list_for_each_prev(p, &maint_buf.head) {
		struct maint_entry *e = (struct maint_entry *)p;
		
		if (e && e->dp)
			len += sprintf(buffer+len, "  %-15s %-6d %-6u %-8lu\n", print_ip(e->dp->dst.s_addr), e->rexmt, e->id, (jiffies - e->tx_time) / HZ);
	}

	len += sprintf(buffer+len,
		       "\nQueue length      : %u\n"
		       "Queue max. length : %u\n",
		       maint_buf.len,
		       maint_buf.max_len);
	
	read_unlock_bh(&maint_buf.lock);
	
	*start = buffer + offset;
	len -= offset;

	if (len > length)
		len = length;
	else if (len < 0)
		len = 0;
	return len;
}

int maint_buf_init(void)
{
	struct proc_dir_entry *proc;
		
	init_timer(&ack_timer);
	
	ack_timer.function = &maint_buf_timeout;
	ack_timer.expires = 0;

	proc = proc_net_create(MAINT_BUF_PROC_FS_NAME, 0, maint_buf_get_info);
	if (proc)
		proc->owner = THIS_MODULE;
	else {
		printk(KERN_ERR "maint_buf: failed to create proc entry\n");
		return -1;
	}

	return 1;
}


void maint_buf_cleanup(void)
{
	del_timer_sync(&ack_timer);
	tbl_flush(&maint_buf, crit_free_pkt);
	proc_net_remove(MAINT_BUF_PROC_FS_NAME);
}
