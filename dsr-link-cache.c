#include <linux/proc_fs.h>

#include "dsr-rtc.h"
#include "tbl.h"
#include "debug.h"

#define LC_NODES_MAX 500
#define LC_LINKS_MAX 100 /* TODO: Max links should be calculated from Max
			  * nodes */
#define LC_PROC_NAME "dsr_lc"

TBL(node_tbl, LC_NODES_MAX);
TBL(link_tbl, LC_LINKS_MAX);


struct lc_node {
	struct list_head l;
	struct in_addr addr;
	int links;
};

struct lc_link {
	struct list_head l;
	struct lc_node *n1, *n2;
	int status;
	int cost;
};

struct lc_node_link {
	struct list_head l;
	struct lc_link *link;
};

static inline int crit_addr(void *pos, void *addr)
{
	struct in_addr *a = addr; 
	struct lc_node *p = pos;
	
	if (p->addr.s_addr == a->s_addr)
		return 1;
	return 0;
}
static inline int crit_link_query(void *pos, void *query)
{
	struct in_addr *q = query; 
	struct lc_link *p = pos;
	
	if ((p->n1->addr.s_addr == q->s_addr && 
	     p->n2->addr.s_addr == (q+1)->s_addr) ||
	    (p->n2->addr.s_addr == q->s_addr && 
	     p->n1->addr.s_addr == (q+1)->s_addr))
		return 1;
	return 0;
}

static inline struct lc_node *lc_node_create(struct in_addr addr)
{
	struct lc_node *n;

	n = kmalloc(sizeof(struct lc_node), GFP_ATOMIC);
	
	if (!n)
		return NULL;
		
	n->addr = addr;
	n->links = 0;
/* 	INIT_TBL(&n->links, LC_LINKS_MAX); */
	
	return n;
};


static inline struct lc_link *__lc_link_tbl_find(struct in_addr a1, 
						 struct in_addr a2)
{
	struct in_addr query[2];
	
	query[0] = a1;
	query[1] = a2;
	
	return __tbl_find(&link_tbl, query, crit_link_query);
}

static int lc_link_tbl_add(struct lc_node *n1, struct lc_node *n2, 
			   int status, int cost)
{	
	struct lc_link *link;

	if (!n1 || !n2)
		return -1;

	write_lock_bh(&link_tbl.lock);
	
	link = __lc_link_tbl_find(n1->addr, n2->addr);
	
	if (!link) {
		
		link = kmalloc(sizeof(struct lc_link), GFP_ATOMIC);
		
		if (!link) {
			DEBUG("Could not allocate link\n");
			write_unlock_bh(&link_tbl.lock);
			return -1;
		}
		link->n1 = n1;
		link->n2 = n2;
		link->status = status;
		link->cost = cost;
	}
	
	__tbl_add(&node_tbl, &n2->l, crit_none);
	
	write_unlock_bh(&link_tbl.lock);

	return 1;
}


int lc_link_add(struct in_addr addr1, struct in_addr addr2, 
		int status, int cost)
{
	struct lc_node *n1, *n2;
	
	write_lock_bh(&node_tbl.lock);
	
	n1 = __tbl_find(&node_tbl, &addr1, crit_addr);
	n2 = __tbl_find(&node_tbl, &addr2, crit_addr);

	if (!n1) {
		n1 = lc_node_create(addr1);

		if (!n1) {
			DEBUG("Could not allocate nodes\n");
			write_unlock_bh(&node_tbl.lock);
			return -1;
		}
		__tbl_add(&node_tbl, &n1->l, crit_none);
		
	}
	if (!n2) {
		n2 = lc_node_create(addr2);
		if (!n2) {
			DEBUG("Could not allocate nodes\n");
			write_unlock_bh(&node_tbl.lock);
			return -1;
		}
		__tbl_add(&node_tbl, &n2->l, crit_none);
	}
	
	lc_link_tbl_add(n1, n2, status, cost);
	
	write_unlock_bh(&node_tbl.lock);

	return 0;
}

int lc_link_del(struct in_addr addr1, struct in_addr addr2)
{
	struct lc_link *link;

	
	write_lock_bh(&link_tbl.lock);

	link = __lc_link_tbl_find(addr1, addr2);

	if (!link) {
		write_unlock_bh(&link_tbl.lock);
		return 0;
	}	

	/* Also free the nodes if they lack other links */
	write_lock_bh(&node_tbl.lock);
	
	if (--link->n1->links < 0)
		__tbl_del(&node_tbl, &link->n1->l);

	if (--link->n2->links < 0)
		__tbl_del(&node_tbl, &link->n2->l);

	write_unlock_bh(&node_tbl.lock);
	write_unlock_bh(&link_tbl.lock);
	
	return 1;
}
static int lc_print(char *buf)
{
	struct list_head *pos;
	int len = 0;
    
	read_lock_bh(&link_tbl.lock);
    
	len += sprintf(buf, "# %-15s %-15s\n", "Addr", "Addr");

	list_for_each(pos, &link_tbl.head) {
		struct lc_link *link = (struct lc_link *)pos;
		
		len += sprintf(buf+len, "  %-15s %-15s\n", 
			       print_ip(link->n1->addr.s_addr),
			       print_ip(link->n2->addr.s_addr));
			       
	}
    
	read_unlock_bh(&link_tbl.lock);
	return len;

}

static int lc_proc_info(char *buffer, char **start, off_t offset, int length)
{
	int len;

	len = lc_print(buffer);
    
	*start = buffer + offset;
	len -= offset;
	if (len > length)
		len = length;
	else if (len < 0)
		len = 0;
	return len;    
}

static int dijkstra(void)
{
	struct lc_node *pi, *s;
	struct list_head *pos;
	int size;
	
	read_lock_bh(&node_tbl.lock);
	
	size = node_tbl.len*sizeof(pi);
	
	pi = kmalloc(size, GFP_ATOMIC);

	s = kmalloc(size, GFP_ATOMIC);
	
	memset(pi, 0, size);
	memset(s, 0, size);

	list_for_each(pos, &node_tbl.head) {
		struct lc_node *node = (struct lc_node *)pos;

	}
	
	kfree(pi);
	kfree(s);

	read_unlock_bh(&node_tbl.lock);

	return 0;
}


int __init lc_init(void)
{
	proc_net_create(LC_PROC_NAME, 0, lc_proc_info);
	return 0;
}

void __exit lc_cleanup(void)
{
	tbl_flush(&link_tbl, NULL);
	tbl_flush(&node_tbl, NULL);
	proc_net_remove(LC_PROC_NAME);
}
