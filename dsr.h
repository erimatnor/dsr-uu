#ifndef _DSR_H
#define _DSR_H

#include <asm/byteorder.h>
#include <linux/types.h>
#include <linux/in.h>

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
	char options[0];
} dsr_hdr_t;

#define DSR_OPT_PADN       0
#define DSR_OPT_RREP       1
#define DSR_OPT_RREQ       2
#define DSR_OPT_ERR        3
#define DSR_OPT_PREV_HOP   5
#define DSR_OPT_ACK       32
#define DSR_OPT_SRC_RTE   96
#define DSR_OPT_TIMEOUT  128
#define DSR_OPT_FLOWID   129
#define DSR_OPT_AREQ     160
#define DSR_OPT_PAD1     224

typedef struct dsr_opt {
	u_int8_t type;
	u_int8_t length;
} dsr_opt_t;


struct netdev_info {
    __u32 ip_addr;
    __u32 bc_addr;
    int ifindex;
};
#define DSR_PKT_MIN_LEN 24 /* IP header + DSR header =  20 + 4 */

#define DSR_BROADCAST ((unsigned long int) 0xffffffff)
#define IPPROTO_DSR 168 /* Is this correct? */
#define IP_HDR_LEN 20

/* Local device info */
extern struct netdev_info ldev_info;  /* defined in dsr-dev.c */

dsr_hdr_t *dsr_hdr_add(char *buf, int len, unsigned int protocol);

#endif
