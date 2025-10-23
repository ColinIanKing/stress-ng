/*
 * Copyright (C) 2023-2025 Colin Ian King.
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
#include "stress-ng.h"
#include "core-asm-arm.h"
#include "core-asm-loong64.h"
#include "core-asm-ppc64.h"
#include "core-asm-riscv.h"
#include "core-asm-x86.h"
#include "core-asm-generic.h"
#include "core-cpu-cache.h"
#include "core-builtin.h"
#include "core-madvise.h"
#include "core-mmap.h"
#include "core-pthread.h"
#include "core-put.h"
#include "core-sched.h"
#include "core-target-clones.h"
#include "core-vecmath.h"

#include <math.h>
#include <sched.h>
#include <time.h>

#if defined(HAVE_MQUEUE_H)
#include <mqueue.h>
#endif

#if defined(HAVE_LIB_PTHREAD) &&	\
    defined(HAVE_LIB_RT) &&		\
    defined(HAVE_MQ_POSIX) &&		\
    defined(HAVE_MQUEUE_H)
#define WORKLOAD_THREADED	(1)
#endif

typedef struct {
#if defined(WORKLOAD_THREADED)
	pthread_t pthread;
#endif
	int ret;
} workload_thread_t;

#if defined(WORKLOAD_THREADED)
typedef struct {
	mqd_t	mq;
	uint8_t *buffer;
	size_t buffer_len;
	int workload_method;
} stress_workload_ctxt_t;
#endif

#define NUM_BUCKETS	(20)

#define STRESS_WORKLOAD_DIST_CLUSTER	(0)
#define STRESS_WORKLOAD_DIST_EVEN	(1)
#define STRESS_WORKLOAD_DIST_POISSON	(2)
#define STRESS_WORKLOAD_DIST_RANDOM1	(3)
#define STRESS_WORKLOAD_DIST_RANDOM2	(4)
#define STRESS_WORKLOAD_DIST_RANDOM3	(5)

#define STRESS_WORKLOAD_THREADS		(4)

#define STRESS_WORKLOAD_METHOD_ALL	(0)
#define STRESS_WORKLOAD_METHOD_TIME	(1)
#define STRESS_WORKLOAD_METHOD_NOP	(2)
#define STRESS_WORKLOAD_METHOD_MEMSET	(3)
#define STRESS_WORKLOAD_METHOD_MEMMOVE	(4)
#define STRESS_WORKLOAD_METHOD_SQRT	(5)
#define STRESS_WORKLOAD_METHOD_INC64	(6)
#define STRESS_WORKLOAD_METHOD_MWC64	(7)
#define STRESS_WORKLOAD_METHOD_GETPID	(8)
#define STRESS_WORKLOAD_METHOD_MEMREAD	(9)
#define STRESS_WORKLOAD_METHOD_PAUSE	(10)
#define STRESS_WORKLOAD_METHOD_PROCNAME	(11)
#define STRESS_WORKLOAD_METHOD_FMA	(12)
#define STRESS_WORKLOAD_METHOD_RANDOM	(13)
#define STRESS_WORKLOAD_METHOD_VECFP	(14)
#define STRESS_WORKLOAD_METHOD_MAX	STRESS_WORKLOAD_METHOD_VECFP

#define SCHED_UNDEFINED	(-1)

typedef struct {
	double when_us;
	double run_duration_sec;
} stress_workload_t;

typedef struct {
	const char *name;
	const int type;
} stress_workload_dist_t;

typedef struct {
	const char *name;
	const int method;
} stress_workload_method_t;

typedef struct {
	double width;
	uint64_t bucket[NUM_BUCKETS];
	uint64_t overflow;
} stress_workload_bucket_t;

static const stress_help_t help[] = {
	{ NULL,	"workload N",		"start N workers that exercise a mix of scheduling loads" },
	{ NULL,	"workload-dist type",	"workload distribution type [random1 | random2 | random3 | cluster]" },
	{ NULL, "workload-load P",	"percentage load P per workload time slice" },
	{ NULL,	"workload-ops N",	"stop after N workload bogo operations" },
	{ NULL,	"workload-quanta-us N",	"max duration of each quanta work item in microseconds" },
	{ NULL, "workload-sched P",	"select scheduler policy [ batch | deadline | ext | idle | fifo | rr | other ]" },
	{ NULL, "workload-slice-us N",	"duration of workload time load in microseconds" },
	{ NULL,	"workload-threads N",	"number of workload threads workers to use, default is 0 (disabled)" },
	{ NULL, "workload-method M",	"select a workload method, default is all" },
	{ NULL,	NULL,			NULL }
};

static const stress_workload_dist_t workload_dists[] = {
	{ "cluster",	STRESS_WORKLOAD_DIST_CLUSTER },
	{ "even",	STRESS_WORKLOAD_DIST_EVEN },
	{ "poisson",	STRESS_WORKLOAD_DIST_POISSON },
	{ "random1",	STRESS_WORKLOAD_DIST_RANDOM1 },
	{ "random2",	STRESS_WORKLOAD_DIST_RANDOM2 },
	{ "random3",	STRESS_WORKLOAD_DIST_RANDOM3 },
};

static const stress_workload_method_t workload_methods[] = {
	{ "all",	STRESS_WORKLOAD_METHOD_ALL },
	{ "fma",	STRESS_WORKLOAD_METHOD_FMA },
	{ "getpid",	STRESS_WORKLOAD_METHOD_GETPID },
	{ "time",	STRESS_WORKLOAD_METHOD_TIME },
	{ "inc64",	STRESS_WORKLOAD_METHOD_INC64 },
	{ "memmove",	STRESS_WORKLOAD_METHOD_MEMMOVE },
	{ "memread",	STRESS_WORKLOAD_METHOD_MEMREAD },
	{ "memset",	STRESS_WORKLOAD_METHOD_MEMSET },
	{ "mwc64",	STRESS_WORKLOAD_METHOD_MWC64 },
	{ "nop",	STRESS_WORKLOAD_METHOD_NOP },
	{ "pause",	STRESS_WORKLOAD_METHOD_PAUSE },
	{ "procname",	STRESS_WORKLOAD_METHOD_PROCNAME },
	{ "random",	STRESS_WORKLOAD_METHOD_RANDOM },
	{ "sqrt",	STRESS_WORKLOAD_METHOD_SQRT },
	{ "vecfp",	STRESS_WORKLOAD_METHOD_VECFP },
};

static const char *stress_workload_dist(const size_t i)
{
	return (i < SIZEOF_ARRAY(workload_dists)) ? workload_dists[i].name : NULL;
}

static const char *stress_workload_method(const size_t i)
{
	return (i < SIZEOF_ARRAY(workload_methods)) ? workload_methods[i].name : NULL;
}

static const char *stress_workload_sched(const size_t i)
{
	return (i < stress_sched_types_length) ? stress_sched_types[i].sched_name : NULL;
}

static const stress_opt_t opts[] = {
	{ OPT_workload_dist,      "workload-dist",      TYPE_ID_SIZE_T_METHOD, 0, 0, stress_workload_dist },
	{ OPT_workload_load,      "workload-load",      TYPE_ID_UINT32, 1, 100, NULL },
	{ OPT_workload_method,    "workload-method",    TYPE_ID_SIZE_T_METHOD, 0, 0, stress_workload_method },
	{ OPT_workload_quanta_us, "workload-quanta-us", TYPE_ID_UINT32,  1, 10000000, NULL },
	{ OPT_workload_sched,     "workload-sched",     TYPE_ID_SIZE_T_METHOD, 0, 0, stress_workload_sched },
	{ OPT_workload_slice_us,  "workload-slice-us",  TYPE_ID_UINT32, 1, 10000000, NULL },
	{ OPT_workload_threads,   "workload-threads",   TYPE_ID_UINT32, 0, 1024, NULL },
	END_OPT,
};

#if (defined(_POSIX_PRIORITY_SCHEDULING) || defined(__linux__)) &&	\
     !defined(__OpenBSD__) &&						\
     !defined(__minix__) &&						\
     !defined(__APPLE__) &&						\
     !defined(__HAIKU__) &&						\
     !defined(__serenity__)
static int stress_workload_set_sched(
	stress_args_t *args,
	const size_t workload_sched)
{
#if defined(SCHED_DEADLINE) &&		\
    defined(HAVE_SCHED_GETATTR) &&	\
    defined(HAVE_SCHED_SETATTR)
	struct shim_sched_attr attr;
#else
	UNEXPECTED
#endif
	struct sched_param param;
	int ret = 0;
	int max_prio, min_prio, rng_prio;
	const pid_t pid = getpid();
	const char *policy_name;
	int policy;

	if ((workload_sched < 1) || (workload_sched >= stress_sched_types_length))
		return 0;

	policy_name = stress_sched_types[workload_sched].sched_name;
	policy = stress_sched_types[workload_sched].sched;

	errno = 0;
	switch (policy) {
#if defined(SCHED_DEADLINE) &&		\
    defined(HAVE_SCHED_GETATTR) &&	\
    defined(HAVE_SCHED_SETATTR)
	case SCHED_DEADLINE:
		(void)shim_memset(&attr, 0, sizeof(attr));
		attr.size = sizeof(attr);
		attr.sched_flags = 0;
		attr.sched_nice = 0;
		attr.sched_priority = 0;
		attr.sched_policy = SCHED_DEADLINE;
		/* runtime <= deadline <= period */
		attr.sched_runtime = 64 * 1000000;
		attr.sched_deadline = 128 * 1000000;
		attr.sched_period = 256 * 1000000;

		ret = shim_sched_setattr(0, &attr, 0);
		break;
#endif
#if defined(SCHED_EXT)
	case SCHED_EXT:
#endif
#if defined(SCHED_BATCH)
	case SCHED_BATCH:
#endif
#if defined(SCHED_IDLE)
	case SCHED_IDLE:
#endif
#if defined(SCHED_OTHER)
	case SCHED_OTHER:
#endif
		param.sched_priority = 0;
		ret = sched_setscheduler(pid, policy, &param);

		break;
#if defined(SCHED_RR)
	case SCHED_RR:
#if defined(HAVE_SCHED_RR_GET_INTERVAL)
		{
			struct timespec t;

			VOID_RET(int, sched_rr_get_interval(pid, &t));
		}
#endif
		goto case_sched_fifo;
#endif
#if defined(SCHED_FIFO)
		case SCHED_FIFO:
#endif
case_sched_fifo:
		min_prio = sched_get_priority_min(policy);
		max_prio = sched_get_priority_max(policy);

		/* Check if min/max is supported or not */
		if ((min_prio == -1) || (max_prio == -1)) {
			pr_inf("%s: cannot get min/max priority levels, not setting scheduler policy\n",
				args->name);
		}

		rng_prio = max_prio - min_prio;
		if (UNLIKELY(rng_prio == 0)) {
			pr_err("%s: invalid min/max priority "
				"range for scheduling policy %s "
				"(min=%d, max=%d)\n",
				args->name,
				policy_name,
				min_prio, max_prio);
			break;
		}
		param.sched_priority = (int)stress_mwc32modn(rng_prio) + min_prio;
		ret = sched_setscheduler(pid, policy, &param);
		break;
	default:
		/* Should never get here */
		break;
	}

	if (ret < 0) {
		if (errno == EPERM) {
			if (stress_instance_zero(args))
				pr_inf("%s: insufficient privilege to set scheduler to '%s'\n",
					args->name, policy_name);
			return 0;
		}
		/*
		 *  Some systems return EINVAL for non-POSIX
		 *  scheduling policies, silently ignore these
		 *  failures.
		 */
		pr_inf("%s: sched_setscheduler "
			"failed, errno=%d (%s) "
			"for scheduler policy %s\n",
			args->name, errno, strerror(errno),
			policy_name);
	} else {
		if (stress_instance_zero(args))
			pr_inf("%s: using '%s' scheduler\n",
				args->name, policy_name);
	}
	return ret;
}
#else
static int stress_workload_set_sched(
	stress_args_t *args,
	const size_t workload_sched)
{
	(void)args;
	(void)workload_sched;

	return 0;
}
#endif

static void stress_workload_nop(void)
{
	register int i;

	for (i = 0; i < 16; i++) {
		stress_asm_nop();
		stress_asm_nop();
		stress_asm_nop();
		stress_asm_nop();
		stress_asm_nop();
		stress_asm_nop();
		stress_asm_nop();
		stress_asm_nop();
		stress_asm_nop();
		stress_asm_nop();
		stress_asm_nop();
		stress_asm_nop();
		stress_asm_nop();
		stress_asm_nop();
		stress_asm_nop();
		stress_asm_nop();
	}
}

static TARGET_CLONES void stress_workload_fma(void)
{
	const uint32_t r = stress_mwc32();
	const double a = (double)r;
	const double b = (double)(r >> 4);
	const double c = (double)(r ^ 0xa5a55a5a);

	stress_double_put((a * b) + c);
	stress_double_put((a * c) + b);
	stress_double_put((b * c) + a);

	stress_double_put(a + (b * c));
	stress_double_put(a + (c * b));
	stress_double_put(b + (c * a));
}

static void stress_workload_math(const double v1, const double v2)
{
	double r;

	r = sqrt(v1) + hypot(v1, v1 + v2);
	r += sqrt(v2) + hypot(v2, v1 + v2);
	r += sqrt(v1 + v2);

	stress_double_put(r);
}

static void stress_workload_pause(void)
{
#if defined(HAVE_ASM_X86_PAUSE)
	stress_asm_x86_pause();
	stress_asm_x86_pause();
	stress_asm_x86_pause();
	stress_asm_x86_pause();
#elif defined(HAVE_ASM_ARM_YIELD)
	stress_asm_arm_yield();
	stress_asm_arm_yield();
	stress_asm_arm_yield();
	stress_asm_arm_yield();
#elif defined(STRESS_ARCH_PPC64)
	stress_asm_ppc64_yield();
	stress_asm_ppc64_yield();
	stress_asm_ppc64_yield();
	stress_asm_ppc64_yield();
#elif defined(STRESS_ARCH_PPC)
	stress_asm_ppc_yield();
	stress_asm_ppc_yield();
	stress_asm_ppc_yield();
	stress_asm_ppc_yield();
#elif defined(STRESS_ARCH_RISCV)
	stress_asm_riscv_pause();
	stress_asm_riscv_pause();
	stress_asm_riscv_pause();
	stress_asm_riscv_pause();
#elif defined(HAVE_ASM_LOONG64_DBAR)
	stress_asm_loong64_dbar();
	stress_asm_loong64_dbar();
	stress_asm_loong64_dbar();
	stress_asm_loong64_dbar();
#else
	stress_asm_mb();
	stress_asm_nop();
	stress_asm_mb();
	stress_asm_nop();
	stress_asm_mb();
	stress_asm_nop();
	stress_asm_mb();
	stress_asm_nop();
#endif
}

static void OPTIMIZE3 TARGET_CLONES stress_workload_read(void *buffer, const size_t buffer_len)
{
#if defined(HAVE_VECMATH)
	typedef int64_t stress_vint64_t __attribute__ ((vector_size (128)));

	register stress_vint64_t *ptr = (stress_vint64_t *)buffer;
	register stress_vint64_t *end = (stress_vint64_t *)(((uintptr_t)buffer) + buffer_len);

	while (ptr < end) {
		stress_vint64_t v;

		v = *(volatile stress_vint64_t *)&ptr[0];
		(void)v;
		ptr += 2;
	}

	stress_cpu_data_cache_flush(buffer, buffer_len);
#else
	register uint64_t *ptr = (uint64_t *)buffer;
	register uint64_t *end = (uint64_t *)(((uintptr_t)buffer) + buffer_len);

	stress_cpu_data_cache_flush(buffer, buffer_len);
	while (ptr < end) {
		(void)*(volatile uint64_t *)&ptr[0x00];
		(void)*(volatile uint64_t *)&ptr[0x01];
		(void)*(volatile uint64_t *)&ptr[0x02];
		(void)*(volatile uint64_t *)&ptr[0x03];
		(void)*(volatile uint64_t *)&ptr[0x04];
		(void)*(volatile uint64_t *)&ptr[0x05];
		(void)*(volatile uint64_t *)&ptr[0x06];
		(void)*(volatile uint64_t *)&ptr[0x07];
		(void)*(volatile uint64_t *)&ptr[0x08];
		(void)*(volatile uint64_t *)&ptr[0x09];
		(void)*(volatile uint64_t *)&ptr[0x0a];
		(void)*(volatile uint64_t *)&ptr[0x0b];
		(void)*(volatile uint64_t *)&ptr[0x0c];
		(void)*(volatile uint64_t *)&ptr[0x0d];
		(void)*(volatile uint64_t *)&ptr[0x0e];
		(void)*(volatile uint64_t *)&ptr[0x0f];
		ptr += 16;
	}
#endif
}

static void TARGET_CLONES stress_workload_vecfp(void)
{
#if defined(HAVE_VECMATH)
	/* Explicit vectorized version */
	typedef union {
		double v   ALIGNED(2048) __attribute__ ((vector_size(sizeof(double) * 64)));
		double f[64] ALIGNED(2048);
	} stress_vecfp_double_64_t;

	stress_vecfp_double_64_t a, b;
	double sum = 0.0;
	static int v = 0;
	register size_t i;

	for (i = 0; i < 64; i++) {
		a.f[i] = v;
		b.f[i] = v * v;
		v++;
	}
	a.v *= b.v;
	a.v += -b.v;

	for (i = 0; i < 64; i++) {
		sum += a.f[i];
	}
	stress_long_double_put(sum);
#else
	/* See how well compiler can vectorize version */
        double a[64], b[64];
	double sum = 0.0;
	static int v = 0;
	register size_t i;

	for (i = 0; i < 64; i++) {
		a[i] = v;
		b[i] = v * v;
		v++;
	}
	for (i = 0; i < 64; i++) {
		a[i] *= b[i];
	}
	for (i = 0; i < 64; i++) {
		a[i] += b[i];
	}
	for (i = 0; i < 64; i++) {
		sum += a[i];
	}
	stress_long_double_put(sum);
#endif
}

static void stress_workload_procname(void)
{
	char procname[64];

	(void)snprintf(procname, sizeof(procname),
		"workload-%" PRIx64 "%" PRIx64 "%" PRIx64,
		stress_mwc64(), stress_mwc64(), stress_mwc64());
	stress_set_proc_name(procname);
}

static inline void stress_workload_waste_time(
	const int workload_method,
	const double run_duration_sec,
	uint8_t *buffer,
	const size_t buffer_len)
{
	const double t_end = stress_time_now() + run_duration_sec;
	double t;
	static volatile uint64_t val = 0;
	const int which = (workload_method == STRESS_WORKLOAD_METHOD_ALL) ?
		stress_mwc8modn(STRESS_WORKLOAD_METHOD_MAX) + 1 : workload_method;

	switch (which) {
	case STRESS_WORKLOAD_METHOD_TIME:
		while (stress_time_now() < t_end)
			(void)time(NULL);
		break;
	case STRESS_WORKLOAD_METHOD_NOP:
		while (stress_time_now() < t_end)
			stress_workload_nop();
		break;
	case STRESS_WORKLOAD_METHOD_MEMSET:
		while (stress_time_now() < t_end)
			shim_memset(buffer, stress_mwc8(), buffer_len);
		break;
	case STRESS_WORKLOAD_METHOD_MEMMOVE:
		while (stress_time_now() < t_end)
			shim_memmove(buffer, buffer + 1, buffer_len - 1);
		break;
	case STRESS_WORKLOAD_METHOD_SQRT:
		while ((t = stress_time_now()) < t_end)
			stress_workload_math(t, t_end);
		break;
	case STRESS_WORKLOAD_METHOD_INC64:
		while (stress_time_now() < t_end)
			val++;
		break;
	case STRESS_WORKLOAD_METHOD_MWC64:
		while (stress_time_now() < t_end)
			(void)stress_mwc64();
		break;
	case STRESS_WORKLOAD_METHOD_GETPID:
		while (stress_time_now() < t_end)
			(void)getpid();
		break;
	case STRESS_WORKLOAD_METHOD_MEMREAD:
		while (stress_time_now() < t_end)
			stress_workload_read(buffer, buffer_len);
		break;
	case STRESS_WORKLOAD_METHOD_PAUSE:
		while (stress_time_now() < t_end)
			stress_workload_pause();
		break;
	case STRESS_WORKLOAD_METHOD_FMA:
		while (stress_time_now() < t_end)
			stress_workload_fma();
		break;
	case STRESS_WORKLOAD_METHOD_VECFP:
		while (stress_time_now() < t_end)
			stress_workload_vecfp();
		break;
	case STRESS_WORKLOAD_METHOD_PROCNAME:
		while (stress_time_now() < t_end)
			stress_workload_procname();
		break;
	case STRESS_WORKLOAD_METHOD_RANDOM:
	default:
		while ((t = stress_time_now()) < t_end) {
			switch (stress_mwc8modn(STRESS_WORKLOAD_METHOD_MAX - 1) + 1) {
			case STRESS_WORKLOAD_METHOD_TIME:
				(void)time(NULL);
				break;
			case STRESS_WORKLOAD_METHOD_NOP:
				stress_workload_nop();
				break;
			case STRESS_WORKLOAD_METHOD_MEMSET:
				shim_memset(buffer, stress_mwc8(), buffer_len);
				break;
			case STRESS_WORKLOAD_METHOD_MEMMOVE:
				shim_memmove(buffer, buffer + 1, buffer_len - 1);
				break;
			case STRESS_WORKLOAD_METHOD_INC64:
				while (stress_time_now() < t_end)
					val++;
				break;
			case STRESS_WORKLOAD_METHOD_MWC64:
				(void)stress_mwc64();
				break;
			case STRESS_WORKLOAD_METHOD_GETPID:
				(void)getpid();
				break;
			case STRESS_WORKLOAD_METHOD_SQRT:
				stress_workload_math(t, t_end);
				break;
			case STRESS_WORKLOAD_METHOD_MEMREAD:
				stress_workload_read(buffer, buffer_len);
				break;
			case STRESS_WORKLOAD_METHOD_PAUSE:
				stress_workload_pause();
				break;
			case STRESS_WORKLOAD_METHOD_FMA:
				stress_workload_fma();
				break;
			case STRESS_WORKLOAD_METHOD_VECFP:
				stress_workload_vecfp();
				break;
			default:
			case STRESS_WORKLOAD_METHOD_PROCNAME:
				stress_workload_procname();
				break;
			}
		}
		break;
	}
}

static void stress_workload_bucket_init(stress_workload_bucket_t *bucket, const double width)
{
	size_t i;

	for (i = 0; i < SIZEOF_ARRAY(bucket->bucket); i++)
		bucket->bucket[i] = 0;
	bucket->width = width / SIZEOF_ARRAY(bucket->bucket);
	bucket->overflow = 0;
}

static void stress_workload_bucket_account(stress_workload_bucket_t *bucket, const double value)
{
	ssize_t i;

	i = (ssize_t)(value / bucket->width);
	if (UNLIKELY(i < 0))
		i = 0;
	if (i < (ssize_t)SIZEOF_ARRAY(bucket->bucket))
		bucket->bucket[i]++;
	else
		bucket->overflow++;
}

static void stress_workload_bucket_report(stress_workload_bucket_t *bucket)
{
	size_t i;
	int width1, width2;
	char buf[64];
	uint64_t total;

	(void)snprintf(buf, sizeof(buf), "%" PRIu64,
		(uint64_t)((SIZEOF_ARRAY(bucket->bucket) + 1) * bucket->width));
	width1 = (int)strlen(buf);
	if (width1 < 7)
		width1 = 7;

	total = bucket->overflow;
	for (i = 0; i < SIZEOF_ARRAY(bucket->bucket); i++)
		total += bucket->bucket[i];
	(void)snprintf(buf, sizeof(buf), "%" PRIu64, total);
	width2 = (int)strlen(buf);
	if (width2 < 7)
		width2 = 7;

	pr_block_begin();
	pr_dbg("distribution of workload start time in workload slice:\n");
	pr_dbg("%-*s %*s %4s\n",
		(width1 * 2) + 4, "start time (us)",
		width2, "count", "%");
	for (i = 0; i < SIZEOF_ARRAY(bucket->bucket); i++) {
		pr_dbg("%*" PRIu64 " .. %*" PRIu64 " %*" PRIu64 " %4.1f\n",
			width1, (uint64_t)(i * bucket->width),
			width1, (uint64_t)((i + 1) * bucket->width) - 1,
			width2, bucket->bucket[i],
			(double)100.0 * (double)bucket->bucket[i] / (double)total);
	}
	pr_dbg("%*" PRIu64 " .. %*s %*" PRIu64 " %4.1f\n",
		width1, (uint64_t)(i * bucket->width),
		width1, "",
		width2, bucket->overflow,
		(double)100.0 * (double)bucket->overflow / (double)total);
	pr_block_end();
}

static int stress_workload_cmp(const void *p1, const void *p2)
{
	const stress_workload_t *w1 = (const stress_workload_t *)p1;
	const stress_workload_t *w2 = (const stress_workload_t *)p2;

	register const double when1 = w1->when_us;
	register const double when2 = w2->when_us;

	if (when1 < when2)
		return -1;
	else if (when1 > when2)
		return 1;
	else
		return 0;
}

static int stress_workload_exercise(
	stress_args_t *args,
#if defined(WORKLOAD_THREADED)
	const mqd_t mq,
#endif
	const uint32_t workload_method,
	const uint32_t workload_load,
	const uint32_t workload_slice_us,
	const uint32_t workload_quanta_us,
	const uint32_t workload_threads,
	const uint32_t max_quanta,
	const int workload_dist,
	stress_workload_t *workload,
	stress_workload_bucket_t *slice_offset_bucket,
	uint8_t *buffer,
	const size_t buffer_len)
{
	size_t i;
	const double scale_us_to_sec = 1.0 / STRESS_DBL_MICROSECOND;
	double t_begin, t_end, sleep_duration_ns, run_duration_sec;
	const double scale32bit = 1.0 / (double)4294967296.0;
	double sum, scale;
	uint32_t offset;

	run_duration_sec = (double)workload_quanta_us * scale_us_to_sec * ((double)workload_load / 100.0);

	switch (workload_dist) {
	case STRESS_WORKLOAD_DIST_RANDOM1:
		for (i = 0; i < max_quanta; i++) {
			workload[i].when_us = (double)stress_mwc32modn(workload_slice_us - workload_quanta_us);
			workload[i].run_duration_sec = run_duration_sec;
		}
		break;
	case STRESS_WORKLOAD_DIST_RANDOM2:
		for (i = 0; i < max_quanta; i++) {
			workload[i].when_us = (double)(stress_mwc32modn(workload_slice_us - workload_quanta_us) +
					       stress_mwc32modn(workload_slice_us - workload_quanta_us)) / 2.0;
			workload[i].run_duration_sec = run_duration_sec;
		}
		break;
	case STRESS_WORKLOAD_DIST_RANDOM3:
		for (i = 0; i < max_quanta; i++) {
			workload[i].when_us = (double)(stress_mwc32modn(workload_slice_us - workload_quanta_us) +
					       stress_mwc32modn(workload_slice_us - workload_quanta_us) +
					       stress_mwc32modn(workload_slice_us - workload_quanta_us)) / 3.0;
			workload[i].run_duration_sec = run_duration_sec;
		}
		break;
	case STRESS_WORKLOAD_DIST_CLUSTER:
		offset = stress_mwc32modn(workload_slice_us / 2);
		for (i = 0; i < (max_quanta * 2) / 3; i++) {
			workload[i].when_us = (double)(stress_mwc32modn(workload_quanta_us) + offset);
			workload[i].run_duration_sec = run_duration_sec;
		}
		for (; i < max_quanta; i++) {
			workload[i].when_us = (double)stress_mwc32modn(workload_slice_us - workload_quanta_us);
			workload[i].run_duration_sec = run_duration_sec;
		}
		break;
	case STRESS_WORKLOAD_DIST_POISSON:
		sum = 0.0;
		for (i = 0; i < max_quanta; i++) {
			double rnd = (double)stress_mwc32() * scale32bit;
			double val = -log(1.0 - rnd);

			sum += val;
			workload[i].when_us = sum;
		}
		scale = sum > 0.0 ? (workload_slice_us - workload_quanta_us) / sum : 0.0;
		for (i = 0; i < max_quanta; i++) {
			workload[i].when_us *= scale;
			workload[i].run_duration_sec = run_duration_sec;
		}
		break;
	case STRESS_WORKLOAD_DIST_EVEN:
		scale = (double)workload_slice_us / (double)max_quanta;
		for (i = 0; i < max_quanta; i++) {
			workload[i].when_us = (double)i * scale;
			workload[i].run_duration_sec = run_duration_sec;
		}
		break;
	}

	qsort(workload, max_quanta, sizeof(*workload), stress_workload_cmp);

	t_begin = stress_time_now();
	t_end = t_begin + ((double)workload_slice_us * scale_us_to_sec);

	for (i = 0; i < max_quanta; i++) {
		const double run_when = t_begin + (workload[i].when_us * scale_us_to_sec);

		sleep_duration_ns = (run_when - stress_time_now()) * STRESS_DBL_NANOSECOND;
		if (sleep_duration_ns > 10000.0) {
			(void)shim_nanosleep_uint64((uint64_t)sleep_duration_ns);
		} else {
			(void)shim_sched_yield();
		}
		stress_workload_bucket_account(slice_offset_bucket, STRESS_DBL_MICROSECOND * (stress_time_now() - t_begin));
		if (run_duration_sec > 0.0) {
			if (workload_threads) {
#if defined(WORKLOAD_THREADED)
				double sleep_secs;

				if (i == (max_quanta - 1)) {
					sleep_secs = t_end - stress_time_now();
				} else {
					sleep_secs = (workload[i + 1].when_us - workload[i].when_us) / STRESS_DBL_MICROSECOND;
				}
				(void)mq_send(mq, (const char *)&workload[i], sizeof(workload[i]), 0);
				if (sleep_secs > 0.0)
					(void)shim_nanosleep_uint64((uint64_t)(run_duration_sec * STRESS_DBL_NANOSECOND));
#else
				stress_workload_waste_time(workload_method, run_duration_sec, buffer, buffer_len);
#endif
			} else {
				stress_workload_waste_time(workload_method, run_duration_sec, buffer, buffer_len);
			}
		}
		stress_bogo_inc(args);
	}
	sleep_duration_ns = (t_end - stress_time_now()) * STRESS_DBL_NANOSECOND;
	if (sleep_duration_ns > 100.0)
		(void)shim_nanosleep_uint64((uint64_t)sleep_duration_ns);

	return EXIT_SUCCESS;
}

#if defined(WORKLOAD_THREADED)
static void *stress_workload_thread(void *ctxt)
{
	stress_workload_ctxt_t *c = (stress_workload_ctxt_t *)ctxt;

	for (;;) {
		unsigned int prio;
		ssize_t ret;
		stress_workload_t wl;

		ret = mq_receive(c->mq, (char *)&wl, sizeof(wl), &prio);
		if (ret == sizeof(wl))
			stress_workload_waste_time(c->workload_method, wl.run_duration_sec, c->buffer, c->buffer_len);
		else {
			if ((errno == EINTR) || (errno == ETIMEDOUT)) {
				continue;
			}
			break;
		}
	}
	return NULL;
}
#endif

static int stress_workload(stress_args_t *args)
{
	uint32_t workload_load = 30;
	uint32_t workload_slice_us = 100000;	/* 1/10th second */
	uint32_t workload_quanta_us = 1000;	/* 1/1000th second */
	uint32_t workload_threads = 2;		/* 0 = disabled */
	uint32_t max_quanta;
	size_t workload_sched = 0;		/* undefined */
	size_t workload_dist_idx = 0;
	size_t workload_method_idx = 0;
	int workload_dist, workload_method;
	stress_workload_t *workload;
	uint8_t *buffer;
	const size_t buffer_len = MB;
	stress_workload_bucket_t slice_offset_bucket;
	int rc = EXIT_SUCCESS;
#if defined(WORKLOAD_THREADED)
	workload_thread_t *threads = NULL;
	char mq_name[64];
	mqd_t mq = (mqd_t)-1;
	uint32_t i;
#endif

	(void)stress_get_setting("workload-dist", &workload_dist_idx);
	(void)stress_get_setting("workload-load", &workload_load);
	(void)stress_get_setting("workload-method", &workload_method_idx);
	(void)stress_get_setting("workload-quanta-us", &workload_quanta_us);
	(void)stress_get_setting("workload-sched", &workload_sched);
	(void)stress_get_setting("workload-slice-us", &workload_slice_us);
	(void)stress_get_setting("workload-threads", &workload_threads);

	workload_method = workload_methods[workload_method_idx].method;
	workload_dist = workload_dists[workload_dist_idx].type;

	if (stress_instance_zero(args)) {
		uint32_t timer_slack_ns;

		if (!stress_get_setting("timer-slack", &timer_slack_ns))
			timer_slack_ns = 50000;

		if (workload_quanta_us < timer_slack_ns / 1000) {
			pr_inf("%s: workload-quanta-us %" PRIu32 " is less than the "
				"timer_slack duration, use --timer-slack %" PRIu32
				" for best results\n", args->name,
				workload_quanta_us, workload_quanta_us * 1000);
		}
	}

	buffer = (uint8_t *)stress_mmap_populate(NULL, buffer_len,
				PROT_READ | PROT_WRITE,
				MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (buffer == MAP_FAILED) {
		pr_inf_skip("%s: failed to mmap %zu byte buffer%s, "
			"errno=%d (%s), skipping stressor\n",
			args->name, buffer_len,
			stress_get_memfree_str(), errno, strerror(errno));
		return EXIT_NO_RESOURCE;
	}
	(void)stress_madvise_nohugepage(buffer, buffer_len);
	stress_set_vma_anon_name(buffer, buffer_len, "workload-buffer");

	if (workload_threads > 0) {
#if defined(WORKLOAD_THREADED)
		struct mq_attr attr;
		static stress_workload_ctxt_t c;
		uint32_t threads_started = 0;

		(void)snprintf(mq_name, sizeof(mq_name), "/%s-%" PRIdMAX "-%" PRIu32,
			args->name, (intmax_t)args->pid, args->instance);
		attr.mq_flags = 0;
		attr.mq_maxmsg = 10;
		attr.mq_msgsize = sizeof(stress_workload_t);
                attr.mq_curmsgs = 0;
		mq = mq_open(mq_name, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR, &attr);
		if (mq == (mqd_t)-1) {
			pr_inf_skip("%s: cannot create message queue, errno=%d (%s), "
				"skipping stressor\n", args->name,
				errno, strerror(errno));
			rc = EXIT_NO_RESOURCE;
			goto exit_free_buffer;
		}
		threads = (workload_thread_t *)calloc((size_t)workload_threads, sizeof(*threads));
		if (!threads) {
			pr_inf_skip("%s: failed to allocate %" PRIu32 " thread "
				"descriptors%s, skipping stressor\n",
				args->name, workload_threads, stress_get_memfree_str());
			rc = EXIT_NO_RESOURCE;
			goto exit_close_mq;
		}

		c.buffer = buffer;
		c.buffer_len = buffer_len;
		c.workload_method = workload_method;
		c.mq = mq;
		for (i = 0; i < workload_threads; i++) {
			threads[i].ret = pthread_create(&threads[i].pthread, NULL,
                                stress_workload_thread, (void *)&c);
			if (threads[i].ret == 0)
				threads_started++;
		}
		if (threads_started == 0) {
			pr_inf_skip("%s: no threads started, skipping stressor\n",
				args->name);
			rc = EXIT_NO_RESOURCE;
			goto exit_free_threads;
		}
#else
		if (stress_instance_zero(args)) {
			pr_inf("%s: %" PRIu32 " workload threads were requested but "
				"system does not have pthread or POSIX message queue "
				"support, dropping back to single process workload "
				"worker\n", args->name, workload_threads);
		}
		workload_threads = 0;
#endif
	}
	if (stress_instance_zero(args))
		pr_inf("%s: running with %" PRIu32 " threads per stressor instance\n",
			args->name, workload_threads);

	if (workload_quanta_us > workload_slice_us) {
		pr_err("%s: workload-quanta-us %" PRIu32 " must be less "
			"than workload-slice-us %" PRIu32 "\n",
			args->name, workload_quanta_us, workload_slice_us);
		rc =  EXIT_FAILURE;
#if defined(WORKLOAD_THREADED)
		goto exit_free_threads;
#else
		goto exit_free_buffer;
#endif
	}

	max_quanta = workload_slice_us / workload_quanta_us;
	if (max_quanta < 1)
		max_quanta = 1;
	/* Scale workload by number of worker threads */
	if (workload_threads > 0)
		max_quanta *= workload_threads;

	workload = (stress_workload_t *)calloc(max_quanta, sizeof(*workload));
	if (!workload) {
		pr_inf_skip("%s: cannot allocate %" PRIu32 " scheduler workload timings%s, "
			"skipping stressor\n", args->name, max_quanta,
			stress_get_memfree_str());
		rc = EXIT_NO_RESOURCE;
#if defined(WORKLOAD_THREADED)
		goto exit_free_threads;
#else
		goto exit_free_buffer;
#endif
	}

	stress_workload_bucket_init(&slice_offset_bucket, (double)workload_slice_us);

	(void)stress_workload_set_sched(args, workload_sched);

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		stress_workload_exercise(args,
#if defined(WORKLOAD_THREADED)
					mq,
#endif
					workload_method,
					workload_load,
					workload_slice_us,
					workload_quanta_us,
					workload_threads,
					max_quanta, workload_dist,
					workload,
					&slice_offset_bucket,
					buffer, buffer_len);
	} while (stress_continue(args));

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	if (stress_instance_zero(args))
		stress_workload_bucket_report(&slice_offset_bucket);

	free(workload);

#if defined(WORKLOAD_THREADED)
exit_free_threads:
	for (i = 0; threads && (i < workload_threads); i++) {
		if (threads[i].ret == 0) {
			VOID_RET(int, pthread_cancel(threads[i].pthread));
			VOID_RET(int, pthread_join(threads[i].pthread, NULL));
		}
	}

exit_close_mq:
	if (mq != (mqd_t)-1) {
		(void)mq_close(mq);
		(void)mq_unlink(mq_name);
	}

	free(threads);
#endif
exit_free_buffer:
	(void)munmap((void *)buffer, buffer_len);
	return rc;
}

const stressor_info_t stress_workload_info = {
	.stressor = stress_workload,
	.classifier = CLASS_SCHEDULER | CLASS_OS,
	.opts = opts,
	.verify = VERIFY_ALWAYS,
	.help = help
};
