#ifndef _DSR_H
#define _DSR_H

#include <asm/byteorder.h>
#include <linux/types.h>
#include <linux/in.h>

#include "dsr-srt.h"

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

struct netdev_info {
    struct in_addr ifaddr;
    struct in_addr bcaddr;
    int ifindex;
};

#define DSR_BROADCAST ((unsigned long int) 0xffffffff)
#define IPPROTO_DSR 168 /* Is this correct? */
#define IP_HDR_LEN 20

/* Local device info */
extern struct netdev_info ldev_info;  /* defined in dsr-dev.c */

struct iphdr *dsr_build_ip(char *buf, int len, struct in_addr src, 
			   struct in_addr dst, int ttl);
dsr_hdr_t *dsr_hdr_add(char *buf, int len, unsigned int protocol);
int dsr_recv(char *buf, int len, struct in_addr src, struct in_addr dst);
int dsr_rrep_send(dsr_srt_t *srt);
int dsr_opts_remove(struct iphdr *iph, int len);

#endif /* _DSR_H */
