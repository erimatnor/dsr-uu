#ifndef _DSR_RREQ
#define _DSR_RREQ

#include <asm/byteorder.h>
#include <linux/types.h>

typedef struct dsr_rreq_opt {
	u_int8_t type;
	u_int8_t length;
	u_int16_t id;
	u_int32_t taddr;
	char addrs[0];
} dsr_rreq_opt_t;

#define DSR_RREQ_HDR_LEN 6

struct sk_buff *dsr_rreq_create(__u32 taddr);

#endif
