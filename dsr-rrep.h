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

#endif /* _DSR_RREP */
