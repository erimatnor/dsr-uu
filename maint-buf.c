
#define MAINT_BUF_MAX_LEN 100

TBL(maint_buf, MAINT_BUF_MAX_LEN);

struct {
	struct in_addr nxt_hop;
	unsigned int rexmt;
	unsigned short ack_id;
	unsigned long tx_time;
	struct sk_buff *skb;
};

  
