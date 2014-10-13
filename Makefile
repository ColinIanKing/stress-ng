VERSION=0.02.08
#
# Codename "chronically fatigued"
#

CFLAGS += -Wall -Wextra -DVERSION='"$(VERSION)"' -O2

BINDIR=/usr/bin
MANDIR=/usr/share/man/man1

OBJS =	stress-affinity.o \
	stress-bigheap.o \
	stress-cache.o \
	stress-cpu.o \
	stress-dentry.o \
	stress-dir.o \
	stress-fallocate.o \
	stress-flock.o \
	stress-fork.o \
	stress-fstat.o \
	stress-hdd.o \
	stress-iosync.o \
	stress-link.o \
	stress-mmap.o \
	stress-noop.o \
	stress-open.o \
	stress-pipe.o \
	stress-poll.o \
	stress-qsort.o \
	stress-rename.o \
	stress-sem.o \
	stress-sigsegv.o \
	stress-sigq.o \
	stress-socket.o \
	stress-switch.o \
	stress-timer.o \
	stress-urandom.o \
	stress-utime.o \
	stress-vm.o \
	stress-yield.o \
	helper.o \
	log.o \
	mwc.o \
	time.o \
 	stress-ng.o

stress-ng: $(OBJS)
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
