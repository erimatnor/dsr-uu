/*****************************************************************************
 *
 * Copyright (C) 2001 Uppsala University and Ericsson AB.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Authors: Erik Nordström, <erik.nordstrom@it.uu.se>
 *
 *****************************************************************************/
#ifndef _LIST_H
#define _LIST_H

/* Simple linked list inspired from the Linux kernel list implementation */
typedef struct list {
	struct list *prev, *next;
} list_t;

#define LIST_NULL -1
#define LIST_SUCCESS 1

#define LIST_INIT_HEAD(name) { &(name), &(name) }

#define LIST(name) list_t name = LIST_INIT_HEAD(name)

#define INIT_LIST(h) do { \
	(h)->next = (h); (h)->prev = (h); \
} while (0)

#define INIT_LIST_ELM(le) do { \
	(le)->next = NULL; (le)->prev = NULL; \
} while (0)

static inline int listelm_detach(list_t * prev, list_t * next)
{
	next->prev = prev;
	prev->next = next;

	return LIST_SUCCESS;
}

static inline int listelm_add(list_t * le, list_t * prev, list_t * next)
{
	prev->next = le;
	le->prev = prev;
	le->next = next;
	next->prev = le;
	
	return LIST_SUCCESS;
}

static inline int list_add(list_t * head, list_t * le)
{
	
	if (!head || !le)
		return LIST_NULL;

	listelm_add(le, head, head->next);
	
	return LIST_SUCCESS;
}

static inline int list_add_tail(list_t * head, list_t * le)
{

	if (!head || !le)
		return LIST_NULL;

	listelm_add(le, head->prev, head);

	return LIST_SUCCESS;
}

static inline int list_detach(list_t * le)
{
	if (!le)
		return LIST_NULL;

	listelm_detach(le->prev, le->next);

	le->next = le->prev = NULL;

	return LIST_SUCCESS;
}

#define list_for_each(curr, head) \
        for (curr = (head)->next; curr != (head); curr = curr->next)

#define list_for_each_safe(pos, tmp, head) \
        for (pos = (head)->next, tmp = pos->next; pos != (head); \
                pos = tmp, tmp = pos->next)

#define list_empty(head) ((head) == (head)->next)

#define list_first(head) ((head)->next)

#define list_unattached(le) ((le)->next == NULL && (le)->prev == NULL)

#define list_del(le) list_detach(le)

#endif /* _LIST_H */
