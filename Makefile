#
# Copyright (C) 2013-2021 Canonical, Ltd.
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

VERSION=0.12.04
#
# Codename "synthetic system strainer"
#

CFLAGS += -Wall -Wextra -DVERSION='"$(VERSION)"' -O2 -std=gnu99

#
# Pedantic flags
#
ifeq ($(PEDANTIC),1)
CFLAGS += -Wcast-qual -Wfloat-equal -Wmissing-declarations \
	-Wmissing-format-attribute -Wno-long-long -Wpacked \
	-Wredundant-decls -Wshadow -Wno-missing-field-initializers \
	-Wno-missing-braces -Wno-sign-compare -Wno-multichar
endif

GREP = grep
#
# SunOS requires special grep for -e support
#
KERNEL=$(shell uname -s)
NODENAME=$(shell uname -n)
ifeq ($(KERNEL),SunOS)
ifneq ($(NODENAME),dilos)
GREP = /usr/xpg4/bin/grep
endif
endif

#
# Static flags, only to be used when using GCC
#
ifeq ($(STATIC),1)
LDFLAGS += -static -z muldefs
CFLAGS += -DBUILD_STATIC
endif

BINDIR=/usr/bin
MANDIR=/usr/share/man/man1
JOBDIR=/usr/share/stress-ng/example-jobs
BASHDIR=/usr/share/bash-completion/completions

#
#  Stressors
#
STRESS_SRC = \
	src/stress-access.c \
	src/stress-affinity.c \
	src/stress-af-alg.c \
	src/stress-aio.c \
	src/stress-aio-linux.c \
	src/stress-apparmor.c \
	src/stress-atomic.c \
	src/stress-bad-altstack.c \
	src/stress-bad-ioctl.c \
	src/stress-bigheap.c \
	src/stress-bind-mount.c \
	src/stress-binderfs.c \
	src/stress-branch.c \
	src/stress-brk.c \
	src/stress-bsearch.c \
	src/stress-cache.c \
	src/stress-cap.c \
	src/stress-chattr.c \
	src/stress-chdir.c \
	src/stress-chmod.c \
	src/stress-chown.c \
	src/stress-chroot.c \
	src/stress-clock.c \
	src/stress-clone.c \
	src/stress-close.c \
	src/stress-context.c \
	src/stress-copy-file.c \
	src/stress-cpu.c \
	src/stress-cpu-online.c \
	src/stress-crypt.c \
	src/stress-cyclic.c \
	src/stress-daemon.c \
	src/stress-dccp.c \
	src/stress-dentry.c \
	src/stress-dev.c \
	src/stress-dev-shm.c \
	src/stress-dir.c \
	src/stress-dirdeep.c \
	src/stress-dnotify.c \
	src/stress-dup.c \
	src/stress-dynlib.c \
	src/stress-efivar.c \
	src/stress-enosys.c \
	src/stress-env.c \
	src/stress-epoll.c \
	src/stress-eventfd.c \
	src/stress-exec.c \
	src/stress-fallocate.c \
	src/stress-fanotify.c \
	src/stress-fault.c \
	src/stress-fcntl.c \
	src/stress-file-ioctl.c \
	src/stress-fiemap.c \
	src/stress-fifo.c \
	src/stress-filename.c \
	src/stress-flock.c \
	src/stress-fork.c \
	src/stress-fp-error.c \
	src/stress-fstat.c \
	src/stress-full.c \
	src/stress-funccall.c \
	src/stress-funcret.c \
	src/stress-futex.c \
	src/stress-get.c \
	src/stress-getrandom.c \
	src/stress-getdent.c \
	src/stress-handle.c \
	src/stress-hdd.c \
	src/stress-heapsort.c \
	src/stress-hrtimers.c \
	src/stress-hsearch.c \
	src/stress-icache.c \
	src/stress-icmp-flood.c \
	src/stress-idle-page.c \
	src/stress-inode-flags.c \
	src/stress-inotify.c \
	src/stress-iomix.c \
	src/stress-ioport.c \
	src/stress-ioprio.c \
	src/stress-iosync.c \
	src/stress-io-uring.c \
	src/stress-ipsec-mb.c \
	src/stress-itimer.c \
	src/stress-judy.c \
	src/stress-kcmp.c \
	src/stress-key.c \
	src/stress-kill.c \
	src/stress-klog.c \
	src/stress-l1cache.c \
	src/stress-lease.c \
	src/stress-link.c \
	src/stress-lockbus.c \
	src/stress-locka.c \
	src/stress-lockf.c \
	src/stress-lockofd.c \
	src/stress-longjmp.c \
	src/stress-loop.c \
	src/stress-lsearch.c \
	src/stress-madvise.c \
	src/stress-malloc.c \
	src/stress-matrix.c \
	src/stress-matrix-3d.c \
	src/stress-mcontend.c \
	src/stress-membarrier.c \
	src/stress-memcpy.c \
	src/stress-memfd.c \
	src/stress-memhotplug.c \
	src/stress-memrate.c \
	src/stress-memthrash.c \
	src/stress-mergesort.c \
	src/stress-mincore.c \
	src/stress-mknod.c \
	src/stress-mlock.c \
	src/stress-mlockmany.c \
	src/stress-mmap.c \
	src/stress-mmapaddr.c \
	src/stress-mmapfixed.c \
	src/stress-mmapfork.c \
	src/stress-mmapmany.c \
	src/stress-mremap.c \
	src/stress-msg.c \
	src/stress-msync.c \
	src/stress-mq.c \
	src/stress-nanosleep.c \
	src/stress-netdev.c \
	src/stress-netlink-proc.c \
	src/stress-netlink-task.c \
	src/stress-nice.c \
	src/stress-nop.c \
	src/stress-null.c \
	src/stress-numa.c \
	src/stress-oom-pipe.c \
	src/stress-opcode.c \
	src/stress-open.c \
	src/stress-personality.c \
	src/stress-physpage.c \
	src/stress-pidfd.c \
	src/stress-ping-sock.c \
	src/stress-pipe.c \
	src/stress-pipeherd.c \
	src/stress-pkey.c \
	src/stress-poll.c \
	src/stress-prctl.c \
	src/stress-procfs.c \
	src/stress-pthread.c \
	src/stress-ptrace.c \
	src/stress-pty.c \
	src/stress-quota.c \
	src/stress-qsort.c \
	src/stress-radixsort.c \
	src/stress-ramfs.c \
	src/stress-rawdev.c \
	src/stress-rawpkt.c \
	src/stress-rawsock.c \
	src/stress-rawudp.c \
	src/stress-rdrand.c \
	src/stress-readahead.c \
	src/stress-reboot.c \
	src/stress-remap-file-pages.c \
	src/stress-rename.c \
	src/stress-resources.c \
	src/stress-revio.c \
	src/stress-rlimit.c \
	src/stress-rmap.c \
	src/stress-rseq.c \
	src/stress-rtc.c \
	src/stress-sctp.c \
	src/stress-schedpolicy.c \
	src/stress-seal.c \
	src/stress-seccomp.c \
	src/stress-secretmem.c \
	src/stress-seek.c \
	src/stress-sem.c \
	src/stress-sem-sysv.c \
	src/stress-sendfile.c \
	src/stress-session.c \
	src/stress-set.c \
	src/stress-shellsort.c \
	src/stress-shm.c \
	src/stress-shm-sysv.c \
	src/stress-sigabrt.c \
	src/stress-sigchld.c \
	src/stress-sigfd.c \
	src/stress-sigfpe.c \
	src/stress-sigio.c \
	src/stress-signal.c \
	src/stress-sigpending.c \
	src/stress-sigpipe.c \
	src/stress-sigq.c \
	src/stress-sigrt.c \
	src/stress-sigsegv.c \
	src/stress-sigsuspend.c \
	src/stress-sigtrap.c \
	src/stress-skiplist.c \
	src/stress-sleep.c \
	src/stress-sock.c \
	src/stress-sockabuse.c \
	src/stress-sockdiag.c \
	src/stress-sockfd.c \
	src/stress-sockpair.c \
	src/stress-sockmany.c \
	src/stress-softlockup.c \
	src/stress-spawn.c \
	src/stress-splice.c \
	src/stress-stack.c \
	src/stress-stackmmap.c \
	src/stress-str.c \
	src/stress-stream.c \
	src/stress-swap.c \
	src/stress-switch.c \
	src/stress-sync-file.c \
	src/stress-sysbadaddr.c \
	src/stress-sysinfo.c \
	src/stress-sysinval.c \
	src/stress-sysfs.c \
	src/stress-tee.c \
	src/stress-timer.c \
	src/stress-timerfd.c \
	src/stress-tlb-shootdown.c \
	src/stress-tmpfs.c \
	src/stress-tree.c \
	src/stress-tsc.c \
	src/stress-tsearch.c \
	src/stress-tun.c \
	src/stress-udp.c \
	src/stress-udp-flood.c \
	src/stress-unshare.c \
	src/stress-uprobe.c \
	src/stress-urandom.c \
	src/stress-userfaultfd.c \
	src/stress-utime.c \
	src/stress-vdso.c \
	src/stress-vecmath.c \
	src/stress-verity.c \
	src/stress-vforkmany.c \
	src/stress-vm.c \
	src/stress-vm-addr.c \
	src/stress-vm-rw.c \
	src/stress-vm-segv.c \
	src/stress-vm-splice.c \
	src/stress-wait.c \
	src/stress-watchdog.c \
	src/stress-wcstr.c \
	src/stress-x86syscall.c \
	src/stress-xattr.c \
	src/stress-yield.c \
	src/stress-zero.c \
	src/stress-zlib.c \
	src/stress-zombie.c \

#
# Stress core
#
CORE_SRC = \
	src/core-affinity.c \
	src/core-cache.c \
	src/core-cpu.c \
	src/core-hash.c \
	src/core-helper.c \
	src/core-ignite-cpu.c \
	src/core-io-priority.c \
	src/core-job.c \
	src/core-limit.c \
	src/core-log.c \
	src/core-madvise.c \
	src/core-mincore.c \
	src/core-mlock.c \
	src/core-mmap.c \
	src/core-mounts.c \
	src/core-mwc.c \
	src/core-net.c \
	src/core-out-of-memory.c \
	src/core-parse-opts.c \
	src/core-perf.c \
	src/core-sched.c \
	src/core-setting.c \
	src/core-shim.c \
	src/core-thermal-zone.c \
	src/core-time.c \
	src/core-thrash.c \
	src/core-ftrace.c \
	src/core-try-open.c \
	src/core-vmstat.c \
	src/stress-ng.c

SRC = $(CORE_SRC) $(STRESS_SRC)
OBJS = $(SRC:.c=.o)

APPARMOR_PARSER=/sbin/apparmor_parser

all:
	$(MAKE) makeconfig -j1
	$(MAKE) stress-ng

#
#  Load in and set flags based on config
#
-include config
CFLAGS += $(CONFIG_CFLAGS)
LDFLAGS += $(CONFIG_LDFLAGS)
OBJS += $(CONFIG_OBJS)

.SUFFIXES: .c .o

.o: src/stress-ng.h Makefile

.c.o:
	@echo "CC $<"
	@$(CC) $(CFLAGS) -c -o $@ $<

stress-ng: $(OBJS)
	@echo "LD $@"
	@$(CC) $(CPPFLAGS) $(CFLAGS) $(OBJS) -lm $(LDFLAGS) -o $@
	@sync

makeconfig:
	@if [ ! -e config ]; then \
		STATIC=$(STATIC) $(MAKE) -f Makefile.config; \
	fi

#
#  generate apparmor data using minimal core utils tools from apparmor
#  parser output
#
apparmor-data.o: src/usr.bin.pulseaudio.eg
	@$(APPARMOR_PARSER) -Q usr.bin.pulseaudio.eg  -o apparmor-data.bin
	@echo "#include <stddef.h>" > apparmor-data.c
	@echo "char g_apparmor_data[]= { " >> apparmor-data.c
	@od -tx1 -An -v < apparmor-data.bin | \
		sed 's/[0-9a-f][0-9a-f]/0x&,/g' | \
		sed '$$ s/.$$//' >> apparmor-data.c
	@echo "};" >> apparmor-data.c
	@echo "const size_t g_apparmor_data_len = sizeof(g_apparmor_data);" >> apparmor-data.c
	@echo "CC $<"
	@$(CC) -c apparmor-data.c -o apparmor-data.o
	@rm -rf apparmor-data.c apparmor-data.bin

#
#  extract the PER_* personality enums
#
src/personality.h:
	@$(CPP) $(CONFIG_CFLAGS) src/core-personality.c | $(GREP) -e "PER_[A-Z0-9]* =.*," | cut -d "=" -f 1 \
	| sed "s/.$$/,/" > src/personality.h
	@echo "MK personality.h"

src/stress-personality.c: src/personality.h

#
#  extract IORING_OP enums and #define HAVE_ prefixed values
#  so we can check if these enums exist
#
src/io-uring.h:
	@$(CPP) $(CONFIG_CFLAGS) src/core-io-uring.c  | $(GREP) IORING_OP | sed 's/,//' | \
	sed 's/IORING_OP_/#define HAVE_IORING_OP_/' > src/io-uring.h
	@echo "MK io-uring.h"

src/stress-io-uring.c: src/io-uring.h

core-perf.o: src/core-perf.c src/core-perf-event.c
	@$(CC) $(CFLAGS) -E src/core-perf-event.c | $(GREP) "PERF_COUNT" | \
	sed 's/,/ /' | sed s/'^ *//' | \
	awk {'print "#define _SNG_" $$1 " (1)"'} > src/core-perf-event.h
	@echo CC $<
	@$(CC) $(CFLAGS) -c -o $@ $<

stress-vecmath.o: stress-vecmath.c
	@echo CC $<
	@$(CC) $(CFLAGS) -fno-builtin -c -o $@ $<

$(OBJS): src/stress-ng.h Makefile

stress-ng.1.gz: stress-ng.1
	gzip -c $< > $@

.PHONY: dist
dist:
	rm -rf stress-ng-$(VERSION)
	mkdir stress-ng-$(VERSION)
	cp -rp Makefile Makefile.config $(SRC) src/stress-ng.h stress-ng.1 \
		core-personality.c core-io-uring.c \
		COPYING syscalls.txt mascot README \
		stress-af-alg-defconfigs.h README.Android test snap \
		TODO core-perf-event.c usr.bin.pulseaudio.eg \
		stress-version.h bash-completion example-jobs .travis.yml \
		kernel-coverage.sh code-of-conduct.txt stress-ng-$(VERSION)
	tar -Jcf stress-ng-$(VERSION).tar.xz stress-ng-$(VERSION)
	rm -rf stress-ng-$(VERSION)

.PHONY: pdf
pdf:
	man -t ./stress-ng.1 | ps2pdf - > stress-ng.pdf


.PHONY: clean
clean:
	@rm -f stress-ng $(OBJS) stress-ng.1.gz stress-ng.pdf
	@rm -f stress-ng-$(VERSION).tar.xz
	@rm -f personality.h
	@rm -f io-uring.h
	@rm -f perf-event.h
	@rm -f apparmor-data.bin
	@rm -f *.o
	@rm -f config

.PHONY: fast-test-all
fast-test-all: all
	STRESS_NG=./stress-ng debian/tests/fast-test-all

.PHONY: lite-test
lite-test: all
	STRESS_NG=./stress-ng debian/tests/lite-test

.PHONY: slow-test-all
slow-test-all: all
	./stress-ng --seq 0 -t 15 --pathological --verbose --times --tz --metrics

.PHONY: install
install: stress-ng stress-ng.1.gz
	mkdir -p ${DESTDIR}${BINDIR}
	cp stress-ng ${DESTDIR}${BINDIR}
	mkdir -p ${DESTDIR}${MANDIR}
	cp stress-ng.1.gz ${DESTDIR}${MANDIR}
	mkdir -p ${DESTDIR}${JOBDIR}
	cp -rp example-jobs/*.job ${DESTDIR}${JOBDIR}
	mkdir -p ${DESTDIR}${BASHDIR}
	cp bash-completion/stress-ng ${DESTDIR}${BASHDIR}
