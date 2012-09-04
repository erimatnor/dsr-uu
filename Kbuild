SRC = \
    dsr-pkt.c \
    dsr-io.c \
    dsr-opt.c \
    dsr-rreq.c \
    dsr-rrep.c \
    dsr-rerr.c \
    dsr-ack.c \
    dsr-srt.c \
    send-buf.c \
    neigh.c \
    maint-buf.c \
    dsr-module.c \
    dsr-dev.c \
    debug.c

HDR = \
    atomic.h \
    debug.h \
    dsr-ack.h \
    dsr-dev.h \
    dsr.h \
    dsr-io.h \
    dsr-opt.h \
    dsr-pkt.h \
    dsr-rerr.h \
    dsr-rrep.h \
    dsr-rreq.h \
    dsr-rtc.h \
    dsr-srt.h \
    link-cache.h \
    list.h \
    maint-buf.h \
    neigh.h \
    send-buf.h \
    tbl.h \
    timer.h

RTC_SRC = \
	link-cache.c

EXTRA_CFLAGS =-DKERNEL26 -Wall -g

obj-m += dsr.o 
dsr-objs := $(SRC:%.c=%.o)

obj-m += linkcache.o
linkcache-objs := $(RTC_SRC:%.c=%.o)

clean-files := *~
clean-dirs := .tmp_versions