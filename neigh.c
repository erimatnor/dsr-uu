#include <linux/proc_fs.h>
#include <linux/timer.h>

#include "tbl.h"
#include "neigh.h"
#include "debug.h"

#define NEIGH_TBL_MAX_LEN 50
#define MAX_AREQ_TX 2

static TBL(neigh_tbl, NEIGH_TBL_MAX_LEN);

#define NEIGH_TBL_GARBAGE_COLLECT_TIMEOUT 3000 
#define NEIGH_TBL_TIMEOUT 2000
#define RTT_DEF 2000 /* usec */

#define NEIGH_TBL_PROC_NAME "dsr_neigh_tbl"

struct neighbor {
	struct list_head l;
	struct in_addr addr;
	struct sockaddr hw_addr;
	unsigned short id;
	int srtt, rttvar, jitter; /* RTT in usec */
};

static struct timer_list garbage_timer;

static inline int crit_addr(void *pos, void *addr)
{
	struct in_addr *a = addr; 
	struct neighbor *e = pos;
	
	if (e->addr.s_addr == a->s_addr)
		return 1;
	return 0;
}
static inline int crit_addr_get_hwaddr(void *pos, void *data)
{
	struct {
		struct in_addr *a;
		struct sockaddr *hw;
	} *d;
	
	struct neighbor *e = pos;
	d = data;

	if (e->addr.s_addr == d->a->s_addr) {
		memcpy(d->hw, &e->hw_addr, sizeof(struct sockaddr));
		return 1;
	}
	return 0;
}
static inline int crit_addr_get_id(void *pos, void *data)
{
	struct {
		struct in_addr *a;
		unsigned short *id;
	} *d;
	
	struct neighbor *e = pos;
	d = data;

	if (e->addr.s_addr == d->a->s_addr) {
		/* Increase id so it is always unique */
		*(d->id) = ++e->id;
		return 1;
	}
	return 0;
}
static inline int crit_addr_get_rtt(void *pos, void *data)
{
	struct {
		struct in_addr *a;
		int *srtt;
	} *d;
	
	struct neighbor *e = pos;
	d = data;

	if (e->addr.s_addr == d->a->s_addr) {
		*(d->srtt) = e->srtt;
		return 1;
	}
	return 0;
}


static inline int crit_garbage(void *pos, void *foo)
{
	

	return 0;
}

static void neigh_tbl_garbage_timeout(unsigned long data)
{
	tbl_for_each_del(&neigh_tbl, NULL, crit_garbage);
	
	read_lock_bh(&neigh_tbl.lock);
	
	if (!TBL_EMPTY(&neigh_tbl)) {
		garbage_timer.expires = jiffies + 
			MSECS_TO_JIFFIES(NEIGH_TBL_GARBAGE_COLLECT_TIMEOUT);
		add_timer(&garbage_timer);	
	}

	read_unlock_bh(&neigh_tbl.lock);
}



static struct neighbor *neigh_tbl_create(struct in_addr addr, 
					 struct sockaddr *hw_addr,
					 unsigned short id)
{
	struct neighbor *neigh;
	
	neigh = kmalloc(sizeof(struct neighbor), GFP_ATOMIC);
	
	if (!neigh)
		return NULL;
	
	memset(neigh, 0, sizeof(struct neighbor));

	neigh->id = id;
	neigh->addr = addr;
	neigh->srtt = DSRTV_SRTTBASE;
	neigh->rttvar = 
	memcpy(&neigh->hw_addr, hw_addr, sizeof(struct sockaddr));
	
/* 	garbage_timer.expires = jiffies + NEIGH_TBL_GARBAGE_COLLECT_TIMEOUT / 1000*HZ; */
/* 	add_timer(&garbage_timer); */

	return neigh;
}

int neigh_tbl_add(struct in_addr neigh_addr, struct sockaddr *hw_addr)
{
	struct neighbor *neigh;
	
	if (in_tbl(&neigh_tbl, &neigh_addr, crit_addr))
		return 0;

	neigh = neigh_tbl_create(neigh_addr, hw_addr, 0);

	if (!neigh) {
		DEBUG("Could not create new neighbor entry\n");
		return -1;
	}
	
	tbl_add(&neigh_tbl, &neigh->l, crit_none);

	return 1;
}

int neigh_tbl_del(struct in_addr neigh_addr)
{
	return tbl_for_each_del(&neigh_tbl, &neigh_addr, crit_addr);
}

#define DSRTV_SRTTBASE 0
#define DSRTV_MIN 2
#define DSRTV_REXMTMAX
#define TICK 2
#define RTT_DEF 3 /* secs */
int neigh_tbl_rtt_update(struct in_addr nxt_hop, int nticks)
{
	struct neighbor *neigh;
	int delta;
	
	write_lock_bh(&neigh_tbl.lock);

	neigh = __tbl_find(&neigh_tbl, &nxt_hop, crit_addr);
	
	if (!neigh) {
		write_unlock_bh(&neigh_tbl.lock);
		return -1;
	}
	
	/* Use TCP RTO estimation */
	delta = nticks - neigh->srtt;
	
       	neigh->srtt = neigh->srtt + (delta >> 3);
	neigh->rttvar = neigh->rttvar + 

	write_unlock_bh(&neigh_tbl.lock);

	return 0;
}

int neigh_tbl_get_rto(struct in_addr nxt_hop)
{
	int srtt, rttvar;
	
	read_lock_bh(&neigh_tbl.lock);

	neigh = __tbl_find(&neigh_tbl, &nxt_hop, crit_addr);
	
	if (!neigh) {
		read_unlock_bh(&neigh_tbl.lock);
		return -1;
	}
	srtt = neigh->srtt;
	rttvar neigh->rttvar;

	read_unlock_bh(&neigh_tbl.lock);

	rttvar
	

}

int neigh_tbl_get_hwaddr(struct in_addr neigh_addr, struct sockaddr *hw_addr)
{
	struct {
		struct in_addr *a;
		struct sockaddr *hw;
	} data;
	
	data.a = &neigh_addr;
	data.hw = hw_addr;
	
	return in_tbl(&neigh_tbl, &data, crit_addr_get_hwaddr);
}

unsigned short neigh_tbl_get_id(struct in_addr neigh_addr)
{
	unsigned short id = 0;

	struct {
		struct in_addr *a;
		unsigned short *id;
	} data;
	
	data.a = &neigh_addr;
	data.id = &id;
	
	in_tbl(&neigh_tbl, &data, crit_addr_get_id);
	
	return id;
}

int neigh_tbl_get_rtt(struct in_addr neigh_addr)
{
	int srtt;

	struct {
		struct in_addr *a;
		int *srtt;
	} data;
	
	data.a = &neigh_addr;
	data.srtt = &srtt;
	
	in_tbl(&neigh_tbl, &data, crit_addr_get_rtt);
	
	return srtt;
}

static int neigh_tbl_print(char *buf)
{
	struct list_head *pos;
	int len = 0;
    
	read_lock_bh(&neigh_tbl.lock);
    
	len += sprintf(buf, "# %-15s %-17s %-3s %-6s\n", "Addr", "HwAddr", "RTT", "Id" /*, "AckRxTime","AckTxTime" */);

	list_for_each(pos, &neigh_tbl.head) {
		struct neighbor *neigh =(struct neighbor *)pos;
		
		len += sprintf(buf+len, "  %-15s %-17s %-3d %-6u\n", 
			       print_ip(neigh->addr.s_addr), 
			       print_eth(neigh->hw_addr.sa_data),
			       neigh->srtt, neigh->id);
	}
    
	read_unlock_bh(&neigh_tbl.lock);
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
	
	garbage_timer.function = neigh_tbl_garbage_timeout;

	proc_net_create(NEIGH_TBL_PROC_NAME, 0, neigh_tbl_proc_info);
	return 0;
}


void __exit neigh_tbl_cleanup(void)
{
	tbl_flush(&neigh_tbl, crit_none);
	proc_net_remove(NEIGH_TBL_PROC_NAME);
}
