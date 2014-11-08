#
# Copyright (C) 2013-2014 Canonical, Ltd.
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

VERSION=0.02.26
#
# Codename "excessive exerciser"
#

CFLAGS += -Wall -Wextra -DVERSION='"$(VERSION)"' -O2 -g

BINDIR=/usr/bin
MANDIR=/usr/share/man/man1

SRC =   stress-affinity.c \
	stress-bigheap.c \
	stress-bsearch.c \
	stress-cache.c \
	stress-clock.c \
	stress-cpu.c \
	stress-dentry.c \
	stress-dir.c \
	stress-eventfd.c \
	stress-fallocate.c \
	stress-flock.c \
	stress-fork.c \
	stress-fstat.c \
	stress-futex.c \
	stress-get.c \
	stress-hdd.c \
	stress-inotify.c \
	stress-iosync.c \
	stress-kill.c \
	stress-lsearch.c \
	stress-link.c \
	stress-mmap.c \
	stress-msg.c \
	stress-nice.c \
	stress-noop.c \
	stress-null.c \
	stress-open.c \
	stress-pipe.c \
	stress-poll.c \
	stress-procfs.c \
	stress-qsort.c \
	stress-rdrand.c \
	stress-rename.c \
	stress-sem.c \
	stress-sendfile.c \
	stress-sigfpe.c \
	stress-sigsegv.c \
	stress-sigq.c \
	stress-socket.c \
	stress-switch.c \
	stress-sysinfo.c \
	stress-timer.c \
	stress-tsearch.c \
	stress-urandom.c \
	stress-utime.c \
	stress-vm.c \
	stress-wait.c \
	stress-yield.c \
	stress-zero.c \
	coredump.c \
	helper.c \
	io-priority.c \
	lock-mem.c \
	log.c \
	madvise.c \
	mincore.c \
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
	cp -rp Makefile $(SRC) stress-ng.h stress-ng.1 \
		COPYING syscalls.txt stress-ng-$(VERSION)
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
