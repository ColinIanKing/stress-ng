#
# Copyright (C) 2013-2021 Canonical, Ltd.
# Copyright (C) 2021 Colin Ian King
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

VERSION=0.13.07
#
# Codename "effulgent golden cetacean"
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

#
# Verbosity
#
override undefine V
ifeq ($(VERBOSE),)
V=@
Q=@
else
V=
Q=@#
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
	stress-access.c \
	stress-affinity.c \
	stress-af-alg.c \
	stress-aio.c \
	stress-aio-linux.c \
	stress-alarm.c \
	stress-apparmor.c \
	stress-atomic.c \
	stress-bad-altstack.c \
	stress-bad-ioctl.c \
	stress-bigheap.c \
	stress-bind-mount.c \
	stress-binderfs.c \
	stress-branch.c \
	stress-brk.c \
	stress-bsearch.c \
	stress-cache.c \
	stress-cap.c \
	stress-chattr.c \
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
	stress-dirmany.c \
	stress-dnotify.c \
	stress-dup.c \
	stress-dynlib.c \
	stress-efivar.c \
	stress-enosys.c \
	stress-env.c \
	stress-epoll.c \
	stress-eventfd.c \
	stress-exec.c \
	stress-exit-group.c \
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
	stress-fpunch.c \
	stress-fstat.c \
	stress-full.c \
	stress-funccall.c \
	stress-funcret.c \
	stress-futex.c \
	stress-get.c \
	stress-getrandom.c \
	stress-getdent.c \
	stress-goto.c \
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
	stress-io-uring.c \
	stress-ipsec-mb.c \
	stress-itimer.c \
	stress-judy.c \
	stress-kcmp.c \
	stress-key.c \
	stress-kill.c \
	stress-klog.c \
	stress-l1cache.c \
	stress-landlock.c \
	stress-lease.c \
	stress-link.c \
	stress-list.c \
	stress-loadavg.c \
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
	stress-memhotplug.c \
	stress-memrate.c \
	stress-memthrash.c \
	stress-mergesort.c \
	stress-mincore.c \
	stress-misaligned.c \
	stress-mknod.c \
	stress-mlock.c \
	stress-mlockmany.c \
	stress-mmap.c \
	stress-mmapaddr.c \
	stress-mmapfixed.c \
	stress-mmapfork.c \
	stress-mmaphuge.c \
	stress-mmapmany.c \
	stress-mq.c \
	stress-mremap.c \
	stress-msg.c \
	stress-msync.c \
	stress-munmap.c \
	stress-nanosleep.c \
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
	stress-pci.c \
	stress-personality.c \
	stress-physpage.c \
	stress-pidfd.c \
	stress-ping-sock.c \
	stress-pipe.c \
	stress-pipeherd.c \
	stress-pkey.c \
	stress-poll.c \
	stress-prctl.c \
	stress-prefetch.c \
	stress-procfs.c \
	stress-pthread.c \
	stress-ptrace.c \
	stress-pty.c \
	stress-quota.c \
	stress-qsort.c \
	stress-radixsort.c \
	stress-ramfs.c \
	stress-rawdev.c \
	stress-rawpkt.c \
	stress-rawsock.c \
	stress-rawudp.c \
	stress-rdrand.c \
	stress-readahead.c \
	stress-reboot.c \
	stress-remap-file-pages.c \
	stress-rename.c \
	stress-resched.c \
	stress-resources.c \
	stress-revio.c \
	stress-rlimit.c \
	stress-rmap.c \
	stress-rseq.c \
	stress-rtc.c \
	stress-sctp.c \
	stress-schedpolicy.c \
	stress-seal.c \
	stress-seccomp.c \
	stress-secretmem.c \
	stress-seek.c \
	stress-sem.c \
	stress-sem-sysv.c \
	stress-sendfile.c \
	stress-session.c \
	stress-set.c \
	stress-shellsort.c \
	stress-shm.c \
	stress-shm-sysv.c \
	stress-sigabrt.c \
	stress-sigchld.c \
	stress-sigfd.c \
	stress-sigfpe.c \
	stress-sigio.c \
	stress-signal.c \
	stress-signest.c \
	stress-sigpending.c \
	stress-sigpipe.c \
	stress-sigq.c \
	stress-sigrt.c \
	stress-sigsegv.c \
	stress-sigsuspend.c \
	stress-sigtrap.c \
	stress-skiplist.c \
	stress-sleep.c \
	stress-smi.c \
	stress-sock.c \
	stress-sockabuse.c \
	stress-sockdiag.c \
	stress-sockfd.c \
	stress-sockpair.c \
	stress-sockmany.c \
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
	stress-sysinval.c \
	stress-sysfs.c \
	stress-tee.c \
	stress-timer.c \
	stress-timerfd.c \
	stress-tlb-shootdown.c \
	stress-tmpfs.c \
	stress-tree.c \
	stress-tsc.c \
	stress-tsearch.c \
	stress-tun.c \
	stress-udp.c \
	stress-udp-flood.c \
	stress-unshare.c \
	stress-uprobe.c \
	stress-urandom.c \
	stress-userfaultfd.c \
	stress-utime.c \
	stress-vdso.c \
	stress-vecmath.c \
	stress-vecwide.c \
	stress-verity.c \
	stress-vforkmany.c \
	stress-vm.c \
	stress-vm-addr.c \
	stress-vm-rw.c \
	stress-vm-segv.c \
	stress-vm-splice.c \
	stress-wait.c \
	stress-watchdog.c \
	stress-wcstr.c \
	stress-x86syscall.c \
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
	core-hash.c \
	core-helper.c \
	core-ignite-cpu.c \
	core-io-priority.c \
	core-job.c \
	core-killpid.c \
	core-limit.c \
	core-log.c \
	core-madvise.c \
	core-mincore.c \
	core-mlock.c \
	core-mmap.c \
	core-module.c \
	core-mounts.c \
	core-mwc.c \
	core-net.c \
	core-out-of-memory.c \
	core-parse-opts.c \
	core-perf.c \
	core-sched.c \
	core-setting.c \
	core-shim.c \
	core-smart.c \
	core-thermal-zone.c \
	core-time.c \
	core-thrash.c \
	core-ftrace.c \
	core-try-open.c \
	core-vmstat.c \
	stress-ng.c

SRC = $(CORE_SRC) $(STRESS_SRC)
OBJS = $(SRC:.c=.o)

APPARMOR_PARSER=/sbin/apparmor_parser

all: makeconfig
	+$(MAKE) stress-ng VERBOSE=$(VERBOSE)

#
#  Load in and set flags based on config
#
-include config
CFLAGS += $(CONFIG_CFLAGS)
LDFLAGS += $(CONFIG_LDFLAGS)
OBJS += $(CONFIG_OBJS)

.SUFFIXES: .c .o

.o: Makefile

%.o: %.c stress-ng.h config.h git-commit-id.h
	$(Q)echo "CC $<"
	$(V)$(CC) $(CFLAGS) -c -o $@ $<

stress-ng: $(OBJS)
	$(Q)echo "LD $@"
	$(V)$(CC) $(CPPFLAGS) $(CFLAGS) $(OBJS) -lm $(LDFLAGS) -o $@
	$(V)sync

config.h:
	+$(MAKE) -f Makefile.config STATIC=$(STATIC) -j

.PHONY:
makeconfig: config.h

#
#  generate apparmor data using minimal core utils tools from apparmor
#  parser output
#
apparmor-data.o: usr.bin.pulseaudio.eg
	$(V)$(APPARMOR_PARSER) -Q usr.bin.pulseaudio.eg  -o apparmor-data.bin
	$(V)echo "#include <stddef.h>" > apparmor-data.c
	$(V)echo "char g_apparmor_data[]= { " >> apparmor-data.c
	$(V)od -tx1 -An -v < apparmor-data.bin | \
		sed 's/[0-9a-f][0-9a-f]/0x&,/g' | \
		sed '$$ s/.$$//' >> apparmor-data.c
	$(V)echo "};" >> apparmor-data.c
	$(V)echo "const size_t g_apparmor_data_len = sizeof(g_apparmor_data);" >> apparmor-data.c
	$(Q)echo "CC $<"
	$(V)$(CC) -c apparmor-data.c -o apparmor-data.o
	$(V)rm -rf apparmor-data.c apparmor-data.bin

#
#  extract the PER_* personality enums
#
personality.h:
	$(V)$(CPP) $(CONFIG_CFLAGS) core-personality.c | $(GREP) -e "PER_[A-Z0-9]* =.*," | cut -d "=" -f 1 \
	| sed "s/.$$/,/" > personality.h
	$(Q)echo "MK personality.h"

stress-personality.c: personality.h

#
#  extract IORING_OP enums and #define HAVE_ prefixed values
#  so we can check if these enums exist
#
io-uring.h:
	$(V)$(CPP) $(CFLAGS) core-io-uring.c  | $(GREP) IORING_OP | sed 's/,//' | \
	sed 's/IORING_OP_/#define HAVE_IORING_OP_/' > io-uring.h
	$(Q)echo "MK io-uring.h"

stress-io-uring.c: io-uring.h

core-perf.o: core-perf.c core-perf-event.c
	$(V)$(CC) $(CFLAGS) -E core-perf-event.c | $(GREP) "PERF_COUNT" | \
	sed 's/,/ /' | sed s/'^ *//' | \
	awk {'print "#define _SNG_" $$1 " (1)"'} > core-perf-event.h
	$(Q)echo CC $<
	$(V)$(CC) $(CFLAGS) -c -o $@ $<

stress-vecmath.o: stress-vecmath.c
	$(Q)echo CC $<
	$(V)$(CC) $(CFLAGS) -fno-builtin -c -o $@ $<

#
#  define STRESS_GIT_COMMIT_ID
#
git-commit-id.h:
	$(Q)echo "MK $@"
	@if [ -e .git/HEAD -a -e .git/index ]; then \
		echo "#define STRESS_GIT_COMMIT_ID \"$(shell git rev-parse HEAD)\"" > $@ ; \
	else \
		echo "#define STRESS_GIT_COMMIT_ID \"\"" > $@ ; \
	fi

$(OBJS): stress-ng.h Makefile

stress-ng.1.gz: stress-ng.1
	$(V)gzip -n -c $< > $@

.PHONY: dist
dist:
	rm -rf stress-ng-$(VERSION)
	mkdir stress-ng-$(VERSION)
	cp -rp Makefile Makefile.config $(SRC) stress-ng.h stress-ng.1 \
		core-personality.c core-io-uring.c \
		COPYING syscalls.txt mascot README.md \
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
	$(V)rm -f stress-ng $(OBJS) stress-ng.1.gz stress-ng.pdf
	$(V)rm -f stress-ng-$(VERSION).tar.xz
	$(V)rm -f io-uring.h
	$(V)rm -f git-commit-id.h
	$(V)rm -f perf-event.h
	$(V)rm -f personality.h
	$(V)rm -f apparmor-data.bin
	$(V)rm -f *.o
	$(V)rm -f config config.h
	$(V)rm -f tags

.PHONY: fast-test-all
fast-test-all: all
	STRESS_NG=./stress-ng debian/tests/fast-test-all

.PHONY: lite-test
lite-test: all
	STRESS_NG=./stress-ng debian/tests/lite-test

.PHONY: slow-test-all
slow-test-all: all
	./stress-ng --seq 0 -t 15 --pathological --verbose --times --tz --metrics

.PHONY: tags
tags:
	ctags -R --extra=+f --c-kinds=+p *

.PHONY: install
install: stress-ng stress-ng.1.gz
	mkdir -p ${DESTDIR}${BINDIR}
	cp stress-ng ${DESTDIR}${BINDIR}
	mkdir -p ${DESTDIR}${MANDIR}
ifneq ($(MAN_COMPRESS),0)
	cp stress-ng.1.gz ${DESTDIR}${MANDIR}
else
	cp stress-ng.1 ${DESTDIR}${MANDIR}
endif
	mkdir -p ${DESTDIR}${JOBDIR}
	cp -r example-jobs/*.job ${DESTDIR}${JOBDIR}
	mkdir -p ${DESTDIR}${BASHDIR}
	cp bash-completion/stress-ng ${DESTDIR}${BASHDIR}
