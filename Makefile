
SRC=dsr-module.c dsr-pkt.c dsr-dev.c dsr-opt.c dsr-rreq.c dsr-rrep.c dsr-srt.c send-buf.c debug.c neigh.c maint-buf.c dsr-ack.c

DEFS=-DDEBUG 

MODNAME=kdsr
RTC_TRG=linkcache
RTC_SRC=link-cache.c

ifneq (,$(findstring 2.6,$(KERNELRELEASE)))

EXTRA_CFLAGS += -DKERNEL26 $(DEFS)

obj-m += $(MODNAME).o 
$(MODNAME)-objs := $(SRC:%.c=%.o)

obj-m += $(RTC_TRG).o
$(RTC_TRG)-objs := $(RTC_SRC:%.c=%.o)

else

KOBJS := $(SRC:%.c=%.o)

KERNEL=$(shell uname -r)
KERNEL_DIR=/lib/modules/$(KERNEL)/build
KERNEL_INC=$(KERNEL_DIR)/include

CC=gcc
MIPS_CC=mipsel-linux-gcc
MIPS_LD=mipsel-linux-ld

#######
VERSION=$(shell if [ ! -d $(KERNEL_DIR) ]; then echo "No linux source found!!! Check your setup..."; exit; fi; grep ^VERSION $(KERNEL_DIR)/Makefile | cut -d' ' -f 3)
PATCHLEVEL=$(shell grep ^PATCHLEVEL $(KERNEL_DIR)/Makefile | cut -d' ' -f 3)
SUBLEVEL=$(shell grep ^SUBLEVEL $(KERNEL_DIR)/Makefile | cut -d' ' -f 3)
#######

KDEFS=-D__KERNEL__ -DMODULE -DEXPORT_SYMTAB $(DEFS) #-DCONFIG_MODVERSIONS -DMODVERSIONS -include $(KERNEL_INC)/linux/modversions.h 

KINC=-nostdinc $(shell $(CC) -print-search-dirs | sed -ne 's/install: \(.*\)/-I \1include/gp') -I$(KERNEL_INC)
KCFLAGS=-Wall -fno-strict-aliasing -O2 $(KDEFS) $(KINC)

.PHONY: mips default depend

# Check for kernel version
ifeq ($(PATCHLEVEL),6)
default: $(MODNAME).ko $(RTC_TRG).ko TODO
else 
# Assume kernel 2.4
default: $(MODNAME).o $(RTC_TRG).o TODO
endif

mips: 
	$(MAKE) default CC=$(MIPS_CC) LD=$(MIPS_LD)

$(MODNAME).ko: $(SRC) Makefile
	$(MAKE) -C $(KERNEL_DIR) SUBDIRS=$(PWD) MODVERDIR=$(PWD) modules

$(RTC_TRG).ko: $(RTC_SRC) Makefile
	$(MAKE) -C $(KERNEL_DIR) SUBDIRS=$(PWD) MODVERDIR=$(PWD) modules

$(KOBJS): %.o: %.c Makefile
	$(CC) $(KCFLAGS) -c -o $@ $<

$(MODNAME).o: $(KOBJS)
	$(LD) -r $^ -o $@

$(RTC_TRG).o: $(RTC_SRC) Makefile
	$(CC) $(KCFLAGS) -c -o $@ $<

depend:
	@echo "Updating Makefile dependencies..."
	@makedepend -Y./ -- $(DEFS) -- *.c *.h &>/dev/null

TODO:
	grep -n "TODO:" *.c *.h > TODO
	cat TODO

TAGS: *.c *.h
	etags.emacs *.c *.h

clean:
	rm -rf .*ko* $(RTC_TRG).*mod* $(MODNAME).*mod* .*cmd .tmp_versions *~ *.ko *.o *.ver Makefile.bak .*o.d TAGS TODO
endif
# DO NOT DELETE

dsr-ack.o: tbl.h dsr.h debug.h dsr-opt.h dsr-ack.h dsr-dev.h dsr-rtc.h
dsr-ack.o: dsr-srt.h
dsr-dev.o: debug.h dsr.h kdsr.h dsr-opt.h dsr-rreq.h dsr-rtc.h dsr-srt.h
dsr-dev.o: send-buf.h
dsr-module.o: dsr.h dsr-opt.h dsr-dev.h dsr-rreq.h dsr-rrep.h dsr-srt.h
dsr-module.o: send-buf.h debug.h dsr-rtc.h dsr-ack.h
dsr-opt.o: debug.h dsr.h dsr-opt.h dsr-rreq.h dsr-rrep.h dsr-srt.h dsr-ack.h
dsr-opt.o: kdsr.h
dsr-rrep.o: dsr.h debug.h tbl.h dsr-rrep.h dsr-srt.h dsr-opt.h dsr-rtc.h
dsr-rrep.o: dsr-dev.h send-buf.h kdsr.h
dsr-rreq.o: debug.h dsr.h tbl.h kdsr.h dsr-rrep.h dsr-srt.h dsr-rreq.h
dsr-rreq.o: dsr-opt.h dsr-rtc.h dsr-dev.h send-buf.h
dsr-rtc-simple.o: tbl.h dsr-rtc.h dsr-srt.h dsr.h debug.h
dsr-srt.o: dsr.h dsr-srt.h dsr-opt.h dsr-ack.h debug.h
link-cache.o: dsr-rtc.h dsr-srt.h dsr.h tbl.h debug.h
send-buf.o: send-buf.h dsr.h debug.h dsr-rtc.h dsr-srt.h kdsr.h
dsr-rrep.o: dsr.h dsr-srt.h
dsr-rreq.o: dsr.h
dsr-rtc.o: dsr-srt.h dsr.h
dsr-srt.o: dsr.h
send-buf.o: dsr.h
