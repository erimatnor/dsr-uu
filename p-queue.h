/*****************************************************************************
 *
 * Copyright (C) 2001 Uppsala University.
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
 * Author: Erik Nordström, <erik.nordstrom@it.uu.se>
 * 
 *****************************************************************************/
#ifndef _P_QUEUE_H
#define _P_QUEUE_H

#define P_QUEUE_DROP 1
#define P_QUEUE_SEND 2

int p_queue_find(__u32 daddr);
int p_queue_enqueue_packet(struct sk_buff *skb, 
			       int (*okfn)(struct sk_buff *));
int p_queue_set_verdict(int verdict, __u32 daddr);
void p_queue_flush(void);
int p_queue_init(void);
void p_queue_fini(void);

#endif /* _P_QUEUE_H */
