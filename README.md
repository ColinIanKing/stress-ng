# stress-ng (stress next generation)

<a href="https://repology.org/project/stress-ng/versions">
    <img src="https://repology.org/badge/vertical-allrepos/stress-ng.svg" alt="Packaging status" align="right">
</a>

stress-ng will stress test a computer system in various selectable ways. It
was designed to exercise various physical subsystems of a computer as well as
the various operating system kernel interfaces. Stress-ng features:

  * 330+ stress tests
  * 80+ CPU specific stress tests that exercise floating point, integer,
    bit manipulation and control flow
  * 20+ virtual memory stress tests
  * 40+ file system stress tests
  * 30+ memory/CPU cache stress tests
  * portable: builds on Linux (Debian, Devuan, RHEL, Fedora, Centos, Slackware
    OpenSUSE, Ubuntu, etc..), Solaris, FreeBSD, NetBSD, OpenBSD, DragonFlyBSD,
    Minix, Android, MacOS X, Serenity OS, GNU/Hurd, Haiku, Windows Subsystem
    for Linux, Cygwin and SunOs/Dilos/Solaris.
    with gcc, musl-gcc, clang, icc, icx, tcc and pcc.
  * tested on alpha, armel, armhf, arm64, hppa, i386, loong64, m68k, mips32, mips64,
    power32, ppc64el, risc-v, sh4, s390x, sparc64, x86-64

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

## Running latest stress-ng snapshot in a container

```bash
docker run --rm ghcr.io/colinianking/stress-ng --help
```

or

```bash
docker run --rm colinianking/stress-ng --help
```

## Debian packages for Ubuntu

Recent versions of stress-ng are available in the Ubuntu stress-ng ppa for various
Ubuntu releases:

https://launchpad.net/~colin-king/+archive/ubuntu/stress-ng

```
sudo add-apt-repository ppa:colin-king/stress-ng
sudo apt update
sudo apt install stress-ng
```

## Building stress-ng

To build, the following libraries will ensure a fully functional stress-ng
build: (note libattr is not required for more recent disto releases).

Debian, Ubuntu:

  * gcc
  * g++
  * libacl1-dev
  * libaio-dev
  * libapparmor-dev
  * libatomic1
  * libattr1-dev
  * libbsd-dev
  * libcap-dev
  * libeigen3-dev
  * libgbm-dev
  * libcrypt-dev
  * libglvnd-dev
  * libipsec-mb-dev
  * libjpeg-dev
  * libjudy-dev
  * libkeyutils-dev
  * libkmod-dev
  * libmd-dev
  * libmpfr-dev
  * libsctp-dev
  * libxxhash-dev
  * zlib1g-dev

RHEL, Fedora, Centos:

  * gcc
  * g++
  * eigen3-devel
  * Judy-devel
  * keyutils-libs-devel
  * kmod-devel
  * libacl-devel
  * libaio-devel
  * libatomic
  * libattr-devel
  * libbsd-devel
  * libcap-devel
  * libgbm-devel
  * libcrypt-devel
  * libglvnd-core-devel
  * libglvnd-devel
  * libjpeg-devel
  * libmd-devel
  * mpfr-devel
  * libX11-devel
  * libXau-devel
  * libxcb-devel
  * lksctp-tools-devel
  * xorg-x11-proto-devel
  * xxhash-devel
  * zlib-devel

RHEL, Fedora, Centos (static builds):

  * gcc
  * g++
  * eigen3-devel
  * glibc-static
  * Judy-devel
  * keyutils-libs-devel
  * libacl-devel
  * libaio-devel
  * libatomic-static
  * libattr-devel
  * libbsd-devel
  * libcap-devel
  * libgbm-devel
  * libcrypt-devel
  * libglvnd-core-devel
  * libglvnd-devel
  * libjpeg-devel
  * libmd-devel
  * libX11-devel
  * libXau-devel
  * libxcb-devel
  * lksctp-tools-devel
  * mpfr-devel
  * xorg-x11-proto-devel
  * xxhash-devel
  * zlib-devel

SUSE:
  * gcc
  * gcc-c++
  * eigen3-devel
  * keyutils-devel
  * libaio-devel
  * libapparmor-devel
  * libatomic1
  * libattr-devel
  * libbsd-devel
  * libcap-devel
  * libgbm-devel
  * libglvnd-devel
  * libjpeg-turbo
  * libkmod-devel
  * libmd-devel
  * libseccomp-devel
  * lksctp-tools-devel
  * mpfr-devel
  * xxhash-devel
  * zlib-devel

ClearLinux:
  * devpkg-acl
  * devpkg-eigen
  * devpkg-Judy
  * devpkg-kmod
  * devpkg-attr
  * devpkg-libbsd
  * devpkg-libjpeg-turbo
  * devpkg-libsctp
  * devpkg-mesa

Alpine Linux:
  * build-base
  * eigen-dev
  * jpeg-dev
  * judy-dev
  * keyutils-dev
  * kmod-dev
  * libacl-dev
  * libaio-dev
  * libatomic
  * libattr
  * libbsd-dev
  * libcap-dev
  * libmd-dev
  * libseccomp-dev
  * lksctp-tools-dev
  * mesa-dev
  * mpfr-dev
  * xxhash-dev
  * zlib-dev

Snaps:
  * stress-ng is not intended to be snap'd with snapcraft. Doing so is strictly
    against the wishes of the project maintainer and main developer.

NOTE: the build will try to detect build dependencies and will build an image
with functionality disabled if the support libraries are not installed.

At build-time stress-ng will detect kernel features that are available on the
target build system and enable stress tests appropriately. Stress-ng has been
build-tested on Ubuntu, Debian, Debian GNU/Hurd, Slackware, RHEL, SLES, Centos,
kFreeBSD, OpenBSD, NetBSD, FreeBSD, Debian kFreeBSD, DragonFly BSD, OS X, Minix,
Solaris 11.3, OpenIndiana and Hiaku. Ports to other POSIX/UNIX like operating
systems should be relatively easy.

NOTE: ALWAYS run ```make clean``` after fetching changes from the git repository
to force the build to regenerate the build configuration file. Parallel builds using
make -j are supported.

To build on BSD systems, one requires gcc and GNU make:
```
        CC=gcc gmake clean
	CC=gcc gmake
```

To build on OS X systems, just use:
```
	make clean
	make -j
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
	make clean
	CC=musl-gcc
```

To build with the Intel C compiler icc use:
```
	make clean
	CC=icc make
```

To build with the Intel C compiler icx use:
```
	make clean
	CC=icx make
```

To perform a cross-compilation using gcc, use a static build, specify
the toolchain (both CC and CXX). For example, a mips64 cross build:

```
	make clean
	STATIC=1 CC=mips64-linux-gnuabi64-gcc CXX=mips64-linux-gnuabi64-g++ make -j $(nproc)
```

To perform a cross-compile for qnx, for example, a aarch64 qnx cross build:

```
    make clean
    CC=aarch64-unknown-nto-qnx7.1.0-gcc CXX=aarch64-unknown-nto-qnx7.1.0-g++ STATIC=1 make
```

To build with debug (-g) enabled use:
```
	make clean
	DEBUG=1 make
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
sudo stress-ng --branch 1 --perf -t 10 --stdout | grep Branch
stress-ng: info:  [1171714]                604,703,327 Branch Instructions            53.30 M/sec
stress-ng: info:  [1171714]                598,760,234 Branch Misses                  52.77 M/sec (99.02%)
```

## Bugs and regressions found with stress-ng

stress-ng has found Kernel and QEMU bugs/regressions and appropriate fixes have been landed to address these issues:

2015:
* [KEYS: ensure we free the assoc array edit if edit is valid](https://git.kernel.org/cgit/linux/kernel/git/torvalds/linux.git/commit/?id=ca4da5dd1f99fe9c59f1709fb43e818b18ad20e0)
* [proc: fix -ESRCH error when writing to /proc/$pid/coredump_filter](https://git.kernel.org/cgit/linux/kernel/git/torvalds/linux.git/commit/?id=41a0c249cb8706a2efa1ab3d59466b23a27d0c8b)
* [SMP divide error](https://bugs.centos.org/view.php?id=14366)

2016:
* [fs/locks.c: kernel oops during posix lock stress test](https://lkml.org/lkml/2016/11/27/212)
* [sched/core: Fix a race between try_to_wake_up() and a woken up task](https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/commit/?id=135e8c9250dd5c8c9aae5984fde6f230d0cbfeaf)
* [devpts: fix null pointer dereference on failed memory allocation](https://git.kernel.org/cgit/linux/kernel/git/torvalds/linux.git/commit/?id=5353ed8deedee9e5acb9f896e9032158f5d998de)
* [arm64: do not enforce strict 16 byte alignment to stack pointer](https://git.kernel.org/cgit/linux/kernel/git/torvalds/linux.git/commit/?id=e6d9a52543338603e25e71e0e4942f05dae0dd8a)

2017:
* [ARM: dts: meson8b: add reserved memory zone to fix silent freezes](https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/commit/?id=b9b4bf504c9e94fe38b93aa2784991c80cebcf2e)
* [ARM64: dts: meson-gx: Add firmware reserved memory zones](https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/commit/?id=bba8e3f42736cf7f974968a818e53b128286ad1d)
* [ext4: lock the xattr block before checksuming it](https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/commit/?id=dac7a4b4b1f664934e8b713f529b629f67db313c)
* [rcu_preempt detected stalls on CPUs/tasks](https://lkml.org/lkml/2017/8/28/574)
* [BUG: unable to handle kernel NULL pointer dereference](https://lkml.org/lkml/2017/10/30/247)
* [WARNING: possible circular locking dependency detected](https://www.spinics.net/lists/kernel/msg2679315.html)

2018:
* [Illumos: ofdlock(): assertion failed: lckdat->l_start == 0](https://www.illumos.org/issues/9061)
* [debugobjects: Use global free list in __debug_check_no_obj_freed()](https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/commit/?id=1ea9b98b007a662e402551a41a4413becad40a65)
* [ext4_validate_inode_bitmap:99: comm stress-ng: Corrupt inode bitmap](https://bugs.launchpad.net/ubuntu/+source/linux/+bug/1780137)
* [virtio/s390: fix race in ccw_io_helper()](https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/commit/?id=78b1a52e05c9db11d293342e8d6d8a230a04b4e7)

2019:
* [mm/page_idle.c: fix oops because end_pfn is larger than max_pfn](https://git.kernel.org/pub/scm/linux/kernel/git/next/linux-next.git/commit/mm/page_idle.c?id=d96d6145d9796d5f1eac242538d45559e9a23404)
* [mm: compaction: avoid 100% CPU usage during compaction when a task is killed](https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/commit/?id=670105a25608affe01cb0ccdc2a1f4bd2327172b)
* [mm/vmalloc.c: preload a CPU with one object for split purpose](https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/commit/?id=82dd23e84be3ead53b6d584d836f51852d1096e6)
* [perf evlist: Use unshare(CLONE_FS) in sb threads to let setns(CLONE_NEWNS) work](https://git.kernel.org/cgit/linux/kernel/git/torvalds/linux.git/commit/?id=b397f8468fa27f08b83b348ffa56a226f72453af)
* [riscv: reject invalid syscalls below -1](https://git.kernel.org/cgit/linux/kernel/git/torvalds/linux.git/commit/?id=556f47ac6083d778843e89aa21b1242eee2693ed)

2020:
* [RISC-V: Don't allow write+exec only page mapping request in mmap](https://git.kernel.org/cgit/linux/kernel/git/torvalds/linux.git/commit/?id=e0d17c842c0f824fd4df9f4688709fc6907201e1)
* [riscv: set max_pfn to the PFN of the last page](https://git.kernel.org/cgit/linux/kernel/git/torvalds/linux.git/commit/?id=c749bb2d554825e007cbc43b791f54e124dadfce)
* [crypto: hisilicon - update SEC driver module parameter](https://git.kernel.org/cgit/linux/kernel/git/torvalds/linux.git/commit/?id=57b1aac1b426b7255afa195298ed691ffea204c6)
* [net: atm: fix update of position index in lec_seq_next](https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/commit/?id=2f71e00619dcde3d8a98ba3e7f52e98282504b7d)
* [sched/debug: Fix memory corruption caused by multiple small reads of flags](https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/commit/?id=8d4d9c7b4333abccb3bf310d76ef7ea2edb9828f)
* [ocfs2: ratelimit the 'max lookup times reached' notice](https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/commit/?id=45680967ee29e67b62e6800a8780440b840a0b1f)
* [using perf can crash kernel with a stack overflow](https://bugs.launchpad.net/ubuntu/+source/linux/+bug/1875941)
* [stress-ng on gcov enabled focal kernel triggers OOPS](https://bugs.launchpad.net/ubuntu/+source/linux/+bug/1879470)
* [kernel bug list_del corruption on s390x from stress-ng mknod and stress-ng symlink](https://bugzilla.redhat.com/show_bug.cgi?id=1849196)

2021:
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
* [block: loop: fix deadlock between open and remove](https://git.kernel.org/cgit/linux/kernel/git/torvalds/linux.git/commit/?id=990e78116d38059c9306cf0560c1c4ed1cf358d3)
* [tmpfs: O_DIRECT | O_CREATE open reports open failure but actually creates a file](https://bugzilla.kernel.org/show_bug.cgi?id=218049)

2022:
* [copy_process(): Move fd_install() out of sighand->siglock critical section](https://git.kernel.org/cgit/linux/kernel/git/torvalds/linux.git/commit/?id=ddc204b517e60ae64db34f9832dc41dafa77c751)
* [minix: fix bug when opening a file with O_DIRECT](https://git.kernel.org/cgit/linux/kernel/git/torvalds/linux.git/commit/?id=9ce3c0d26c42d279b6c378a03cd6a61d828f19ca)
* [arch/arm64: Fix topology initialization for core scheduling](https://git.kernel.org/cgit/linux/kernel/git/torvalds/linux.git/commit/?id=5524cbb1bfcdff0cad0aaa9f94e6092002a07259)
* [running stress-ng on Minux 3.4.0-RC6 on amd64 assert in vm/region.c:313](https://github.com/Stichting-MINIX-Research-Foundation/minix/issues/333)
* [unshare test triggers unhandled page fault](https://bugs.launchpad.net/ubuntu/+source/linux/+bug/1959215)
* [request_module DoS](https://www.spinics.net/lists/kernel/msg4349826.html)
* [NUMA Benchmark Regression In Linux 5.18](https://lore.kernel.org/lkml/YmrWK%2FKoU1zrAxPI@fuller.cnet)
* [Underflow in mas_spanning_rebalance() and test](https://lore.kernel.org/linux-mm/20220625003854.1230114-1-Liam.Howlett@oracle.com/)
* [mm/huge_memory: do not clobber swp_entry_t during THP split](https://git.kernel.org/cgit/linux/kernel/git/torvalds/linux.git/commit/?id=71e2d666ef85d51834d658830f823560c402b8b6)
* [AppArmor: -42.5% regression of stress-ng.kill.ops_per_sec due to commit](https://lkml.org/lkml/2022/12/31/27)
* [clocksource: Suspend the watchdog temporarily when high read lantency detected](https://lore.kernel.org/lkml/20221220082512.186283-1-feng.tang@intel.com/t/)

2023:
* [qemu-system-m68k segfaults on opcode 0x4848](https://gitlab.com/qemu-project/qemu/-/issues/1462)
* [rtmutex: Ensure that the top waiter is always woken up](https://git.kernel.org/cgit/linux/kernel/git/torvalds/linux.git/commit/?id=db370a8b9f67ae5f17e3d5482493294467784504)
* [mm/swap: fix swap_info_struct race between swapoff and get_swap_pages()](https://git.kernel.org/cgit/linux/kernel/git/torvalds/linux.git/commit/?id=6fe7d6b992113719e96744d974212df3fcddc76c)
* [block, bfq: Fix division by zero error on zero wsum](https://git.kernel.org/cgit/linux/kernel/git/torvalds/linux.git/commit/?id=e53413f8deedf738a6782cc14cc00bd5852ccf18)
* [riscv: mm: Ensure prot of VM_WRITE and VM_EXEC must be readable](https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/commit/?id=6569fc12e442ea973d96db39e542aa19a7bc3a79)
* [Revert "mm: vmscan: make global slab shrink lockless"](https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/commit/?id=71c3ad65fabec9620d3f548b2da948c79c7ad9d5)
* [crash/hang in mm/swapfile.c:718 add_to_avail_list when exercising stress-ng](https://bugzilla.kernel.org/show_bug.cgi?id=217738)
* [mm: fix zswap writeback race condition](https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/commit/?id=04fc7816089c5a32c29a04ec94b998e219dfb946)
* [x86/fpu: Set X86_FEATURE_OSXSAVE feature after enabling OSXSAVE in CR4](https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/commit/?id=2c66ca3949dc701da7f4c9407f2140ae425683a5)
* [kernel/fork: beware of __put_task_struct() calling context](https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/commit/?id=d243b34459cea30cfe5f3a9b2feb44e7daff9938)
* [arm64: dts: ls1028a: add l1 and l2 cache info](https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/commit/?id=fcf7ff67a2aa6d8055b9b815ad8a28a5231afa1e)
* [filemap: add filemap_map_order0_folio() to handle order0 folio](https://git.kernel.org/cgit/linux/kernel/git/torvalds/linux.git/commit/?id=c8be03806738c86521dbf1e0503bc90855fb99a3)
* [mm: shrinker: add infrastructure for dynamically allocating shrinker](https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/commit/?id=c42d50aefd17a6bad3ed617769edbbb579137545)
* [mm: shrinker: make global slab shrink lockless](https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/commit/?id=ca1d36b823944f24b5755311e95883fb5fdb807b)
* [bcachefs: Clear btree_node_just_written() when node reused or evicted](https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/commit/?id=0b438c5bfaebda3fdf6edc35d9572d4e2f66aef1)
* [tracing: Fix incomplete locking when disabling buffered events](https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/commit/?id=7fed14f7ac9cf5e38c693836fe4a874720141845)
* [mm: migrate: fix getting incorrect page mapping during page migration](https://git.kernel.org/cgit/linux/kernel/git/torvalds/linux.git/commit/?id=d1adb25df7111de83b64655a80b5a135adbded61)
* [mm: mmap: map MAP_STACK to VM_NOHUGEPAGE](https://git.kernel.org/cgit/linux/kernel/git/torvalds/linux.git/commit/?id=c4608d1bf7c6536d1a3d233eb21e50678681564e)

2024:
* [fs: improve dump_mapping() robustness](https://git.kernel.org/cgit/linux/kernel/git/torvalds/linux.git/commit/?id=8b3d838139bcd1e552f1899191f734264ce2a1a5)
* [tracing: Ensure visibility when inserting an element into tracing_map](https://git.kernel.org/cgit/linux/kernel/git/torvalds/linux.git/commit/?id=2b44760609e9eaafc9d234a6883d042fc21132a7)
* [connector/cn_proc: revert "connector: Fix proc_event_num_listeners count not cleared"](https://git.kernel.org/cgit/linux/kernel/git/torvalds/linux.git/commit/?id=8929f95b2b587791a7dcd04cc91520194a76d3a6)
* [powerpc/pseries: fix accuracy of stolen time](https://git.kernel.org/cgit/linux/kernel/git/torvalds/linux.git/commit/?id=cbecc9fcbbec60136b0180ba0609c829afed5c81)
* [OpenBSD: libm: segfault in sincosl](https://marc.info/?l=openbsd-bugs&m=171453603728385&w=2)
* [opening and closing /dev/dri/card0 in a QEMU KVM instance will shutdown system, 6.10.0-rc6+](https://bugzilla.kernel.org/show_bug.cgi?id=219007)
* [uprobes: prevent mutex_lock() under rcu_read_lock()](https://git.kernel.org/cgit/linux/kernel/git/torvalds/linux.git/commit/?id=699646734ab51bf5b1cd4a7a30c20074f6e74f6e)
* [system lock up with RT kernel on amd64](https://bugs.launchpad.net/ubuntu-realtime/+bug/2068900)
* [WARNING: CPU: 17 PID: 118273 at kernel/sched/deadline.c:794 setup_new_dl_entity+0x12c/0x1e8](https://bugs.launchpad.net/ubuntu-realtime/+bug/2068720)
* [kernel oops in pick_next_task_fair in 6.8.1-1002-realtime kernel](https://bugs.launchpad.net/ubuntu-realtime/+bug/2068615)
* [kernel oops in aafs_create in 6.8.1-1002-realtime kernel](https://bugs.launchpad.net/ubuntu-realtime/+bug/2068602)
* [mm: optimize the redundant loop](https://git.kernel.org/cgit/linux/kernel/git/torvalds/linux.git/commit/?id=cf3f9a593dab87a032d2b6a6fb205e7f3de4f0a1)

## Kernel improvements that used stress-ng

2020:
* [selinux: complete the inlining of hashtab functions](https://git.kernel.org/cgit/linux/kernel/git/torvalds/linux.git/commit/?id=54b27f9287a7b3dfc85549f01fc9d292c92c68b9)
* [selinux: store role transitions in a hash table](https://git.kernel.org/cgit/linux/kernel/git/torvalds/linux.git/commit/?id=e67b2ec9f6171895e774f6543626913960e019df)
* [sched/rt: Optimize checking group RT scheduler constraints](https://git.kernel.org/cgit/linux/kernel/git/torvalds/linux.git/commit/?id=b4fb015eeff7f3e5518a7dbe8061169a3e2f2bc7)
* [sched/fair: handle case of task_h_load() returning 0](https://git.kernel.org/cgit/linux/kernel/git/torvalds/linux.git/commit/?id=01cfcde9c26d8555f0e6e9aea9d6049f87683998)
* [sched/deadline: Unthrottle PI boosted threads while enqueuing](https://git.kernel.org/cgit/linux/kernel/git/torvalds/linux.git/commit/?id=feff2e65efd8d84cf831668e182b2ce73c604bbb)
* [mm: fix madvise WILLNEED performance problem](https://git.kernel.org/cgit/linux/kernel/git/torvalds/linux.git/commit/?id=66383800df9cbdbf3b0c34d5a51bf35bcdb72fd2)
* [powerpc/dma: Fix dma_map_ops::get_required_mask](https://git.kernel.org/cgit/linux/kernel/git/torvalds/linux.git/commit/?id=437ef802e0adc9f162a95213a3488e8646e5fc03)
* [stress-ng close causes kernel oops(es) v5.6-rt and v5.4-rt](https://www.spinics.net/lists/linux-rt-users/msg22350.html)

2021:
* [Revert "mm, slub: consider rest of partial list if acquire_slab() fails](https://git.kernel.org/cgit/linux/kernel/git/torvalds/linux.git/commit/?id=9b1ea29bc0d7b94d420f96a0f4121403efc3dd85)
* [mm: memory: add orig_pmd to struct vm_fault](https://git.kernel.org/cgit/linux/kernel/git/torvalds/linux.git/commit/?id=5db4f15c4fd7ae74dd40c6f84bf56dfcf13d10cf)
* [selftests/powerpc: Add test of mitigation patching](https://git.kernel.org/cgit/linux/kernel/git/torvalds/linux.git/commit/?id=34f7f79827ec4db30cff9001dfba19f496473e8d)
* [dm crypt: Avoid percpu_counter spinlock contention in crypt_page_alloc()](https://git.kernel.org/cgit/linux/kernel/git/torvalds/linux.git/commit/?id=528b16bfc3ae5f11638e71b3b63a81f9999df727)
* [mm/migrate: optimize hotplug-time demotion order updates](https://git.kernel.org/cgit/linux/kernel/git/torvalds/linux.git/commit/?id=295be91f7ef0027fca2f2e4788e99731aa931834)
* [powerpc/rtas: rtas_busy_delay() improvements](https://git.kernel.org/cgit/linux/kernel/git/torvalds/linux.git/commit/?id=38f7b7067dae0c101be573106018e8af22a90fdf)

2022:
* [sched/core: Accounting forceidle time for all tasks except idle task](https://git.kernel.org/cgit/linux/kernel/git/torvalds/linux.git/commit/?id=b171501f258063f5c56dd2c5fdf310802d8d7dc1)
* [ipc/mqueue: use get_tree_nodev() in mqueue_get_tree()](https://git.kernel.org/cgit/linux/kernel/git/torvalds/linux.git/commit/?id=d60c4d01a98bc1942dba6e3adc02031f5519f94b)

2023:
* [mm/swapfile: add cond_resched() in get_swap_pages()](https://git.kernel.org/cgit/linux/kernel/git/torvalds/linux.git/commit/?id=7717fc1a12f88701573f9ed897cc4f6699c661e3)
* [module: add debug stats to help identify memory pressure](https://git.kernel.org/cgit/linux/kernel/git/torvalds/linux.git/commit/?id=df3e764d8e5cd416efee29e0de3c93917dff5d33)
* [module: avoid allocation if module is already present and ready](https://git.kernel.org/cgit/linux/kernel/git/torvalds/linux.git/commit/?id=064f4536d13939b6e8cdb71298ff5d657f4f8caa)
* [sched: Interleave cfs bandwidth timers for improved single thread performance at low utilization](https://git.kernel.org/cgit/linux/kernel/git/torvalds/linux.git/commit/?id=41abdba9374734b743019fc1cc05e3225c82ba6b)
* [mm/khugepaged: remove redundant try_to_freeze()](https://git.kernel.org/cgit/linux/kernel/git/torvalds/linux.git/commit/?id=b39ca208403c8f2c17dab1fbfef1f5ecaff25e53)

2024:
* [mm/vmalloc: eliminated the lock contention from twice to once](https://git.kernel.org/cgit/linux/kernel/git/torvalds/linux.git/commit/?id=aaab830ad887629156ef17097c2ad24ce6fb8177)
* [mm: switch mm->get_unmapped_area() to a flag](https://git.kernel.org/cgit/linux/kernel/git/torvalds/linux.git/commit/?id=529ce23a764f25d172198b4c6ba90f1e2ad17f93)

## Presentations

* [Stress-ng presentation at ELCE 2019 Lyon](https://static.sched.com/hosted_files/osseu19/29/Lyon-stress-ng-presentation-oct-2019.pdf)
* [Video of the above presentation](https://www.youtube.com/watch?v=8QaXStKfq3A)
* [Linux Foundation Mentoring Session, May 2022](https://www.youtube.com/watch?v=gD3Hn02VSHA)
* [Kernel Recipes presentation, Sept 2023](https://www.youtube.com/watch?v=PD0NOZCTIVQ)

## Citations

* [Linux kernel performance test tool](https://cdrdv2-public.intel.com/723902/lkp-tests.pdf)

2015:
* [Enhancing Cloud energy models for optimizing datacenters efficiency](https://cs.gmu.edu/~menasce/cs788/papers/ICCAC2015-Outin.pdf)
* [Tejo: A Supervised Anomaly Detection Scheme for NewSQL Databases](https://hal.archives-ouvertes.fr/hal-01211772/document)
* [CoMA: Resource Monitoring of Docker Containers](http://www.scitepress.org/Papers/2015/54480/54480.pdf)
* [An Investigation of CPU utilization relationship between host and guests in a Cloud infrastructure](http://www.diva-portal.org/smash/get/diva2:861239/FULLTEXT02)

2016:
* [Increasing Platform Determinism PQOS DPDK](https://www.intel.com/content/dam/www/public/us/en/documents/white-papers/increasing-platform-determinism-pqos-dpdk-paper.pdf)
* [Towards Energy Efficient Data Management in HPC: The Open Ethernet Drive Approach](http://www.pdsw.org/pdsw-discs16/papers/p43-kougkas.pdf)
* [CPU and memory performance analysis on dynamic and dedicated resource allocation using XenServer in Data Center environment](http://ieeexplore.ieee.org/document/7877341/)
* [How Much Power Does your Server Consume? Estimating Wall Socket Power Using RAPL Measurements](http://www.ena-hpc.org/2016/pdf/khan_et_al_enahpc.pdf)
* [DevOps for IoT Applications using Cellular Networks and Cloud](https://www.ericsson.com/assets/local/publications/conference-papers/devops.pdf)
* [A Virtual Network Function Workload Simulator](https://uu.diva-portal.org/smash/get/diva2:1043751/FULLTEXT01.pdf)
* [Characterizing and Reducing Cross-Platform Performance Variability Using OS-level Virtualization](http://www.lofstead.org/papers/2016-varsys.pdf)
* [How much power does your server consume? Estimating wall socket power using RAPL measurements](https://www.researchgate.net/publication/306004997_How_much_power_does_your_server_consume_Estimating_wall_socket_power_using_RAPL_measurements)
* [UIE: User-centric Interference Estimation for Cloud Applications](https://www3.cs.stonybrook.edu/~anshul/ic2e16_uie.pdf)

2017:
* [Auto-scaling of Containers: the impact of Relative and Absolute Metrics](https://www.researchgate.net/publication/319905237_Auto-Scaling_of_Containers_The_Impact_of_Relative_and_Absolute_Metrics)
* [Testing the Windows Subsystem for Linux](https://blogs.msdn.microsoft.com/wsl/2017/04/11/testing-the-windows-subsystem-for-linux/)
* [Practical analysis of the Precision Time Protocol under different types of system load](http://www.diva-portal.org/smash/get/diva2:1106630/FULLTEXT01.pdf)
* [Towards Virtual Machine Energy-Aware Cost Prediction in Clouds](http://eprints.whiterose.ac.uk/124309/1/paper_final.pdf)
* [Algorithms and Architectures for Parallel Processing](https://books.google.co.uk/books?id=S4wwDwAAQBAJ&pg=PA7&lpg=PA7&dq=http://kernel.ubuntu.com/~cking/stress-ng/&source=bl&ots=bVZccBq2Io&sig=rIqKWyEhGmVPosAJiemKjGgEv0M&hl=en&sa=X&ved=0ahUKEwiFo6LO2fbXAhWBtxQKHRcnDY04HhDoAQguMAE#v=onepage&q=http%3A%2F%2Fkernel.ubuntu.com%2F~cking%2Fstress-ng%2F&f=false)
* [Advanced concepts and tools for renewable energy supply of Data Centres](https://www.riverpublishers.com/pdf/ebook/RP_9788793519411.pdf)
* [Monitoring and Modelling Open Compute Servers](http://staff.www.ltu.se/~damvar/Publications/Eriksson%20et%20al.%20-%202017%20-%20Monitoring%20and%20Modelling%20Open%20Compute%20Servers.pdf)
* [Experimental and numerical analysis for potential heat reuse in liquid cooled data centres](http://personals.ac.upc.edu/jguitart/HomepageFiles/ECM16.pdf)
* [Modeling and Analysis of Performance under Interference in the Cloud](https://www3.cs.stonybrook.edu/~anshul/mascots17.pdf)
* [Effectively Measure and Reduce Kernel Latencies for Real time Constraints](https://elinux.org/images/a/a9/ELC2017-_Effectively_Measure_and_Reduce_Kernel_Latencies_for_Real-time_Constraints_%281%29.pdf)
* [Monitoring and Analysis of CPU load relationships between Host and Guests in a Cloud Networking Infrastructure](http://www.diva-portal.org/smash/get/diva2:861235/FULLTEXT02)
* [Measuring the impacts of the Preempt-RT patch](http://events17.linuxfoundation.org/sites/events/files/slides/rtpatch.pdf)
* [Reliable Library Identification Using VMI Techniques](https://rp.os3.nl/2016-2017/p64/report.pdf)
* [Elastic-PPQ: A two-level autonomic system for spatial preference query processing over dynamic data stream](https://www.researchgate.net/publication/319613604_Elastic-PPQ_A_two-level_autonomic_system_for_spatial_preference_query_processing_over_dynamic_data_streams)
* [OpenEPC integration within 5GTN as an NFV proof of concept](http://jultika.oulu.fi/files/nbnfioulu-201706082638.pdf)
* [Time-Aware Dynamic Binary Instrumentation](https://uwspace.uwaterloo.ca/bitstream/handle/10012/12182/Arafa_Pansy.pdf?sequence=3)
* [Experience Report: Log Mining using Natural Language Processing and Application to Anomaly Detection](https://hal.laas.fr/hal-01576291/document)
* [Mixed time-criticality process interferences characterization on a multicore Linux system](https://re.public.polimi.it/retrieve/handle/11311/1033069/292404/paper-accepted-version.pdf)
* [Cloud Orchestration at the Level of Application](https://ec.europa.eu/research/participants/documents/downloadPublic/RFpPOGljenYrclUyK3N5eFN4NnVVZEJpVEl3TTAxcFhXRzRGaXdzN2dmSjBycjNIbXB6dlJ3PT0=/attachment/VFEyQTQ4M3ptUWQ4VDN5UWNDYVZ0UEVSWSt2REhrV1Q=)

2018:
* [Multicore Emulation on Virtualised Environment](https://indico.esa.int/event/165/contributions/1230/attachments/1195/1412/04b_-_Multicore_-_Presentation.pdf)
* [Stress-SGX : Load and Stress your Enclaves for Fun and Profit](https://seb.vaucher.org/papers/stress-sgx.pdf)
* [quiho: Automated Performance Regression Testing Using Inferred Resource Utilization Profiles](https://dl.acm.org/citation.cfm?id=3184422&dl=ACM&coll=DL&preflayout=flat)
* [Hypervisor and Virtual Machine Memory Optimization Analysis](http://dspace.ut.ee/bitstream/handle/10062/60705/Viitkar_BSc2018.pdf)
* [Real-Time testing with Fuego](https://elinux.org/images/4/43/ELC2018_Real-time_testing_with_Fuego-181024m.pdf)
* [FECBench: An Extensible Framework for Pinpointing Sources of Performance Interference in the Cloud-Edge Resource Spectrum](https://www.academia.edu/68455840/FECBench_An_Extensible_Framework_for_Pinpointing_Sources_of_Performance_Interference_in_the_Cloud_Edge_Resource_Spectrum)
* [Quantifying the Interaction Between Structural Properties of Software and Hardware in the ARM Big.LITTLE Architecture](https://research.abo.fi/ws/files/26568716/QuantifyingInteraction.pdf)
* [RAPL in Action: Experiences in Using RAPL for Power Measurements](https://dl.acm.org/doi/10.1145/3177754)

2019:
* [Performance Isolation of Co-located Workload in a Container-based Vehicle Software Architecture](https://www.thinkmind.org/articles/ambient_2019_2_20_40020.pdf)
* [Analysis and Detection of Cache-Based Exploits](https://ssg.lancs.ac.uk/wp-content/uploads/2020/07/analysis_and_detection_vateva.pdf)
* [kMVX: Detecting Kernel Information Leaks with Multi-variant Execution](https://research.vu.nl/ws/files/122357910/KMVX.pdf)
* [Scalability of Kubernetes Running Over AWS](https://www.diva-portal.org/smash/get/diva2:1367111/FULLTEXT02)
* [A study on performance measures for auto-scaling CPU-intensive containerized applications](https://link.springer.com/article/10.1007/s10586-018-02890-1)
* [Scavenger: A Black-Box Batch Workload Resource Manager for Improving Utilization in Cloud Environments](https://www3.cs.stonybrook.edu/~sjavadi/files/javadi_socc2019.pdf)
* [Estimating Cloud Application Performance Based on Micro-Benchmark Profiling](https://core.ac.uk/download/pdf/198051426.pdf)

2020:
* [Performance and Energy Trade-Offs for Parallel Applications on Heterogeneous Multi-Processing Systems](https://www.mdpi.com/1996-1073/13/9/2409/htm)
* [C-Balancer: A System for Container Profiling and Scheduling](https://arxiv.org/pdf/2009.08912.pdf)
* [Modelling VM Latent Characteristics and Predicting Application Performance using Semi-supervised Non-negative Matrix Factorization](https://ieeexplore.ieee.org/document/9284328)
* [Semi-dynamic load balancing: efficient distributed learning in non-dedicated environments](https://dl.acm.org/doi/10.1145/3419111.3421299)
* [A Performance Analysis of Hardware-assisted Security Technologies](https://openscholarship.wustl.edu/cgi/viewcontent.cgi?article=1556&context=eng_etds)
* [Green Cloud Software Engineering for Big Data Processing](https://eprints.leedsbeckett.ac.uk/id/eprint/7294/1/GreenCloudSoftwareEngineeringForBigDataProcessingPV-KOR.pdf)
* [Real-Time Detection for Cache Side Channel Attack using Performance Counter Monitor](https://www.proquest.com/docview/2533920884)
* [Subverting Linux’ Integrity Measurement Architecture](https://svs.informatik.uni-hamburg.de/publications/2020/2020-08-27-Bohling-IMA.pdf)
* [Real-time performance assessment using fast interrupt request on a standard Linux kernel](https://onlinelibrary.wiley.com/doi/full/10.1002/eng2.12114)
* [Low Energy Consumption on Post-Moore Platforms for HPC Research](https://revistas.usfq.edu.ec/index.php/avances/article/download/2108/2919/18081)
* [Managing Latency in Edge-Cloud Environment](https://s2group.cs.vu.nl/files/pubs/2020-JSS-IG-Edge_Cloud.pdf)
* [Demystifying the Real-Time Linux Scheduling Latency](https://bristot.me/files/research/papers/ecrts2020/deOliveira2020Demystifying.pdf)

2021:
* [Streamline: A Fast, Flushless Cache Covert-Channel Attack by Enabling Asynchronous Collusion](https://dl.acm.org/doi/pdf/10.1145/3445814.3446742)
* [Experimental Analysis in Hadoop MapReduce: A Closer Look at Fault Detection and Recovery Techniques](https://www.mdpi.com/1131714)
* [Performance Characteristics of the BlueField-2 SmartNIC](https://arxiv.org/pdf/2105.06619.pdf)
* [Evaluating Latency in Multiprocessing Embedded Systems for the Smart Grid](https://www.mdpi.com/1996-1073/14/11/3322)
* [Work-in-Progress: Timing Diversity as a Protective Mechanism](https://dl.acm.org/doi/pdf/10.1145/3477244.3477614)
* [Sequential Deep Learning Architectures for Anomaly Detection in Virtual Network Function Chains](https://arxiv.org/pdf/2109.14276.pdf)
* [WattEdge: A Holistic Approach for Empirical Energy Measurements in Edge Computing](https://www.researchgate.net/publication/356342806_WattEdge_A_Holistic_Approach_for_Empirical_Energy_Measurements_in_Edge_Computing)
* [PTEMagnet: Fine-Grained Physical Memory Reservation for Faster Page Walks in Public Clouds](https://www.pure.ed.ac.uk/ws/portalfiles/portal/196157550/PTEMagnet_MARGARITOV_DOA19112020_AFV.pdf)
* [The Price of Meltdown and Spectre: Energy Overhead of Mitigations at Operating System Level](https://www4.cs.fau.de/Publications/2021/herzog_2021_eurosec.pdf)
* [An Empirical Study of Thermal Attacks on Edge Platforms](https://digitalcommons.kennesaw.edu/cgi/viewcontent.cgi?article=1590&context=undergradsymposiumksu)
* [Sage: Practical & Scalable ML-Driven Performance Debugging in Microservices](https://people.csail.mit.edu/delimitrou/papers/2021.asplos.sage.pdf)
* [A Generalized Approach For Practical Task Allocation Using A MAPE-K Control Loop](https://www.marquez-barja.com/images/papers/A_Generalized_Approach_For_Software_Placement_In_The_Fog_Using_A_MAPE_K_Control_Loop-AuthorVersion.pdf)]
* [Towards Independent Run-time Cloud Monitoring](https://research.spec.org/icpe_proceedings/2021/companion/p21.pdf)
* [FIRESTARTER 2: Dynamic Code Generation for Processor Stress Tests](https://tu-dresden.de/zih/forschung/ressourcen/dateien/projekte/firestarter/FIRESTARTER-2-Dynamic-Code-Generation-for-Processor-Stress-Tests.pdf?lang=en)
* [Performance comparison between a Kubernetes cluster and an embedded system](http://www.diva-portal.se/smash/get/diva2:1569829/FULLTEXT01.pdf)
* [Performance Exploration of Virtualization Systems](https://www.researchgate.net/publication/350061536_Performance_Exploration_of_Virtualization_Systems)
* [Tricking Hardware into Efficiently Securing Software](https://research.vu.nl/ws/portalfiles/portal/120740500/983666.pdf)

2022:
* [A general method for evaluating the overhead when consolidating servers: performance degradation in virtual machines and containers](https://link.springer.com/article/10.1007/s11227-022-04318-5)
* [FedComm: Understanding Communication Protocols for Edge-based Federated Learning](https://arxiv.org/pdf/2208.08764.pdf)
* [Achieving Isolation in Mixed-Criticality Industrial Edge Systems with Real-Time Containers](https://drops.dagstuhl.de/opus/volltexte/2022/16332/pdf/LIPIcs-ECRTS-2022-15.pdf)
* [Design and Implementation of Machine Learning-Based Fault Prediction System in Cloud Infrastructure](https://www.mdpi.com/2079-9292/11/22/3765)
* [The TSN Building Blocks in Linux](https://arxiv.org/pdf/2211.14138.pdf)
* [uKharon: A Membership Service for Microsecond Applications](https://www.usenix.org/system/files/atc22-guerraoui.pdf)
* [Evaluating Secure Enclave Firmware Development for Contemporary RISC-V WorkstationsContemporary RISC-V Workstation](https://scholar.afit.edu/cgi/viewcontent.cgi?article=6319&context=etd)
* [Evaluation of Real-Time Linux on RISC-V processor architecture](https://trepo.tuni.fi/bitstream/handle/10024/138547/J%C3%A4mb%C3%A4ckMarkus.pdf)
* [Hertzbleed: Turning Power Side-Channel Attacks Into Remote Timing Attacks on x86](https://www.hertzbleed.com/hertzbleed.pdf)
* [Don’t Mesh Around: Side-Channel Attacks and Mitigations on Mesh Interconnects](https://www.cs.cmu.edu/~rpaccagn/papers/dont-mesh-around-usenix2022.pdf)
* [Performance Implications for Multi-Core RISC-V Systems with Dedicated Security Hardware](https://papers.academic-conferences.org/index.php/iccws/article/download/56/55/96)

2023:
* [Fight Hardware with Hardware: System-wide Detection and Mitigation of Side-Channel Attacks using Performance Counters](https://dl.acm.org/doi/10.1145/3519601)
* [Introducing k4.0s: a Model for Mixed-Criticality Container Orchestration in Industry 4.0 ](https://arxiv.org/pdf/2205.14188.pdf)
* [A Comprehensive Study on Optimizing Systems with Data Processing Units](https://arxiv.org/pdf/2301.06070.pdf)
* [Estimating Cloud Application Performance Based on Micro-Benchmark Profiling](https://research.chalmers.se/publication/506903/file/506903_Fulltext.pdf)
* [PSPRAY: Timing Side-Channel based Linux Kernel Heap Exploitation Technique](https://lifeasageek.github.io/papers/yoochan-pspray.pdf)
* [Robust and accurate performance anomaly detection and prediction for cloud applications: a novel ensemble learning-based framework](https://journalofcloudcomputing.springeropen.com/articles/10.1186/s13677-022-00383-6#Fn4)
* [Feasibility Study for a Python-Based Embedded Real-Time Control System](https://www.mdpi.com/2079-9292/12/6/1426)
* [Adaptation of Parallel SaaS to Heterogeneous Co-Located Cloud Resources](https://www.mdpi.com/2076-3417/13/8/5115#B56-applsci-13-05115)
* [A Methodology and Framework to Determine the Isolation Capabilities of Virtualisation Technologies](https://dl.acm.org/doi/pdf/10.1145/3578244.3583728)
* [Data Station: Delegated, Trustworthy, and Auditable Computation to Enable Data-Sharing Consortia with a Data Escrow](https://arxiv.org/pdf/2305.03842.pdf)
* [An Empirical Study of Resource-Stressing Faults in Edge-Computing Applications](https://dl.acm.org/doi/pdf/10.1145/3578354.3592873)
* [Finding flaky tests in JavaScript applications using stress and test suite reordering](https://repositories.lib.utexas.edu/handle/2152/120282)
* [The Power of Telemetry: Uncovering Software-Based Side-Channel Attacks on Apple M1/M2 Systems](https://arxiv.org/pdf/2306.16391.pdf)
* [A Performance Evaluation of Embedded Multi-core Mixed-criticality System Based on PREEMPT RT Linux](https://www.jstage.jst.go.jp/article/ipsjjip/31/0/31_78/_pdf)
* [Data Leakage in Isolated Virtualized Enterprise Computing SystemsSystems](https://scholar.smu.edu/cgi/viewcontent.cgi?article=1034&context=engineering_compsci_etds)
* [Considerations for Benchmarking Network Performance in Containerized Infrastructures](https://datatracker.ietf.org/doc/draft-dcn-bmwg-containerized-infra/)
* [EnergAt: Fine-Grained Energy Attribution for Multi-Tenancy](https://hotcarbon.org/2023/pdf/a4-he.pdf)
* [Quantifying the Security Profile of Linux Applications](https://dl.acm.org/doi/10.1145/3609510.3609814)
* [Gotham Testbed: a Reproducible IoT Testbed for Security Experiments and Dataset Generation](https://arxiv.org/pdf/2207.13981.pdf)
* [Profiling with Trust: System Monitoring from Trusted Execution Environments](https://assets.researchsquare.com/files/rs-3169665/v1_covered_63751076-8387-429e-8296-3f3cc4c3ed34.pdf?c=1689832627)
* [Thermal-Aware on-Device Inference Using Single-Layer Parallelization with Heterogeneous Processors](https://www.sciopen.com/article/pdf/10.26599/TST.2021.9010075.pdf)
* [Towards Fast, Adaptive, and Hardware-Assisted User-Space Scheduling](https://arxiv.org/pdf/2308.02896.pdf)
* [Heterogeneous Anomaly Detection for Software Systems via Semi-supervised Cross-modal Attention](https://arxiv.org/pdf/2302.06914.pdf)
* [Green coding : an empirical approach to harness the energy consumption ofsoftware services](https://theses.hal.science/tel-04074973/document)
* [Enhancing Empirical Software Performance Engineering Research with Kernel-Level Events: A Comprehensive System Tracing Approach](https://papers.ssrn.com/sol3/papers.cfm?abstract_id=4556983)
* [Cloud White: Detecting and Estimating QoS Degradation of Latency-Critical Workloads in the Public Cloud](https://www.sciencedirect.com/science/article/pii/S0167739X22002734)
* [Dynamic Resource Management for Cloud-native Bulk Synchronous Parallel Applications](http://www.dre.vanderbilt.edu/~gokhale/WWW/papers/ISORC23_BSP.pdf)
* [Towards Serverless Optimization with In-place Scaling](https://arxiv.org/pdf/2311.09526.pdf)
* [A Modular Approach to Design an Experimental Framework for Resource Management Research](https://assets.researchsquare.com/files/rs-3400308/v1_covered_90a108f8-065c-4e38-9c56-990424f66afd.pdf?c=1697164725)
* [Targeted Deanonymization via the Cache Side Channel: Attacks and Defenses](https://leakuidatorplusteam.github.io/preprint.pdf)
* [Validating Full-System RISC-V Simulator: A Systematic Approach](https://infoscience.epfl.ch/record/302433)
* [Lightweight Implementation of Per-packet Service Protection in eBPF/XDP](https://arxiv.org/pdf/2312.07152.pdf)
* [ROS2 Real-time Performance Optimization and Evaluation](https://cjme.springeropen.com/articles/10.1186/s10033-023-00976-5)
* [Experimental and numerical analysis of the thermal behaviour of a single-phase immersion-cooled data centre](https://www.sciencedirect.com/science/article/pii/S1359431123012899)

2024:
* [IdleLeak: Exploiting Idle State Side Effects for Information Leakage](https://www.sicheroder.net/files/idleleak.pdf)
* [Profiling with trust: system monitoring from trusted execution environments](https://www.researchgate.net/publication/378265673_Profiling_with_trust_system_monitoring_from_trusted_execution_environments)
* [Exemplary Determination of Cgroups-Based QoS Isolation for a Database Workload](https://dl.acm.org/doi/pdf/10.1145/3629527.3652267)
* [BARO: Robust Root Cause Analysis for Microservices via Multivariate Bayesian Online Change Point Detection](https://arxiv.org/pdf/2405.09330)
* [Disambiguating Performance Anomalies from Workload Changes in Cloud-Native Applications](https://dl.acm.org/doi/pdf/10.1145/3629526.3645046)
* [Take a Step Further: Understanding Page Spray in Linux Kernel Exploitation](https://arxiv.org/html/2406.02624v2)

I am keen to add to the stress-ng project page any citations to research or
projects that use stress-ng.  I also appreciate information concerning kernel
bugs or performance regressions found with stress-ng.

## Contributors

Many thanks to the following contributors to stress-ng (in alphabetical order):

Abdul Haleem, Aboorva Devarajan, Adriand Martin, Adrian Ratiu,
Aleksandar N. Kostadinov, Alexander Kanavin, Alexandru Ardelean,
Alfonso Sánchez-Beato, Allen H, Amit Singh Tomar, Andrey Gelman,
André Wild, Anisse Astier, Anton Eliasson, Arjan van de Ven,
Baruch Siach, Bryan W. Lewis, Camille Constans, Carlos Santos,
Christian Ehrhardt, Christopher Brown, Chunyu Hu, Daniel Andriesse,
Danilo Krummrich, Davidson Francis, David Turner, Dominik B Czarnota,
Dorinda Bassey, Eder Zulian, Eric Lin, Erik Stahlman, Erwan Velu,
Fabien Malfoy, Fabrice Fontaine, Fernand Sieber, Florian Weimer,
Francis Laniel, Guilherme Janczak, Hui Wang, Hsieh-Tseng Shen,
Iyán Méndez Veiga, James Hunt, Jan Luebbe, Jianshen Liu, John Kacur,
Jules Maselbas, Julien Olivain, Kenny Gong, Khalid Elmously, Khem Raj,
Luca Pizzamiglio, Luis Chamberlain, Luis Henriques, Matthew Tippett,
Mauricio Faria de Oliveira, Maxime Chevallier, Max Kellermann,
Maya Rashish, Mayuresh Chitale, Meysam Azad, Mike Koreneff,
Munehisa Kamata, Myd Xia, Nick Hanley, Nikolas Kyx, Paul Menzel,
Piyush Goyal, Ralf Ramsauer, Rosen Penev, Rulin Huang, Sascha Hauer,
Sergey Matyukevich, Siddhesh Poyarekar, Shoily Rahman, Stian Onarheim,
Thadeu Lima de Souza Cascardo, Thia Wyrod, Thinh Tran, Tim Gardner,
Tim Gates, Tim Orling, Tommi Rantala, Witold Baryluk, Yiwei Lin,
Yong-Xuan Wang, Zhiyi Sun.
