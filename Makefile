
SRC=kdsr.c dsr-dev.c dsr-opt.c dsr-rreq.c dsr-rrep.c p-queue.c

DEFS=-DDEBUG

ifneq (,$(findstring 2.6,$(KERNELRELEASE)))

EXTRA_CFLAGS += -DKERNEL26 $(DEFS)
obj-m += k-dsr.o
k-dsr-objs := $(SRC:%.c=%.o)

else

KOBJS := $(SRC:%.c=%.o)

CC=gcc
KCC=gcc
KERNEL=$(shell uname -r)
KERNEL_DIR=/lib/modules/$(KERNEL)/build
KERNEL_INC=$(KERNEL_DIR)/include

#######
VERSION=$(shell if [ ! -d $(KERNEL_DIR) ]; then echo "No linux source found!!! Check your setup..."; exit; fi; grep ^VERSION $(KERNEL_DIR)/Makefile | cut -d' ' -f 3)
PATCHLEVEL=$(shell grep ^PATCHLEVEL $(KERNEL_DIR)/Makefile | cut -d' ' -f 3)
SUBLEVEL=$(shell grep ^SUBLEVEL $(KERNEL_DIR)/Makefile | cut -d' ' -f 3)
#######

KDEFS=-D__KERNEL__ -DMODULE $(DEFS)

KINC=-nostdinc -DMODVERSIONS -include $(KERNEL_INC)/linux/modversions.h $(shell $(CC) -print-search-dirs | sed -ne 's/install: \(.*\)/-I \1include/gp') -I$(KERNEL_INC)
KCFLAGS=-Wall -Wno-strict-aliasing -O2 $(KDEFS) $(KINC)

# Check for kernel version
ifeq ($(PATCHLEVEL),6)
default: k-dsr.ko
else 
# Assume kernel 2.4
default: k-dsr.o
endif

k-dsr.ko: $(SRC) Makefile
	$(MAKE) -C $(KERNEL_DIR) SUBDIRS=$(PWD) modules

$(KOBJS): %.o: %.c Makefile
	$(KCC) $(KCFLAGS) -c -o $@ $<

k-dsr.o: $(KOBJS)
	$(LD) -r $^ -o $@

depend:
	@echo "Updating Makefile dependencies..."
	@makedepend -Y./ -- $(DEFS) -- $(SRC) &>/dev/null

clean:
	rm -rf .*ko* .*mod* .*cmd *mod* .tmp_versions *~ *.ko *.o Makefile.bak
endif
# DO NOT DELETE

kdsr.o: dsr.h dsr-dev.h dsr-rreq.h p-queue.h debug.h
dsr-dev.o: debug.h dsr.h kdsr.h dsr-rreq.h p-queue.h
dsr-opt.o: debug.h dsr.h dsr-rreq.h kdsr.h
dsr-rreq.o: debug.h dsr.h kdsr.h dsr-rreq.h
dsr-rrep.o: dsr.h dsr-rrep.h
p-queue.o: p-queue.h debug.h
