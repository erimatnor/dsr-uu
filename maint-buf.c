#include "dsr.h"
#include "debug.h"
#include "tbl.h"
#include "neigh.h"
#include "dsr-ack.h"
#include "dsr-rtc.h"

#define MAINT_BUF_MAX_LEN 100
#define MAX_REXMT 2

TBL(maint_buf, MAINT_BUF_MAX_LEN);

struct timer_list ack_timer;

struct maint_entry {
	struct list_head l;
	struct in_addr nxt_hop;
	unsigned int rexmt;
	unsigned short id;
	unsigned long tx_time;
	struct dsr_pkt *dp;
};
static void maint_buf_timeout(unsigned long data);

static inline int crit_nxt_hop_id(void *pos, void *data)
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
	    m->id == *(d->id))
		return 1;

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

static void maint_buf_set_timeout(void)
{
	struct maint_entry *m;
	struct in_addr neigh_addr;
	unsigned long tx_time, rto;

	if (timer_pending(&ack_timer))
		return;

	read_lock_bh(&maint_buf.lock);

	m = __tbl_find(&maint_buf, NULL, crit_none);

	if (!m) {
		read_unlock_bh(&maint_buf.lock);
		return;
	}
	
	tx_time = m->tx_time;
	neigh_addr = m->nxt_hop;

	read_unlock_bh(&maint_buf.lock);
	
	rto = neigh_tbl_get_rto(neigh_addr);

	if (tx_time + rto < jiffies) {		
		maint_buf_timeout(0);
		return;
	}
	ack_timer.expires = tx_time + rto;

	DEBUG("jiff=%lu exp=%lu\n", jiffies, ack_timer.expires);

	add_timer(&ack_timer);
}

static struct maint_entry *maint_entry_create(struct dsr_pkt *dp)
{
	struct maint_entry *m;
	unsigned short id;
	
	id = neigh_tbl_get_id(dp->nxt_hop);
	
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
	m->dp = dsr_pkt_alloc(skb_copy(dp->skb, GFP_ATOMIC));

	return m;
}

int maint_buf_add(struct dsr_pkt *dp)
{
	struct maint_entry *m;
	unsigned short id;
	
	m = maint_entry_create(dp);
	
	if (!m)
		return -1;
	
	id = m->id;

	if (tbl_add_tail(&maint_buf, &m->l) < 0) {
		DEBUG("Buffer full, dropping packet!\n");
		dsr_pkt_free(dp);
		kfree(m);
		return -1;
	}
	
	dsr_ack_req_opt_add(dp, id);

	if (!timer_pending(&ack_timer))
	    maint_buf_set_timeout();

	return 1;
}
static void maint_buf_timeout(unsigned long data)
{
	struct maint_entry *m;
	
	m = tbl_detach_first(&maint_buf);
	
	if (!m) {
		DEBUG("Nothing in maint buf\n");
		return;
	}
	
	DEBUG("nxt_hop=%s id=%u rexmt=%d\n", 
	      print_ip(m->nxt_hop.s_addr), m->id, m->rexmt);

	/* Increase the number of retransmits */
	m->rexmt++;
	
	if (m->rexmt >= PARAM(MaxMaintRexmt)) {
		DEBUG("Rexmt max reached, send RERR\n");
		lc_link_del(my_addr(), m->nxt_hop);
		neigh_tbl_del(m->nxt_hop);
		dsr_pkt_free(m->dp);
		kfree(m);
		return;
	} 
	
	/* Set new Transmit time */
	m->tx_time = jiffies;
		
	/* Send new ACK REQ */
	dsr_ack_req_send(m->nxt_hop, m->id);
	
	/* Add at end of buffer */
	tbl_add_tail(&maint_buf, &m->l);
	
	maint_buf_set_timeout();

	return;
}

int maint_buf_del(struct in_addr nxt_hop, unsigned short id)
{
	struct maint_entry *m;
	struct {
		struct in_addr *nxt_hop;
		unsigned short *id;
	} d;
	
	/* Disable timer in case we remove the packet that is first in the
	 * queue */
	if (timer_pending(&ack_timer))
		del_timer_sync(&ack_timer);
	
	d.id = &id;
	d.nxt_hop = &nxt_hop;

	/* Find the buffered packet to delete */
	m = tbl_find_detach(&maint_buf, &d, crit_nxt_hop_id);

	if (!m)
		return 0;
	
	if (m->dp)
		dsr_pkt_free(m->dp);
	
	kfree(m);
	
	DEBUG("Removed pkt id=%u next hop %s\n", id, print_ip(nxt_hop.s_addr));

	/* Set the new timeout */
	maint_buf_set_timeout();

	return 1;
}

void maint_buf_rexmt(struct in_addr nxt_hop)
{
	tbl_do_for_each(&maint_buf, &nxt_hop, crit_nxt_hop_rexmt);
}

int maint_buf_init(void)
{
	init_timer(&ack_timer);
	
	ack_timer.function = &maint_buf_timeout;
	ack_timer.expires = 0;
	
	return 1;
}


void maint_buf_cleanup(void)
{
	del_timer_sync(&ack_timer);
	tbl_flush(&maint_buf, crit_free_pkt);
}
