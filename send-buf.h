#ifndef _SEND_BUF_H
#define _SEND_BUF_H

#include "dsr.h"

#define SEND_BUF_DROP 1
#define SEND_BUF_SEND 2

int send_buf_find(__u32 daddr);
int send_buf_enqueue_packet(struct dsr_pkt *dp, int (*okfn)(struct dsr_pkt *));
int send_buf_set_verdict(int verdict, unsigned long daddr);
int send_buf_flush(void);
int send_buf_init(void);
void send_buf_cleanup(void);

#endif /* _SEND_BUF_H */
