#ifdef NS2
#include "ns-agent.h"
#else
#include "dsr-dev.h"
#endif

#include "dsr.h"
#include "dsr-pkt.h"
#include "dsr-rreq.h"
#include "dsr-rrep.h"
#include "dsr-srt.h"
#include "dsr-ack.h"
#include "dsr-rtc.h"
#include "dsr-ack.h"
#include "maint-buf.h"
#include "neigh.h"
#include "dsr-opt.h"
#include "link-cache.h"
#include "debug.h"
#include "send-buf.h"

void NSCLASS dsr_recv(struct dsr_pkt *dp)
{
	int i = 0, action;
	int mask = DSR_PKT_NONE;

	/* Process DSR Options */
	action = dsr_opt_recv(dp);
	
      	/* Add mac address of previous hop to the neighbor table */
	if (dp->srt && dp->mac.raw) {
		struct sockaddr hw_addr;
#ifdef NS2

		/* This should probably be changed to lookup the MAC type
		 * dynamically in case the simulation is run over a non 802.11
		 * mac layer... Or is there a uniform way to get hold of the mac
		 * source for all mac headers? */
		struct hdr_mac802_11 *mh_802_11=(struct hdr_mac802_11 *)dp->mac.ethh;
		int mac_src = ETHER_ADDR(mh_802_11->dh_ta);

		inttoeth(&mac_src, (char *)&hw_addr);
#else
		memcpy(hw_addr.sa_data, dp->mac.ethh->h_source, ETH_ALEN);
#endif		
		dp->prv_hop = dsr_srt_prev_hop(dp->srt, my_addr());
		
		DEBUG("prev hop=%s\n", print_ip(dp->prv_hop));

		neigh_tbl_add(dp->prv_hop, &hw_addr);
	}
	
	for (i = 0; i < DSR_PKT_ACTION_LAST; i++) {
		//DEBUG("i=%d action=0x%08x mask=0x%08x (action & mask)=0x%08x\n", i, action, (mask), (action & mask));
		
		switch (action & mask) {
		case DSR_PKT_NONE:
			DEBUG("DSR_PKT_NONE - Do nothing\n");
			dsr_pkt_free(dp);
			return;
		case DSR_PKT_DROP:
		case DSR_PKT_ERROR:
			DEBUG("DSR_PKT_DROP or DSR_PKT_ERROR\n");
			dsr_pkt_free(dp);
			return;
		case DSR_PKT_SEND_ACK:
			if (dp->ack_req_opt && dp->srt) {
				unsigned short id = ntohs(dp->ack_req_opt->id);
				
				DEBUG("send ACK: src=%s prv=%s id=%u\n", 
				      print_ip(dp->src), 
				      print_ip(dp->prv_hop), id);
			
				dsr_ack_send(dp->prv_hop, id);
			}
			break;
		case DSR_PKT_SRT_REMOVE:
			DEBUG("Remove source route\n");
			// Hmm, we remove the DSR options when we deliver a
			//packet
			//dsr_opts_remove(dp);
			break;
		case DSR_PKT_FORWARD:
			DEBUG("Forwarding %s %s nh %s\n", 
			      print_ip(dp->src), 
			      print_ip(dp->dst), 
			      print_ip(dp->nxt_hop));
#ifdef NS2	
			if (dp->nh.iph->ttl() < 1)
#else
			if (dp->nh.iph->ttl < 1)
#endif
			{
				DEBUG("ttl=0, dropping!\n");
				dsr_pkt_free(dp);
				return;
			} else {
				DEBUG("Forwarding (dev_queue_xmit)\n");
				XMIT(dp);
				return;
			}
			break;
		case DSR_PKT_FORWARD_RREQ:
			DEBUG("Forward RREQ\n");
			XMIT(dp);
			return;
		case DSR_PKT_SEND_RREP:
			DEBUG("Send RREP\n");
			
			if (dp->srt) {
				/* send rrep.... */
				dsr_rrep_send(dp->srt);
			}
			break;
		case DSR_PKT_SEND_ICMP:
			DEBUG("Send ICMP\n");
			break;
		case DSR_PKT_SEND_BUFFERED:
			DEBUG("Sending buffered packets\n");
			if (dp->srt) 
				send_buf_set_verdict(SEND_BUF_SEND, 
						     dp->srt->src);
			break;
		case DSR_PKT_DELIVER:
			DEBUG("Deliver to DSR device\n");
			DELIVER(dp);
			return;
		case 0:
			break;
		default:
			DEBUG("Unknown pkt action\n");
		}
		mask = (mask << 1);
	}
	dsr_pkt_free(dp);
}

void NSCLASS dsr_start_xmit(struct dsr_pkt *dp)
{
	int res;
	
	if (!dp) {
		DEBUG("Could not allocate DSR packet\n");
		return;
	}
	
	dp->srt = dsr_rtc_find(dp->src, dp->dst);
	
	if (dp->srt) {
		
		if (dsr_srt_add(dp) < 0) {
			DEBUG("Could not add source route\n");
			goto out;
		}
		/* Send packet */
		
		XMIT(dp);
		
		return;
		
	} else {			
#ifdef NS2
		res = send_buf_enqueue_packet(dp, &DSRUU::xmit);
#else
		res = send_buf_enqueue_packet(dp, dsr_dev_xmit);
#endif	
		if (res < 0) {
			DEBUG("Queueing failed!\n");
			goto out;
		}
		res = dsr_rreq_route_discovery(dp->dst);
		
		if (res < 0)
			DEBUG("RREQ Transmission failed...");
		
		return;
	}
 out:
	dsr_pkt_free(dp);
}