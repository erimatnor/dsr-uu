ifneq (,$(findstring 2.6,$(KERNELRELEASE)))

EXTRA_CFLAGS += -DKERNEL26 -DDEBUG
obj-m += kdsr.o
kdsr-objs := dsr-dev.o dsr-opt.o dsr-rreq.o p-queue.o


else

SRC := dsr-dev.c dsr-opt.c dsr-rreq.c p-queue.c

CC=gcc
KERNEL=$(shell uname -r)
KERNEL_DIR=/lib/modules/$(KERNEL)/build
KERNEL_INC=$(KERNEL_DIR)/include

kdsr.ko: $(SRC) Makefile
	$(MAKE) -C $(KERNEL_DIR) SUBDIRS=$(PWD) modules

clean:
	rm -rf .*ko* .*mod* .*cmd *mod* .tmp_versions *~ *.ko *.o
endif
