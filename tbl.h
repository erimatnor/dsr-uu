/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */
/* Copyright (C) Uppsala University
 *
 * This file is distributed under the terms of the GNU general Public
 * License (GPL), see the file LICENSE
 *
 * Author: Erik Nordström, <erikn@it.uu.se>
 */
#ifndef _TBL_H
#define _TBL_H

#include "platform.h"

#define TBL_FIRST(tbl) (tbl)->head.next
#define TBL_EMPTY(tbl) (TBL_FIRST(tbl) == &(tbl)->head)
#define TBL_FULL(tbl) ((tbl)->len >= (tbl)->max_len)
#define tbl_is_first(tbl, e) (&e->l == TBL_FIRST(tbl))

typedef struct list_head list_t;
#define LIST_INIT_HEAD(name) LIST_HEAD_INIT(name)

#define INIT_TBL(ptr, max_length) do {                                  \
                (ptr)->head.next = (ptr)->head.prev = &((ptr)->head);   \
                (ptr)->len = 0; (ptr)->max_len = max_length;            \
                (ptr)->lock = __RW_LOCK_UNLOCKED(&(ptr)->lock);         \
        } while (0)

#define TBL(_name, _max_len)                                      \
	struct tbl _name = {                                      \
                .head = { &(_name).head, &(_name).head },         \
                .len = 0,                                         \
                .max_len = _max_len,                              \
                .lock = __RW_LOCK_UNLOCKED(&(_name).lock)         \
        }

struct tbl {
        list_t head;
        unsigned int len;
        unsigned int max_len;
	rwlock_t lock;
};

/* Criteria function should return 1 if the criteria is fulfilled or 0 if not
 * fulfilled */
typedef int (*criteria_t) (void *elm, void *data);
typedef int (*do_t) (void *elm, void *data);

static inline int crit_none(void *foo, void *bar)
{
	return 1;
}

/* Functions prefixed with "__" are unlocked, the others are safe. */

static inline int tbl_empty(struct tbl *t)
{
	return (TBL_FIRST(t) == &(t)->head);
}

static inline int __tbl_add(struct tbl *t, list_t * l, criteria_t crit)
{
	int len;

	if (t->len >= t->max_len) {
		//printk(KERN_WARNING "Max list len reached\n");
		return -ENOSPC;
	}

	if (list_empty(&t->head)) {
		list_add(l, &t->head);
	} else {
		list_t *pos;

		list_for_each(pos, &t->head) {

			if (crit(pos, l))
				break;
		}
		list_add(l, pos->prev);
	}

	len = ++t->len;

	return len;
}

static inline int __tbl_add_tail(struct tbl *t, list_t * l)
{
	int len;

	if (t->len >= t->max_len) {
		//printk(KERN_WARNING "Max list len reached\n");
		return -ENOSPC;
	}

	list_add_tail(l, &t->head);

	len = ++t->len;

	return len;
}

static inline int tbl_add_tail(struct tbl *t, list_t * l)
{
	int len;
	write_lock_bh(&t->lock);
	len = __tbl_add_tail(t, l);
	write_unlock_bh(&t->lock);
	return len;
}

static inline void *__tbl_find(struct tbl *t, void *id, criteria_t crit)
{
	list_t *pos;

	list_for_each(pos, &t->head) {
		if (crit(pos, id))
			return pos;
	}
	return NULL;
}

static inline void *__tbl_detach(struct tbl *t, list_t * l)
{
	int len;

	if (TBL_EMPTY(t))
		return NULL;

	list_del(l);

	len = --t->len;

	return l;
}

static inline int __tbl_del(struct tbl *t, list_t * l)
{

	if (!__tbl_detach(t, l))
		return -1;

	kfree(l);

	return 1;
}

static inline int __tbl_find_do(struct tbl *t, void *data, do_t func)
{
	list_t *pos, *tmp;

	list_for_each_safe(pos, tmp, &t->head)
	    if (func(pos, data))
		return 1;

	return 0;
}

static inline int tbl_find_do(struct tbl *t, void *data, do_t func)
{
	int res;

	write_lock_bh(&t->lock);
	res = __tbl_find_do(t, data, func);
	write_unlock_bh(&t->lock);

	return res;
}

static inline int __tbl_do_for_each(struct tbl *t, void *data, do_t func)
{
	list_t *pos;
	int res = 0;

	list_for_each(pos, &t->head)
	    res += func(pos, data);

	return res;
}

static inline int tbl_do_for_each(struct tbl *t, void *data, do_t func)
{
	int res;

	write_lock_bh(&t->lock);
	res = __tbl_do_for_each(t, data, func);
	write_unlock_bh(&t->lock);

	return res;
}

static inline void *__tbl_find_detach(struct tbl *t, void *id, criteria_t crit)
{
	list_t *e;

	e = (list_t *) __tbl_find(t, id, crit);

	if (!e) {
		return NULL;
	}

	list_del(e);
	t->len--;

	return e;
}

static inline void *tbl_find_detach(struct tbl *t, void *id, criteria_t crit)
{
	list_t *e;

	write_lock_bh(&t->lock);
	e = __tbl_find_detach(t, id, crit);
	write_unlock_bh(&t->lock);

	return e;
}

static inline void *tbl_detach(struct tbl *t, list_t * l)
{
	void *e;

	write_lock_bh(&t->lock);
	e = __tbl_detach(t, l);
	write_unlock_bh(&t->lock);
	return e;
}

static inline void *__tbl_detach_first(struct tbl *t)
{
	list_t *e;

	if (TBL_EMPTY(t)) {
		return NULL;
	}

	e = TBL_FIRST(t);

	list_del(e);
	t->len--;

	return e;
}

static inline void *tbl_detach_first(struct tbl *t)
{
	list_t *e;

	write_lock_bh(&t->lock);
	e = __tbl_detach_first(t);
	write_unlock_bh(&t->lock);

	return e;
}

static inline int tbl_add(struct tbl *t, list_t * l, criteria_t crit)
{
	int len;

	write_lock_bh(&t->lock);
	len = __tbl_add(t, l, crit);
	write_unlock_bh(&t->lock);
	return len;
}

static inline int tbl_del(struct tbl *t, list_t * l)
{
	int res;

	write_lock_bh(&t->lock);
	res = __tbl_del(t, l);
	write_unlock_bh(&t->lock);

	return res;
}
static inline int tbl_find_del(struct tbl *t, void *id, criteria_t crit)
{
	list_t *e;

	write_lock_bh(&t->lock);

	e = (list_t *) __tbl_find(t, id, crit);

	if (!e) {
		write_unlock_bh(&t->lock);
		return -1;
	}
	list_del(e);
	t->len--;
	kfree(e);

	write_unlock_bh(&t->lock);

	return 1;
}

static inline int tbl_del_first(struct tbl *t)
{
	list_t *l;
	int n = 0;

	l = (list_t *) tbl_detach_first(t);

	kfree(l);

	return n;
}

static inline int __tbl_for_each_del(struct tbl *t, void *id, criteria_t crit)
{
	list_t *pos, *tmp;
	int n = 0;

	list_for_each_safe(pos, tmp, &t->head) {
		if (crit(pos, id)) {
			list_del(pos);
			t->len--;
			n++;
			kfree(pos);
		}
	}

	return n;
}

static inline int tbl_for_each_del(struct tbl *t, void *id, criteria_t crit)
{
	int n;

	write_lock_bh(&t->lock);
	n = __tbl_for_each_del(t, id, crit);
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

static inline void __tbl_flush(struct tbl *t, do_t at_flush)
{
	list_t *pos, *tmp;

	list_for_each_safe(pos, tmp, &t->head) {
		list_del(pos);

		if (at_flush)
			at_flush(pos, NULL);

		t->len--;
		kfree(pos);
	}
}

static inline void tbl_flush(struct tbl *t, do_t at_flush)
{
	write_lock_bh(&t->lock);
	__tbl_flush(t, at_flush);
	write_unlock_bh(&t->lock);
}

#endif				/* _TBL_H */
