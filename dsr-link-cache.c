#include <linux/proc_fs.h>

#include "dsr-rtc.h"
#include "tbl.h"
#include "debug.h"

#define LC_NODES_MAX 500
#define LC_LINKS_MAX 100 /* TODO: Max links should be calculated from Max
			  * nodes */
#define LC_PROC_NAME "dsr_lc"
#define COST_INF UINT_MAX


/* TBL(node_tbl, LC_NODES_MAX); */
/* TBL(link_tbl, LC_LINKS_MAX); */

struct lc_node {
	struct list_head l;
	struct in_addr addr;
	int links;
	unsigned int cost; /* Cost estimate from source when running Dijkstra */
	struct lc_node *pred; /* predecessor */
};

struct lc_link {
	struct list_head l;
	struct lc_node *src, *dst;
	unsigned long time;
	int status;
	unsigned int cost;
};

struct lc_graph {
	struct tbl nodes;
	struct tbl links;
	struct lc_node **S;
	rwlock_t lock;
};

static struct lc_graph G;

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
	struct {
		int cost;
		struct lc_node *node;
	} *c = cheapest;
	
	if (c->cost != COST_INF && c->cost != 0 && n->cost < c->cost) {
		c->cost = n->cost;
		c->node = n;
	}	
	return 0;
}

static inline int do_relax(void *pos, void *nodes)
{	
	struct lc_link *link = pos;
	struct {
		struct lc_node *u, *v;
	} *n = nodes;

	/* If u and v have a link between them, update cost if cheaper */
	if (link->src == n->v && link->dst == n->u) {
		unsigned int w = link->cost;
		
		if (n->v->cost > (n->u->cost + w)) {
			n->v->cost = n->u->cost + w;
			n->v->pred = n->u;
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
		n->pred = n;
	} else {
		n->cost = COST_INF;
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
	n->cost = COST_INF;
	return n;
};


static inline struct lc_link *__lc_link_find(struct in_addr src, 
					     struct in_addr dst)
{
	struct {
		struct in_addr src, dst;
	} query = { src, dst };
	
	return __tbl_find(&G.links, &query, crit_link_query);
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
		link->src = src;
		link->dst = dst;
		link->status = status;
		link->cost = cost;
	}
	
	__tbl_add(&G.links, &link->l, crit_none);
	
	return 1;
}


int lc_link_add(struct in_addr addr1, struct in_addr addr2, 
		int status, int cost)
{
	struct lc_node *n1, *n2;
	int res;
	
	write_lock_bh(&G.lock);
	
	n1 = __tbl_find(&G.nodes, &addr1, crit_addr);
	n2 = __tbl_find(&G.nodes, &addr2, crit_addr);

	if (!n1) {
		n1 = lc_node_create(addr1);

		if (!n1) {
			DEBUG("Could not allocate nodes\n");
			write_unlock_bh(&G.lock);
			return -1;
		}
		__tbl_add(&G.nodes, &n1->l, crit_none);
		
	}
	if (!n2) {
		n2 = lc_node_create(addr2);
		if (!n2) {
			DEBUG("Could not allocate nodes\n");
			write_unlock_bh(&G.lock);
			return -1;
		}
		__tbl_add(&G.nodes, &n2->l, crit_none);
	}
	
	res = __lc_link_tbl_add(n1, n2, status, cost);
	
	if (!res) {
		DEBUG("Could not add new link\n");
	}
	write_unlock_bh(&G.lock);

	return 0;
}

int lc_link_del(struct in_addr src, struct in_addr dst)
{
	struct lc_link *link;

	
	write_lock_bh(&G.lock);

	link = __lc_link_find(src, dst);

	if (!link) {
		write_unlock_bh(&G.lock);
		return 0;
	}	

	/* Also free the nodes if they lack other links */
	if (--link->src->links < 0)
		__tbl_del(&G.nodes, &link->src->l);

	if (--link->dst->links < 0)
		__tbl_del(&G.nodes, &link->dst->l);

	write_unlock_bh(&G.lock);
	
	return 1;
}
static int lc_print(char *buf)
{
	struct list_head *pos;
	int len = 0;
    
	read_lock_bh(&G.lock);
    
	len += sprintf(buf, "# %-15s %-15s  Cost\n", "Addr", "Addr");

	list_for_each(pos, &G.links.head) {
		struct lc_link *link = (struct lc_link *)pos;
		
		len += sprintf(buf+len, "  %-15s %-15s  %u\n", 
			       print_ip(link->src->addr.s_addr),
			       print_ip(link->dst->addr.s_addr),
			       link->cost);
			       
	}
    
	read_unlock_bh(&G.lock);
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
	__tbl_do_for_each(&G.nodes, &src, do_init);
}

static inline struct lc_node *__dijkstra_find_lowest_cost_node(void)
{
	struct {
		int cost;
		struct lc_node *node;
	} closest = { -1 , NULL };
	
	__tbl_do_for_each(&G.nodes, &closest, do_lowest_cost);
	
	return closest.node;
}
/*
  relax( Node u, Node v, double w[][] )
      if d[v] > d[u] + w[u,v] then
          d[v] := d[u] + w[u,v]
          pi[v] := u

*/
static inline void __dijkstra_relax(struct lc_node *u, struct lc_node *v)
{
	struct {
		struct lc_node *u, *v;
	} neigh = { u, v};
	
	__tbl_do_for_each(&G.links, &neigh, do_relax);
	
}

static int dijkstra(struct in_addr src)
{
	struct list_head *pos;
	int size, i = 0;
	int first = 1;
	
	write_lock_bh(&G.lock);
		
	size = G.nodes.len*sizeof(G.S);

	G.S = kmalloc(size, GFP_ATOMIC);
	
	memset(G.S, 0, size);

	__dijkstra_init_single_source(src);
	
	list_for_each(pos, &G.nodes.head) {
		struct lc_node *v = (struct lc_node *)pos;
		struct lc_node *u;
		
		if (first) {
			u = __tbl_find(&G.nodes, &src, crit_addr);
			first = 0;
		} else
			u = __dijkstra_find_lowest_cost_node();
		
		G.S[i++] = u;

		__dijkstra_relax(u, v);
	}
	
	kfree(G.S);

	write_unlock_bh(&G.lock);

	return 0;
}


int __init lc_init(void)
{
	/* Initialize Graph */
	INIT_TBL(&G.links, LC_LINKS_MAX);
	INIT_TBL(&G.nodes, LC_NODES_MAX);
	G.lock = RW_LOCK_UNLOCKED;

	proc_net_create(LC_PROC_NAME, 0, lc_proc_info);
	return 0;
}

void __exit lc_cleanup(void)
{
	tbl_flush(&G.links, NULL);
	tbl_flush(&G.nodes, NULL);
	proc_net_remove(LC_PROC_NAME);
}
