#ifndef _DSR_RREP_H
#define _DSR_RREP_H

#include "dsr.h"
#include "dsr-srt.h"

struct dsr_rrep_opt {
	u_int8_t type;
	u_int8_t length;
#if defined(__LITTLE_ENDIAN_BITFIELD)
	u_int8_t res:7;
	u_int8_t l:1;		
#elif defined (__BIG_ENDIAN_BITFIELD)
	u_int8_t l:1;		
	u_int8_t res:7;
#else
#error  "Please fix <asm/byteorder.h>"
#endif
	u_int32_t addrs[0];
};

#define DSR_RREP_HDR_LEN sizeof(struct dsr_rrep_opt)
#define DSR_RREP_OPT_LEN(srt) (DSR_RREP_HDR_LEN + srt->laddrs + sizeof(struct in_addr))
/* Length of source route is length of option, minus reserved/flags field minus
 * the last source route hop (which is the destination) */
#define DSR_RREP_ADDRS_LEN(rrep_opt) (rrep_opt->length - (1 + sizeof(struct in_addr))) 

//struct dsr_rrep_opt *dsr_rrep_opt_add(char *buf, int len, struct dsr_srt *sr);
int dsr_rrep_opt_recv(struct dsr_pkt *dp, struct dsr_rrep_opt *rrep_opt);
int dsr_rrep_send(struct dsr_srt *srt);

#endif /* _DSR_RREP */
