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
#include <linux/ctype.h>

#include "dsr.h"
#include "dsr-dev.h"
#include "dsr-io.h"
#include "dsr-pkt.h"
#include "debug.h"
#include "neigh.h"
#include "dsr-rreq.h"
#include "maint-buf.h"
#include "send-buf.h"

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

static char *confval_names[CONFVAL_TYPE_MAX] = { "Seconds (s)", 
						 "Milliseconds (ms)", 
						 "Microseconds (us)", 
						 "Nanoseconds (ns)", 
						 "Quanta", 
						 "Binary" };

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
#ifdef ENABLE_DISABLED
static int dsr_arpset(struct in_addr addr, struct sockaddr *hw_addr, 
		       struct net_device *dev)
{
	struct neighbour *neigh;

	DEBUG("Setting arp for %s %s\n", print_ip(addr), 
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
#endif

static int is_promisc_recv(struct sk_buff *skb)
{
	struct ethhdr *ethh;
	char bc[ETH_ALEN] = { 0xff,0xff,0xff,0xff,0xff,0xff };
	int res = 1;
	
	ethh = (struct ethhdr *)skb->mac.raw;
	
	dsr_node_lock(dsr_node);
	if (dsr_node->slave_dev) {

		DEBUG("dst=%s\n",print_eth(ethh->h_dest));
		DEBUG("bc=%s\n",print_eth(bc));
		
		if (memcmp(ethh->h_dest, dsr_node->slave_dev->dev_addr,
			   ETH_ALEN) == 0 ||
		    memcmp(ethh->h_dest, bc, ETH_ALEN) == 0)
			res = 0;
	}
	dsr_node_unlock(dsr_node);
		
	return res;
}
static int dsr_ip_recv(struct sk_buff *skb)
{
	struct dsr_pkt *dp;
#ifdef DEBUG
	atomic_inc(&num_pkts);
#endif 
	DEBUG("Received DSR packet\n");
	
	dp = dsr_pkt_alloc(skb);

	if (!dp) {
		DEBUG("Could not allocate DSR packet\n");
		return -1;
	}
	
	if (is_promisc_recv(skb)) {
		DEBUG("Setting flag PKT_PROMISC_RECV\n");
		dp->flags |= PKT_PROMISC_RECV;
	}
	if ((skb->len + (dp->nh.iph->ihl << 2)) < ntohs(dp->nh.iph->tot_len)) {
		DEBUG("data to short according to IP header len=%d tot_len=%d!\n", skb->len + (dp->nh.iph->ihl << 2), ntohs(dp->nh.iph->tot_len));
		dsr_pkt_free(dp);
		return -1;
	}
	
	
	DEBUG("iph_len=%d iph_totlen=%d dsr_opts_len=%d data_len=%d\n", 
	      (dp->nh.iph->ihl << 2), ntohs(dp->nh.iph->tot_len), dsr_pkt_opts_len(dp), dp->payload_len);

	
	/* Add mac address of previous hop to the arp table */
	dsr_recv(dp);
	
	return 0;
};

static void dsr_ip_recv_err(struct sk_buff *skb, u32 info)
{
	DEBUG("received error, info=%u\n", info);
	
	kfree_skb(skb);
}

struct sk_buff *dsr_skb_create(struct dsr_pkt *dp,
			       struct net_device *dev)
{
	struct sk_buff *skb;
	char *buf;
	int ip_len;
	int tot_len;
	int dsr_opts_len = dsr_pkt_opts_len(dp);
	
	ip_len = dp->nh.iph->ihl << 2;
	
	tot_len = ip_len + dsr_opts_len + dp->payload_len;
	
	DEBUG("ip_len=%d dsr_opts_len=%d payload_len=%d tot_len=%d\n", 
	      ip_len, dsr_opts_len, dp->payload_len, tot_len);
#ifdef KERNEL26
	skb = alloc_skb(tot_len +  LL_RESERVED_SPACE(dev), GFP_ATOMIC);
#else
	skb = alloc_skb(dev->hard_header_len + 15 + tot_len, GFP_ATOMIC);
#endif
	if (!skb) {
		DEBUG("alloc_skb failed\n");
		return NULL;
	}
	
	/* We align to 16 bytes, for ethernet: 2 bytes + 14 bytes header */
#ifdef KERNEL26
	skb_reserve(skb, LL_RESERVED_SPACE(dev));
#else
       	skb_reserve(skb, (dev->hard_header_len+15)&~15);
#endif
	skb->nh.raw = skb->data;
	skb->dev = dev;
	skb->protocol = htons(ETH_P_IP);

	/* Copy in all the headers in the right order */
	buf = skb_put(skb, tot_len);

	memcpy(buf, dp->nh.raw, ip_len);
	
	buf += ip_len;
	
	/* Add DSR header if it exists */
	if (dsr_opts_len) {
		memcpy(buf, dp->dh.raw, dsr_opts_len);
		buf += dsr_opts_len;
	}

	/* Add payload */
	if (dp->payload_len && dp->payload)
		memcpy(buf, dp->payload, dp->payload_len);
	
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
		if (neigh_tbl_get_hwaddr(dp->nxt_hop, &dest) < 0) {
			DEBUG("Could not get hardware address for next hop %s\n", print_ip(dp->nxt_hop));
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
	
	for (i = 0; i < CONFVAL_MAX; i++)
		len += sprintf(buffer+len, "%s=%u %30s\n", 
			       confvals_def[i].name, 
			       get_confval(i), 
			       confval_names[confvals_def[i].type]);
    
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
  char cmd[CMD_MAX_LEN];
  int i;
  
  memset(cmd, '\0', CMD_MAX_LEN);

  if (count > CMD_MAX_LEN )
	  return -EINVAL;
  
  /* Don't read the '\n' or '\0' character... */
  if(copy_from_user(cmd, buffer, count))
	  return -EFAULT;

  for (i = 0; i < CONFVAL_MAX; i++) {
	  int n = strlen(confvals_def[i].name);
	  
	  if (strlen(cmd) - 2 <= n)
		  continue;

	  if (strncmp(cmd, confvals_def[i].name, n) == 0) {
		  char *from, *to;
		  unsigned int val, val_prev;
		  
		  from = strstr(cmd, "="); 
		  from++; /* Exclude '=' */
		  val_prev = ConfVal(i);
		  val = simple_strtol(from, &to, 10); 

		  if (confvals_def[i].type == BIN)
			  val = (val ? 1 : 0);
		  
		  set_confval(i, val);
		  
		  if (i == PromiscOperation && val_prev != val && dsr_node) {
			  if (val)
				  DEBUG("Setting promiscuous operation\n");
			  else
				  DEBUG("Disabling promiscuous operation\n");
			  
			  dsr_node_lock(dsr_node);
			  dev_set_promiscuity(dsr_node->slave_dev, val ? 1 : -1);
			  dsr_node_unlock(dsr_node);
		  }
		  if (i == RequestTableSize)
			  rreq_tbl_set_max_len(val);
		  
		  if (i == RexmtBufferSize)
			  maint_buf_set_max_len(val);

		  if (i == SendBufferSize)
			  send_buf_set_max_len(val);
		  
		  DEBUG("Setting %s to %d\n",  confvals_def[i].name, val);
	  }
  }
  return count;
}


/* This hook is used to do mac filtering or to receive promiscuously snooped
 * packets */
static unsigned int dsr_pre_routing_recv(unsigned int hooknum,
					 struct sk_buff **skb,
					 const struct net_device *in,
					 const struct net_device *out,
					 int (*okfn) (struct sk_buff *))
{	
	if (in && in->ifindex == get_slave_dev_ifindex() && 
	    (*skb)->protocol == htons(ETH_P_IP)) {
		
		if (do_mackill((*skb)->mac.raw + ETH_ALEN))
			return NF_DROP;
		
		if (is_promisc_recv(*skb)) {
			dsr_ip_recv(*skb);
			return NF_STOLEN;
		}
	}
	return NF_ACCEPT;	
}

/* This hook is the architecturally correct place to look at DSR packets that
 * are to be forwarded. This enables you to, for example, disable forwarding by
 * setting "/proc/sys/net/ipv4/conf/<eth*>/forwarding" to 0. */
static unsigned int dsr_ip_forward_recv(unsigned int hooknum,
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

static struct nf_hook_ops dsr_ip_forward_hook = {
	
	.hook		= dsr_ip_forward_recv,
#ifdef KERNEL26
	.owner		= THIS_MODULE,
#endif
	.pf		= PF_INET,
	.hooknum	= NF_IP_FORWARD,
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

#ifdef DEBUG
	dbg_init();
#endif
	parse_mackill();
	

	res = dsr_dev_init(ifname);

	if (res < 0) {
		DEBUG("dsr-dev init failed\n");
		return -EAGAIN;
	}
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

	res = nf_register_hook(&dsr_ip_forward_hook);
	
	if (res < 0)
		goto cleanup_nf_hook2;
	
	res = maint_buf_init();

	if (res < 0)
		goto cleanup_nf_hook1;

	proc = create_proc_entry(CONFIG_PROC_NAME, S_IRUGO | S_IWUSR, proc_net);
	
	if (!proc)
		goto cleanup_maint_buf;
	
	proc->owner = THIS_MODULE;
	proc->read_proc = dsr_config_proc_read;
	proc->write_proc = dsr_config_proc_write;

#ifndef KERNEL26
	inet_add_protocol(&dsr_inet_prot);
	DEBUG("Setup finished\n");
	return 0;
#else
	res = inet_add_protocol(&dsr_inet_prot, IPPROTO_DSR);
	
	if (res < 0) {
		DEBUG("Could not register inet protocol\n");
		goto cleanup_proc;
	}
	
	DEBUG("Setup finished res=%d\n", res);
	
	return 0;
 cleanup_proc:
	proc_net_remove(CONFIG_PROC_NAME);
#endif

 cleanup_maint_buf:
	maint_buf_cleanup();
 cleanup_nf_hook1:
	nf_unregister_hook(&dsr_ip_forward_hook);
 cleanup_nf_hook2:
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
	proc_net_remove(CONFIG_PROC_NAME);
	nf_unregister_hook(&dsr_pre_routing_hook);
	nf_unregister_hook(&dsr_ip_forward_hook);
	send_buf_cleanup();
	rreq_tbl_cleanup();
	neigh_tbl_cleanup();
	maint_buf_cleanup();
	dsr_dev_cleanup();
#ifdef DEBUG
	dbg_cleanup();
#endif
}

module_init(dsr_module_init);
module_exit(dsr_module_cleanup);
