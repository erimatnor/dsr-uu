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

static int dsr_dev_srt_add(struct sk_buff *skb, dsr_srt_t *srt)
{
	struct iphdr *iph;
	struct sk_buff *nskb;
	char *ndx;
	int dsr_len;
	
	if (!skb || !srt)
		return -1;
	
	/* Calculate extra space needed */
	dsr_len = DSR_OPT_HDR_LEN + DSR_SRT_OPT_LEN(srt);
	
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

int dsr_dev_queue_xmit(struct sk_buff *skb)
{
	struct net_device_stats *stats = skb->dev->priv;
	struct ethhdr *ethh;
	struct iphdr *iph;
	dsr_srt_t *srt;
	
	ethh = (struct ethhdr *)skb->data;
	
	switch (ntohs(ethh->h_proto)) {
	case ETH_P_IP:
		iph = skb->nh.iph;
		
		srt = dsr_rtc_find(iph->daddr);
		
		if (srt) {
			struct sockaddr hw_addr;
			struct in_addr dst;
		       
			dst.s_addr = iph->daddr;
			
			/* Add source route */
			if (dsr_dev_srt_add(skb, srt) < 0)
				break;
			
			if (kdsr_get_hwaddr(dst, &hw_addr, skb->dev) < 0)
				break;
			
			/* Build hw header */
			if (dsr_dev_build_hw_hdr(skb, &hw_addr) < 0)
				break;
			
			/* Send packet */
			dev_queue_xmit(skb);
			
			stats->tx_packets++;
			stats->tx_bytes+=skb->len;

			/* We must free the source route */
			kfree(srt);
		}
		return -1;
	default:
		DEBUG("Unkown packet type\n");
		dev_kfree_skb(skb);
	}
	
	DEBUG("Could not send packet, freeing...\n");
	dev_kfree_skb(skb);
	return -1;
}

static int dsr_dev_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct net_device_stats *stats = dev->priv;
	struct ethhdr *ethh;
	struct iphdr *iph;
	struct net_device *slave_dev;
	dsr_srt_t *srt;
	int res = 0;

	ethh = (struct ethhdr *)skb->data;
	
	switch (ntohs(ethh->h_proto)) {
	case ETH_P_IP:
		
		slave_dev = dev_get_by_index(ldev_info.ifindex);
		skb->dev = slave_dev;
		dev_put(slave_dev);

		iph = skb->nh.iph;
		
		srt = dsr_rtc_find(iph->daddr);
		
		if (srt) {
			struct sockaddr hw_addr;
			struct in_addr dst;
		       
			dst.s_addr = iph->daddr;
			
			/* Add source route */
			if (dsr_dev_srt_add(skb, srt) < 0)
				break;
			
			if (kdsr_get_hwaddr(dst, &hw_addr, skb->dev) < 0)
				break;
			
			/* Build hw header */
			if (dsr_dev_build_hw_hdr(skb, &hw_addr) < 0)
				break;
			
			/* Send packet */
			dev_queue_xmit(skb);
		
			stats->tx_packets++;
			stats->tx_bytes+=skb->len;
			
			/* We must free the source route */
			kfree(srt);
		} else {
			/* Ok-function (okfn) calls this function again, hopefully we
			 * have a route at that point */
			p_queue_enqueue_packet(skb, dsr_dev_queue_xmit);
			
			res = dsr_rreq_send(iph->daddr);
			
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
