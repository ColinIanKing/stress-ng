# stress-ng

stress-ng will stress test a computer system in various selectable ways. It
was designed to exercise various physical subsystems of a computer as well as
the various operating system kernel interfaces. Stress-ng features:

  * Over 650 stress tests
  * 85+ CPU specific stress tests that exercise floating point, integer,
    bit manipulation and control flow
  * over 20 virtual memory stress tests
  * portable: builds on Linux, Solaris, *BSD, Minix, Android, MacOS X,
    GNU/Hurd, Haiku, Windows Subsystem for Linux and SunOs/Dilos with
    gcc, clang, tcc and pcc.
  * tested on x86-64, i386, s390x, ppc64el, armhf, arm64, sparc64, risc-v,
    m68k, mips64

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

NOTE: the build will try to detect build dependencies and will build an image
with functionality disabled if the support libraries are not installed.

At build-time stress-ng will detect kernel features that are available on the
target build system and enable stress tests appropriately. Stress-ng has been
build-tested on Ubuntu, Debian, Debian GNU/Hurd, Slackware, RHEL, SLES, Centos,
kFreeBSD, OpenBSD, NetBSD, FreeBSD, Debian kFreeBSD, DragonFly BSD, OS X, Minix,
Solaris 11.3, OpenIndiana and Hiaku. Ports to other POSIX/UNIX like operating
systems should be relatively easy.


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

## Contributing to stress-ng:

Send patches to colin.i.king@gmail.com or merge requests at
https://github.com/ColinIanKing/stress-ng

## Quick Start Reference Guide
The [Ubuntu stress-ng reference guide](https://wiki.ubuntu.com/Kernel/Reference/stress-ng)
contains a brief overview and worked examples.

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
* [sparc64: locking/atomic, kernel OOPS on running stress-ng](https://lore.kernel.org/lkml/CADxRZqzcrnSMzy50T+kWb_mQVguWDCMu6RoXsCc+-fNDPYXbaw@mail.gmail.com/#r)
* [btrfs: fix exhaustion of the system chunk array due to concurrent allocations](https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/commit/?id=986aa0f276752ca4809f95b260f59fafef01a6a7)
* [btrfs: rework chunk allocation to avoid exhaustion of the system chunk array](https://git.kernel.org/cgit/linux/kernel/git/torvalds/linux.git/commit/?id=79bd37120b149532af5b21953643ed74af69654f)
* [btrfs: fix deadlock with concurrent chunk allocations involving system chunks](https://git.kernel.org/cgit/linux/kernel/git/torvalds/linux.git/commit/?id=1cb3db1cf383a3c7dbda1aa0ce748b0958759947)
* [locking/atomic: sparc: Fix arch_cmpxchg64_local()](https://git.kernel.org/cgit/linux/kernel/git/torvalds/linux.git/commit/?id=7e1088760cfe0bb1fdb1f0bd155bfd52f080683a)
* [pipe: do FASYNC notifications for every pipe IO, not just state changes](https://git.kernel.org/cgit/linux/kernel/git/torvalds/linux.git/commit/?id=fe67f4dd8daa252eb9aa7acb61555f3cc3c1ce4c)
* [io-wq: remove GFP_ATOMIC allocation off schedule out path](https://git.kernel.org/cgit/linux/kernel/git/torvalds/linux.git/commit/?id=d3e9f732c415cf22faa33d6f195e291ad82dc92e)
* [mm/swap: consider max pages in iomap_swapfile_add_extent](https://git.kernel.org/cgit/linux/kernel/git/torvalds/linux.git/commit/?id=36ca7943ac18aebf8aad4c50829eb2ea5ec847df)

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

I am keen to add to the stress-ng project page any citations to research or
projects that use stress-ng.  I also appreciate information concerning kernel
bugs or performance regressions found with stress-ng.

