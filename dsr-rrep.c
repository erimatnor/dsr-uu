#include <linux/proc_fs.h>
#include <linux/string.h>
#include <linux/if_ether.h>
#include <net/ip.h>

#include "dsr.h"
#include "debug.h"
#include "tbl.h"
#include "dsr-rrep.h"
#include "dsr-opt.h"
#include "dsr-srt.h"
#include "dsr-rtc.h"
#include "dsr-dev.h"
#include "send-buf.h"
#include "kdsr.h"

#define GRAT_RREP_TBL_MAX_LEN 64
#define GRAT_RREP_TBL_PROC_NAME "dsr_grat_rrep_tbl"
#define GRAT_REPLY_HOLDOFF 1

static TBL(grat_rrep_tbl, GRAT_RREP_TBL_MAX_LEN);

struct grat_rrep_entry {
	struct list_head l;
	struct in_addr src, prev_hop;
	unsigned long expire;
};
struct timer_list grat_rrep_tbl_timer;

static inline int crit_query(void *pos, void *query)
{
	struct grat_rrep_entry *p = pos;
	struct in_addr *src = query;
	struct in_addr *prev_hop = src+1;
	
	if (p->src.s_addr == src->s_addr && 
	    p->prev_hop.s_addr == prev_hop->s_addr)
		return 1;
	return 0;
}
static inline int crit_time(void *pos, void *time)
{
	struct grat_rrep_entry *p = pos;
	unsigned long *t = time;

	if (p->expire > *t)
		return 1;

	return 0;
}

static void grat_rrep_tbl_timeout(unsigned long data)
{
	struct grat_rrep_entry *e = tbl_detach_first(&grat_rrep_tbl);
	
	kfree(e);

	read_lock_bh(&grat_rrep_tbl.lock);
	
	if (!TBL_EMPTY(&grat_rrep_tbl)) {
		e = (struct grat_rrep_entry *)TBL_FIRST(&grat_rrep_tbl);
		grat_rrep_tbl_timer.function = grat_rrep_tbl_timeout;
		grat_rrep_tbl_timer.expires = e->expire;
		add_timer(&grat_rrep_tbl_timer);
		read_unlock_bh(&grat_rrep_tbl.lock);
	}	
	read_unlock_bh(&grat_rrep_tbl.lock);
}

int grat_rrep_tbl_add(struct in_addr src, struct in_addr prev_hop)
{
	struct in_addr q[2] = { src, prev_hop };
	struct grat_rrep_entry *e;
	unsigned long int time = jiffies;
	
	if (in_tbl(&grat_rrep_tbl, q, crit_query))
		return 0;

	e = kmalloc(sizeof(struct grat_rrep_entry), GFP_ATOMIC);
	
	if (!e)
		return -1;

	e->src = src;
	e->prev_hop = prev_hop;
	e->expire = time + (GRAT_REPLY_HOLDOFF * HZ);
	
	if (timer_pending(&grat_rrep_tbl_timer))
		del_timer(&grat_rrep_tbl_timer);
			
	if (tbl_add(&grat_rrep_tbl, &e->l, crit_time)) {
		
		read_lock_bh(&grat_rrep_tbl.lock);
		e = (struct grat_rrep_entry *)TBL_FIRST(&grat_rrep_tbl);
		grat_rrep_tbl_timer.function = grat_rrep_tbl_timeout;
		grat_rrep_tbl_timer.expires = e->expire;
		add_timer(&grat_rrep_tbl_timer);
		read_unlock_bh(&grat_rrep_tbl.lock);		
	}
	return 1;
}

static int grat_rrep_tbl_print(char *buf)
{
	struct list_head *pos;
	int len = 0;
    
	read_lock_bh(&grat_rrep_tbl.lock);
    
	len += sprintf(buf, "# %-15s %-15s Time\n", "Source", "Prev hop");

	list_for_each(pos, &grat_rrep_tbl.head) {
		struct grat_rrep_entry *e = (struct grat_rrep_entry *)pos;
		
		len += sprintf(buf+len, "  %-15s %-15s %lu\n", 
			       print_ip(e->src.s_addr), 
			       print_ip(e->prev_hop.s_addr),
			       e->expire ? ((e->expire - jiffies) / HZ) : 0);
	}
    
	read_unlock_bh(&grat_rrep_tbl.lock);
	return len;

}
static int grat_rrep_tbl_proc_info(char *buffer, char **start, off_t offset, int length)
{
	int len;

	len = grat_rrep_tbl_print(buffer);
    
	*start = buffer + offset;
	len -= offset;
	if (len > length)
		len = length;
	else if (len < 0)
		len = 0;
	return len;    
}

int __init grat_rrep_tbl_init(void)
{
	proc_net_create(GRAT_RREP_TBL_PROC_NAME, 0, grat_rrep_tbl_proc_info);
	return 0;
}


void __exit grat_rrep_tbl_cleanup(void)
{
	tbl_flush(&grat_rrep_tbl, NULL);
	proc_net_remove(GRAT_RREP_TBL_PROC_NAME);
}

static inline int dsr_rrep_add_srt(struct dsr_rrep_opt *rrep_opt, struct dsr_srt *srt)
{
	int n;

	if (!rrep_opt | !srt)
		return -1;

	n = srt->laddrs / sizeof(struct in_addr);

	memcpy(rrep_opt->addrs, srt->addrs, srt->laddrs);
	memcpy(&rrep_opt->addrs[n], &srt->dst, sizeof(struct in_addr));
	
	return 0;
}

static struct dsr_rrep_opt *dsr_rrep_opt_add(char *buf, int len, struct dsr_srt *srt)
{
	struct dsr_rrep_opt *rrep_opt;
	
	if (!buf || !srt || len < DSR_RREP_OPT_LEN(srt))
		return NULL;

	rrep_opt = (struct dsr_rrep_opt *)buf;
	
	rrep_opt->type = DSR_OPT_RREP;
	rrep_opt->length = srt->laddrs + sizeof(struct in_addr) + 1;
	rrep_opt->l = 0;
	rrep_opt->res = 0;

	/* Add source route to RREP */
	dsr_rrep_add_srt(rrep_opt, srt);
       
	return rrep_opt;	
}

int dsr_rrep_send(struct dsr_srt *srt)
{
	struct dsr_pkt dp;
	char ip_buf[IP_HDR_LEN];
	char *buf;
	struct dsr_srt *srt_to_me;
	struct dsr_pad1_opt *pad1_opt;
	int len;
	
	if (!srt) {
		DEBUG("no source route!\n");
		return -1;
	}	
	
	memset(&dp, 0, sizeof(dp));

	len = DSR_OPT_HDR_LEN + DSR_SRT_OPT_LEN(srt) + DSR_RREP_OPT_LEN(srt) + DSR_OPT_PAD1_LEN;

	dp.dst = srt->dst;
	dp.src = my_addr();
	dp.nxt_hop = dsr_srt_next_hop(srt);
	dp.srt = srt;
	dp.dsr_opts_len = len;
	dp.data = NULL;
	dp.data_len = 0;

	
	DEBUG("IP_HDR_LEN=%d DSR_OPT_HDR_LEN=%d DSR_SRT_OPT_LEN=%d DSR_RREP_OPT_LEN=%d DSR_OPT_PAD1_LEN=%d RREP len=%d\n", IP_HDR_LEN, DSR_OPT_HDR_LEN, DSR_SRT_OPT_LEN(srt), DSR_RREP_OPT_LEN(srt), DSR_OPT_PAD1_LEN, len);
	
	DEBUG("srt: %s\n", print_srt(srt));
	
	dp.nh.iph = dsr_build_ip(ip_buf, IP_HDR_LEN, IP_HDR_LEN + len, dp.src, dp.dst, 1);
	
	if (!dp.nh.iph) {
		DEBUG("Could not create IP header\n");
		return -1;
	}
	
	buf = kmalloc(len, GFP_ATOMIC);
	
	if (!buf)
		return -1;
	
	dp.dh.opth = dsr_opt_hdr_add(buf, len, 0);
	
	if (!dp.dh.opth) {
		DEBUG("Could not create DSR options header\n");
		kfree(buf);
		return -1;
	}

	buf += DSR_OPT_HDR_LEN;
	len -= DSR_OPT_HDR_LEN;

	srt_to_me = dsr_srt_new_rev(dp.srt);
	
	if (!srt_to_me) {
		kfree(dp.dh.raw);
		return -1;
	}
	/* Add the source route option to the packet */
	dp.srt_opt = dsr_srt_opt_add(buf, len, srt_to_me);

	kfree(srt_to_me);

	if (!dp.srt_opt) {
		DEBUG("Could not create Source Route option header\n");
		kfree(dp.dh.raw);
		return -1;
	}

	buf += DSR_SRT_OPT_LEN(dp.srt);
	len -= DSR_SRT_OPT_LEN(dp.srt);

	dp.rrep_opt = dsr_rrep_opt_add(buf, len, dp.srt);
	
	if (!dp.rrep_opt) {
		DEBUG("Could not create RREP option header\n");
		kfree(dp.dh.raw);
		return -1;
	}

	buf += DSR_RREP_OPT_LEN(dp.srt);
	len -= DSR_RREP_OPT_LEN(dp.srt);
	
	pad1_opt = (struct dsr_pad1_opt *)buf;
	pad1_opt->type = DSR_OPT_PAD1;
	
	dsr_dev_xmit(&dp);
	
	return 0;
}

int dsr_rrep_opt_recv(struct dsr_pkt *dp)
{
	struct in_addr myaddr;
	struct dsr_srt *rrep_opt_srt;

	if (!dp || !dp->rrep_opt)
		return DSR_PKT_DROP;

	
	myaddr = my_addr();
	
	rrep_opt_srt = dsr_srt_new(dp->dst, dp->src, 
			      DSR_RREP_ADDRS_LEN(dp->rrep_opt), 
			      (char *)dp->rrep_opt->addrs);
	
	if (!rrep_opt_srt)
		return DSR_PKT_DROP;
	
	DEBUG("Adding srt to cache\n");
	dsr_rtc_add(rrep_opt_srt, 60000, 0);
	
	kfree(rrep_opt_srt);
	
	if (dp->dst.s_addr == myaddr.s_addr) {
		/*RREP for this node */
		
		DEBUG("RREP for me!\n");
				
		return DSR_PKT_DROP | DSR_PKT_SEND_BUFFERED;				
	}
	
	DEBUG("I am not RREP destination\n");
	/* Forward */
	return DSR_PKT_FORWARD;	
}
