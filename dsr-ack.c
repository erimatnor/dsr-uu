#include "tbl.h"
#include "dsr.h"
#include "debug.h"
#include "dsr-opt.h"
#include "dsr-ack.h"

unsigned short ack_id = 0;

#define ACK_TBL_MAX_LEN 24
#define MAX_AREQ_REXMTS 2

static TBL(ack_tbl, ACK_TBL_MAX_LEN);

struct ack_entry {
	struct list_head l;
	struct in_addr neigh;
	unsigned short id;
	struct timer_list timer;
	int rtt, rexmt;
};

int dsr_send_ack_req(struct in_addr, unsigned short id);

static inline int crit_addr(void *pos, void *addr)
{
	struct in_addr *a = addr; 
	struct ack_entry *p = pos;
	
	if (p->neigh.s_addr == a->s_addr)
		return 1;
	return 0;
}

static inline int crit_ack(void *pos, void *ack)
{
	struct dsr_ack_opt *a = ack; 
	struct ack_entry *p = pos;
	struct in_addr myaddr = my_addr();

	if (p->neigh.s_addr == a->src && 
	    p->id == a->id && 
	    a->dst == myaddr.s_addr)
		return 1;
	return 0;
}

static void ack_tbl_timeout(unsigned long data)
{
	struct ack_entry *ae;
	
	write_lock_bh(&ack_tbl.lock);
	
	ae = (struct ack_entry *)data;
	
	if (!ae)
		goto out;
	
	if (ae->rexmt++ > MAX_AREQ_REXMTS) {
		__tbl_del(&ack_tbl, &ae->l);
		goto out;
	}
 out:
	write_unlock_bh(&ack_tbl.lock);
}

static void ack_tbl_set_timer(struct ack_entry *ae)
{
	ae->timer.function = ack_tbl_timeout;
	ae->timer.expires = jiffies + (ae->rtt * 2 * HZ / 1000 );
	ae->timer.data = (unsigned long)ae; 
	add_timer(&ae->timer);
}

int ack_tbl_add_unacked(struct in_addr neigh, int rtt) 
{
	struct ack_entry *ae;
	
	if (in_tbl(&ack_tbl, &neigh, crit_addr)) {
		DEBUG("Already waiting for an ACK from %s\n", 
		      print_ip(neigh.s_addr));
		return 0;
	}
	
	ae = kmalloc(sizeof(ae), GFP_ATOMIC);
	
	if (!ae) 
		return -1;

	ae->neigh = neigh;
	ae->id = ack_id++;
	ae->rtt = rtt;

	ack_tbl_set_timer(ae);

	tbl_add(&ack_tbl, &ae->l, crit_none);

	return 1;	
}

int dsr_ack_req_opt_add(unsigned long id, char *buf, int len)
{
	struct dsr_ack_req_opt *areq = (struct dsr_ack_req_opt *)buf;
       
	if (len < DSR_OPT_AREQ + DSR_OPT_HDR_LEN)
		return -1;
	
	areq->type = DSR_OPT_AREQ;
	areq->length = DSR_ACK_OPT_LEN;
	areq->id = id;
	
	return 1;
}

int dsr_send_ack_req(struct in_addr neigh, unsigned short id)
{
	
	
	return 1;
}
int dsr_ack_opt_recv(struct dsr_ack_opt *ack)
{
	struct ack_entry *ae;

	if (!ack)
		return -1;

	ae = tbl_find_detach(&ack_tbl, ack, crit_ack);

	if (!ae) {
		
		return -1;
	}
	return 1;
}
