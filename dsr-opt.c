#include <net/ip.h>

#include "debug.h"
#include "dsr.h"
#include "dsr-rreq.h"
#include "dsr-rrep.h"
#include "dsr-srt.h"
#include "kdsr.h"


dsr_pkt_t *dsr_pkt_alloc(void)
{
	dsr_pkt_t *dp;

/* 	if (len < sizeof(dsr_pkt_t)) { */
/* 		DEBUG("len=%d too short\n", len); */
/* 		return NULL; */
/* 	} */
	dp = kmalloc(sizeof(dsr_pkt_t), GFP_ATOMIC);
	
	memset(dp, 0, len);
	
	return dp;
}

void dsr_pkt_free(dsr_pkt_t *dp)
{
	if (!dp)
		return;
	
	/* Can't free skb, might be needed further down, or up, the stack */

	if (dp->srt)
		kfree(srt);
	
	kfree(dp);
}

dsr_hdr_t *dsr_hdr_add(char *buf, int len, unsigned int protocol)
{
	dsr_hdr_t *dh;
	
	if (len < DSR_OPT_HDR_LEN)
		return NULL;
	
	dh = (dsr_hdr_t *)buf;

	dh->nh = protocol;
	dh->f = 0;
	dh->res = 0;
      	dh->length = htons(len - DSR_OPT_HDR_LEN);

	return dh;
}

struct iphdr *dsr_build_ip(char *buf, int len, struct in_addr src, 
			   struct in_addr dst, int ttl)
{
	struct iphdr *iph;
	
	if (len < sizeof(struct iphdr))
		return NULL;
	
	iph = (struct iphdr *)buf;
	
	iph->version = IPVERSION;
	iph->ihl = 5;
	iph->tos = 0;
	iph->tot_len = htons(len);
	iph->id = 0;
	iph->frag_off = 0;
	iph->ttl = (ttl ? ttl : IPDEFTTL);
	iph->protocol = IPPROTO_DSR;
	iph->saddr = src.s_addr;		
	iph->daddr = dst.s_addr;
	
	ip_send_check(iph);

	return iph;
}

int dsr_opts_remove(struct iphdr *iph, int len)
{
	dsr_hdr_t *dh;
	int dsr_len;
	
	dh = (dsr_hdr_t *)((char *)iph + (iph->ihl << 2));

	dsr_len = ntohs(dh->length) + DSR_OPT_HDR_LEN;
	
	if (dsr_len > len) {
		DEBUG("data to short according to DSR header len=%d dh->length=%d!\n", len, dsr_len);
		return -1;
	}
	/* Update IP header */
	iph->protocol = dh->nh;
	iph->tot_len = htons((iph->ihl << 2) + len - dsr_len);
	ip_send_check(iph);

	memcpy((char *)dh, (char *)dh + dsr_len, len - dsr_len);
	
	/* Return new length */
	return (len - dsr_len);
}
int dsr_recv(dsr_pkt_t *dp)
{	
	int dsr_len, l;
	int res = DSR_PKT_KILL;
	dsr_opt_t *dopt;
	
	if (!dp)
		return DSR_PKT_ERROR;
	
	dp->dh = (dsr_hdr_t *)dp->data;

	dsr_len = ntohs(dh->length);
	
	if (dsr_len > dp->len) {
		DEBUG("data to short, DSR header len=%d dh->length=%d!\n", 
		      dp->len, dsr_len);
		return -1;
	}
	
	l = DSR_OPT_HDR_LEN;
	dopt = DSR_OPT_HDR(dh);
	
	while (l < dsr_len) {
		DEBUG("len=%d dsr_len=%d l=%d\n", len, dsr_len, l);
		switch (dp->dopt->type) {
		case DSR_OPT_PADN:
			break;
		case DSR_OPT_RREQ:
			DEBUG("Received RREQ\n");
			dp->rreq = (dsr_rreq_opt_t *)dopt;
			res = dsr_rreq_recv(dp);
			break;
		case DSR_OPT_RREP:
			DEBUG("Received RREP\n");
			dp->rrep = (dsr_rrep_opt_t *)dopt;
			res = dsr_rrep_recv(dp);
			break;
		case DSR_OPT_ERR:
			DEBUG("Received RERR\n");
			break;
		case DSR_OPT_PREV_HOP:
			break;
		case DSR_OPT_ACK:
			DEBUG("Received ACK\n");
			break;
		case DSR_OPT_SRT:
			DEBUG("Received SRT\n");
			dp->sopt = (dsr_srt_opt_t *)dopt;
			res = dsr_srt_recv(dp);
			break;
		case DSR_OPT_TIMEOUT:	
			break;
		case DSR_OPT_FLOWID:
			break;
		case DSR_OPT_AREQ:
			break;
		case DSR_OPT_PAD1:
			break;
		default:
			DEBUG("Unknown DSR option type=%d\n", dopt->type);
		}
		l = l + dopt->length + 2;
		dopt = DSR_NEXT_OPT(dopt);
	}
	return res;
}


int dsr_srt_add(dsr_pkt_t *dp)
{
	struct iphdr *iph;
	struct sk_buff *skb, *nskb;
	char *ndx;
	int dsr_len;
	
	if (!dp || !sp->skb || !dp->srt)
		return -1;
	
	skb = dp->skb;

	/* Calculate extra space needed */
	dsr_len = DSR_OPT_HDR_LEN + DSR_SRT_OPT_LEN(dp->srt);
	
	/* Allocate new data space at head */
	nskb = skb_copy_expand(skb, skb_headroom(skb),
			       skb_tailroom(skb) + dsr_len, 
			       GFP_ATOMIC);
	
	if (nskb == NULL) {
		printk("Could not allocate new skb\n");
		return -1;	
	}
	/* Set old owner */
	if (skb->sk != NULL)
		skb_set_owner_w(nskb, skb->sk);
	
	dev_kfree_skb(skb);
	skb = nskb;
	
	skb_put(skb, dsr_len);
	
	/* Update IP header */
	iph = skb->nh.iph;
	
	iph->tot_len = htons(skb->len - sizeof(struct ethhdr));
	iph->protocol = IPPROTO_DSR;
	
	ip_send_check(iph);
	
	/* Get index to where the DSR header should go */
	ndx = skb->mac.raw + sizeof(struct ethhdr) + (iph->ihl << 2);
	memcpy(ndx + dsr_len, ndx, dsr_len);
	
	dsr_hdr_add(ndx, dsr_len, iph->protocol);
	
	dsr_srt_opt_add(ndx + DSR_OPT_HDR_LEN, dsr_len - DSR_OPT_HDR_LEN, srt);

	return 0;
}

int dsr_rreq_send(u_int32_t target)
{
	int res = 0;
	struct net_device *dev;
	dsr_pkt_t *dp;
	struct sockaddr broadcast = {AF_UNSPEC, {0xff, 0xff, 0xff, 0xff, 0xff, 0xff}};
	dev = dev_get_by_index(ldev_info.ifindex);
	
	if (!dev) {
		DEBUG("no send device!\n");
		res = -1;
		goto out_err;
	}
	
	dp->dsr_pkt_alloc();
	
	dp->len = IP_HDR_LEN + DSR_OPT_HDR_LEN + DSR_RREQ_HDR_LEN;
	dp->dst.s_addr = target;
	
	dp->skb = kdsr_pkt_alloc(dp->len, dev);
	
	if (!skb) {
		res = -1;
		goto out_err;
	}
	
	dp->skb->dev = dev;
	dp->data = skb->data;
	dp->dst.s_addr = target.s_addr;
	
	res = dsr_rreq_create(dp);

	if (res < 0) {
		DEBUG("Could not create RREQ\n");
		goto out_err;
	}
	
	res = dsr_dev_build_hw_hdr(dp->skb, &broadcast);
	
	if (res < 0) {
		DEBUG("RREQ transmission failed...\n");
		dev_kfree_skb(dp->skb);
		goto out_err;
	}

	dev_queue_xmit(dp->skb);

 out_err:	
	dsr_pkt_free(dp);
	dev_put(dev);
	return res;
}

int dsr_rrep_send(dsr_srt_t *srt)
{
	int res = 0;
	struct net_device *dev;
	dsr_pkt_t *dp;
	struct sockaddr dest;
	
	dev = dev_get_by_index(ldev_info.ifindex);
	
	if (!dev) {
		DEBUG("no send device!\n");
		res = -1;
		goto out_err;
	}
	
	if (!srt) {
		DEBUG("no source route!\n");
		res = -1;
		goto out_err;
	}

	DEBUG("Sending RREP\n");
	
	dp->dsr_pkt_alloc();
	
	dp->len = IP_HDR_LEN + DSR_OPT_HDR_LEN + DSR_SRT_OPT_LEN(srt) + DSR_RREP_OPT_LEN(srt);
		
	dp->skb = kdsr_pkt_alloc(dp->len, dev);
	
	if (!skb) {
		res = -1;
		goto out_err;
	}

	dp->skb->dev = dev;
	dp->srt = srt;
	dp->data = skb->data;
	
	res = dsr_rrep_create(dp);

	if (res < 0) {
		DEBUG("Could not create RREP\n");
		goto out_err;
	}
	
	if (srt->laddrs == 0) {
		DEBUG("source route is one hop\n");
		dp->nh.s_addr = srt->src.s_addr;
	} else		
		dp->nh.s_addr = srt->addrs->s_addr;
	
	if (kdsr_get_hwaddr(dp->nh, &dest, dev) < 0) {
		DEBUG("Could not get hardware address for %s\n", 
		      print_ip(dp->nh.s_addr));
		goto out_err;
	}
	
	res = dsr_dev_build_hw_hdr(dp->skb, &dest);
	
	if (res < 0) {
		DEBUG("RREP transmission failed...\n");
		dev_kfree_skb(dp->skb);
		goto out_err;
	}	
	dev_queue_xmit(skb);
 out_err:	
	dsr_pkt_free(sp);
	dev_put(dev);
	return res;
}
