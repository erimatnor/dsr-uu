#include <linux/proc_fs.h>
#include <linux/timer.h>

#include "dsr-rtc.h"
#include "dsr-srt.h"
#include "tbl.h"
#include "debug.h"

#define LC_NODES_MAX 500
#define LC_LINKS_MAX 100 /* TODO: Max links should be calculated from Max
			  * nodes */
#define LC_PROC_NAME "dsr_lc"
#define LC_COST_INF UINT_MAX
#define LC_HOPS_INF UINT_MAX

/* TBL(node_tbl, LC_NODES_MAX); */
/* TBL(link_tbl, LC_LINKS_MAX); */

struct lc_node {
	struct list_head l;
	struct in_addr addr;
	int links;
	unsigned int cost; /* Cost estimate from source when running Dijkstra */
	unsigned int hops;  /* Number of hops from source. Used to get the
			     * length of the source route to allocate. Same as
			     * cost if cost is hops. */
	struct lc_node *pred; /* predecessor */
};

struct lc_link {
	struct list_head l;
	struct lc_node *src, *dst;
	int status;
	unsigned int cost;
	unsigned long time;
};

struct lc_graph {
	struct tbl nodes;
	struct tbl links;
	struct lc_node *src;
	rwlock_t lock;
#ifdef LC_TIMER
	static struct timer_list timer;
#endif
};

static struct lc_graph LC;

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
	struct lc_link *p = pos;
	struct {
		struct in_addr src, dst; 
	} *q = query;

	if (p->src->addr.s_addr == q->src.s_addr && 
	    p->dst->addr.s_addr == (q+1)->dst.s_addr)
		return 1;
	return 0;
}

static inline int do_lowest_cost(void *pos, void *cheapest)
{
	struct lc_node *n = pos;
	struct lc_node *c = cheapest;
	
	if (!c || n->cost < c->cost)
		cheapest = n;		
	return 0;
}

static inline int do_relax(void *pos, void *node)
{	
	struct lc_link *link = pos;
	struct lc_node *u = node;
	struct lc_node *v = link->dst;

	/* If u and v have a link between them, update cost if cheaper */
	if (link->src == u) {
		unsigned int w = link->cost;
		
		if (v->cost > (u->cost + w)) {
			v->cost = u->cost + w;
			v->hops = u->hops + 1;
			v->pred = u;
			return 1;
		}
	}
	return 0;
}

static inline int do_init(void *pos, void *addr)
{
	struct in_addr *a = addr; 
	struct lc_node *n = pos;
	
	if (n->addr.s_addr == a->s_addr) {
		n->cost = 0;
		n->hops = 0;
		n->pred = n;
	} else {
		n->cost = LC_COST_INF;
		n->hops = LC_HOPS_INF;
		n->pred = NULL;
	}	
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
	n->cost = LC_COST_INF;
	return n;
};


static inline struct lc_link *__lc_link_find(struct in_addr src, 
					     struct in_addr dst)
{
	struct {
		struct in_addr src, dst;
	} query = { src, dst };
	
	return __tbl_find(&LC.links, &query, crit_link_query);
}

static int __lc_link_tbl_add(struct lc_node *src, struct lc_node *dst, 
			   int status, int cost)
{	
	struct lc_link *link;

	if (!src || !dst)
		return -1;
	
	link = __lc_link_find(src->addr, dst->addr);
	
	if (!link) {
		
		link = kmalloc(sizeof(struct lc_link), GFP_ATOMIC);
		
		if (!link) {
			DEBUG("Could not allocate link\n");
			return -1;
		}
		__tbl_add(&LC.links, &link->l, crit_none);
	}
	
	link->src = src;
	link->dst = dst;
	link->status = status;
	link->cost = cost;
	link->time = jiffies;
	
	return 1;
}


int lc_link_add(struct in_addr addr1, struct in_addr addr2, 
		int status, int cost)
{
	struct lc_node *n1, *n2;
	int res;
	
	write_lock_bh(&LC.lock);
	
	n1 = __tbl_find(&LC.nodes, &addr1, crit_addr);
	n2 = __tbl_find(&LC.nodes, &addr2, crit_addr);

	if (!n1) {
		n1 = lc_node_create(addr1);

		if (!n1) {
			DEBUG("Could not allocate nodes\n");
			write_unlock_bh(&LC.lock);
			return -1;
		}
		__tbl_add(&LC.nodes, &n1->l, crit_none);
		
	}
	if (!n2) {
		n2 = lc_node_create(addr2);
		if (!n2) {
			DEBUG("Could not allocate nodes\n");
			write_unlock_bh(&LC.lock);
			return -1;
		}
		__tbl_add(&LC.nodes, &n2->l, crit_none);
	}
	
	res = __lc_link_tbl_add(n1, n2, status, cost);
	
	if (!res) {
		DEBUG("Could not add new link\n");
	}
	write_unlock_bh(&LC.lock);

	return 0;
}

int lc_link_del(struct in_addr src, struct in_addr dst)
{
	struct lc_link *link;

	
	write_lock_bh(&LC.lock);

	link = __lc_link_find(src, dst);

	if (!link) {
		write_unlock_bh(&LC.lock);
		return 0;
	}	

	/* Also free the nodes if they lack other links */
	if (--link->src->links < 0)
		__tbl_del(&LC.nodes, &link->src->l);

	if (--link->dst->links < 0)
		__tbl_del(&LC.nodes, &link->dst->l);

	write_unlock_bh(&LC.lock);
	
	return 1;
}
static int lc_print(char *buf)
{
	struct list_head *pos;
	int len = 0;
    
	read_lock_bh(&LC.lock);
    
	len += sprintf(buf, "# %-15s %-15s %-4s Age\n", "Addr", "Addr", "Cost");

	list_for_each(pos, &LC.links.head) {
		struct lc_link *link = (struct lc_link *)pos;
		
		len += sprintf(buf+len, "  %-15s %-15s %-4u %lu\n", 
			       print_ip(link->src->addr.s_addr),
			       print_ip(link->dst->addr.s_addr),
			       link->cost,
			       (jiffies - link->time) * HZ);
	}
    
	read_unlock_bh(&LC.lock);
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

static inline void __dijkstra_init_single_source(struct in_addr src)
{
	__tbl_do_for_each(&LC.nodes, &src, do_init);
}

static inline struct lc_node *__dijkstra_find_lowest_cost_node(void)
{
	struct lc_node *cheapest = NULL;

	__tbl_do_for_each(&LC.nodes, cheapest, do_lowest_cost);
	
	return cheapest;
}
/*
  relax( Node u, Node v, double w[][] )
      if d[v] > d[u] + w[u,v] then
          d[v] := d[u] + w[u,v]
          pi[v] := u

*/

static void __dijkstra(struct in_addr src)
{	
	TBL(S, LC_NODES_MAX);
	struct lc_node *src_node;
	
	__dijkstra_init_single_source(src);
	
	src_node = __tbl_find(&LC.nodes, &src, crit_addr);

	while (!TBL_EMPTY(&LC.nodes)) {
		struct lc_node *u;
		
		u = __dijkstra_find_lowest_cost_node();
		
		if (!u) {
			DEBUG("Dijkstra Error? No lowest cost node u!\n");
			continue;
		}

		tbl_detach(&LC.nodes, &u->l);
		
		/* Add to S */
		tbl_add(&S, &u->l, crit_none);

		tbl_do_for_each(&LC.links, u, do_relax);
	}
	/* Restore the nodes in the LC graph */
	memcpy(&LC.nodes, &S, sizeof(S));
	
	/* Set currently calculated source */
	LC.src = src_node;
}

struct dsr_srt *dsr_rtc_find(struct in_addr src, struct in_addr dst)
{
	struct dsr_srt *srt = NULL;
	struct lc_node *dst_node;

	
	write_lock_bh(&LC.lock);
	
	if (!LC.src || LC.src->addr.s_addr != src.s_addr)
		__dijkstra(src);
	
	dst_node = __tbl_find(&LC.nodes, &dst, crit_addr);

	if (!dst_node) {
		write_unlock_bh(&LC.lock);
		return NULL;
	}
	
	DEBUG("Hops to %s: %u\n", print_ip(dst.s_addr), dst_node->hops);
	
	if (dst_node->hops != 0) {
		struct lc_node *n;
		int i = 0;
		
		srt = kmalloc(sizeof(srt) * dst_node->hops, GFP_ATOMIC);
		
		srt->dst = dst;
		srt->src = src;
		srt->laddrs = dst_node->hops * sizeof(srt->dst);
		
		for (n = dst_node->pred; n != n->pred; n = n->pred) {
			srt->addrs[i] = n->addr;
			i++;				
		}
		if (i != srt->laddrs)
			DEBUG("hop count ERROR!!!\n");
	}
	write_unlock_bh(&LC.lock);
	return srt;
}

int dsr_rtc_add(struct dsr_srt *srt, unsigned long time, unsigned short flags)
{
	int i, n, links = 0;
	struct in_addr addr1, addr2;
	
	
	if (!srt)
		return -1;

	n = srt->laddrs / sizeof(struct in_addr);
	
	addr1 = srt->src;
	
	write_lock_bh(&LC.lock);
	for (i = 0; i < n; i++) {
		addr2 = srt->addrs[i];
		
		lc_link_add(addr1, addr2, 0, 1);
		links++;
		
		if (srt->flags & SRT_BIDIR) {
			lc_link_add(addr2, addr1, 0, 1);
			links++;
		}
		addr1 = addr2;
	}
	addr2 = srt->dst;
	
	lc_link_add(addr1, addr2, 0, 1);
	links++;
	
	if (srt->flags & SRT_BIDIR) {
		lc_link_add(addr2, addr1, 0, 1);
		links++;
	}
	
	/* Set currently calculated source to NULL since we have modified the
	 * link cache. */
	LC.src = NULL;
	
	write_unlock_bh(&LC.lock);
	return links;
}

int __dsr_rtc_del(struct dsr_srt *srt)
{
	int i, n, links = 0;
	struct in_addr addr1, addr2;
	
	
	if (!srt)
		return -1;

	n = srt->laddrs / sizeof(struct in_addr);
	
	addr1 = srt->src;
	
	write_lock_bh(&LC.lock);

	for (i = 0; i < n; i++) {
		addr2 = srt->addrs[i];
		
		lc_link_del(addr1, addr2);
		links++;
		
		if (srt->flags & SRT_BIDIR) {
			lc_link_del(addr2, addr1);
			links++;
		}
		addr1 = addr2;
	}
	addr2 = srt->dst;
	
	lc_link_del(addr1, addr2);
	links++;
	
	if (srt->flags & SRT_BIDIR) {
		lc_link_del(addr2, addr1);
		links++;
	}
	/* Set currently calculated source to NULL since we have modified the
	 * link cache. */
	LC.src = NULL;
	
	write_unlock_bh(&LC.lock);
	return links;
}

void lc_flush(void)
{
	write_lock_bh(&LC.lock);
#ifdef LC_TIMER
	if (timer_pending(&LC.timer))
		del_timer(&LC.timer);
#endif
	tbl_flush(&LC.links, NULL);
	tbl_flush(&LC.nodes, NULL);
	
	LC.src = NULL;
	
	write_unlock_bh(&LC.lock);
}

void dsr_rtc_flush(void)
{
	lc_flush();
}

int __init lc_init(void)
{
	/* Initialize Graph */
	INIT_TBL(&LC.links, LC_LINKS_MAX);
	INIT_TBL(&LC.nodes, LC_NODES_MAX);
	LC.lock = RW_LOCK_UNLOCKED;
	LC.src = NULL;

	proc_net_create(LC_PROC_NAME, 0, lc_proc_info);
	return 0;
}

void __exit lc_cleanup(void)
{
	lc_flush();
	proc_net_remove(LC_PROC_NAME);
}

EXPORT_SYMBOL(dsr_rtc_add);
/* EXPORT_SYMBOL(dsr_rtc_del); */
EXPORT_SYMBOL(dsr_rtc_find);
EXPORT_SYMBOL(dsr_rtc_flush);

module_init(lc_init);
module_exit(lc_cleanup);
