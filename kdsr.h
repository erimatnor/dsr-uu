#ifndef _KDSR_H
#define _KDSR_H

#include <linux/skbuff.h>
#include <linux/netdevice.h>

//struct sk_buff *kdsr_pkt_alloc(unsigned int size, struct net_device *dev);
int dsr_rreq_send(u_int32_t target);
int dsr_rrep_send(dsr_srt_t *srt);

#endif /* _KDSR_H */
