VERSION=0.02.15
#
# Codename "chronically fatigued"
#

CFLAGS += -Wall -Wextra -DVERSION='"$(VERSION)"' -O2

BINDIR=/usr/bin
MANDIR=/usr/share/man/man1

SRC =   stress-affinity.c \
	stress-bigheap.c \
	stress-cache.c \
	stress-cpu.c \
	stress-dentry.c \
	stress-dir.c \
	stress-fallocate.c \
	stress-flock.c \
	stress-fork.c \
	stress-fstat.c \
	stress-hdd.c \
	stress-iosync.c \
	stress-link.c \
	stress-mmap.c \
	stress-msg.c \
	stress-nice.c \
	stress-noop.c \
	stress-open.c \
	stress-pipe.c \
	stress-poll.c \
	stress-qsort.c \
	stress-rdrand.c \
	stress-rename.c \
	stress-sem.c \
	stress-sigfpe.c \
	stress-sigsegv.c \
	stress-sigq.c \
	stress-socket.c \
	stress-switch.c \
	stress-timer.c \
	stress-urandom.c \
	stress-utime.c \
	stress-vm.c \
	stress-yield.c \
	coredump.c \
	helper.c \
	io-priority.c \
	lock-mem.c \
	log.c \
	mwc.c \
	out-of-memory.c \
	parse-opts.c \
	proc-name.c \
	sched.c \
	time.c \
	stress-ng.c

OBJS = $(SRC:.c=.o)

%.o: %.c stress-ng.h
	$(CC) $(CFLAGS) -c -o $@ $<

stress-ng: $(OBJS) Makefile
	$(CC) $(CPPFLAGS) $(CFLAGS) $(OBJS) -lm -lrt -lpthread -o $@ $(LDFLAGS)

stress-ng.1.gz: stress-ng.1
	gzip -c $< > $@

dist:
	rm -rf stress-ng-$(VERSION)
	mkdir stress-ng-$(VERSION)
	cp -rp Makefile stress-ng.c helper.c stress-ng.1 COPYING stress-ng-$(VERSION)
	tar -zcf stress-ng-$(VERSION).tar.gz stress-ng-$(VERSION)
	rm -rf stress-ng-$(VERSION)

clean:
	rm -f stress-ng $(OBJS) stress-ng.1.gz
	rm -f stress-ng-$(VERSION).tar.gz

install: stress-ng stress-ng.1.gz
	mkdir -p ${DESTDIR}${BINDIR}
	cp stress-ng ${DESTDIR}${BINDIR}
	mkdir -p ${DESTDIR}${MANDIR}
	cp stress-ng.1.gz ${DESTDIR}${MANDIR}
