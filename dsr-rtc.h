#ifndef _DSR_RTC_H
#define _DSR_RTC_H

#include <linux/list.h>
#include <linux/in.h>
#include <linux/types.h>

#include "dsr-srt.h"

#define DSR_RTC_PROC_NAME "dsr_rtc"

/* DSR route cache API */

struct dsr_srt *dsr_rtc_find(struct in_addr src, struct in_addr dst);
int dsr_rtc_add(struct dsr_srt *srt, unsigned long time, unsigned short flags);
/* int dsr_rtc_del(struct in_addr addr); */
void dsr_rtc_flush(void);
int lc_link_del(struct in_addr src, struct in_addr dst);

#endif /* _DSR_RTC_H */
