#ifndef _DEBUG_H
#define _DEBUG_H

#include <stdarg.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/ctype.h>
#include <linux/kernel.h>
#include <linux/if_ether.h>

#ifdef DEBUG
#undef DEBUG
#define DEBUG_PROC
#define DEBUG(f, args...) dsr_printk(__FUNCTION__, f, ## args) 
#else 
#define DEBUG(f, args...)
#endif

#define DEBUG_BUFLEN 256

static inline char *print_ip(__u32 addr)
{
	static char buf[16 * 4];
	static int index = 0;
	char *str;
	
	sprintf(&buf[index], "%d.%d.%d.%d",
		0x0ff & addr,
		0x0ff & (addr >> 8),
		0x0ff & (addr >> 16),
		0x0ff & (addr >> 24));
	
	str = &buf[index];
	index += 16;
	index %= 64;
	return str;
}

static inline char *print_eth(char *addr)
{
	static char buf[30];

	sprintf(buf, "%02x:%02x:%02x:%02x:%02x:%02x",
		 (unsigned char) addr[0], (unsigned char) addr[1],
		 (unsigned char) addr[2], (unsigned char) addr[3],
		 (unsigned char) addr[4], (unsigned char) addr[5]);

	return buf;
}
int dsr_printk(const char *fmt, ...);

/* static inline void dsr_print(const char *func, char *fmt, ...) */
/* { */
/*     char buf[DEBUG_BUFLEN]; */
/*     va_list args; */
    
/*     va_start(args, fmt); */

/*     vsnprintf(buf, DEBUG_BUFLEN, fmt, args); */

/*     va_end(args); */

/*     printk(KERN_DEBUG "DSR: %s: %s", func, buf); */
/* } */
void __init dbg_init(void);
void __exit dbg_cleanup(void);

#endif /* _DEBUG_H */
