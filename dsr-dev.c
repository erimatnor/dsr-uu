
#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/inetdevice.h>
#include <linux/etherdevice.h>
#include <linux/init.h>
#include <linux/if_ether.h>
#include <linux/ip.h>

#include "debug.h"
#include "dsr.h"
#include "dsr-rreq.h"
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
			ldev_info.ip_addr = ifa->ifa_address;
			ldev_info.bc_addr = ifa->ifa_broadcast;
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

	/* Fill in device structure with ethernet-generic values. */
	ether_setup(dev);
	dev->tx_queue_len = 0;
	dev->flags |= IFF_NOARP;
	dev->flags &= ~IFF_MULTICAST;
	SET_MODULE_OWNER(dev);
	random_ether_addr(dev->dev_addr);
}

static int dsr_pkt_send(struct sk_buff *skb, struct sockaddr *dest, 
			struct net_device *dev)
{
	int res = 0;
	
	if (dev->hard_header) {
		dev->hard_header(skb, dev, ETH_P_IP, 
				 dest->sa_data, 0, skb->len);
		
		res = dev_queue_xmit(skb);
		
		if (res < 0)
			DEBUG("xmit failed\n");
	} else {
		DEBUG("no dev->hard_header...\n");
		dev_kfree_skb(skb);
	}
	return res;
}

static int dsr_dev_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct net_device_stats *stats = dev->priv;
	struct ethhdr *ethh;
	struct iphdr *iph;
	struct sk_buff *rreq_skb;
	struct sockaddr broadcast = {AF_UNSPEC, {0xff, 0xff, 0xff, 0xff, 0xff, 0xff}};
	int res = 0;

	ethh = (struct ethhdr *)skb->data;
	
	switch (ntohs(ethh->h_proto)) {
	case ETH_P_IP:
		iph = (struct iphdr *)(skb->data + sizeof(struct ethhdr));
			
		p_queue_enqueue_packet(skb, dev_queue_xmit);
		
		rreq_skb = dsr_rreq_create(iph->daddr, basedev);
		
		if (!rreq_skb) {
			DEBUG("RREQ creation failed!\n");
			return -1;
		}
		res = dsr_pkt_send(rreq_skb, &broadcast, basedev);
		
		if (res < 0) 
			DEBUG("RREQ transmission failed... Free skb?\n");
		break;
	default:
		DEBUG("Unkown packet type\n");
		dev_kfree_skb(skb);
	}
	stats->tx_packets++;
	stats->tx_bytes+=skb->len;

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

	res = register_netdev(dsr_dev);

	if (res < 0)
		goto cleanup_netdev;

	res = register_netdevice_notifier(&dsr_dev_notifier);

	if (res < 0)
		goto cleanup_netdev_register;
	
	if (basedevname) {
		dev = dev_get_by_name(basedevname);
		if (!dev) {
			DEBUG("device %s not found\n", basedevname);
			res = -1;
			goto cleanup_notifier;
		} 
		
		if (dev == dsr_dev) {
			DEBUG("invalid base device %s\n", basedevname);
			res = -1;
			goto cleanup_dev;
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
			basedevname = dev->name;
			DEBUG("wireless interface is %s\n", dev->name);
			/* netbox_addL2if(dev->ifindex, mysapfhandle); */
		}
		read_unlock(&dev_base_lock);
	}
	
	if (dev)
		dev_put(dev);
		
	return res;
	
 cleanup_dev:
	if (dev)
		dev_put(dev);
 cleanup_notifier:
	unregister_netdevice_notifier(&dsr_dev_notifier);
 cleanup_netdev_register:
	unregister_netdev(dsr_dev);
 cleanup_netdev:
	free_netdev(dsr_dev);
	dsr_dev = NULL;
	
	return res;
} 

void __exit dsr_dev_cleanup(void)
{
        unregister_netdevice_notifier(&dsr_dev_notifier);
	unregister_netdev(dsr_dev);
	free_netdev(dsr_dev);
}
