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
#include <net/icmp.h>

#include "p-queue.h"
#include "debug.h"
#include "dsr-rtc.h"
/*
 * This is basically a shameless rippoff of the linux kernel's ip_queue module.
 */

#define P_QUEUE_QMAX_DEFAULT 1024
#define P_QUEUE_PROC_FS_NAME "p_queue"
#define NET_P_QUEUE_QMAX 2088
#define NET_P_QUEUE_QMAX_NAME "p_queue_maxlen"

struct p_queue_entry {
	struct list_head list;
	struct dsr_pkt *dp;
	int (*okfn)(struct dsr_pkt *);
};

typedef int (*p_queue_cmpfn)(struct p_queue_entry *, unsigned long);

static unsigned int queue_maxlen = P_QUEUE_QMAX_DEFAULT;
static rwlock_t queue_lock = RW_LOCK_UNLOCKED;
static unsigned int queue_total;
static LIST_HEAD(queue_list);

static inline int __p_queue_enqueue_entry(struct p_queue_entry *entry)
{
       if (queue_total >= queue_maxlen) {
               if (net_ratelimit()) 
                       printk(KERN_WARNING "ip_queue: full at %d entries, "
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
static inline struct p_queue_entry *__p_queue_find_entry(p_queue_cmpfn cmpfn, unsigned long data)
{
	struct list_head *p;

	list_for_each_prev(p, &queue_list) {
		struct p_queue_entry *entry = (struct p_queue_entry *)p;
		
		if (!cmpfn || cmpfn(entry, data))
			return entry;
	}
	return NULL;
}

static inline void __p_queue_dequeue_entry(struct p_queue_entry *entry)
{
	list_del(&entry->list);
	queue_total--;
}

static inline struct p_queue_entry *__p_queue_find_dequeue_entry(p_queue_cmpfn cmpfn, unsigned long data)
{
	struct p_queue_entry *entry;

	entry = __p_queue_find_entry(cmpfn, data);
	if (entry == NULL)
		return NULL;

	__p_queue_dequeue_entry(entry);
	return entry;
}


static inline void __p_queue_flush(void)
{
	struct p_queue_entry *entry;
	int n = 0;
	
	while ((entry = __p_queue_find_dequeue_entry(NULL, 0))) {
	    kfree_skb(entry->dp->skb);
	    dsr_pkt_free(entry->dp);
	    kfree(entry);
	    n++;
	}
	DEBUG("%d pkts flushed\n", n);
}

static inline void __p_queue_reset(void)
{
    __p_queue_flush();
}

static struct p_queue_entry *p_queue_find_dequeue_entry(p_queue_cmpfn cmpfn, unsigned long data)
{
	struct p_queue_entry *entry;
	
	write_lock_bh(&queue_lock);
	entry = __p_queue_find_dequeue_entry(cmpfn, data);
	write_unlock_bh(&queue_lock);
	return entry;
}

void p_queue_flush(void)
{
	write_lock_bh(&queue_lock);
	__p_queue_flush();
	write_unlock_bh(&queue_lock);
}

int p_queue_enqueue_packet(dsr_pkt_t *dp, int (*okfn)(dsr_pkt_t *))
{
	int status = -EINVAL;
	struct p_queue_entry *entry;

	entry = kmalloc(sizeof(*entry), GFP_ATOMIC);
	
	if (entry == NULL) {
		printk(KERN_ERR "ip_queue: OOM in p_queue_enqueue_packet()\n");
		return -ENOMEM;
	}

	/* printk("enquing packet queue_len=%d\n", queue_total); */
	entry->okfn = okfn;
	entry->dp = dp;
	
	write_lock_bh(&queue_lock);

	status = __p_queue_enqueue_entry(entry);
	
	if (status < 0)
		goto err_out_unlock;

	write_unlock_bh(&queue_lock);
	return status;
	
err_out_unlock:
	write_unlock_bh(&queue_lock);
	kfree(entry);

	return status;
}

static inline int dest_cmp(struct p_queue_entry *e, unsigned long daddr)
{
	return (daddr == e->dp->dst.s_addr);
}

int p_queue_find(__u32 daddr)
{
    struct p_queue_entry *entry;
    int res = 0;

    read_lock_bh(&queue_lock);
    entry = __p_queue_find_entry(dest_cmp, daddr);
    if (entry != NULL)
	res = 1;
    
    read_unlock_bh(&queue_lock);
    return res;    
}


int p_queue_set_verdict(int verdict, struct in_addr addr)
{
    struct p_queue_entry *entry;
    int pkts = 0;
    
    if (verdict == P_QUEUE_DROP) {
	
	while (1) {
	    entry = p_queue_find_dequeue_entry(dest_cmp, addr.s_addr);
	    
	    if (entry == NULL)
		return pkts;
	    
	    /* Send an ICMP message informing the application that the
	     * destination was unreachable. */
	    if (pkts == 0)
		icmp_send(entry->dp->skb, ICMP_DEST_UNREACH, ICMP_HOST_UNREACH, 0);	    
	    
	    kfree_skb(entry->dp->skb);
	    dsr_pkt_free(entry->dp);
	    kfree(entry);
	    pkts++;
	}
	
	DEBUG("Dropped %d queued pkts\n", pkts);

    } else if (verdict == P_QUEUE_SEND) {
	/* struct expl_entry e; */

	while (1) {
	    entry = p_queue_find_dequeue_entry(dest_cmp, addr.s_addr);
	    
	    if (entry == NULL) {
		    DEBUG("No packets for dest %s\n", print_ip(addr.s_addr));
		    return pkts;
	    }
	    entry->dp->srt = dsr_rtc_find(entry->dp->dst);
	    
	    /* Add source route */
	    if (!entry->dp->srt || dsr_srt_add(entry->dp) < 0) {
		    kfree_skb(entry->dp->skb);
		    dsr_pkt_free(entry->dp);
		    kfree(entry);
		    continue;
	    }
		    
	    pkts++;
	    
	    /* Inject packet */
	    entry->okfn(entry->dp);
	    kfree(entry);
	}
	DEBUG("Sent %d queued pkts\n", pkts);
    }
    return 0;
}

static int
p_queue_get_info(char *buffer, char **start, off_t offset, int length)
{
	struct list_head *p;
	int len;

	len = sprintf(buffer, "# Queue info\n");

	read_lock_bh(&queue_lock);

	list_for_each_prev(p, &queue_list) {
		struct p_queue_entry *entry = (struct p_queue_entry *)p;
		
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
	proc = proc_net_create(P_QUEUE_PROC_FS_NAME, 0, p_queue_get_info);
	if (proc)
		proc->owner = THIS_MODULE;
	else {
		printk(KERN_ERR "p_queue: failed to create proc entry\n");
	return -1;
	}
	return 1;

cleanup:
#ifdef KERNEL26
	synchronize_net();
#endif
	p_queue_flush();
	
	proc_net_remove(P_QUEUE_PROC_FS_NAME);
	
	return status;
}

int __init p_queue_init(void)
{
	
	return init_or_cleanup(1);
}

void __exit p_queue_cleanup(void)
{
	init_or_cleanup(0);
}

