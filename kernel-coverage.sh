#!/bin/bash
#
# Copyright (C) 2016-2020 Canonical
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

PERF_PARANOID=/proc/sys/kernel/perf_event_paranoid
LOG=stress-ng-$(date '+%Y%m%d-%H%M').log
echo "Logging to $LOG"

rm -f $LOG

#
# stress-ng kernel coverage test:
#  - requires lcov to be installed
#  - requires a gcov kernel with the following configuration:
#      CONFIG_DEBUG_FS=y
#      CONFIG_GCOV_KERNEL=y
#      CONFIG_GCOV_PROFILE_ALL=y
#  - for ease of use, ensure kernel is built on the target machine
#  - no support for this script, if it breaks, you get the pieces
#
if [ -z "$STRESS_NG" ]; then
        STRESS_NG=./stress-ng
fi

if [ ! -x "$STRESS_NG" ]; then
	echo "Cannot find executable $STRESS_NG"
	exit 1
fi
STRESSORS=$($STRESS_NG --stressors)

#
#  The stressors can potentially spam the logs so
#  keep them truncated as much as possible. Hammer to
#  crack the nut.
#
clear_journal()
{
	which journalctl >& /dev/null
	if [ $? -eq 0 ]; then
		sudo journalctl --rotate >& /dev/null
		sudo journalctl --vacuum-time 1s >& /dev/null
		sudo journalctl --rotate >& /dev/null
	fi
}

do_stress()
{
	ARGS="-t $DURATION --pathological --timestamp --tz --syslog --perf --no-rand-seed --times --metrics"
	if grep -q "\-\-oom\-pipe" <<< "$*"; then
		ARGS="$ARGS --oomable"
	fi
	echo "STARTED:  $(date '+%F %X') using $* $ARGS" >> $LOG
	sync
	echo running $* $ARGS
	$STRESS_NG $* $ARGS
	sudo $STRESS_NG $* $ARGS
	echo "FINISHED: $(date '+%F %X') using $* $ARGS (return $?)" >> $LOG
	sync
	clear_journal
}

if [ -e $PERF_PARANOID ]; then
	paranoid_saved=$(cat /proc/sys/kernel/perf_event_paranoid)
	(echo 0 | sudo tee $PERF_PARANOID) > /dev/null
fi

#
#  Try to ensure that this script and parent won't be oom'd
#
if [ -e /proc/self/oom_score_adj ]; then
	echo -900 | sudo tee /proc/self/oom_score_adj >& /dev/null
	echo -900 | sudo tee /proc/$PPID/oom_score_adj >& /dev/null
elif [ -e /proc/self/oom_adj ]; then
	echo -14 | sudo tee /proc/self/oom_adj >& /dev/null
	echo -14 | sudo tee /proc/$PPID/oom_adj >& /dev/null
fi
#
# Ensure oom killer kills the stressor hogs rather
# than the wrong random process (e.g. this script)
#
if [ -e /proc/sys/vm/oom_kill_allocating_task ]; then
	echo 0 | sudo tee /proc/sys/vm/oom_kill_allocating_task >& /dev/null
fi

SWP=/tmp/swap.img
dd if=/dev/zero of=$SWP bs=1M count=1024
chmod 0600 $SWP
sudo chown root:root $SWP
sudo mkswap $SWP
sudo swapon $SWP

sudo lcov --zerocounters

if [ -f	/sys/kernel/debug/tracing/trace_stat/branch_all ]; then
	sudo cat  /sys/kernel/debug/tracing/trace_stat/branch_all > branch_all.start
fi

#
#  Exercise I/O schedulers
#
DURATION=20
scheds=$(${STRESS_NG} --sched which 2>&1 | tail -1 | cut -d':' -f2-)
for s in ${scheds}
do
	sudo ${STRESS_NG} --sched $s --cpu 0 -t 5 --timestamp --tz --syslog --perf --no-rand-seed --times --metrics
	sudo ${STRESS_NG} --sched $s --cpu 0 -t 5 --sched-reclaim --timestamp --tz --syslog --perf --no-rand-seed --times --metrics
done

#
#  Exercise ionice classes
#
ionices=$(${STRESS_NG} --ionice-class which 2>&1 | tail -1 | cut -d':' -f2-)
for i in ${innices}
do
	do_stress --ionice-class $i --iomix 0 -t 20
done

#
#  Exercise all stressors, limit to 1 CPU for ones that
#  can spawn way too many processes
#
DURATION=15
for S in $STRESSORS
do
	case $S in
		clone|fork|vfork)
			do_stress --${S} 1
			;;
		*)
			do_stress --${S} 8
			;;
	esac
done

DURATION=60
do_stress --all 1

#
#  Exercise various stressor options
#
do_stress --brk 0 --brk-notouch
do_stress --brk 0 --brk-mlock

do_stress --cpu 0 --sched batch
do_stress --cpu 0 --taskset 0,2 --ignite-cpu
do_stress --cpu 0 --taskset 1,2,3
do_stress --cpu 0 --taskset 0,1,2 --thrash

do_stress --cyclic 0 --cyclic-policy deadline
do_stress --cyclic 0 --cyclic-policy fifo
do_stress --cyclic 0 --cyclic-policy rr
do_stress --cyclic 0 --cyclic-method clock_ns
do_stress --cyclic 0 --cyclic-method itimer
do_stress --cyclic 0 --cyclic-method poll
do_stress --cyclic 0 --cyclic-method posix_ns
do_stress --cyclic 0 --cyclic-method pselect
do_stress --cyclic 0 --cyclic-method usleep
do_stress --cyclic 0 --cyclic-prio 50

do_stress --dccp 0 --dccp-opts send
do_stress --dccp 0 --dccp-opts sendmsg
do_stress --dccp 0 --dccp-opts sendmmsg

do_stress --dccp 0 --dccp-domain ipv4
do_stress --dccp 0 --dccp-domain ipv6

do_stress --epoll 0 --epoll-domain ipv4
do_stress --epoll 0 --epoll-domain ipv6
do_stress --epoll 0 --epoll-domain unix

do_stress --eventfd 0 --eventfd-nonblock

do_stress --hdd 0 --hdd-opts direct,utimes
do_stress --hdd 0 --hdd-opts dsync
do_stress --hdd 0 --hdd-opts iovec
do_stress --hdd 0 --hdd-opts fsync
do_stress --hdd 0 --hdd-opts fdatasync
do_stress --hdd 0 --hdd-opts rd-rnd,wr-rnd,fadv-rnd
do_stress --hdd 0 --hdd-opts rd-seq,wr-seq
do_stress --hdd 0 --hdd-opts fadv-normal
do_stress --hdd 0 --hdd-opts fadv-noreuse
do_stress --hdd 0 --hdd-opts fadv-rnd
do_stress --hdd 0 --hdd-opts fadv-seq
do_stress --hdd 0 --hdd-opts fadv-willneed
do_stress --hdd 0 --hdd-opts fadv-dontneed

do_stress --itimer 0 --itimer-rand

do_stress --lease 0 --lease-breakers 8
do_stress --lockf 0 --lockf-nonblock

do_stress --mincore 0 --mincore-random
do_stress --open 0 --open-fd

do_stress --mmap 0 --mmap-file
do_stress --mmap 0 --mmap-mprotect
do_stress --mmap 0 --mmap-async
do_stress --mmap 0 --mmap-odirect
do_stress --mmap 0 --mmap-osync

do_stress --pipe 0 --pipe-size 64K
do_stress --pipe 0 --pipe-size 1M

do_stress --pthread 0 --pthread-max 512
do_stress --pthread 0 --pthread-max 1024

do_stress --sctp 0 --sctp-domain ipv4
do_stress --sctp 0 --sctp-domain ipv6

do_stress --seek 0 --seek-punch

do_stress --shm-sysv 0 --shm-sysv-segs 128

do_stress --sock 0 --sock-nodelay
do_stress --sock 0 --sock-domain ipv4
do_stress --sock 0 --sock-domain ipv6
do_stress --sock 0 --sock-domain unix
do_stress --sock 0 --sock-type stream
do_stress --sock 0 --sock-type seqpacket
do_stress --sock 0 --sock-opts random

do_stress --stream 0 --stream-madvise hugepage
do_stress --stream 0 --stream-madvise nohugepage
do_stress --stream 0 --stream-madvise normal

do_stress --timer 0 --timer-rand
do_stress --timer 0 --timer-freq 1000000
do_stress --timer 0 --timer-freq 100000 --timer-slack 1000

do_stress --timerfd 0 --timerfd-rand

do_stress --tmpfs 0 --tmpfs-mmap-async
do_stress --tmpfs 0 --tmpfs-mmap-file

do_stress --tun 0
do_stress --tun 0 --tun-tap

do_stress --udp 0 --udp-domain ipv4
do_stress --udp 0 --udp-domain ipv6
do_stress --udp 0 --udp-lite

do_stress --udp-flood 0 --udp-flood-domain ipv4
do_stress --udp-flood 0 --udp-flood-domain ipv6

do_stress --utime 0 --utime-fsync

do_stress --vm 0 --vm-keep
do_stress --vm 0 --vm-locked
do_stress --vm 0 --vm-populate
do_stress --vm 0 --vm-madvise dontneed
do_stress --vm 0 --vm-madvise hugepage
do_stress --vm 0 --vm-madvise mergeable
do_stress --vm 0 --vm-madvise nohugepage
do_stress --vm 0 --vm-madvise mergeable
do_stress --vm 0 --vm-madvise normal
do_stress --vm 0 --vm-madvise random
do_stress --vm 0 --vm-madvise sequential
do_stress --vm 0 --vm-madvise unmergeable
do_stress --vm 0 --vm-madvise willneed --page-in

#
#  Longer duration stress testing to get more
#  coverage because of the large range of files to
#  traverse
#
DURATION=180
do_stress --dev 32
do_stress --sysinval 0

DURATION=360
do_stress --sysfs 16
do_stress --procfs 32

DURATION=120
do_stress --bad-ioctl 0 --pathological

#
#  And exercise I/O with plenty of time for file setup
#  overhead.
#
DURATION=60
sudo $STRESS_NG --class filesystem --ftrace --seq 0 -v --timestamp --syslog -t $DURATION
sudo $STRESS_NG --class io --ftrace --seq 0 -v --timestamp --syslog -t $DURATION

if [ -f	/sys/kernel/debug/tracing/trace_stat/branch_all ]; then
	sudo cat  /sys/kernel/debug/tracing/trace_stat/branch_all > branch_all.finish
fi

sudo swapoff $SWP
sudo rm $SWP

if [ -e $PERF_PARANOID ]; then
	(echo $paranoid_saved | sudo tee $PERF_PARANOID) > /dev/null
fi

sudo lcov -c -o kernel.info >& /dev/null
sudo genhtml -o html kernel.info
