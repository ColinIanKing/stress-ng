#
# Copyright (C) 2013-2021 Canonical, Ltd.
# Copyright (C) 2021-2023 Colin Ian King
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

VERSION=0.15.04
#
# Codename "inconsequential levitating tardigrade"
#

CFLAGS += -Wall -Wextra -DVERSION='"$(VERSION)"' -std=gnu99
#
# Default -O2 if optimization level not defined
#
ifeq "$(findstring -O,$(CFLAGS))" ""
	CFLAGS += -O2
endif

#
# Pedantic flags
#
ifeq ($(PEDANTIC),1)
CFLAGS += -Wcast-qual -Wfloat-equal -Wmissing-declarations \
	-Wmissing-format-attribute -Wno-long-long -Wpacked \
	-Wredundant-decls -Wshadow -Wno-missing-field-initializers \
	-Wno-missing-braces -Wno-sign-compare -Wno-multichar \
	-DHAVE_PEDANTIC
endif

#
# Test for hardening flags and apply them if applicable
#
MACHINE = $(shell uname -m)
ifneq ($(PRESERVE_CFLAGS),1)
ifneq ($(MACHINE),$(filter $(MACHINE),alpha parisci ia64))
ifeq ($(shell $(CC) $(CFLAGS) -fstack-protector-strong -E -xc /dev/null > /dev/null 2>& 1 && echo 1),1)
CFLAGS += -fstack-protector-strong
endif
endif
ifeq ($(shell $(CC) $(CFLAGS) -Werror=format-security -E -xc /dev/null > /dev/null 2>& 1 && echo 1),1)
CFLAGS += -Werror=format-security
endif
ifneq ($(findstring pcc,$(CC)),pcc)
ifeq ($(shell $(CC) $(CFLAGS) -D_FORTIFY_SOURCE=2 -E -xc /dev/null > /dev/null 2>& 1 && echo 1),1)
CFLAGS += -D_FORTIFY_SOURCE=2
endif
endif
endif

#
# Expected build warnings
#
ifeq ($(UNEXPECTED),1)
CFLAGS += -DCHECK_UNEXPECTED
endif

#
# Disable any user defined PREFV setting
#
ifneq ($(PRE_V),)
override undefine PRE_V
endif
#
# Verbosity prefixes
#
ifeq ($(VERBOSE),)
PRE_V=@
PRE_Q=@
else
PRE_V=
PRE_Q=@#
endif

ifneq ($(PRESERVE_CFLAGS),1)
ifeq ($(findstring icc,$(CC)),icc)
CFLAGS += -no-inline-max-size -no-inline-max-total-size
CFLAGS += -axAVX,CORE-AVX2,CORE-AVX-I,CORE-AVX512,SSE2,SSE3,SSSE3,SSE4.1,SSE4.2,SANDYBRIDGE,SKYLAKE,SKYLAKE-AVX512,TIGERLAKE,SAPPHIRERAPIDS
CFLAGS += -ip -falign-loops -funroll-loops -ansi-alias -fma -qoverride-limits
endif
endif

#ifeq ($(findstring clang,$(CC)),clang)
#CFLAGS += -Weverything
#endif

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
# Header files
#
HEADERS = \
	core-arch.h \
	core-asm-arm.h \
	core-asm-ppc64.h \
	core-asm-riscv.h \
	core-asm-s390.h \
	core-asm-sparc.h \
	core-asm-x86.h \
	core-bitops.h \
	core-builtin.h \
	core-capabilities.h \
	core-cpu.h \
	core-cpu-cache.h \
	core-ftrace.h \
	core-hash.h \
	core-icache.h \
	core-io-priority.h \
	core-nt-load.h \
	core-nt-store.h \
	core-net.h \
	core-perf.h \
	core-pragma.h \
	core-pthread.h \
	core-put.h \
	core-resources.h \
	core-smart.h \
	core-sort.h \
	core-stressors.h \
	core-syslog.h \
	core-target-clones.h \
	core-thermal-zone.h \
	core-thrash.h \
	core-vecmath.h \
	core-version.h \
	stress-af-alg-defconfigs.h \
	stress-ng.h

#
#  Build time generated header files
#
HEADERS_GEN = \
	config.h \
	git-commit-id.h \
	io-uring.h \
	personality.h

#
#  Stressors
#
STRESS_SRC = \
	stress-access.c \
	stress-affinity.c \
	stress-af-alg.c \
	stress-aio.c \
	stress-aiol.c \
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
	stress-cacheline.c \
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
	stress-dekker.c \
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
	stress-far-branch.c \
	stress-fault.c \
	stress-fcntl.c \
	stress-file-ioctl.c \
	stress-fiemap.c \
	stress-fifo.c \
	stress-filename.c \
	stress-flock.c \
	stress-flushcache.c \
	stress-fork.c \
	stress-forkheavy.c \
	stress-fp.c \
	stress-fp-error.c \
	stress-fpunch.c \
	stress-fsize.c \
	stress-fstat.c \
	stress-full.c \
	stress-funccall.c \
	stress-funcret.c \
	stress-futex.c \
	stress-get.c \
	stress-getrandom.c \
	stress-getdent.c \
	stress-goto.c \
	stress-gpu.c \
	stress-handle.c \
	stress-hash.c \
	stress-hdd.c \
	stress-heapsort.c \
	stress-hrtimers.c \
	stress-hsearch.c \
	stress-icache.c \
	stress-icmp-flood.c \
	stress-idle-page.c \
	stress-inode-flags.c \
	stress-inotify.c \
	stress-io.c \
	stress-iomix.c \
	stress-ioport.c \
	stress-ioprio.c \
	stress-io-uring.c \
	stress-ipsec-mb.c \
	stress-itimer.c \
	stress-jpeg.c \
	stress-judy.c \
	stress-kcmp.c \
	stress-key.c \
	stress-kill.c \
	stress-klog.c \
	stress-kvm.c \
	stress-l1cache.c \
	stress-landlock.c \
	stress-lease.c \
	stress-link.c \
	stress-list.c \
	stress-llc-affinity.c \
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
	stress-mprotect.c \
	stress-mq.c \
	stress-mremap.c \
	stress-msg.c \
	stress-msync.c \
	stress-msyncmany.c \
	stress-munmap.c \
	stress-mutex.c \
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
	stress-pagemove.c \
	stress-pageswap.c \
	stress-pci.c \
	stress-personality.c \
	stress-peterson.c \
	stress-physpage.c \
	stress-pidfd.c \
	stress-ping-sock.c \
	stress-pipe.c \
	stress-pipeherd.c \
	stress-pkey.c \
	stress-plugin.c \
	stress-poll.c \
	stress-prctl.c \
	stress-prefetch.c \
	stress-priv-instr.c \
	stress-procfs.c \
	stress-pthread.c \
	stress-ptrace.c \
	stress-pty.c \
	stress-quota.c \
	stress-qsort.c \
	stress-race-sched.c \
	stress-radixsort.c \
	stress-randlist.c \
	stress-ramfs.c \
	stress-rawdev.c \
	stress-rawpkt.c \
	stress-rawsock.c \
	stress-rawudp.c \
	stress-rdrand.c \
	stress-readahead.c \
	stress-reboot.c \
	stress-regs.c \
	stress-remap.c \
	stress-rename.c \
	stress-resched.c \
	stress-resources.c \
	stress-revio.c \
	stress-ring-pipe.c \
	stress-rlimit.c \
	stress-rmap.c \
	stress-rotate.c \
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
	stress-sparsematrix.c \
	stress-splice.c \
	stress-stack.c \
	stress-stackmmap.c \
	stress-str.c \
	stress-stream.c \
	stress-swap.c \
	stress-switch.c \
	stress-sync-file.c \
	stress-syncload.c \
	stress-sysbadaddr.c \
	stress-syscall.c \
	stress-sysinfo.c \
	stress-sysinval.c \
	stress-sysfs.c \
	stress-tee.c \
	stress-timer.c \
	stress-timerfd.c \
	stress-tlb-shootdown.c \
	stress-tmpfs.c \
	stress-touch.c \
	stress-tree.c \
	stress-tsc.c \
	stress-tsearch.c \
	stress-tun.c \
	stress-udp.c \
	stress-udp-flood.c \
	stress-umount.c \
	stress-unshare.c \
	stress-uprobe.c \
	stress-urandom.c \
	stress-userfaultfd.c \
	stress-usersyscall.c \
	stress-utime.c \
	stress-vdso.c \
	stress-vecfp.c \
	stress-vecmath.c \
	stress-vecshuf.c \
	stress-vecwide.c \
	stress-verity.c \
	stress-vforkmany.c \
	stress-vm.c \
	stress-vm-addr.c \
	stress-vm-rw.c \
	stress-vm-segv.c \
	stress-vm-splice.c \
	stress-wait.c \
	stress-waitcpu.c \
	stress-watchdog.c \
	stress-wcs.c \
	stress-x86cpuid.c \
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
	core-cpu.c \
	core-cpu-cache.c \
	core-hash.c \
	core-helper.c \
	core-icache.c \
	core-ignite-cpu.c \
	core-io-uring.c \
	core-io-priority.c \
	core-job.c \
	core-killpid.c \
	core-klog.c \
	core-limit.c \
	core-lock.c \
	core-log.c \
	core-madvise.c \
	core-mincore.c \
	core-mlock.c \
	core-mmap.c \
	core-module.c \
	core-mounts.c \
	core-mwc.c \
	core-net.c \
	core-numa.c \
	core-out-of-memory.c \
	core-parse-opts.c \
	core-perf.c \
	core-resources.c \
	core-sched.c \
	core-setting.c \
	core-shared-heap.c \
	core-shim.c \
	core-smart.c \
	core-sort.c \
	core-thermal-zone.c \
	core-time.c \
	core-thrash.c \
	core-ftrace.c \
	core-try-open.c \
	core-vmstat.c \
	stress-ng.c

SRC = $(CORE_SRC) $(STRESS_SRC)
OBJS = apparmor-data.o
OBJS += $(SRC:.c=.o)

APPARMOR_PARSER=/sbin/apparmor_parser

all: config.h stress-ng

.SUFFIXES: .c .o

.o: Makefile

%.o: %.c $(HEADERS) $(HEADERS_GEN)
	$(PRE_Q)echo "CC $<"
	$(PRE_V)$(CC) $(CFLAGS) -c -o $@ $<

stress-ng: config.h $(OBJS)
	$(PRE_Q)echo "LD $@"
	$(eval LDFLAGS_EXTRA := $(shell cat config | grep CONFIG_LDFLAGS | sed 's/CONFIG_LDFLAGS += //' | tr '\n' ' '))
	$(PRE_V)$(CC) $(CPPFLAGS) $(CFLAGS) $(OBJS) -lm $(LDFLAGS) $(LDFLAGS_EXTRA) -o $@

config.h config:
	$(PRE_Q)echo "Generating config.."
	$(MAKE) CC="$(CC)" STATIC=$(STATIC) -f Makefile.config

makeconfig: config.h

#
#  generate apparmor data using minimal core utils tools from apparmor
#  parser output
#
apparmor-data.o: usr.bin.pulseaudio.eg config.h
	$(PRE_Q)rm -f apparmor-data.bin
	$(PRE_V)if [ -n "$(shell grep '^#define HAVE_APPARMOR' config.h)" ]; then \
		echo "Generating AppArmor profile from usr.bin.pulseaudio.eg"; \
		$(APPARMOR_PARSER) -Q usr.bin.pulseaudio.eg  -o apparmor-data.bin >/dev/null 2>&1 ; \
	else \
		echo "Generating empty AppArmor profile"; \
		touch apparmor-data.bin; \
	fi
	$(PRE_V)echo "#include <stddef.h>" > apparmor-data.c
	$(PRE_V)echo "char g_apparmor_data[]= { " >> apparmor-data.c
	$(PRE_V)od -tx1 -An -v < apparmor-data.bin | \
		sed 's/[0-9a-f][0-9a-f]/0x&,/g' | \
		sed '$$ s/.$$//' >> apparmor-data.c
	$(PRE_V)echo "};" >> apparmor-data.c
	$(PRE_V)rm -f apparmor-data.bin
	$(PRE_V)echo "const size_t g_apparmor_data_len = sizeof(g_apparmor_data);" >> apparmor-data.c
	$(PRE_Q)echo "CC apparmor-data.c"
	$(PRE_V)$(CC) $(CFLAGS) -c apparmor-data.c -o apparmor-data.o
	$(PRE_V)rm -f apparmor-data.c

#
#  extract the PER_* personality enums
#
personality.h: config.h
	$(PRE_V)$(CPP) $(CFLAGS) core-personality.c | $(GREP) -e "PER_[A-Z0-9]* =.*," | cut -d "=" -f 1 \
	| sed "s/.$$/,/" > personality.h
	$(PRE_Q)echo "MK personality.h"

stress-personality.c: personality.h

#
#  extract IORING_OP enums and #define HAVE_ prefixed values
#  so we can check if these enums exist
#
io-uring.h: config.h
	$(PRE_V)$(CPP) $(CFLAGS) core-io-uring.c  | $(GREP) IORING_OP | sed 's/,//' | \
	sed 's/.*\(IORING_OP_.*\)/#define HAVE_\1/' > io-uring.h
	$(PRE_Q)echo "MK io-uring.h"

stress-io-uring.c: io-uring.h

core-perf.o: core-perf.c core-perf-event.c config.h
	$(PRE_V)$(CC) $(CFLAGS) -E core-perf-event.c | $(GREP) "PERF_COUNT" | \
	sed 's/,/ /' | sed s/'^ *//' | \
	awk {'print "#define STRESS_" $$1 " (1)"'} > core-perf-event.h
	$(PRE_Q)echo CC $<
	$(PRE_V)$(CC) $(CFLAGS) -c -o $@ $<

stress-vecmath.o: stress-vecmath.c config.h
	$(PRE_Q)echo CC $<
	$(PRE_V)$(CC) $(CFLAGS) -fno-builtin -c -o $@ $<

#
#  define STRESS_GIT_COMMIT_ID
#
git-commit-id.h:
	$(PRE_Q)echo "MK $@"
	@if [ -e .git/HEAD -a -e .git/index ]; then \
		echo "#define STRESS_GIT_COMMIT_ID \"$(shell git rev-parse HEAD)\"" > $@ ; \
	else \
		echo "#define STRESS_GIT_COMMIT_ID \"\"" > $@ ; \
	fi

$(OBJS): stress-ng.h Makefile Makefile.config

stress-ng.1.gz: stress-ng.1
	$(PRE_V)gzip -n -c $< > $@

.PHONY: dist
dist:
	rm -rf stress-ng-$(VERSION)
	mkdir stress-ng-$(VERSION)
	cp -rp Makefile Makefile.config $(SRC) $(HEADERS) stress-ng.1 \
		COPYING syscalls.txt mascot README.md Dockerfile \
		README.Android test snap presentations .github \
		TODO core-perf-event.c usr.bin.pulseaudio.eg \
		core-personality.c bash-completion example-jobs .travis.yml \
		kernel-coverage.sh code-of-conduct.txt stress-ng-$(VERSION)
	tar -Jcf stress-ng-$(VERSION).tar.xz stress-ng-$(VERSION)
	rm -rf stress-ng-$(VERSION)

.PHONY: pdf
pdf:
	man -t ./stress-ng.1 | ps2pdf - > stress-ng.pdf


.PHONY: clean
clean:
	$(PRE_V)rm -f stress-ng $(OBJS) stress-ng.1.gz stress-ng.pdf
	$(PRE_V)rm -f stress-ng-$(VERSION).tar.xz
	$(PRE_V)rm -f io-uring.h
	$(PRE_V)rm -f git-commit-id.h
	$(PRE_V)rm -f core-perf-event.h
	$(PRE_V)rm -f personality.h
	$(PRE_V)rm -f apparmor-data.bin
	$(PRE_V)rm -f *.o
	$(PRE_V)rm -f config config.h
	$(PRE_V)rm -rf configs
	$(PRE_V)rm -f tags

.PHONY: fast-test-all
fast-test-all: all
	STRESS_NG=./stress-ng debian/tests/fast-test-all

.PHONY: lite-test
lite-test: all
	STRESS_NG=./stress-ng debian/tests/lite-test

.PHONY: slow-test-all
slow-test-all: all
	./stress-ng --seq 0 -t 15 --pathological --verbose --times --tz --metrics --klog-check

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

.PHONY: uninstall
uninstall:
	rm -f ${DESTDIR}${BINDIR}/stress-ng
	rm -f ${DESTDIR}${MANDIR}/stress-ng.1.gz
	rm -f ${DESTDIR}${MANDIR}/stress-ng.1
	rm -f ${DESTDIR}${JOBDIR}/*.job
	rm -f ${DESTDIR}${BASHDIR}/stress-ng
	
