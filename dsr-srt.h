#ifndef _DSR_SRT_H
#define _DSR_SRT_H

#include <asm/byteorder.h>
#include <linux/types.h>
#include <linux/in.h>

/* Source route options header */
typedef struct dsr_srt_opt {
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
} dsr_srt_opt_t;

#define DSR_SRT_HDR_LEN sizeof(dsr_srt_opt_t)
#define DSR_SRT_OPT_LEN(srt) (DSR_SRT_HDR_LEN + srt->laddrs)

/* Internal representation of a source route */
typedef struct dsr_srt {
	struct in_addr src;
	struct in_addr dst;
	unsigned int laddrs;  /* length in bytes if addrs */
	struct in_addr addrs[0];  /* Intermediate nodes */
} dsr_srt_t;


char *print_srt(dsr_srt_t *srt);
dsr_srt_t *dsr_srt_new(struct in_addr src, struct in_addr dst, 
		       unsigned int length, u_int32_t *addrs);
dsr_srt_t *dsr_srt_new_rev(dsr_srt_t *srt);
dsr_srt_opt_t *dsr_srt_opt_add(char *buf, int len, dsr_srt_t *srt);
int dsr_srt_recv(dsr_pkt_t *dp);

#endif /* _DSR_SRT_H */
