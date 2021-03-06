/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */
/* Copyright (C) Uppsala University
 *
 * This file is distributed under the terms of the GNU general Public
 * License (GPL), see the file LICENSE
 *
 * Author: Erik Nordström, <erikn@it.uu.se>
 */
#ifndef _DSR_H
#define _DSR_H

#include "platform.h"
#ifdef __KERNEL__
#include <linux/version.h>
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,20)
#include <linux/config.h>
#endif
#include <asm/byteorder.h>
#include <linux/types.h>
//#include <linux/in.h>
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
//#include <endian.h>
#include <netinet/in.h>
#endif				/* __KERNEL__ */

#include "dsr-pkt.h"
#include "timer.h"

#ifndef NO_GLOBALS

#define DSR_BROADCAST ((unsigned int) 0xffffffff)
#ifdef NS2
#define IPPROTO_DSR PT_DSR
#else
#define IPPROTO_DSR 168		/* Is this correct? */
#endif
#define IP_HDR_LEN 20
#define DSR_OPTS_MAX_SIZE 50	/* This is used to reduce the MTU of the DSR *
				 * device so that packets are not too big after
				 * adding the DSR header. A better solution
				 * should probably be found... */

enum confval {
#ifdef ENABLE_DEBUG
	PrintDebug,
#endif
	AutomaticRouteShortening,
	RoundTripTimeout, /* Determines the Round trip timeout (RTO)
			   * used for DSR acks. If set to 0, dynamic
			   * measurement is used. */
	FlushLinkCache,
	PromiscOperation,
	BroadCastJitter,
	RouteCacheTimeout,
	SendBufferTimeout,
	SendBufferSize,
	RequestTableSize,
	RequestTableIds,
	MaxRequestRexmt,
	MaxRequestPeriod,
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
	CONFVAL_MAX,
};

enum confval_type {
	SECONDS,
	MILLISECONDS,
	MICROSECONDS,
	NANOSECONDS,
	QUANTA,
	BINARY,
	COMMAND,
	CONFVAL_TYPE_MAX,
};

#define MAINT_BUF_MAX_LEN 100
#define RREQ_TBL_MAX_LEN 64	/* Should be enough */
#define SEND_BUF_MAX_LEN 100
#define RREQ_TLB_MAX_ID 16

static struct {
	const char *name;
	const unsigned int val;
	enum confval_type type;
} confvals_def[CONFVAL_MAX] = {
#ifdef ENABLE_DEBUG
	{
		"PrintDebug", 0, BINARY},
#endif
	{
		"AutomaticRouteShortening", 1, BINARY}, {
		"RoundTripTimeout", 2000, MILLISECONDS}, {
		"FlushLinkCache", 1, COMMAND}, {
		"PromiscOperation", 1, BINARY}, {
		"BroadCastJitter", 20, MILLISECONDS}, {
		"RouteCacheTimeout", 300, SECONDS}, {
		"SendBufferTimeout", 30, SECONDS}, {
		"SendBufferSize", SEND_BUF_MAX_LEN, QUANTA}, {
		"RequestTableSize", RREQ_TBL_MAX_LEN, QUANTA}, {
		"RequestTableIds", RREQ_TLB_MAX_ID, QUANTA}, {
		"MaxRequestRexmt", 16, QUANTA}, {
		"MaxRequestPeriod", 10, SECONDS}, {
		"RequestPeriod", 500, MILLISECONDS}, {
		"NonpropRequestTimeout", 30, MILLISECONDS}, {
		"RexmtBufferSize", MAINT_BUF_MAX_LEN}, {
		"MaintHoldoffTime", 250, MILLISECONDS}, {
		"MaxMaintRexmt", 2, QUANTA}, {
		"UseNetworkLayerAck", 1, BINARY}, {
		"TryPassiveAcks", 1, QUANTA}, {
		"PassiveAckTimeout", 100, MILLISECONDS}, {
		"GratReplyHoldOff", 1, SECONDS}, {
		"MAX_SALVAGE_COUNT", 15, QUANTA}
};

struct dsr_node {
	struct in_addr ifaddr;
	struct in_addr bcaddr;
	unsigned int confvals[CONFVAL_MAX];
#ifdef __KERNEL__
	char slave_ifname[IFNAMSIZ];
	struct net_device *slave_dev;
	struct in_device *slave_indev;
	struct net_device_stats stats;
	spinlock_t lock;
#endif
};

#ifdef __KERNEL__
#define NSCLASS
#define XMIT(pkt)           dsr_dev_xmit(pkt)

/* Some macros to access different layer headers in the skb. The APIs
 * changed around kernel 2.6.22, so these macros make us backwards
 * compatible with older kernels. */
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,21)
#define SKB_NETWORK_HDR_RAW(skb) skb->nh.raw
#define SKB_NETWORK_HDR_RIPH(skb) skb->nh.iph
#define SKB_MAC_HDR_RAW(skb) skb->mac.raw
#define SKB_TAIL(skb) skb->tail
#define SKB_SET_MAC_HDR(skb, offset) (skb->mac.raw = (skb->data + (offset)))
#define SKB_SET_NETWORK_HDR(skb, offset) (skb->nh.raw = (skb->data + (offset)))
#else
#define SKB_NETWORK_HDR_RAW(skb) skb_network_header(skb)
#define SKB_NETWORK_HDR_IPH(skb) ((struct iphdr *)skb_network_header(skb))
#define SKB_MAC_HDR_RAW(skb) skb_mac_header(skb)
#define SKB_TAIL(skb) skb_tail_pointer(skb)
#define SKB_SET_MAC_HDR(skb, offset) skb_set_mac_header(skb, offset)
#define SKB_SET_NETWORK_HDR(skb, offset) skb_set_network_header(skb, offset)
#endif
#endif				/* __KERNEL__ */

#ifdef __KERNEL__

#define ConfVal(cv) get_confval(cv)
#define ConfValToUsecs(cv) confval_to_usecs(cv)

extern struct dsr_node *dsr_node;

static inline unsigned int get_confval(enum confval cv)
{
	unsigned int val = 0;

	if (dsr_node) {
		spin_lock_bh(&dsr_node->lock);
		val = dsr_node->confvals[cv];
		spin_unlock_bh(&dsr_node->lock);
	}
	return val;
}

static inline int set_confval(enum confval cv, unsigned int val)
{
	if (dsr_node) {
		spin_lock_bh(&dsr_node->lock);
		dsr_node->confvals[cv] = val;
		spin_unlock_bh(&dsr_node->lock);
	} else
		return -1;

	return val;
}

static inline void dsr_node_init(struct dsr_node *dn, char *ifname)
{
	int i;
	dn->slave_indev = NULL;
	dn->slave_dev = NULL;
        dn->slave_ifname[0] - '\0';

        if (ifname)
                memcpy(dn->slave_ifname, ifname, IFNAMSIZ);

	spin_lock_init(&dn->lock);

	for (i = 0; i < CONFVAL_MAX; i++) {
		dn->confvals[i] = confvals_def[i].val;
	}
}

static inline struct in_addr my_addr(void)
{
	static struct in_addr my_addr;
	if (dsr_node) {
		spin_lock_bh(&dsr_node->lock);
		my_addr = dsr_node->ifaddr;
		spin_unlock_bh(&dsr_node->lock);
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

static inline int get_slave_dev_ifindex(void)
{
	int ifindex = -1;

	if (dsr_node) {
                spin_lock_bh(&dsr_node->lock);
		if (dsr_node->slave_dev)
			ifindex = dsr_node->slave_dev->ifindex;
		spin_unlock_bh(&dsr_node->lock);
	}
	return ifindex;
}

static inline void dsr_node_lock(struct dsr_node *dnode)
{
	spin_lock_bh(&dnode->lock);
}

static inline void dsr_node_unlock(struct dsr_node *dnode)
{
	spin_unlock_bh(&dnode->lock);
}
int dsr_ip_recv(struct sk_buff *skb);
int do_mackill(char *mac);

#endif

#endif				/* NO_GLOBALS */

#ifndef NO_DECLS

static inline usecs_t confval_to_usecs(enum confval cv)
{
	usecs_t usecs = 0;
	unsigned int val;

	val = ConfVal(cv);

	switch (confvals_def[cv].type) {
	case SECONDS:
		usecs = val * 1000000;
		break;
	case MILLISECONDS:
		usecs = val * 1000;
		break;
	case MICROSECONDS:
		usecs = val;
		break;
	case NANOSECONDS:
		usecs = val / 1000;
		break;
	case BINARY:
	case QUANTA:
	case COMMAND:
	case CONFVAL_TYPE_MAX:
		break;
	}

	return usecs;
}

#endif				/* NO_DECLS */

#endif				/* _DSR_H */
