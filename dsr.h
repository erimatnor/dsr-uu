#ifndef _DSR_H
#define _DSR_H

#include <asm/byteorder.h>
#include <linux/types.h>
#include <linux/in.h>
#include <linux/skbuff.h>
#include <linux/ip.h>

typedef struct dsr_opt {
	u_int8_t type;
	u_int8_t length;
} dsr_opt_t;

typedef struct dsr_hdr {
	u_int8_t nh;
#if defined(__LITTLE_ENDIAN_BITFIELD)

	u_int8_t res:7;
	u_int8_t f:1;		
#elif defined (__BIG_ENDIAN_BITFIELD)
	u_int8_t f:1;		
	u_int8_t res:7;
#else
#error  "Please fix <asm/byteorder.h>"
#endif
	u_int16_t length;
	dsr_opt_t option[0];
} dsr_hdr_t;

/* Header lengths */
#define DSR_OPT_HDR_LEN sizeof(struct dsr_hdr)
#define DSR_PKT_MIN_LEN 24 /* IP header + DSR header =  20 + 4 */

#define DSR_OPT_PADN       0
#define DSR_OPT_RREP       1
#define DSR_OPT_RREQ       2
#define DSR_OPT_ERR        3
#define DSR_OPT_PREV_HOP   5
#define DSR_OPT_ACK       32
#define DSR_OPT_SRT       96
#define DSR_OPT_TIMEOUT  128
#define DSR_OPT_FLOWID   129
#define DSR_OPT_AREQ     160
#define DSR_OPT_PAD1     224

#define DSR_FIXED_HDR(iph) (dsr_hdr_t *)((char *)iph + (iph->ihl << 2))
#define DSR_OPT_HDR(dh) (dh->option)
#define DSR_NEXT_OPT(dopt) ((dsr_opt_t *)((char *)dopt + dopt->length + 2))

#define DSR_BROADCAST ((unsigned long int) 0xffffffff)
#define IPPROTO_DSR 168 /* Is this correct? */
#define IP_HDR_LEN 20

/* typedef struct dsr_rreq_opt dsr_rreq_opt_t; */
/* typedef struct dsr_rrep_opt dsr_rrep_opt_t; */
/* typedef struct dsr_srt_opt dsr_srt_opt_t; */

/* Internal representation of a packet. For portability */
typedef struct dsr_pkt {
	int len;              /* Length of data, i.e., skb->len */
	char *data;           /* Beginning of data */
	struct dsr_hdr *dh;
	struct dsr_srt_opt *sopt;
	struct dsr_rreq_opt *rreq;
	struct dsr_rrep_opt *rrep;
	struct in_addr src;   /* IP level data */
	struct in_addr dst;
       	struct in_addr nh;
#ifdef __KERNEL__
	struct sk_buff *skb;
#endif
	struct dsr_srt *srt;
} dsr_pkt_t;

/* Packet actions: */
/* Actions to take after processing source route option: */
#define DSR_PKT_ERROR          -1
#define DSR_PKT_SRT_REMOVE      1
#define DSR_PKT_FORWARD         2
#define DSR_PKT_DELIVER         3
#define DSR_PKT_SEND_ICMP       4
#define DSR_PKT_DROP            5

/* Local device info */
//extern struct netdev_info ldev_info;  /* defined in dsr-dev.c */

struct dsr_node {
	struct net_device *dev;
	struct net_device *slave_dev;
	struct net_device_stats	stats;
	struct in_addr ifaddr;
	struct in_addr bcaddr;
};
extern struct net_device *dsr_dev;
extern struct dsr_node *dsr_node;

dsr_pkt_t *dsr_pkt_alloc(void);
void dsr_pkt_free(dsr_pkt_t *dp);

struct iphdr *dsr_build_ip(char *buf, int len, struct in_addr src, 
			   struct in_addr dst, int ttl);
dsr_hdr_t *dsr_hdr_add(char *buf, int len, unsigned int protocol);
int dsr_recv(dsr_pkt_t *dp);
int dsr_opts_remove(dsr_pkt_t *dp);

#endif /* _DSR_H */
