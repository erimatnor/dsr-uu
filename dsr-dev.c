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
static struct net_device *dsr_dev;

/* Slave device (WiFi interface) */
static struct net_device *basedev = NULL;
static char *basedevname = NULL;

struct netdev_info ldev_info;
static int dsr_dev_netdev_event(struct notifier_block *this,
				unsigned long event, void *ptr);

static struct notifier_block dsr_dev_notifier = {
	notifier_call: dsr_dev_netdev_event,
};

static int dsr_dev_set_ldev_info(struct net_device *dev) 
{
	struct in_device *indev = NULL;
	struct in_ifaddr **ifap = NULL;
	struct in_ifaddr *ifa = NULL;
	
	indev = in_dev_get(dev);
	
	if (indev) {
		for (ifap = &indev->ifa_list; (ifa = *ifap) != NULL;
		     ifap = &ifa->ifa_next)
			if (!strcmp(dev->name, ifa->ifa_label))
				break;
		
		if (ifa) {
			if (basedev)
				ldev_info.ifindex = basedev->ifindex;
			ldev_info.ifaddr.s_addr = ifa->ifa_address;
			ldev_info.bcaddr.s_addr = ifa->ifa_broadcast;
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

	if (!dev)
		return NOTIFY_DONE;

	switch (event) {
        case NETDEV_REGISTER:
		DEBUG("notifier register %s\n", dev->name);
	
		break;
        case NETDEV_UP:
		DEBUG("notifier up %s\n", dev->name);
		if (basedev == NULL && dev->get_wireless_stats) {
			basedev = dev;
			basedevname = dev->name;
			DEBUG("new dsr slave interface %s\n", dev->name);
		} else if (dev == dsr_dev) {
			int res;
			
			res = dsr_dev_set_ldev_info(dev);

			if (res < 0)
				return NOTIFY_DONE;
		}
		break;
        case NETDEV_UNREGISTER:
		DEBUG("notifier unregister %s\n", dev->name);		
		break;
        case NETDEV_DOWN:
		DEBUG("notifier down %s\n", dev->name);
                if (dev == basedev) {
                        DEBUG("dsr slave interface %s went away\n", dev->name);
			basedev = NULL;
			basedevname = NULL;
			//netbox_dropL2if(dev->ifindex);
                }
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
/* 	if (basedev) { */
/* 		int i = basedev->ifindex; */
/* 		basedev = 0; */
/* 		netbox_dropL2if(i); */
/* 	} */
        netif_stop_queue(dev);

        return 0;
}

static void __init dsr_dev_setup(struct net_device *dev)
{
	/* Fill in device structure with ethernet-generic values. */
	ether_setup(dev);
	/* Initialize the device structure. */
	dev->get_stats = dsr_dev_get_stats;
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


/* Transmit a DSR packet... this function assumes that the packet has a valid
 * source route already. */
int dsr_dev_queue_xmit(dsr_pkt_t *dp)
{
	struct net_device_stats *stats = dsr_dev->priv;
	struct ethhdr *ethh;
	struct sockaddr hw_addr;
		
	if (!dp)
		return -1;
	
	ethh = dp->skb->mac.ethernet;
	
	switch (ntohs(ethh->h_proto)) {
	case ETH_P_IP:	
		
		if (kdsr_get_hwaddr(dp->nh, &hw_addr, dp->skb->dev) < 0)
			break;
		
		/* Build hw header */
		if (dsr_dev_build_hw_hdr(dp->skb, &hw_addr) < 0)
			break;
		
		/* Send packet */
		dev_queue_xmit(dp->skb);
		
		stats->tx_packets++;
		stats->tx_bytes+=dp->skb->data_len;
		
		/* We must free the DSR packet */
		dsr_pkt_free(dp);
		return 0;
	default:
		DEBUG("Unkown packet type\n");
	}
	
	DEBUG("Could not send packet, freeing...\n");
	dev_kfree_skb(dp->skb);
	dsr_pkt_free(dp);
	return -1;
}

/* Main receive function for packets originated in user space */
static int dsr_dev_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct net_device_stats *stats = dev->priv;
	struct ethhdr *ethh;
	struct net_device *slave_dev;
	dsr_pkt_t *dp;
	int res = 0;
	
	/* Allocate a DSR packet */
	dp = dsr_pkt_alloc();
	dp->skb = skb;
	dp->data = skb->data;
	dp->len = skb->len;

	ethh = (struct ethhdr *)skb->data;
	
	switch (ntohs(ethh->h_proto)) {
	case ETH_P_IP:
		
		slave_dev = dev_get_by_index(ldev_info.ifindex);
		skb->dev = slave_dev;
		dev_put(slave_dev);
		
		dp->src.s_addr = skb->nh.iph->saddr;
		dp->dst.s_addr = skb->nh.iph->daddr;
					
		dp->srt = dsr_rtc_find(dp->dst);
		
		if (dp->srt) {
			struct sockaddr hw_addr;
		       
			if (dp->srt->laddrs == 0)
				dp->nh.s_addr = dp->srt->dst.s_addr;
			else
				dp->nh.s_addr = dp->srt->addrs[0].s_addr;
			
			/* Add source route */
			if (dsr_srt_add(dp) < 0)
				break;
			
			if (kdsr_get_hwaddr(dp->dst, &hw_addr, skb->dev) < 0)
				break;
			
			/* Build hw header */
			if (dsr_dev_build_hw_hdr(skb, &hw_addr) < 0)
				break;
			
			/* Send packet */
			dev_queue_xmit(skb);
		
			stats->tx_packets++;
			stats->tx_bytes+=skb->len;
			
			/* We must free the DSR packet */
			dsr_pkt_free(dp);
		} else {			
			p_queue_enqueue_packet(dp, dsr_dev_queue_xmit);
			
			res = dsr_rreq_send(dp->dst);
			
			if (res < 0)
				DEBUG("Transmission failed...");
		}
		break;
	default:
		DEBUG("Unkown packet type\n");
		dsr_pkt_free(dp);
		dev_kfree_skb(skb);
	}	
	return 0;
}

static struct net_device_stats *dsr_dev_get_stats(struct net_device *dev)
{
	return dev->priv;
}

int __init dsr_dev_init(char *ifname)
{ 
	int res = 0;	
	struct net_device *dev = NULL;

	basedevname = ifname;
	
	dsr_dev = alloc_netdev(sizeof(struct net_device_stats),
			       "dsr%d", dsr_dev_setup);

	if (!dsr_dev)
		return -ENOMEM;

	if (basedevname) {
		dev = dev_get_by_name(basedevname);
		if (!dev) {
			DEBUG("device %s not found\n", basedevname);
			res = -1;
			goto cleanup_netdev;
		} 
		
		if (dev == dsr_dev) {
			DEBUG("invalid base device %s\n", basedevname);
			res = -1;
			dev_put(dev);
			goto cleanup_netdev;
		}		
		basedev = dev;

		if (dev)
			dev_put(dev);
	
	} else {
		read_lock(&dev_base_lock);
		for (dev = dev_base; dev != NULL; dev = dev->next) {
			if (dev->get_wireless_stats)
				break;
		}
		if (dev) {
			basedev = dev;
			basedevname = dev->name;
			DEBUG("wireless interface is %s\n", dev->name);
		}
		read_unlock(&dev_base_lock);
	}
	
	DEBUG("Setting %s as slave interface\n", basedev->name);

	res = register_netdev(dsr_dev);

	if (res < 0)
		goto cleanup_netdev;

	res = register_netdevice_notifier(&dsr_dev_notifier);

	if (res < 0)
		goto cleanup_netdev_register;
	
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
