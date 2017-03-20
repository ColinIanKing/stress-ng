#
# Copyright (C) 2013-2017 Canonical, Ltd.
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

VERSION=0.07.26
#
# Codename "systematic stress inducer"
#

CFLAGS += -Wall -Wextra -DVERSION='"$(VERSION)"' -O2 -std=gnu99

#
# Pedantic flags
#
PEDANTIC=1
ifeq ($(PEDANTIC),1)
CFLAGS += -Wabi -Wcast-qual -Wfloat-equal -Wmissing-declarations \
	-Wmissing-format-attribute -Wno-long-long -Wpacked \
	-Wredundant-decls -Wshadow -Wno-missing-field-initializers \
	-Wno-missing-braces -Wno-sign-compare -Wno-multichar
endif

#
# Static flags, only to be used when using GCC
#
ifeq ($(STATIC),1)
LDFLAGS += -static
endif

BINDIR=/usr/bin
MANDIR=/usr/share/man/man1

#
#  Stressors
#
STRESS_SRC = \
	stress-affinity.c \
	stress-af-alg.c \
	stress-aio.c \
	stress-aio-linux.c \
	stress-apparmor.c \
	stress-atomic.c \
	stress-bigheap.c \
	stress-bind-mount.c \
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
	stress-context.c \
	stress-copy-file.c \
	stress-cpu.c \
	stress-cpu-online.c \
	stress-crypt.c \
	stress-daemon.c \
	stress-dccp.c \
	stress-dentry.c \
	stress-dir.c \
	stress-dirdeep.c \
	stress-dnotify.c \
	stress-dup.c \
	stress-epoll.c \
	stress-eventfd.c \
	stress-exec.c \
	stress-fallocate.c \
	stress-fault.c \
	stress-fcntl.c \
	stress-fiemap.c \
	stress-fifo.c \
	stress-filename.c \
	stress-flock.c \
	stress-fanotify.c \
	stress-fork.c \
	stress-fp-error.c \
	stress-fstat.c \
	stress-full.c \
	stress-futex.c \
	stress-get.c \
	stress-getrandom.c \
	stress-getdent.c \
	stress-handle.c \
	stress-hdd.c \
	stress-heapsort.c \
	stress-hsearch.c \
	stress-icache.c \
	stress-icmp-flood.c \
	stress-inotify.c \
	stress-iomix.c \
	stress-ioprio.c \
	stress-iosync.c \
	stress-itimer.c \
	stress-kcmp.c \
	stress-key.c \
	stress-kill.c \
	stress-klog.c \
	stress-lease.c \
	stress-lsearch.c \
	stress-link.c \
	stress-lockbus.c \
	stress-locka.c \
	stress-lockf.c \
	stress-lockofd.c \
	stress-longjmp.c \
	stress-madvise.c \
	stress-malloc.c \
	stress-matrix.c \
	stress-membarrier.c \
	stress-memcpy.c \
	stress-memfd.c \
	stress-mergesort.c \
	stress-mincore.c \
	stress-mknod.c \
	stress-mlock.c \
	stress-mmap.c \
	stress-mmapfork.c \
	stress-mmapmany.c \
	stress-mremap.c \
	stress-msg.c \
	stress-msync.c \
	stress-mq.c \
	stress-netlink-proc.c \
	stress-nice.c \
	stress-nop.c \
	stress-null.c \
	stress-numa.c \
	stress-oom-pipe.c \
	stress-opcode.c \
	stress-open.c \
	stress-personality.c \
	stress-pipe.c \
	stress-poll.c \
	stress-procfs.c \
	stress-pthread.c \
	stress-ptrace.c \
	stress-pty.c \
	stress-quota.c \
	stress-qsort.c \
	stress-rdrand.c \
	stress-readahead.c \
	stress-remap-file-pages.c \
	stress-rename.c \
	stress-resources.c \
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
	stress-shm.c \
	stress-shm-sysv.c \
	stress-sigfd.c \
	stress-sigfpe.c \
	stress-sigpending.c \
	stress-sigsegv.c \
	stress-sigsuspend.c \
	stress-sigq.c \
	stress-sleep.c \
	stress-socket.c \
	stress-socket-fd.c \
	stress-socketpair.c \
	stress-spawn.c \
	stress-splice.c \
	stress-stack.c \
	stress-stackmmap.c \
	stress-str.c \
	stress-stream.c \
	stress-switch.c \
	stress-sync-file.c \
	stress-sysinfo.c \
	stress-sysfs.c \
	stress-tee.c \
	stress-timer.c \
	stress-timerfd.c \
	stress-tlb-shootdown.c \
	stress-tmpfs.c \
	stress-tsc.c \
	stress-tsearch.c \
	stress-udp.c \
	stress-udp-flood.c \
	stress-unshare.c \
	stress-urandom.c \
	stress-userfaultfd.c \
	stress-utime.c \
	stress-vecmath.c \
	stress-vforkmany.c \
	stress-vm.c \
	stress-vm-rw.c \
	stress-vm-splice.c \
	stress-wait.c \
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
	affinity.c \
	cache.c \
	helper.c \
	ignite-cpu.c \
	io-priority.c \
	limit.c \
	log.c \
	madvise.c \
	mincore.c \
	mlock.c \
	mmap.c \
	mounts.c \
	mwc.c \
	net.c \
	out-of-memory.c \
	parse-opts.c \
	perf.c \
	sched.c \
	shim.c \
	thermal-zone.c \
	time.c \
	thrash.c \
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

HAVE_NOT=HAVE_APPARMOR=0 HAVE_KEYUTILS_H=0 HAVE_XATTR_H=0 HAVE_LIB_BSD=0 \
	 HAVE_LIB_Z=0 HAVE_LIB_CRYPT=0 HAVE_LIB_RT=0 HAVE_LIB_PTHREAD=0 \
	 HAVE_FLOAT_DECIMAL=0 HAVE_SECCOMP_H=0 HAVE_LIB_AIO=0 HAVE_SYS_CAP_H=0 \
	 HAVE_VECMATH=0 HAVE_ATOMIC=0 HAVE_LIB_SCTP=0 HAVE_ASM_NOP=0

#
#  Load in current config; use 'make clean' to clear this
#
-include config

CFLAGS += $(CONFIG_CFLAGS)
LDFLAGS += $(CONFIG_LDFLAGS)
OBJS += $(CONFIG_OBJS)

HAVE_VARS := $(foreach h,$(HAVE_NOT), $(firstword $(subst =, ,$h)))

ifeq "$(MAKECMDGOALS)" ""
NEED_CONFIG=1
else ifeq "$(findstring $(MAKECMDGOALS),clean dist install)" ""
NEED_CONFIG=1
endif

#
#  Determine the system build config
#
ifndef HAVE_CONFIG
ifdef NEED_CONFIG
HAVE_CONFIG=1
#
# A bit recursive, 2nd time around HAVE_APPARMOR is
# defined so we don't call ourselves over and over
#
ifeq ($(shell uname -s),SunOS)
	CONFIG_LDFLAGS += -lsocket -lnsl
endif

ifndef $(HAVE_APPARMOR)
HAVE_APPARMOR = $(shell $(MAKE) --no-print-directory $(HAVE_NOT) have_apparmor)
ifeq ($(HAVE_APPARMOR),1)
	CONFIG_OBJS += apparmor-data.o
	CONFIG_CFLAGS += -DHAVE_APPARMOR
	CONFIG_LDFLAGS += $(LIB_APPARMOR)
$(info autoconfig: using $(LIB_APPARMOR))
endif
endif

ifndef $(HAVE_LIB_BSD)
HAVE_LIB_BSD = $(shell $(MAKE) --no-print-directory $(HAVE_NOT) have_lib_bsd)
ifeq ($(HAVE_LIB_BSD),1)
	CONFIG_CFLAGS += -DHAVE_LIB_BSD
	CONFIG_LDFLAGS += $(LIB_BSD)
$(info autoconfig: using $(LIB_BSD))
endif
endif

ifndef $(HAVE_LIB_Z)
HAVE_LIB_Z = $(shell $(MAKE) --no-print-directory $(HAVE_NOT) have_lib_z)
ifeq ($(HAVE_LIB_Z),1)
	CONFIG_CFLAGS += -DHAVE_LIB_Z
	CONFIG_LDFLAGS += $(LIB_Z)
$(info autoconfig: using $(LIB_Z))
endif
endif

ifndef $(HAVE_LIB_CRYPT)
HAVE_LIB_CRYPT = $(shell $(MAKE) --no-print-directory $(HAVE_NOT) have_lib_crypt)
ifeq ($(HAVE_LIB_CRYPT),1)
	CONFIG_CFLAGS += -DHAVE_LIB_CRYPT
	CONFIG_LDFLAGS += $(LIB_CRYPT)
$(info autoconfig: using $(LIB_CRYPT))
endif
endif

ifndef $(HAVE_LIB_RT)
HAVE_LIB_RT = $(shell $(MAKE) --no-print-directory $(HAVE_NOT) have_lib_rt)
ifeq ($(HAVE_LIB_RT),1)
	CONFIG_CFLAGS += -DHAVE_LIB_RT
	CONFIG_LDFLAGS += $(LIB_RT)
$(info autoconfig: using $(LIB_RT))
endif
endif

ifndef $(HAVE_LIB_PTHREAD)
HAVE_LIB_PTHREAD = $(shell $(MAKE) --no-print-directory $(HAVE_NOT) have_lib_pthread)
ifeq ($(HAVE_LIB_PTHREAD),1)
	CONFIG_CFLAGS += -DHAVE_LIB_PTHREAD
	CONFIG_LDFLAGS += $(LIB_PTHREAD)
$(info autoconfig: using $(LIB_PTHREAD))
endif
endif

ifndef $(HAVE_LIB_SCTP)
HAVE_LIB_SCTP = $(shell $(MAKE) --no-print-directory $(HAVE_NOT) have_lib_sctp)
ifeq ($(HAVE_LIB_SCTP),1)
	CONFIG_CFLAGS += -DHAVE_LIB_SCTP
	CONFIG_LDFLAGS += $(LIB_SCTP)
$(info autoconfig: using $(LIB_SCTP))
endif
endif

ifndef $(HAVE_LIB_AIO)
HAVE_LIB_AIO = $(shell $(MAKE) --no-print-directory $(HAVE_NOT) have_lib_aio)
ifeq ($(HAVE_LIB_AIO),1)
	CONFIG_CFLAGS += -DHAVE_LIB_AIO
	CONFIG_LDFLAGS += $(LIB_AIO)
$(info autoconfig: using $(LIB_AIO))
endif
endif

ifndef $(HAVE_KEYUTILS_H)
HAVE_KEYUTILS_H = $(shell $(MAKE) --no-print-directory $(HAVE_NOT) have_keyutils_h)
ifeq ($(HAVE_KEYUTILS_H),1)
	CONFIG_CFLAGS += -DHAVE_KEYUTILS_H
$(info autoconfig: using keyutils.h)
endif
endif

ifndef $(HAVE_XATTR_H)
HAVE_XATTR_H = $(shell $(MAKE) --no-print-directory $(HAVE_NOT) have_xattr_h)
ifeq ($(HAVE_XATTR_H),1)
	CONFIG_CFLAGS += -DHAVE_XATTR_H
$(info autoconfig: using attr/xattr.h)
endif
endif

ifndef $(HAVE_SECCOMP_H)
HAVE_SECCOMP_H = $(shell $(MAKE) --no-print-directory $(HAVE_NOT) have_seccomp_h)
ifeq ($(HAVE_SECCOMP_H),1)
	CONFIG_CFLAGS += -DHAVE_SECCOMP_H
$(info autoconfig: using linux/seccomp.h)
endif
endif

ifndef $(HAVE_SYS_CAP_H)
HAVE_SYS_CAP_H = $(shell $(MAKE) --no-print-directory $(HAVE_NOT) have_sys_cap_h)
ifeq ($(HAVE_SYS_CAP_H),1)
	CONFIG_CFLAGS += -DHAVE_SYS_CAP_H
$(info autoconfig: using sys/capability.h)
endif
endif

ifndef $(HAVE_FLOAT_DECIMAL)
HAVE_FLOAT_DECIMAL = $(shell $(MAKE) --no-print-directory $(HAVE_NOT) have_float_decimal)
ifeq ($(HAVE_FLOAT_DECIMAL),1)
	CONFIG_CFLAGS += -DHAVE_FLOAT_DECIMAL
$(info autoconfig: using float decimal support)
endif
endif

ifndef $(HAVE_VECMATH)
HAVE_VECMATH = $(shell $(MAKE) --no-print-directory $(HAVE_NOT) have_vecmath)
ifeq ($(HAVE_VECMATH),1)
	CONFIG_CFLAGS += -DHAVE_VECMATH
$(info autoconfig: using vector math support)
endif
endif

ifndef $(HAVE_ATOMIC)
HAVE_ATOMIC = $(shell $(MAKE) --no-print-directory $(HAVE_NOT) have_atomic)
ifeq ($(HAVE_ATOMIC),1)
	CONFIG_CFLAGS += -DHAVE_ATOMIC
$(info autoconfig: using atomic support)
endif
endif

ifndef $(HAVE_ASM_NOP)
HAVE_ASM_NOP = $(shell $(MAKE) --no-print-directory $(HAVE_NOT) have_asm_nop)
ifeq ($(HAVE_ASM_NOP),1)
	CONFIG_CFLAGS += -DHAVE_ASM_NOP
$(info autoconfig: using nop assembler instruction)
endif
endif

endif
endif


.SUFFIXES: .c .o

.o: stress-ng.h Makefile

.c.o: stress-ng.h Makefile $(SRC)
	@echo $(CC) $(CFLAGS) -c -o $@ $<
	@$(CC) $(CFLAGS) -c -o $@ $<

stress-ng: save_config $(OBJS)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(OBJS) -lm $(LDFLAGS) -lc -o $@

#
#  save configuration
#
.PHONY: save_config
save_config:
	@echo $(foreach h,$(HAVE_VARS),$h=$($(h))) | tr ' ' '\n' > config
	@echo CONFIG_CFLAGS=$(CONFIG_CFLAGS) >> config
	@echo CONFIG_LDFLAGS=$(CONFIG_LDFLAGS) >> config
	@echo CONFIG_OBJS=$(CONFIG_OBJS) >> config
	@echo HAVE_CONFIG=1 >> config

#
#  check if we can build against AppArmor
#
.PHONY: have_apparmor
have_apparmor:
	@$(CC) $(CPPFLAGS) test-apparmor.c $(LIB_APPARMOR) -o test-apparmor 2> /dev/null || true
	@if [ -f test-apparmor ]; then \
		if [ -x "$(APPARMOR_PARSER)" ]; then \
			echo 1 ;\
		else \
			echo 0 ;\
		fi \
	else \
		echo 0 ;\
	fi
	@rm -f test-apparmor

#
#  check if we have keyutils.h
#
.PHONY: have_keyutils_h
have_keyutils_h:
	@echo "#include <sys/types.h>" > test-key.c
	@echo "#include <keyutils.h>" >> test-key.c
	@$(CC) $(CPPFLAGS) -c -o test-key.o test-key.c 2> /dev/null || true
	@if [ -f test-key.o ]; then \
		echo 1 ;\
	else \
		echo 0 ;\
	fi
	@rm -f test-key.c test-key.o

#
#  check if we have xattr.h
#
.PHONY: have_xattr_h
have_xattr_h:
	@echo "#include <sys/types.h>" > test-xattr.c
	@echo "#include <attr/xattr.h>" >> test-xattr.c
	@$(CC) $(CPPFLAGS) -c -o test-xattr.o test-xattr.c 2> /dev/null || true
	@if [ -f test-xattr.o ]; then \
		echo 1 ;\
	else \
		echo 0 ;\
	fi
	@rm -f test-xattr.c test-xattr.o

#
#  check if we can build against libbsd
#
.PHONY: have_lib_bsd
have_lib_bsd:
	@$(CC) $(CPPFLAGS) test-libbsd.c $(LIB_BSD) -o test-libbsd 2> /dev/null || true
	@if [ -f test-libbsd ]; then \
		echo 1 ;\
	else \
		echo 0 ;\
	fi
	@rm -f test-libbsd

#
#  check if we can build against libz
#
.PHONY: have_lib_z
have_lib_z:
	@$(CC) $(CPPFLAGS) test-libz.c $(LIB_Z) -o test-libz 2> /dev/null || true
	@if [ -f test-libz ]; then \
		echo 1 ;\
	else \
		echo 0 ;\
	fi
	@rm -f test-libz

#
#  check if we can build against libcrypt
#
.PHONY: have_lib_crypt
have_lib_crypt:
	@$(CC) $(CPPFLAGS) test-libcrypt.c $(LIB_CRYPT) -o test-libcrypt 2> /dev/null || true
	@if [ -f test-libcrypt ]; then \
		echo 1 ;\
	else \
		echo 0 ;\
	fi
	@rm -f test-libcrypt

#
#  check if we can build against librt
#
.PHONY: have_lib_rt
have_lib_rt:
	@$(CC) $(CPPFLAGS) test-librt.c $(LIB_RT) -o test-librt 2> /dev/null || true
	@if [ -f test-librt ]; then \
		echo 1 ;\
	else \
		echo 0 ;\
	fi
	@rm -f test-librt

#
#  check if we can build against libpthread
#
.PHONY: have_lib_pthread
have_lib_pthread:
	@$(CC) $(CPPFLAGS) test-libpthread.c $(LIB_PTHREAD) -o test-libpthread 2> /dev/null || true
	@if [ -f test-libpthread ]; then \
		echo 1 ;\
	else \
		echo 0 ;\
	fi
	@rm -f test-libpthread

#
#  check if we can build against libsctp
#
.PHONY: have_lib_sctp
have_lib_sctp:
	@$(CC) $(CPPFLAGS) test-libsctp.c $(LIB_SCTP) -o test-libsctp 2> /dev/null || true
	@if [ -f test-libsctp ]; then \
		echo 1 ;\
	else \
		echo 0 ;\
	fi
	@rm -f test-libsctp


#
#  check if compiler supports floating point decimal format
#
.PHONY: have_float_decimal
have_float_decimal:
	@echo "_Decimal32 x;" > test-decimal.c
	@$(CC) $(CPPFLAGS) -c -o test-decimal.o test-decimal.c 2> /dev/null || true
	@if [ -f test-decimal.o ]; then \
		echo 1 ;\
	else \
		echo 0 ;\
	fi
	@rm -f test-decimal.c test-decimal.o

#
#  check if we have seccomp.h
#
.PHONY: have_seccomp_h
have_seccomp_h:
	@echo "#include <linux/seccomp.h>" > test-seccomp.c
	@$(CC) $(CPPFLAGS) -c -o test-seccomp.o test-seccomp.c 2> /dev/null || true
	@if [ -f test-seccomp.o ]; then \
		echo 1 ;\
	else \
		echo 0 ;\
	fi
	@rm -f test-seccomp.c test-seccomp.o

#
#  check if we can build against libaio
#
.PHONY: have_lib_aio
have_lib_aio:
	@$(CC) $(CPPFLAGS) test-libaio.c $(LIB_AIO) -o test-libaio 2> /dev/null || true
	@if [ -f test-libaio ]; then \
		echo 1 ;\
	else \
		echo 0 ;\
	fi
	@rm -f test-libaio

#
#  check if we can use assembler nop instruction
#
.PHONY: have_asm_nop
have_asm_nop:
	@$(CC) $(CPPFLAGS) test-asm-nop.c -o test-asm-nop 2> /dev/null || true
	@if [ -f test-asm-nop ]; then \
		echo 1 ;\
	else \
		echo 0 ;\
	fi
	@rm -f test-asm-nop


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
	$(CC) -c apparmor-data.c -o apparmor-data.o
	@rm -rf apparmor-data.c

#
#  check if we have sys/capability.h
#
have_sys_cap_h:
	@$(CC) $(CPPFLAGS) test-cap.c -o test-cap 2> /dev/null || true
	@if [ -f test-cap ]; then \
		echo 1 ;\
	else \
		echo 0 ;\
	fi
	@rm -f test-cap

#
#  check if we can build vecmath related code
#
have_vecmath: stress-vecmath.c
	@$(CC) $(CPPFLAGS) -DHAVE_VECMATH -c -o stress-vecmath-test.o stress-vecmath.c 2> /dev/null || true
	@if [ -f stress-vecmath-test.o ]; then \
		echo 1 ;\
	else \
		echo 0 ;\
	fi
	@rm -rf stress-vecmath-test.o

#
#  check if we can build atomic related code
#
have_atomic: stress-atomic.c
	@$(CC) $(CPPFLAGS) -DTEST_ATOMIC_BUILD -DHAVE_ATOMIC stress-atomic.c -o stress-atomic-test 2> /dev/null || true
	@if [ -f stress-atomic-test ]; then \
		echo 1 ;\
	else \
		echo 0 ;\
	fi
	@rm -rf stress-atomic-test

#
#  extract the PER_* personality enums
#
personality.h:
	@$(CPP) personality.c | grep -e "PER_[A-Z0-9]* =.*," | cut -d "=" -f 1 \
	| sed "s/.$$/,/" > personality.h

stress-personality.c: personality.h

stress-cpu.o: stress-cpu.c
	@echo $(CC) $(CFLAGS) -c -o $@ $<
	@echo "_Decimal32 x;" > test-decimal.c
	@$(CC) $(CPPFLAGS) -c -o test-decimal.o test-decimal.c 2> /dev/null || true
	@if [ -f test-decimal.o ]; then \
		$(CC) $(CFLAGS) -DSTRESS_FLOAT_DECIMAL -c -o $@ $< ;\
	else \
		$(CC) $(CFLAGS) -c -o $@ $< ;\
	fi
	@rm -f test-decimal.c test-decimal.o

perf.o: perf.c perf-event.c
	@gcc -E perf-event.c | grep "PERF_COUNT" | sed 's/,/ /' | awk {'print "#define _SNG_" $$1 " (1)"'} > perf-event.h
	$(CC) $(CFLAGS) -c -o $@ $<

stress-wcstr.o: stress-wcstr.c
	@echo $(CC) $(CFLAGS) -fno-builtin -c -o $@ $<
	@$(CC) $(CFLAGS) -fno-builtin -c -o $@ $<

stress-vecmath.o: stress-vecmath.c
	@echo $(CC) $(CFLAGS) -fno-builtin -c -o $@ $<
	@$(CC) $(CFLAGS) -fno-builtin -c -o $@ $<
	@touch stress-ng.c

$(OBJS): stress-ng.h Makefile

stress-ng.1.gz: stress-ng.1
	gzip -c $< > $@

.PHONY: dist
dist:
	rm -rf stress-ng-$(VERSION)
	mkdir stress-ng-$(VERSION)
	cp -rp Makefile $(SRC) stress-ng.h stress-ng.1 personality.c \
		COPYING syscalls.txt mascot README README.Android \
		test-apparmor.c test-libbsd.c test-libz.c \
		test-libcrypt.c test-librt.c test-libpthread.c \
		test-libaio.c test-cap.c test-libsctp.c \
		test-asm-nop.c usr.bin.pulseaudio.eg perf-event.c \
		snapcraft smatchify.sh config stress-ng-$(VERSION)
	tar -zcf stress-ng-$(VERSION).tar.gz stress-ng-$(VERSION)
	rm -rf stress-ng-$(VERSION)

.PHONY: pdf
pdf:
	man -t ./stress-ng.1 | ps2pdf - > stress-ng.pdf


.PHONY: clean
clean:
	@rm -f stress-ng $(OBJS) stress-ng.1.gz stress-ng.pdf
	@rm -f stress-ng-$(VERSION).tar.gz
	@rm -f test-decimal.c
	@rm -f personality.h
	@rm -f perf-event.h
	@rm -f *.o
	@:> config

.PHONY: fast-test-all
fast-test-all: stress-ng
	STRESS_NG=./stress-ng debian/tests/fast-test-all

.PHONY: install
install: stress-ng stress-ng.1.gz
	mkdir -p ${DESTDIR}${BINDIR}
	cp stress-ng ${DESTDIR}${BINDIR}
	mkdir -p ${DESTDIR}${MANDIR}
	cp stress-ng.1.gz ${DESTDIR}${MANDIR}
