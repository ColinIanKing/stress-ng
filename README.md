# stress-ng

stress-ng will stress test a computer system in various selectable ways. It
was designed to exercise various physical subsystems of a computer as well as
the various operating system kernel interfaces. Stress-ng features:

  * Over 270 stress tests
  * 85+ CPU specific stress tests that exercise floating point, integer,
    bit manipulation and control flow
  * over 20 virtual memory stress tests
  * portable: builds on Linux, Solaris, *BSD, Minix, Android, MacOS X,
    GNU/Hurd, Haiku, Windows Subsystem for Linux and SunOs/Dilos with
    gcc, clang, icc, tcc and pcc.
  * tested on alpha, armhf, arm64, hppa, i386, m68k, mips32, mips64, ppc64el,
    risc-v, s390x, sparc64, x86-64

stress-ng was originally intended to make a machine work hard and trip hardware
issues such as thermal overruns as well as operating system bugs that only
occur when a system is being thrashed hard. Use stress-ng with caution as some
of the tests can make a system run hot on poorly designed hardware and also can
cause excessive system thrashing which may be difficult to stop.

stress-ng can also measure test throughput rates; this can be useful to observe
performance changes across different operating system releases or types of
hardware. However, it has never been intended to be used as a precise benchmark
test suite, so do NOT use it in this manner.

Running stress-ng with root privileges will adjust out of memory settings on
Linux systems to make the stressors unkillable in low memory situations, so use
this judiciously. With the appropriate privilege, stress-ng can allow the ionice
class and ionice levels to be adjusted, again, this should be used with care.

## Tarballs

Tarballs of each version of stress-ng can be downloaded using the URL:

https://github.com/ColinIanKing/stress-ng/tarball/version

where version is the relevant version number, for example:

https://github.com/ColinIanKing/stress-ng/tarball/V0.13.05

## Building stress-ng

To build, the following libraries will ensure a fully functional stress-ng
build: (note libattr is not required for more recent disto releases).

Debian, Ubuntu:

  * libaio-dev
  * libapparmor-dev
  * libattr1-dev
  * libbsd-dev
  * libcap-dev
  * libgcrypt11-dev
  * libipsec-mb-dev
  * libjudy-dev
  * libkeyutils-dev
  * libsctp-dev
  * libatomic1
  * zlib1g-dev
  * libkmod-dev
  * libxxhash-dev

RHEL, Fedora, Centos:

  * libaio-devel
  * libattr-devel
  * libbsd-devel
  * libcap-devel
  * libgcrypt-devel
  * Judy-devel
  * keyutils-libs-devel
  * lksctp-tools-devel
  * libatomic
  * zlib-devel
  * kmod-devel
  * xxhash-devel

RHEL, Fedora, Centos (static builds):

  * libaio-devel
  * libattr-devel
  * libbsd-devel
  * libcap-devel
  * libgcrypt-devel
  * Judy-devel
  * keyutils-libs-devel
  * lksctp-tools-devel
  * libatomic-static
  * zlib-devel
  * glibc-static
  * xxhash-devel

SUSE:
  * keyutils-devel
  * libaio-devel
  * libapparmor-devel
  * libattr-devel
  * libbsd-devel
  * libcap-devel
  * libseccomp-devel
  * lksctp-tools-devel
  * libatomic1
  * zlib-devel
  * libkmod-devel
  * xxhash-devel

NOTE: the build will try to detect build dependencies and will build an image
with functionality disabled if the support libraries are not installed.

At build-time stress-ng will detect kernel features that are available on the
target build system and enable stress tests appropriately. Stress-ng has been
build-tested on Ubuntu, Debian, Debian GNU/Hurd, Slackware, RHEL, SLES, Centos,
kFreeBSD, OpenBSD, NetBSD, FreeBSD, Debian kFreeBSD, DragonFly BSD, OS X, Minix,
Solaris 11.3, OpenIndiana and Hiaku. Ports to other POSIX/UNIX like operating
systems should be relatively easy.

NOTE: ALWAYS run ```make clean``` after fetching changes from the git repository
to force the build to regenerate the build configuration file.

To build on BSD systems, one requires gcc and GNU make:
```
        CC=gcc gmake clean
	CC=gcc gmake
```

To build on OS X systems, just use:
```
	make clean
	make
```

To build on MINIX, gmake and clang are required:
```
	CC=clang gmake clean
	CC=clang gmake
```

To build on SunOS, one requires GCC and GNU make, build using:
```
	CC=gcc gmake clean
	CC=gcc gmake
```

To build on Dilos, one requires GCC and GNU make, build using:
```
	CC=gcc gmake clean
	CC=gcc gmake
```

To build on Haiku Alpha 4:
```
	make clean
	make
```

To build a static image (example, for Android), use:
```
	make clean
	STATIC=1 make
```

To build with full warnings enabled:
```
	make clean
	PEDANTIC=1 make
```

To build with the Tiny C compiler:
```
	make clean
	CC=tcc make
```

To build  with the PCC portable C compiler use:
```
	make clean
	CC=pcc make
```

To build with the musl C library:
```
	CC=musl-gcc make
```

To build with the Intel C compiler use:
```
	make clean
	CC=icc make
```

## Contributing to stress-ng:

Send patches to colin.i.king@gmail.com or merge requests at
https://github.com/ColinIanKing/stress-ng

## Quick Start Reference Guide
The [Ubuntu stress-ng reference guide](https://wiki.ubuntu.com/Kernel/Reference/stress-ng)
contains a brief overview and worked examples.

## Examples

Run 4 CPU, 2 virtual memory, 1 disk and 8 fork stressors for 2 minutes and print measurements:
```
stress-ng --cpu 4 --vm 2 --hdd 1 --fork 8 --timeout 2m --metrics
stress-ng: info:  [573366] setting to a 120 second (2 mins, 0.00 secs) run per stressor
stress-ng: info:  [573366] dispatching hogs: 4 cpu, 2 vm, 1 hdd, 8 fork
stress-ng: info:  [573366] successful run completed in 123.78s (2 mins, 3.78 secs)
stress-ng: info:  [573366] stressor       bogo ops real time  usr time  sys time   bogo ops/s     bogo ops/s CPU used per
stress-ng: info:  [573366]                           (secs)    (secs)    (secs)   (real time) (usr+sys time) instance (%)
stress-ng: info:  [573366] cpu              515396    120.00    453.02      0.18      4294.89        1137.24        94.42
stress-ng: info:  [573366] vm              2261023    120.01    223.80      1.80     18840.15       10022.27        93.99
stress-ng: info:  [573366] hdd              367558    123.78     10.63     11.67      2969.49       16482.42        18.02
stress-ng: info:  [573366] fork             598058    120.00     68.24     65.88      4983.80        4459.13        13.97
```

Run matrix stressor on all online CPUs for 60 seconds and measure temperature:
```
stress-ng --matrix -1 --tz -t 60
stress-ng: info:  [1171459] setting to a 60 second run per stressor
stress-ng: info:  [1171459] dispatching hogs: 8 matrix
stress-ng: info:  [1171459] successful run completed in 60.01s (1 min, 0.01 secs)
stress-ng: info:  [1171459] matrix:
stress-ng: info:  [1171459]               acpitz0   75.00 C (348.15 K)
stress-ng: info:  [1171459]               acpitz1   75.00 C (348.15 K)
stress-ng: info:  [1171459]          pch_skylake   60.17 C (333.32 K)
stress-ng: info:  [1171459]         x86_pkg_temp   62.72 C (335.87 K)
```

Run a mix of 4 I/O stressors and check for changes in disk S.M.A.R.T. metadata:
```
sudo stress-ng --iomix 4 --smart -t 30s
stress-ng: info:  [1171471] setting to a 30 second run per stressor
stress-ng: info:  [1171471] dispatching hogs: 4 iomix
stress-ng: info:  [1171471] successful run completed in 30.37s
stress-ng: info:  [1171471] Device     ID S.M.A.R.T. Attribute                 Value      Change
stress-ng: info:  [1171471] sdc        01 Read Error Rate                   88015771       71001
stress-ng: info:  [1171471] sdc        07 Seek Error Rate                   59658169          92
stress-ng: info:  [1171471] sdc        c3 Hardware ECC Recovered            88015771       71001
stress-ng: info:  [1171471] sdc        f1 Total LBAs Written               481904395         877
stress-ng: info:  [1171471] sdc        f2 Total LBAs Read                 3768039248        5139
stress-ng: info:  [1171471] sdd        be Temperature Difference             3670049           1
```

Benchmark system calls using the VDSO:
```
stress-ng --vdso 1 -t 5 --metrics
stress-ng: info:  [1171584] setting to a 5 second run per stressor
stress-ng: info:  [1171584] dispatching hogs: 1 vdso
stress-ng: info:  [1171585] stress-ng-vdso: exercising vDSO functions: clock_gettime time gettimeofday getcpu
stress-ng: info:  [1171585] stress-ng-vdso: 9.88 nanoseconds per call (excluding 1.73 nanoseconds test overhead)
stress-ng: info:  [1171584] successful run completed in 5.10s
stress-ng: info:  [1171584] stressor       bogo ops real time  usr time  sys time   bogo ops/s     bogo ops/s CPU used per
stress-ng: info:  [1171584]                           (secs)    (secs)    (secs)   (real time) (usr+sys time) instance (%)
stress-ng: info:  [1171584] vdso          430633496      5.10      5.10      0.00  84375055.96    84437940.39        99.93
stress-ng: info:  [1171584] vdso               9.88 nanoseconds per call (average per stressor)
```

Generate and measure branch misses using perf metrics:
```
sudo stress-ng --branch 1 --perf -t 10 2>& 1 | grep Branch
stress-ng: info:  [1171714]                604,703,327 Branch Instructions            53.30 M/sec
stress-ng: info:  [1171714]                598,760,234 Branch Misses                  52.77 M/sec (99.02%)
```

## Bugs found with stress-ng

stress-ng has found several Linux Kernel bugs and appropriate fixes have been landed to address these issues:

* [fs/locks.c: kernel oops during posix lock stress test](https://lkml.org/lkml/2016/11/27/212)
* [rcu_preempt detected stalls on CPUs/tasks](https://lkml.org/lkml/2017/8/28/574)
* [BUG: unable to handle kernel NULL pointer dereference](https://lkml.org/lkml/2017/10/30/247)
* [WARNING: possible circular locking dependency detected](https://www.spinics.net/lists/kernel/msg2679315.html)
* [SMP divide error](https://bugs.centos.org/view.php?id=14366)
* [ext4_validate_inode_bitmap:99: comm stress-ng: Corrupt inode bitmap](https://bugs.launchpad.net/ubuntu/+source/linux/+bug/1780137)
* [4.15 s390x kernel BUG at /build/linux-Gycr4Z/linux-4.15.0/drivers/block/virtio_blk.c:565](https://bugs.launchpad.net/ubuntu/+source/linux/+bug/1788432)
* [mm/page_idle.c: fix oops because end_pfn is larger than max_pfn](https://git.kernel.org/pub/scm/linux/kernel/git/next/linux-next.git/commit/mm/page_idle.c?id=d96d6145d9796d5f1eac242538d45559e9a23404)
* [Illumos: ofdlock(): assertion failed: lckdat->l_start == 0](https://www.illumos.org/issues/9061)
* [mm: compaction: avoid 100% CPU usage during compaction when a task is killed](https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/commit/?id=670105a25608affe01cb0ccdc2a1f4bd2327172b)
* [mm/vmalloc.c: preload a CPU with one object for split purpose](https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/commit/?id=82dd23e84be3ead53b6d584d836f51852d1096e6)
* [debugobjects: Use global free list in __debug_check_no_obj_freed()](https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/commit/?id=1ea9b98b007a662e402551a41a4413becad40a65)
* [ARM: dts: meson8b: add reserved memory zone to fix silent freezes](https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/commit/?id=b9b4bf504c9e94fe38b93aa2784991c80cebcf2e)
* [ARM64: dts: meson-gx: Add firmware reserved memory zones](https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/commit/?id=bba8e3f42736cf7f974968a818e53b128286ad1d)
* [sched/core: Fix a race between try_to_wake_up() and a woken up task](https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/commit/?id=135e8c9250dd5c8c9aae5984fde6f230d0cbfeaf)
* [ext4: lock the xattr block before checksuming it](https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/commit/?id=dac7a4b4b1f664934e8b713f529b629f67db313c)
* [devpts: fix null pointer dereference on failed memory allocation](https://git.kernel.org/cgit/linux/kernel/git/torvalds/linux.git/commit/?id=5353ed8deedee9e5acb9f896e9032158f5d998de)
* [KEYS: ensure we free the assoc array edit if edit is valid](https://git.kernel.org/cgit/linux/kernel/git/torvalds/linux.git/commit/?id=ca4da5dd1f99fe9c59f1709fb43e818b18ad20e0)
* [arm64: do not enforce strict 16 byte alignment to stack pointer](https://git.kernel.org/cgit/linux/kernel/git/torvalds/linux.git/commit/?id=e6d9a52543338603e25e71e0e4942f05dae0dd8a)
* [proc: fix -ESRCH error when writing to /proc/$pid/coredump_filter](https://git.kernel.org/cgit/linux/kernel/git/torvalds/linux.git/commit/?id=41a0c249cb8706a2efa1ab3d59466b23a27d0c8b)
* [perf evlist: Use unshare(CLONE_FS) in sb threads to let setns(CLONE_NEWNS) work](https://git.kernel.org/cgit/linux/kernel/git/torvalds/linux.git/commit/?id=b397f8468fa27f08b83b348ffa56a226f72453af)
* [riscv: reject invalid syscalls below -1](https://git.kernel.org/cgit/linux/kernel/git/torvalds/linux.git/commit/?id=556f47ac6083d778843e89aa21b1242eee2693ed)
* [RISC-V: Don't allow write+exec only page mapping request in mmap](https://git.kernel.org/cgit/linux/kernel/git/torvalds/linux.git/commit/?id=e0d17c842c0f824fd4df9f4688709fc6907201e1)
* [riscv: set max_pfn to the PFN of the last page](https://git.kernel.org/cgit/linux/kernel/git/torvalds/linux.git/commit/?id=c749bb2d554825e007cbc43b791f54e124dadfce)
* [crypto: hisilicon - update SEC driver module parameter](https://git.kernel.org/cgit/linux/kernel/git/torvalds/linux.git/commit/?id=57b1aac1b426b7255afa195298ed691ffea204c6)
* [net: atm: fix update of position index in lec_seq_next](https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/commit/?id=2f71e00619dcde3d8a98ba3e7f52e98282504b7d)
* [sched/debug: Fix memory corruption caused by multiple small reads of flags](https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/commit/?id=8d4d9c7b4333abccb3bf310d76ef7ea2edb9828f)
* [ocfs2: ratelimit the 'max lookup times reached' notice](https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/commit/?id=45680967ee29e67b62e6800a8780440b840a0b1f)
* [using perf can crash kernel with a stack overflow](https://bugs.launchpad.net/ubuntu/+source/linux/+bug/1875941)
* [stress-ng on gcov enabled focal kernel triggers OOPS](https://bugs.launchpad.net/ubuntu/+source/linux/+bug/1879470)
* [sparc64: Fix opcode filtering in handling of no fault loads](https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/commit/?id=e5e8b80d352ec999d2bba3ea584f541c83f4ca3f)
* [opening a file with O_DIRECT on a file system that does not support it will leave an empty file](https://bugzilla.kernel.org/show_bug.cgi?id=213041)
* [locking/atomic: sparc: Fix arch_cmpxchg64_local()](https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/commit/?id=7e1088760cfe0bb1fdb1f0bd155bfd52f080683a)
* [btrfs: fix exhaustion of the system chunk array due to concurrent allocations](https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/commit/?id=986aa0f276752ca4809f95b260f59fafef01a6a7)
* [btrfs: rework chunk allocation to avoid exhaustion of the system chunk array](https://git.kernel.org/cgit/linux/kernel/git/torvalds/linux.git/commit/?id=79bd37120b149532af5b21953643ed74af69654f)
* [btrfs: fix deadlock with concurrent chunk allocations involving system chunks](https://git.kernel.org/cgit/linux/kernel/git/torvalds/linux.git/commit/?id=1cb3db1cf383a3c7dbda1aa0ce748b0958759947)
* [locking/atomic: sparc: Fix arch_cmpxchg64_local()](https://git.kernel.org/cgit/linux/kernel/git/torvalds/linux.git/commit/?id=7e1088760cfe0bb1fdb1f0bd155bfd52f080683a)
* [pipe: do FASYNC notifications for every pipe IO, not just state changes](https://git.kernel.org/cgit/linux/kernel/git/torvalds/linux.git/commit/?id=fe67f4dd8daa252eb9aa7acb61555f3cc3c1ce4c)
* [io-wq: remove GFP_ATOMIC allocation off schedule out path](https://git.kernel.org/cgit/linux/kernel/git/torvalds/linux.git/commit/?id=d3e9f732c415cf22faa33d6f195e291ad82dc92e)
* [mm/swap: consider max pages in iomap_swapfile_add_extent](https://git.kernel.org/cgit/linux/kernel/git/torvalds/linux.git/commit/?id=36ca7943ac18aebf8aad4c50829eb2ea5ec847df)
* [copy_process(): Move fd_install() out of sighand->siglock critical section](https://git.kernel.org/cgit/linux/kernel/git/torvalds/linux.git/commit/?id=ddc204b517e60ae64db34f9832dc41dafa77c751)

## Kernel improvements that used stress-ng

* [selinux: complete the inlining of hashtab functions](https://git.kernel.org/cgit/linux/kernel/git/torvalds/linux.git/commit/?id=54b27f9287a7b3dfc85549f01fc9d292c92c68b9)
* [selinux: store role transitions in a hash table](https://git.kernel.org/cgit/linux/kernel/git/torvalds/linux.git/commit/?id=e67b2ec9f6171895e774f6543626913960e019df)
* [sched/rt: Optimize checking group RT scheduler constraints](https://git.kernel.org/cgit/linux/kernel/git/torvalds/linux.git/commit/?id=b4fb015eeff7f3e5518a7dbe8061169a3e2f2bc7)
* [sched/fair: handle case of task_h_load() returning 0](https://git.kernel.org/cgit/linux/kernel/git/torvalds/linux.git/commit/?id=01cfcde9c26d8555f0e6e9aea9d6049f87683998)
* [sched/deadline: Unthrottle PI boosted threads while enqueuing](https://git.kernel.org/cgit/linux/kernel/git/torvalds/linux.git/commit/?id=feff2e65efd8d84cf831668e182b2ce73c604bbb)
* [mm: fix madvise WILLNEED performance problem](https://git.kernel.org/cgit/linux/kernel/git/torvalds/linux.git/commit/?id=66383800df9cbdbf3b0c34d5a51bf35bcdb72fd2)
* [powerpc/dma: Fix dma_map_ops::get_required_mask](https://git.kernel.org/cgit/linux/kernel/git/torvalds/linux.git/commit/?id=437ef802e0adc9f162a95213a3488e8646e5fc03)
* [Revert "mm, slub: consider rest of partial list if acquire_slab() fails](https://git.kernel.org/cgit/linux/kernel/git/torvalds/linux.git/commit/?id=9b1ea29bc0d7b94d420f96a0f4121403efc3dd85)
* [mm: memory: add orig_pmd to struct vm_fault](https://git.kernel.org/cgit/linux/kernel/git/torvalds/linux.git/commit/?id=5db4f15c4fd7ae74dd40c6f84bf56dfcf13d10cf)
* [selftests/powerpc: Add test of mitigation patching](https://git.kernel.org/cgit/linux/kernel/git/torvalds/linux.git/commit/?id=34f7f79827ec4db30cff9001dfba19f496473e8d)
* [dm crypt: Avoid percpu_counter spinlock contention in crypt_page_alloc()](https://git.kernel.org/cgit/linux/kernel/git/torvalds/linux.git/commit/?id=528b16bfc3ae5f11638e71b3b63a81f9999df727)
* [mm/migrate: optimize hotplug-time demotion order updates](https://git.kernel.org/cgit/linux/kernel/git/torvalds/linux.git/commit/?id=295be91f7ef0027fca2f2e4788e99731aa931834)
* [powerpc/rtas: rtas_busy_delay() improvements](https://git.kernel.org/cgit/linux/kernel/git/torvalds/linux.git/commit/?id=38f7b7067dae0c101be573106018e8af22a90fdf)
* [sched/core: Accounting forceidle time for all tasks except idle task](https://git.kernel.org/cgit/linux/kernel/git/torvalds/linux.git/commit/?id=b171501f258063f5c56dd2c5fdf310802d8d7dc1)

## Presentations

* [Stress-ng presentation at ELCE 2019 Lyon](https://static.sched.com/hosted_files/osseu19/29/Lyon-stress-ng-presentation-oct-2019.pdf)
* [Video of the above presentation](https://www.youtube.com/watch?v=8QaXStKfq3A)

## Citations

* [Auto-scaling of Containers: the impact of Relative and Absolute Metrics](https://www.researchgate.net/publication/319905237_Auto-Scaling_of_Containers_The_Impact_of_Relative_and_Absolute_Metrics)
* [Increasing Platform Determinism PQOS DPDK](https://www.intel.com/content/dam/www/public/us/en/documents/white-papers/increasing-platform-determinism-pqos-dpdk-paper.pdf)
* [Testing the Windows Subsystem for Linux](https://blogs.msdn.microsoft.com/wsl/2017/04/11/testing-the-windows-subsystem-for-linux/)
* [Practical analysis of the Precision Time Protocol under different types of system load](http://www.diva-portal.org/smash/get/diva2:1106630/FULLTEXT01.pdf)
* [Towards Virtual Machine Energy-Aware Cost Prediction in Clouds](http://eprints.whiterose.ac.uk/124309/1/paper_final.pdf)
* [Towards Energy Efficient Data Management in HPC: The Open Ethernet Drive Approach](http://www.pdsw.org/pdsw-discs16/papers/p43-kougkas.pdf)
* [Enhancing Cloud energy models for optimizing datacenters efficiency](https://cs.gmu.edu/~menasce/cs788/papers/ICCAC2015-Outin.pdf)
* [CPU and memory performance analysis on dynamic and dedicated resource allocation using XenServer in Data Center environment](http://ieeexplore.ieee.org/document/7877341/)
* [Algorithms and Architectures for Parallel Processing](https://books.google.co.uk/books?id=S4wwDwAAQBAJ&pg=PA7&lpg=PA7&dq=http://kernel.ubuntu.com/~cking/stress-ng/&source=bl&ots=bVZccBq2Io&sig=rIqKWyEhGmVPosAJiemKjGgEv0M&hl=en&sa=X&ved=0ahUKEwiFo6LO2fbXAhWBtxQKHRcnDY04HhDoAQguMAE#v=onepage&q=http%3A%2F%2Fkernel.ubuntu.com%2F~cking%2Fstress-ng%2F&f=false)
* [Advanced concepts and tools for renewable energy supply of IT Data Centres](http://www.renewit-tool.eu/Content/File/5-IT%20Load.pdf)
* [How Much Power Does your Server Consume? Estimating Wall Socket Power Using RAPL Measurements](http://www.ena-hpc.org/2016/pdf/khan_et_al_enahpc.pdf)
* [Tejo: A Supervised Anomaly Detection Scheme for NewSQL Databases](https://hal.archives-ouvertes.fr/hal-01211772/document)
* [Monitoring and Modelling Open Compute Servers](http://staff.www.ltu.se/~damvar/Publications/Eriksson%20et%20al.%20-%202017%20-%20Monitoring%20and%20Modelling%20Open%20Compute%20Servers.pdf)
* [Experimental and numerical analysis for potential heat reuse in liquid cooled data centres](http://personals.ac.upc.edu/jguitart/HomepageFiles/ECM16.pdf)
* [DevOps for IoT Applications using Cellular Networks and Cloud](https://www.ericsson.com/assets/local/publications/conference-papers/devops.pdf)
* [Modeling and Analysis of Performance under Interference in the Cloud](https://www3.cs.stonybrook.edu/~anshul/mascots17.pdf)
* [Effectively Measure and Reduce Kernel Latencies for Real time Constraints](https://elinux.org/images/a/a9/ELC2017-_Effectively_Measure_and_Reduce_Kernel_Latencies_for_Real-time_Constraints_%281%29.pdf)
* [Monitoring and Analysis of CPU load relationships between Host and Guests in a Cloud Networking Infrastructure](http://www.diva-portal.org/smash/get/diva2:861235/FULLTEXT02)
* [Measuring the impacts of the Preempt-RT patch](http://events17.linuxfoundation.org/sites/events/files/slides/rtpatch.pdf)
* [Multicore Emulation on Virtualised Environment](https://indico.esa.int/indico/event/165/contribution/5/material/1/0.pdf)
* [Stress-SGX : Load and Stress your Enclaves for Fun and Profit](https://seb.vaucher.org/papers/stress-sgx.pdf)
* [Reliable Library Identification Using VMI Techniques](http://www.delaat.net/rp/2016-2017/p64/report.pdf)
* [Elastic-PPQ: A two-level autonomic system for spatial preference query processing over dynamic data stream](https://www.researchgate.net/publication/319613604_Elastic-PPQ_A_two-level_autonomic_system_for_spatial_preference_query_processing_over_dynamic_data_streams)
* [Caliper Benchmarking](http://open-estuary.org/caliper-benchmarking)
* [OpenEPC integration within 5GTN as an NFV proof of concept](http://jultika.oulu.fi/files/nbnfioulu-201706082638.pdf)
* [quiho: Automated Performance Regression Testing Using Inferred Resource Utilization Profiles](https://dl.acm.org/citation.cfm?id=3184422&dl=ACM&coll=DL&preflayout=flat)
* [A Virtual Network Function Workload Simulator](https://uu.diva-portal.org/smash/get/diva2:1043751/FULLTEXT01.pdf)
* [Time-Aware Dynamic Binary Instrumentation](https://uwspace.uwaterloo.ca/bitstream/handle/10012/12182/Arafa_Pansy.pdf?sequence=3)
* [Characterizing and Reducing Cross-Platform Performance Variability Using OS-level Virtualization](http://www.lofstead.org/papers/2016-varsys.pdf)
* [Experience Report: Log Mining using Natural Language Processing and Application to Anomaly Detection](https://hal.laas.fr/hal-01576291/document)
* [CoMA: Resource Monitoring of Docker Containers](http://www.scitepress.org/Papers/2015/54480/54480.pdf)
* [An Investigation of CPU utilization relationship between host and guests in a Cloud infrastructure](http://www.diva-portal.org/smash/get/diva2:861239/FULLTEXT02)
* [Hypervisor and Virtual Machine Memory Optimization Analysis](https://www.tuit.ut.ee/sites/default/files/tuit/at/thesis/bsc/2018/atprog-courses-bakalaureuset55-loti.05.029-tambet-viitkar-text-20180520.pdf)
* [Linux kernel performance test tool](https://01.org/sites/default/files/documentation/lkp-tests.pdf)
* [Real-Time testing with Fuego](https://elinux.org/images/4/43/ELC2018_Real-time_testing_with_Fuego-181024m.pdf)
* [Performance and Energy Trade-Offs for Parallel Applications on Heterogeneous Multi-Processing Systems](https://www.mdpi.com/1996-1073/13/9/2409/htm)
* [C-Balancer: A System for Container Profiling and Scheduling](https://arxiv.org/pdf/2009.08912.pdf)
* [Modelling VM Latent Characteristics and Predicting Application Performance using Semi-supervised Non-negative Matrix Factorization](https://ieeexplore.ieee.org/document/9284328)
* [Semi-dynamic load balancing: efficient distributed learning in non-dedicated environments](https://dl.acm.org/doi/10.1145/3419111.3421299)
* [Streamline: A Fast, Flushless Cache Covert-Channel Attack byEnabling Asynchronous Collusion](https://dl.acm.org/doi/pdf/10.1145/3445814.3446742)
* [Experimental Analysis in Hadoop MapReduce: A Closer Look at Fault Detection and Recovery Techniques](https://www.mdpi.com/1131714)
* [Performance Characteristics of theBlueField-2 SmartNIC](https://arxiv.org/pdf/2105.06619.pdf)
* [Evaluating Latency in Multiprocessing Embedded Systems for the Smart Grid](https://www.mdpi.com/1996-1073/14/11/3322)
* [Work-in-Progress: Timing Diversity as a Protective Mechanism](https://dl.acm.org/doi/pdf/10.1145/3477244.3477614)
* [Sequential Deep Learning Architectures for Anomaly Detection in Virtual Network Function Chains](https://arxiv.org/pdf/2109.14276.pdf)
* [WattEdge: A Holistic Approach for Empirical Energy Measurements in Edge Computing](https://www.researchgate.net/publication/356342806_WattEdge_A_Holistic_Approach_for_Empirical_Energy_Measurements_in_Edge_Computing)
* [FECBench: An Extensible Framework for Pinpointing Sources of Performance Interference in the Cloud-Edge Resource Spectrum](https://www.academia.edu/68455840/FECBench_An_Extensible_Framework_for_Pinpointing_Sources_of_Performance_Interference_in_the_Cloud_Edge_Resource_Spectrum)
* [A general method for evaluating the overhead when consolidating servers: performance degradation in virtual machines and containers](https://link.springer.com/article/10.1007/s11227-022-04318-5)

I am keen to add to the stress-ng project page any citations to research or
projects that use stress-ng.  I also appreciate information concerning kernel
bugs or performance regressions found with stress-ng.
