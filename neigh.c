#ifdef __KERNEL__
#include <linux/proc_fs.h>
#include <linux/timer.h>
#endif

#include "tbl.h"
#include "neigh.h"
#include "debug.h"

#ifdef NS2
#include "ns-agent.h"
#else
#define NEIGH_TBL_MAX_LEN 50


static TBL(neigh_tbl, NEIGH_TBL_MAX_LEN);

#define NEIGH_TBL_PROC_NAME "dsr_neigh_tbl"

static DSRTimer garbage_timer;

#endif /* NS2 */

#define DSRTV_SRTTBASE 0
#define DSRTV_MIN 2
#define DSRTV_REXMTMAX
#define TICK 2

#define NEIGH_TBL_GARBAGE_COLLECT_TIMEOUT 3000 
#define NEIGH_TBL_TIMEOUT 2000
#define RTT_DEF SECONDS(1) /* sec */

struct neighbor {
	list_t l;
	struct in_addr addr;
	struct sockaddr hw_addr;
	unsigned short id;
	int rtt, srtt, rttvar, jitter; /* RTT in usec */
};

struct hw_query {
	struct in_addr *a;
	struct sockaddr *hw;
};
struct id_query {
	struct in_addr *a;
	unsigned short *id;
};
struct rtt_query {
	struct in_addr *a;
	int *srtt;
};
		
static inline int crit_addr(void *pos, void *addr)
{
	struct in_addr *a = (struct in_addr *)addr; 
	struct neighbor *e = (struct neighbor *)pos;
	
	if (e->addr.s_addr == a->s_addr)
		return 1;
	return 0;
}
static inline int crit_addr_get_hwaddr(void *pos, void *data)
{	
	struct neighbor *e = (struct neighbor *)pos;
	struct hw_query *q = (struct hw_query *)data;

	if (e->addr.s_addr == q->a->s_addr) {
		memcpy(q->hw, &e->hw_addr, sizeof(struct sockaddr));
		return 1;
	}
	return 0;
}
static inline int crit_addr_get_id(void *pos, void *data)
{
	struct neighbor *e = (struct neighbor *)pos;
	struct id_query *q = (struct id_query *)data;

	if (e->addr.s_addr == q->a->s_addr) {
		/* Increase id so it is always unique */
		*(q->id) = ++e->id;
		return 1;
	}
	return 0;
}
static inline int crit_addr_get_rtt(void *pos, void *data)
{
	struct neighbor *e = (struct neighbor *)pos;
	struct rtt_query *q = (struct rtt_query *)data;

	if (e->addr.s_addr == q->a->s_addr) {
		*(q->srtt) = e->srtt;
		return 1;
	}
	return 0;
}


static inline int crit_garbage(void *pos, void *foo)
{
	

	return 0;
}

void NSCLASS neigh_tbl_garbage_timeout(unsigned long data)
{
	tbl_for_each_del(&neigh_tbl, NULL, crit_garbage);
	
	DSR_READ_LOCK(&neigh_tbl.lock);
	
	if (!TBL_EMPTY(&neigh_tbl)) {
	/* 	garbage_timer.expires = jiffies +  */
/* 			MSECS_TO_JIFFIES(NEIGH_TBL_GARBAGE_COLLECT_TIMEOUT); */
	/* 	add_timer(&garbage_timer);	 */
	}

	DSR_READ_UNLOCK(&neigh_tbl.lock);
}



static struct neighbor *neigh_tbl_create(struct in_addr addr, 
					 struct sockaddr *hw_addr,
					 unsigned short id)
{
	struct neighbor *neigh;
	
	neigh = (struct neighbor *)MALLOC(sizeof(struct neighbor), GFP_ATOMIC);
	
	if (!neigh)
		return NULL;
	
	memset(neigh, 0, sizeof(struct neighbor));

	neigh->id = id;
	neigh->addr = addr;
	neigh->srtt = DSRTV_SRTTBASE;
	neigh->rttvar = 0;
	neigh->rtt = RTT_DEF;
	memcpy(&neigh->hw_addr, hw_addr, sizeof(struct sockaddr));
	
/* 	garbage_timer.expires = jiffies + NEIGH_TBL_GARBAGE_COLLECT_TIMEOUT / 1000*HZ; */
/* 	add_timer(&garbage_timer); */

	return neigh;
}

int NSCLASS neigh_tbl_add(struct in_addr neigh_addr, struct sockaddr *hw_addr)
{
	struct neighbor *neigh;
	
	if (in_tbl(&neigh_tbl, &neigh_addr, crit_addr))
		return 0;

	neigh = neigh_tbl_create(neigh_addr, hw_addr, 1);

	if (!neigh) {
		DEBUG("Could not create new neighbor entry\n");
		return -1;
	}
	
	tbl_add(&neigh_tbl, &neigh->l, crit_none);

	return 1;
}

int NSCLASS neigh_tbl_del(struct in_addr neigh_addr)
{
	return tbl_for_each_del(&neigh_tbl, &neigh_addr, crit_addr);
}

int NSCLASS neigh_tbl_rtt_update(struct in_addr nxt_hop, int nticks)
{
	struct neighbor *neigh;
	int delta;
	
	DSR_WRITE_LOCK(&neigh_tbl.lock);

	neigh = (struct neighbor *)__tbl_find(&neigh_tbl, &nxt_hop, crit_addr);
	
	if (!neigh) {
		DSR_WRITE_UNLOCK(&neigh_tbl.lock);
		return -1;
	}
	
	/* Use TCP RTO estimation */
	delta = nticks - neigh->srtt;
	
       	neigh->srtt = neigh->srtt + (delta >> 3);

	DSR_WRITE_UNLOCK(&neigh_tbl.lock);

	return 0;
}

long NSCLASS neigh_tbl_get_rto(struct in_addr nxt_hop)
{
	struct neighbor *neigh;
	int rtt, srtt, rttvar;

	DSR_READ_LOCK(&neigh_tbl.lock);

	neigh = (struct neighbor *)__tbl_find(&neigh_tbl, &nxt_hop, crit_addr);
	
	if (!neigh) {
		DSR_READ_UNLOCK(&neigh_tbl.lock);
		return -1;
	}
	srtt = neigh->srtt;
	rttvar = neigh->rttvar;
	rtt = neigh->rtt;
	
	DSR_READ_UNLOCK(&neigh_tbl.lock);

	return rtt;
}

int NSCLASS neigh_tbl_get_hwaddr(struct in_addr neigh_addr, struct sockaddr *hw_addr)
{
	struct hw_query q;
	
	q.a = &neigh_addr;
	q.hw = hw_addr;
	
	return in_tbl(&neigh_tbl, &q, crit_addr_get_hwaddr);
}

unsigned short NSCLASS neigh_tbl_get_id(struct in_addr neigh_addr)
{
	unsigned short id = 0;
	struct id_query q;
	
	q.a = &neigh_addr;
	q.id = &id;
	
	in_tbl(&neigh_tbl, &q, crit_addr_get_id);
	
	return id;
}

int NSCLASS neigh_tbl_get_rtt(struct in_addr neigh_addr)
{
	int srtt;
	struct rtt_query q;
	
	q.a = &neigh_addr;
	q.srtt = &srtt;
	
	in_tbl(&neigh_tbl, &q, crit_addr_get_rtt);
	
	return srtt;
}

#ifdef __KERNEL__
static int neigh_tbl_print(char *buf)
{
	list_t *pos;
	int len = 0;
    
	DSR_READ_LOCK(&neigh_tbl.lock);
    
	len += sprintf(buf, "# %-15s %-17s %-3s %-6s\n", "Addr", "HwAddr", "RTT", "Id" /*, "AckRxTime","AckTxTime" */);

	list_for_each(pos, &neigh_tbl.head) {
		struct neighbor *neigh =(struct neighbor *)pos;
		
		len += sprintf(buf+len, "  %-15s %-17s %-3d %-6u\n", 
			       print_ip(neigh->addr), 
			       print_eth(neigh->hw_addr.sa_data),
			       neigh->rtt, neigh->id);
	}
    
	DSR_READ_UNLOCK(&neigh_tbl.lock);
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
	
	garbage_timer.function = &neigh_tbl_garbage_timeout;

	proc_net_create(NEIGH_TBL_PROC_NAME, 0, neigh_tbl_proc_info);
	return 0;
}


void __exit neigh_tbl_cleanup(void)
{
	tbl_flush(&neigh_tbl, crit_none);
	proc_net_remove(NEIGH_TBL_PROC_NAME);
}

#endif /* __KERNEL__ */
