#ifndef _DSR_RREQ
#define _DSR_RREQ

#include <asm/byteorder.h>
#include <linux/types.h>

#include "dsr.h"

struct dsr_rreq_opt {
	u_int8_t type;
	u_int8_t length;
	u_int16_t id;
	u_int32_t target;
	u_int32_t addrs[0];
};

#define DSR_RREQ_HDR_LEN sizeof(struct dsr_rreq_opt)
#define DSR_RREQ_TOT_LEN IP_HDR_LEN + sizeof(struct dsr_opt_hdr) + sizeof(struct dsr_rreq_opt)
#define DSR_RREQ_ADDRS_LEN(rreq_opt) (rreq_opt->length - 6)

int rreq_tbl_init(void);
void rreq_tbl_cleanup(void);

int dsr_rreq_opt_recv(struct dsr_pkt *dp);
int dsr_rreq_send(struct in_addr target, int ttl);

#endif  /* _DSR_RREQ */
