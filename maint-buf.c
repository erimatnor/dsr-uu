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
	me->dp = dp;

	return me;
}

int maint_buf_add(struct dsr_pkt *dp)
{
	struct maint_entry *me;

	me = maint_entry_create(dp);
	
	if (!me)
		return -1;

	tbl_add(&maint_buf, &me->l, crit_none);

	return 1;
}
