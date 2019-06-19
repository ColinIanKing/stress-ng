#!/bin/bash
#
# Copyright (C) 2016-2019 Canonical
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
DURATION=30

do_stress()
{
	ARGS="-t $DURATION --pathological"
	echo running $* $ARGS
	$STRESS_NG $* $ARGS
	sudo $STRESS_NG $* $ARGS
}

sudo lcov --zerocounters

DURATION=10
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

do_stress --hdd 0 --hdd-opts direct,utimes
do_stress --hdd 0 --hdd-opts dsync
do_stress --hdd 0 --hdd-opts iovec
do_stress --hdd 0 --hdd-opts fsync
do_stress --hdd 0 --hdd-opts fdatasync
do_stress --hdd 0 --hdd-opts rd-rnd,wr-rnd,fadv-rnd
do_stress --hdd 0 --hdd-opts rd-seq,wr-seq

do_stress --itimer 0 --itimer-rand

do_stress --lockf 0 --lockf-nonblock

do_stress --mincore 0 --mincore-random

do_stress --mmap 0 --mmap-file
do_stress --mmap 0 --mmap-mprotect
do_stress --mmap 0 --mmap-async

do_stress --sctp 0 --sctp-domain ipv4
do_stress --sctp 0 --sctp-domain ipv6

do_stress --seek 0 --seek-punch

do_stress --sock 0 --sock-nodelay
do_stress --sock 0 --sock-domain ipv4
do_stress --sock 0 --sock-domain ipv6
do_stress --sock 0 --sock-domain unix
do_stress --sock 0 --sock-type stream
do_stress --sock 0 --sock-type seqpacket

do_stress --stream 0 --stream-madvise hugepage
do_stress --stream 0 --stream-madvise nohugepage
do_stress --stream 0 --stream-madvise normal

do_stress --timer 0 --timer-rand
do_stress --timerfd 0 --timerfd-rand

do_stress --tmpfs 0 --tmpfs-mmap-async
do_stress --tmpfs 0 --tmpfs-mmap-file

do_stress --udp 0 --udp-domain ipv4
do_stress --udp 0 --udp-domain ipv6
do_stress --udp 0 --udp-domain unix
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
do_stress --vm 0 --vm-madvise willneed

DURATION=60
 
for S in $STRESSORS
do
	do_stress --${S} 0
done
sudo lcov -c -o kernel.info
sudo genhtml -o html kernel.info
