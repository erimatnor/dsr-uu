#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/inetdevice.h>
#include <linux/etherdevice.h>
#include <linux/init.h>
#include <linux/if_ether.h>
#include <net/ip.h>
#include <linux/random.h>

#include "debug.h"
#include "dsr.h"
#include "kdsr.h"
#include "dsr-rreq.h"
#include "dsr-rtc.h"
#include "dsr-srt.h"
#include "p-queue.h"

/* Our dsr device */
struct net_device *dsr_dev;
struct dsr_node *dsr_node;
/* Slave device (WiFi interface) */
//static struct net_device *basedev = NULL;
//static char *basedevname = NULL;


//struct netdev_info ldev_info;
static int dsr_dev_netdev_event(struct notifier_block *this,
				unsigned long event, void *ptr);

static struct notifier_block dsr_dev_notifier = {
	notifier_call: dsr_dev_netdev_event,
};

static int dsr_dev_set_node_info(struct net_device *dev) 
{
	struct in_device *indev = NULL;
	struct in_ifaddr **ifap = NULL;
	struct in_ifaddr *ifa = NULL;
	struct dsr_node *dnode = dev->priv;

	indev = in_dev_get(dev);
	
	if (indev) {
		for (ifap = &indev->ifa_list; (ifa = *ifap) != NULL;
		     ifap = &ifa->ifa_next)
			if (!strcmp(dev->name, ifa->ifa_label))
				break;
		
		if (ifa) {
			dsr_node_lock(dnode);
			dnode->ifaddr.s_addr = ifa->ifa_address;
			dnode->bcaddr.s_addr = ifa->ifa_broadcast;
			dsr_node_unlock(dnode);
			
			DEBUG("dsr ip=%s, broadcast=%s\n", 
			      print_ip(ifa->ifa_address), 
			      print_ip(ifa->ifa_broadcast));
		}
		in_dev_put(indev);
	} else {
		DEBUG("could not get ldev_info from indev\n");
		return -1;
	}
	return 1;
}

/* From kernel lunar */
static int dsr_dev_netdev_event(struct notifier_block *this,
                              unsigned long event, void *ptr)
{
        struct net_device *dev = (struct net_device *) ptr;
	struct dsr_node *dnode = dsr_dev->priv;

	if (!dev)
		return NOTIFY_DONE;

	switch (event) {
        case NETDEV_REGISTER:
		DEBUG("notifier register %s\n", dev->name);
		if (dnode->slave_dev == NULL && dev->get_wireless_stats) {
			dsr_node_lock(dnode);
			dnode->slave_dev = dev;
			dsr_node_unlock(dnode);
			dev_hold(dnode->slave_dev);
			DEBUG("new dsr slave interface %s\n", dev->name);
		} 
		break;
	case NETDEV_CHANGE:
		DEBUG("Netdev change\n");
		break;
        case NETDEV_UP:
	case NETDEV_CHANGEADDR:
		DEBUG("notifier up %s\n", dev->name);
		if (dev == dsr_dev) {
			int res;
			
			res = dsr_dev_set_node_info(dev);

			if (res < 0)
				return NOTIFY_DONE;
		}
		break;
        case NETDEV_UNREGISTER:
		DEBUG("notifier unregister %s\n", dev->name); 
		if (dev == dnode->slave_dev) {
                        DEBUG("dsr slave interface %s went away\n", dev->name);
			dsr_node_lock(dnode);
			dev_put(dnode->slave_dev);
			dnode->slave_dev = NULL;
			dsr_node_unlock(dnode);
                }
		break;
        case NETDEV_DOWN:
		DEBUG("notifier down %s\n", dev->name);
		
                break;

        default:
                break;
        };

        return NOTIFY_DONE;
}

static int dsr_dev_xmit(struct sk_buff *skb, struct net_device *dev);
static struct net_device_stats *dsr_dev_get_stats(struct net_device *dev);

static int dsr_dev_set_address(struct net_device *dev, void *p)
{
	struct sockaddr *sa = p;

	if (!is_valid_ether_addr(sa->sa_data)) 
		return -EADDRNOTAVAIL;
		
	memcpy(dev->dev_addr, sa->sa_data, ETH_ALEN);
	return 0;
}

/* fake multicast ability */
static void set_multicast_list(struct net_device *dev)
{
}

#ifdef CONFIG_NET_FASTROUTE
static int dsr_dev_accept_fastpath(struct net_device *dev, struct dst_entry *dst)
{
	return -1;
}
#endif
static int dsr_dev_open(struct net_device *dev)
{
	netif_start_queue(dev);
        return 0;
}

static int dsr_dev_stop(struct net_device *dev)
{
        netif_stop_queue(dev);
        return 0;
}

static void dsr_dev_uninit(struct net_device *dev)
{
	struct dsr_node *dnode = dev->priv;
	
	DEBUG("Calling dev_put on interfaces dnode->slave_dev=%u dsr_dev=%u\n",
	      (unsigned int)dnode->slave_dev, (unsigned int)dsr_dev);

	dsr_node_lock(dnode);
	if (dnode->slave_dev)
		dev_put(dnode->slave_dev);
	dsr_node_unlock(dnode);
	dev_put(dsr_dev);
	dsr_node = NULL;
}

static void __init dsr_dev_setup(struct net_device *dev)
{
	/* Fill in device structure with ethernet-generic values. */
	ether_setup(dev);
	/* Initialize the device structure. */
	dev->get_stats = dsr_dev_get_stats;
	dev->uninit = dsr_dev_uninit;
	dev->open = dsr_dev_open;
	dev->stop = dsr_dev_stop;
	dev->hard_start_xmit = dsr_dev_xmit;
	dev->set_multicast_list = set_multicast_list;
	dev->set_mac_address = dsr_dev_set_address;
#ifdef CONFIG_NET_FASTROUTE
	dev->accept_fastpath = dsr_dev_accept_fastpath;
#endif

	dev->tx_queue_len = 0;
	dev->flags |= IFF_NOARP;
	dev->flags &= ~IFF_MULTICAST;
	SET_MODULE_OWNER(dev);
	//random_ether_addr(dev->dev_addr);
	get_random_bytes(dev->dev_addr, 6);
}

int dsr_dev_build_hw_hdr(struct sk_buff *skb, struct sockaddr *dest)
{
	
	if (skb->dev && skb->dev->hard_header) {
		skb->dev->hard_header(skb, skb->dev, ETH_P_IP, 
				      dest->sa_data, 0, skb->len);
		return 0;
	} 
	DEBUG("no dev->hard_header...\n");
	return -1;
}

int dsr_dev_deliver(struct sk_buff *skb)
{	
	struct dsr_node *dnode = skb->dev->priv;
	int res;

	skb->protocol = htons(ETH_P_IP);
	skb->pkt_type = PACKET_HOST;
	
	dsr_node_lock(dnode);
	dsr_node->stats.rx_packets++;
	dsr_node->stats.rx_bytes += skb->len;
	dsr_node_unlock(dnode);

	skb->dev = dsr_dev;
	dst_release(skb->dst);
	skb->dst = NULL;
#ifdef CONFIG_NETFILTER
	nf_conntrack_put(skb->nfct);
	skb->nfct = NULL;
#ifdef CONFIG_NETFILTER_DEBUG
	skb->nf_debug = 0;
#endif
#endif
	res = netif_rx(skb);	
	
	if (res == NET_RX_DROP) {
		DEBUG("Netif_rx DROP\n");
	}
	return res;
}



/* Transmit a DSR packet... this function assumes that the packet has a valid
 * source route already. */
/* int dsr_dev_queue_xmit(dsr_pkt_t *dp) */
/* { */
/* 	struct dsr_node *dnode = dsr_dev->priv; */
/* 	struct ethhdr *ethh; */
/* 	struct sockaddr hw_addr; */
		
/* 	if (!dp) */
/* 		return -1; */

/* 	if (!dp->skb) { */
/* 		dsr_pkt_free(dp); */
/* 		return -1; */
/* 	}	 */

/* 	dp->skb->dev = dnode->slave_dev; */

/* 	ethh = (struct ethhdr *)dp->skb->data; */

/* 	if (kdsr_get_hwaddr(dp->nh, &hw_addr, dp->skb->dev) < 0) */
/* 		goto out_err; */
	
/* 	DEBUG("Transmitting head=%d skb->data=%lu skb->nh.iph=%lu\n", skb_headroom(dp->skb), (unsigned long)dp->skb->data, (unsigned long)dp->skb->nh.iph); */

/*  	/\* Build hw header *\/  */
/* 	dp->skb->dev->rebuild_header(dp->skb); */

/* 	memcpy(ethh->h_source, dp->skb->dev->dev_addr, dp->skb->dev->addr_len); */

/* 	/\* Send packet *\/ */
/* 	dev_queue_xmit(dp->skb); */
	
/* 	dsr_node_lock(dnode); */
/* 	dnode->stats.tx_packets++; */
/* 	dnode->stats.tx_bytes+=dp->skb->len; */
/* 	dsr_node_unlock(dnode); */
/* 	/\* We must free the DSR packet *\/ */
		
/* 	dsr_pkt_free(dp); */
/* 	return 0; */
/* /\* 	default: *\/ */
/* /\* 		DEBUG("Unkown packet type\n"); *\/ */
/* /\* 	} *\/ */
/*  out_err: */
/* 	DEBUG("Could not send packet, freeing...\n"); */
/* 	dev_kfree_skb(dp->skb); */
/* 	dsr_pkt_free(dp); */
/* 	return -1; */
/* } */

/* Main receive function for packets originated in user space */
static int dsr_dev_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct dsr_node *dnode = (struct dsr_node *)dev->priv;
	struct ethhdr *ethh;
	dsr_srt_t *srt = NULL;
	struct in_addr dst;
	int res = 0;
	
	ethh = (struct ethhdr *)skb->data;

	DEBUG("headroom=%d skb->data=%lu skb->nh.iph=%lu\n", 
	      skb_headroom(skb), (unsigned long)skb->data, 
	      (unsigned long)skb->nh.iph);

	switch (ntohs(ethh->h_proto)) {
	case ETH_P_IP:
		
		/* slave_dev = dev_get_by_index(.ifindex); */
		skb->dev = dnode->slave_dev;
	/* 	dev_put(slave_dev); */
		dst.s_addr = skb->nh.iph->daddr;

		srt = dsr_rtc_find(dst);
		
		if (srt) {
			dsr_pkt_t *dp;
			struct sockaddr hw_addr;
		       
				/* Allocate a DSR packet */
			dp = dsr_pkt_alloc();
			dp->skb = skb;
			dp->data = skb->data;
			dp->len = skb->len;
	
			dp->src.s_addr = skb->nh.iph->saddr;
			dp->dst.s_addr = skb->nh.iph->daddr;
			
			dp->srt = srt;
		
			if (dp->srt->laddrs == 0)
				dp->nh.s_addr = dp->srt->dst.s_addr;
			else
				dp->nh.s_addr = dp->srt->addrs[0].s_addr;
			
			/* Add source route */
			if (dsr_srt_add(dp) < 0) {
				dev_kfree_skb(skb);
				dsr_pkt_free(dp);
				break;
			}
				
			if (kdsr_get_hwaddr(dp->nh, &hw_addr, skb->dev) < 0) {
				dev_kfree_skb(skb);
				dsr_pkt_free(dp);
				break;
			}
			
			dp->skb->dev->rebuild_header(dp->skb);

			memcpy(ethh->h_source, skb->dev->dev_addr, skb->dev->addr_len);

			/* Send packet */
			dev_queue_xmit(skb);
			
			dsr_node_lock(dnode);
			dnode->stats.tx_packets++;
			dnode->stats.tx_bytes+=skb->len;
			dsr_node_unlock(dnode);

			/* We must free the DSR packet */
			dsr_pkt_free(dp);
		} else {			
			res = p_queue_enqueue_packet(skb, dev_queue_xmit);
			
			if (res < 0) {
				DEBUG("Queueing failed!\n");
			/* 	dsr_pkt_free(dp); */
				dev_kfree_skb(skb);
				return -1;
			}
			res = dsr_rreq_send(dst);
			
			if (res < 0)
				DEBUG("Transmission failed...");
		}
		break;
	default:
		DEBUG("Unkown packet type\n");
		dev_kfree_skb(skb);
	}	
	return 0;
}

static struct net_device_stats *dsr_dev_get_stats(struct net_device *dev)
{
	return &(((struct dsr_node*)dev->priv)->stats);
}

int __init dsr_dev_init(char *ifname)
{ 
	int res = 0;	
	struct dsr_node *dnode;

	dsr_dev = alloc_netdev(sizeof(struct dsr_node),
			       "dsr%d", dsr_dev_setup);

	if (!dsr_dev)
		return -ENOMEM;

	dnode = dsr_node = (struct dsr_node *)dsr_dev->priv;

	spin_lock_init(&dnode->lock);

	if (ifname) {
		dnode->slave_dev = dev_get_by_name(ifname);
		
		if (!dnode->slave_dev) {
			DEBUG("device %s not found\n", ifname);
			res = -1;
			goto cleanup_netdev;
		} 
		
		if (dnode->slave_dev == dsr_dev) {
			DEBUG("invalid slave device %s\n", ifname);
			res = -1;
			dev_put(dnode->slave_dev);
			goto cleanup_netdev;
		}	
	} else {
		read_lock(&dev_base_lock);
		for (dnode->slave_dev = dev_base; 
		     dnode->slave_dev != NULL; 
		     dnode->slave_dev = dnode->slave_dev->next) {
			
			if (dnode->slave_dev->get_wireless_stats)
				break;
		}
		read_unlock(&dev_base_lock);
		
		if (dnode->slave_dev) {
			dev_hold(dnode->slave_dev);
			DEBUG("wireless interface is %s\n", 
			      dnode->slave_dev->name);
		} else {
			DEBUG("No proper slave device found\n");
			res = -1;
			goto cleanup_netdev;
		}
	}
	
	DEBUG("Setting %s as slave interface\n", dnode->slave_dev->name);

	res = register_netdev(dsr_dev);

	if (res < 0)
		goto cleanup_netdev;

	res = register_netdevice_notifier(&dsr_dev_notifier);

	if (res < 0)
		goto cleanup_netdev_register;
	
	/* We must increment usage count since we hold a reference */
	dev_hold(dsr_dev);
	return res;
	
 cleanup_netdev_register:
	unregister_netdev(dsr_dev);
 cleanup_netdev:
	free_netdev(dsr_dev);
	return res;
} 

void __exit dsr_dev_cleanup(void)
{
        unregister_netdevice_notifier(&dsr_dev_notifier);
	unregister_netdev(dsr_dev);
	free_netdev(dsr_dev);
}
