
SRC=kdsr.c dsr-dev.c dsr-opt.c dsr-rreq.c dsr-rrep.c dsr-srt.c p-queue.c

DEFS=-DDEBUG 

MODNAME=kdsr-m
RTCNAME=kdsr-rtc-simple

ifneq (,$(findstring 2.6,$(KERNELRELEASE)))

EXTRA_CFLAGS += -DKERNEL26 $(DEFS)

obj-m += $(MODNAME).o 
$(MODNAME)-objs := $(SRC:%.c=%.o)

obj-m += $(RTCNAME).o
$(RTCNAME)-objs := dsr-rtc-simple.o

else

KOBJS := $(SRC:%.c=%.o)

KERNEL=$(shell uname -r)
KERNEL_DIR=/lib/modules/$(KERNEL)/build
KERNEL_INC=$(KERNEL_DIR)/include

CC=gcc
KCC=gcc

#######
VERSION=$(shell if [ ! -d $(KERNEL_DIR) ]; then echo "No linux source found!!! Check your setup..."; exit; fi; grep ^VERSION $(KERNEL_DIR)/Makefile | cut -d' ' -f 3)
PATCHLEVEL=$(shell grep ^PATCHLEVEL $(KERNEL_DIR)/Makefile | cut -d' ' -f 3)
SUBLEVEL=$(shell grep ^SUBLEVEL $(KERNEL_DIR)/Makefile | cut -d' ' -f 3)
#######

KDEFS=-D__KERNEL__ -DMODULE -DEXPORT_SYMTAB -DCONFIG_MODVERSIONS $(DEFS)

KINC=-nostdinc -DMODVERSIONS -include $(KERNEL_INC)/linux/modversions.h $(shell $(CC) -print-search-dirs | sed -ne 's/install: \(.*\)/-I \1include/gp') -I$(KERNEL_INC)
KCFLAGS=-Wall -fno-strict-aliasing -O2 $(KDEFS) $(KINC)

# Check for kernel version
ifeq ($(PATCHLEVEL),6)
default: $(MODNAME).ko $(RTCNAME).ko
else 
# Assume kernel 2.4
default: $(MODNAME).o $(RTCNAME).o
endif

$(MODNAME).ko: $(SRC) Makefile
	$(MAKE) -C $(KERNEL_DIR) SUBDIRS=$(PWD) MODVERDIR=$(PWD) modules

$(RTCNAME).ko: dsr-rtc-simple.c
	$(MAKE) -C $(KERNEL_DIR) SUBDIRS=$(PWD) MODVERDIR=$(PWD) modules

$(KOBJS): %.o: %.c Makefile
	$(KCC) $(KCFLAGS) -c -o $@ $<

$(MODNAME).o: $(KOBJS)
	$(LD) -r $^ -o $@

$(RTCNAME).o: dsr-rtc-simple.c
	$(KCC) $(KCFLAGS) -c -o $@ $<

depend:
	@echo "Updating Makefile dependencies..."
	@makedepend -Y./ -- $(DEFS) -- $(SRC) &>/dev/null

clean:
	rm -rf .*ko* .*mod* .*cmd *mod* .tmp_versions *~ *.ko *.o *.ver Makefile.bak
endif
# DO NOT DELETE

kdsr.o: dsr.h dsr-srt.h dsr-dev.h dsr-rreq.h dsr-rrep.h p-queue.h debug.h
dsr-dev.o: debug.h dsr.h dsr-srt.h kdsr.h dsr-rreq.h p-queue.h
dsr-opt.o: debug.h dsr.h dsr-srt.h dsr-rreq.h kdsr.h
dsr-rreq.o: debug.h dsr.h dsr-srt.h kdsr.h dsr-rrep.h dsr-rreq.h dsr-rtc.h
dsr-rrep.o: dsr.h dsr-srt.h debug.h dsr-rrep.h dsr-rtc.h
dsr-srt.o: dsr.h dsr-srt.h debug.h
p-queue.o: p-queue.h debug.h
