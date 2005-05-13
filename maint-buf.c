#ifdef __KERNEL__
#include <linux/proc_fs.h>
#include <linux/module.h>
#endif

#ifdef NS2
#include "ns-agent.h"
#else

#include "dsr.h"
#include "debug.h"
#include "tbl.h"
#include "neigh.h"
#include "dsr-ack.h"
#include "link-cache.h"
#include "dsr-rerr.h"
#include "timer.h"
#include "maint-buf.h"

#define MAINT_BUF_PROC_FS_NAME "maint_buf"

TBL(maint_buf, MAINT_BUF_MAX_LEN);

static DSRUUTimer ack_timer;

#endif				/* NS2 */

struct maint_entry {
	list_t l;
	struct in_addr nxt_hop;
	unsigned int rexmt;
	unsigned short id;
	struct timeval tx_time, expires;
	usecs_t rto;
	int ack_req_sent;
	struct dsr_pkt *dp;
};

struct maint_buf_query {
	struct in_addr *nxt_hop;
	unsigned short *id;
	usecs_t rtt;
};

static int maint_buf_print(struct tbl *t, char *buffer);

static inline int crit_addr_id_del(void *pos, void *data)
{
	struct maint_entry *m = (struct maint_entry *)pos;
	struct maint_buf_query *q = (struct maint_buf_query *)data;

	if (m->nxt_hop.s_addr == q->nxt_hop->s_addr && m->id <= *(q->id)) {
		struct timeval now;
		
		gettime(&now);
		
		/* Only update RTO if this was not a retransmission */
		if (m->id == *(q->id) && m->rexmt == 0)
			q->rtt = timeval_diff(&now, &m->tx_time);

		if (m->dp) {
#ifdef NS2
			if (m->dp->p)
/* 				drop(m->dp->p, DROP_RTR_SALVAGE); */
				Packet::free(m->dp->p);
#endif
			dsr_pkt_free(m->dp);
			return 1;
		}	
	}
	return 0;
}

static inline int crit_addr_del(void *pos, void *data)
{
	struct maint_entry *m = (struct maint_entry *)pos;
	struct in_addr *nxt_hop = (struct in_addr *)data;

	if (m->nxt_hop.s_addr == nxt_hop->s_addr) {
		if (m->dp) {
#ifdef NS2
			if (m->dp->p)
				Packet::free(m->dp->p);
#endif
			dsr_pkt_free(m->dp);
			return 1;
		}
	}
	return 0;
}

static inline int crit_expires(void *pos, void *data)
{
	struct maint_entry *m = (struct maint_entry *)pos;
	struct maint_entry *m_new = (struct maint_entry *)data;

	if (timeval_diff(&m->expires, &m_new->expires) > 0)
		return 1;
	return 0;

}

static inline int crit_ack_req_sent(void *pos, void *data)
{
	struct maint_entry *m = (struct maint_entry *)pos;

	if (m->ack_req_sent)
		return 1;
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

	m = (struct maint_entry *)MALLOC(sizeof(struct maint_entry),
					 GFP_ATOMIC);

	if (!m)
		return NULL;

	m->nxt_hop = dp->nxt_hop;
	gettime(&m->tx_time);
	m->expires = m->tx_time;
	timeval_add_usecs(&m->expires, rto);
	m->rexmt = 0;
	m->id = id;
	m->rto = rto;
	m->ack_req_sent = 0;
#ifdef NS2
	if (dp->p)
		m->dp = dsr_pkt_alloc(dp->p->copy());
#else
	m->dp = dsr_pkt_alloc(skb_copy(dp->skb, GFP_ATOMIC));
#endif
	m->dp->nxt_hop = dp->nxt_hop;

	return m;
}

void NSCLASS maint_buf_timeout(unsigned long data)
{
	struct maint_entry *m;

	if (timer_pending(&ack_timer))
		return;

	/* Remove the first packet */
/* 	m = (struct maint_entry *)tbl_find_detach(&maint_buf, NULL, crit_none); */
	m = (struct maint_entry *)tbl_detach_first(&maint_buf);
		
	if (!m) {
		DEBUG("Nothing in maint buf\n");
		return;
	}
	m->rexmt++;

	DEBUG("nxt_hop=%s id=%u rexmt=%d\n",
	      print_ip(m->nxt_hop), m->id, m->rexmt);

	/* Increase the number of retransmits */
	if (m->rexmt >= ConfVal(MaxMaintRexmt)) {

		DEBUG("MaxMaintRexmt reached!\n");

		if (m->ack_req_sent) {
			int n;

			lc_link_del(my_addr(), m->nxt_hop);
			
/* 		neigh_tbl_del(m->nxt_hop); */
			
			dsr_rerr_send(m->dp, m->nxt_hop);
			
			n = maint_buf_del_all_id(m->nxt_hop, m->id);

			DEBUG("Deleted %d packets from maint_buf\n", n);
		} else {
			DEBUG("No ACK REQ sent for this packet\n");
		}

		if (m->dp) {
#ifdef NS2
			if (m->dp->p)
				drop(m->dp->p, DROP_RTR_SALVAGE);
#endif
			dsr_pkt_free(m->dp);
		}

		FREE(m);
		goto out;
	}

	/* Set new Transmit time */
	gettime(&m->tx_time);
	m->expires = m->tx_time;
	timeval_add_usecs(&m->expires, m->rto);

	/* Send new ACK REQ */
	if (m->ack_req_sent)
		dsr_ack_req_send(m->nxt_hop, m->id);

	/* Add at end of buffer */
	tbl_add(&maint_buf, &m->l, crit_expires);
/* 	tbl_add_tail(&maint_buf, &m->l); */
      out:
	maint_buf_set_timeout();

	return;
}

void NSCLASS maint_buf_set_timeout(void)
{
	struct maint_entry *m;
	usecs_t rto;
	struct timeval tx_time, now, expires;

	if (tbl_empty(&maint_buf)/*  || timer_pending(&ack_timer) */)
		return;

	gettime(&now);

	DSR_WRITE_LOCK(&maint_buf.lock);
	/* Get first packet in maintenance buffer */
	m = (struct maint_entry *)__tbl_find(&maint_buf, NULL,
					     crit_ack_req_sent);

	if (!m) {
		DEBUG("No packet to set timeout for\n");
		DSR_WRITE_UNLOCK(&maint_buf.lock);
		return;
	}

	tx_time = m->tx_time;
	rto = m->rto;
	m->expires = tx_time;
	timeval_add_usecs(&m->expires, m->rto);

	expires = m->expires;

	DSR_WRITE_UNLOCK(&maint_buf.lock);

	/* Check if this packet has already expired */
	if (timeval_diff(&now, &tx_time) > (int)rto)
		maint_buf_timeout(0);
	else {
		DEBUG("ACK Timer: exp=%ld.%06ld now=%ld.%06ld\n",
		      expires.tv_sec, expires.tv_usec, now.tv_sec, now.tv_usec);
/* 		ack_timer.data = (unsigned long)m; */
		set_timer(&ack_timer, &expires);
	}
}

int NSCLASS maint_buf_add(struct dsr_pkt *dp)
{
	struct neighbor_info neigh_info;
	struct timeval now;
	int res;
	struct maint_entry *m;
/* 	char buf[2048]; */

       	if (!dp) {
		DEBUG("dp is NULL!?\n");
		return -1;
	}

	gettime(&now);

	res = neigh_tbl_query(dp->nxt_hop, &neigh_info);

	if (!res) {
		DEBUG("No neighbor info about %s\n", print_ip(dp->nxt_hop));
		return -1;
	}
	
	m = maint_entry_create(dp, neigh_info.id, neigh_info.rto);
		
	if (!m)
		return -1;
	
	/* Check if we should add an ACK REQ */
	if (dp->flags & PKT_REQUEST_ACK) {
		if ((usecs_t) timeval_diff(&now, &neigh_info.last_ack_req) > 
		    ConfValToUsecs(MaintHoldoffTime)) {
			m->ack_req_sent = 1;
			
			/* Set last_ack_req time */
			neigh_tbl_set_ack_req_time(m->nxt_hop);
		
			neigh_tbl_id_inc(m->nxt_hop);	
			
			dsr_ack_req_opt_add(dp, m->id);
		}
		
		if (tbl_add_tail(&maint_buf, &m->l) < 0) {
			DEBUG("Buffer full - not buffering!\n");
			FREE(m);
			return -1;
		}
		
		maint_buf_set_timeout();
	       
	} else {
		DEBUG("Deferring ACK REQ for %s since_last=%ld limit=%ld\n",
		      print_ip(dp->nxt_hop), timeval_diff(&now, &neigh_info.last_ack_req), ConfValToUsecs(MaintHoldoffTime));
	}
	
/* 	maint_buf_print(&maint_buf, buf); */

/* 	DEBUG("\n%s\n", buf); */

	return 1;
}

/* Remove all packets for a next hop */
int NSCLASS maint_buf_del_all(struct in_addr nxt_hop)
{
	int n;

	if (timer_pending(&ack_timer))
		del_timer_sync(&ack_timer);

	n = tbl_for_each_del(&maint_buf, &nxt_hop, crit_addr_del);

	maint_buf_set_timeout();

	return n;
}

/* Remove packets for a next hop with a specific ID */
int NSCLASS maint_buf_del_all_id(struct in_addr nxt_hop, unsigned short id)
{
	struct maint_buf_query q;
	int n;

	q.id = &id;
	q.nxt_hop = &nxt_hop;
	q.rtt = 0;

	if (timer_pending(&ack_timer))
		del_timer_sync(&ack_timer);

	/* Find the buffered packet to mark as acked */
	n = tbl_for_each_del(&maint_buf, &q, crit_addr_id_del);
	
	if (q.rtt > 0) {
		struct neighbor_info neigh_info;
		
		neigh_info.id = id;
		neigh_info.rtt = q.rtt;
		neigh_tbl_set_rto(nxt_hop, &neigh_info);
	}

	maint_buf_set_timeout();

	return n;
}

static int maint_buf_print(struct tbl *t, char *buffer)
{
	list_t *p;
	int len;
	struct timeval now;

	gettime(&now);

	len = sprintf(buffer, "# %-15s %-5s %-6s %-2s %-8s %-15s %-15s\n",
		      "NeighAddr", "Rexmt", "Id", "AR", "RTO", "TxTime", "Expires");

	DSR_READ_LOCK(&t->lock);

	list_for_each(p, &t->head) {
		struct maint_entry *e = (struct maint_entry *)p;

		if (e && e->dp)
			len +=
			    sprintf(buffer + len,
				    "  %-15s %-5d %-6u %-2d %-8u %-15s %-15s\n",
				    print_ip(e->nxt_hop), e->rexmt, e->id,
				    e->ack_req_sent, (unsigned int)e->rto, 
				    print_timeval(&e->tx_time),
				    print_timeval(&e->expires));
	}

	len += sprintf(buffer + len,
		       "\nQueue length      : %u\n"
		       "Queue max. length : %u\n", t->len, t->max_len);

	DSR_READ_UNLOCK(&t->lock);

	return len;
}

#ifdef __KERNEL__
static int
maint_buf_get_info(char *buffer, char **start, off_t offset, int length)
{
	int len;

	len = maint_buf_print(&maint_buf, buffer);

	*start = buffer + offset;
	len -= offset;

	if (len > length)
		len = length;
	else if (len < 0)
		len = 0;
	return len;
}

#endif				/* __KERNEL__ */

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
	INIT_TBL(&maint_buf, MAINT_BUF_MAX_LEN);

	init_timer(&ack_timer);

	ack_timer.function = &NSCLASS maint_buf_timeout;
	ack_timer.expires = 0;

	return 1;
}

void NSCLASS maint_buf_cleanup(void)
{
	struct maint_entry *m;
	del_timer_sync(&ack_timer);

	while ((m = (struct maint_entry *)tbl_detach_first(&maint_buf))) {
#ifdef NS2
		if (m->dp->p)
			Packet::free(m->dp->p);
#endif
		dsr_pkt_free(m->dp);
	}
#ifdef __KERNEL__
	proc_net_remove(MAINT_BUF_PROC_FS_NAME);
#endif
}
