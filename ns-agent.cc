#include <trace.h>

#include "ns-agent.h"


/* Link to OTcl name space */
static class DSRUUClass : public TclClass {
public:
	DSRUUClass() : TclClass("Agent/DSRUU") {}
	TclObject* create(int, const char*const*) {
		return (new DSRUU);
	}
} class_DSRUU;


int DSRUU::params[PARAMS_MAX];

DSRUU::DSRUU() : Agent(PT_DSR), ack_timer(this), grat_rrep_tbl_timer(this), 
		 send_buf_timer(this), neigh_tbl_timer(this), lc_timer(this)
{
	int i;
	
	DEBUG("Initializing DSR Agent\n");
	/* Initialize Link Cache */
	
	trace_ = NULL; 
	mac_ = NULL;
	ll_ = NULL;
	ifq_ = NULL;
	node_ = NULL;

	for (i = 0; i < PARAMS_MAX; i++)
		params[i] = params_def[i].val;

	/* Initilize tables */
	lc_init();
	neigh_tbl_init();
	rreq_tbl_init();
	grat_rrep_tbl_init();
	maint_buf_init();
	send_buf_init();
	
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
	
	len = sprintf(buf, "# %.5f:%d:%s: ", Scheduler::instance().clock(), 
			     ip_addr.s_addr, func);
	
	/* Emit the output into the temporary buffer */
	len += vsnprintf(buf+len, BUF_LEN-len, fmt, args);
	
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

Packet *
DSRUU::ns_packet_create(struct dsr_pkt *dp)
{
	Packet *p;
	struct hdr_cmn *ch;
	struct hdr_ip *iph;
	
	p = allocpkt();

	ch = HDR_CMN(p);
	iph = HDR_IP(p);

	

	return p;
}


void 
DSRUU::recv(Packet* p, Handler*)
{
	struct dsr_pkt *dp;
	int action, res = -1;

	dp = dsr_pkt_alloc(p);

	if (dp->src.s_addr == ip_addr.s_addr) {
		
		dsr_start_xmit(dp);
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
	MAX_CMD
};

char *cmd[MAX_CMD] = {
	"addr",
	"mac-addr",
	"node",
	"add-ll",
	"install-tap",
	"port-dmux",
	"tracetarget"
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
	switch (name2cmd(argv[1])) {
	case SET_ADDR:
		ip_addr.s_addr = Address::instance().str2addr(argv[2]);
		break;
	case SET_MAC_ADDR:
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
		break;
	case SET_TRACE_TARGET:
		trace_ = (Trace *)TclObject::lookup(argv[2]);
		break;
	default:
		return TCL_OK;
	}
	return TCL_OK;
}

void
DSRUUTimer::expire (Event *e)
{
	(a_->*function)(data);
	return;
}
