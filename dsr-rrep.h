#ifndef _DSR_RREP_H
#define _DSR_RREP_H

typedef struct dsr_rrep_opt {
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
} dsr_rrep_opt_t;

#define DSR_RREP_HDR_LEN sizeof(dsr_rrep_opt_t)
#define DSR_RREP_TOT_LEN IP_HDR_LEN + sizeof(dsr_rrep_opt_t)
#define DSR_RREP_OPT_LEN(srt) (DSR_RREP_HDR_LEN + srt->laddrs + sizeof(struct in_addr))
/* Length of source route is length of option, minus reserved/flags field minus
 * the last source route hop (which is the destination) */
#define DSR_RREP_ADDRS_LEN(rrep) (rrep->length - (1 + sizeof(struct in_addr))) 

//dsr_rrep_opt_t *dsr_rrep_opt_add(char *buf, int len, dsr_srt_t *sr);
int dsr_rrep_create(char *buf, int len, dsr_srt_t *srt);
#endif /* _DSR_RREP */
