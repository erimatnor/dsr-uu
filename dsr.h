#ifndef _DSR_H
#define _DSR_H

#include <asm/byteorder.h>
#include <linux/types.h>
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
	struct iphdr *iph;
	int data_len;          
	char *data;           /* Packet data (IP not included)*/
	struct dsr_srt *srt; /* Source route */
	/* These are pointers into the dsr options in the dsr header */
	int dsr_opts_len;
	struct dsr_opt_hdr *opt_hdr;
	struct dsr_srt_opt *srt_opt;
	struct dsr_rreq_opt *rreq_opt;
	struct dsr_rrep_opt *rrep_opt;
};

/* Packet actions: */
/* Actions to take after processing source route option: */
#define DSR_PKT_ERROR          0x1
#define DSR_PKT_SRT_REMOVE     0x2
#define DSR_PKT_FORWARD        0x4
#define DSR_PKT_DELIVER        0x8
#define DSR_PKT_SEND_ICMP      0x10
#define DSR_PKT_DROP           0x20
#define DSR_PKT_SEND_RREP      0x40
#define DSR_PKT_SEND_BUFFERED  0x80

/* Local device info (shared data) */
//extern struct netdev_info ldev_info;  /* defined in dsr-dev.c */

struct dsr_node {
	struct net_device *dev;
	struct net_device *slave_dev;
	struct net_device_stats	stats;
	struct in_addr ifaddr;
	struct in_addr bcaddr;
	spinlock_t lock;
};

extern struct dsr_node *dsr_node;

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

/* struct dsr_pkt *dsr_pkt_alloc(int size); */
/* void dsr_pkt_free(struct dsr_pkt *dp); */

#endif /* _DSR_H */
