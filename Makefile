#
# Copyright (C) 2013-2019 Canonical, Ltd.
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

VERSION=0.10.00
#
# Codename "systematic system smasher"
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
# Check for KFreeBSD
#
ifeq ($(KERNEL),GNU/kFreeBSD)
CFLAGS += -D__FreeBSD_Kernel__
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
	stress-access.c \
	stress-affinity.c \
	stress-af-alg.c \
	stress-aio.c \
	stress-aio-linux.c \
	stress-apparmor.c \
	stress-atomic.c \
	stress-bad-altstack.c \
	stress-bigheap.c \
	stress-bind-mount.c \
	stress-branch.c \
	stress-brk.c \
	stress-bsearch.c \
	stress-cache.c \
	stress-cap.c \
	stress-chdir.c \
	stress-chmod.c \
	stress-chown.c \
	stress-chroot.c \
	stress-clock.c \
	stress-clone.c \
	stress-close.c \
	stress-context.c \
	stress-copy-file.c \
	stress-cpu.c \
	stress-cpu-online.c \
	stress-crypt.c \
	stress-cyclic.c \
	stress-daemon.c \
	stress-dccp.c \
	stress-dentry.c \
	stress-dev.c \
	stress-dev-shm.c \
	stress-dir.c \
	stress-dirdeep.c \
	stress-dnotify.c \
	stress-dup.c \
	stress-dynlib.c \
	stress-efivar.c \
	stress-enosys.c \
	stress-epoll.c \
	stress-eventfd.c \
	stress-exec.c \
	stress-fallocate.c \
	stress-fanotify.c \
	stress-fault.c \
	stress-fcntl.c \
	stress-file-ioctl.c \
	stress-fiemap.c \
	stress-fifo.c \
	stress-filename.c \
	stress-flock.c \
	stress-fork.c \
	stress-fp-error.c \
	stress-fstat.c \
	stress-full.c \
	stress-funccall.c \
	stress-funcret.c \
	stress-futex.c \
	stress-get.c \
	stress-getrandom.c \
	stress-getdent.c \
	stress-handle.c \
	stress-hdd.c \
	stress-heapsort.c \
	stress-hrtimers.c \
	stress-hsearch.c \
	stress-icache.c \
	stress-icmp-flood.c \
	stress-idle-page.c \
	stress-inode-flags.c \
	stress-inotify.c \
	stress-iomix.c \
	stress-ioport.c \
	stress-ioprio.c \
	stress-iosync.c \
	stress-ipsec-mb.c \
	stress-itimer.c \
	stress-kcmp.c \
	stress-key.c \
	stress-kill.c \
	stress-klog.c \
	stress-lease.c \
	stress-link.c \
	stress-lockbus.c \
	stress-locka.c \
	stress-lockf.c \
	stress-lockofd.c \
	stress-longjmp.c \
	stress-loop.c \
	stress-lsearch.c \
	stress-madvise.c \
	stress-malloc.c \
	stress-matrix.c \
	stress-matrix-3d.c \
	stress-mcontend.c \
	stress-membarrier.c \
	stress-memcpy.c \
	stress-memfd.c \
	stress-memrate.c \
	stress-memthrash.c \
	stress-mergesort.c \
	stress-mincore.c \
	stress-mknod.c \
	stress-mlock.c \
	stress-mlockmany.c \
	stress-mmap.c \
	stress-mmapaddr.c \
	stress-mmapfixed.c \
	stress-mmapfork.c \
	stress-mmapmany.c \
	stress-mremap.c \
	stress-msg.c \
	stress-msync.c \
	stress-mq.c \
	stress-netdev.c \
	stress-netlink-proc.c \
	stress-netlink-task.c \
	stress-nice.c \
	stress-nop.c \
	stress-null.c \
	stress-numa.c \
	stress-oom-pipe.c \
	stress-opcode.c \
	stress-open.c \
	stress-personality.c \
	stress-physpage.c \
	stress-pidfd.c \
	stress-pipe.c \
	stress-pkey.c \
	stress-poll.c \
	stress-prctl.c \
	stress-procfs.c \
	stress-pthread.c \
	stress-ptrace.c \
	stress-pty.c \
	stress-quota.c \
	stress-qsort.c \
	stress-radixsort.c \
	stress-rawdev.c \
	stress-rawsock.c \
	stress-rdrand.c \
	stress-readahead.c \
	stress-remap-file-pages.c \
	stress-rename.c \
	stress-resources.c \
	stress-revio.c \
	stress-rlimit.c \
	stress-rmap.c \
	stress-rtc.c \
	stress-sctp.c \
	stress-schedpolicy.c \
	stress-seal.c \
	stress-seccomp.c \
	stress-seek.c \
	stress-sem.c \
	stress-sem-sysv.c \
	stress-sendfile.c \
	stress-set.c \
	stress-shellsort.c \
	stress-shm.c \
	stress-shm-sysv.c \
	stress-sigfd.c \
	stress-sigfpe.c \
	stress-sigio.c \
	stress-sigpending.c \
	stress-sigpipe.c \
	stress-sigq.c \
	stress-sigrt.c \
	stress-sigsegv.c \
	stress-sigsuspend.c \
	stress-sleep.c \
	stress-socket.c \
	stress-socket-diag.c \
	stress-socket-fd.c \
	stress-socketpair.c \
	stress-softlockup.c \
	stress-spawn.c \
	stress-splice.c \
	stress-stack.c \
	stress-stackmmap.c \
	stress-str.c \
	stress-stream.c \
	stress-swap.c \
	stress-switch.c \
	stress-sync-file.c \
	stress-sysbadaddr.c \
	stress-sysinfo.c \
	stress-sysfs.c \
	stress-tee.c \
	stress-timer.c \
	stress-timerfd.c \
	stress-tlb-shootdown.c \
	stress-tmpfs.c \
	stress-tree.c \
	stress-tsc.c \
	stress-tsearch.c \
	stress-udp.c \
	stress-udp-flood.c \
	stress-unshare.c \
	stress-urandom.c \
	stress-userfaultfd.c \
	stress-utime.c \
	stress-vdso.c \
	stress-vecmath.c \
	stress-vforkmany.c \
	stress-vm.c \
	stress-vm-addr.c \
	stress-vm-rw.c \
	stress-vm-segv.c \
	stress-vm-splice.c \
	stress-wait.c \
	stress-watchdog.c \
	stress-wcstr.c \
	stress-xattr.c \
	stress-yield.c \
	stress-zero.c \
	stress-zlib.c \
	stress-zombie.c \

#
# Stress core
#
CORE_SRC = \
	core-affinity.c \
	core-cache.c \
	core-cpu.c \
	core-helper.c \
	core-ignite-cpu.c \
	core-io-priority.c \
	core-job.c \
	core-limit.c \
	core-log.c \
	core-madvise.c \
	core-mincore.c \
	core-mlock.c \
	core-mmap.c \
	core-mounts.c \
	core-mwc.c \
	core-net.c \
	core-out-of-memory.c \
	core-parse-opts.c \
	core-perf.c \
	core-sched.c \
	core-setting.c \
	core-shim.c \
	core-thermal-zone.c \
	core-time.c \
	core-thrash.c \
	stress-ng.c

SRC = $(STRESS_SRC) $(CORE_SRC)
OBJS = $(SRC:.c=.o)

APPARMOR_PARSER=/sbin/apparmor_parser

LIB_APPARMOR := -lapparmor
LIB_BSD := -lbsd
LIB_Z := -lz
LIB_CRYPT := -lcrypt
LIB_RT := -lrt
LIB_PTHREAD := -lpthread
LIB_AIO = -laio
LIB_SCTP = -lsctp
LIB_DL = -ldl

#
#  Load in and set flags based on config
#
-include config
CFLAGS += $(CONFIG_CFLAGS)
LDFLAGS += $(CONFIG_LDFLAGS)
OBJS += $(CONFIG_OBJS)

all:
ifneq ("$(wildcard config)","")
	$(MAKE) makeconfig
endif
	$(MAKE) stress-ng

.SUFFIXES: .c .o

.o: stress-ng.h Makefile

.c.o: stress-ng.h Makefile $(SRC)
	@echo "CC $<"
	@$(CC) $(CFLAGS) -c -o $@ $<

stress-ng: $(OBJS)
	@echo "LD $@"
	@$(CC) $(CPPFLAGS) $(CFLAGS) $(OBJS) -lm $(LDFLAGS) -o $@
	@sync

makeconfig:
	@if [ ! -s config ]; then \
		STATIC=$(STATIC) $(MAKE) -f Makefile.config; \
	fi

#
#  generate apparmor data using minimal core utils tools from apparmor
#  parser output
#
apparmor-data.o: usr.bin.pulseaudio.eg
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
personality.h:
	@$(CPP) core-personality.c | $(GREP) -e "PER_[A-Z0-9]* =.*," | cut -d "=" -f 1 \
	| sed "s/.$$/,/" > personality.h

stress-personality.c: personality.h

core-perf.o: core-perf.c core-perf-event.c
	@$(CC) $(CFLAGS) -E core-perf-event.c | grep "PERF_COUNT" | sed 's/,/ /' | \
	awk {'print "#define _SNG_" $$1 " (1)"'} > core-perf-event.h
	@echo CC $<
	@$(CC) $(CFLAGS) -c -o $@ $<

stress-vecmath.o: stress-vecmath.c
	@echo CC $<
	@$(CC) $(CFLAGS) -fno-builtin -c -o $@ $<
	@touch stress-ng.c

$(OBJS): stress-ng.h Makefile

stress-ng.1.gz: stress-ng.1
	gzip -c $< > $@

.PHONY: dist
dist:
	rm -rf stress-ng-$(VERSION)
	mkdir stress-ng-$(VERSION)
	cp -rp Makefile Makefile.config $(SRC) stress-ng.h stress-ng.1 \
		core-personality.c COPYING syscalls.txt mascot README \
		README.Android test snap smatchify.sh config TODO \
		core-perf-event.c usr.bin.pulseaudio.eg stress-version.h \
		bash-completion example-jobs .travis.yml kernel-coverage.sh \
		stress-ng-$(VERSION)
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
	@rm -f perf-event.h
	@rm -f apparmor-data.bin
	@rm -f *.o
	@:> config

.PHONY: fast-test-all
fast-test-all: all
	STRESS_NG=./stress-ng debian/tests/fast-test-all

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
