#include "dsr.h"
#include "debug.h"
#include "tbl.h"

#define MAINT_BUF_MAX_LEN 100

TBL(maint_buf, MAINT_BUF_MAX_LEN);

struct maint_entry {
	struct list_head l;
	struct in_addr nxt_hop;
	unsigned int rexmt;
	unsigned short ack_id;
	unsigned long tx_time;
	struct dsr_pkt *dp;
};

static inline int crit_nxt_hop_del(void *pos, void *nh)
{
	struct in_addr *nxt_hop = nh; 
	struct maint_entry *p = pos;
	
	if (p->nxt_hop.s_addr == nxt_hop->s_addr) {
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
		/* TODO: Salvage or send RERR ? */
		return 1;
	}
	return 0;
}

static struct maint_entry *maint_entry_create(struct dsr_pkt *dp)
{
	struct maint_entry *me;

	me = kmalloc(sizeof(struct maint_entry), GFP_ATOMIC);

	if (!me) {
		DEBUG("Could not allocate maintenance buf entry\n");
		return NULL;
	}
		
	me->nxt_hop = dp->nxt_hop;
	me->tx_time = jiffies;
	me->rexmt = 0;
	me->dp = dsr_pkt_alloc(skb_copy(dp->skb, GFP_ATOMIC), 0);

	return me;
}

int maint_buf_add(struct dsr_pkt *dp)
{
	struct maint_entry *me;

	me = maint_entry_create(dp);
	
	if (!me)
		return -1;

	if (tbl_add(&maint_buf, &me->l, crit_none) < 0) {
		DEBUG("Buffer full!n");
		dsr_pkt_free(dp);
		kfree(me);
		return -1;
	}
	return 1;
}

int maint_buf_del(struct in_addr nxt_hop)
{
	return tbl_for_each_del(&maint_buf, &nxt_hop, crit_nxt_hop_del);
}

void maint_buf_rexmt(struct in_addr nxt_hop)
{
	tbl_do_for_each(&maint_buf, &nxt_hop, crit_nxt_hop_rexmt);

}
