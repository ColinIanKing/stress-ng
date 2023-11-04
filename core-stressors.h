/*
 * Copyright (C) 2022-2023 Colin Ian King
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 */
#ifndef CORE_STRESSORS_H
#define CORE_STRESSORS_H

/*
 *  stressor names, 1-to-1 match of stressor info struct names and
 *  stressors, only to be included in stress-ng.c
 */

#define STRESSORS(MACRO)	\
	MACRO(access) 		\
	MACRO(af_alg) 		\
	MACRO(affinity) 	\
	MACRO(aio) 		\
	MACRO(aiol) 		\
	MACRO(apparmor) 	\
	MACRO(alarm)		\
	MACRO(atomic)		\
	MACRO(bad_altstack) 	\
	MACRO(bad_ioctl) 	\
	MACRO(bigheap)		\
	MACRO(bind_mount)	\
	MACRO(binderfs)		\
	MACRO(branch)		\
	MACRO(brk)		\
	MACRO(bsearch)		\
	MACRO(cache)		\
	MACRO(cacheline)	\
	MACRO(cap)		\
	MACRO(cgroup)		\
	MACRO(chattr)		\
	MACRO(chdir)		\
	MACRO(chmod)		\
	MACRO(chown)		\
	MACRO(chroot)		\
	MACRO(clock)		\
	MACRO(clone)		\
	MACRO(close)		\
	MACRO(context)		\
	MACRO(copy_file)	\
	MACRO(cpu)		\
	MACRO(cpu_online)	\
	MACRO(crypt)		\
	MACRO(cyclic)		\
	MACRO(daemon)		\
	MACRO(dccp)		\
	MACRO(dekker)		\
	MACRO(dentry)		\
	MACRO(dev)		\
	MACRO(dev_shm)		\
	MACRO(dir)		\
	MACRO(dirdeep)		\
	MACRO(dirmany)		\
	MACRO(dnotify)		\
	MACRO(dup)		\
	MACRO(dynlib)		\
	MACRO(eigen)		\
	MACRO(efivar)		\
	MACRO(enosys)		\
	MACRO(env)		\
	MACRO(epoll)		\
	MACRO(eventfd) 		\
	MACRO(exec)		\
	MACRO(exit_group)	\
	MACRO(fallocate)	\
	MACRO(fanotify)		\
	MACRO(far_branch)	\
	MACRO(fault)		\
	MACRO(fcntl)		\
	MACRO(fiemap)		\
	MACRO(fifo)		\
	MACRO(file_ioctl)	\
	MACRO(filename)		\
	MACRO(flock)		\
	MACRO(flushcache)	\
	MACRO(fma)		\
	MACRO(fork)		\
	MACRO(forkheavy)	\
	MACRO(fp)		\
	MACRO(fp_error)		\
	MACRO(fpunch)		\
	MACRO(fsize)		\
	MACRO(fstat)		\
	MACRO(full)		\
	MACRO(funccall)		\
	MACRO(funcret)		\
	MACRO(futex)		\
	MACRO(get)		\
	MACRO(getdent)		\
	MACRO(getrandom)	\
	MACRO(goto)		\
	MACRO(gpu)		\
	MACRO(handle)		\
	MACRO(hash)		\
	MACRO(hdd)		\
	MACRO(heapsort)		\
	MACRO(hrtimers)		\
	MACRO(hsearch)		\
	MACRO(icache)		\
	MACRO(icmp_flood)	\
	MACRO(idle_page)	\
	MACRO(inode_flags)	\
	MACRO(inotify)		\
	MACRO(io)		\
	MACRO(iomix)		\
	MACRO(ioport)		\
	MACRO(ioprio)		\
	MACRO(io_uring)		\
	MACRO(ipsec_mb)		\
	MACRO(itimer)		\
	MACRO(jpeg)		\
	MACRO(judy)		\
	MACRO(kcmp)		\
	MACRO(key)		\
	MACRO(kill)		\
	MACRO(klog)		\
	MACRO(kvm)		\
	MACRO(l1cache)		\
	MACRO(landlock)		\
	MACRO(lease)		\
	MACRO(led)		\
	MACRO(link)		\
	MACRO(list)		\
	MACRO(llc_affinity)	\
	MACRO(loadavg)		\
	MACRO(locka)		\
	MACRO(lockbus)		\
	MACRO(lockf)		\
	MACRO(lockofd)		\
	MACRO(longjmp)		\
	MACRO(loop)		\
	MACRO(lsearch)		\
	MACRO(madvise)		\
	MACRO(malloc)		\
	MACRO(matrix)		\
	MACRO(matrix_3d)	\
	MACRO(mcontend)		\
	MACRO(membarrier)	\
	MACRO(memcpy)		\
	MACRO(memfd)		\
	MACRO(memhotplug)	\
	MACRO(memrate)		\
	MACRO(memthrash)	\
	MACRO(mergesort)	\
	MACRO(metamix)		\
	MACRO(mincore)		\
	MACRO(misaligned)	\
	MACRO(mknod)		\
	MACRO(mlock)		\
	MACRO(mlockmany)	\
	MACRO(mmap)		\
	MACRO(mmapaddr)		\
	MACRO(mmapfixed)	\
	MACRO(mmapfork)		\
	MACRO(mmaphuge)		\
	MACRO(mmapmany)		\
	MACRO(module)		\
	MACRO(mprotect)		\
	MACRO(mpfr)		\
	MACRO(mq)		\
	MACRO(mremap)		\
	MACRO(msg)		\
	MACRO(msync)		\
	MACRO(msyncmany)	\
	MACRO(munmap)		\
	MACRO(mutex)		\
	MACRO(nanosleep)	\
	MACRO(netdev)		\
	MACRO(netlink_proc)	\
	MACRO(netlink_task)	\
	MACRO(nice)		\
	MACRO(nop)		\
	MACRO(null)		\
	MACRO(numa)		\
	MACRO(oom_pipe)		\
	MACRO(opcode)		\
	MACRO(open)		\
	MACRO(pagemove)		\
	MACRO(pageswap)		\
	MACRO(pci)		\
	MACRO(personality)	\
	MACRO(peterson)		\
	MACRO(physpage)		\
	MACRO(pidfd)		\
	MACRO(ping_sock)	\
	MACRO(pipe)		\
	MACRO(pipeherd)		\
	MACRO(pkey)		\
	MACRO(plugin)		\
	MACRO(poll)		\
	MACRO(prctl)		\
	MACRO(prefetch)		\
	MACRO(priv_instr)	\
	MACRO(procfs)		\
	MACRO(pthread)		\
	MACRO(ptrace)		\
	MACRO(pty)		\
	MACRO(qsort)		\
	MACRO(quota)		\
	MACRO(race_sched)	\
	MACRO(radixsort)	\
	MACRO(randlist)		\
	MACRO(ramfs)		\
	MACRO(rawdev)		\
	MACRO(rawpkt)		\
	MACRO(rawsock)		\
	MACRO(rawudp)		\
	MACRO(rdrand)		\
	MACRO(readahead)	\
	MACRO(reboot)		\
	MACRO(regs)		\
	MACRO(remap)		\
	MACRO(rename)		\
	MACRO(resched)		\
	MACRO(resources)	\
	MACRO(revio)		\
	MACRO(ring_pipe)	\
	MACRO(rlimit)		\
	MACRO(rmap)		\
	MACRO(rotate)		\
	MACRO(rseq)		\
	MACRO(rtc)		\
	MACRO(schedmix)		\
	MACRO(schedpolicy)	\
	MACRO(sctp)		\
	MACRO(seal)		\
	MACRO(seccomp)		\
	MACRO(secretmem)	\
	MACRO(seek)		\
	MACRO(sem)		\
	MACRO(sem_sysv)		\
	MACRO(sendfile)		\
	MACRO(session)		\
	MACRO(set)		\
	MACRO(shellsort)	\
	MACRO(shm)		\
	MACRO(shm_sysv)		\
	MACRO(sigabrt)		\
	MACRO(sigbus)		\
	MACRO(sigchld)		\
	MACRO(sigfd)		\
	MACRO(sigfpe)		\
	MACRO(sigio)		\
	MACRO(signal)		\
	MACRO(signest)		\
	MACRO(sigpending)	\
	MACRO(sigpipe)		\
	MACRO(sigq)		\
	MACRO(sigrt)		\
	MACRO(sigsegv)		\
	MACRO(sigsuspend)	\
	MACRO(sigtrap)		\
	MACRO(skiplist)		\
	MACRO(sleep)		\
	MACRO(smi)		\
	MACRO(sock)		\
	MACRO(sockabuse)	\
	MACRO(sockdiag)		\
	MACRO(sockfd)		\
	MACRO(sockpair)		\
	MACRO(sockmany)		\
	MACRO(softlockup)	\
	MACRO(spawn)		\
	MACRO(sparsematrix)	\
	MACRO(splice)		\
	MACRO(stack)		\
	MACRO(stackmmap)	\
	MACRO(str)		\
	MACRO(stream)		\
	MACRO(swap)		\
	MACRO(switch)		\
	MACRO(symlink)		\
	MACRO(sync_file)	\
	MACRO(syncload)		\
	MACRO(sysbadaddr)	\
	MACRO(syscall)		\
	MACRO(sysinfo)		\
	MACRO(sysinval)		\
	MACRO(sysfs)		\
	MACRO(tee)		\
	MACRO(timer)		\
	MACRO(timerfd)		\
	MACRO(tlb_shootdown)	\
	MACRO(tmpfs)		\
	MACRO(touch)		\
	MACRO(tree)		\
	MACRO(trig)		\
	MACRO(tsc)		\
	MACRO(tsearch)		\
	MACRO(tun)		\
	MACRO(udp)		\
	MACRO(udp_flood)	\
	MACRO(umount)		\
	MACRO(unshare)		\
	MACRO(uprobe)		\
	MACRO(urandom)		\
	MACRO(userfaultfd)	\
	MACRO(usersyscall)	\
	MACRO(utime)		\
	MACRO(vdso)		\
	MACRO(vecfp)		\
	MACRO(vecmath)		\
	MACRO(vecshuf)		\
	MACRO(vecwide)		\
	MACRO(verity)		\
	MACRO(vfork)		\
	MACRO(vforkmany)	\
	MACRO(vm)		\
	MACRO(vm_addr)		\
	MACRO(vm_rw)		\
	MACRO(vm_segv)		\
	MACRO(vm_splice)	\
	MACRO(vma)		\
	MACRO(vnni)		\
	MACRO(wait)		\
	MACRO(waitcpu)		\
	MACRO(watchdog)		\
	MACRO(wcs)		\
	MACRO(workload)		\
	MACRO(x86cpuid)		\
	MACRO(x86syscall)	\
	MACRO(xattr)		\
	MACRO(yield)		\
	MACRO(zero)		\
	MACRO(zlib)		\
	MACRO(zombie)

/*
 *  Declaration of stress_*_info object
 */
#define STRESSOR_ENUM(name)	\
	STRESS_ ## name,


/*
 *  Elements in stressor array
 */
#define STRESSOR_ELEM(name)		\
{					\
	&stress_ ## name ## _info,	\
	STRESS_ ## name,		\
	OPT_ ## name,			\
	OPT_ ## name  ## _ops,		\
	# name				\
},

/*
 *  Declaration of stress_*_info object
 */
#define STRESSOR_INFO(name)     \
	extern stressor_info_t stress_ ## name ## _info;

#endif
