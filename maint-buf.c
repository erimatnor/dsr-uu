/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */
/* Copyright (C) Uppsala University
 *
 * This file is distributed under the terms of the GNU general Public
 * License (GPL), see the file LICENSE
 *
 * Author: Erik Nordström, <erik.nordstrom@gmail.com>
 */
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
#include "dsr-dev.h"
#include "dsr-srt.h"
#include "dsr-opt.h"
#include "timer.h"
#include "maint-buf.h"

#define MAINT_BUF_PROC_FS_NAME "maint_buf"

TBL(maint_buf, MAINT_BUF_MAX_LEN);

static DSRUUTimer ack_timer;

#endif /* NS2 */

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

#ifdef __KERNEL__
static int maint_buf_print(struct tbl *t, char *buffer);
#endif

/* Criteria function for deleting packets from buffer based on next hop and
 * id */
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
				Packet::free(m->dp->p);
#endif
			dsr_pkt_free(m->dp);
			return 1;
		}	
	}
	return 0;
}

/* Criteria function for deleting packets from buffer based on next hop */
static inline int crit_addr_del(void *pos, void *data)
{
	struct maint_entry *m = (struct maint_entry *)pos;
	struct maint_buf_query *q = (struct maint_buf_query *)data;

	if (m->nxt_hop.s_addr == q->nxt_hop->s_addr) {
		struct timeval now;
		
		gettime(&now);
		
		if (m->rexmt == 0)
			q->rtt = timeval_diff(&now, &m->tx_time);
		
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

/* Criteria function for buffered packets based on next hop */
static inline int crit_addr(void *pos, void *data)
{
	struct maint_entry *m = (struct maint_entry *)pos;
	struct in_addr *nxt_hop = (struct in_addr *)data;

	if (m->nxt_hop.s_addr == nxt_hop->s_addr)
		return 1;

	return 0;
}

/* Criteria function for buffered packets based on expire time */
static inline int crit_expires(void *pos, void *data)
{
	struct maint_entry *m = (struct maint_entry *)pos;
	struct maint_entry *m_new = (struct maint_entry *)data;

	if (timeval_diff(&m->expires, &m_new->expires) > 0)
		return 1;
	return 0;

}

/* Criteria function for buffered packets based on sent ACK REQ */
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

	m = (struct maint_entry *)kmalloc(sizeof(struct maint_entry),
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
	if (!m->dp) {
		kfree(m);
		return NULL;
	}
	m->dp->nxt_hop = dp->nxt_hop;

	return m;
}

int NSCLASS maint_buf_salvage(struct dsr_pkt *dp)
{
	struct dsr_srt *alt_srt, *old_srt, *srt;
	int old_srt_opt_len, new_srt_opt_len, sleft, salv;

	if (!dp)
		return -1;
	
	if (dp->srt) {
		DEBUG("old internal source route exists\n");
		kfree(dp->srt);
	}

	alt_srt = dsr_rtc_find(my_addr(), dp->dst);
	
	if (!alt_srt) {
		DEBUG("No alt. source route - cannot salvage packet\n");
		return -1;
	}
	
	if (!dp->srt_opt) {
		DEBUG("No old source route\n");
		kfree(alt_srt);
		return -1;
	}

	old_srt = dsr_srt_new(dp->src, dp->dst, dp->srt_opt->length - 2, 
			      (char *)dp->srt_opt->addrs);

	if (!old_srt) {
		kfree(alt_srt);
		return -1;
	}

	DEBUG("opt_len old srt: %s\n", print_srt(old_srt));

	/* Salvaging as described in the draft does not really make that much
	 * sense to me... For example, why should the new source route be
	 * <orig_src> -> <this_node> -> < ... > -> <dst> ?. Then it looks like
	 * this node has one hop connectivity with the src? Further, the draft
	 * does not mention anything about checking for loops or "going back"
	 * the same way the packet arrived, i.e, <orig_src> -> <this_node> ->
	 * <orig_src> -> <...> -> <dst>. */

	/* Rip out the source route to me */

	if (old_srt->addrs[0].s_addr == dp->nxt_hop.s_addr) {
		srt = alt_srt;
		sleft = (srt->laddrs) / 4;
	} else {
		struct dsr_srt *srt_to_me;

		srt_to_me = dsr_srt_new_split(old_srt, my_addr());
		
		if (!srt_to_me) { 
			kfree(alt_srt);
			kfree(old_srt);
			return -1;
		}
		srt = dsr_srt_concatenate(srt_to_me, alt_srt);
		
		sleft = (srt->laddrs) / 4 - (srt_to_me->laddrs / 4) - 1;
		
		DEBUG("old_srt: %s\n", print_srt(old_srt));
		DEBUG("alt_srt: %s\n", print_srt(alt_srt));
		
		kfree(alt_srt);
		kfree(srt_to_me);
	}

	kfree(old_srt);
		
	if (!srt)
		return -1;
	
	DEBUG("Salvage packet sleft=%d srt: %s\n", sleft, print_srt(srt));

	if (dsr_srt_check_duplicate(srt)) {
		DEBUG("Duplicate address in new source route, aborting salvage\n");
		kfree(srt);
		return -1;
	}
	
	/* TODO: Check unidirectional MAC tx support and potentially discard
	 * RREP option... */

	/* TODO: Check/set First and Last hop external bits */

	old_srt_opt_len = dp->srt_opt->length + 2;
	new_srt_opt_len = DSR_SRT_OPT_LEN(srt);
	salv = dp->srt_opt->salv;

	DEBUG("Salvage - source route length new=%d old=%d\n",
	      new_srt_opt_len, old_srt_opt_len);

	if (old_srt_opt_len == new_srt_opt_len) {
		DEBUG("new and old srt of same length\n");
		
		dp->srt_opt = dsr_srt_opt_add((char *)dp->srt_opt, 
					      new_srt_opt_len, 0, 
					      salv + 1, srt);
	} else {
		int old_opt_len, new_opt_len;
		char *old_opt = dp->dh.raw;
		char *old_srt_opt = (char *)dp->srt_opt;
		char *buf;
		
		DEBUG("Creating new options header\n");
		
		old_opt_len = dsr_pkt_opts_len(dp);
		new_opt_len = old_opt_len - old_srt_opt_len + new_srt_opt_len;
		
		DEBUG("opt_len old=%d new=%d srt: %s\n", 
		      old_opt_len, new_opt_len, print_srt(srt));

		/* Allocate new options space */
		buf = dsr_pkt_alloc_opts(dp, new_opt_len);
		
		if (!buf) {
			kfree(srt);
			return -1;
		}
				
		/* Copy everything up to old source route option */
		memcpy(buf, old_opt, old_srt_opt - old_opt);
		
		buf += (old_srt_opt - old_opt);
		
		/* Add new source route option */
		dp->srt_opt = dsr_srt_opt_add(buf, new_srt_opt_len, 0, 
					      salv + 1, srt);

		buf += new_srt_opt_len;
		
		/* Copy everything from after old source route option and to the
		 * end */
		memcpy(buf, old_srt_opt + old_srt_opt_len, 
		       old_opt + old_opt_len - 
		       (old_srt_opt + old_srt_opt_len));

		kfree(old_opt);	
		
		/* Set new length in DSR header */
		dp->dh.opth->p_len = htons(new_opt_len - DSR_OPT_HDR_LEN);
	}

	/* We got this packet directly from the previous hop */
	dp->srt_opt->sleft = sleft;
	
	dp->nxt_hop = dsr_srt_next_hop(srt, dp->srt_opt->sleft);

	DEBUG("Next hop=%s p_len=%d\n", print_ip(dp->nxt_hop), ntohs(dp->dh.opth->p_len));

	dp->srt = srt;

	XMIT(dp);
	
	return 0;
}

void NSCLASS maint_buf_timeout(unsigned long data)
{
        write_lock_bh(&maint_buf.lock);
	_maint_buf_timeout(data);
	write_unlock_bh(&maint_buf.lock);
}

void NSCLASS _maint_buf_timeout(unsigned long data)
{
	struct maint_entry *m, *m2;

	if (timer_pending(&ack_timer))
		return;

	/* Get the first packet */
	m = (struct maint_entry *)__tbl_detach_first(&maint_buf);
		
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
			int n = 0;

			lc_link_del(my_addr(), m->nxt_hop);
#ifdef NS2
			/* Remove packets from interface queue */
			Packet *qp;
			
			while ((qp = ifq_->prq_get_nexthop((nsaddr_t)m->nxt_hop.s_addr))) {
				Packet::free(qp);
			}
#endif			
			dsr_rerr_send(m->dp, m->nxt_hop);

			/* Salvage timed out packet */
			if (maint_buf_salvage(m->dp) < 0) {
#ifdef NS2
				if (m->dp->p) 
					drop(m->dp->p, DROP_RTR_SALVAGE);
#endif
				dsr_pkt_free(m->dp);
			} else
				n++;
			/* Salvage other packets in maintenance buffer with the
			 * same next hop */
			while ((m2 = (struct maint_entry *)__tbl_find_detach(&maint_buf, &m->nxt_hop, crit_addr))) {
				
				if (maint_buf_salvage(m2->dp) < 0) {
#ifdef NS2
					if (m2->dp->p)
						drop(m2->dp->p, DROP_RTR_SALVAGE);
#endif
					dsr_pkt_free(m2->dp);
				}
				kfree(m2);
				n++;
			}
			DEBUG("Salvaged %d packets from maint_buf\n", n);
		} else {
			DEBUG("No ACK REQ sent for this packet\n");

			if (m->dp) {
#ifdef NS2
				if (m->dp->p)
					drop(m->dp->p, DROP_RTR_SALVAGE);
#endif
				dsr_pkt_free(m->dp);
			}			
		}		
		
		kfree(m);
		goto out;
	}

	/* Set new Transmit time */
	gettime(&m->tx_time);
	m->expires = m->tx_time;
	timeval_add_usecs(&m->expires, m->rto);

	/* Send new ACK REQ for this buffered packet */
	if (m->ack_req_sent)
		dsr_ack_req_send(m->nxt_hop, m->id);

	/* Add to maintenence buffer again */
	__tbl_add(&maint_buf, &m->l, crit_expires);
      out:
	_maint_buf_set_timeout();

	return;
}

void NSCLASS maint_buf_set_timeout(void)
{
	write_lock_bh(&maint_buf.lock);
	
	_maint_buf_set_timeout();
	
        write_unlock_bh(&maint_buf.lock);
}

void NSCLASS _maint_buf_set_timeout(void)
{
	struct maint_entry *m;
	usecs_t rto;
	struct timeval tx_time, now, expires;

	if (tbl_empty(&maint_buf))
		return;

	gettime(&now);

	/* Get first packet in maintenance buffer */
	m = (struct maint_entry *)__tbl_find(&maint_buf, NULL,
					     crit_ack_req_sent);

	if (!m) {
		DEBUG("No packet to set timeout for\n");
		return;
	}

	tx_time = m->tx_time;
	rto = m->rto;
	m->expires = tx_time;
	timeval_add_usecs(&m->expires, m->rto);

	expires = m->expires;

	/* Check if this packet has already expired */
	if (timeval_diff(&now, &tx_time) > (int)rto)
		_maint_buf_timeout(0);
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
		
		write_lock_bh(&maint_buf.lock);

		if (__tbl_add_tail(&maint_buf, &m->l) < 0) {
			DEBUG("Buffer full - not buffering!\n");
			dsr_pkt_free(m->dp);
			kfree(m);
                        write_unlock_bh(&maint_buf.lock);
			return -1;
		}
		
		_maint_buf_set_timeout();

		write_unlock_bh(&maint_buf.lock);
	       
	} else {
		DEBUG("Delaying ACK REQ for %s since_last=%ld limit=%ld\n",
		      print_ip(dp->nxt_hop), 
		      timeval_diff(&now, &neigh_info.last_ack_req), 
		      ConfValToUsecs(MaintHoldoffTime));
	}
	
	return 1;
}

/* Remove all packets for a next hop */
int NSCLASS maint_buf_del_all(struct in_addr nxt_hop)
{
	struct maint_buf_query q;
	int n;

	q.id = NULL;
	q.nxt_hop = &nxt_hop;
	q.rtt = 0;

	write_lock_bh(&maint_buf.lock);
	
	if (timer_pending(&ack_timer))
		del_timer_sync(&ack_timer);

	n = __tbl_for_each_del(&maint_buf, &q, crit_addr_del);

	_maint_buf_set_timeout();

	write_unlock_bh(&maint_buf.lock);
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

	write_lock_bh(&maint_buf.lock);

	if (timer_pending(&ack_timer))
		del_timer_sync(&ack_timer);

	/* Find the buffered packet to mark as acked */
	n = __tbl_for_each_del(&maint_buf, &q, crit_addr_id_del);
	
	if (q.rtt > 0) {
		struct neighbor_info neigh_info;
		
		neigh_info.id = id;
		neigh_info.rtt = q.rtt;
		neigh_tbl_set_rto(nxt_hop, &neigh_info);
	}

	_maint_buf_set_timeout();

	write_unlock_bh(&maint_buf.lock);

	return n;
}
int NSCLASS maint_buf_del_addr(struct in_addr nxt_hop)
{
	struct maint_buf_query q;
	int n;

	q.id = NULL;
	q.nxt_hop = &nxt_hop;
	q.rtt = 0;

        write_lock_bh(&maint_buf.lock);

	if (timer_pending(&ack_timer))
		del_timer_sync(&ack_timer);

	/* Find the buffered packet to mark as acked */
	n = __tbl_for_each_del(&maint_buf, &q, crit_addr_del);
	
	if (q.rtt > 0) {
		struct neighbor_info neigh_info;
		
		neigh_info.id = 0;
		neigh_info.rtt = q.rtt;
		neigh_tbl_set_rto(nxt_hop, &neigh_info);
	}

	_maint_buf_set_timeout();

        write_unlock_bh(&maint_buf.lock);

	return n;
}

#ifdef __KERNEL__
static int maint_buf_print(struct tbl *t, char *buffer)
{
	list_t *p;
	int len;
	struct timeval now;

	gettime(&now);

	len = sprintf(buffer, "# %-15s %-5s %-6s %-2s %-8s %-15s %-15s\n",
		      "NeighAddr", "Rexmt", "Id", "AR", "RTO", "TxTime", "Expires");

	read_lock_bh(&t->lock);

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

	read_unlock_bh(&t->lock);

	return len;
}

static int
maint_buf_get_info(char *buffer, char **start, off_t offset, int length, int *eof, void *data)
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
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,6,23))
#define proc_net init_net.proc_net
#endif
	proc = create_proc_read_entry(MAINT_BUF_PROC_FS_NAME, 0, proc_net, maint_buf_get_info, NULL);

	if (!proc) {
		printk(KERN_ERR "maint_buf: failed to create proc entry\n");
		return -1;
	}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,30))
	proc->owner = THIS_MODULE;
#endif
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

	write_lock_bh(&maint_buf.lock);

	del_timer_sync(&ack_timer);

	while ((m = (struct maint_entry *)__tbl_detach_first(&maint_buf))) {
#ifdef NS2
		if (m->dp->p)
			Packet::free(m->dp->p);
#endif
		dsr_pkt_free(m->dp);

		kfree(m);
	}

	write_unlock_bh(&maint_buf.lock);

#ifdef __KERNEL__
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,24))
	proc_net_remove(MAINT_BUF_PROC_FS_NAME);
#else
	proc_net_remove(&init_net, MAINT_BUF_PROC_FS_NAME);
#endif
#endif
}
