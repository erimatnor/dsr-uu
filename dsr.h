#ifndef _DSR_H
#define _DSR_H

#ifdef __KERNEL__
#include <asm/byteorder.h>
#include <linux/types.h>
#include <linux/in.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/ip.h>
#include <linux/time.h>
#ifdef KERNEL26
#include <linux/jiffies.h>
#endif
#else
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <endian.h>
#include <netinet/in.h>
#endif /* __KERNEL__ */

#include "dsr-pkt.h"

#define DSR_BROADCAST ((unsigned long int) 0xffffffff)
#define IPPROTO_DSR 168 /* Is this correct? */
#define IP_HDR_LEN 20
#define DSR_OPTS_MAX_SIZE 100 /* This is used to reduce the MTU of the dsr
			       * device so that packets are not too big after
			       * adding the dsr header. A better solution should
			       * probably be found... */

enum {
	BroadCastJitter,
	RouteCacheTimeout,
	SendBufferTimeout,
	SendBufferSize,
	RequestTableSize,
	RequestTableIds,
	MaxRequestRexmt,
	RequestPeriod,
	NonpropRequestTimeout,
	RexmtBufferSize,
	MaintHoldoffTime,
	MaxMaintRexmt,
	UseNetworkLayerAck,
	TryPassiveAcks,
	PassiveAckTimeout,
	GratReplyHoldOff,
	MAX_SALVAGE_COUNT,
	PARAMS_MAX,
};

#define MAINT_BUF_MAX_LEN 100
#define RREQ_TBL_MAX_LEN 64 /* Should be enough */
#define SEND_BUF_MAX_LEN 100
#define RREQ_TLB_MAX_ID 16

static struct { 
	const char *name; 
	const int val; 
} params_def[PARAMS_MAX] = {
	{ "BroadCastJitter", 10 },
	{ "RouteCacheTimeout", 300000 },
	{ "SendBufferTimeout", 30 },
	{ "SendBufferSize", SEND_BUF_MAX_LEN },
	{ "RequestTableSize", RREQ_TBL_MAX_LEN },
	{ "RequestTableIds", RREQ_TLB_MAX_ID },
	{ "MaxRequestRexmt", 5 },
	{ "RequestPeriod", 10 },
	{ "NonpropRequestTimeout", 30 },
	{ "RexmtBufferSize", MAINT_BUF_MAX_LEN },
	{ "MaintHoldoffTime", 250000 },
	{ "MaxMaintRexmt", 2 },
	{ "UseNetworkLayerAck", 1 },
	{ "TryPassiveAcks", 1 },
	{ "PassiveAckTimeout", 100000 },
	{ "GratReplyHoldOff", 1 },
	{ "MAX_SALVAGE_COUNT", 15 }
};

struct dsr_node {
	struct in_addr ifaddr;
	struct in_addr bcaddr;
	int params[PARAMS_MAX];
#ifdef __KERNEL__
	struct net_device *dev;
	struct net_device *slave_dev;
	struct net_device_stats	stats;
	spinlock_t lock;
#endif
};

#ifdef __KERNEL__
#define DSR_SPIN_LOCK(l)    spin_lock(l)
#define DSR_SPIN_UNLOCK(l)  spin_unlock(l)
#define MALLOC(s, p)        kmalloc(s, p)
#define FREE(p)             kfree(p)
#define NSCLASS
#define XMIT(pkt) dsr_dev_xmit(pkt)
#else
#define DSR_SPIN_LOCK(l)
#define DSR_SPIN_UNLOCK(l)
#endif /* __KERNEL__ */



#ifdef __KERNEL__

#define PARAM(name) (get_param(name))

extern struct dsr_node *dsr_node;

static inline const int get_param(int index)
{
	int param = 0;
	
	if (dsr_node) {
		DSR_SPIN_LOCK(&dsr_node->lock);
		param = dsr_node->params[index];
		DSR_SPIN_UNLOCK(&dsr_node->lock);
	}
	return param;  
}
static inline const int set_param(int index, int val)
{
	
	if (dsr_node) {
		DSR_SPIN_LOCK(&dsr_node->lock);
		dsr_node->params[index] = val;
		DSR_SPIN_UNLOCK(&dsr_node->lock);
	} else
		return -1;

	return val;  
}


static inline void dsr_node_init(struct dsr_node *dn)
{
	int i;
	spin_lock_init(&dn->lock);
	dn->dev = NULL;
	dn->slave_dev = NULL;
	
	for (i = 0; i < PARAMS_MAX; i++) {
		dn->params[i] = params_def[i].val;
	}
}

static inline struct in_addr my_addr(void)
{
	static struct in_addr my_addr;
	if (dsr_node) {
		DSR_SPIN_LOCK(&dsr_node->lock);
		my_addr = dsr_node->ifaddr;
		DSR_SPIN_UNLOCK(&dsr_node->lock);
	}
	return my_addr;
}

static inline unsigned long time_add_msec(unsigned long msecs)
{
	struct timespec t;

	t.tv_sec = msecs / 1000;
	t.tv_nsec = (msecs * 1000000) % 1000000000;
	
	return timespec_to_jiffies(&t);
}

static inline const int get_slave_dev_ifindex(void)
{
	int ifindex = -1;
	
	if (dsr_node) {
		DSR_SPIN_LOCK(&dsr_node->lock);
		if (dsr_node->slave_dev)
			ifindex = dsr_node->slave_dev->ifindex;
		DSR_SPIN_UNLOCK(&dsr_node->lock);
	}
	return ifindex;  
}

static inline void dsr_node_lock(struct dsr_node *dnode)
{
	spin_lock(&dnode->lock);
}

static inline void dsr_node_unlock(struct dsr_node *dnode)
{
	spin_unlock(&dnode->lock);
}

#endif

#endif /* _DSR_H */
