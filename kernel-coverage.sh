#!/bin/bash
#
# Copyright (C) 2016-2021 Canonical
# Copyright (C) 2022-2025 Colin Ian King
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

export PATH=$PATH:/usr/bin:/usr/sbin
PERF_PARANOID=/proc/sys/kernel/perf_event_paranoid
SWAP=/tmp/swap.img
FSIMAGE=/tmp/fs.img
MNT=/tmp/mnt
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
STRESSORS=$($STRESS_NG --stressors | sed 's/smi//')

get_stress_ng_pids()
{
	ps -e | grep stress-ng | awk '{ print $1}'
}

kill_stress_ng()
{
	for J in $(seq 10)
	do
		for I in $(seq 10)
		do
			pids=$(get_stress_ng_pids)
			if [ -z "$pids" ]; then
				return
			fi
			kill -ALRM $pids >& /dev/null
			sleep 1
		done

		for I in $(seq 10)
		do
			pids=$(get_stress_ng_pids)
			if [ -z "$pids" ]; then
				return
			fi

			kill -KILL $pids >& /dev/null
			sleep 1
		done
	done
}

mount_filesystem()
{
	rm -f ${FSIMAGE}
	COUNT=4000
	case $1 in
		ext2)	MKFS_CMD="mkfs.ext2"
			MKFS_ARGS="-F ${FSIMAGE}"
			MNT_CMD="sudo mount -o loop ${FSIMAGE} ${MNT}"
			dd if=/dev/zero of=${FSIMAGE} bs=1M count=${COUNT}
			;;
		ext3)	MKFS_CMD="mkfs.ext3"
			MKFS_ARGS="-F ${FSIMAGE}"
			MNT_CMD="sudo mount -o loop ${FSIMAGE} ${MNT}"
			dd if=/dev/zero of=${FSIMAGE} bs=1M count=${COUNT}
			;;
		ext4)	MKFS_CMD="mkfs.ext4"
			MKFS_ARGS="-F ${FSIMAGE} -O inline_data,dir_index,metadata_csum,64bit,ea_inode,ext_attr,quota,verity,extent,filetype,huge_file,mmp"
			MNT_CMD="sudo mount -o journal_checksum,loop ${FSIMAGE} ${MNT}"
			dd if=/dev/zero of=${FSIMAGE} bs=1M count=${COUNT}
			;;
		xfs)
			MKFS_CMD="mkfs.xfs"
			MKFS_ARGS="-m crc=1,bigtime=1,finobt=1,rmapbt=1 -f ${FSIMAGE}"
			MNT_CMD="sudo mount -o loop ${FSIMAGE} ${MNT}"
			dd if=/dev/zero of=${FSIMAGE} bs=1M count=${COUNT}
			;;
		hfs)
			MKFS_CMD="mkfs.hfs"
			MKFS_ARGS="${FSIMAGE}"
			MNT_CMD="sudo mount -o loop -o rw ${FSIMAGE} ${MNT}"
			dd if=/dev/zero of=${FSIMAGE} bs=1M count=${COUNT}
			;;
		hfsplus)
			MKFS_CMD="mkfs.hfsplus"
			MKFS_ARGS="-s ${FSIMAGE}"
			MNT_CMD="sudo mount -o loop -o rw ${FSIMAGE} ${MNT}"
			dd if=/dev/zero of=${FSIMAGE} bs=1M count=${COUNT}
			;;
		jfs)	MKFS_CMD="mkfs.jfs"
			MKFS_ARGS="-q ${FSIMAGE}"
			MNT_CMD="sudo mount -o loop ${FSIMAGE} ${MNT}"
			dd if=/dev/zero of=${FSIMAGE} bs=1M count=${COUNT}
			;;
		minix1)	MKFS_CMD="mkfs.minix"
			MKFS_ARGS="-1 ${FSIMAGE}"
			MNT_CMD="sudo mount -o loop ${FSIMAGE} ${MNT}"
			dd if=/dev/zero of=${FSIMAGE} bs=1M count=${COUNT}
			;;
		minix2)	MKFS_CMD="mkfs.minix"
			MKFS_ARGS="-2 ${FSIMAGE}"
			MNT_CMD="sudo mount -o loop ${FSIMAGE} ${MNT}"
			dd if=/dev/zero of=${FSIMAGE} bs=1M count=${COUNT}
			;;
		minix3)	MKFS_CMD="mkfs.minix"
			MKFS_ARGS="-3 ${FSIMAGE}"
			MNT_CMD="sudo mount -o loop ${FSIMAGE} ${MNT}"
			dd if=/dev/zero of=${FSIMAGE} bs=1M count=${COUNT}
			;;
		nilfs)	MKFS_CMD="mkfs.nilfs2"
			MKFS_ARGS="-f -b 1024 -O block_count ${FSIMAGE}"
			MNT_CMD="sudo mount -o loop ${FSIMAGE} ${MNT}"
			dd if=/dev/zero of=${FSIMAGE} bs=1M count=${COUNT}
			;;
		fat)	MKFS_CMD="mkfs.fat"
			MKFS_ARGS="-S 1024 ${FSIMAGE}"
			MNT_CMD="sudo mount -o loop ${FSIMAGE} ${MNT}"
			dd if=/dev/zero of=${FSIMAGE} bs=1M count=${COUNT}
			;;
		vfat)	MKFS_CMD="mkfs.vfat"
			MKFS_ARGS="-S 1024 ${FSIMAGE}"
			MNT_CMD="sudo mount -o loop ${FSIMAGE} ${MNT}"
			dd if=/dev/zero of=${FSIMAGE} bs=1M count=${COUNT}
			;;
		exfat)	MKFS_CMD="mkfs.exfat"
			MKFS_ARGS="${FSIMAGE}"
			MNT_CMD="sudo mount -o loop ${FSIMAGE} ${MNT}"
			dd if=/dev/zero of=${FSIMAGE} bs=1M count=${COUNT}
			;;
		ubifs)	sudo modprobe nandsim first_id_byte=0x20 \
			second_id_byte=0xaa third_id_byte=0x00 \
			fourth_id_byte=0x15
			sudo modprobe ubi mtd=0
			sleep 5
			MKFS_CMD="ubimkvol"
			MKFS_ARGS="/dev/ubi0 -N ubifs-vol -s ${COUNT}MiB"
			MNT_CMD="sudo mount -t ubifs /dev/ubi0_0 ${MNT}"
			;;
		udf)	MKFS_CMD="mkfs.udf"
			MKFS_ARGS="-b 1024 -md hd ${FSIMAGE}"
			MNT_CMD="sudo mount -o loop ${FSIMAGE} ${MNT}"
			dd if=/dev/zero of=${FSIMAGE} bs=1M count=${COUNT}
			;;
		ntfs)	MKFS_CMD="mkfs.ntfs"
			MKFS_ARGS="-F -C -s 1024 -v ${FSIMAGE}"
			MNT_CMD="sudo mount -o loop ${FSIMAGE} ${MNT}"
			dd if=/dev/zero of=${FSIMAGE} bs=1M count=${COUNT}
			;;
		f2fs)	MKFS_CMD="mkfs.f2fs"
			MKFS_ARGS="-f ${FSIMAGE} -i -O encrypt,extra_attr,inode_checksum,quota,verity,sb_checksum,compression,lost_found,flexible_inline_xattr,inode_crtime"
			MNT_CMD="sudo mount -o loop ${FSIMAGE} ${MNT}"
			dd if=/dev/zero of=${FSIMAGE} bs=1M count=${COUNT}
			;;
		bfs)	MKFS_CMD="mkfs.bfs"
			MKFS_ARGS="${FSIMAGE}"
			MNT_CMD="sudo mount -o loop ${FSIMAGE} ${MNT}"
			dd if=/dev/zero of=${FSIMAGE} bs=1M count=${COUNT}
			;;
		btrfs)	MKFS_CMD="mkfs.btrfs"
			MKFS_ARGS="-O extref --csum xxhash -R quota,free-space-tree -f ${FSIMAGE}"
			MNT_CMD="sudo mount -o compress,autodefrag,datasum -o loop ${FSIMAGE} ${MNT}"
			dd if=/dev/zero of=${FSIMAGE} bs=1M count=${COUNT}
			;;
		tmpfs)	MKFS_CMD="true"
			MKFS_ARGS=""
			MNT_CMD="sudo mount -t tmpfs -o size=1G,nr_inodes=10k,mode=777 tmpfs ${MNT}"
			;;
		ramfs)	MKFS_CMD="true"
			MKFS_ARGS=""
			MNT_CMD="sudo mount -t ramfs -o size=1G ramfs ${MNT}"
			;;
		reiserfs)
			MKFS_CMD="mkfs.reiserfs"
			MKFS_ARGS="-q -f -h tea -b 1024 ${FSIMAGE}"
			MNT_CMD="sudo mount -o loop -o acl ${FSIMAGE} ${MNT}"
			dd if=/dev/zero of=${FSIMAGE} bs=1M count=${COUNT}
			;;
		overlay)
			MKFS_CMD="true"
			MKFS_ARGS=""
			MNT_CMD="sudo mount -t overlay overlay -o lowerdir=/tmp/lower,upperdir=/tmp/upper,workdir=/tmp/work ${MNT}"
			mkdir /tmp/lower /tmp/upper /tmp/work
			;;
		bcachefs)
			MKFS_CMD="bcachefs"
			MKFS_ARGS="format -f --compression=lz4 --replicas=3 --discard ${FSIMAGE}"
			MNT_CMD="sudo mount -o loop ${FSIMAGE} ${MNT}"
			dd if=/dev/zero of=${FSIMAGE} bs=1M count=${COUNT}
			;;
		*)
			echo "unsupported file system $1"
			return 1
		;;
	esac

	if which ${MKFS_CMD} ; then
		echo ${MKFS_CMD} ${MKFS_ARGS}
		sudo ${MKFS_CMD} ${MKFS_ARGS}
		rc=$?
		if [ $rc -ne 0 ]; then
			echo "${MKFS_CMD} ${MKFS_ARGS} failed, error: $rc"
			return 1
		fi
		mkdir -p ${MNT}
		sudo ${MNT_CMD}
		rc=$?
		if [ $rc -ne 0 ]; then
			echo "${MNT_CMD} failed, error: $rc"
			return 1
		fi

		sudo chmod 777 ${MNT}
		own=$(whoami)
		sudo chown $own:$own ${MNT}
	else
		echo "${MKFS_CMD} does not exist"
		return 1
	fi
	return 0
}

umount_filesystem()
{
	for I in $(seq 10)
	do
		kill_stress_ng
		sudo umount ${MNT}
		if [ $? -eq 0 ]; then
			break;
		else
			mnts=$(cat /proc/mounts | grep ${MNT} > /dev/null)
			if [ $? -eq 1 ]; then
				break;
			else
				echo umount of ${MNT} failed, retrying...
			fi
		fi
		sleep 1
	done
	rmdir ${MNT}
	rm -f ${FSIMAGE}

	case $1 in
		ubifs)
			sudo rmmod ubifs
			sudo rmmod ubi
		;;
	esac
}

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
	ARGS="-t $DURATION --pathological --timestamp --tz --syslog --perf --no-rand-seed --times --metrics --klog-check --status 5 -x smi -v --interrupts --change-cpu"
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

core_pattern_saved="$(cat /proc/sys/kernel/core_pattern)"
echo core | sudo tee /proc/sys/kernel/core_pattern >& /dev/null

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

fallocate -l 2G $SWAP
chmod 0600 $SWAP
sudo chown root:root $SWAP
sudo mkswap $SWAP
sudo swapon $SWAP

sudo lcov --zerocounters

if [ -f	/sys/kernel/debug/tracing/trace_stat/branch_all ]; then
	sudo cat  /sys/kernel/debug/tracing/trace_stat/branch_all > branch_all.start
fi
DURATION=180
do_stress --dev 32

for FS in bcachefs bfs btrfs ext2 ext3 ext4 exfat f2fs fat hfs hfsplus jfs minix1 minix2 minix3 nilfs ntfs overlay ramfs reiserfs tmpfs ubifs udf vfat xfs
do
	if mount_filesystem $FS; then
		MNTDEV=$(findmnt -T $MNT -o SOURCE  --verbose -n)
		MNTDEVBASE=$(basename $MNTDEV)
		echo MNTDEV $MNTDEV MNTDEVBASE $MNTDEVBASE
		DURATION=10
		if [ -e /sys/block/${MNTDEVBASE}/queue/scheduler ]; then
			IOSCHED=$(cat /sys/block/${MNTDEVBASE}/queue/scheduler | sed  's/.*\[\(.*\)\].*/\1/')
			IOSCHEDS=$(cat /sys/block/${MNTDEVBASE}/queue/scheduler | sed 's/\[//' | sed s'/\]//')
			for IO in $IOSCHEDS
			do
				echo "Filesystem: $FS $MNTDEV, iosched $IO of $IOSCHEDS"
				echo $IO | sudo tee /sys/block/${MNTDEVBASE}/queue/scheduler
				do_stress --iomix -1 --iostat 1
			done
			# revert to original ioscheduler
			echo $IOSCHED | sudo tee /sys/block/${MNTDEVBASE}/queue/scheduler
		else
			echo "Filesystem: $FS $MNTDEV, no iosched"
			do_stress --iomix -1 --iostat 1
		fi

		echo "Filesystem: $FS"
		do_stress --hdd -1 --hdd-ops 50000 --hdd-opts direct,utimes  --temp-path $MNT --iostat 1
		echo "Filesystem: $FS"
		do_stress --hdd -1 --hdd-ops 50000 --hdd-opts dsync --temp-path $MNT --iostat 1
		echo "Filesystem: $FS"
		do_stress --hdd -1 --hdd-ops 50000 --hdd-opts iovec,noatime --temp-path $MNT --iostat 1
		echo "Filesystem: $FS"
		do_stress --hdd -1 --hdd-ops 50000 --hdd-opts fsync,syncfs --temp-path $MNT --iostat 1
		echo "Filesystem: $FS"
		do_stress --hdd -1 --hdd-ops 50000 --hdd-opts fdatasync --temp-path $MNT --iostat 1
		echo "Filesystem: $FS"
		do_stress --hdd -1 --hdd-ops 50000 --hdd-opts rd-rnd,wr-rnd,fadv-rnd --temp-path $MNT --iostat 1
		echo "Filesystem: $FS"
		do_stress --hdd -1 --hdd-ops 50000 --hdd-opts rd-seq,wr-seq --temp-path $MNT --iostat 1
		echo "Filesystem: $FS"
		do_stress --hdd -1 --hdd-ops 50000 --hdd-opts fadv-normal --temp-path $MNT --iostat 1
		echo "Filesystem: $FS"
		do_stress --hdd -1 --hdd-ops 50000 --hdd-opts fadv-noreuse --temp-path $MNT --iostat 1
		echo "Filesystem: $FS"
		do_stress --hdd -1 --hdd-ops 50000 --hdd-opts fadv-rnd --temp-path $MNT --iostat 1
		echo "Filesystem: $FS"
		do_stress --hdd -1 --hdd-ops 50000 --hdd-opts fadv-seq --temp-path $MNT --iostat 1
		echo "Filesystem: $FS"
		do_stress --hdd -1 --hdd-ops 50000 --hdd-opts fadv-willneed --temp-path $MNT --iostat 1
		echo "Filesystem: $FS"
		do_stress --hdd -1 --hdd-ops 50000 --hdd-opts fadv-dontneed --temp-path $MNT --iostat 1
		echo "Filesystem: $FS"
		do_stress --verity -1 --temp-path $MNT --iostat 1
		echo "Filesystem: $FS"
		do_stress --utime -1 --utime-fsync --temp-path $MNT --iostat 1
		echo "Filesystem: $FS"
		sudo $STRESS_NG --swap 4 --temp-path $MNT -t $DURATION --iostat 1 --status 5 --timestamp -v
		DURATION=10
		echo "Filesystem: $FS"
		sudo $STRESS_NG --class filesystem --seq -1 -v --timestamp --syslog -t $DURATION --temp-path $MNT --iostat 1 --status 5
		echo "Filesystem: $FS"
		sudo $STRESS_NG --class io --seq -1 -v --timestamp --syslog -t $DURATION --temp-path $MNT --iostat 1 --status 5
		DURATION=5
		echo "Filesystem: $FS"
		do_stress --sysinfo -1 --temp-path $MNT --iostat 1
		umount_filesystem $FS
	fi
done

#
#  Exercise CPU schedulers
#
DURATION=20
scheds=$(${STRESS_NG} --sched which 2>&1 | tail -1 | cut -d':' -f2-)
for s in ${scheds}
do
	sudo ${STRESS_NG} --sched $s --cpu -1 -t 5 --timestamp --tz --syslog --perf --no-rand-seed --times --metrics --status 5
	sudo ${STRESS_NG} --sched $s --cpu -1 -t 5 --sched-reclaim --timestamp --tz --syslog --perf --no-rand-seed --times --metrics --status 5
done

#
#  Exercise ionice classes
#
ionices=$(${STRESS_NG} --ionice-class which 2>&1 | tail -1 | cut -d':' -f2-)
for i in ${ionices}
do
	do_stress --ionice-class $i --iomix -1 -t 30 --smart --iostat 1
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
			do_stress --${S} 1 --${S}-ops 10000
			;;
		*)
			do_stress --${S} 8 --${S}-ops 10000
			;;
	esac
done

DURATION=60
do_stress --all 1

#
#  Exercise various stressor options
#
do_stress --acl -1 --acl-rand

do_stress --affinity -1 --affinity-pin
do_stress --affinity -1 --affinity-rand
do_stress --affinity -1 --affinity-sleep

do_stress --bigheap -1 --bigheap-mlock

do_stress --brk -1 --brk-notouch --vmstat 1
do_stress --brk -1 --brk-mlock
do_stress --brk -1 --thrash

do_stress --cacheline 32 --cacheline-affinity

do_stress --cachehammer -1 --cachehammer-numa

do_stress --cpu -1 --sched batch --thermalstat 1
do_stress --cpu -1 --taskset 0,2 --ignite-cpu
do_stress --cpu -1 --taskset 1,2,3
do_stress --cpu -1 --taskset 0
do_stress --cpu -1 --cpu-load-slice 50
do_stress --cpu -1 --thermalstat 1 --vmstat 1 --tz
do_stress --cpu -1 --raplstat 1 --rapl
do_stress --cpu -1 --c-state

do_stress --cpu-online -1 --cpu-online-all --vmstat 1 --tz --pathological
do_stress --cpu-online -1 --cpu-online-affinity --pathological

do_stress --cpu-sched -1 --autogroup

do_stress --cyclic -1 --cyclic-policy deadline
do_stress --cyclic -1 --cyclic-policy fifo
do_stress --cyclic -1 --cyclic-policy rr
do_stress --cyclic -1 --cyclic-method clock_ns
do_stress --cyclic -1 --cyclic-method itimer
do_stress --cyclic -1 --cyclic-method poll
do_stress --cyclic -1 --cyclic-method posix_ns
do_stress --cyclic -1 --cyclic-method pselect
do_stress --cyclic -1 --cyclic-method usleep
do_stress --cyclic -1 --cyclic-prio 50

do_stress --dccp -1 --dccp-opts send
do_stress --dccp -1 --dccp-opts sendmsg
do_stress --dccp -1 --dccp-opts sendmmsg

do_stress --dccp -1 --dccp-domain ipv4
do_stress --dccp -1 --dccp-domain ipv6

do_stress --epoll -1 --epoll-domain ipv4
do_stress --epoll -1 --epoll-domain ipv6
do_stress --epoll -1 --epoll-domain unix
do_stress --epoll -1 --epoll-sockets 10000

do_stress --dentry -1 --dentry-order stride
do_stress --dentry -1 --dentry-order random

do_stress --eventfd -1 --eventfd-nonblock

do_stress --exec -1 --exec-no-pthread
do_stress --exec -1 --exec-fork-method clone
do_stress --exec -1 --exec-fork-method fork
do_stress --exec -1 --exec-fork-method spawn
do_stress --exec -1 --exec-fork-method vfork

do_stress --far-branch -1 --far-branch-flush

do_stress --fifo -1 --fifo-data-size 4096
do_stress --fifo -1 --fifo-readers 64

do_stress --fork -1 --fork-vm
do_stress --fork -1 --fork-max 64
do_stress --fork -1 --fork-pageout

do_stress --forkheavy -1 --forkheavy-mlock

do_stress --get -1 --get-slow-sync

do_stress --goto -1 --goto-direction forward
do_stress --goto -1 --goto-direction backward

do_stress --hrtimers -1 --hrtimers-adjust

do_stress --icmp-flood -1 --icmp-flood-max-size

do_stress --itimer -1 --itimer-rand
do_stress --itimer -1 --itimer-freq 1000

do_stress --l1cache -1 --l1cache-mlock

do_stress --link -1 --link-sync

do_stress --lease -1 --lease-breakers 8

do_stress --llc-affinity -1 --llc-affinity-clflush
do_stress --llc-affinity -1 --llc-affinity-mlock
do_stress --llc-affinity -1 --llc-affinity-numa

do_stress --lockf -1 --lockf-nonblock

do_stress --lockbus -1 --lockbus-nosplit

do_stress --madvise -1 --madvise-hwpoison

do_stress --malloc -1 --malloc-mlock
do_stress --malloc -1 --malloc-pthreads 4
do_stress --malloc -1 --malloc-touch
do_stress --malloc -1 --malloc-zerofree
do_stress --malloc -1 --malloc-trim

do_stress --memcontend -1 --memcontend-numa

do_stress --memfd -1 --memfd-fds 4096
do_stress --memfd -1 --memfd-mlock
do_stress --memfd -1 --memfd-numa
do_stress --memfd -1 --memfd-zap-pte

do_stress --memhotplug -1 --memhotplug-mmap

do_stress --memrate -1 --memrate-flush

do_stress --mincore -1 --mincore-random

do_stress --min-nanosleep -1 --min-nanosleep-sched batch
do_stress --min-nanosleep -1 --min-nanosleep-sched deadline
do_stress --min-nanosleep -1 --min-nanosleep-sched ext
do_stress --min-nanosleep -1 --min-nanosleep-sched fifo
do_stress --min-nanosleep -1 --min-nanosleep-sched idle
do_stress --min-nanosleep -1 --min-nanosleep-sched other
do_stress --min-nanosleep -1 --min-nanosleep-sched rr

do_stress --mmap -1 --mmap-async
do_stress --mmap -1 --mmap-file
do_stress --mmap -1 --mmap-madvise
do_stress --mmap -1 --mmap-mergeable
do_stress --mmap -1 --mmap-mlock
do_stress --mmap -1 --mmap-mprotect
do_stress --mmap -1 --mmap-odirect
do_stress --mmap -1 --mmap-osync
do_stress --mmap -1 --mmap-mmap2
do_stress --mmap -1 --mmap-write-check
do_stress --mmap -1 --mmap-stressful
do_stress --mmap -1 --mmap-slow-munmap
do_stress --mmap -1 --mmap-numa
do_stress --mmap -1 --thrash

do_stress --mmapaddr -1 --mmapaddr-mlock

do_stress --mmapcow -1 --mmapcow-fork
do_stress --mmapcow -1 --mmapcow-free
do_stress --mmapcow -1 --mmapcow-mlock
do_stress --mmapcow -1 --mmapcow-numa

do_stress --mmapfiles -1 --mmapfiles-numa
do_stress --mmapfiles -1 --mmapfiles-populate
do_stress --mmapfiles -1 --mmapfiles-shared

do_stress --mmapfixed -1 --mmapfixed-mlock
do_stress --mmapfixed -1 --mmapfixed-numa

do_stress --mmaphuge -1 --mmaphuge-file
do_stress --mmaphuge -1 --mmaphuge-mlock
do_stress --mmaphuge -1 --mmaphuge-numa
do_stress --mmaphuge -1 --mmaphuge-mmaps 32768

do_stress --mmapmany -1 --mmapmany-mlock
do_stress --mmapmany -1 --mmapmany-numa

do_stress --mmaprandom -1 --mmaprandom-numa
do_stress --mmaprandom -1 --mmaprandom-mappings 512
do_stress --mmaprandom -1 --mmaprandom-maxpages 128

do_stress --mmaptorture -1 --mmaptorture-bytes 30%
do_stress --mmaptorture -1 --mmaptorture-msync 95

do_stress --module -1 --module-name bfq

do_stress --mpfr -1 --mpfr-precision 8192

do_stress --mremap -1 --mremap-mlock
do_stress --mremap -1 --mremap-numa

do_stress --msg -1 --msg-types 100
do_stress --msg -1 --msg-bytes 8192

do_stress --mutex -1 --mutex-procs 64

do_stress --nanosleep -1 --nanosleep-threads 128
do_stress --nanosleep -1 --nanosleep-method cstate
do_stress --nanosleep -1 --nanosleep-method random
do_stress --nanosleep -1 --nanosleep-method ns
do_stress --nanosleep -1 --nanosleep-method us
do_stress --nanosleep -1 --nanosleep-method ms

do_stress --nice -1 --autogroup

do_stress --null -1 --null-write

do_stress --numa -1 --numa-shuffle-addr
do_stress --numa -1 --numa-shuffle-node

do_stress --open -1 --open-fd
do_stress --open -1 --open-max 100000

do_stress --pagemove -1 --pagemove-mlock
do_stress --pagemove -1 --pagemove-numa

do_stress --pipe -1 --pipe-size 64K
do_stress --pipe -1 --pipe-size 1M
do_stress --pipe -1 --pipe-data-size 64
do_stress --pipe -1 --pipe-vmsplice

do_stress --pipeherd 1 --pipeherd-yield

do_stress --poll -1 --poll-fds 8192
do_stress --poll -1 --poll-random-us 1000000

do_stress --prefetch -1 --prefetch-l3-size 16M

do_stress --pthread -1 --pthread-max 512
do_stress --pthread -1 --pthread-max 1024

do_stress --physmmap -1 --physmmap-read

do_stress --physpage -1 --physpage-mtrr

do_stress --pipe -1 --pipe-vmsplice

do_stress --pipeherd -1 --pipeherd-yield

do_stress --poll -1 --poll-random-us

do_stress --prio-inv -1 --prio-inv-type inherit
do_stress --prio-inv -1 --prio-inv-type none
do_stress --prio-inv -1 --prio-inv-type protect
do_stress --prio-inv -1 --prio-inv-policy batch
do_stress --prio-inv -1 --prio-inv-policy ext
do_stress --prio-inv -1 --prio-inv-policy fifo
do_stress --prio-inv -1 --prio-inv-policy idle
do_stress --prio-inv -1 --prio-inv-policy other
do_stress --prio-inv -1 --prio-inv-policy rr

do_stress --pseek -1 --pseek-io-size 1M
do_stress --pseek -1 --pseek-rand

do_stress --ptr-chase -1 --ptr-chase-pages 8192

do_stress --race-sched -1 --race-sched-method next
do_stress --race-sched -1 --race-sched-method prev
do_stress --race-sched -1 --race-sched-method randinc
do_stress --race-sched -1 --race-sched-method syncnext
do_stress --race-sched -1 --race-sched-method syncprev

do_stress --randlist -1 --randist-compact

do_stress --ramfs -1 --ramfs-fill
do_stress --ramfs -1 --ramfs-size 16M

do_stress --rawpkt -1 --rawpkt-rxring 2
do_stress --rawpkt -1 --rawpkt-rxring 16

do_stress --remap -1 --remap-mlock
do_stress --remap -1 --remap-pages 64

do_stress --resched -1 --autogroup

do_stress --resources -1 --resources-mlock

do_stress --revio -1 --revio-write-size 17

do_stress --ring-pipe -1 --ring-pipe-splice
do_stress --ring-pipe -1 --ring-pipe-num 1024
do_stress --ring-pipe -1 --ring-pipe-size 37

do_stress --sctp -1 --sctp-domain ipv4
do_stress --sctp -1 --sctp-domain ipv6
do_stress --sctp -1 --sctp-domain ipv4 --sctp-sched fcfs
do_stress --sctp -1 --sctp-domain ipv4 --sctp-sched prio
do_stress --sctp -1 --sctp-domain ipv4 --sctp-sched rr

do_stress --schedmix -1 --schedmix-procs 64
do_stress --schedmix -1 --autogroup

do_stress --schedpolicy -1 --autogroup

do_stress --shm -1 --shm-objs 100000
do_stress --shm -1 --shm-mlock

do_stress --shm-sysv -1 --shm-sysv-segs 128
do_stress --shm-sysv -1 --shm-sysv-mlock
do_stress --shm-sysv -1 --sem-sysv-setall

do_stress --seek -1 --seek-punch

do_stress --sem -1 --sem-procs 64
do_stress --sem -1 --sem-shared

do_stress --sem-sysv -1 --sem-sysv-procs 64
do_stress --sem-sysv -1 --sem-sysv-setall

do_stress --shm -1 --shm-mlock

do_stress --shm-sysv -1 --shm-sysv-mlock

do_stress --sleep -1 --sleep-max 4096

do_stress --sock -1 --sock-nodelay
do_stress --sock -1 --sock-domain ipv4
do_stress --sock -1 --sock-domain ipv6
do_stress --sock -1 --sock-domain unix
do_stress --sock -1 --sock-type stream
do_stress --sock -1 --sock-type seqpacket
do_stress --sock -1 --sock-protocol mptcp
do_stress --sock -1 --sock-opts random
do_stress --sock -1 --sock-opts send --sock-zerocopy

do_stress --sockfd -1 --sockfd-reuse

do_stress --spinmem -1 --spinmem-affinity
do_stress --spinmem -1 --spinmem-numa
do_stress --spinmem -1 --spinmem-yield

do_stress --splice -1 --splice-bytes 4K

do_stress --stack -1 --stack-mlock
do_stress --stack -1 --stack-fill
do_stress --stack -1 --stack-pageout
do_stress --stack -1 --stack-unmap

do_stress --stream -1 --stream-mlock
do_stress --stream -1 --stream-madvise collapse
do_stress --stream -1 --stream-madvise hugepage
do_stress --stream -1 --stream-madvise nohugepage
do_stress --stream -1 --stream-madvise normal
do_stress --stream -1 --stream-index 3
do_stress --stream -1 --stream-prefetch

do_stress --swap -1 --swap-self

do_stress --switch -1 --switch-freq 1000000
do_stress --switch -1 --switch-method mq
do_stress --switch -1 --switch-method pipe
do_stress --switch -1 --switch-method sem-sysv

do_stress --symlink -1 --symlink-sync

do_stress --syncload -1 --syncload-msbusy 200 --syncload-mssleep 100
do_stress --syncload -1 --syncload-msbusy 20 --syncload-mssleep 10

do_stress --timer -1 --timer-rand
do_stress --timer -1 --timer-freq 1000000
do_stress --timer -1 --timer-freq 100000 --timer-slack 1000

do_stress --timerfd -1 --timerfd-rand
do_stress --timerfd -1 --timerfd-freq 100000

do_stress --tsc -1 --tsc-lfence
do_stress --tsc -1 --tsc-rdtscp

do_stress --tmpfs -1 --tmpfs-mmap-async
do_stress --tmpfs -1 --tmpfs-mmap-file

do_stress --tun -1
do_stress --tun -1 --tun-tap

do_stress --udp -1 --udp-domain ipv4
do_stress --udp -1 --udp-domain ipv6
do_stress --udp -1 --udp-lite --udp-domain ipv4
do_stress --udp -1 --udp-lite --udp-domain ipv6
do_stress --udp -1 --udp-gro

do_stress --udp-flood -1 --udp-flood-domain ipv4
do_stress --udp-flood -1 --udp-flood-domain ipv6

do_stress --utime -1 --utime-fsync

do_stress --vfork 1 --vfork-max 64

do_stress --vforkmany 1 --vforkmany-vm

do_stress --vm -1 --vm-flush
do_stress --vm -1 --vm-keep
do_stress --vm -1 --vm-hang 1
do_stress --vm -1 --vm-locked
do_stress --vm -1 --vm-populate
do_stress --vm -1 --vm-madvise dontneed
do_stress --vm -1 --vm-madvise hugepage
do_stress --vm -1 --vm-madvise mergeable
do_stress --vm -1 --vm-madvise nohugepage
do_stress --vm -1 --vm-madvise mergeable
do_stress --vm -1 --vm-madvise normal
do_stress --vm -1 --vm-madvise random
do_stress --vm -1 --vm-madvise sequential
do_stress --vm -1 --vm-madvise unmergeable
do_stress --vm -1 --vm-madvise willneed --page-in
do_stress --vm -1 --vm-numa
do_stress --vm -1 --vm-populate --ksm

do_stress --vm-addr -1 --vm-addr-mlock
do_stress --vm-addr -1 --vm-addr-numa

do_stress --workload -1 --workload-sched batch --workload-load 90
do_stress --workload -1 --workload-sched deadline --workload-load 90
do_stress --workload -1 --workload-sched ext --workload-load 90
do_stress --workload -1 --workload-sched idle --workload-load 90
do_stress --workload -1 --workload-sched other --workload-load 90
do_stress --workload -1 --workload-threads 8
do_stress --workload -1 --workload-dist cluster
do_stress --workload -1 --workload-dist even
do_stress --workload -1 --workload-dist poisson
do_stress --workload -1 --workload-dist random1
do_stress --workload -1 --workload-dist random2
do_stress --workload -1 --workload-dist random3

do_stress --yield -1 --yield-sched deadline
do_stress --yield -1 --yield-sched ext
do_stress --yield -1 --yield-sched idle
do_stress --yield -1 --yield-sched fifo
do_stress --yield -1 --yield-sched other
do_stress --yield -1 --yield-sched rr

do_stress --zero -1 --zero-read

do_stress --zombie 1 --zombie-max 1000000

#
#  Longer duration stress testing to get more
#  coverage because of the large range of files to
#  traverse
#

DURATION=600
do_stress --sysfs 16
do_stress --procfs 32
DURATION=360
do_stress --sysinval 8 --pathological

DURATION=120
do_stress --bad-ioctl -1 --pathological
do_stress --sysbadaddr 8

#
#  And exercise I/O with plenty of time for file setup
#  overhead.
#
DURATION=60
sudo $STRESS_NG --class filesystem --ftrace --seq -1 -v --timestamp --syslog -t $DURATION --status 5
sudo $STRESS_NG --class io --ftrace --seq -1 -v --timestamp --syslog -t $DURATION --status 5

if [ -f	/sys/kernel/debug/tracing/trace_stat/branch_all ]; then
	sudo cat  /sys/kernel/debug/tracing/trace_stat/branch_all > branch_all.finish
fi

sudo swapoff $SWAP
sudo rm $SWAP
echo "$core_pattern_saved" | sudo tee /proc/sys/kernel/core_pattern >& /dev/null

if [ -e $PERF_PARANOID ]; then
	(echo $paranoid_saved | sudo tee $PERF_PARANOID) > /dev/null
fi

#
#  Process gcov data
#
sudo lcov -c --branch-coverage -o kernel.info --keep-going >& /dev/null
#
#  Convert to html
#
sudo genhtml --ignore-errors inconsistent -o html kernel.info
#
#  Convert to ascii for machine parsing
#
sudo find html -print -name "*.html" -exec html2text -ascii -o {}.txt {} \;
