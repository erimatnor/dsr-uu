#include <linux/proc_fs.h>
#include <linux/timer.h>
#include <linux/module.h>

#undef DEBUG
#include "dsr-rtc.h"
#include "dsr-srt.h"
#include "tbl.h"
#include "debug.h"


#define DEBUG(f, args...)

#define LC_NODES_MAX 500
#define LC_LINKS_MAX 100 /* TODO: Max links should be calculated from Max
			  * nodes */
#define LC_PROC_NAME "dsr_lc"
#define LC_COST_INF UINT_MAX
#define LC_HOPS_INF UINT_MAX

#define LC_TIMER

#ifdef LC_TIMER
#define LC_GARBAGE_COLLECT_INTERVAL 5 /* Seconds */
#endif

MODULE_AUTHOR("erik.nordstrom@it.uu.se");
MODULE_DESCRIPTION("DSR link cache kernel module");
MODULE_LICENSE("GPL");

struct lc_node {
	list_t l;
	struct in_addr addr;
	unsigned int links;
	unsigned int cost; /* Cost estimate from source when running Dijkstra */
	unsigned int hops;  /* Number of hops from source. Used to get the
			     * length of the source route to allocate. Same as
			     * cost if cost is hops. */
	struct lc_node *pred; /* predecessor */
};

struct lc_link {
	list_t l;
	struct lc_node *src, *dst;
	int status;
	unsigned int cost;
	unsigned long expire;
};

struct lc_graph {
	struct tbl nodes;
	struct tbl links;
	struct lc_node *src;
	rwlock_t lock;
#ifdef LC_TIMER
	DSRTimer timer;
#endif
};

static struct lc_graph LC;

static inline void __lc_link_del(struct lc_link *link)
{
	/* Also free the nodes if they lack other links */
	if (--link->src->links == 0)
		__tbl_del(&LC.nodes, &link->src->l);

	if (--link->dst->links == 0)
		__tbl_del(&LC.nodes, &link->dst->l);

	__tbl_del(&LC.links, &link->l);
}

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
	    p->dst->addr.s_addr == q->dst.s_addr)
		return 1;
	return 0;
}

static inline int crit_expire(void *pos, void *data)
{
	struct lc_link *link = pos;

	if (link->expire < jiffies) {
		__lc_link_del(link);
		return 1;
	}
	return 0;
}

static inline int do_lowest_cost(void *pos, void *cheapest)
{
	struct lc_node *n = pos;
	struct {
		struct lc_node *cheapest;
	} *d = cheapest;
	
	if (!d->cheapest || n->cost < d->cheapest->cost) {
		DEBUG("New lowest cost %lu, %s\n", n->cost, print_ip(n->addr));
		d->cheapest = n;		
	}
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
		
		if ((u->cost + w) < v->cost) {
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
	
	if (!a || !n)
		return -1;

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

#ifdef LC_TIMER

static void lc_garbage_collect_set(void);

static void lc_garbage_collect(unsigned long data)
{
	DSR_WRITE_LOCK(&LC.lock);
	
	tbl_do_for_each(&LC.links, NULL, crit_expire);

	if (!TBL_EMPTY(&LC.links))
		lc_garbage_collect_set();
	
	DSR_WRITE_UNLOCK(&LC.lock);
}

static void lc_garbage_collect_set(void)
{
	LC.timer.function = lc_garbage_collect;
	LC.timer.data = 0;
	LC.timer.expires = jiffies + (LC_GARBAGE_COLLECT_INTERVAL * HZ);

	add_timer(&LC.timer);
}

#endif /* LC_TIMER */


static inline struct lc_node *lc_node_create(struct in_addr addr)
{
	struct lc_node *n;

	n = MALLOC(sizeof(struct lc_node), GFP_ATOMIC);
	
	if (!n)
		return NULL;
	
	memset(n, 0, sizeof(struct lc_node));
	n->addr = addr;
	n->links = 0;
	n->cost = LC_COST_INF;
	n->pred = NULL;

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
			     unsigned long timeout, int status, int cost)
{	
	struct lc_link *link;
	int res;

	if (!src || !dst)
		return -1;
	
	link = __lc_link_find(src->addr, dst->addr);
	
	if (!link) {
		link = MALLOC(sizeof(struct lc_link), GFP_ATOMIC);
		
		if (!link) {
			DEBUG("Could not allocate link\n");
			return -1;
		}
		memset(link, 0, sizeof(struct lc_link));

		DEBUG("Adding Link %s <-> %s cost=%d\n", 
		      print_ip(src->addr), 
		      print_ip(dst->addr), 
		      cost);
		
		__tbl_add_tail(&LC.links, &link->l);
		
		link->src = src;
		link->dst = dst;
		src->links++;
		dst->links++;

		res = 1;
	} else
		res = 0;
	
      	link->status = status;
	link->cost = cost;
	link->expire = jiffies + (timeout / 1000 * HZ);
	
	return res;
}

int lc_link_add(struct in_addr src, struct in_addr dst, 
		unsigned long timeout, int status, int cost)
{
	struct lc_node *sn, *dn;
	int res;
	
	DSR_WRITE_LOCK(&LC.lock);
	
	sn = __tbl_find(&LC.nodes, &src, crit_addr);

	if (!sn) {
		sn = lc_node_create(src);

		if (!sn) {
			DEBUG("Could not allocate nodes\n");
			DSR_WRITE_UNLOCK(&LC.lock);
			return -1;
		}
		__tbl_add_tail(&LC.nodes, &sn->l);
		
	}

	dn = __tbl_find(&LC.nodes, &dst, crit_addr);

	if (!dn) {
		dn = lc_node_create(dst);
		if (!dn) {
			DEBUG("Could not allocate nodes\n");
			DSR_WRITE_UNLOCK(&LC.lock);
			return -1;
		}
		__tbl_add_tail(&LC.nodes, &dn->l);
	}
	
	res = __lc_link_tbl_add(sn, dn, timeout, status, cost);
		
	if (res) {	
#ifdef LC_TIMER
		if (!timer_pending(&LC.timer))
			lc_garbage_collect_set();
#endif
		
	} else if (res < 0)
		DEBUG("Could not add new link\n");
	
	DSR_WRITE_UNLOCK(&LC.lock);

	return 0;
}


int lc_link_del(struct in_addr src, struct in_addr dst)
{
	struct lc_link *link;
	int res = 1;

	DSR_WRITE_LOCK(&LC.lock);

	link = __lc_link_find(src, dst);

	if (!link) {
		res = -1;
		goto out;
	}	
	
	__lc_link_del(link);

	/* Assume bidirectional links for now */
	link = __lc_link_find(dst, src);

	if (!link) {
		res = -1;
		goto out;
	}	
	
	__lc_link_del(link);

 out:
	LC.src = NULL;
	DSR_WRITE_UNLOCK(&LC.lock);
	
	return res;
}

static inline void __dijkstra_init_single_source(struct in_addr src)
{
	DEBUG("Initializing source\n");
	__tbl_do_for_each(&LC.nodes, &src, do_init);
}

static inline struct lc_node *__dijkstra_find_lowest_cost_node(void)
{
	struct {
		struct lc_node *cheapest;
	} data = { NULL };

	__tbl_do_for_each(&LC.nodes, &data, do_lowest_cost);
	
	return data.cheapest;
}
/*
  relax( Node u, Node v, double w[][] )
      if d[v] > d[u] + w[u,v] then
          d[v] := d[u] + w[u,v]
          pi[v] := u

*/
static void __lc_move(struct tbl *to, struct tbl *from)
{
	while (!TBL_EMPTY(from)) {
		struct lc_node *n;
		
		n = tbl_detach_first(from);
		
		tbl_add_tail(to, &n->l);
	}
}

static void __dijkstra(struct in_addr src)
{	
	TBL(S, LC_NODES_MAX);
	struct lc_node *src_node, *u;
		
	if (TBL_EMPTY(&LC.nodes)) {
		DEBUG("No nodes in Link Cache\n");
		return;
	}

	__dijkstra_init_single_source(src);
	
	src_node = __tbl_find(&LC.nodes, &src, crit_addr);

	if (!src_node) 
		return;
	
	while ((u = __dijkstra_find_lowest_cost_node())) {
		
		tbl_detach(&LC.nodes, &u->l);
		
		/* Add to S */
		tbl_add_tail(&S, &u->l);
		
		tbl_do_for_each(&LC.links, u, do_relax);
	}
	
	/* Restore the nodes in the LC graph */
	/* memcpy(&LC.nodes, &S, sizeof(S)); */
/* 	LC.nodes = S; */
	__lc_move(&LC.nodes, &S);

	/* Set currently calculated source */
	LC.src = src_node;
}

struct dsr_srt *dsr_rtc_find(struct in_addr src, struct in_addr dst)
{
	struct dsr_srt *srt = NULL;
	struct lc_node *dst_node;
	
	DSR_WRITE_LOCK(&LC.lock);
	
/* 	if (!LC.src || LC.src->addr.s_addr != src.s_addr) */
	__dijkstra(src);
	
	dst_node = __tbl_find(&LC.nodes, &dst, crit_addr);

	if (!dst_node) {
		DEBUG("%s not found\n", print_ip(dst));
		goto out;
	}
	
	DEBUG("Hops to %s: %u\n", print_ip(dst), dst_node->hops);
	
	if (dst_node->cost != LC_COST_INF) {
		struct lc_node *n;
		int k = (dst_node->hops - 1);
		int i = 0;
		
		srt = MALLOC(sizeof(srt) * k, GFP_ATOMIC);
		
		if (!srt) {
			DEBUG("Could not allocate source route!!!\n");
			goto out;
		}

		srt->dst = dst;
		srt->src = src;		
		srt->laddrs = k * sizeof(srt->dst);

		if (!dst_node->pred) {
			FREE(srt);
			srt = NULL;
			DEBUG("Predecessor was NULL\n");
			goto out;			
		}
			
		/* Fill in the source route by traversing the nodes starting
		 * from the destination predecessor */
		for (n = dst_node->pred; n && (n != n->pred) && n->pred; n = n->pred) {
			srt->addrs[k-i-1] = n->addr;
			i++;
		}

		if ((i + 1) != dst_node->hops)
			DEBUG("hop count ERROR i+1=%d hops=%d!!!\n", i + 1, 
			       dst_node->hops);
	}
 out:
	DSR_WRITE_UNLOCK(&LC.lock);

	return srt;
}

int dsr_rtc_add(struct dsr_srt *srt, unsigned long timeout, unsigned short flags)
{
	int i, n, links = 0;
	struct in_addr addr1, addr2;
	
        timeout = 300000;
	
	if (!srt)
		return -1;

	n = srt->laddrs / sizeof(struct in_addr);
	
	addr1 = srt->src;
	
	for (i = 0; i < n; i++) {
		addr2 = srt->addrs[i];
		
		lc_link_add(addr1, addr2, timeout, 0, 1);
		links++;
		
		if (srt->flags & SRT_BIDIR) {
			lc_link_add(addr2, addr1, timeout, 0, 1);
			links++;
		}
		addr1 = addr2;
	}
	addr2 = srt->dst;
	
	lc_link_add(addr1, addr2, timeout, 0, 1);
	links++;
	
	if (srt->flags & SRT_BIDIR) {
		lc_link_add(addr2, addr1, timeout, 0, 1);
		links++;
	}
	
	return links;
}
int dsr_rtc_del(struct in_addr src, struct in_addr dst)
{
	return 0;
}


void lc_flush(void)
{
	DSR_WRITE_LOCK(&LC.lock);
#ifdef LC_TIMER
	if (timer_pending(&LC.timer))
		del_timer(&LC.timer);
#endif
	tbl_flush(&LC.links, NULL);
	tbl_flush(&LC.nodes, NULL);
	
	LC.src = NULL;
	
	DSR_WRITE_UNLOCK(&LC.lock);
}

void dsr_rtc_flush(void)
{
	lc_flush();
}

static char *print_hops(unsigned int hops)
{
	static char c[18];
	
	if (hops == LC_HOPS_INF)
		sprintf(c, "INF");
	else
		sprintf(c, "%u", hops);
	return c;
}

static char *print_cost(unsigned int cost)
{
	static char c[18];
	
	if (cost == LC_COST_INF)
		sprintf(c, "INF");
	else
		sprintf(c, "%u", cost);
	return c;
}
static int lc_print(char *buf)
{
	list_t *pos;
	int len = 0;
	
	DSR_READ_LOCK(&LC.lock);
    
	len += sprintf(buf, "# %-15s %-15s %-4s Timeout\n", "Src Addr", "Dst Addr", "Cost");

	list_for_each(pos, &LC.links.head) {
		struct lc_link *link = (struct lc_link *)pos;
		
		len += sprintf(buf+len, "  %-15s %-15s %-4u %lu\n", 
			       print_ip(link->src->addr),
			       print_ip(link->dst->addr),
			       link->cost,
			       (link->expire - jiffies) / HZ);
	}
    
	len += sprintf(buf+len, "\n# %-15s %-4s %-4s %-5s %12s %12s\n", "Addr", "Hops", "Cost", "Links", "This", "Pred");

	list_for_each(pos, &LC.nodes.head) {
		struct lc_node *n = (struct lc_node *)pos;
		
		len += sprintf(buf+len, "  %-15s %4s %4s %5u %12lX %12lX\n", 
			       print_ip(n->addr),
			       print_hops(n->hops),
			       print_cost(n->cost),
			       n->links,
			       (unsigned long)n,
			       (unsigned long)n->pred);
	}
    
	DSR_READ_UNLOCK(&LC.lock);
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

int __init lc_init(void)
{
	/* Initialize Graph */
	INIT_TBL(&LC.links, LC_LINKS_MAX);
	INIT_TBL(&LC.nodes, LC_NODES_MAX);
	LC.lock = RW_LOCK_UNLOCKED;
	LC.src = NULL;
#ifdef LC_TIMER
	init_timer(&LC.timer);
#endif
	proc_net_create(LC_PROC_NAME, 0, lc_proc_info);
	return 0;
}

void __exit lc_cleanup(void)
{
	lc_flush();
	proc_net_remove(LC_PROC_NAME);
}

EXPORT_SYMBOL(dsr_rtc_add);
EXPORT_SYMBOL(dsr_rtc_find);
EXPORT_SYMBOL(dsr_rtc_flush);
EXPORT_SYMBOL(lc_link_del);

module_init(lc_init);
module_exit(lc_cleanup);
