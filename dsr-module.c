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
#include <linux/parser.h>
#include <linux/netfilter_ipv4.h>
#ifdef KERNEL26
#include <linux/moduleparam.h>
#endif
#include <net/icmp.h>
//#include <net/xfrm.h>

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

MODULE_AUTHOR("erik.nordstrom@it.uu.se");
MODULE_DESCRIPTION("Dynamic Source Routing (DSR) protocol stack");
MODULE_LICENSE("GPL");

#ifdef KERNEL26
module_param(ifname, charp, 0);
#else
MODULE_PARM(ifname, "s");
#endif

#define CONFIG_PROC_NAME "dsr_config"

static int kdsr_arpset(struct in_addr addr, struct sockaddr *hw_addr, 
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
	struct dsr_pkt dp;
	int action;

	DEBUG("Received DSR packet\n");
		
	memset(&dp, 0, sizeof(dp));
	
	dp.iph = skb->nh.iph;
		
	if ((skb->len + (dp.iph->ihl << 2)) < ntohs(dp.iph->tot_len)) {
		DEBUG("data to short according to IP header len=%d tot_len=%d!\n", skb->len + (dp.iph->ihl << 2), ntohs(dp.iph->tot_len));
		kfree_skb(skb);
		return -1;
	}
	
	dp.opt_hdr = (struct dsr_opt_hdr *)skb->data;
	dp.dsr_opts_len = ntohs(dp.opt_hdr->p_len) + DSR_OPT_HDR_LEN;

	dp.data = skb->data + dp.dsr_opts_len;
	dp.data_len = skb->len - dp.dsr_opts_len;

	/* Get IP stuff that we need */
	dp.src.s_addr = dp.iph->saddr;
	dp.dst.s_addr = dp.iph->daddr;
	
	DEBUG("iph_len=%d iph_totlen=%d dsr_opts_len=%d data_len=%d\n", 
	      (dp.iph->ihl << 2), ntohs(dp.iph->tot_len), dp.dsr_opts_len, dp.data_len);
	/* Process packet */
	action = dsr_opt_recv(&dp);  /* Kernel panics here!!! */

	/* Add mac address of previous hop to the arp table */
	if (dp.srt && skb->mac.raw) {
		struct sockaddr hw_addr;
		struct in_addr prev_hop;
		struct ethhdr *eth;
		int n;
		
		eth = (struct ethhdr *)skb->mac.raw;
			
		memcpy(hw_addr.sa_data, eth->h_source, ETH_ALEN);
		n = dp.srt->laddrs / sizeof(u_int32_t);
		
		/* Find the previous hop */
		if (n == 0)
			prev_hop.s_addr = dp.srt->src.s_addr;
		else
			prev_hop.s_addr = dp.srt->addrs[n-1].s_addr;
		
		kdsr_arpset(prev_hop, &hw_addr, skb->dev);
	}

	/* Check action... */

	if (action & DSR_PKT_SRT_REMOVE) {
		DEBUG("DSR srt options remove!\n");
		
	}
	if (action & DSR_PKT_FORWARD) {
		DEBUG("Forwarding %s", print_ip(dp.src.s_addr));
		printk(" %s", print_ip(dp.dst.s_addr));		
		printk(" nh %s\n", print_ip(dp.nxt_hop.s_addr));

		if (dp.iph->ttl < 1) {
			DEBUG("ttl=0, dropping!\n");
			action = DSR_PKT_DROP;
		} else {
			DEBUG("Forwarding (dev_queue_xmit)\n");
			dsr_dev_xmit(&dp);
		}
	}
	if (action & DSR_PKT_SEND_RREP) {
		struct dsr_srt *srt_rev;

		DEBUG("Send RREP\n");
		
		if (dp.srt) {
			srt_rev = dsr_srt_new_rev(dp.srt);
			
			DEBUG("srt_rev: %s\n", print_srt(srt_rev));
			/* send rrep.... */
			dsr_rrep_send(srt_rev);
			kfree(srt_rev);
		}
	}

	if (action & DSR_PKT_SEND_ICMP) {
		DEBUG("Send ICMP\n");
	}
	if (action & DSR_PKT_SEND_BUFFERED) {
		/* Send buffered packets */
		DEBUG("Sending buffered packets\n");
		if (dp.srt) {
			send_buf_set_verdict(SEND_BUF_SEND, dp.srt->src.s_addr);
		}
	}

	/* Free source route. Should probably think of a better way to handle
	 * source routes that are dynamically allocated. */
	if (dp.srt) {
		DEBUG("Freeing source route\n");
		kfree(dp.srt);
	}

	if (action & DSR_PKT_DELIVER) {
		dsr_opts_remove(&dp);
		DEBUG("Deliver to DSR device\n");
		dsr_dev_deliver(&dp);
		kfree_skb(skb);
		return 0;
	}

	if (action & DSR_PKT_DROP || action & DSR_PKT_ERROR) {
		DEBUG("DSR_PKT_DROP or DSR_PKT_ERROR\n");
		kfree_skb(skb);
		return 0;
	}
	
	return 0;
};

static void dsr_ip_recv_err(struct sk_buff *skb, u32 info)
{
	DEBUG("received error, info=%u\n", info);
	
	kfree_skb(skb);
}



static int kdsr_get_hwaddr(struct in_addr addr, struct sockaddr *hwaddr, 
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

struct sk_buff *kdsr_skb_create(struct dsr_pkt *dp,
				struct net_device *dev)
{
	struct sk_buff *skb;
	char *buf;
	int ip_len;
	int tot_len;
	
	ip_len = dp->iph->ihl << 2;
	
	tot_len = ip_len + dp->dsr_opts_len + dp->data_len;
	
	DEBUG("iph_len=%d dp->data_len=%d dp->dsr_opts_len=%d TOT len=%d\n", 
	      ip_len, dp->data_len, dp->dsr_opts_len, tot_len);
	
	skb = alloc_skb(dev->hard_header_len + 15 + tot_len, GFP_ATOMIC);
	
	if (!skb) {
		DEBUG("alloc_skb failed\n");
		return NULL;
	}
	
	/* We align to 16 bytes, for ethernet: 2 bytes + 14 bytes header */
       	skb_reserve(skb, (dev->hard_header_len+15)&~15); 
	skb->nh.raw = skb->data;
	skb->dev = dev;
	skb->protocol = htons(ETH_P_IP);

	/* Copy in all the headers in the right order */
	buf = skb_put(skb, tot_len);

	memcpy(buf, dp->iph, ip_len);
	
	buf += ip_len;
	
	if (dp->dsr_opts_len && dp->opt_hdr) {
		memcpy(buf, dp->opt_hdr, dp->dsr_opts_len);
		buf += dp->dsr_opts_len;
	}

	if (dp->data_len && dp->data)
		memcpy(buf, dp->data, dp->data_len);
	
	return skb;
}

int kdsr_hw_header_create(struct dsr_pkt *dp, struct sk_buff *skb) 
{

		
	struct sockaddr broadcast = {AF_UNSPEC, {0xff, 0xff, 0xff, 0xff, 0xff, 0xff}};
	struct sockaddr dest;
	
	if (dp->dst.s_addr == DSR_BROADCAST)
		memcpy(dest.sa_data , broadcast.sa_data, ETH_ALEN);
	else {
		/* Get hardware destination address */
		if (kdsr_get_hwaddr(dp->nxt_hop, &dest, skb->dev) < 0) {
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
		  substring_t sub_str;
		  int val = 0;
		  
		  sub_str.from = strstr(cmd, "=");
		  sub_str.from++;  /* Exclude '=' */
		  sub_str.to = cmd + count - 1; /* Exclude trailing '\n' */
		  match_int(&sub_str, &val);
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
	
	if (in == NULL || in->ifindex != get_slave_dev_ifindex() ||
	    iph == NULL || iph->protocol != IPPROTO_DSR || iph->daddr == myaddr.s_addr)
		return NF_ACCEPT;

	dsr_ip_recv(*skb);

	return NF_STOLEN;
}
/* static struct file_operations config_proc_fops = { */
/* 	.read		= dsr_config_proc_read, */
/* 	.write          = dsr_config_proc_write, */
/* }; */

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
/* 	proc->proc_fops = &config_proc_fops; */
	proc->read_proc = dsr_config_proc_read;
	proc->write_proc = dsr_config_proc_write;

#ifndef KERNEL26
	inet_add_protocol(&dsr_inet_prot);
	DEBUG("Setup finished\n");
	return 0;
#else
	
	if (inet_add_protocol(&dsr_inet_prot, IPPROTO_DSR) < 0) {
		DEBUG("Could not register inet protocol\n");
		goto cleanup_proc;
	}
	
	DEBUG("Setup finished res=%d\n", res);
	
	return 0;
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
