#include <linux/config.h>
#include <linux/version.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/skbuff.h>
#include <net/protocol.h>
#include <linux/netdevice.h>
#include <linux/if_ether.h>
#include <linux/proc_fs.h>
#include <linux/socket.h>
#include <net/arp.h>
#include <net/ip.h>
#include <net/dst.h>
#include <net/neighbour.h>
#include <asm/uaccess.h>
#include <linux/netfilter_ipv4.h>
#ifdef KERNEL26
#include <linux/moduleparam.h>
#endif
#include <net/icmp.h>

#include "dsr.h"
#include "dsr-opt.h"
#include "dsr-dev.h"
#include "dsr-rreq.h"
#include "dsr-rrep.h"
#include "dsr-srt.h"
#include "send-buf.h"
#include "debug.h"
#include "dsr-rtc.h"
#include "dsr-ack.h"

static char *ifname = NULL;
static char *mackill = NULL;

MODULE_AUTHOR("erik.nordstrom@it.uu.se");
MODULE_DESCRIPTION("Dynamic Source Routing (DSR) protocol stack");
MODULE_LICENSE("GPL");

#ifdef KERNEL26
module_param(ifname, charp, 0);
module_param(mackill, charp, 0);
#else
MODULE_PARM(ifname, "s");
MODULE_PARM(mackill, "s");
#endif

#define CONFIG_PROC_NAME "dsr_config"

#define MAX_MACKILL 10

static unsigned char mackill_list[MAX_MACKILL][ETH_ALEN];
static int mackill_len = 0;

/* Stolen from LUNAR <christian.tschudin@unibas.ch> */
static int parse_mackill(void)
{
	char *pa[MAX_MACKILL], *cp;
	int i, j; // , ia[ETH_ALEN];

	cp = mackill;
	while (cp && mackill_len < MAX_MACKILL) {
		pa[mackill_len] = strsep(&cp, ",");
		if (!pa[mackill_len])
			break;
		mackill_len++;
	}
	for (i = 0; i < mackill_len; i++) {
		// lnx kernel bug in 2.4.X: sscanf format "%x" does not work ....
		cp = pa[i];
		for (j = 0; j < ETH_ALEN; j++, cp++) {
			mackill_list[i][j] = 0;
			for (; isxdigit(*cp); cp++) {
				mackill_list[i][j] =
					(mackill_list[i][j] << 4) |
					(*cp <= '9' ? (*cp - '0') :
					           ((*cp & 0x07) + 9 ));
			}
			if (*cp && *cp != ':') break;
		}
		if ( j != ETH_ALEN) {
			DEBUG("mackill: error in MAC addr %s\n", pa[i]);
			mackill_len--;
			return -1;
		}

		DEBUG("mackill +%s\n", print_eth(mackill_list[i]));
	}
	return 0;
}

static int do_mackill(char *mac)
{
	int i;

	for (i = 0; i < mackill_len; i++) {
		if (memcmp(mac, mackill_list[i], ETH_ALEN) == 0)
			return 1;
	}
	return 0;
}

static int dsr_arpset(struct in_addr addr, struct sockaddr *hw_addr, 
		       struct net_device *dev)
{
	struct neighbour *neigh;

	DEBUG("Setting arp for %s %s\n", print_ip(addr.s_addr), 
	      print_eth(hw_addr->sa_data));

	neigh = __neigh_lookup_errno(&arp_tbl, &(addr.s_addr), dev);
	//        err = PTR_ERR(neigh);
        if (!IS_ERR(neigh)) {
		neigh->parms->delay_probe_time = 0;
#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,8)
                neigh_update(neigh, hw_addr->sa_data, NUD_REACHABLE, 1);
#else
		neigh_update(neigh, hw_addr->sa_data, NUD_REACHABLE, 1, 0);
#endif
                neigh_release(neigh);
        }
	return 0;
}

static int dsr_ip_recv(struct sk_buff *skb)
{
	struct dsr_pkt *dp;
	int action;

#ifdef DEBUG
	atomic_inc(&num_pkts);
#endif 
	DEBUG("Received DSR packet\n");

	dp = dsr_pkt_alloc(skb, 0);

	if (!dp) {
		DEBUG("Could not allocate DSR packet\n");
		return -1;
	}
	
	if ((skb->len + (dp->nh.iph->ihl << 2)) < ntohs(dp->nh.iph->tot_len)) {
		DEBUG("data to short according to IP header len=%d tot_len=%d!\n", skb->len + (dp->nh.iph->ihl << 2), ntohs(dp->nh.iph->tot_len));
		dsr_pkt_free(dp);
		return -1;
	}
	
	dp->dsr_opts_len = ntohs(dp->dh.opth->p_len) + DSR_OPT_HDR_LEN;

	dp->data = skb->data + dp->dsr_opts_len;
	dp->data_len = skb->len - dp->dsr_opts_len;

	
	DEBUG("iph_len=%d iph_totlen=%d dsr_opts_len=%d data_len=%d\n", 
	      (dp->nh.iph->ihl << 2), ntohs(dp->nh.iph->tot_len), dp->dsr_opts_len, dp->data_len);

	
	/* Process packet */
	action = dsr_opt_recv(dp);

	/* Add mac address of previous hop to the arp table */
	if (dp->srt && skb->mac.raw) {
		struct sockaddr hw_addr;
		struct in_addr prev_hop;
		struct ethhdr *eth;
		
		eth = (struct ethhdr *)skb->mac.raw;
			
		memcpy(hw_addr.sa_data, eth->h_source, ETH_ALEN);
		
		prev_hop = dsr_srt_prev_hop(dp->srt);

		dsr_arpset(prev_hop, &hw_addr, skb->dev);
	}

	if (action & DSR_PKT_DROP || action & DSR_PKT_ERROR) {
		DEBUG("DSR_PKT_DROP or DSR_PKT_ERROR\n");
		dsr_pkt_free(dp);
		return 0;
	}

	if (action & DSR_PKT_FORWARD) {
		DEBUG("Forwarding %s %s nh %s\n", 
		      print_ip(dp->src.s_addr), 
		      print_ip(dp->dst.s_addr), 
		      print_ip(dp->nxt_hop.s_addr));

		if (dp->nh.iph->ttl < 1) {
			DEBUG("ttl=0, dropping!\n");
			action = DSR_PKT_NONE;
		} else {
			DEBUG("Forwarding (dev_queue_xmit)\n");
			dsr_dev_xmit(dp);
			return 0;
		}
	}
	if (action & DSR_PKT_FORWARD_RREQ) {
		struct in_addr myaddr = my_addr();
		int n, dsr_len, len_to_rreq;		
		char *tmp;

		/* We need to add ourselves to the source route in the RREQ */
		n = DSR_RREQ_ADDRS_LEN(dp->rreq_opt) / sizeof(struct in_addr);
	
		dsr_len = ntohs(dp->dh.opth->p_len) + DSR_OPT_HDR_LEN;
		len_to_rreq = (char *)dp->rreq_opt - dp->dh.raw;

		tmp = dsr_pkt_alloc_data(dp, dsr_len + sizeof(struct in_addr));
		/* dsr_pkt_add_dsr_hdr(dsr_len + sizeof(struct in_addr)); */

		if ((dp->dh.raw + dsr_len) > 
		    ((char *)dp->rreq_opt + dp->rreq_opt->length + 2)) {
			char *tmp2;
			int len_after_rreq;
			
			len_after_rreq = (dp->dh.raw + dsr_len) - ((char *)dp->rreq_opt + dp->rreq_opt->length + 2);
			
			/* Copy everything up to and including rreq_opt */
			memcpy(tmp, dp->dh.raw, len_to_rreq + dp->rreq_opt->length + 2);
			tmp2 = tmp + len_to_rreq + dp->rreq_opt->length + 2 + sizeof(struct in_addr);

			memcpy(tmp2, dp->dh.raw + len_to_rreq + dp->rreq_opt->length + 2 + sizeof(struct in_addr), len_after_rreq);

			
		} else {
			memcpy(tmp, dp->dh.raw, dsr_len);
		}
		dp->dh.raw = tmp;
		dp->dh.opth->p_len = htons(dsr_len + sizeof(struct in_addr));
		dp->dsr_opts_len += sizeof(struct in_addr);
		dp->rreq_opt = (struct dsr_rreq_opt *)(tmp + len_to_rreq);
		dp->rreq_opt->addrs[n] = myaddr.s_addr;
		dp->rreq_opt->length += sizeof(struct in_addr);
		
		dp->nh.iph->tot_len = htons(ntohs(dp->nh.iph->tot_len) + sizeof(struct in_addr));
		ip_send_check(dp->nh.iph);

		dsr_dev_xmit(dp);
		
		return 0;

	}

	if (action & DSR_PKT_SEND_RREP) {

		DEBUG("Send RREP\n");
		
		if (dp->srt) {
			/* send rrep.... */
			dsr_rrep_send(dp->srt);
		}
	}

	if (action & DSR_PKT_SEND_ICMP) {
		DEBUG("Send ICMP\n");
	}
	if (action & DSR_PKT_SEND_BUFFERED) {
		/* Send buffered packets */
		DEBUG("Sending buffered packets\n");
		if (dp->srt) {
			send_buf_set_verdict(SEND_BUF_SEND, dp->srt->src.s_addr);
		}
	}

	/* Free source route. Should probably think of a better way to handle
	 * source routes that are dynamically allocated. */
/* 	if (dp->srt) { */
/* 		DEBUG("Freeing source route\n"); */
/* 		kfree(dp->srt); */
/* 	} */

	if (action & DSR_PKT_DELIVER) {
		dsr_opts_remove(dp);
		DEBUG("Deliver to DSR device\n");
		dsr_dev_deliver(dp);
		
		return 0;
	}
	dsr_pkt_free(dp);
	return 0;
};

static void dsr_ip_recv_err(struct sk_buff *skb, u32 info)
{
	DEBUG("received error, info=%u\n", info);
	
	kfree_skb(skb);
}



static int dsr_get_hwaddr(struct in_addr addr, struct sockaddr *hwaddr, 
			  struct net_device *dev)
{	
	struct neighbour *neigh;

	neigh = neigh_lookup(&arp_tbl, &addr.s_addr, dev);
	
	if (neigh) {
		
		hwaddr->sa_family = AF_UNSPEC;
	      
		read_lock_bh(&neigh->lock);
		memcpy(hwaddr->sa_data, neigh->ha, ETH_ALEN);
		read_unlock_bh(&neigh->lock);

		return 0;
	}
	return -1;
}

struct sk_buff *dsr_skb_create(struct dsr_pkt *dp,
			       struct net_device *dev)
{
	struct sk_buff *skb;
	char *buf;
	int ip_len;
	int tot_len;
	
	ip_len = dp->nh.iph->ihl << 2;
	
	tot_len = ip_len + dp->dsr_opts_len + dp->data_len;
	
	DEBUG("iph_len=%d dp->data_len=%d dp->dsr_opts_len=%d TOT len=%d\n", 
	      ip_len, dp->data_len, dp->dsr_opts_len, tot_len);
	
/* 	skb = alloc_skb(dev->hard_header_len + 15 + tot_len, GFP_ATOMIC); */
	skb = alloc_skb(tot_len +  LL_RESERVED_SPACE(dev), GFP_ATOMIC);

	if (!skb) {
		DEBUG("alloc_skb failed\n");
		return NULL;
	}
	
	/* We align to 16 bytes, for ethernet: 2 bytes + 14 bytes header */
	skb_reserve(skb, LL_RESERVED_SPACE(dev));
       /* 	skb_reserve(skb, (dev->hard_header_len+15)&~15);  */
	skb->nh.raw = skb->data;
	skb->dev = dev;
	skb->protocol = htons(ETH_P_IP);

	/* Copy in all the headers in the right order */
	buf = skb_put(skb, tot_len);

	memcpy(buf, dp->nh.iph, ip_len);
	
	buf += ip_len;
	
	if (dp->dsr_opts_len && dp->dh.raw) {
		memcpy(buf, dp->dh.opth, dp->dsr_opts_len);
		buf += dp->dsr_opts_len;
	}

	if (dp->data_len && dp->data)
		memcpy(buf, dp->data, dp->data_len);
	
	return skb;
}

int dsr_hw_header_create(struct dsr_pkt *dp, struct sk_buff *skb) 
{

		
	struct sockaddr broadcast = {AF_UNSPEC, {0xff, 0xff, 0xff, 0xff, 0xff, 0xff}};
	struct sockaddr dest;
	
	if (dp->dst.s_addr == DSR_BROADCAST)
		memcpy(dest.sa_data , broadcast.sa_data, ETH_ALEN);
	else {
		/* Get hardware destination address */
		if (dsr_get_hwaddr(dp->nxt_hop, &dest, skb->dev) < 0) {
			DEBUG("Could not get hardware address for next hop %s\n", print_ip(dp->nxt_hop.s_addr));
			return -1;
		}
	}
	
	if (skb->dev->hard_header) {
		skb->dev->hard_header(skb, skb->dev, ETH_P_IP,
				      dest.sa_data, 0, skb->len);
	} else {
		DEBUG("Missing hard_header\n");
		return -1;
	}
	return 0;
}

static int dsr_config_proc_read(char *buffer, char **start, off_t offset, int length, int* eof, void* data)
{
	int len = 0;
	int i;
	
	for (i = 0; i < PARAMS_MAX; i++)
		len += sprintf(buffer+len, "%s=%d\n", 
			       params_def[i].name, get_param(i));
    
	*start = buffer + offset;
	len -= offset;
	if (len > length)
		len = length;
	else if (len < 0)
		len = 0;
	return len;    
}
static int dsr_config_proc_write(struct file* file, const char* buffer, 
				unsigned long count, void* data) {
#define CMD_MAX_LEN 256
  char cmd[CMD_MAX_LEN ];
  int i;
  
  memset(cmd, '\0', CMD_MAX_LEN);

  if (count > CMD_MAX_LEN )
	  return -EINVAL;
  
  /* Don't read the '\n' or '\0' character... */
  if(copy_from_user(cmd, buffer, count))
	  return -EFAULT;

		  
  for (i = 0; i < PARAMS_MAX; i++) {
	  int n = strlen(params_def[i].name);
	  
	  if (strlen(cmd) - 2 <= n)
		  return -EFAULT;

	  if (strncmp(cmd, params_def[i].name, n) >= 0) {
		  char *from, *to;
		  int val = 0;
		  
		  from = strstr(cmd, "="); 
		  from++; /* Exclude '=' */
		  val = simple_strtol(from, &to, 10); 
		  set_param(i, val);
		  DEBUG("Setting %s to %d\n",  params_def[i].name, val);
	  }
  }
  return count;
}


/* We hook in before IP's routing so that IP doesn't get a chance to drop it
 * before we can look at it... Packet to this node can go through since it will
 * be routed to us anyway */
static unsigned int dsr_pre_routing_recv(unsigned int hooknum,
					 struct sk_buff **skb,
					 const struct net_device *in,
					 const struct net_device *out,
					 int (*okfn) (struct sk_buff *))
{	
	struct iphdr *iph = (*skb)->nh.iph;
	struct in_addr myaddr = my_addr();


	if (in && in->ifindex == get_slave_dev_ifindex() && 
	    (*skb)->protocol == htons(ETH_P_IP)) {

	/* 	DEBUG("IP packet on nf hook\n"); */

		if (do_mackill((*skb)->mac.raw + ETH_ALEN)) {
		/* 	DEBUG("Dropping pkt\n"); */
			return NF_DROP;
		}
		if (iph && iph->protocol == IPPROTO_DSR && 
		    iph->daddr != myaddr.s_addr && 
		    iph->daddr != DSR_BROADCAST) {
		
		/* 	DEBUG("Packet for ip_rcv\n"); */
			
			(*skb)->data = (*skb)->nh.raw + (iph->ihl << 2);
			(*skb)->len = (*skb)->tail - (*skb)->data;
			dsr_ip_recv(*skb);
		/* 	DEBUG("Stolen\n"); */
			return NF_STOLEN;
		}
		/* DEBUG("Accepted\n"); */
	}
	return NF_ACCEPT;	
}

static struct nf_hook_ops dsr_pre_routing_hook = {
	
	.hook		= dsr_pre_routing_recv,
#ifdef KERNEL26
	.owner		= THIS_MODULE,
#endif
	.pf		= PF_INET,
	.hooknum	= NF_IP_PRE_ROUTING,
	.priority	= NF_IP_PRI_FIRST,
};

/* This is kind of a mess */
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,7)
static struct inet_protocol dsr_inet_prot = {
#else
static struct net_protocol dsr_inet_prot = {
#endif
	.handler = dsr_ip_recv,
	.err_handler = dsr_ip_recv_err,
#ifdef KERNEL26
	.no_policy = 1,
#else
	.protocol    = IPPROTO_DSR,
	.name        = "DSR"
#endif
};

static int __init dsr_module_init(void)
{
	int res = -EAGAIN;
	struct proc_dir_entry *proc;

	res = dsr_dev_init(ifname);

	if (res < 0) {
		DEBUG("dsr-dev init failed\n");
		return -EAGAIN;
	}
#ifdef DEBUG
	dbg_init();
#endif
	parse_mackill();

	res = send_buf_init();

	if (res < 0) 
		goto cleanup_dsr_dev;
	
	res = rreq_tbl_init();

	if (res < 0) 
		goto cleanup_send_buf;
			
	res = neigh_tbl_init();

	if (res < 0) 
		goto cleanup_rreq_tbl;

	res = nf_register_hook(&dsr_pre_routing_hook);

	if (res < 0)
		goto cleanup_neigh_tbl;

	proc = create_proc_entry(CONFIG_PROC_NAME, S_IRUGO | S_IWUSR, proc_net);
	
	if (!proc)
		goto cleanup_nf_hook;
	
	proc->owner = THIS_MODULE;
	proc->read_proc = dsr_config_proc_read;
	proc->write_proc = dsr_config_proc_write;

#ifndef KERNEL26
	inet_add_protocol(&dsr_inet_prot);
	DEBUG("Setup finished\n");
	return res;
#else
	res = inet_add_protocol(&dsr_inet_prot, IPPROTO_DSR);
	
	if (res < 0) {
		DEBUG("Could not register inet protocol\n");
		goto cleanup_proc;
	}
	
	DEBUG("Setup finished res=%d\n", res);
	
	return res;
 cleanup_proc:
	proc_net_remove(CONFIG_PROC_NAME);
#endif
 cleanup_nf_hook:
	nf_unregister_hook(&dsr_pre_routing_hook);
 cleanup_neigh_tbl:
	neigh_tbl_cleanup();
 cleanup_rreq_tbl:
	rreq_tbl_cleanup();
 cleanup_send_buf:
	send_buf_cleanup();
 cleanup_dsr_dev:
	dsr_dev_cleanup();
#ifdef DEBUG
	dbg_cleanup();
#endif
	return res;
}

static void __exit dsr_module_cleanup(void)
{
#ifdef KERNEL26
	inet_del_protocol(&dsr_inet_prot, IPPROTO_DSR);
#else
	inet_del_protocol(&dsr_inet_prot);
#endif
	nf_unregister_hook(&dsr_pre_routing_hook);
	send_buf_cleanup();
	dsr_dev_cleanup();
	rreq_tbl_cleanup();
	neigh_tbl_cleanup();
#ifdef DEBUG
	dbg_cleanup();
#endif
}

module_init(dsr_module_init);
module_exit(dsr_module_cleanup);
