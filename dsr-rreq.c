#include <net/ip.h>
#include <linux/proc_fs.h>

#include "debug.h"
#include "dsr.h"
#include "tbl.h"
#include "kdsr.h"
#include "dsr-rrep.h"
#include "dsr-rreq.h"
#include "dsr-opt.h"
#include "dsr-rtc.h"
#include "dsr-dev.h"
#include "p-queue.h"

#define RREQ_TBL_MAX_LEN 512 /* Should be enough */
#define RREQ_TBL_PROC_NAME "dsr_rreq_tbl"

static TBL(rreq_tbl, RREQ_TBL_MAX_LEN);

struct rreq_tbl_entry {
	struct list_head l;
	struct in_addr addr;
	int ttl;
	unsigned long sent_time;
};

static unsigned int rreq_seqno = 1;


static inline int crit_address(void *rreq_entry, void *addr)
{
	struct in_addr *a = (struct in_addr *)addr; 
	struct rreq_tbl_entry *e = (struct rreq_tbl_entry *)rreq_entry;
	
	if (e->addr.s_addr == a->s_addr)
		return 1;

	return 0;
}

struct rreq_tbl_entry *rreq_tbl_add(struct in_addr addr, int ttl, unsigned long time)
{
	struct rreq_tbl_entry *e;
	
	e = kmalloc(sizeof(struct rreq_tbl_entry), GFP_ATOMIC);
	
	if (!e)
		return NULL;
	
	e->addr = addr;
	e->ttl = ttl;
	e->sent_time = time;

	if (tbl_add(&rreq_tbl, e, crit_none)) {
		kfree(e);
		return NULL;
	}
	return e;
}

static int rreq_tbl_print(char *buf)
{
	struct list_head *pos;
	int len = 0;
    
	read_lock_bh(&rreq_tbl.lock);
    
	len += sprintf(buf, "# %-15s %-3s Sent      entries=%d max_entries=%d tbl->l=%lu entry->l->next=%lu entry->l->prev=%lu\n", "Addr", "TTL", rreq_tbl.len, rreq_tbl.max_len, (unsigned long)&rreq_tbl.head, (unsigned long)rreq_tbl.head.next, (unsigned long)rreq_tbl.head.prev);

	list_for_each(pos, &rreq_tbl.head) {
		struct rreq_tbl_entry *e = (struct rreq_tbl_entry *)pos;
		
		len += sprintf(buf+len, "  %-15s %-3u %lu\n", print_ip(e->addr.s_addr), e->ttl, (e->sent_time - jiffies) * 1000 / HZ);
	}
    
	read_unlock_bh(&rreq_tbl.lock);
	return len;

}

static int rreq_tbl_proc_info(char *buffer, char **start, off_t offset, int length)
{
	int len;

	len = rreq_tbl_print(buffer);
    
	*start = buffer + offset;
	len -= offset;
	if (len > length)
		len = length;
	else if (len < 0)
		len = 0;
	return len;    
}

int __init rreq_tbl_init(void)
{
	proc_net_create(RREQ_TBL_PROC_NAME, 0, rreq_tbl_proc_info);
	return 0;
}
void __exit rreq_tbl_cleanup(void)
{
	tbl_flush(&rreq_tbl);
	proc_net_remove(RREQ_TBL_PROC_NAME);
}



static struct dsr_rreq_opt *dsr_rreq_opt_add(char *buf, int len, 
					struct in_addr target)
{
	struct dsr_rreq_opt *rreq_opt;

	if (!buf || len < DSR_RREQ_HDR_LEN)
		return NULL;

	rreq_opt = (struct dsr_rreq_opt *)buf;
	
	rreq_opt->type = DSR_OPT_RREQ;
	rreq_opt->length = 6;
	rreq_opt->id = htons(rreq_seqno++);
	rreq_opt->target = target.s_addr;
	
	return rreq_opt;
}

int dsr_rreq_create(struct dsr_pkt *dp, struct in_addr target, char *buf, int len)
{
	dp->iph = dsr_build_ip(buf, len, dp->src, dp->dst, 1);
	
	if (!dp->iph) {
		DEBUG("Could not create IP header\n");
		return -1;
	}
	
	buf += IP_HDR_LEN;
	len -= IP_HDR_LEN;

	dp->dsr_opts_len = len;

	dp->opt_hdr = dsr_opt_hdr_add(buf, len, 0);
	
	if (!dp->opt_hdr) {
		DEBUG("Could not create DSR opt header\n");
		return -1;
	}
	
	buf += DSR_OPT_HDR_LEN;
	len -= DSR_OPT_HDR_LEN;
	
	dp->rreq_opt = dsr_rreq_opt_add(buf, len, target);

	if (!dp->rreq_opt) {
		DEBUG("Could not create RREQ opt\n");
		return -1;
	}
	
	return 0;
}
int dsr_rreq_send(struct in_addr target)
{
	struct dsr_pkt dp;
	char buf[IP_HDR_LEN + DSR_OPT_HDR_LEN + DSR_RREQ_HDR_LEN];
	int len = IP_HDR_LEN + DSR_OPT_HDR_LEN + DSR_RREQ_HDR_LEN;
	int res = 0;
	
	memset(&dp, 0, sizeof(dp));
	memset(buf, 0, IP_HDR_LEN + DSR_OPT_HDR_LEN + DSR_RREQ_HDR_LEN);
	
	dp.data = NULL; /* No data in this packet */
	dp.data_len = 0;
	dp.dst.s_addr = DSR_BROADCAST;
	dp.nxt_hop.s_addr = DSR_BROADCAST;
	
	dsr_node_lock(dsr_node);
	dp.src = dsr_node->ifaddr;
	dsr_node_unlock(dsr_node);

	res = dsr_rreq_create(&dp, target, buf, len);
	
	if (res < 0) {
		DEBUG("Could not create RREQ\n");
		return -1;
	}
	
	dsr_dev_xmit(&dp);
	
/* 	rreq_tbl_add(target, 5, jiffies); */

	return 0;
}

int dsr_rreq_opt_recv(struct dsr_pkt *dp)
{
	struct in_addr my_addr;

	if (!dp || !dp->rreq_opt)
		return DSR_PKT_DROP;


	dsr_node_lock(dsr_node);
	my_addr = dsr_node->ifaddr;
	dsr_node_unlock(dsr_node);
	
	if (dp->src.s_addr == my_addr.s_addr)
		return DSR_PKT_DROP;
	
	dp->srt = dsr_srt_new(dp->src, my_addr,
			      DSR_RREQ_ADDRS_LEN(dp->rreq_opt),
			      (char *)dp->rreq_opt->addrs);

	DEBUG("RREQ target=%s\n", print_ip(dp->rreq_opt->target));
	DEBUG("my addr %s\n", print_ip(my_addr.s_addr));

	if (dp->rreq_opt->target == my_addr.s_addr) {
		struct dsr_srt *srt_rev;

		DEBUG("RREQ OPT for me\n");
		
		/* According to the draft, the dest addr in the IP header must
		 * be updated with the target address */
		dp->iph->daddr = dp->rreq_opt->target;
	
		DEBUG("srt: %s\n", print_srt(dp->srt));

		/* Add reversed source route */
		srt_rev = dsr_srt_new_rev(dp->srt);
		
		DEBUG("srt_rev: %s\n", print_srt(srt_rev));

		dsr_rtc_add(srt_rev, 60000, 0);
		
		/* Send buffered packets */
		p_queue_set_verdict(P_QUEUE_SEND, srt_rev->dst.s_addr);

		kfree(srt_rev);
		
		return DSR_PKT_SEND_RREP | DSR_PKT_DROP;
	} else {
		int i, n;
		
		n = DSR_RREQ_ADDRS_LEN(dp->rreq_opt) / sizeof(struct in_addr);
		
		/* Examine source route if this node already exists in it */
		for (i = 0; i < n; i++)
			if (dp->srt->addrs[i].s_addr == my_addr.s_addr) {
				return DSR_PKT_DROP;
			}
		
		/* TODO: Reply if I have a route */

		
		/* TODO: Add myself to source route.... */
		
		/* Forward RREQ */
		return DSR_PKT_FORWARD;
	}

	return DSR_PKT_DROP;
}

