#!/bin/bash
# PROTOCOL=DSR-UU
command=$1
IFNAME=eth0

killproc() {
    pidlist=$(/sbin/pidof $1)
    for pid in $pidlist; do
	kill $pid &>/dev/null
    done
    return 0
}


if [ "$command" = "start" ]; then 
   
    # Start LUNAR
    IP=`/sbin/ifconfig $IFNAME | grep inet`
    IP=${IP%%" Bcast:"*}
    IP=${IP##*"inet addr:"}
    echo $IP > .dsr.ip
    host_nr=`echo $IP | awk 'BEGIN{FS="."} { print $4 }'`

    if [ -f linkcache.o ] && [ -f dsr.o ]; then
	# Reconfigure the default interface
	insmod linkcache.o
	insmod dsr.o ifname=$IFNAME
	/sbin/ifconfig $IFNAME 192.168.1.$host_nr up
	/sbin/ifconfig dsr0 $IP up
	echo "DSR-UU started with virtual host IP $IP"
	# Enable IP-forwarding...
	echo 1 > /proc/sys/net/ipv4/ip_forward
	echo 0 > /proc/sys/net/ipv4/conf/$IFNAME/rp_filter
    else
	echo "DSR-UU not installed"
	exit
    fi
elif [ "$command" = "stop" ]; then 
    IP=`cat .dsr.ip`
    /sbin/ifconfig dsr0 down
    rmmod dsr linkcache
    /sbin/ifconfig $IFNAME $IP up
    rm -f .dsr.ip
fi
