#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/init.h>
#include <linux/ip.h>
#include <linux/notifier.h>
#include <linux/netdevice.h>
#include <linux/netfilter_ipv4.h>
#include <linux/spinlock.h>
#include <linux/sysctl.h>
#include <linux/proc_fs.h>
#include <net/sock.h>
#include <net/route.h>
#include <linux/icmp.h>
#include <net/icmp.h>

#include "send-buf.h"
#include "debug.h"
#include "dsr-rtc.h"
#include "kdsr.h"
/*
 * This is basically a shameless rippoff of the linux kernel's isend_buf module.
 */

#define SEND_BUF_QMAX_DEFAULT 1024
#define SEND_BUF_PROC_FS_NAME "send_buf"
#define NET_SEND_BUF_QMAX 2088
#define NET_SEND_BUF_QMAX_NAME "send_buf_maxlen"

struct send_buf_entry {
	struct list_head list;
	struct dsr_pkt *dp;
/* 	struct sk_buff *skb; */
	int (*okfn)(struct dsr_pkt *);
};

typedef int (*send_buf_cmpfn)(struct send_buf_entry *, unsigned long);

static unsigned int queue_maxlen = SEND_BUF_QMAX_DEFAULT;
static rwlock_t queue_lock = RW_LOCK_UNLOCKED;
static unsigned int queue_total;
static LIST_HEAD(queue_list);

static inline int __send_buf_enqueue_entry(struct send_buf_entry *entry)
{
	if (queue_total >= queue_maxlen) {
		if (net_ratelimit()) 
			printk(KERN_WARNING "isend_buf: full at %d entries, "
			       "dropping packet(s).\n", queue_total);
		return -ENOSPC;
	}
	list_add(&entry->list, &queue_list);
	queue_total++;
	return 0;
}

/*
 * Find and return a queued entry matched by cmpfn, or return the last
 * entry if cmpfn is NULL.
 */
static inline struct send_buf_entry *__send_buf_find_entry(send_buf_cmpfn cmpfn, unsigned long data)
{
	struct list_head *p;

	list_for_each_prev(p, &queue_list) {
		struct send_buf_entry *entry = (struct send_buf_entry *)p;
		
		if (!cmpfn || cmpfn(entry, data))
			return entry;
	}
	return NULL;
}

static inline struct send_buf_entry *__send_buf_find_dequeue_entry(send_buf_cmpfn cmpfn, unsigned long data)
{
	struct send_buf_entry *entry;

	entry = __send_buf_find_entry(cmpfn, data);
	if (entry == NULL)
		return NULL;

	list_del(&entry->list);
	queue_total--;

	return entry;
}


static inline void __send_buf_flush(void)
{
	struct send_buf_entry *entry;
	int n = 0;
	
	while ((entry = __send_buf_find_dequeue_entry(NULL, 0))) {
		dsr_pkt_free(entry->dp);
		kfree(entry);
		n++;
	}
	DEBUG("%d pkts flushed\n", n);
}

static inline void __send_buf_reset(void)
{
	__send_buf_flush();
}

static struct send_buf_entry *send_buf_find_dequeue_entry(send_buf_cmpfn cmpfn, unsigned long data)
{
	struct send_buf_entry *entry;
	
	write_lock_bh(&queue_lock);
	entry = __send_buf_find_dequeue_entry(cmpfn, data);
	write_unlock_bh(&queue_lock);
	return entry;
}

void send_buf_flush(void)
{
	write_lock_bh(&queue_lock);
	__send_buf_flush();
	write_unlock_bh(&queue_lock);
}

int send_buf_enqueue_packet(struct dsr_pkt *dp, int (*okfn)(struct dsr_pkt *))
{
	int status = -EINVAL;
	struct send_buf_entry *entry;

	entry = kmalloc(sizeof(*entry), GFP_ATOMIC);
	
	if (entry == NULL) {
		printk(KERN_ERR "isend_buf: OOM in send_buf_enqueue_packet()\n");
		return -ENOMEM;
	}
	
	entry->okfn = okfn;
	/* entry->skb = skb; */
/* 	memcpy(&entry->dp, dp, sizeof(struct dsr_pkt)); */

/* 	entry->dp.nh.iph = dp->nh.iph; */
/* 	entry->dp.data = dp->data; */
	entry->dp = dp;

	write_lock_bh(&queue_lock);

	status = __send_buf_enqueue_entry(entry);
	
	if (status < 0)
		goto err_out_unlock;

	DEBUG("enquing packet, queue_len=%d\n",
	      queue_total);

	write_unlock_bh(&queue_lock);
	return status;
	
 err_out_unlock:
	write_unlock_bh(&queue_lock);
	kfree(entry);

	return status;
}

static inline int dest_cmp(struct send_buf_entry *e, unsigned long daddr)
{
	return (daddr == e->dp->skb->nh.iph->daddr);
}

int send_buf_find(__u32 daddr)
{
	struct send_buf_entry *entry;
	int res = 0;

	read_lock_bh(&queue_lock);
	entry = __send_buf_find_entry(dest_cmp, daddr);
	if (entry != NULL)
		res = 1;
    
	read_unlock_bh(&queue_lock);
	return res;    
}


int send_buf_set_verdict(int verdict, unsigned long daddr)
{
	struct send_buf_entry *entry;
	int pkts = 0;

	if (verdict == SEND_BUF_DROP) {
	
		while (1) {
			entry = send_buf_find_dequeue_entry(dest_cmp, daddr);
	    
			if (entry == NULL)
				break;
	    
			/* Send an ICMP message informing the application that the
			 * destination was unreachable. */
			if (pkts == 0)
				icmp_send(entry->dp->skb, ICMP_DEST_UNREACH, ICMP_HOST_UNREACH, 0);	    
	    
			dsr_pkt_free(entry->dp);
			kfree(entry);
			pkts++;
		}
	
		DEBUG("Dropped %d queued pkts\n", pkts);

	} else if (verdict == SEND_BUF_SEND) {

		while (1) {
			entry = send_buf_find_dequeue_entry(dest_cmp, daddr);
		    
			if (entry == NULL) {
				if (pkts == 0)
					DEBUG("No packets for dest %s\n", print_ip(daddr));
				break;
			}
		    
			entry->dp->srt = dsr_rtc_find(entry->dp->src, entry->dp->dst);
		    
			if (entry->dp->srt) {
				
				DEBUG("Source route=%s\n", print_srt(entry->dp->srt));
				
				if (dsr_srt_add(entry->dp) < 0) {
					DEBUG("Could not add source route\n");
					dsr_pkt_free(entry->dp);
				} else {
					
					/* Send packet */
					entry->okfn(entry->dp);
				    
				}
			} else {
				DEBUG("No source route found for %s!\n", print_ip(daddr));
		    
				dsr_pkt_free(entry->dp);
			}
			pkts++;
			kfree(entry);
		}
		DEBUG("Sent or Removed %d queued pkts\n", pkts);
	}
	return pkts;
}

static int send_buf_get_info(char *buffer, char **start, off_t offset, int length)
{
	struct list_head *p;
	int len;

	len = sprintf(buffer, "# Queue info\n");

	read_lock_bh(&queue_lock);

	list_for_each_prev(p, &queue_list) {
		struct send_buf_entry *entry = (struct send_buf_entry *)p;
		
		if (entry && entry->dp)
			len += sprintf(buffer+len, "%s\n", print_ip(entry->dp->dst.s_addr));
	}

	len += sprintf(buffer+len,
		       "\nQueue length      : %u\n"
		       "Queue max. length : %u\n",
		       queue_total,
		       queue_maxlen);
	
	read_unlock_bh(&queue_lock);
	
	*start = buffer + offset;
	len -= offset;

	if (len > length)
		len = length;
	else if (len < 0)
		len = 0;
	return len;
}

static int init_or_cleanup(int init)
{
	int status = -ENOMEM;
	struct proc_dir_entry *proc;
	
	if (!init)
		goto cleanup;

	queue_total = 0;
	proc = proc_net_create(SEND_BUF_PROC_FS_NAME, 0, send_buf_get_info);
	if (proc)
		proc->owner = THIS_MODULE;
	else {
		printk(KERN_ERR "send_buf: failed to create proc entry\n");
		return -1;
	}
	return 1;

 cleanup:
#ifdef KERNEL26
	synchronize_net();
#endif
	send_buf_flush();
	
	proc_net_remove(SEND_BUF_PROC_FS_NAME);
	
	return status;
}

int __init send_buf_init(void)
{
	
	return init_or_cleanup(1);
}

void __exit send_buf_cleanup(void)
{
	init_or_cleanup(0);
}

