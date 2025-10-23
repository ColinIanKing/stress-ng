# stress-ng (stress next generation)

<a href="https://repology.org/project/stress-ng/versions">
    <img src="https://repology.org/badge/vertical-allrepos/stress-ng.svg" alt="Packaging status" align="right">
</a>

stress-ng will stress test a computer system in various selectable ways. It
was designed to exercise various physical subsystems of a computer as well as
the various operating system kernel interfaces. Stress-ng features:

  * 360+ stress tests
  * 100+ CPU specific stress tests that exercise floating point, integer,
    bit manipulation and control flow
  * 60+ virtual memory stress tests
  * 80+ file system stress tests
  * 50+ memory/CPU cache stress tests
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

  * gcc g++ libacl1-dev libaio-dev libapparmor-dev libatomic1 libattr1-dev libbsd-dev libcap-dev libeigen3-dev libgbm-dev libcrypt-dev libglvnd-dev libipsec-mb-dev libjpeg-dev libjudy-dev libkeyutils-dev libkmod-dev libmd-dev libmpfr-dev libsctp-dev libxxhash-dev liblzma-dev zlib1g-dev

RHEL, Fedora, Centos:

  * gcc g++ eigen3-devel Judy-devel keyutils-libs-devel kmod-devel libacl-devel libaio-devel libatomic libattr-devel libbsd-devel libcap-devel libgbm-devel libcrypt-devel libglvnd-core-devel libglvnd-devel libjpeg-devel libmd-devel mpfr-devel libX11-devel libXau-devel libxcb-devel lksctp-tools-devel xorg-x11-proto-devel xxhash-devel zlib-devel

RHEL, Fedora, Centos (static builds):

  * gcc g++ eigen3-devel glibc-static Judy-devel keyutils-libs-devel libacl-devel libaio-devel libatomic-static libattr-devel libbsd-devel libcap-devel libgbm-devel libcrypt-devel libglvnd-core-devel libglvnd-devel libjpeg-devel libmd-devel libX11-devel libXau-devel libxcb-devel lksctp-tools-devel mpfr-devel xorg-x11-proto-devel xxhash-devel zlib-devel

SUSE:
  * gcc gcc-c++ eigen3-devel keyutils-devel libaio-devel libapparmor-devel libatomic1 libattr-devel libbsd-devel libcap-devel libgbm-devel libglvnd-devel libjpeg-turbo libkmod-devel libmd-devel libseccomp-devel lksctp-tools-devel mpfr-devel xxhash-devel zlib-devel

ClearLinux:
  * devpkg-acl devpkg-eigen devpkg-Judy devpkg-kmod devpkg-attr devpkg-libbsd devpkg-libjpeg-turbo devpkg-libsctp devpkg-mesa

Alpine Linux:
  * build-base eigen-dev jpeg-dev judy-dev keyutils-dev kmod-dev libacl-dev libaio-dev libatomic libattr libbsd-dev libcap-dev libmd-dev libseccomp-dev lksctp-tools-dev mesa-dev mpfr-dev xxhash-dev zlib-dev

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

To build on MINIX, gmake, binutils and clang are required:
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

To build on Haiku R1/beta5:
```
    # GCC
    make clean
    make
    # Clang
    CC=clang make clean
    CC=clang make
```

To build a static image (example, for Android), use:
```
# path to Android NDK
# get NDK from https://developer.android.com/ndk/downloads
    export NDK=$HOME/android-ndk-r27c
    export PATH=$PATH:$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin
    export TARGET=aarch64-linux-android
    # Define Android API level
    export API=27
    export CC=$TARGET$API-clang

    make clean
    STATIC=1 make
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

To generate a PDF version of the manual (requires ps2pdf to be installed)
```
    make pdf
```

Build option: DEBUG, build with debug (-g) enabled:
```
    make clean
    DEBUG=1 make
```

Build option: LTO, Link Time Optimization (~1-2% performance improvement on compute stressors):
```
    make clean
    LTO=1 make
```

Build option: PEDANTIC, enable pedantic build flags:
```
    make clean
    PEDANTIC=1 make
```

Build option: GARBAGE_COLLECT, warn of unused code:
```
    make clean
    GARBAGE_COLLECT=1 make
```

Build option: UNEXPECTED=1, warn of unexpected #ifdef'd out code:
```
    make clean
    UNEXPECTED=1 make
```

Build option: SOURCE_DATE_EPOCH=seconds since epoch, add build date
```
    make clean
    SOURCE_DATE_EPOCH=1750685870 make
```

Build option: EXTRA_BUILDINFO=1, add CFLAGS, CXXFLAGS and LDFLAGS to --buildinfo option
NOTE: This can lead to build information being leaked and is not recommended for any distro releases.
```
    make clean
    EXTRA_BUILDINFO=1 make -j 10
```

## Contributing to stress-ng:

Send patches to colin.i.king@gmail.com or merge requests at
https://github.com/ColinIanKing/stress-ng

## Quick Start Reference Guide
The [Ubuntu stress-ng reference guide](https://wiki.ubuntu.com/Kernel/Reference/stress-ng)
contains a brief overview and worked examples.

## Examples

Run 8 CPU stressors for 60 seconds:
```
stress-ng --cpu 8 --timeout 60
stress-ng: info:  [184401] setting to a 1 min run per stressor
stress-ng: info:  [184401] dispatching hogs: 8 cpu
stress-ng: info:  [184401] skipped: 0
stress-ng: info:  [184401] passed: 8: cpu (8)
stress-ng: info:  [184401] failed: 0
stress-ng: info:  [184401] metrics untrustworthy: 0
stress-ng: info:  [184401] successful run completed in 1 min
```

Run 8 CPU stressors for 2 minutes, just using the square root CPU stressor method and show compute metrics:
```
stress-ng --cpu 8 --timeout 2m --cpu-method sqrt --metrics
stress-ng: info:  [184135] setting to a 2 mins run per stressor
stress-ng: info:  [184135] dispatching hogs: 8 cpu
stress-ng: metrc: [184135] stressor       bogo ops real time  usr time  sys time   bogo ops/s     bogo ops/s CPU used per       RSS Max
stress-ng: metrc: [184135]                           (secs)    (secs)    (secs)   (real time) (usr+sys time) instance (%)          (KB)
stress-ng: metrc: [184135] cpu             1531429    120.00    916.87      0.28     12762.02        1669.78        95.54          3148
stress-ng: info:  [184135] skipped: 0
stress-ng: info:  [184135] passed: 8: cpu (8)
stress-ng: info:  [184135] failed: 0
stress-ng: info:  [184135] metrics untrustworthy: 0
stress-ng: info:  [184135] successful run completed in 2 mins
```

Run 8 CPU stressors for 60 seconds and report thermal zone temperatures
```
stress-ng --cpu 8 --timeout 60 --tz
stress-ng: info:  [184291] setting to a 1 min run per stressor
stress-ng: info:  [184291] dispatching hogs: 8 cpu
stress-ng: info:  [184291] cpu:
stress-ng: info:  [184291]  B0D4                   96.05 C (369.20 K)
stress-ng: info:  [184291]  INT3400_Thermal        20.00 C (293.15 K)
stress-ng: info:  [184291]  SEN1                   41.05 C (314.20 K)
stress-ng: info:  [184291]  acpitz                 96.00 C (369.15 K)
stress-ng: info:  [184291]  iwlwifi_1              28.00 C (301.15 K)
stress-ng: info:  [184291]  pch_skylake            62.50 C (335.65 K)
stress-ng: info:  [184291]  x86_pkg_temp           73.25 C (346.40 K)
stress-ng: info:  [184291] skipped: 0
stress-ng: info:  [184291] passed: 8: cpu (8)
stress-ng: info:  [184291] failed: 0
stress-ng: info:  [184291] metrics untrustworthy: 0
stress-ng: info:  [184291] successful run completed in 1 min
```

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
sudo stress-ng --branch 1 --perf -t 10 | grep Branch
stress-ng: info:  [1171714]                604,703,327 Branch Instructions            53.30 M/sec
stress-ng: info:  [1171714]                598,760,234 Branch Misses                  52.77 M/sec (99.02%)
```

Run permutations of I/O stressors on a ZFS file system, excluding the rawdev stressor with kernel log error checking:
```
stress-ng --class io --permute 0 -x rawdev -t 1m --vmstat 1 --klog-check  --temp-path /zfs-pool/test
```

x86 only: measure power using the RAPL interfaces on 8 concurrent 3D matrix stressors with verification enabled.
Note that reading RAPL requires root permission.

```
sudo stress-ng --matrix-3d 8 --matrix-3d-size 512 --rapl -t 10 --verify
stress-ng: info:  [4563] setting to a 10 secs run per stressor
stress-ng: info:  [4563] dispatching hogs: 8 matrix-3d
stress-ng: info:  [4563] matrix-3d:
stress-ng: info:  [4563]  core                     6.11 W
stress-ng: info:  [4563]  dram                     2.71 W
stress-ng: info:  [4563]  pkg-0                    8.20 W
stress-ng: info:  [4563]  psys                    16.90 W
stress-ng: info:  [4563]  uncore                   0.06 W
stress-ng: info:  [4563] skipped: 0
stress-ng: info:  [4563] passed: 8: matrix-3d (8)
stress-ng: info:  [4563] failed: 0
stress-ng: info:  [4563] metrics untrustworthy: 0
stress-ng: info:  [4563] successful run completed in 11.38 secs
```

Measure C-state residency:
```
stress-ng --intmath 0 -t 1m --c-states
stress-ng: info:  [6998] setting to a 1 min run per stressor
stress-ng: info:  [6998] dispatching hogs: 8 intmath
stress-ng: info:  [6998] intmath:
stress-ng: info:  [6998]  C0     99.98%
stress-ng: info:  [6998]  C1      0.00%
stress-ng: info:  [6998]  C1E     0.01%
stress-ng: info:  [6998]  C3      0.00%
stress-ng: info:  [6998]  C6      0.01%
stress-ng: info:  [6998]  C7s     0.00%
stress-ng: info:  [6998]  C8      0.00%
stress-ng: info:  [6998]  POLL    0.00%
stress-ng: info:  [6998] skipped: 0
stress-ng: info:  [6998] passed: 8: intmath (8)
stress-ng: info:  [6998] failed: 0
stress-ng: info:  [6998] metrics untrustworthy: 0
stress-ng: info:  [6998] successful run completed in 1 min
```

Run all scheduler stressors with 64 instances per stressor for 5 minutes per stressor, show progress and enable test verification:
```
stress-ng --seq 64 --class scheduler -t 5m --progress --verify
```

Run permutations of all the vector stressors, 8 instances of each, 5 seconds per permutation, show progress and enable test verification
and enable verbose mode for output:
```
stress-ng --class vector --permute 8 -t 5 --progress --verify -v
```

Run as root two instances of the virtual memory stressor, use 95% of available memory, log results to example.log, check kernel log for errors,
run for 1 hour, show virtual memory statistics every 10 seconds and verify memort tests:
```
sudo stress-ng --log-file example.log --vm 2 --vm-bytes 95% --klog-check -v -t 1h  --vmstat 10 --verify
```

## Bugs and regressions found with stress-ng

stress-ng has found various Kernel, QEMU bugs/regressions, and libc bugs; appropriate fixes have been landed to address these issues:

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
* [MultiVM - L2 guest(s) running stress-ng getting stuck at booting after triggering crash](https://bugs.launchpad.net/ubuntu/+source/linux/+bug/2077722)
* [powerpc/qspinlock: Fix deadlock in MCS queue](https://git.kernel.org/cgit/linux/kernel/git/torvalds/linux.git/commit/?id=734ad0af3609464f8f93e00b6c0de1e112f44559)
* [kernel regression with ext4 and ea_inode mount flags and exercising xattrs](https://bugs.launchpad.net/linux/+bug/2080853)
* [sched_ext: TASK_DEAD tasks must be switched into SCX on ops_enable](https://git.kernel.org/cgit/linux/kernel/git/torvalds/linux.git/commit/?id=a8532fac7b5d27b8d62008a89593dccb6f9786ef)
* [sched/deadline: Fix task_struct reference leak](https://git.kernel.org/cgit/linux/kernel/git/torvalds/linux.git/commit/?id=b58652db66c910c2245f5bee7deca41c12d707b9)
* [sched_ext: Split the global DSQ per NUMA node](https://git.kernel.org/cgit/linux/kernel/git/torvalds/linux.git/commit/?id=b7b3b2dbae73b412c2d24b3d0ebf1110991e4510)
* [calling getcpu with misaligned address causes kernel panic](https://bugzilla.kernel.org/show_bug.cgi?id=219339)
* [cygwin: pread/pwrite: prevent EBADF error after fork()](https://sourceware.org/pipermail/cygwin-patches/2024q3/012793.html)
* [cygwin 3.5.4-1: signal handling destroys 'long double' values](https://sourceware.org/pipermail/cygwin/2024-October/256503.html)
* [cygwin: timer_delete: Fix return value](https://sourceware.org/pipermail/cygwin-patches/2024q4/012803.html)
* [cygwin: change pthread_sigqueue() to accept thread id](https://cygwin.com/git/?p=newlib-cygwin.git;a=commit;h=1e8c92e21d386d2e4a29fa92e8258979ff19ae6b)
* [security/keys: fix slab-out-of-bounds in key_task_permission](https://git.kernel.org/cgit/linux/kernel/git/torvalds/linux.git/commit/?4a74da044ec9ec8679e6beccc4306b936b62873f)
* [sched_ext: Don't hold scx_tasks_lock for too long](https://git.kernel.org/cgit/linux/kernel/git/torvalds/linux.git/commit/?b07996c7abac0fe3f70bf74b0b3f76eb7852ef5a)
* [sched/numa: Fix the potential null pointer dereference in task_numa_work()](https://git.kernel.org/cgit/linux/kernel/git/torvalds/linux.git/commit/?9c70b2a33cd2aa6a5a59c5523ef053bd42265209)
* [reiserfs panic using fsize stressor](https://bugzilla.kernel.org/show_bug.cgi?id=219497)
* [soft-lockups: mm/page_alloc: add cond_resched in __drain_all_pages()](https://lore.kernel.org/linux-mm/3b000941-b1b6-befa-4ec9-2bff63d557c1@google.com/T/)]
* [can: m_can: fix missed interrupts with m_can_pci](https://git.kernel.org/cgit/linux/kernel/git/torvalds/linux.git/commit/?id=743375f8deee360b0e902074bab99b0c9368d42f)
* [sched/deadline: Fix warning in migrate_enable for boosted tasks](https://git.kernel.org/cgit/linux/kernel/git/torvalds/linux.git/commit/?id=0664e2c311b9fa43b33e3e81429cd0c2d7f9c638)
* [iomap: elide flush from partial eof zero range](https://git.kernel.org/cgit/linux/kernel/git/torvalds/linux.git/commit/?id=fde4c4c3ec1c1590eb09f97f9525fa7dd8df8343)
* [opening /dev/snapshot twice causes oops](https://bugzilla.kernel.org/show_bug.cgi?id=219975)
* [Cygwin: cygwin 3.5.4-1: lockf() aborts on overlap and does not fail on overflow](https://cygwin.com/pipermail/cygwin/2024-October/256528.html)
* [Cygwin: cygwin 3.5.4-1: signal handling destroys 'long double' values](https://cygwin.com/pipermail/cygwin/2024-October/256503.html)
* [Cygwin: Segfault in pthread_sigqueue() or sigtimewait()](https://cygwin.com/pipermail/cygwin/2024-November/256762.html)
* [Cygwin: stress-ng --lockmix 1 crashes with *** fatal error - NtCreateEvent(lock): 0xC0000035](https://cygwin.com/pipermail/cygwin/2024-November/256750.html)
* [Cygwin: SIGKILL may no longer work after many SIGCONT/SIGSTOP signals](https://cygwin.com/pipermail/cygwin/2024-November/256744.html)

2025:
* [Tegra Security Engine driver improvements](https://www.spinics.net/lists/kernel/msg5546707.html)
* [sched_ext: Fix lock imbalance in dispatch_to_local_dsq()](https://git.kernel.org/cgit/linux/kernel/git/torvalds/linux.git/commit/?id=1626e5ef0b00386a4fd083fa7c46c8edbd75f9b4)
* [mm/zswap: fix inconsistency when zswap_store_page() fails](https://git.kernel.org/cgit/linux/kernel/git/torvalds/linux.git/commit/?id=63895d20d63b446f5049a963983489319c2ea3e2)
* [LoongArch: Set max_pfn with the PFN of the last page](https://git.kernel.org/cgit/linux/kernel/git/torvalds/linux.git/commit/?id=c8477bb0a8e7f6b2e47952b403c5cb67a6929e55)
* [net: ipv4: Fix destination address determination in flowi4_init_output](https://lkml.org/lkml/2025/5/8/536)
* [Cygwin: mq_send()/mq_receive() may never return if used from threads](https://cygwin.com/pipermail/cygwin/2025-January/257120.html)
* [Cygwin: mq_send(-1, ...) segfaults instead of failing with EBADF](https://cygwin.com/pipermail/cygwin/2025-January/257090.html)
* [Cygwin: STATUS_HEAP_CORRUPTION if signal arrives when x86 direction flag is set](https://cygwin.com/pipermail/cygwin/2025-March/257704.html)
* [Cygwin: No errno set after too many open("/dev/ptmx", ...)](https://cygwin.com/pipermail/cygwin/2025-March/257786.html)
* [Cygwin: strace: infinite exception c0000005 loop on segmentation fault](https://cygwin.com/pipermail/cygwin/2025-May/258144.html)
* [Cygwin: Hang or crash after multiple SIGILL or SIGSEGV and siglongjmp](https://sourceware.org/pipermail/cygwin/2025-March/257726.html)
* [binder: fix use-after-free in binderfs_evict_inode()](https://git.kernel.org/cgit/linux/kernel/git/torvalds/linux.git/commit/?id=8c0a559825281764061a127632e5ad273f0466ad)
* [MultiVM - L2 guest(s) running stress-ng getting stuck at booting after triggering crash](https://bugs.launchpad.net/ubuntu/+source/linux/+bug/2077722)
* [cifs: Fix null-ptr-deref by static initializing global lock](https://git.kernel.org/cgit/linux/kernel/git/torvalds/linux.git/commit/?id=b0b73329ebeeb727913f07b5b6bb85e66e03d156)
* [Revert "sched_ext: Skip per-CPU tasks in scx_bpf_reenqueue_local()"](https://git.kernel.org/cgit/linux/kernel/git/torvalds/linux.git/commit/?id=0b47b6c3543efd65f2e620e359b05f4938314fbd)
* [mm/mremap: avoid expensive folio lookup on mremap folio pte batch](https://git.kernel.org/cgit/linux/kernel/git/torvalds/linux.git/commit/?id=0b5be138ce00f421bd7cc5a226061bd62c4ab850)

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
* [mm: always inline _compound_head() with CONFIG_HUGETLB_PAGE_OPTIMIZE_VMEMMAP=y](https://git.kernel.org/cgit/linux/kernel/git/torvalds/linux.git/commit/?id=ef5f379de302884b9b7ad9b62587a942a9f0bb55)
* [mm: optimize the redundant loop of mm_update_owner_next()](https://git.kernel.org/cgit/linux/kernel/git/torvalds/linux.git/commit/?id=76ba6acfcce871db13ad51c6dc8f56fec2e92853)

2025:
* [sched: Add unlikey branch hints to several system calls](https://git.kernel.org/cgit/linux/kernel/git/torvalds/linux.git/commit/?id=1a5d3492f8e14719184945893c610e0802c05533)
* [mm/mincore: improve performance by adding an unlikely hint](https://git.kernel.org/cgit/linux/kernel/git/torvalds/linux.git/commit/?id=9fa26fb554baaf71826814804749f5cff130c4d6)
* [select: do_pollfd: add unlikely branch hint return path](https://git.kernel.org/cgit/linux/kernel/git/torvalds/linux.git/commit/?id=5730609ffd7e558e1e3305d0c6839044e8f6591b)
* [select: core_sys_select add unlikely branch hint on return path](https://git.kernel.org/cgit/linux/kernel/git/torvalds/linux.git/commit/?id=6b24a702ecf167ab61456276bb72133d84ccca45)
* [mm: fix the inaccurate memory statistics issue for users](https://git.kernel.org/cgit/linux/kernel/git/torvalds/linux.git/commit/?id=82241a83cd15aaaf28200a40ad1a8b480012edaf)
* [powerpc/defconfigs: Set HZ=1000 on ppc64 and powernv defconfigs](https://git.kernel.org/cgit/linux/kernel/git/torvalds/linux.git/commit/?id=a206d233401208e1c36cf7c66f23a36f91d33de3)

I appreciate information concerning kernel bugs or performance regressions found with stress-ng.

## Presentations

* [Stress-ng presentation at ELCE 2019 Lyon](https://static.sched.com/hosted_files/osseu19/29/Lyon-stress-ng-presentation-oct-2019.pdf)
* [Video of the above presentation](https://www.youtube.com/watch?v=8QaXStKfq3A)
* [Linux Foundation Mentoring Session, May 2022](https://www.youtube.com/watch?v=gD3Hn02VSHA)
* [Kernel Recipes presentation, Sept 2023](https://www.youtube.com/watch?v=PD0NOZCTIVQ)
* [Linux Foundation, ELISA, June 2024](https://www.youtube.com/watch?v=-B1K-xpICtQ)

## Citations

* [Citations](./CITATIONS.md)

## Contributors

Many thanks to the following contributors to stress-ng (in alphabetical order):

Abdul Haleem, Aboorva Devarajan, Adriand Martin, Adrian Ratiu,
Aleksandar N. Kostadinov, Alexander Kanavin, Alexandru Ardelean,
Alfonso Sánchez-Beato, Allen H, Amit Singh Tomar, Andrey Gelman,
André Wild, Anisse Astier, Anton Eliasson, Arjan van de Ven,
Artur Malchanau, Baruch Siach, Bryan W. Lewis, Camille Constans,
Carlos Santos, Christian Ehrhardt, Christian Franke, Christopher Brown,
Chunyu Hu, Daniel Andriesse, Daniel Hodges, Danilo Krummrich,
Davidson Francis, David Turner, Denis Ovsienko, Dmitry Antipov,
Dmitry Grand, Dominik B Czarnota, Dorinda Bassey, Eder Zulian,
Eric Lin, Erik Stahlman, Erwan Velu, Fabien Malfoy,
Fabrice Fontaine, Fernand Sieber, Fejza Indrit, Florian Weimer,
Francis Laniel, Guilherme Janczak, Hui Wang, Hsieh-Tseng Shen,
Iyán Méndez Veiga, Ivan Shapovalov, James Hunt, Jan Luebbe, 
Jesse Huang, Jianshen Liu, Jimmy Ho, Jimmy Durand Wesolowskim,
John Kacur, Julee, Jules Maselbas, Julien Olivain, Kenny Gong,
Khalid Elmously, Khem Raj, Luca Pizzamiglio, Luis Chamberlain,
Luis Henriques, Lukas Durfina, Matteo Italia, Matthew Tippett,
Mauricio Faria de Oliveira, Maxime Chevallier, Max Kellermann,
Maya Rashish, Mayuresh Chitale, Meysam Azad, Mike Koreneff,
Munehisa Kamata, Myd Xia, Nick Hanley, Nicolas Bouton,
Nikolas Kyx, Paul Menzel, Pierre Ducroquet, Piyush Goyal,
Ralf Ramsauer, Rosen Penev, Rulin Huang, Sascha Hauer,
Sergey Fedorov, Sergey Matyukevich, Siddhesh Poyarekar,
Shoily Rahman, Dominik Steinberger, Stian Onarheim,
Thadeu Lima de Souza Cascardo, Thia Wyrod, Thinh Tran,
Tim Gardner, Tim Gates, Tim Orling, Tommi Rantala, Witold Baryluk,
Yiwei Lin, Yong-Xuan Wang, Zhiyi Sun, Zong Li.

## Static Analysis

<a href="https://scan.coverity.com/projects/stress-ng">
  <img alt="Coverity Scan Build Status"
       src="https://scan.coverity.com/projects/921/badge.svg"/>
</a>
