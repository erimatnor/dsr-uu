#ifndef _TBL_H
#define _TBL_H

#include <linux/list.h>
#include <linux/spinlock.h>

#define tbl_is_first(tbl, e) (&e->l == tbl->head.next)

struct tbl {
	struct list_head head;
	unsigned int len;
	unsigned int max_len;
	rwlock_t lock;
};

#define TBL_INIT(name, max_len) { LIST_HEAD_INIT(name.head), \
                                 0, \
                                 max_len, \
                                 RW_LOCK_UNLOCKED }

#define TBL(name, max_len) \
	struct tbl name = TBL_INIT(name, max_len)

/* Criteria function should return 1 if the criteria is fulfilled or 0 if not
 * fulfilled */
typedef int (*criteria_t) (void *, void *);

static inline int crit_none(void *foo, void *bar)
{
	return 1;
}

static inline void tbl_flush(struct tbl *t)
{
	struct list_head *pos, *tmp;
	
	write_lock_bh(&t->lock);
	
	list_for_each_safe(pos, tmp, &t->head) {
		list_del(pos);
		t->len--;
		kfree(pos);
	}
	write_unlock_bh(&t->lock);
}

static inline int tbl_add(struct tbl *t, void *e, criteria_t crit)
{
	struct list_head *l = (struct list_head *)e;

	write_lock(&t->lock);
	
	if (t->len >= t->max_len) {
		printk(KERN_WARNING "tbl_add: Max list len reached\n");
		write_unlock_bh(&t->lock);
		return -ENOSPC;
	}
    
/* 	if (list_empty(&t->head)) { */
/* 		list_add(l, &t->head); */
/* 	} else { */
/* 		struct list_head *pos; */
	
/* 		list_for_each(pos, &t->head) { */
/* 			if (crit(pos, l)) */
/* 				break; */
/* 		} */
/* 		list_add(l, pos->prev); */
/* 	} */
/* 	t->len++; */

	write_unlock_bh(&t->lock);

	return 1;
}

static inline void *__tbl_find(struct tbl *t, void *id, criteria_t crit)
{
	struct list_head *pos;
    
	list_for_each(pos, &t->head) {
		if (crit(pos, id))
			return pos;
	}
	return NULL;
}

static inline void *tbl_find(struct tbl *t, void *id, criteria_t crit)
{
	void *e;

	read_lock_bh(&t->lock);
	e = __tbl_find(t, id, crit);
	read_unlock_bh(&t->lock);
	
	return e;
}

static inline int tbl_del(struct tbl *t, void *id, criteria_t crit)
{
	int res;
	struct list_head *e;
  
	write_lock_bh(&t->lock); 
    
	e = __tbl_find(t, id, crit);

	if (e == NULL) {
		res = 0;
	} else {
		list_del(e);
		kfree(e);
		res = 1;
	}
	write_unlock_bh(&t->lock);
    
	return res;
}

#endif /* _TBL_H */
