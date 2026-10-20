
## Introduction

stress-ng will stress test a computer system in various selectable ways. It was designed to exercise various physical subsystems of a computer as well as the various operating system kernel interfaces. stress-ng also has a wide range of CPU specific stress tests that exercise floating point, integer, bit manipulation and control flow.

stress-ng was originally intended to make a machine work hard and trip hardware issues such as thermal overruns as well as operating system bugs that only occur when a system is being thrashed hard. Use stress-ng with caution as some of the tests can make a system run hot on poorly designed hardware and also can cause excessive system thrashing which may be difficult to stop.

The tool has a wide range of different stress mechanisms (known as "stressors") and a full description of these is included in the man page.  This document is a quick-start reference guide and covers some of the more typical use cases for stress-ng.

## A simple example

The matrix stressor is a good way to exercise the CPU floating point operations as well as memory and processor data cache. Of all the tests, this one generally heats x86 CPUs the best.

To run 1 instance of this for 60 seconds, use:

```
stress-ng --matrix 1 -t 1m
```

If you want to run an instance of this on ALL the CPUs on your machine, specify 0 instances and stress-ng will figure out how many to run:

```
stress-ng --matrix 0 -t 1m
```

You can get an idea of how much user time and system (kernel) time is being used via the --times option:

```
stress-ng --matrix 0 -t 1m --times
stress-ng: info:  [16783] dispatching hogs: 4 matrix
stress-ng: info:  [16783] successful run completed in 60.00s (1 min, 0.00 secs)
stress-ng: info:  [16783] for a 60.00s run time:
stress-ng: info:  [16783]     240.00s available CPU time
stress-ng: info:  [16783]     205.21s user time   ( 85.50%)
stress-ng: info:  [16783]       0.32s system time (  0.13%)
stress-ng: info:  [16783]     205.53s total time  ( 85.64%)
stress-ng: info:  [16783] load average: 3.20 1.25 1.40
```

In the above example, I ran this on a machine that wasn't particularly idle with 4 CPU threads, so 4 instances were executed. The total CPU time was 4 x 60 seconds (240 seconds), of which 0.13% was in the kernel, and 85.50% in user time and stress-ng only got 85.64% of all the CPUs (since the machine was a bit busy doing other work at the same time).

Now consider a more interesting stress test, such as passing messages between processes using a POSIX message queue. We can run the mq stressor with the --perf option to see some more detail on what the machine is doing during the run:

```
stress-ng --mq 0 -t 30s --times --perf
stress-ng: info:  [16973] dispatching hogs: 4 mq
stress-ng: info:  [16973] successful run completed in 30.00s
stress-ng: info:  [16973] mq:
stress-ng: info:  [16973]            290,423,383,332 CPU Cycles                     9.68 B/sec
stress-ng: info:  [16973]            223,288,693,644 Instructions                   7.44 B/sec (0.769 instr. per cycle)
stress-ng: info:  [16973]                138,916,980 Cache References               4.63 M/sec
stress-ng: info:  [16973]                  5,305,248 Cache Misses                   0.18 M/sec ( 3.82%)
stress-ng: info:  [16973]            183,625,100,272 Stalled Cycles Frontend        6.12 B/sec
stress-ng: info:  [16973]             42,638,257,404 Branch Instructions            1.42 B/sec
stress-ng: info:  [16973]                167,682,072 Branch Misses                  5.59 M/sec ( 0.39%)
stress-ng: info:  [16973]             10,231,977,988 Bus Cycles                     0.34 B/sec
stress-ng: info:  [16973]            256,043,743,440 Total Cycles                   8.53 B/sec
stress-ng: info:  [16973]                        176 Page Faults Minor              5.87 sec  
stress-ng: info:  [16973]                          0 Page Faults Major              0.00 sec  
stress-ng: info:  [16973]                 22,901,328 Context Switches               0.76 M/sec
stress-ng: info:  [16973]                        952 CPU Migrations                31.73 sec  
stress-ng: info:  [16973]                          0 Alignment Faults               0.00 sec  
stress-ng: info:  [16973] for a 30.00s run time:
stress-ng: info:  [16973]     120.02s available CPU time
stress-ng: info:  [16973]      11.26s user time   (  9.38%)
stress-ng: info:  [16973]      93.84s system time ( 78.19%)
stress-ng: info:  [16973]     105.10s total time  ( 87.57%)
stress-ng: info:  [16973] load average: 3.72 1.67 1.42
```

So we can see here that the mq stressor is forcing the processes to context switch at around 0.76 million per second, and we're getting quite low data cache misses.

## Bogo Ops

Stress-ng measures a stress test "throughput" using "bogus operations per second". The size of a bogo op depends on the stressor being run, and are not comparable between different stressors.  They give some rough notion of performance but should not be used as an accurate benchmarking figure.  They are useful to see if performance changes between kernel versions or different compiler versions used to build stress-ng. One can also use them to get a notional rough comparison of performance between different systems.  But caveat emptor: they are NOT intended to be a scientifically accurate benchmarking metric.

Use the --metrics-brief option to show the bogo ops. Let's see how the matrix stressor fares on an i5-3210M laptop:

```
stress-ng --matrix 0 -t 60s --metrics-brief
stress-ng: info:  [17579] dispatching hogs: 4 matrix
stress-ng: info:  [17579] successful run completed in 60.01s (1 min, 0.01 secs)
stress-ng: info:  [17579] stressor      bogo ops real time  usr time  sys time   bogo ops/s   bogo ops/s
stress-ng: info:  [17579]                          (secs)    (secs)    (secs)   (real time) (usr+sys time)
stress-ng: info:  [17579] matrix          349322     60.00    203.23      0.19      5822.03      1717.25
```

...we are primarily interested in the bogo ops/s (real time) rate, that is, the total bogo ops measured divided by the total run time.

..and now run it on a 48 thread Xeon(R) CPU E5-2680 server:

```
stress-ng --matrix 0 -t 60s --metrics-brief
stress-ng: info:  [113534] dispatching hogs: 48 matrix
stress-ng: info:  [113534] successful run completed in 60.01s (1 min, 0.01 secs)
stress-ng: info:  [113534] stressor      bogo ops real time  usr time  sys time   bogo ops/s   bogo ops/s
stress-ng: info:  [113534]                          (secs)    (secs)    (secs)   (real time) (usr+sys time)
stress-ng: info:  [113534] matrix         6594214     60.00   2882.38      0.02    109903.67      2287.75
```

so 5822.03 vs 109903.67, the Xeon server has 12 times as many threads and has about 18.8 x more throughput on this specific stress test.

## Running many stressors

stress-ng can run more than one stress test.  By default it will run the requested stressors in parallel, for example, running 2 instances of the CPU stressor, 1 instance of the matrix stressor and 3 instances of the message queue stressor in parallel for 5 minutes:

```
stress-ng --cpu 2 --matrix 1 --mq 3 -t 5m
```

One can invoke all the stress tests to run in parallel, with the --all option. The following example will run 2 instances of each of all the stress tests in parallel:

```
stress-ng --all 2
```

Or, alternatively, run each different stressor sequentially. The following example will run 4 instances of each stress test at a time, for 20 seconds for each stressor:

```
stress-ng --seq 4 -t 20
```

One may want to exclude specific stressors from the --all and --seq options, one can do that with the -x option:

```
stress-ng --seq 1 -x numa,matrix,hdd
```

...this will run all the stressors except for the numa, matrix and hdd stressors.

## How hot?

If your machine supports thermal zones, then stress-ng can report on the temperature at the end of a run with the --tz option, for example, 60 seconds of the CPU stressor:

```
stress-ng --cpu 0 --tz -t 60
stress-ng: info:  [18065] dispatching hogs: 4 cpu
stress-ng: info:  [18065] successful run completed in 60.07s (1 min, 0.07 secs)
stress-ng: info:  [18065] cpu:
stress-ng: info:  [18065]         x86_pkg_temp   88.75 °C
stress-ng: info:  [18065]               acpitz   88.38 °C
```

## More stressy

The --aggressive option cranks up the stress by enabling more file, cache and memory aggressive options in the stress tests if they are available.  It will also force processes to jump around between CPUs which will stress SMP and NUMA systems further.

Stressors are configured to run with default settings, such as memory sizes, cache sizes, file sizes etc. The --maximize option forces stressors to use the largest settings that are sanely possible, causing more stress, for example more I/O and considerably more paging.

Running stress-ng with root privilege is even more aggressive since stress-ng will change scheduling priorities and will maximize itself to the ulimit limits. Don't use this unless you are willing to totally lock up a machine.

## Verbose mode

The -v option will enable verbose mode. This will show some extra debug information in case you want to see what stress-ng is doing.

## Classes

The stress-ng stressors are grouped together in different classes:

- compute - CPU computations
- cpu - CPU intensive
- cpu-cache - stress CPU instruction and/or data caches
- device - raw device driver stressors
- filesystem - file system activity
- fp - floating point operations
- gpu - GPU stressors
- hot - stressors that make the system run hot
- integer - integer operations
- interrupt - high interrupt load generators
- io - generic input/output
- ipc - inter-process communication
- memory - stack, heap, memory mapping, shared memory stressors
- network - TCP/IP, UDP and UNIX domain socket stressors
- os - core kernel stressors
- pipe - pipe and UNIX socket stressors
- scheduler - force high levels of context switching
- search - search algorithms
- security - AppArmor and Linux Security Module (LSM) stressors
- signal - software signals
- sort - sorting algorithms
- sound - sound devices
- tlb - Translation Lookaside Buffer (TLB) stressors
- vector - vector math operations
- vm - Virtual Memory stressor (paging and memory)

stressors may be in one or more classes, for example, the lsearch (linear search) stressor is in the cpu, cpu-cache, memory and search classes as it touches all these four activities.

To list the stressors in a specific class, follow the class name with a question mark (escape it so the shell does not expand it):

```
stress-ng --class vm\?
```

For example, to run all the stress tests in parallel under the network class, with 1 instance of each being run, use:

```
stress-ng --class network --all 1
```

..or to run all the networking class stressors one by one with an instance of each being run on ALL cpus, use:

```
stress-ng --class network --seq 0
```

## Worked Examples

### Getting the CPU hot

The matrix stressor is generally best for this.  Using the --matrix-size option we can set the N x N size of the matrix of floating point values being operated on inside the stressor.  Generally it is best to match the number of instances with the number of CPUs.

```
stress-ng --matrix 0 --matrix-size 64 --tz  -t 60
stress-ng: info:  [18351] dispatching hogs: 4 matrix
stress-ng: info:  [18351] successful run completed in 60.00s (1 min, 0.00 secs)
stress-ng: info:  [18351] matrix:
stress-ng: info:  [18351]         x86_pkg_temp   88.00 °C
stress-ng: info:  [18351]               acpitz   87.00 °C
```

In the above example, the x86 processor package thermal zone reached 88 degrees Celsius. You may wish to change the matrix size and run time to see how hot you can get the CPU. I believe making the size small enough to fit into the L2 cache may be best, but it depends on the machine.

### Forcing memory pressure

Running out of memory is a great way to see what happens to applications and the kernel. stress-ng has found several bugs in applications, daemons and kernel drivers when memory has run low and code does not check for memory allocation failures correctly.

The --brk (expand heap break point), --stack (expand stack), and --bigheap stressors try to rapidly consume memory. The kernel will eventually kill these using the Out Of Memory (OOM) killer, however, stress-ng will respawn the processes to keep the kernel busy.  On a system with swap enabled the swap device will be heavily exercised.  One can try running this with swap disabled using swapoff -a before invoking stress-ng.

Example:

```
stress-ng --brk 2 --stack 2 --bigheap 2
```

## Methods

Some stressors contain more than one method of causing stress.  For example, the cpu stress test contains a wide range of ways to exercise a CPU; these are known as "methods".  Passing "which" as a method name, makes stress-ng print the list of all the available methods:

```
stress-ng --cpu-method which
option cpu-method choice 'which' not known, choices are: all ackermann apery bitops
callfunc cdouble cfloat clongdouble collatz correlate crc16 decimal32 decimal64
decimal128 dither div8 div16 div32 div64 div128 double euler explog factorial
fibonacci fft fletcher16 float float32 float64 float80 float128 floatconversion
gamma gcd gray hamming hanoi hyperbolic idct int128 int64 int32 int16 int8
int128float int128double int128longdouble int128decimal32 int128decimal64
int128decimal128 int64float int64double int64longdouble int32float int32double
int32longdouble intconversion ipv4checksum jmp lfsr32 ln2 logmap longdouble loop
matrixprod nsqrt omega parity phi pi prime psi queens rand rand48 rgb sieve stats
sqrt trig union zeta
```

The list of methods varies between versions of stress-ng and the architecture it was built for. For a full explanation of these, please consult the manual.

By default, when running the cpu stressor without specifying a method, the stressor will step through all the methods one by one in round-robin fashion to exercise the CPU with each one.  To select just a specific cpu stressor method, for example, the Fast Fourier Transform (fft) method, use:

```
stress-ng --cpu 1 --cpu-method fft -t 1m
```

So to do a quick and dirty integer bogo ops benchmark, one could do:

```
for m in int8 int16 int32 int64; do stress-ng --cpu 0 --cpu-method $m -t 10s --metrics-brief; done
```

## Verify mode

Some stressors have a verification mode.  A stress test is run and the results are checked, if the results are incorrect then stress-ng will flag up a warning error message.  Suppose we want to run some exhaustive memory checks on a blob of virtually mapped memory via the vm stressor, enable the --verify mode and this will sanity check the read/write results on the memory:

```
stress-ng --vm 1 --vm-bytes 2G --verify -v
```

Note that not all stressors have a verify mode, and enabling it will reduce the bogo op stats as there is an extra verification step being invoked.

## Causing More Virtual Memory (VM) Stress

When under memory pressure, the kernel will start writing pages out to swap. By checking which pages in a memory mapping are not resident in memory and touching them we can force them back into memory, causing the VM system to be heavily exercised.  The --page-in option enables this mode for the bigheap, mmap and vm stressors. For example, expect a lot of swapping on a system with only 4GB of memory - 2 x 2GB of vm stressor and 2 x 2GB of mmap stressor with page-in enabled:

```
stress-ng --vm 2 --vm-bytes 2G --mmap 2 --mmap-bytes 2G --page-in
```

## Generating a large interrupt load

Running timers at high frequency can generate a large interrupt load. The timer stressor with an appropriately selected timer frequency can be used to force many hundreds of thousands of interrupts per second, for example, 32 instances at 1MHz:

```
stress-ng --timer 32 --timer-freq 1000000
```

## Generating major page faults

You can generate major page faults (by accessing a page that is not loaded in memory at the time of the fault) and see the page fault rate using:

```
stress-ng --fault 0 --perf -t 1m
```

or with newer kernels use the userfaultfd stressor to force even more major faults:

```
stress-ng --userfaultfd 0 --perf -t 1m
```

## Frequently Asked Questions

Q: How can I run a stressor on specific CPUs?

A: For example, 5 matrix stressors on CPUs 0,1,5,6,7

```
stress-ng --taskset 0,1,5-7 --matrix 5
```
