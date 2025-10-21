#
# Copyright (C) 2013-2021 Canonical, Ltd.
# Copyright (C) 2021-2025 Colin Ian King
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

#
# Codename "tricky timer tester"
#
VERSION=0.19.05

#
# Determine supported toolchains
#
COMPILER = cc
ifneq ($(shell $(CC) -v 2>&1 | grep version | grep gcc),)
COMPILER = gcc
endif
ifneq ($(shell $(CC) -v 2>&1 | grep version | grep icc),)
COMPILER = icc
endif
ifneq ($(shell $(CC) -v 2>&1 | grep "Portable C Compiler"),)
COMPILER = pcc
endif
ifneq ($(shell $(CC) -v 2>&1 | grep "tcc"),)
COMPILER = tcc
endif
ifneq ($(shell $(CC) -v 2>&1 | grep version | grep clang),)
COMPILER = clang
endif
ifneq ($(shell $(CC) -v 2>&1 | grep oneAPI | grep Compiler),)
COMPILER = icx
endif
ifeq ($(shell echo | $(CC) -E -Wp,-v - 2>&1 | grep musl  > /dev/null && echo 1),1)
COMPILER = musl-gcc
override CFLAGS += -DHAVE_CC_MUSL_GCC
endif
ifneq ($(shell $(CC) -v 2>&1 | grep scan-build),)
COMPILER = scan-build
override CC := $(CC) clang
endif

#
# check for ALT linux gcc, define HAVE_ALT_LINUX_GCC, see core-shim.c
# https://github.com/ColinIanKing/stress-ng/issues/452
#
ifneq ($(shell $(CC) -v 2>&1 | grep Target | grep "alt-linux"),)
override CFLAGS += -DHAVE_ALT_LINUX_GCC
endif

KERNEL=$(shell uname -s)
NODENAME=$(shell uname -n)

override CFLAGS += -Wall -Wextra -DVERSION='"$(VERSION)"'

#
#  Building stress-vnni with less than -O2 causes breakage with
#  gcc-13.2, so remove then and ensure at least -O2 is used or
#  honour flags > -O2 if they are provided
#
VNNI_OFLAGS_REMOVE=-O0 -O1 -Os -Oz -Og
VNNI_CFLAGS += $(filter-out $(VNNI_OFLAGS_REMOVE),$(CFLAGS))

#
# Default -O2 if optimization level not defined
#
ifeq "$(findstring -O,$(CFLAGS))" ""
	override CFLAGS += -O2
endif
ifeq "$(findstring -O,$(VNNI_CFLAGS))" ""
	override VNNI_CFLAGS += -O2
endif

#
# Debug flag
#
ifeq ($(DEBUG),1)
override CFLAGS += -g
endif

#
# Check if compiler supports flag set in $(flag)
#
cc_supports_flag = $(shell $(CC) -Werror $(flag) -E -xc /dev/null > /dev/null 2>&1 && echo $(flag))

#
# Pedantic flags
#
ifeq ($(PEDANTIC),1)
ifeq ($(COMPILER),icc)
PEDANTIC_FLAGS := \
	-Wcast-qual -Wfloat-equal -Wmissing-declarations \
	-Wno-long-long -Wshadow -Wno-missing-field-initializers \
	-Wno-missing-braces -Wno-sign-compare -Wno-multichar \
	-DHAVE_PEDANTIC
else
PEDANTIC_FLAGS := \
	-Wcast-qual -Wfloat-equal -Wmissing-declarations \
	-Wmissing-format-attribute -Wno-long-long -Wpacked \
	-Wredundant-decls -Wshadow -Wno-missing-field-initializers \
	-Wno-missing-braces -Wno-sign-compare -Wno-multichar \
	-DHAVE_PEDANTIC
endif
override CFLAGS += $(foreach flag,$(PEDANTIC_FLAGS),$(cc_supports_flag))
endif

#
# Sanitize flags
#
ifeq ($(SANITIZE),1)
SANITIZE_FLAGS := \
	-fsanitize=null -fsanitize=bounds-strict -fsanitize=bounds \
	-fsanitize=object-size -fsanitize=pointer-overflow -fsanitize=builtin \
	-fsanitize=alignment -fsanitize=object-size
override CFLAGS += $(foreach flag,$(SANITIZE_FLAGS),$(cc_supports_flag))
endif

#
# Test for hardening flags and apply them if applicable
#
MACHINE := $(shell make -f Makefile.machine)
ifneq ($(PRESERVE_CFLAGS),1)
ifneq ($(MACHINE),$(filter $(MACHINE),alpha hppa ia64))
flag = -Wformat -fstack-protector-strong -Werror=format-security
#
# add -D_FORTIFY_SOURCE=2 if _FORTIFY_SOURCE is not already defined
#
ifeq ($(shell echo _FORTIFY_SOURCE | $(CC) $(CFLAGS) -E -xc - | tail -1),_FORTIFY_SOURCE)
flag += -D_FORTIFY_SOURCE=2
endif
override CFLAGS += $(cc_supports_flag)
endif
endif

#
# Optimization flags
#
ifneq ($(filter-out clang icc scan-build,$(COMPILER)),)
override CFLAGS += $(foreach flag,-fipa-pta -fivopts,$(cc_supports_flag))
override CFLAGS += $(foreach flag,-ftree-vectorize -ftree-slp-vectorize,$(cc_supports_flag))
ifneq ($(MACHINE),$(filter $(MACHINE),ibms390 s390))
override CFLAGS += $(foreach flag,-fmodulo-sched,$(cc_supports_flag))
endif
endif

#
# Relax compxchg flags
#
ifneq ($(filter-out clang icc pcc scan-build,$(COMPILER)),)
override CFLAGS += $(foreach flag,-mrelax-cmpxchg-loop,$(cc_supports_flag))
endif

ifeq ($(COMPILER),icx)
override CFLAGS += -vec -ax
endif


#
# Enable Link Time Optimization
#
ifeq ($(LTO),1)
override CFLAGS += $(foreach flag,-flto=auto,$(cc_supports_flag))
endif

ifeq ($(GARBAGE_COLLECT),1)
#
# Sections flags
#
SECTIONS_FLAGS := -ffunction-sections -fdata-sections -Wl,--gc-sections
override CFLAGS += $(foreach flag,$(SECTIONS_FLAGS),$(cc_supports_flag))

ifneq ($(VERBOSE),)
GC_SECTIONS_FLAGS := -Wl,--print-gc-sections
override CFLAGS += $(foreach flag,$(GC_SECTIONS_FLAGS),$(cc_supports_flag))
endif
endif

ifneq ($(SOURCE_DATE_EPOCH),)
override CFLAGS += -DHAVE_SOURCE_DATE_EPOCH=$(SOURCE_DATE_EPOCH)
endif

ifneq ($(EXTRA_BUILDINFO),)
override CFLAGS += -DHAVE_EXTRA_BUILDINFO
endif

#
# Expected build warnings
#
ifeq ($(UNEXPECTED),1)
override CFLAGS += -DCHECK_UNEXPECTED
endif

#
# Building with Coverity?
#
ifeq ($(COVERITY),1)
override CFLAGS += -DCOVERITY
endif

#
# Building in github?
#
ifneq ($(GITHUB_RUN_ATTEMPT),)
override VERBOSE=1
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
ifeq ($(findstring icc,$(COMPILER)),icc)
override CFLAGS += -no-inline-max-size -no-inline-max-total-size
override CFLAGS += -axAVX,CORE-AVX2,CORE-AVX-I,CORE-AVX512,SSE2,SSE3,SSSE3,SSE4.1,SSE4.2,SANDYBRIDGE,SKYLAKE,SKYLAKE-AVX512,TIGERLAKE,SAPPHIRERAPIDS
override CFLAGS += -ip -falign-loops -funroll-loops -ansi-alias -fma -qoverride-limits
endif
endif

#ifeq ($(findstring clang,$(COMPILER)),clang)
#override CFLAGS += -Weverything
#endif

GREP = grep
#
# SunOS requires special grep for -e support
#
ifeq ($(KERNEL),SunOS)
ifneq ($(NODENAME),dilos)
GREP = /usr/xpg4/bin/grep
endif
endif

#
# Static flags, only to be used when using GCC
#
ifeq ($(STATIC),1)
override LDFLAGS += -static -z muldefs -ffunction-sections -fdata-sections -Wl,--gc-sections
override CFLAGS += -DBUILD_STATIC
endif

#
# Finalize flags
#

override CXXFLAGS += $(CFLAGS)

override CXXFLAGS := $(CXXFLAGS)
override CPPFLAGS := $(CPPFLAGS)
override LDFLAGS := $(LDFLAGS)
override VNNI_CFLAGS := $(VNNI_CFLAGS)
override CFLAGS := $(CFLAGS) -std=gnu99

BINDIR=/usr/bin
MANDIR=/usr/share/man/man1
JOBDIR=/usr/share/stress-ng/example-jobs
BASHDIR=/usr/share/bash-completion/completions

#
# Header files
#
HEADERS = \
	core-arch.h \
	core-affinity.h \
	core-asm-arm.h \
	core-asm-generic.h \
	core-asm-loong64.h \
	core-asm-ppc64.h \
	core-asm-riscv.h \
	core-asm-s390.h \
	core-asm-sparc.h \
	core-asm-x86.h \
	core-asm-ret.h \
	core-attribute.h \
	core-bitops.h \
	core-builtin.h \
	core-capabilities.h \
	core-clocksource.h \
	core-config-check.h \
	core-cpu.h \
	core-cpu-cache.h \
	core-cpu-freq.h \
	core-cpuidle.h \
	core-filesystem.h \
	core-ftrace.h \
	core-hash.h \
	core-ignite-cpu.h \
	core-interrupts.h \
	core-io-priority.h \
	core-job.h \
	core-helper.h \
	core-killpid.h \
	core-klog.h \
	core-limit.h \
	core-lock.h \
	core-log.h \
	core-madvise.h \
	core-memory.h \
	core-mlock.h \
	core-mmap.h \
	core-mincore.h \
	core-module.h \
	core-mounts.h \
	core-mwc.h \
	core-nt-load.h \
	core-nt-store.h \
	core-net.h \
	core-numa.h \
	core-opts.h \
	core-out-of-memory.h \
	core-parse-opts.h \
	core-perf.h \
	core-pragma.h \
	core-prime.h \
	core-processes.h \
	core-pthread.h \
	core-put.h \
	core-rapl.h \
	core-resctrl.h \
	core-resources.h \
	core-sched.h \
	core-setting.h \
	core-shared-cache.h \
	core-shared-heap.h \
	core-shim.h \
	core-signal.h \
	core-smart.h \
	core-sort.h \
	core-stack.h \
	core-stressors.h \
	core-sync.h \
	core-syslog.h \
	core-target-clones.h \
	core-thermal-zone.h \
	core-thrash.h \
	core-time.h \
	core-try-open.h \
	core-vecmath.h \
	core-version.h \
	core-vmstat.h \
	stress-af-alg-defconfigs.h \
	stress-eigen-ops.h \
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
# Stress core
#
CORE_SRC = \
	core-affinity.c \
	core-arch.c \
	core-asm-ret.c \
	core-capabilities.c \
	core-cpu.c \
	core-cpu-cache.c \
	core-cpu-freq.c \
	core-cpuidle.c \
	core-clocksource.c \
	core-config-check.c \
	core-filesystem.c \
	core-hash.c \
	core-helper.c \
	core-ignite-cpu.c \
	core-interrupts.c \
	core-io-uring.c \
	core-io-priority.c \
	core-job.c \
	core-killpid.c \
	core-klog.c \
	core-limit.c \
	core-lock.c \
	core-log.c \
	core-madvise.c \
	core-memory.c \
	core-mincore.c \
	core-mlock.c \
	core-mmap.c \
	core-module.c \
	core-mounts.c \
	core-mwc.c \
	core-net.c \
	core-numa.c \
	core-opts.c \
	core-out-of-memory.c \
	core-parse-opts.c \
	core-perf.c \
	core-prime.c \
	core-processes.c \
	core-rapl.c \
	core-resctrl.c \
	core-resources.c \
	core-sched.c \
	core-setting.c \
	core-shared-cache.c \
	core-shared-heap.c \
	core-shim.c \
	core-signal.c \
	core-smart.c \
	core-sort.c \
	core-stack.c \
	core-sync.c \
	core-thermal-zone.c \
	core-time.c \
	core-thrash.c \
	core-ftrace.c \
	core-try-open.c \
	core-vmstat.c \
	stress-ng.c

#
#  Build time core source files
#
CORE_SRC_GEN = \
	core-config.c

#
#  Stressors
#
STRESS_SRC = \
	stress-access.c \
	stress-acl.c \
	stress-affinity.c \
	stress-af-alg.c \
	stress-aio.c \
	stress-aiol.c \
	stress-alarm.c \
	stress-apparmor.c \
	stress-atomic.c \
	stress-bad-altstack.c \
	stress-bad-ioctl.c \
	stress-besselmath.c \
	stress-bigheap.c \
	stress-bind-mount.c \
	stress-binderfs.c \
	stress-bitonicsort.c \
	stress-bitops.c \
	stress-branch.c \
	stress-brk.c \
	stress-bsearch.c \
	stress-bubblesort.c \
	stress-cache.c \
	stress-cachehammer.c \
	stress-cacheline.c \
	stress-cap.c \
	stress-cgroup.c \
	stress-chattr.c \
	stress-chdir.c \
	stress-chmod.c \
	stress-chown.c \
	stress-chroot.c \
	stress-chyperbolic.c \
	stress-clock.c \
	stress-clone.c \
	stress-close.c \
	stress-context.c \
	stress-copy-file.c \
	stress-cpu.c \
	stress-cpu-online.c \
	stress-cpu-sched.c \
	stress-crypt.c \
	stress-ctrig.c \
	stress-cyclic.c \
	stress-daemon.c \
	stress-dccp.c \
	stress-dekker.c \
	stress-dentry.c \
	stress-dev.c \
	stress-dev-shm.c \
	stress-dfp.c \
	stress-dir.c \
	stress-dirdeep.c \
	stress-dirmany.c \
	stress-dnotify.c \
	stress-dup.c \
	stress-dynlib.c \
	stress-easy-opcode.c \
	stress-eigen.c \
	stress-efivar.c \
	stress-enosys.c \
	stress-env.c \
	stress-epoll.c \
	stress-eventfd.c \
	stress-exec.c \
	stress-exit-group.c \
	stress-expmath.c \
	stress-factor.c \
	stress-fallocate.c \
	stress-fanotify.c \
	stress-far-branch.c \
	stress-fault.c \
	stress-fcntl.c \
	stress-fd-abuse.c \
	stress-fd-fork.c \
	stress-fd-race.c \
	stress-fibsearch.c \
	stress-fiemap.c \
	stress-fifo.c \
	stress-file-ioctl.c \
	stress-filename.c \
	stress-filerace.c \
	stress-flipflop.c \
	stress-flock.c \
	stress-flushcache.c \
	stress-fma.c \
	stress-fork.c \
	stress-forkheavy.c \
	stress-fp.c \
	stress-fp-error.c \
	stress-fpunch.c \
	stress-fractal.c \
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
	stress-hyperbolic.c \
	stress-icache.c \
	stress-icmp-flood.c \
	stress-idle-page.c \
	stress-inode-flags.c \
	stress-inotify.c \
	stress-insertionsort.c \
	stress-intmath.c \
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
	stress-led.c \
	stress-link.c \
	stress-list.c \
	stress-llc-affinity.c \
	stress-loadavg.c \
	stress-lockbus.c \
	stress-locka.c \
	stress-lockf.c \
	stress-lockmix.c \
	stress-lockofd.c \
	stress-logmath.c \
	stress-longjmp.c \
	stress-loop.c \
	stress-lsearch.c \
	stress-lsm.c \
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
	stress-metamix.c \
	stress-mincore.c \
	stress-min-nanosleep.c \
	stress-misaligned.c \
	stress-mknod.c \
	stress-mlock.c \
	stress-mlockmany.c \
	stress-mmap.c \
	stress-mmapaddr.c \
	stress-mmapcow.c \
	stress-mmapfiles.c \
	stress-mmapfixed.c \
	stress-mmapfork.c \
	stress-mmaphuge.c \
	stress-mmapmany.c \
	stress-mmaprandom.c \
	stress-mmaptorture.c \
	stress-module.c \
	stress-monte-carlo.c \
	stress-mprotect.c \
	stress-mpfr.c \
	stress-mq.c \
	stress-mremap.c \
	stress-mseal.c \
	stress-msg.c \
	stress-msync.c \
	stress-msyncmany.c \
	stress-mtx.c \
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
	stress-physmmap.c \
	stress-pidfd.c \
	stress-ping-sock.c \
	stress-pipe.c \
	stress-pipeherd.c \
	stress-pkey.c \
	stress-plugin.c \
	stress-poll.c \
	stress-powmath.c \
	stress-prctl.c \
	stress-prefetch.c \
	stress-prime.c \
	stress-prio-inv.c \
	stress-priv-instr.c \
	stress-procfs.c \
	stress-pseek.c \
	stress-pthread.c \
	stress-ptrace.c \
	stress-ptr-chase.c \
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
	stress-regex.c \
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
	stress-schedmix.c \
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
	stress-sigbus.c \
	stress-sigchld.c \
	stress-sigfd.c \
	stress-sigfpe.c \
	stress-sighup.c \
	stress-sigill.c \
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
	stress-sigurg.c \
	stress-sigvtalrm.c \
	stress-sigxcpu.c \
	stress-sigxfsz.c \
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
	stress-spinmem.c \
	stress-splice.c \
	stress-stack.c \
	stress-stackmmap.c \
	stress-statmount.c \
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
	stress-timermix.c \
	stress-time-warp.c \
	stress-tlb-shootdown.c \
	stress-tmpfs.c \
	stress-touch.c \
	stress-tree.c \
	stress-trig.c \
	stress-tsc.c \
	stress-tsearch.c \
	stress-tun.c \
	stress-udp.c \
	stress-udp-flood.c \
	stress-umask.c \
	stress-umount.c \
	stress-unlink.c \
	stress-unshare.c \
	stress-uprobe.c \
	stress-urandom.c \
	stress-userfaultfd.c \
	stress-usersyscall.c \
	stress-utime.c \
	stress-vdso.c \
	stress-veccmp.c \
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
	stress-vma.c \
	stress-vnni.c \
	stress-wait.c \
	stress-waitcpu.c \
	stress-watchdog.c \
	stress-wcs.c \
	stress-workload.c \
	stress-x86cpuid.c \
	stress-x86syscall.c \
	stress-xattr.c \
	stress-yield.c \
	stress-zero.c \
	stress-zlib.c \
	stress-zombie.c \

SRC = $(CORE_SRC) $(CORE_SRC_GEN) $(STRESS_SRC)
OBJS = apparmor-data.o
OBJS += stress-eigen-ops.o
OBJS += $(SRC:.c=.o)

APPARMOR_PARSER=/sbin/apparmor_parser

all: config.h stress-ng

.SUFFIXES: .cpp .c .o

.o: Makefile

%.o: %.c $(HEADERS) $(HEADERS_GEN)
	$(PRE_Q)echo "CC $<"
	$(PRE_V)$(CC) $(CFLAGS) -DHAVE_CFLAGS='"$(CFLAGS)"' -DHAVE_LDFLAGS='"$(LDFLAGS)"' -DHAVE_CXXFLAGS='"$(CXXFLAGS)"' -c -o $@ $<

stress-vnni.o: stress-vnni.c $(HEADERS) $(HEADERS_GEN)
	$(PRE_Q)echo "CC $<"
	$(PRE_V)$(CC) $(VNNI_CFLAGS) -c -o $@ $<

#
#  Use CC for linking if eigen is not being used, otherwise use CXX
#
stress-ng: config.h $(OBJS)
	$(PRE_Q)echo "LD $@"
	$(eval LINK_TOOL := $(shell if [ -n "$(shell grep '^#define HAVE_EIGEN' config.h)" ]; then echo $(CXX); else echo $(CC); fi))
	$(eval LDFLAGS_EXTRA := $(shell grep CONFIG_LDFLAGS config | sed 's/CONFIG_LDFLAGS +=//' | tr '\n' ' '))
	$(PRE_V)$(LINK_TOOL) $(OBJS) -lm $(LDFLAGS) $(LDFLAGS_EXTRA) $(CFLAGS) -o $@

stress-eigen-ops.o: config.h stress-eigen-ops.cpp stress-eigen-ops.c
	$(PRE_V)if grep -q '^#define HAVE_EIGEN' config.h; then \
		echo "CXX stress-eigen-ops.cpp";	\
		$(CXX) $(CXXFLAGS) -c -o stress-eigen-ops.o stress-eigen-ops.cpp; \
	else \
		echo "CC stress-eigen-ops.c";	\
		$(CC) $(CFLAGS) -c -o stress-eigen-ops.o stress-eigen-ops.c; \
	fi

config.h config:
	$(PRE_Q)echo "Generating config.."
	$(MAKE) CC="$(CC)" CXX="$(CXX)" STATIC=$(STATIC) -f Makefile.config
	$(PRE_Q)rm -f core-config.c

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

core-config.c: config.h
	$(PRE_V)echo "const char stress_config[] = " > core-config.c
	$(PRE_V)sed 's/.*/"&\\n"/' config.h >> core-config.c
	$(PRE_V)echo ";" >> core-config.c

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

$(OBJS): stress-ng.h Makefile Makefile.config Makefile.machine

stress-ng.1.gz: stress-ng.1
	$(PRE_V)gzip -n -c $< > $@

.PHONY: dist
dist:
	rm -rf stress-ng-$(VERSION)
	mkdir stress-ng-$(VERSION)
	cp -rp Makefile Makefile.config Makefile.machine $(CORE_SRC) \
		$(STRESS_SRC) $(HEADERS) stress-ng.1 COPYING syscalls.txt \
		mascot README.md CITATIONS.md \
		Dockerfile README.Android test \
		presentations .github TODO core-perf-event.c \
		usr.bin.pulseaudio.eg stress-eigen-ops.c \
		stress-eigen-ops.cpp core-personality.c bash-completion \
		example-jobs .travis.yml kernel-coverage.sh \
		code-of-conduct.txt stress-ng-$(VERSION)
	tar -Jcf stress-ng-$(VERSION).tar.xz stress-ng-$(VERSION)
	rm -rf stress-ng-$(VERSION)

.PHONY: pdf
pdf:
	man -t ./stress-ng.1 | ps2pdf - > stress-ng.pdf

.PHONY: cleanconfig
cleanconfig:
	$(PRE_V)rm -f config config.h core-config.c
	$(PRE_V)rm -rf configs

.PHONY: cleanobj
cleanobj:
	$(PRE_V)rm -f core-config.c
	$(PRE_V)rm -f io-uring.h
	$(PRE_V)rm -f git-commit-id.h
	$(PRE_V)rm -f core-perf-event.h
	$(PRE_V)rm -f personality.h
	$(PRE_V)rm -f apparmor-data.bin
	$(PRE_V)rm -f *.o

.PHONY: clean
clean: cleanconfig cleanobj
	$(PRE_V)rm -f stress-ng $(OBJS) stress-ng.1.gz stress-ng.pdf
	$(PRE_V)rm -f stress-ng-$(VERSION).tar.xz
	$(PRE_V)rm -f tags

.PHONY: fast-test-all
fast-test-all: all
	STRESS_NG=./stress-ng debian/tests/fast-test-all

.PHONY: lite-test
lite-test: all
	STRESS_NG=./stress-ng debian/tests/lite-test

.PHONY: slow-test-all
slow-test-all: all
	./stress-ng --seq 0 -t 15 --pathological --times --tz --metrics --klog-check --progress --cache-enable-all -x smi || true

.PHONY: verify-test-all
verify-test-all: all
	./stress-ng --seq 0 -t 5 --pathological --times --tz --metrics --verify --progress --cache-enable-all -x smi || true

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
