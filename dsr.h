#ifndef _DSR_H
#define _DSR_H

#include <asm/byteorder.h>
#include <linux/types.h>
#include <linux/netdevice.h>
#include <linux/in.h>
#include <linux/skbuff.h>
#include <linux/ip.h>


#define DSR_BROADCAST ((unsigned long int) 0xffffffff)
#define IPPROTO_DSR 168 /* Is this correct? */
#define IP_HDR_LEN 20

/* typedef struct dsr_rreq_opt_opt struct dsr_rreq_opt; */
/* typedef struct dsr_rrep_opt struct dsr_rrep_opt; */
/* typedef struct dsr_srt_opt dsr_srt_opt_t; */

/* Internal representation of a packet. For portability */
struct dsr_pkt {
	struct in_addr src;   /* IP level data */
	struct in_addr dst;
       	struct in_addr nxt_hop;
	union {
		struct iphdr *iph;
		char *raw;
	} nh;
	union {
		struct dsr_opt_hdr *opth;
		char *raw;
	} dh;   
	int dsr_opts_len;
	struct dsr_srt_opt *srt_opt;
	struct dsr_rreq_opt *rreq_opt;
	struct dsr_rrep_opt *rrep_opt;   
	char *data;           /* Packet data (IP not included)*/
	int data_len;
	struct dsr_srt *srt; /* Source route */
	/* These are pointers into the dsr options in the dsr header */
};

/* Packet actions: */
/* Actions to take after processing source route option: */
#define DSR_PKT_NONE           0
#define DSR_PKT_ERROR          0x1
#define DSR_PKT_SRT_REMOVE     0x2
#define DSR_PKT_FORWARD        0x4
#define DSR_PKT_DELIVER        0x8
#define DSR_PKT_SEND_ICMP      0x10
#define DSR_PKT_DROP           0x20
#define DSR_PKT_SEND_RREP      0x40
#define DSR_PKT_SEND_BUFFERED  0x80
#define DSR_PKT_FORWARD_RREQ   0x100

/* Local device info (shared data) */
//extern struct netdev_info ldev_info;  /* defined in dsr-dev.c */
enum {
	BroadCastJitter,
	RouteCacheTimeout,
	SendBufferTimeout,
	RequestTableSize,
	RequestTableIds,
	MaxRequestRexmt,
	RequestPeriod,
	NonpropRequestTimeout,
	RexmtBufferSize,
	MaintHoldoffTime,
	MaxMaintRexmt,
	TryPassiveAcks,
	PassiveAckTimeout,
	GratReplyHoldOff,
	MAX_SALVAGE_COUNT,
	PARAMS_MAX,
};

static struct { 
	const char *name; 
	const int val; 
} params_def[PARAMS_MAX] = {
	{ "BroadCastJitter", 10 },
	{ "RouteCacheTimeout", 300000 },
	{ "SendBufferTimeout", 30000 },
	{ "RequestTableSize", 64 },
	{ "RequestTableIds", 16 },
	{ "MaxRequestRexmt", 5 },
	{ "RequestPeriod", 10 },
	{ "NonpropRequestTimeout", 30 },
	{ "RexmtBufferSize", 50 },
	{ "MaintHoldoffTime", 250000 },
	{ "MaxMaintRexmt", 2 },
	{ "TryPassiveAcks", 1 },
	{ "PassiveAckTimeout", 100000 },
	{ "GratReplyHoldOff", 1 },
	{ "MAX_SALVAGE_COUNT", 15 }
};

struct dsr_node {
	struct net_device *dev;
	struct net_device *slave_dev;
	struct net_device_stats	stats;
	struct in_addr ifaddr;
	struct in_addr bcaddr;
	spinlock_t lock;
	int params[PARAMS_MAX];
};

extern struct dsr_node *dsr_node;

static inline const int get_param(int index)
{
	int param = 0;
	
	if (dsr_node) {
		spin_lock(&dsr_node->lock);
		param = dsr_node->params[index];
		spin_unlock(&dsr_node->lock);
	}
	return param;  
}
static inline const int set_param(int index, int val)
{
	
	if (dsr_node) {
		spin_lock(&dsr_node->lock);
		dsr_node->params[index] = val;
		spin_unlock(&dsr_node->lock);
	} else
		return -1;

	return val;  
}
static inline const int get_slave_dev_ifindex(void)
{
	int ifindex = -1;
	
	if (dsr_node) {
		spin_lock(&dsr_node->lock);
		if (dsr_node->slave_dev)
			ifindex = dsr_node->slave_dev->ifindex;
		spin_unlock(&dsr_node->lock);
	}
	return ifindex;  
}

#define PARAM(name) (get_param(name))

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

static inline void dsr_node_lock(struct dsr_node *dnode)
{
	spin_lock(&dnode->lock);
}

static inline void dsr_node_unlock(struct dsr_node *dnode)
{
	spin_unlock(&dnode->lock);
}


static inline struct in_addr my_addr(void)
{
	static struct in_addr my_addr;
	if (dsr_node) {
		spin_lock(&dsr_node->lock);
		my_addr = dsr_node->ifaddr;
		spin_unlock(&dsr_node->lock);
	}
	return my_addr;
}
static inline unsigned long time_add_msec(unsigned long msecs)
{
	unsigned long long t = msecs * HZ / 1000;

	return jiffies + t;
}
/* struct dsr_pkt *dsr_pkt_alloc(int size); */
/* void dsr_pkt_free(struct dsr_pkt *dp); */

#endif /* _DSR_H */
