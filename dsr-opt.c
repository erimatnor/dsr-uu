#include <linux/ip.h>

#include "debug.h"
#include "dsr.h"
#include "kdsr.h"

dsr_hdr_t *dsr_hdr_add(char *buf, int len, unsigned int protocol)
{
	dsr_hdr_t *dh;
	
	if (len < sizeof(dsr_hdr_t))
		return NULL;
	
	dh = (dsr_hdr_t *)buf;

	dh->nh = protocol;
	dh->f = 0;
	dh->res = 0;
      	dh->length = htons(len);

	return dh;
}

//struct sk_buff *dsr_pkt_create(int size)
