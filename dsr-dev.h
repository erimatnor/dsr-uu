#ifndef _DSR_DEV_H
#define _DSR_DEV_H

#include <linux/netdevice.h>

#include "dsr.h"
#include "dsr-pkt.h"

int dsr_dev_xmit(struct dsr_pkt *dp);
int dsr_dev_deliver(struct dsr_pkt *dp);

int __init dsr_dev_init(char *ifname);
void __exit dsr_dev_cleanup(void);

#endif
