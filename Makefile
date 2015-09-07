#
# Copyright (C) 2013-2015 Canonical, Ltd.
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 2
# of the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
#

VERSION=0.04.16
#
# Codename "break and beleaguer"
#

CFLAGS += -Wall -Wextra -DVERSION='"$(VERSION)"' -O2

BINDIR=/usr/bin
MANDIR=/usr/share/man/man1

SRC =   stress-affinity.c \
	stress-aio.c \
	stress-aio-linux.c \
	stress-bigheap.c \
	stress-brk.c \
	stress-bsearch.c \
	stress-cache.c \
	stress-chdir.c \
	stress-chmod.c \
	stress-clock.c \
	stress-clone.c \
	stress-context.c \
	stress-cpu.c \
	stress-crypt.c \
	stress-dentry.c \
	stress-dir.c \
	stress-dup.c \
	stress-epoll.c \
	stress-eventfd.c \
	stress-fallocate.c \
	stress-fault.c \
	stress-fcntl.c \
	stress-fifo.c \
	stress-flock.c \
	stress-fork.c \
	stress-fstat.c \
	stress-futex.c \
	stress-get.c \
	stress-getrandom.c \
	stress-hdd.c \
	stress-hsearch.c \
	stress-icache.c \
	stress-inotify.c \
	stress-iosync.c \
	stress-itimer.c \
	stress-kcmp.c \
	stress-key.c \
	stress-kill.c \
	stress-lease.c \
	stress-lsearch.c \
	stress-link.c \
	stress-lockf.c \
	stress-longjmp.c \
	stress-malloc.c \
	stress-matrix.c \
	stress-memcpy.c \
	stress-memfd.c \
	stress-mincore.c \
	stress-mlock.c \
	stress-mmap.c \
	stress-mmapfork.c \
	stress-mmapmany.c \
	stress-mremap.c \
	stress-msg.c \
	stress-mq.c \
	stress-nice.c \
	stress-noop.c \
	stress-null.c \
	stress-numa.c \
	stress-open.c \
	stress-pipe.c \
	stress-poll.c \
	stress-procfs.c \
	stress-pthread.c \
	stress-ptrace.c \
	stress-quota.c \
	stress-qsort.c \
	stress-rdrand.c \
	stress-readahead.c \
	stress-rename.c \
	stress-rlimit.c \
	stress-seek.c \
	stress-sem.c \
	stress-sem-sysv.c \
	stress-sendfile.c \
	stress-shm-sysv.c \
	stress-sigfd.c \
	stress-sigfpe.c \
	stress-sigpending.c \
	stress-sigsegv.c \
	stress-sigsuspend.c \
	stress-sigq.c \
	stress-socket.c \
	stress-socketpair.c \
	stress-splice.c \
	stress-stack.c \
	stress-str.c \
	stress-switch.c \
	stress-sysinfo.c \
	stress-sysfs.c \
	stress-tee.c \
	stress-timer.c \
	stress-timerfd.c \
	stress-tsearch.c \
	stress-udp.c \
	stress-udp-flood.c \
	stress-urandom.c \
	stress-utime.c \
	stress-vecmath.c \
	stress-vm.c \
	stress-vm-rw.c \
	stress-vm-splice.c \
	stress-wait.c \
	stress-wcstr.c \
	stress-xattr.c \
	stress-yield.c \
	stress-zero.c \
	stress-zombie.c \
	coredump.c \
	helper.c \
	io-priority.c \
	limit.c \
	log.c \
	madvise.c \
	mincore.c \
	mlock.c \
	mounts.c \
	mwc.c \
	net.c \
	out-of-memory.c \
	parse-opts.c \
	perf.c \
	proc-name.c \
	sched.c \
	thermal-zone.c \
	time.c \
	stress-ng.c

OBJS = $(SRC:.c=.o)

.SUFFIXES: .c .o

.o: stress-ng.h Makefile

.c.o: stress-ng.h Makefile $(SRC)
	@echo $(CC) $(CFLAGS) -c -o $@ $<
	@$(CC) $(CFLAGS) -c -o $@ $<

stress-ng: $(OBJS)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(OBJS) -lm -pthread -lrt -lcrypt -o $@ $(LDFLAGS)

stress-cpu.o: stress-cpu.c
	@echo $(CC) $(CFLAGS) -c -o $@ $<
	@echo "_Decimal32 x;" > test-decimal.c
	@$(CC) $(CPPFLAGS) -c -o test-decimal.o test-decimal.c 2> /dev/null || true
	@if [ -e test-decimal.o ]; then \
		$(CC) $(CFLAGS) -DSTRESS_FLOAT_DECIMAL -c -o $@ $< ;\
	else \
		$(CC) $(CFLAGS) -c -o $@ $< ;\
	fi
	@rm -f test-decimal.c test-decimal.o

stress-str.o: stress-str.c
	@echo $(CC) $(CFLAGS) -fno-builtin -c -o $@ $<
	@$(CC) $(CFLAGS) -fno-builtin -c -o $@ $<

stress-wcstr.o: stress-wcstr.c
	@echo $(CC) $(CFLAGS) -fno-builtin -c -o $@ $<
	@$(CC) $(CFLAGS) -fno-builtin -c -o $@ $<

$(OBJS): stress-ng.h Makefile

stress-ng.1.gz: stress-ng.1
	gzip -c $< > $@

dist:
	rm -rf stress-ng-$(VERSION)
	mkdir stress-ng-$(VERSION)
	cp -rp Makefile $(SRC) stress-ng.h stress-ng.1 \
		COPYING syscalls.txt mascot README README.Android \
		stress-ng-$(VERSION)
	tar -zcf stress-ng-$(VERSION).tar.gz stress-ng-$(VERSION)
	rm -rf stress-ng-$(VERSION)

clean:
	rm -f stress-ng $(OBJS) stress-ng.1.gz
	rm -f stress-ng-$(VERSION).tar.gz
	rm -f test-decimal.c

install: stress-ng stress-ng.1.gz
	mkdir -p ${DESTDIR}${BINDIR}
	cp stress-ng ${DESTDIR}${BINDIR}
	mkdir -p ${DESTDIR}${MANDIR}
	cp stress-ng.1.gz ${DESTDIR}${MANDIR}
