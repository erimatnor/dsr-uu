
SRC=dsr-module.c dsr-pkt.c dsr-dev.c dsr-opt.c dsr-rreq.c dsr-rrep.c dsr-rerr.c dsr-ack.c dsr-srt.c send-buf.c debug.c neigh.c maint-buf.c

NS_SRC=dsr-pkt.c dsr-opt.c dsr-rreq.c dsr-rrep.c dsr-rerr.c dsr-ack.c dsr-srt.c send-buf.c neigh.c maint-buf.c link-cache.c

NS_SRC_CPP=ns-agent.cc

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
CPP=g++

MIPS_CC=mipsel-linux-gcc
MIPS_LD=mipsel-linux-ld

# NS2
OBJS_NS=$(NS_SRC:%.c=%-ns.o)
OBJS_NS_CPP=$(NS_SRC_CPP:%.cc=%-ns.o)

NS_DEFS= # DON'T CHANGE (overridden by NS Makefile)

# Set extra DEFINES here. Link layer feedback is now a runtime option.
EXTRA_NS_DEFS=-DNS2

# Note: OPTS is overridden by NS Makefile
NS_CFLAGS=$(OPTS) $(CPP_OPTS) $(DEBUG) $(NS_DEFS) $(EXTRA_NS_DEFS)

NS_INC= # DON'T CHANGE (overridden by NS Makefile)

NS_TARGET=libdsr-uu.a
# Archiver and options
AR=ar
AR_FLAGS=rc

#######
VERSION=$(shell if [ ! -d $(KERNEL_DIR) ]; then echo "No linux source found!!! Check your setup..."; exit; fi; grep ^VERSION $(KERNEL_DIR)/Makefile | cut -d' ' -f 3)
PATCHLEVEL=$(shell grep ^PATCHLEVEL $(KERNEL_DIR)/Makefile | cut -d' ' -f 3)
SUBLEVEL=$(shell grep ^SUBLEVEL $(KERNEL_DIR)/Makefile | cut -d' ' -f 3)
#######

KDEFS=-D__KERNEL__ -DMODULE -DEXPORT_SYMTAB $(DEFS) -DCONFIG_MODVERSIONS -DMODVERSIONS -include $(KERNEL_INC)/linux/modversions.h 

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

$(OBJS_NS_CPP): %-ns.o: %.cc Makefile
	$(CPP) $(NS_CFLAGS) $(NS_INC) -c -o $@ $<

$(OBJS_NS): %-ns.o: %.c Makefile
	$(CPP) $(NS_CFLAGS) $(NS_INC) -c -o $@ $<

$(NS_TARGET): endian.h $(OBJS_NS_CPP) $(OBJS_NS)
	$(AR) $(AR_FLAGS) $@ $(OBJS_NS_CPP) $(OBJS_NS) > /dev/null

endian.h: endian.c
	$(CC) $(CFLAGS) -o endian endian.c
	./endian > endian.h

depend:
	@echo "Updating Makefile dependencies..."
	@makedepend -Y./ -- $(DEFS) -- *.c *.h &>/dev/null

TODO:
	grep -n "TODO:" *.c *.h > TODO
	cat TODO

TAGS: *.c *.h
	etags.emacs *.c *.h

clean:
	rm -rf .*ko* $(RTC_TRG).*mod* $(MODNAME).*mod* .*cmd .tmp_versions *~ *.ko *.o *.ver Makefile.bak .*o.d TAGS TODO endian endian.h $(NS_TARGET)
endif
# DO NOT DELETE

debug.o: debug.h
dsr-ack.o: tbl.h dsr.h dsr-pkt.h debug.h dsr-opt.h dsr-ack.h dsr-dev.h
dsr-ack.o: dsr-rtc.h dsr-srt.h neigh.h maint-buf.h
dsr-dev.o: debug.h dsr.h dsr-pkt.h kdsr.h dsr-opt.h dsr-rreq.h dsr-rtc.h
dsr-dev.o: dsr-srt.h dsr-ack.h send-buf.h maint-buf.h
dsr-module.o: dsr.h dsr-pkt.h dsr-dev.h dsr-rreq.h dsr-rrep.h dsr-srt.h
dsr-module.o: debug.h dsr-ack.h send-buf.h dsr-rtc.h maint-buf.h neigh.h
dsr-module.o: dsr-opt.h
dsr-opt.o: debug.h dsr.h dsr-pkt.h dsr-opt.h dsr-rreq.h dsr-rrep.h dsr-srt.h
dsr-opt.o: dsr-rerr.h dsr-ack.h kdsr.h
dsr-pkt.o: dsr.h dsr-pkt.h dsr-opt.h
dsr-rerr.o: dsr-rerr.h dsr.h dsr-pkt.h dsr-opt.h debug.h dsr-srt.h dsr-ack.h
dsr-rerr.o: dsr-dev.h dsr-rtc.h maint-buf.h
dsr-rrep.o: dsr.h dsr-pkt.h debug.h tbl.h dsr-rrep.h dsr-srt.h dsr-rreq.h
dsr-rrep.o: dsr-opt.h dsr-rtc.h dsr-dev.h send-buf.h kdsr.h
dsr-rreq.o: debug.h dsr.h dsr-pkt.h tbl.h kdsr.h dsr-rrep.h dsr-srt.h
dsr-rreq.o: dsr-rreq.h dsr-opt.h dsr-rtc.h dsr-dev.h send-buf.h
dsr-rtc-simple.o: tbl.h dsr-rtc.h dsr-srt.h dsr.h dsr-pkt.h debug.h
dsr-srt.o: dsr.h dsr-pkt.h dsr-srt.h debug.h dsr-opt.h dsr-ack.h dsr-rtc.h
link-cache.o: dsr-rtc.h dsr-srt.h dsr.h dsr-pkt.h debug.h tbl.h
maint-buf.o: dsr.h dsr-pkt.h debug.h tbl.h neigh.h dsr-ack.h dsr-rtc.h
maint-buf.o: dsr-srt.h dsr-rerr.h
neigh.o: tbl.h neigh.h dsr.h dsr-pkt.h debug.h
send-buf.o: tbl.h send-buf.h dsr.h dsr-pkt.h debug.h dsr-rtc.h dsr-srt.h
send-buf.o: kdsr.h
dsr.o: dsr-pkt.h
dsr-rerr.o: dsr.h dsr-pkt.h
dsr-rrep.o: dsr.h dsr-pkt.h dsr-srt.h debug.h
dsr-rreq.o: dsr.h dsr-pkt.h
dsr-rtc.o: dsr-srt.h dsr.h dsr-pkt.h debug.h
dsr-srt.o: dsr.h dsr-pkt.h debug.h
neigh.o: dsr.h dsr-pkt.h
send-buf.o: dsr.h dsr-pkt.h
