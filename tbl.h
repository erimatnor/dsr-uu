#ifndef _TBL_H
#define _TBL_H

#include <linux/list.h>
#include <linux/spinlock.h>

#define TBL_FIRST(tbl) (tbl)->head.next
#define TBL_EMPTY(tbl) (TBL_FIRST(tbl) == &(tbl)->head)
#define tbl_is_first(tbl, e) (&e->l == TBL_FIRST(tbl))

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

#define INIT_TBL(ptr, max_length) do { \
        (ptr)->head.next = (ptr)->head.prev = &((ptr)->head); \
        (ptr)->len = 0; (ptr)->max_len = max_length; \
        (ptr)->lock = RW_LOCK_UNLOCKED; \
} while (0)

/* Criteria function should return 1 if the criteria is fulfilled or 0 if not
 * fulfilled */
typedef int (*criteria_t) (void *elm, void *data);
typedef int (*do_t) (void *elm, void *data);

static inline int crit_none(void *foo, void *bar)
{
	return 1;
}

/* Functions prefixed with "__" are unlocked, the others are safe. */

static inline int __tbl_add(struct tbl *t, struct list_head *l, criteria_t crit)
{
	int len;

	if (t->len >= t->max_len) {
		printk(KERN_WARNING "Max list len reached\n");
		return -ENOSPC;
	}
    
	if (list_empty(&t->head)) {
		list_add(l, &t->head);
	} else {
		struct list_head *pos;
	
		list_for_each(pos, &t->head) {
			
			if (crit(pos, l)) 
				break;
		}
		list_add(l, pos->prev);
	}

	len = ++t->len;


	return len;
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


static inline void *__tbl_detach(struct tbl *t, struct list_head *l)
{
	int len;

	if (TBL_EMPTY(t)) 
		return NULL;
	
	list_del(l);
	
	len = --t->len;
	
	return l;
}

static inline int __tbl_del(struct tbl *t, struct list_head *l)
{

	if (!__tbl_detach(t, l)) 
		return -1;
	
	kfree(l);
		
	return 1;
}

static inline void __tbl_do_for_each(struct tbl *t, void *data, do_t func)
{
	struct list_head *pos;
    
	list_for_each(pos, &t->head)
		func(pos, data);
}

static inline void tbl_do_for_each(struct tbl *t, void *data, do_t func)
{
	write_lock_bh(&t->lock);
	__tbl_do_for_each(t, data, func);
	write_unlock_bh(&t->lock);
}

static inline void *tbl_find_detach(struct tbl *t, void *id, criteria_t crit)
{
	struct list_head *e;

	read_lock_bh(&t->lock);

	e = __tbl_find(t, id, crit);
	
	if (!e) {
		read_unlock_bh(&t->lock);
		return NULL;
	}
	list_del(e);
	t->len--;

	read_unlock_bh(&t->lock);
	
	return e;
}

static inline void *tbl_detach_first(struct tbl *t)
{
	struct list_head *e;

	read_lock_bh(&t->lock);

	e = TBL_FIRST(t);
	
	if (!e) {
		read_unlock_bh(&t->lock);
		return NULL;
	}
	list_del(e);
	t->len--;

	read_unlock_bh(&t->lock);
	
	return e;
}

static inline int tbl_add(struct tbl *t, struct list_head *l, criteria_t crit)
{
	int len;

	write_lock_bh(&t->lock);
	len = __tbl_add(t, l, crit);	
	write_unlock_bh(&t->lock);
	return len;
}

static inline int tbl_del(struct tbl *t, struct list_head *l)
{
	int res;
	
	write_lock_bh(&t->lock);

	res = tbl_del(t, l);
	
	write_unlock_bh(&t->lock);
	
	return res;
}


static inline int tbl_find_del(struct tbl *t, void *id, criteria_t crit)
{
	struct list_head *pos, *tmp;
	int n = 0;

	write_lock_bh(&t->lock);
	
	list_for_each_safe(pos, tmp, &t->head) {
		if (crit(pos, id)) {
			list_del(pos);
			t->len--;
			n++;
			kfree(pos);
		}
	}
	write_unlock_bh(&t->lock);
    
	return n;
}

static inline int in_tbl(struct tbl *t, void *id, criteria_t crit)
{
	read_lock_bh(&t->lock);
	if (__tbl_find(t, id, crit)) {
		read_unlock_bh(&t->lock);
		return 1;
	}
	read_unlock_bh(&t->lock);
	return 0;
}

static inline void tbl_flush(struct tbl *t, do_t at_flush)
{
	struct list_head *pos, *tmp;
	
	write_lock_bh(&t->lock);
	
	list_for_each_safe(pos, tmp, &t->head) {
		list_del(pos);
		
		if (at_flush)
			at_flush(pos, NULL);
		
		t->len--;
		kfree(pos);
	}
	write_unlock_bh(&t->lock);
}

#endif /* _TBL_H */
