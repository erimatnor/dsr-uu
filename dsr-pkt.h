#ifndef _DRS_PKT_H
#define _DSR_PKT_H

#define MAX_RREP_OPTS 10
#define MAX_RERR_OPTS 10
#define MAX_ACK_OPTS  10

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
	struct dsr_rreq_opt *rreq_opt; /* Can only be one */
	struct dsr_rrep_opt *rrep_opt[MAX_RREP_OPTS];   
	struct dsr_rerr_opt *rerr_opt[MAX_RERR_OPTS];   
	struct dsr_ack_opt *ack_opt[MAX_ACK_OPTS];   
	int num_rrep_opts, num_rerr_opts, num_ack_opts;
	struct dsr_srt *srt; /* Source route */
	char *data;           /* Packet data (IP not included)*/
	int data_len;
	char *dsr_data; /* DSR specific data */
	int dsr_data_len;
	struct sk_buff *skb;
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

struct dsr_pkt *dsr_pkt_alloc(struct sk_buff *skb, int len);
char *dsr_pkt_alloc_data(struct dsr_pkt *dp, int len);
void dsr_pkt_free(struct dsr_pkt *dp);

#endif /* _DSR_PKT_H */
