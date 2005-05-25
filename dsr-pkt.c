#ifdef __KERNEL_
#include <linux/skbuff.h>
#include <linux/if_ether.h>
#endif

#ifdef NS2
#include "ns-agent.h"
#endif

#include "dsr-opt.h"
#include "dsr.h"

char *dsr_pkt_alloc_opts(struct dsr_pkt *dp, int len)
{
	if (!dp && len)
		return NULL;

	dp->dh.raw = (char *)MALLOC(len + DEFAULT_TAILROOM, GFP_ATOMIC);

	if (!dp->dh.raw)
		return NULL;

	dp->dh.tail = dp->dh.raw + len;
	dp->dh.end = dp->dh.tail + DEFAULT_TAILROOM;

	return dp->dh.raw;
}

char *dsr_pkt_alloc_opts_expand(struct dsr_pkt *dp, int len)
{
	char *tmp;
	int old_len;

	if (!dp || !dp->dh.raw)
		return NULL;

	if (dsr_pkt_tailroom(dp) > len) {
		tmp = dp->dh.tail;
		dp->dh.tail += len;
		return tmp;
	}

	tmp = dp->dh.raw;
	old_len = dsr_pkt_opts_len(dp);

	if (!dsr_pkt_alloc_opts(dp, old_len + len))
		return NULL;

	memcpy(dp->dh.raw, tmp, old_len);

	FREE(tmp);

	return (dp->dh.raw + old_len);
}

int dsr_pkt_free_opts(struct dsr_pkt *dp)
{
	int len;

	if (!dp->dh.raw)
		return -1;

	len = dsr_pkt_opts_len(dp);

	FREE(dp->dh.raw);

	dp->dh.raw = dp->dh.end = dp->dh.tail = NULL;
	dp->srt_opt = NULL;
	dp->rreq_opt = NULL;
	memset(dp->rrep_opt, 0, sizeof(struct dsr_rrep_opt *) * MAX_RREP_OPTS);
	memset(dp->rerr_opt, 0, sizeof(struct dsr_rerr_opt *) * MAX_RERR_OPTS);
	memset(dp->ack_opt, 0, sizeof(struct dsr_ack_opt *) * MAX_ACK_OPTS);
	dp->num_rrep_opts = dp->num_rerr_opts = dp->num_ack_opts = 0;

	return len;
}

#ifdef NS2
struct dsr_pkt *dsr_pkt_alloc(Packet * p)
{
	struct dsr_pkt *dp;
	struct hdr_cmn *cmh;
	int dsr_opts_len = 0;

	dp = (struct dsr_pkt *)MALLOC(sizeof(struct dsr_pkt), GFP_ATOMIC);

	if (!dp)
		return NULL;

	memset(dp, 0, sizeof(struct dsr_pkt));

	if (p) {
		cmh = hdr_cmn::access(p);

		dp->p = p;
		dp->mac.raw = p->access(hdr_mac::offset_);
		dp->nh.iph = hdr_ip::access(p);

		dp->src.s_addr =
		    Address::instance().get_nodeaddr(dp->nh.iph->saddr());
		dp->dst.s_addr =
		    Address::instance().get_nodeaddr(dp->nh.iph->daddr());

		if (cmh->ptype() == PT_DSR) {
			struct dsr_opt_hdr *opth;
			
			opth = hdr_dsr::access(p);

			dsr_opts_len = ntohs(opth->p_len) + DSR_OPT_HDR_LEN;

			if (!dsr_pkt_alloc_opts(dp, dsr_opts_len)) {
				FREE(dp);
				return NULL;
			}

			memcpy(dp->dh.raw, (char *)opth, dsr_opts_len);

			dsr_opt_parse(dp);

			if (DATA_PACKET(dp->dh.opth->nh) ||
			    dp->dh.opth->nh == PT_PING)
				dp->flags |= PKT_REQUEST_ACK;
		} else if (DATA_PACKET(cmh->ptype()) || cmh->ptype() == PT_PING)
			dp->flags |= PKT_REQUEST_ACK;

		/* A trick to calculate payload length... */
		dp->payload_len = cmh->size() - dsr_opts_len - IP_HDR_LEN;
	}
	return dp;
}

#else

struct dsr_pkt *dsr_pkt_alloc(struct sk_buff *skb)
{
	struct dsr_pkt *dp;
	int dsr_opts_len = 0;

	dp = (struct dsr_pkt *)MALLOC(sizeof(struct dsr_pkt), GFP_ATOMIC);

	if (!dp)
		return NULL;

	memset(dp, 0, sizeof(struct dsr_pkt));

	if (skb) {
		dp->skb = skb;

		dp->mac.raw = skb->mac.raw;
		dp->nh.iph = skb->nh.iph;

		dp->src.s_addr = skb->nh.iph->saddr;
		dp->dst.s_addr = skb->nh.iph->daddr;

		if (dp->nh.iph->protocol == IPPROTO_DSR) {
			struct dsr_opt_hdr *opth;
			
			opth = (struct dsr_opt_hdr *)dp->nh.raw + (dp->nh.iph->ihl << 2);
			dsr_opts_len = ntohs(opth->p_len) + DSR_OPT_HDR_LEN;

			if (!dsr_pkt_alloc_opts(dp, dsr_opts_len)) {
				FREE(dp);
				return NULL;
			}

			memcpy(dp->dh.raw, (char *)opth, dsr_opts_len);
			
			dsr_opt_parse(dp);
		}

		dp->payload = dp->nh.raw +
		    (dp->nh.iph->ihl << 2) + dsr_opts_len;

		dp->payload_len = ntohs(dp->nh.iph->tot_len) -
		    (dp->nh.iph->ihl << 2) - dsr_opts_len;

		if (dp->payload_len)
			dp->flags |= PKT_REQUEST_ACK;
	}
	return dp;
}

#endif

void dsr_pkt_free(struct dsr_pkt *dp)
{

	if (!dp)
		return;
#ifdef NS2
/* 	if (dp->p) */
#else
	if (dp->skb)
		kfree_skb(dp->skb);
#endif
	/* fprintf(stderr, "Freeing options\n"); */

	dsr_pkt_free_opts(dp);

	/* fprintf(stderr, "Freeing source route\n"); */

	if (dp->srt)
		FREE(dp->srt);

	/* fprintf(stderr, "Freeing DSR packet\n"); */

	FREE(dp);

/* 	fprintf(stderr, "Free done\n"); */

	return;
}
