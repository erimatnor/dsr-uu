#ifndef _TIMER_H
#define _TIMER_H

#include <linux/time.h>

typedef unsigned long usecs_t;

#ifdef NS2
#include <stdarg.h>

#include <object.h>
#include <agent.h>
#include <trace.h>
#include <scheduler.h>

class DSRUU;

typedef void (DSRUU::*fct_t)(unsigned long data);

class DSRUUTimer : public TimerHandler {
 public:
	DSRUUTimer(DSRUU *a) : TimerHandler() { a_ = a; name_ = "NoName";}
	DSRUUTimer(DSRUU *a, char *name) : TimerHandler() 
		{ a_ = a; name_ = name; }
	fct_t function;
	unsigned long data;
	void init(double expires_,  fct_t fct_, unsigned long data_ ) 
		{expires = expires_; data = data_; function = fct_;}
 protected:
	virtual void expire (Event *e);
	double expires;
	DSRUU *a_;
	char *name_;
};

int gettimeofday(struct timeval *tv)
{
    double current_time, tmp;

    /* Timeval is required, timezone is ignored */
    if (!tv)
	return -1;

    current_time = Scheduler::instance().clock();

    tv->tv_sec = (long)current_time; /* Remove decimal part */
    tmp = (current_time - tv->tv_sec) * 1000000;
    tv->tv_usec = (long)tmp;

    return 0;
}


#define SECONDS(secs) (secs)

#else

#include <linux/timer.h>

typedef struct timer_list DSRUUTimer;

#define SECONDS(secs) (secs*HZ)

void timer_set(DSRUUTimer *t, struct timeval *expires)
{
	unsigned long exp_jiffies;
#ifdef KERNEL26
	
	exp_jiffies = timeval_to_jiffies(expires) = jiffies + ((usecs * HZ) / 1000000l);

	if (timer_pending(t))
		mod_timer(t, expires); 
	else {
		t->expires = expires;
		add_timer(t);
	}
}

#endif /* NS2 */

static inline long timeval_diff(struct timeval *t1, struct timeval *t2)
{
    long long res;  /* We need this to avoid overflows while calculating... */

    if (!t1 || !t2)
	return -1;
    else {

	res = t1->tv_sec;
	res = ((res - t2->tv_sec) * 1000000 + t1->tv_usec - t2->tv_usec) / 1000;
	return (long) res;
    }
}

static inline int timeval_add_msec(struct timeval *t, long msec)
{
    long long add;		/* Protect against overflows */

    if (!t)
	return -1;

    add = t->tv_usec + (msec * 1000);
    t->tv_sec += add / 1000000;
    t->tv_usec = add % 1000000;

    return 0;
}
#endif /* _TIMER_H */
