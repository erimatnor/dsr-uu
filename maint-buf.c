#ifdef __KERNEL__
#include <linux/proc_fs.h>
#include <linux/module.h>
#endif

#include "dsr.h"
#include "debug.h"
#include "tbl.h"
#include "neigh.h"
#include "dsr-ack.h"
#include "link-cache.h"
#include "dsr-rerr.h"
#include "timer.h"

#ifdef NS2
#include "ns-agent.h"
#else

#define MAINT_BUF_PROC_FS_NAME "maint_buf"

TBL(maint_buf, MAINT_BUF_MAX_LEN);

static DSRUUTimer ack_timer;

#endif /* NS2 */

struct maint_entry {
	list_t l;
	struct in_addr nxt_hop;
	unsigned int rexmt;
	unsigned short id;
	Time tx_time;
	Time rto;
	int timer_set;
	struct dsr_pkt *dp;
};

struct maint_buf_query {
	struct in_addr *nxt_hop;
	unsigned short *id;
#ifdef NS2
	DSRUU *a_;
#else
	DSRUUTimer *t;
#endif
};
	
static void maint_buf_set_timeout(void);
static void maint_buf_timeout(unsigned long data);

static inline int crit_addr_id_del(void *pos, void *data)
{	
	struct maint_entry *m = (struct maint_entry *)pos;
	struct maint_buf_query *q = (struct maint_buf_query *)data;

	if (m->nxt_hop.s_addr == q->nxt_hop->s_addr &&
	    m->id == *(q->id)) {
		
		if (m->dp)
			dsr_pkt_free(m->dp);
#ifdef NS2
		if (m->timer_set)
			q->a_->del_timer_sync(&q->a_->ack_timer);
#else
		if (m->timer_set) 
			del_timer_sync(q->t);
#endif		
		return 1;
	}
	return 0;
}

static inline int crit_addr_del(void *pos, void *data)
{	
	struct maint_entry *m = (struct maint_entry *)pos;
	struct maint_buf_query *q = (struct maint_buf_query *)data;
	
	if (m->nxt_hop.s_addr == q->nxt_hop->s_addr) {
		
		if (m->dp)
			dsr_pkt_free(m->dp);
#ifdef NS2
		if (m->timer_set)
			q->a_->del_timer_sync(&q->a_->ack_timer);
#else
		if (m->timer_set) 
			del_timer_sync(q->t);
#endif		
		return 1;
	}
	return 0;
}

static inline int crit_free_pkt(void *pos, void *foo)
{
	struct maint_entry *m = (struct maint_entry *)pos;
	
	if (m->dp)
		dsr_pkt_free(m->dp);
	return 1;
}

static inline int crit_nxt_hop_rexmt(void *pos, void *nh)
{
	struct in_addr *nxt_hop = (struct in_addr *)nh; 
	struct maint_entry *m = (struct maint_entry *)pos;
	
	if (m->nxt_hop.s_addr == nxt_hop->s_addr) {
		m->rexmt++;
		m->tx_time = TimeNow;
		return 1;
	}
	return 0;
}

void NSCLASS maint_buf_set_max_len(unsigned int max_len)
{
	maint_buf.max_len = max_len;
}

static struct maint_entry *maint_entry_create(struct dsr_pkt *dp, 
					      unsigned short id,
					      unsigned long rto)
{
	struct maint_entry *m;
	
	
	m = (struct maint_entry *)MALLOC(sizeof(struct maint_entry), GFP_ATOMIC);

	if (!m)
		return NULL;
	
	m->nxt_hop = dp->nxt_hop;
	m->tx_time = TimeNow;
	m->rexmt = 0;
	m->id = id;
	m->rto = rto;
	m->timer_set = 0;
#ifdef NS2
	m->dp = dsr_pkt_alloc(NULL);
#else
	m->dp = dsr_pkt_alloc(skb_copy(dp->skb, GFP_ATOMIC));
#endif
	m->dp->nxt_hop = dp->nxt_hop;

	return m;
}

int NSCLASS maint_buf_add(struct dsr_pkt *dp)
{
	struct maint_entry *m;
	int empty = 0;
	unsigned short id;
	unsigned long rto;
	
	if (TBL_EMPTY(&maint_buf))
		empty = 1;
	
	id = neigh_tbl_get_id(dp->nxt_hop);
	rto = neigh_tbl_get_rto(dp->nxt_hop);
	
	if (!id) {
		DEBUG("Could not get request id for %s\n", 
		      print_ip(dp->nxt_hop));
		return -1;		
	}
	
	m = maint_entry_create(dp, id, rto);
	
	if (!m)
		return -1;
		
	if (tbl_add_tail(&maint_buf, &m->l) < 0) {
		DEBUG("Buffer full, not buffering!\n");
		FREE(m);
		return -1;
	}
	
	if (PARAM(UseNetworkLayerAck)) {
		dsr_ack_req_opt_add(dp, m->id);
		
		if (empty)
			maint_buf_set_timeout();
	}
	return 1;
}

void NSCLASS maint_buf_set_timeout(void)
{
	struct maint_entry *m;
	Time tx_time, rto;
	Time now = TimeNow;
	
	DSR_WRITE_LOCK(&maint_buf.lock);
	/* Get first packet in maintenance buffer */
	m = (struct maint_entry *)__tbl_find(&maint_buf, NULL, crit_none);
	
	if (!m) {
		DEBUG("No packet to set timeout for\n");
		DSR_WRITE_UNLOCK(&maint_buf.lock);
		return;
	}

	tx_time = m->tx_time;
	rto = m->rto;
	m->timer_set = 1;

	DSR_WRITE_UNLOCK(&maint_buf.lock);
	
	DEBUG("now=%lu exp=%lu\n", now, tx_time + rto);

	/* Check if this packet has already expired */
	if (now > tx_time + rto) 
		maint_buf_timeout(0);
	else {		
		ack_timer.expires = tx_time + rto;
		add_timer(&ack_timer);
	}
}


void NSCLASS maint_buf_timeout(unsigned long data)
{
	struct maint_entry *m;
	
	if (timer_pending(&ack_timer))
	    return;

	m = (struct maint_entry *)tbl_detach_first(&maint_buf);

	if (!m) {
		DEBUG("Nothing in maint buf\n");
		return;
	}

	DEBUG("nxt_hop=%s id=%u rexmt=%d\n", 
	      print_ip(m->nxt_hop), m->id, m->rexmt);
	
	m->timer_set = 0;

	/* Increase the number of retransmits */	
	if (++m->rexmt >= PARAM(MaxMaintRexmt)) {
		DEBUG("MaxMaintRexmt reached, send RERR\n");
		lc_link_del(my_addr(), m->nxt_hop);
/* 		dsr_rtc_del(my_addr(), m->nxt_hop); */
/* 		neigh_tbl_del(m->nxt_hop); */

		dsr_rerr_send(m->dp, m->nxt_hop);

		if (m->dp)
			dsr_pkt_free(m->dp);
		FREE(m);
		goto out;
	} 
	
	/* Set new Transmit time */
	m->tx_time = TimeNow;
		
	/* Send new ACK REQ */
	dsr_ack_req_send(m->nxt_hop, m->id);
	
	/* Add at end of buffer */
	tbl_add_tail(&maint_buf, &m->l);
 out:	
	maint_buf_set_timeout();

	return;
}

int NSCLASS maint_buf_del_all(struct in_addr nxt_hop)
{
	struct maint_buf_query q;

	q.nxt_hop = &nxt_hop;
#ifdef NS2
	q.a_ = this;
#else
	q.t = &ack_timer;
#endif
      	/* Find the buffered packet to mark as acked */
	if (!tbl_for_each_del(&maint_buf, &q, crit_addr_del))
		return 0;

	if (!TBL_EMPTY(&maint_buf) && !timer_pending(&ack_timer))
		maint_buf_set_timeout();
	
	return 1;
}

int NSCLASS maint_buf_del(struct in_addr nxt_hop, unsigned short id)
{
	struct maint_buf_query q;
	
	q.id = &id;
	q.nxt_hop = &nxt_hop;
#ifdef NS2
	q.a_ = this;
#else
	q.t = &ack_timer;
#endif
	/* Find the buffered packet to mark as acked */
	if (!tbl_find_del(&maint_buf, &q, crit_addr_id_del))
		return 0;

	if (!TBL_EMPTY(&maint_buf) && !timer_pending(&ack_timer))
		maint_buf_set_timeout();
	
	return 1;
}

#ifdef __KERNEL__
static int maint_buf_get_info(char *buffer, char **start, off_t offset, int length)
{
	list_t *p;
	int len;

	len = sprintf(buffer, "# %-15s %-6s %-6s %-8s\n", "IPAddr", "Rexmt", "Id", "TxTime");

	DSR_READ_LOCK(&maint_buf.lock);

	list_for_each_prev(p, &maint_buf.head) {
		struct maint_entry *e = (struct maint_entry *)p;
		
		if (e && e->dp)
			len += sprintf(buffer+len, "  %-15s %-6d %-6u %-8lu\n",
				       print_ip(e->dp->dst), e->rexmt, e->id, 
				       (TimeNow - e->tx_time) / HZ);
	}

	len += sprintf(buffer+len,
		       "\nQueue length      : %u\n"
		       "Queue max. length : %u\n",
		       maint_buf.len,
		       maint_buf.max_len);
	
	DSR_READ_UNLOCK(&maint_buf.lock);
	
	*start = buffer + offset;
	len -= offset;

	if (len > length)
		len = length;
	else if (len < 0)
		len = 0;
	return len;
}

#endif /* __KERNEL__ */

int NSCLASS maint_buf_init(void)
{	
#ifdef __KERNEL__
	struct proc_dir_entry *proc;
	
	proc = proc_net_create(MAINT_BUF_PROC_FS_NAME, 0, maint_buf_get_info);
	if (proc)
		proc->owner = THIS_MODULE;
	else {
		printk(KERN_ERR "maint_buf: failed to create proc entry\n");
		return -1;
	}
#endif
	init_timer(&ack_timer);	
	
	ack_timer.function = &NSCLASS maint_buf_timeout;
	ack_timer.expires = 0;

	INIT_TBL(&maint_buf, MAINT_BUF_MAX_LEN);
 
	return 1;
}

void NSCLASS maint_buf_cleanup(void)
{
	del_timer_sync(&ack_timer);

	tbl_flush(&maint_buf, crit_free_pkt);

#ifdef __KERNEL__
	proc_net_remove(MAINT_BUF_PROC_FS_NAME);
#endif
}
