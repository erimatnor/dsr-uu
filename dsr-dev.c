
#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/inetdevice.h>
#include <linux/etherdevice.h>
#include <linux/init.h>
#include <linux/if_ether.h>
#include <linux/ip.h>
#ifdef KERNEL26
#include <linux/moduleparam.h>
#endif

#include "debug.h"
#include "dsr.h"
#include "dsr-rreq.h"
#include "p-queue.h"

static struct net_device *basedev;

struct netdev_info ldev_info;

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

static void __init dsr_dev_setup(struct net_device *dev)
{
	/* Initialize the device structure. */
	dev->get_stats = dsr_dev_get_stats;
	dev->hard_start_xmit = dsr_dev_xmit;
	dev->set_multicast_list = set_multicast_list;
	dev->set_mac_address = dsr_dev_set_address;
#ifdef CONFIG_NET_FASTROUTE
	dev->accept_fastpath = dsr_dev_accept_fastpath;
#endif

	/* Fill in device structure with ethernet-generic values. */
	ether_setup(dev);
	dev->tx_queue_len = 0;
	dev->flags |= IFF_NOARP;
	dev->flags &= ~IFF_MULTICAST;
	SET_MODULE_OWNER(dev);
	random_ether_addr(dev->dev_addr);
}

static int dsr_dev_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct net_device_stats *stats = dev->priv;
	struct ethhdr *ethh;
	struct iphdr *iph;
	struct sk_buff *rreq_skb;
	int res = 0;

	ethh = (struct ethhdr *)skb->data;
	
	switch (ntohs(ethh->h_proto)) {
	case ETH_P_IP:
		iph = (struct iphdr *)(skb->data + sizeof(struct ethhdr));
		DEBUG("IP packet src=%s\n", print_ip(iph->saddr));

		
		p_queue_enqueue_packet(skb, dev_queue_xmit);
		
		rreq_skb = dsr_rreq_create(iph->daddr);
		
		if (!rreq_skb) {
			DEBUG("RREQ creation failed!\n");
			return -1;
		}

		res = dev_queue_xmit(rreq_skb);
		
		if (res < 0)
			DEBUG("RREQ transmission failed... Free skb?\n");
		break;
	default:
		DEBUG("Unkown packet type\n");
	}
	stats->tx_packets++;
	stats->tx_bytes+=skb->len;

	dev_kfree_skb(skb);
	return 0;
}

static struct net_device_stats *dsr_dev_get_stats(struct net_device *dev)
{
	return dev->priv;
}

static struct net_device *dsr_dev;

static char *ifname = NULL;

#ifdef KERNEL26
module_param(ifname, charp, 0);
#else
MODULE_PARM(ifname, "s");
#endif

static int __init dsr_dev_init(void)
{ 
	int err = 0;
	struct net_device *dev = NULL;
	struct in_device *indev = NULL;
	struct in_ifaddr **ifap = NULL;
	struct in_ifaddr *ifa = NULL;

	dsr_dev = alloc_netdev(sizeof(struct net_device_stats),
			       "dsr%d", dsr_dev_setup);

	if (!dsr_dev)
		return -ENOMEM;

	if ((err = register_netdev(dsr_dev))) {
		free_netdev(dsr_dev);
		dsr_dev = NULL;
	} 
	if (ifname) {
		dev = dev_get_by_name(ifname);
		if (!dev) {
			DEBUG("device %s not found\n", ifname);
		} else {
			dev_put(dev);
			if (dev == dsr_dev) {
				DEBUG("invalid base device %s\n",
				      ifname);
				dev = 0;
			}
		}
		basedev = dev;
		/* if (dev) */
/* 			netbox_addL2if(dev->ifindex, mysapfhandle); */
	} else {
		read_lock(&dev_base_lock);
		for (dev = dev_base; dev != NULL; dev = dev->next) {
			if (dev->get_wireless_stats)
				break;
		}
		if (dev) {
			basedev = dev;
			DEBUG("wireless interface is %s\n",
			      dev->name);
			/* netbox_addL2if(dev->ifindex, mysapfhandle); */
		}
		read_unlock(&dev_base_lock);
	}
	
	indev = in_dev_get(dev);
	
	if (indev) {
		for (ifap = &indev->ifa_list; (ifa = *ifap) != NULL;
		     ifap = &ifa->ifa_next)
			if (!strcmp(dev->name, ifa->ifa_label))
				break;
		
		if (ifa) {
			ldev_info.ifindex = dev->ifindex;
			ldev_info.ip_addr = ifa->ifa_address;
			ldev_info.bc_addr = ifa->ifa_broadcast;
		}
		in_dev_put(indev);
	}
	return err;
} 

static void __exit dsr_dev_cleanup(void)
{
	if (basedev)
		dev_put(basedev);

	unregister_netdev(dsr_dev);
	free_netdev(dsr_dev);
}

module_init(dsr_dev_init);
module_exit(dsr_dev_cleanup);
MODULE_LICENSE("GPL");
