#ifndef _DSR_ACK
#define _DSR_ACK

#include <asm/byteorder.h>
#include <linux/types.h>

struct dsr_ack_req_opt {
	u_int8_t type;
	u_int8_t length;
	u_int16_t id;
};

struct dsr_ack_opt {
	u_int8_t type;
	u_int8_t length;
	u_int16_t id;
	u_int32_t src;
	u_int32_t dst;
};
#define DSR_ACK_REQ_HDR_LEN sizeof(struct dsr_ack_req_opt)
#define DSR_ACK_REQ_OPT_LEN sizeof(u_int16_t)
#define DSR_ACK_HDR_LEN sizeof(struct dsr_ack_opt)
#define DSR_ACK_OPT_LEN 6

#endif  /* _DSR_ACK */
