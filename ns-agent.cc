#include <object.h>
#include <agent.h>
#include <trace.h>
#include <packet.h>
#include <scheduler.h>
// #include <random.h>

// #include <mac.h>
// #include <ll.h>
#include <cmu-trace.h>

#include "ns-agent.h"


static class DSRAgentClass : public TclClass {
public:
	DSRAgentClass() : TclClass("Agent/DSRAgent") {}
	TclObject* create(int, const char*const*) {
		return (new DSRAgent);
	}
} class_DSRAgent;

/*===========================================================================
  DSRAgent methods
---------------------------------------------------------------------------*/
DSRAgent::DSRAgent(): Agent(PT_DSR)
{

	return;
}

DSRAgent::~DSRAgent()
{
  fprintf(stderr,"DFU: Don't do this! I haven't figured out ~DSRAgent\n");
  exit(-1);
}


void DSRAgent::recv(Packet* packet, Handler*)
{

	return;
}

void DSRAgent::add_timer(DSRTimer *a)
{
 //  debuglevel=selnet_->debuglevel;

  double timeout = a->expires;
  double now=Scheduler::instance().clock();
  a->resched(timeout); 
  a->expires=0.0;
}


void DSRAgent::del_timer(DSRTimer *a)
{
  // debuglevel=selnet_->debuglevel;
  
  a->cancel();
 
}
