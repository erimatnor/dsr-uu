#include <iostream>
#include <trace.h>
#include <tools/random.h>
#include "ns-agent.h"

int hdr_dsr::offset_;

static class DSRUUHeaderClass:public PacketHeaderClass {

  public:
    DSRUUHeaderClass():PacketHeaderClass("PacketHeader/DSRUU",
					 sizeof(hdr_dsr)) {
	    bind_offset(&hdr_dsr::offset_);
}} class_rtProtoDSRUU_hdr;

/* Link to OTcl name space */
static class DSRUUClass : public TclClass {
public:
	DSRUUClass() : TclClass("Agent/DSRUU") {}
	TclObject* create(int, const char*const*) {
		return (new DSRUU);
	}
} class_DSRUU;


int DSRUU::confvals[CONFVAL_MAX];

DSRUU::DSRUU() : Agent(PT_DSR), 
		 ack_timer(this, "ACKTimer"), 
		 grat_rrep_tbl_timer(this, "GratRREPTimer"), 
		 send_buf_timer(this, "SendBufTimer"), 
		 neigh_tbl_timer(this, "NeighTblTimer"), 
		 lc_timer(this, "LinkCacheTimer")
{
	int i;
	
	DEBUG("Initializing DSR Agent\n");
	/* Initialize Link Cache */
	
	trace_ = NULL; 
	mac_ = NULL;
	ll_ = NULL;
	ifq_ = NULL;
	node_ = NULL;

	for (i = 0; i < CONFVAL_MAX; i++)
		confvals[i] = confvals_def[i].val;

	/* Initilize tables */
	lc_init();
	neigh_tbl_init();
	rreq_tbl_init();
	grat_rrep_tbl_init();
	maint_buf_init();
	send_buf_init();
	
	myaddr.s_addr = 0;

	return;
}

DSRUU::~DSRUU()
{
	lc_cleanup();
	neigh_tbl_cleanup();
	rreq_tbl_cleanup();
	grat_rrep_tbl_cleanup();
	send_buf_cleanup();
 	maint_buf_cleanup();

	fprintf(stderr,"DFU: Don't do this! I haven't figured out ~DSRAgent\n");
	exit(-1);
}



int
DSRUU::trace(const char *func, const char *fmt, ...)
{
	va_list args;
	unsigned long flags;
	int len;
#define BUF_LEN 1024
	static char buf[BUF_LEN];

	if (!trace_)
		return 0;
	
	va_start(args, fmt);
	
	len = sprintf(buf, "# %.5f:_%d_:%s: ", Scheduler::instance().clock(), 
		      myaddr.s_addr, func);
	
	/* Emit the output into the temporary buffer */
	len += vsnprintf(buf+len, BUF_LEN - len, fmt, args);
	
	va_end(args);
	
	buf[len-1] = '\0';

#define DBG_TO_STDOUT

#ifdef DBG_TO_STDOUT
	printf("%s\n", buf);
#else	
 	sprintf(trace_->pt_->buffer(), "%s", buf);

	trace_->pt_->dump();
#endif
	return len;
}

int 
DSRUU::arpset(struct in_addr addr, unsigned int mac_addr)
{
	ARPTable *arpt = ll_->arp_table();
	
	// if (arpt->arplookup((nsaddr_t)mac_addr))
// 		return 0;
	//create a new packet
	Packet *p = Packet::alloc();

	//access all the headers needed for ARP
	hdr_cmn *ch = HDR_CMN(p);
	char	*mh = (char*)HDR_MAC(p);
	hdr_ll  *lh = HDR_LL(p);
	hdr_arp *ah = HDR_ARP(p);
	
	ch->size() = ARP_HDR_LEN;
	ch->error() = 0;
	ch->direction() = hdr_cmn::UP; // send this pkt UP to LL
	ch->ptype() = PT_ARP;

	mac_->hdr_dst(mh, this->macaddr);
	mac_->hdr_src(mh, mac_addr);
	mac_->hdr_type(mh, ETHERTYPE_ARP);

	lh->seqno() = 0;
	lh->lltype() = LL_DATA;

	ah->arp_op  = ARPOP_REPLY;
	ah->arp_tha = this->macaddr;
	ah->arp_sha = mac_addr;
		
	ah->arp_spa = (nsaddr_t)addr.s_addr;
	ah->arp_tpa = (nsaddr_t)myaddr.s_addr;

	DEBUG("Setting ARP Table entry to %d for %s\n", 
	      mac_addr, print_ip(addr));
	// ARPTable *arpt = ll_->arp_table();
	
// 	if (arpt)
// 		arpt->arpinput(p, ll_);
// 	else
// 		DEBUG("No ARP Table\n");

	ll_->recv(p,0);

	return 1;
}

struct hdr_ip *	DSRUU::dsr_build_ip(struct dsr_pkt *dp, struct in_addr src, 
				    struct in_addr dst, int ip_len, 
				    int tot_len, int protocol, int ttl)
{
	hdr_cmn *cmh;
	
	dp->nh.iph = &dp->ip_data;
	
	// Set IP header fields
	dp->nh.iph->saddr() = (nsaddr_t)dp->src.s_addr;
	dp->nh.iph->daddr() = (nsaddr_t)dp->dst.s_addr;
	dp->nh.iph->ttl() = ttl;
	
	// Note: Port number for routing agents, not AODV port number!
// 	dp->nh.iph->sport() = RT_PORT;
// 	dp->nh.iph->dport() = RT_PORT;
	
	if (dp->p) {
		cmh = HDR_CMN(dp->p);
		cmh->ptype() = (packet_t)protocol;
	}
	return dp->nh.iph;
}

Packet *
DSRUU::ns_packet_create(struct dsr_pkt *dp)
{
	hdr_mac *mh;
	hdr_cmn *cmh;
	hdr_ip *iph;	
	dsr_opt_hdr *dh;
	int tot_len;
	int dsr_opts_len = dsr_pkt_opts_len(dp); // - sizeof(struct dsr_opt_hdr) + DSR_FIXED_HDR_LEN;

	if (!dp)
		return NULL;

	if (!dp->p)
		dp->p = allocpkt();

	tot_len = IP_HDR_LEN + dsr_opts_len + dp->payload_len;
	
	mh = HDR_MAC(dp->p);
	cmh = HDR_CMN(dp->p);
	iph = HDR_IP(dp->p);
	dh = HDR_DSRUU(dp->p);
	
	if (dp->dst.s_addr == DSR_BROADCAST) {
		cmh->addr_type() = NS_AF_NONE;
	} else {
		struct sockaddr hw_addr;
		int mac_dst;
		
		if (!neigh_tbl_get_hwaddr(dp->nxt_hop, &hw_addr)) {
			DEBUG("No next hop MAC address in neigh_tbl\n");
			return NULL;
		}

		ethtoint((char *)&hw_addr, &mac_dst);

// 		DEBUG("Mac dst=%d\n", mac_dst);

		/* Broadcast packet */
		mac_->hdr_dst((char*) HDR_MAC(dp->p), mac_dst);
		cmh->addr_type() = NS_AF_INET;

		// Generate fake ARP 
		arpset(dp->nxt_hop, mac_dst);
	}
	// Clear DSR part of packet
	memset(dh, 0, dh->size());
	
	DEBUG("Building packet dsr_opts_len=%d\n", dsr_opts_len);
	// Copy message contents into packet
	if (dsr_opts_len)
		memcpy(dh, dp->dh.raw, dsr_opts_len);
	
	/* Add payload */
// 	if (dp->payload_len && dp->payload)
// 		memcpy(dp->p->userdata(), dp->payload, dp->payload_len);
	
	// Set common header fields
	cmh->ptype() = PT_DSR;
	cmh->direction() = hdr_cmn::DOWN;
	cmh->size() = tot_len;
	cmh->iface() = -2;
	cmh->error() = 0;
	cmh->prev_hop_ = (nsaddr_t)dp->src.s_addr;
	cmh->next_hop_ = (nsaddr_t)dp->nxt_hop.s_addr;
	
	memcpy(iph, dp->nh.iph, sizeof(hdr_ip));
	
	return dp->p;
}

void 
DSRUU::xmit(struct dsr_pkt *dp)
{
	Packet *p;
	
	struct hdr_cmn *cmh;
	struct hdr_ip *iph; 
	double jitter = 0.0;
	
	if ((DATA_PACKET(dp->dh.opth->nh) || dp->dh.opth->nh == PT_PING) && 
	    CONFVAL(UseNetworkLayerAck))
		maint_buf_add(dp);
	
	p = ns_packet_create(dp);

	if (!p) {
		DEBUG("Could not create packet\n");
		if (dp->p) 
			drop(dp->p, DROP_RTR_TTL); /* Should change reason */	
		goto out;
	}
	

	iph = HDR_IP(p);
	cmh = HDR_CMN(p);
    
	/* If TTL = 0, drop packet */
	if (iph->ttl() == 0) {
		DEBUG("Dropping packet with TTL = 0.");
		drop(p, DROP_RTR_TTL);
		goto out;
	}

	DEBUG("xmitting pkt src=%d\n", iph->saddr());
    
	/* Set packet fields depending on packet type */
	if (iph->daddr() == DSR_BROADCAST) {
		/* Broadcast packet */
		jitter = (CONFVAL(BroadCastJitter) / 1000) * Random::uniform();
	}
		
	Scheduler::instance().schedule(ll_, p, jitter);

 out:
	dsr_pkt_free(dp);
}

void 
DSRUU::deliver(struct dsr_pkt *dp)
{
	int len, dsr_len = 0;
	
	struct hdr_cmn *cmh= hdr_cmn::access(dp->p);
	
	if (dp->dh.raw)
		len = dsr_opts_remove(dp);
	
	if (len) {
		dsr_len = len; //- sizeof(struct dsr_opt_hdr) + DSR_FIXED_HDR_LEN;
		DEBUG("Removed %d (%d real) bytes DSR options\n", dsr_len, len);
		
	}

	DEBUG("Packet type %s\n", packet_info.name(cmh->ptype()));
				
	cmh->size() -= IP_HDR_LEN - dsr_len;

	target_->recv(dp->p, (Handler*)0);
	
	dp->p = 0;
	dsr_pkt_free(dp);
}


void 
DSRUU::recv(Packet* p, Handler*)
{
	struct dsr_pkt *dp;
	int action, res = -1;
	struct hdr_cmn *cmh= hdr_cmn::access(p);

	DEBUG("##########\n");

	dp = dsr_pkt_alloc(p);
	
	switch(cmh->ptype()) {
	case PT_DSR:
		if (dp->src.s_addr != myaddr.s_addr) {
			DEBUG("DSR packet from %s\n", print_ip(dp->src));
			dsr_recv(dp);
		} else {
			
			DEBUG("Locally gernerated DSR packet\n");
		}
		break;
	default:
		if (dp->src.s_addr == myaddr.s_addr) {
			DEBUG("Locally generated non DSR packet\n");
			dsr_start_xmit(dp);
		} else {
			// This shouldn't really happen ?
			DEBUG("Data packet from %s without DSR header!n", 
			      print_ip(dp->src));
		}
	}
	return;
}

enum {
	SET_ADDR,
	SET_MAC_ADDR,
	SET_NODE,
	SET_LL,
	SET_TAP,
	SET_DMUX,
	SET_TRACE_TARGET,
	START_DSR,
	MAX_CMD
};

char *cmd[MAX_CMD] = {
	"addr",
	"mac-addr",
	"node",
	"add-ll",
	"install-tap",
	"port-dmux",
	"tracetarget",
	"startdsr"
};

static int name2cmd(const char *name)
{
	int i;

	for (i = 0; i < MAX_CMD; i++) {
		if (strcasecmp(cmd[i], name) == 0) 
			return i;
	}
	return -1;
}

int 
DSRUU::command(int argc, const char* const* argv)
{	

	//cerr << "cmd=" << argv[1] << endl;

	switch (name2cmd(argv[1])) {
	case SET_ADDR:
		myaddr.s_addr = Address::instance().str2addr(argv[2]);
		break;
	case SET_MAC_ADDR:
		macaddr = Address::instance().str2addr(argv[2]);
		break;
	case SET_NODE:
		node_ = (MobileNode *)TclObject::lookup(argv[2]);
		break;
	case SET_LL:
		ll_ = (LL *)TclObject::lookup(argv[2]);
		ifq_ = (CMUPriQueue *)TclObject::lookup(argv[3]);
		break;
	case SET_DMUX:
		break;
	case SET_TAP:
		mac_ = (Mac *)TclObject::lookup(argv[2]);
		macaddr = mac_->addr();
		break;
	case SET_TRACE_TARGET:
		trace_ = (Trace *)TclObject::lookup(argv[2]);
		break;
	case START_DSR:
		break;
	default:
		//cerr << "Unknown command " << argv[1] << endl;
		return Agent::command(argc, argv);
	}
	return TCL_OK;
}

void
DSRUUTimer::expire (Event *e)
{
	a_->trace(__FUNCTION__, "%s Interrupt\n", name_);
	(a_->*function)(data);
}
