#include <linux/skbuff.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <net/sock.h>
#include <linux/icmp.h>
#include <net/icmp.h>

#include "tbl.h"
#include "send-buf.h"
#include "debug.h"
#include "dsr-rtc.h"
#include "kdsr.h"

#define SEND_BUF_MAX_LEN 10
#define SEND_BUF_PROC_FS_NAME "send_buf"

TBL(send_buf, SEND_BUF_MAX_LEN);

struct send_buf_entry {
	struct list_head l;
	struct dsr_pkt *dp;
	unsigned long qtime;
	int (*okfn)(struct dsr_pkt *);
};

struct timer_list send_buf_timer;

static inline int crit_addr(void *pos, void *addr)
{
	struct in_addr *a = addr; 
	struct send_buf_entry *e = pos;
	
	if (e->dp->dst.s_addr == a->s_addr)
		return 1;
	return 0;
}

static inline int crit_garbage(void *pos, void *n)
{
	unsigned long *now = n; 
	struct send_buf_entry *e = pos;
	
	if (e->qtime + (PARAM(SendBufferTimeout) * HZ) < *now) {
		if (e->dp)
			dsr_pkt_free(e->dp);
		return 1;
	}
	return 0;
}
static void send_buf_timeout(unsigned long data)
{
	struct send_buf_entry *e;
	int pkts;
	unsigned long qtime, now = jiffies;	
	
	pkts = tbl_for_each_del(&send_buf, &now, crit_garbage);

	DEBUG("%d packets garbage collected\n", pkts);
	
	read_lock_bh(&send_buf.lock);
	/* Get first packet in maintenance buffer */
	e = __tbl_find(&send_buf, NULL, crit_none);
	
	if (!e) {
		DEBUG("No packet to set timeout for\n");
		read_unlock_bh(&send_buf.lock);
		return;
	}
	qtime = e->qtime;

	read_unlock_bh(&send_buf.lock);
	
	send_buf_timer.expires = qtime + (PARAM(SendBufferTimeout) * HZ);
	add_timer(&send_buf_timer);
}


static struct send_buf_entry *send_buf_entry_create(struct dsr_pkt *dp, int (*okfn)(struct dsr_pkt *))
{
	struct send_buf_entry *e;
	
	e = kmalloc(sizeof(*e), GFP_ATOMIC);
	
	if (!e) {
		DEBUG("OOM in send_buf_enqueue_packet()\n");
		return NULL;
	}

	e->dp = dp;
	e->okfn = okfn;
	e->qtime = jiffies;

	return e;
}

int send_buf_enqueue_packet(struct dsr_pkt *dp, int (*okfn)(struct dsr_pkt *))
{
	struct send_buf_entry *e;
	int res, empty = 0;
	
	if (TBL_EMPTY(&send_buf))
		empty = 1;

	e = send_buf_entry_create(dp, okfn);

	if (!e)
		return -ENOMEM;
	
	res = tbl_add_tail(&send_buf, &e->l);
	
	if (res < 0) {
		struct send_buf_entry *f;
		
		DEBUG("buffer full, removing first\n");
		f = tbl_detach_first(&send_buf);
		
		dsr_pkt_free(f->dp);
		kfree(f);

		res = tbl_add_tail(&send_buf, &e->l);
		
		if (res < 0) {
			DEBUG("Could not buffer packet\n");
			dsr_pkt_free(e->dp);
			kfree (e);
			return -1;
		}
	}
		
	if (empty) {
		send_buf_timer.expires = jiffies + (PARAM(SendBufferTimeout) * HZ);
		add_timer(&send_buf_timer);
	}

	return res;
}

int send_buf_set_verdict(int verdict, unsigned long daddr)
{
	struct send_buf_entry *e;
	struct in_addr dst;
	int pkts = 0;

	dst.s_addr = daddr;

	switch (verdict) {
	case SEND_BUF_DROP:
		
		while ((e = tbl_find_detach(&send_buf, &dst, crit_addr))) {
			/* Only send one ICMP message */
			if (pkts == 0)
				icmp_send(e->dp->skb, ICMP_DEST_UNREACH, ICMP_HOST_UNREACH, 0);	   
			dsr_pkt_free(e->dp);
			kfree(e);
			pkts++;
		}
		DEBUG("Dropped %d queued pkts\n", pkts);
		break;
	case SEND_BUF_SEND:

		while ((e = tbl_find_detach(&send_buf, &dst, crit_addr))) {
			
			/* Get source route */
			e->dp->srt = dsr_rtc_find(e->dp->src, e->dp->dst);
		    
			if (e->dp->srt) {
				
				DEBUG("Source route=%s\n", print_srt(e->dp->srt));
				
				if (dsr_srt_add(e->dp) < 0) {
					DEBUG("Could not add source route\n");
					dsr_pkt_free(e->dp);
				} else 					
					/* Send packet */		
					e->okfn(e->dp);
				
			} else {
				DEBUG("No source route found for %s!\n", print_ip(daddr));
		    
				dsr_pkt_free(e->dp);
			}
			pkts++;
			kfree(e);
		}
		
		if (pkts == 0)
			DEBUG("No packets for dest %s\n", print_ip(daddr));
		break;
	}
	return pkts;
}
static int send_buf_get_info(char *buffer, char **start, off_t offset, int length)
{
	struct list_head *p;
	int len;

	len = sprintf(buffer, "# %-15s %-8s\n", "IPAddr", "Age (s)");

	read_lock_bh(&send_buf.lock);

	list_for_each_prev(p, &send_buf.head) {
		struct send_buf_entry *e = (struct send_buf_entry *)p;
		
		if (e && e->dp)
			len += sprintf(buffer+len, "  %-15s %-8lu\n", print_ip(e->dp->dst.s_addr), (jiffies - e->qtime) / HZ);
	}

	len += sprintf(buffer+len,
		       "\nQueue length      : %u\n"
		       "Queue max. length : %u\n",
		       send_buf.len,
		       send_buf.max_len);
	
	read_unlock_bh(&send_buf.lock);
	
	*start = buffer + offset;
	len -= offset;

	if (len > length)
		len = length;
	else if (len < 0)
		len = 0;
	return len;
}

int send_buf_flush(void)
{
	struct send_buf_entry *e;
	int pkts = 0;
	/* Flush send buffer */
	while((e = tbl_find_detach(&send_buf, NULL, crit_none))) {
		dsr_pkt_free(e->dp);
		kfree(e);
		pkts++;
	}
	return pkts;
}

int __init send_buf_init(void)
{
	struct proc_dir_entry *proc;
		
	init_timer(&send_buf_timer);
	send_buf_timer.function = &send_buf_timeout;

	proc = proc_net_create(SEND_BUF_PROC_FS_NAME, 0, send_buf_get_info);
	if (proc)
		proc->owner = THIS_MODULE;
	else {
		printk(KERN_ERR "send_buf: failed to create proc entry\n");
		return -1;
	}

	return 1;
}

void __exit send_buf_cleanup(void)
{
	int pkts;
#ifdef KERNEL26
	synchronize_net();
#endif
	if (timer_pending(&send_buf_timer))
		del_timer_sync(&send_buf_timer);
	
	pkts = send_buf_flush();
	
	DEBUG("Flushed %d packets\n", pkts);

	proc_net_remove(SEND_BUF_PROC_FS_NAME);
}

