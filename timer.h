#ifndef _TIMER_H
#define _TIMER_H

#ifdef NS2
#include <stdarg.h>

#include <object.h>
#include <agent.h>
#include <trace.h>
#include <scheduler.h>

class DSRUU;

typedef double Time;

typedef void (DSRUU::*fct_t)(unsigned long data);

class DSRUUTimer : public TimerHandler {
 public:
	DSRUUTimer(DSRUU *a) : TimerHandler() { a_=a; }
	Time expires;
	fct_t function;
	unsigned long data;
	void init(double expires_,  fct_t fct_, unsigned long data_ ) 
		{expires = expires_; data = data_; function = fct_;}
 protected:
	virtual void expire (Event *e);
	DSRUU *a_;
};

#define SECONDS(secs) (secs)

#else

#include <linux/timer.h>

typedef unsigned long Time;
typedef struct timer_list DSRUUTimer;

#define SECONDS(secs) (secs*HZ)

#endif /* NS2 */

#endif /* _TIMER_H */
