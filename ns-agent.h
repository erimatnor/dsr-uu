#ifndef _DSR_NS_AGENT
#define _DSR_NS_AGENT

class DSRAgent;

#include <stdarg.h>

#include <object.h>
#include <agent.h>
#include <trace.h>
#include <scheduler.h>

#include "tbl.h"
#include "endian.h"

#define NO_DECLS
#include "dsr-rreq.h"
#include "dsr-rrep.h"
#include "dsr-rerr.h"
#include "dsr-ack.h"
#include "dsr-srt.h"
#include "send-buf.h"
#include "neigh.h"
#include "maint-buf.h"
#undef NO_DECLS

#define NSCLASS DSRAgent::
#define jiffies Scheduler::instance().clock()

#define timer_pending(timer) 1 /* What here ??? */
#define del_timer_sync(timer) del_timer(timer)
#define SECONDS(secs) (secs)
#define MALLOC(s, p)        malloc(s)
#define FREE(p)             free(p)
#define XMIT(pkt) /* What here ??? */

#define IPDEFTTL 64

typedef double Time;

class DSRTimer : public TimerHandler {
 public:
	DSRTimer(DSRAgent *a);//:TimerHandler(){a_=a; debuglevel=debug}
	double expires;
	int debuglevel;
 protected:
	virtual void expire (Event *e);
	DSRAgent *a_;
};

class DSRAgent : public Agent {
 public:

	virtual int command(int argc, const char*const* argv);
	virtual void recv(Packet*, Handler* callback = 0);

	void tap(const Packet *p);
	// tap out all data packets received at this host and promiscously snoop
	// them for interesting tidbits

	void add_timer (DSRTimer *);
	void del_timer (DSRTimer *);

#define NO_GLOBALS
#undef _DSR_RREQ_H
#include "dsr-rreq.h"

#undef _DSR_RREP_H
#include "dsr-rrep.h"

#undef _DSR_RERR_H
#include "dsr-rerr.h"

#undef _DSR_ACK_H
#include "dsr-ack.h"

#undef _DSR_SRT_H
#include "dsr-srt.h"

#undef _SEND_BUF_H
#include "send-buf.h"

#undef _NEIGH_H
#include "neigh.h"

#undef _MAINT_BUF_H
#include "maint-buf.h"

#undef NO_GLOBALS

	int dsr_opt_recv(struct dsr_pkt *dp);

	DSRAgent();
	~DSRAgent();

 private:
	static struct tbl rreq_tbl;
	static struct tbl grat_rrep_tbl;
	static struct tbl send_buf;
	static struct tbl neigh_tbl;
	static struct tbl maint_buf;
	
	static unsigned int rreq_seqno;
	
	static DSRTimer grat_rrep_tbl_timer;
	static DSRTimer send_buf_timer;
	static DSRTimer garbage_timer;
	static DSRTimer ack_timer;
};


#endif /* _DSR_NS_AGENT_H */
