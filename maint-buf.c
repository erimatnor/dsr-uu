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

static inline int crit_nxt_hop_del(void *pos, void *nh)
{
	struct in_addr *nxt_hop = nh; 
	struct maint_entry *p = pos;
	
	if (p->nxt_hop.s_addr == nxt_hop->s_addr
		) {
		if (p->dp)
			dsr_pkt_free(p->dp);
		/* TODO: Salvage or send RERR ? */
		return 1;
	}
	return 0;
}

static inline int crit_nxt_hop_rexmt(void *pos, void *nh)
{
	struct in_addr *nxt_hop = nh; 
	struct maint_entry *p = pos;
	
	if (p->nxt_hop.s_addr == nxt_hop->s_addr) {
		p->rexmt++;
		p->tx_time = jiffies;
		return 1;
	}
	return 0;
}

static struct maint_entry *maint_entry_create(struct dsr_pkt *dp)
{
	struct maint_entry *me;
	unsigned short id;
	
	id = neigh_tbl_get_id(dp->nxt_hop);
	
	if (!id) {
		DEBUG("Could not get request id for %s\n", print_ip(dp->nxt_hop.s_addr));
		return NULL;		
	}
	
	me = kmalloc(sizeof(struct maint_entry), GFP_ATOMIC);

	if (!me) {
		DEBUG("Could not allocate maintenance buf entry\n");
		return NULL;
	}
	
	me->nxt_hop = dp->nxt_hop;
	me->tx_time = jiffies;
	me->rexmt = 0;
	me->id = id;
	me->dp = dsr_pkt_alloc(skb_copy(dp->skb, GFP_ATOMIC));

	return me;
}

int maint_buf_add(struct dsr_pkt *dp)
{
	struct maint_entry *me;
	unsigned short id;
	unsigned long tx_time;
	
	me = maint_entry_create(dp);
	
	if (!me)
		return -1;

	id = me->id;
	tx_time = me->tx_time;
	
	if (tbl_add_tail(&maint_buf, &me->l) < 0) {
		DEBUG("Buffer full, dropping packet!\n");
		dsr_pkt_free(dp);
		kfree(me);
		return -1;
	}

	dsr_ack_req_opt_add(dp, id);
		
/* 	neigh_tbl_set_ack_req_timer(dp->nxt_hop); */

	if (!timer_pending(&ack_timer)) {
		ack_timer.expires = tx_time + 10;
		add_timer(&ack_timer);
	}
	
	return 1;
}
static void maint_buf_timeout(unsigned long data)
{
	struct maint_entry *me;
	unsigned long tx_time;
	
	me = tbl_detach_first(&maint_buf);
	
	if (!me) {
		DEBUG("Nothing in maint buf\n");
		return;
	}
	
	DEBUG("ACK REQ timeout\n");

	me->rexmt++;
	
	if (me->rexmt >= MAX_REXMT) {
		DEBUG("Rexmt max reached, send RERR\n");
		lc_link_del(my_addr(), me->nxt_hop);
		neigh_tbl_del(me->nxt_hop);
		dsr_pkt_free(me->dp);
		kfree(me);
		return;
	} 
	
	me->tx_time = jiffies;
	tbl_add_tail(&maint_buf, &me->l);
       
	read_lock_bh(&maint_buf.lock);

	me = __tbl_find(&maint_buf, NULL, crit_none);
	tx_time = me->tx_time;

	if (tx_time + 10 <= jiffies) {		
		read_unlock_bh(&maint_buf.lock);
		maint_buf_timeout(0);
		return;
	}

	read_unlock_bh(&maint_buf.lock);
	ack_timer.expires = tx_time + 10;

	add_timer(&ack_timer);
	return;
}

int maint_buf_del(struct in_addr nxt_hop, unsigned short id)
{
	struct maint_entry *me;
	unsigned long tx_time = 0;

	DEBUG("Emptying maint_buf for next hop %s\n", print_ip(nxt_hop.s_addr));
	if (timer_pending(&ack_timer))
		del_timer_sync(&ack_timer);
	
	tbl_for_each_del(&maint_buf, &nxt_hop, crit_nxt_hop_del);

	read_lock_bh(&maint_buf.lock);

	me = __tbl_find(&maint_buf, NULL, crit_none);
	
	if (me)
		tx_time = me->tx_time;
	
	read_unlock_bh(&maint_buf.lock);
	
	if (tx_time)
		add_timer(&ack_timer);
	
	return 0;
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
	tbl_flush(&maint_buf, crit_none);
}
