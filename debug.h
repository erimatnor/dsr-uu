#ifndef _DEBUG_H
#define _DEBUG_H

#include <stdarg.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/kernel.h>

#ifdef DEBUG
#undef DEBUG
#define DEBUG(f, args...) dsr_print(f, ## args) 
#else 
#define DEBUG(f, args...)
#endif

#define DEBUG_BUFLEN 256

static inline char *print_ip(__u32 addr)
{
   static char buf[16];
   
   sprintf(buf, "%d.%d.%d.%d",
	   0x0ff & addr,
	   0x0ff & (addr >> 8),
	   0x0ff & (addr >> 16),
	   0x0ff & (addr >> 24));

   return buf;
}

static inline void dsr_print(char *fmt, ...)
{
    char buf[DEBUG_BUFLEN];
    
    va_list ap;
    
    va_start(ap, fmt);

    vsnprintf(buf, DEBUG_BUFLEN, fmt, ap);

    va_end(ap);

    printk("kdsr::%s: %s", __FUNCTION__, buf);
}
#endif
