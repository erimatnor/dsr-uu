#ifndef _DSR_RREQ
#define _DSR_RREQ

#include <asm/byteorder.h>
#include <linux/types.h>

#include "dsr.h"

typedef struct dsr_rreq_opt {
	u_int8_t type;
	u_int8_t length;
	u_int16_t id;
	u_int32_t target;
	u_int32_t addrs[0];
} dsr_rreq_opt_t;

#define DSR_RREQ_HDR_LEN 6
#define DSR_RREQ_TOT_LEN IP_HDR_LEN + sizeof(dsr_hdr_t) + sizeof(dsr_rreq_opt_t)
dsr_rreq_opt_t *dsr_rreq_hdr_add(char *buf, int buflen, struct in_addr taddr);
void dsr_rreq_recv(struct in_addr initiator, dsr_rreq_opt_t *rreq);

#endif  /* _DSR_RREQ */
