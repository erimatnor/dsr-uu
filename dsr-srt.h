#ifndef _DSR_SRT_H
#define _DSR_SRT_H

#include <asm/byteorder.h>
#include <linux/types.h>
#include <linux/in.h>

#include "dsr.h"

/* Source route options header */
struct dsr_srt_opt {
	u_int8_t type;
	u_int8_t length;
#if defined(__LITTLE_ENDIAN_BITFIELD)
	u_int16_t salv1:2;
	u_int16_t sleft:6;
	u_int16_t f:1;
	u_int16_t l:1;	
	u_int16_t res:4;
	u_int16_t salv2:2;	
#define SET_SALVAGE(sr, x) ( {  __u8 __x = (x); \
                               sr->salv1 = (__x & 0x03 << 2); \
                               sr->salv2 = (__x & 0x0c >> 2); })
#elif defined (__BIG_ENDIAN_BITFIELD)
	u_int16_t f:1;	
	u_int16_t l:1;	
	u_int16_t res:4;
	u_int16_t salv:4;
	u_int16_t sleft:6;
#define SET_SALVAGE(sr, x) (sr->salv = x)	
#else
#error  "Please fix <asm/byteorder.h>"
#endif
	u_int32_t addrs[0];
};

#define DSR_SRT_HDR_LEN sizeof(struct dsr_srt_opt)
#define DSR_SRT_OPT_LEN(srt) (DSR_SRT_HDR_LEN + srt->laddrs)

/* Flags */
#define SRT_BIDIR 0x1

/* Internal representation of a source route */
struct dsr_srt {
	struct in_addr src;
	struct in_addr dst;
	unsigned short flags;
	unsigned int laddrs;  /* length in bytes if addrs */
	struct in_addr addrs[0];  /* Intermediate nodes */
};

struct in_addr dsr_srt_next_hop(struct dsr_srt *srt);
char *print_srt(struct dsr_srt *srt);
struct dsr_srt *dsr_srt_new(struct in_addr src, struct in_addr dst, 
		       unsigned int length, char *addrs);
struct dsr_srt *dsr_srt_new_rev(struct dsr_srt *srt);
char *dsr_srt_opt_make_room(struct dsr_srt *srt, struct sk_buff *skb, int len);
struct dsr_srt_opt *dsr_srt_opt_add(char *buf, int len, struct dsr_srt *srt);
int dsr_srt_opt_recv(struct dsr_pkt *dp);
int dsr_srt_add(struct dsr_pkt *dp, struct sk_buff *skb);
void dsr_srt_del(struct dsr_srt *srt);

#endif /* _DSR_SRT_H */
