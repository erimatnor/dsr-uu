#ifdef __KERNEL__
#include "dsr-dev.h"
#endif

#include "dsr.h"
#include "dsr-rerr.h"
#include "dsr-opt.h"
#include "debug.h"
#include "dsr-srt.h"
#include "dsr-ack.h"
#include "link-cache.h"
#include "maint-buf.h"

#ifdef NS2
#include "ns-agent.h"
#endif

static struct dsr_rerr_opt *dsr_rerr_opt_add(char *buf, int len, 
					     int err_type, 
					     struct in_addr err_src, 
					     struct in_addr err_dst, 
					     struct in_addr unreach_addr, 
					     int salv)
{
	struct dsr_rerr_opt *rerr_opt;

	if (!buf || len < DSR_RERR_HDR_LEN)
		return NULL;

	
	rerr_opt = (struct dsr_rerr_opt *)buf;

	rerr_opt->type = DSR_OPT_RERR;
	rerr_opt->length = DSR_RERR_OPT_LEN;
	rerr_opt->err_type = err_type;
	rerr_opt->err_src = err_src.s_addr;
	rerr_opt->err_dst = err_dst.s_addr;
	rerr_opt->res = 0;
	rerr_opt->salv = salv;
	
	switch (err_type) {
	case NODE_UNREACHABLE:
		if (len < DSR_RERR_HDR_LEN + sizeof(struct in_addr))
			return NULL;
		rerr_opt->length += sizeof(struct in_addr);
		memcpy(rerr_opt->info, &unreach_addr, sizeof(struct in_addr));
		break;
	case FLOW_STATE_NOT_SUPPORTED:
		break;
	case OPTION_NOT_SUPPORTED:
		break;
	}

	return rerr_opt;
}


int NSCLASS dsr_rerr_send(struct dsr_pkt *dp_trigg, struct in_addr unr_addr)
{
	struct dsr_pkt *dp;
	struct dsr_srt *srt;
	struct dsr_rerr_opt *rerr_opt;
	struct in_addr dst, err_src, err_dst;
	char *buf;
	int len, salv/* , i */;
	
	if (!dp_trigg)
		return -1;

	dp_trigg->srt_opt = (struct dsr_srt_opt *)dsr_opt_find_opt(dp_trigg, DSR_OPT_SRT);

	if (!dp_trigg->srt_opt) {
		DEBUG("Could not find source route option\n");
		return -1;
	}

	DEBUG("Send RERR\n");

	GET_SALVAGE(dp_trigg->srt_opt, salv);

	if (salv == 0)
		dst = dp_trigg->src;
	else
		dst.s_addr = dp_trigg->srt_opt->addrs[1];
	
	srt = dsr_rtc_find(my_addr(), dst);
	
	if (!srt) {
		DEBUG("No source route to %s\n", print_ip(dst));
		return -1;
	}
	
	len = DSR_OPT_HDR_LEN + DSR_SRT_OPT_LEN(srt) + DSR_RERR_HDR_LEN + 4;

	/* Also count in RERR opts in trigger packet */
/* 	for (i = 0; i < dp_trigg->num_rerr_opts; i++) { */
/* 		if (rerr_opt[i]->salv <= MAX_SALVAGE_COUNT) */
/* 			len += (dp_trigg->rerr_opt[i]->length + 2); */
/* 	} */
	/* Also count in ACK opts in trigger packet */
	/* for (i = 0; i < dp_trigg->num_ack_opts; i++) */
/* 		len += (dp_trigg->ack_opt[i]->length + 2); */

	dp = dsr_pkt_alloc(NULL);
	
	if (!dp) {
		DEBUG("Could not allocate DSR packet\n");
		return -1;
	}
	
	dp->src = my_addr();
	dp->dst = dst;
	dp->srt = srt;
	dp->nxt_hop = dsr_srt_next_hop(dp->srt, dp->src, 0);

	buf = dsr_pkt_alloc_opts(dp, len);

	if (!buf)
		goto out_err;	

	dp->nh.iph = dsr_build_ip(dp, dp->src, dp->dst, IP_HDR_LEN, 
				  IP_HDR_LEN + len, IPPROTO_DSR, IPDEFTTL);

	if (!dp->nh.iph) {
		DEBUG("Could not create IP header\n");
		goto out_err;
	}

	dp->dh.opth = dsr_opt_hdr_add(buf, len, 0);
	
	if (!dp->dh.opth) {
		DEBUG("Could not create DSR options header\n");
		goto out_err;
	}

	buf += DSR_OPT_HDR_LEN;
	len -= DSR_OPT_HDR_LEN;
	
	dp->srt_opt = dsr_srt_opt_add(buf, len, dp->srt);

	if (!dp->srt_opt) {
		DEBUG("Could not create Source Route option header\n");
		goto out_err;
	}

	buf += DSR_SRT_OPT_LEN(dp->srt);
	len -= DSR_SRT_OPT_LEN(dp->srt);
	
	rerr_opt = dsr_rerr_opt_add(buf, len, NODE_UNREACHABLE, dp->src, dp->dst, unr_addr, salv);

	if (!rerr_opt)
		goto out_err;

	buf += (rerr_opt->length + 2);
	len -= (rerr_opt->length + 2);

/* 	for (i = 0; i < dp_trigg->num_rerr_opts; i++) { */
/* 		memcpy(buf, dp_trigg->rerr_opt[i], dp_trigg->rerr_opt[i]->length); */
/* 		len -= (dp_trigg->rerr_opt[i]->length + 2); */
/* 		buf += (dp_trigg->rerr_opt[i]->length + 2); */
/* 	} */
/* 	for (i = 0; i < dp_trigg->num_ack_opts; i++) { */
/* 		memcpy(buf, dp_trigg->ack_opt[i], dp_trigg->ack_opt[i]->length); */
/* 		len -= (dp_trigg->ack_opt[i]->length + 2); */
/* 		buf += (dp_trigg->ack_opt[i]->length + 2); */
/* 	} */
	err_src.s_addr = rerr_opt->err_src;
	err_dst.s_addr = rerr_opt->err_dst;

	DEBUG("Send RERR err_src %s err_dst %s unr_dst %s\n", 
	      print_ip(err_src), 
	      print_ip(err_dst),
	      print_ip(*((struct in_addr *)rerr_opt->info)));

	XMIT(dp);
	
	return 0;

 out_err:
		
	dsr_pkt_free(dp);

	return -1;

}

int NSCLASS dsr_rerr_opt_recv(struct dsr_rerr_opt *rerr_opt)
{
	struct in_addr err_src, err_dst, unr_addr;

	if (!rerr_opt)
		return -1;
	
	switch (rerr_opt->err_type) {
	case NODE_UNREACHABLE:
		err_src.s_addr = rerr_opt->err_src;
		err_dst.s_addr = rerr_opt->err_dst;

		memcpy(&unr_addr, rerr_opt->info, sizeof(struct in_addr));

		DEBUG("NODE_UNREACHABLE err_src=%s err_dst=%s unr=%s\n", 
		      print_ip(err_src), 
		      print_ip(err_dst), 
		      print_ip(unr_addr));
			
		/* For now we drop all unacked packets... should probably
		 * salvage */
		maint_buf_del_all(err_dst);

		/* Remove broken link from cache */
		lc_link_del(err_src, unr_addr);
/* 		dsr_rtc_del(my_addr(), err_dst); */
		break;
	case FLOW_STATE_NOT_SUPPORTED:
		DEBUG("FLOW_STATE_NOT_SUPPORTED\n");
		break;
	case OPTION_NOT_SUPPORTED:
		DEBUG("OPTION_NOT_SUPPORTED\n");
		break;
	}
	
	return 0;
}
