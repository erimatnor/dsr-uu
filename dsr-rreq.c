#include <linux/proc_fs.h>
#include <linux/timer.h>
#include <net/ip.h>

#include "debug.h"
#include "dsr.h"
#include "tbl.h"
#include "kdsr.h"
#include "dsr-rrep.h"
#include "dsr-rreq.h"
#include "dsr-opt.h"
#include "dsr-rtc.h"
#include "dsr-dev.h"
#include "send-buf.h"

#define RREQ_TBL_PROC_NAME "dsr_rreq_tbl"
#define RREQ_TTL_MAX 8

static TBL(rreq_tbl, RREQ_TBL_MAX_LEN);

#define STATE_LATENT        0
#define STATE_IN_ROUTE_DISC 1

static unsigned int rreq_seqno = 1;

struct rreq_tbl_entry {
	struct list_head l;
	int state;
	struct in_addr node_addr;
	int ttl;
	unsigned long tx_time;
	unsigned long timeout;
	int num_rreqs;
	struct tbl rreq_id_tbl;
};

struct id_entry {
	struct list_head l;
	struct in_addr trg_addr;
	unsigned short id;	
};

int dsr_rreq_send(struct in_addr target, int ttl);

static int crit_addr(void *pos, void *data)
{
	struct rreq_tbl_entry *e = pos;
	struct in_addr *a = data;
	
	if (e->node_addr.s_addr == a->s_addr)
		return 1;
	return 0;
}

static int crit_duplicate(void *pos, void *data)
{
	struct rreq_tbl_entry *e = pos;
	struct {
		struct in_addr *initiator;
		struct in_addr *target;
		unsigned int *id;
	} *d;

	d = data;
	
	if (e->node_addr.s_addr == d->initiator->s_addr) {
		struct list_head *p;
	
		list_for_each(p, &e->rreq_id_tbl.head) {
			struct id_entry *id_e = (struct id_entry *)p;
			
			if (id_e->trg_addr.s_addr == d->target->s_addr &&
			    id_e->id == *(d->id))
				return 1;
		}
	}
	return 0;
}

void rreq_tbl_set_max_len(unsigned int max_len)
{
        rreq_tbl.max_len = max_len;
}

static int dsr_rreq_duplicate(struct in_addr initiator, struct in_addr target, unsigned int id)
{
	struct {
		struct in_addr *initiator;
		struct in_addr *target;
		unsigned int *id;
	} d;

	d.initiator = &initiator;
	d.target = &target;
	d.id = &id;

	return in_tbl(&rreq_tbl, &d, crit_duplicate);
}

static struct rreq_tbl_entry *__rreq_tbl_entry_create(struct in_addr node_addr)
{
	struct rreq_tbl_entry *e;
	
	e = kmalloc(sizeof(struct rreq_tbl_entry), GFP_ATOMIC);

	if (!e)
		return NULL;

	e->state = STATE_LATENT;
	e->node_addr = node_addr;
	e->ttl = 0;
	e->tx_time = 0;
	e->num_rreqs = 0;
	INIT_TBL(&e->rreq_id_tbl, PARAM(RequestTableIds));
	
	return e;
}

static struct rreq_tbl_entry *__rreq_tbl_add(struct in_addr node_addr)
{
	struct rreq_tbl_entry *e;
		
	e = __rreq_tbl_entry_create(node_addr);

	if (!e)
		return NULL;
	
	if (TBL_FULL(&rreq_tbl)) {
		struct rreq_tbl_entry *f;
		
		f = (struct rreq_tbl_entry *)TBL_FIRST(&rreq_tbl);

		__tbl_detach(&rreq_tbl, &f->l);

		tbl_flush(&f->rreq_id_tbl, NULL);
		
		kfree(f);	
	}
	__tbl_add_tail(&rreq_tbl, &e->l);

	return e;
}

static int rreq_tbl_add_id(struct in_addr initiator, struct in_addr target, 
			   unsigned short id)
{
	struct rreq_tbl_entry *e;
	struct id_entry *id_e;
	int res = 0;
	
	write_lock_bh(&rreq_tbl);
	
	e = __tbl_find(&rreq_tbl, &initiator, crit_addr);
	
	if (!e)
		e = __rreq_tbl_add(initiator);

	if (!e) {
		res = -ENOMEM;
		goto out;
	}
	
	if (TBL_FULL(&e->rreq_id_tbl))
		tbl_del_first(&e->rreq_id_tbl);
	
	id_e = kmalloc(sizeof(struct id_entry), GFP_ATOMIC);
	
	if (!id_e) {
		res = -ENOMEM;
		goto out;
	}
	
	id_e->trg_addr = target;
	id_e->id = id;
	
	tbl_add_tail(&e->rreq_id_tbl, &id_e->l);

 out:
	write_unlock_bh(&rreq_tbl);

	return 1;
}


int dsr_rreq_route_discovery(struct in_addr target)
{
	struct rreq_tbl_entry *e;
	int res = 0;
	
	write_lock_bh(&rreq_tbl);
	
	e = __tbl_find(&rreq_tbl, &target, crit_addr);
	
	if (!e)
		e = __rreq_tbl_add(target);
	
	if (!e) {
		res = -ENOMEM;
		goto out;
	}
	
	if (e->state == STATE_IN_ROUTE_DISC) {
		DEBUG("Route discovery for %s already in progress\n", 
		      print_ip(target.s_addr));
		goto out;
	} 
	
	write_unlock_bh(&rreq_tbl);

#define	TTL_START 2
	e->state = STATE_IN_ROUTE_DISC;

	dsr_rreq_send(target, TTL_START);
	
	return 1;
 out:
	write_unlock_bh(&rreq_tbl);

	return res;
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

int dsr_rreq_send(struct in_addr target, int ttl)
{
	struct dsr_pkt *dp;
	char *buf;
	int len = DSR_OPT_HDR_LEN + DSR_RREQ_HDR_LEN;
	
       	dp = dsr_pkt_alloc(NULL);

	if (!dp) {
		DEBUG("Could not allocate DSR packet\n");
		return -1;
	}
	dp->dst.s_addr = DSR_BROADCAST;
	dp->nxt_hop.s_addr = DSR_BROADCAST;
	dp->src = my_addr();
		
	buf = dsr_pkt_alloc_opts(dp, len);
	
	if (!buf)
		goto out_err;
	
	dp->nh.iph = dsr_build_ip(dp, dp->src, dp->dst, IP_HDR_LEN, IP_HDR_LEN + len, IPPROTO_DSR, ttl);
	
	if (!dp->nh.iph) 
		goto out_err;

	dp->dh.opth = dsr_opt_hdr_add(buf, len, 0);
	
	if (!dp->dh.opth) {
		DEBUG("Could not create DSR opt header\n");
		goto out_err;
	}
	
	buf += DSR_OPT_HDR_LEN;
	len -= DSR_OPT_HDR_LEN;
	
	dp->rreq_opt = dsr_rreq_opt_add(buf, len, target);

	if (!dp->rreq_opt) {
		DEBUG("Could not create RREQ opt\n");
		goto out_err;
	}
	
	dp->nh.iph->ttl = ttl;
	
	DEBUG("Sending RREQ for %s ttl=%d\n", print_ip(target.s_addr), ttl);

	dsr_dev_xmit(dp);

	return 0;

 out_err:	
	dsr_pkt_free(dp);

	return -1;
}

int dsr_rreq_opt_recv(struct dsr_pkt *dp, struct dsr_rreq_opt *rreq_opt)
{
	struct in_addr myaddr;
	struct in_addr trg;
	struct dsr_srt *srt_rev;

	if (!dp || !rreq_opt)
		return DSR_PKT_DROP;

	myaddr = my_addr();
	
	if (dp->src.s_addr == myaddr.s_addr)
		return DSR_PKT_DROP;
	
	trg.s_addr = rreq_opt->target;

	if (dsr_rreq_duplicate(dp->src, trg, ntohs(rreq_opt->id))) {
		DEBUG("Duplicate RREQ from %s\n", print_ip(dp->src.s_addr));
		return DSR_PKT_DROP;
	}

	rreq_tbl_add_id(dp->src, trg, ntohs(rreq_opt->id));
	
	dp->srt = dsr_srt_new(dp->src, myaddr,
			      DSR_RREQ_ADDRS_LEN(rreq_opt),
			      (char *)rreq_opt->addrs);
	
	if (!dp->srt) {
		DEBUG("Could not extract source route\n");
		return DSR_PKT_ERROR;
	}
	DEBUG("RREQ target=%s\n", print_ip(rreq_opt->target));
	DEBUG("my addr %s\n", print_ip(myaddr.s_addr));
	
        /* Add reversed source route */
	srt_rev = dsr_srt_new_rev(dp->srt);
	
	if (!srt_rev) {
		DEBUG("Could not reverse source route\n");
		return DSR_PKT_ERROR;
	}
	DEBUG("srt: %s\n", print_srt(dp->srt));

	DEBUG("srt_rev: %s\n", print_srt(srt_rev));
	
	dsr_rtc_add(srt_rev, 60000, 0);
	
	/* Send buffered packets */
	send_buf_set_verdict(SEND_BUF_SEND, srt_rev->dst.s_addr);

	kfree(srt_rev);
		
	if (rreq_opt->target == myaddr.s_addr) {

		DEBUG("RREQ OPT for me\n");
		
		/* According to the draft, the dest addr in the IP header must
		 * be updated with the target address */
		dp->nh.iph->daddr = rreq_opt->target;
			
		return DSR_PKT_SEND_RREP;
	} else {
		int i, n;
		struct in_addr myaddr = my_addr();
		
		/* TODO: Reply if I have a route */
		
		n = DSR_RREQ_ADDRS_LEN(rreq_opt) / sizeof(struct in_addr);
		
		/* Examine source route if this node already exists in it */
		for (i = 0; i < n; i++)
			if (dp->srt->addrs[i].s_addr == myaddr.s_addr)
				return DSR_PKT_DROP;
		
		dsr_pkt_alloc_opts_expand(dp, sizeof(struct in_addr));
		
		if (!DSR_LAST_OPT(dp, rreq_opt)) {
			char *to, *from;
			to = (char *)rreq_opt + rreq_opt->length + 2 + sizeof(struct in_addr);
			from = (char *)rreq_opt + rreq_opt->length + 2;

			memmove(to, from, sizeof(struct in_addr));
		}
		rreq_opt->addrs[n] = myaddr.s_addr;
		rreq_opt->length += sizeof(struct in_addr);
		
		dp->dh.opth->p_len = htons(ntohs(dp->dh.opth->p_len) + 
					   sizeof(struct in_addr));
		
		dsr_build_ip(dp, dp->src, dp->dst, IP_HDR_LEN, ntohs(dp->nh.iph->tot_len) + sizeof(struct in_addr), IPPROTO_DSR, dp->nh.iph->ttl);
		
		/* Forward RREQ */
		return DSR_PKT_FORWARD_RREQ;
	}

	return DSR_PKT_DROP;
}
static int rreq_tbl_print(char *buf)
{
	struct list_head *pos1, *pos2;
	int len = 0;
	int first = 1;
    
	read_lock_bh(&rreq_tbl.lock);
    
	len += sprintf(buf, "# %-15s %-6s %-8s %15s:%s\n", "IPAddr", "TTL", "Time", "TargetIPAddr", "ID");

	list_for_each(pos1, &rreq_tbl.head) {
		struct rreq_tbl_entry *e = (struct rreq_tbl_entry *)pos1;
		struct id_entry *id_e;
		
		if (TBL_EMPTY(&e->rreq_id_tbl))
			len += sprintf(buf+len, "  %-15s %-6u %-8lu %15s:%s\n",
				       print_ip(e->node_addr.s_addr), e->ttl,
				       e->tx_time ? ((jiffies - e->tx_time) * HZ) : 0, "-", "-");
		else {
			id_e = (struct id_entry *)TBL_FIRST(&e->rreq_id_tbl);
			len += sprintf(buf+len, "  %-15s %-6u %-8lu %15s:%u\n",
				       print_ip(e->node_addr.s_addr), e->ttl,
				       e->tx_time ? ((jiffies - e->tx_time) * HZ) : 0, print_ip(id_e->trg_addr.s_addr), id_e->id);
		}
		list_for_each(pos2, &e->rreq_id_tbl.head) {
			id_e = (struct id_entry *)pos2;
			if (!first)
				len += sprintf(buf+len, "%49s:%u\n", print_ip(id_e->trg_addr.s_addr), id_e->id);
			first = 0;
		}
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
/* 	tbl_flush(&rreq_tbl, timer_remove); */
	proc_net_remove(RREQ_TBL_PROC_NAME);
}

