ifneq (,$(findstring 2.6,$(KERNELRELEASE)))

EXTRA_CFLAGS += -DKERNEL26 -DDEBUG
obj-m += k-dsr.o
k-dsr-objs := kdsr.o dsr-dev.o dsr-opt.o dsr-rreq.o p-queue.o

else

SRC := dsr-dev.c dsr-opt.c dsr-rreq.c p-queue.c kdsr.c

CC=gcc
KERNEL=$(shell uname -r)
KERNEL_DIR=/lib/modules/$(KERNEL)/build
KERNEL_INC=$(KERNEL_DIR)/include

DEFS=-DKERNEL26 -DDEBUG

k-dsr.ko: $(SRC) Makefile
	$(MAKE) -C $(KERNEL_DIR) SUBDIRS=$(PWD) modules

depend:
	@echo "Updating Makefile dependencies..."
	@makedepend -Y./ -- $(DEFS) -- $(SRC) &>/dev/null

clean:
	rm -rf .*ko* .*mod* .*cmd *mod* .tmp_versions *~ *.ko *.o
endif
# DO NOT DELETE

dsr-dev.o: debug.h dsr.h dsr-rreq.h p-queue.h
dsr-opt.o: debug.h dsr.h kdsr.h
dsr-rreq.o: debug.h dsr.h kdsr.h dsr-rreq.h
p-queue.o: p-queue.h
kdsr.o: dsr.h dsr-dev.h p-queue.h debug.h
