#ifndef _P_QUEUE_H
#define _P_QUEUE_H

#include "dsr.h"

#define P_QUEUE_DROP 1
#define P_QUEUE_SEND 2

int p_queue_find(__u32 daddr);
int p_queue_enqueue_packet(struct sk_buff *, 
			   int (*okfn)(struct sk_buff *));
int p_queue_set_verdict(int verdict, unsigned long daddr);
void p_queue_flush(void);
int p_queue_init(void);
void p_queue_cleanup(void);

#endif /* _P_QUEUE_H */
