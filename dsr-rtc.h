#ifndef _DSR_RTC_H
#define _DSR_RTC_H

#include <linux/list.h>
#include <linux/in.h>
#include <linux/types.h>

#include "dsr-srt.h"

#define DSR_RTC_PROC_NAME "dsr_rtc"

/* DSR route cache API */

int dsr_rtc_get(__u32 daddr);
int dsr_rtc_add(dsr_srt_t *srt, unsigned long time, unsigned short flags);
void dsr_rtc_update(dsr_srt_t *srt, unsigned long time, 
		    unsigned short flags);

int dsr_rtc_del(__u32 daddr);
void dsr_rtc_fini(void);

#endif /* _DSR_RTC_H */
