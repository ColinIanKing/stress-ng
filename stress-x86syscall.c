/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2025 Colin Ian King.
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
#include "core-arch.h"
#include "core-cpu.h"

#include <time.h>

static const stress_help_t help[] = {
	{ NULL,	"x86syscall N",		"start N workers exercising functions using syscall" },
	{ NULL,	"x86syscall-func F",	"use just syscall function F" },
	{ NULL,	"x86syscall-ops N",	"stop after N syscall function calls" },
	{ NULL,	NULL,			NULL }
};

static const stress_opt_t opts[] = {
	{ OPT_x86syscall_func, "x86syscall-func", TYPE_ID_STR, 0, 0, NULL },
	END_OPT,
};

#if defined(__linux__) &&		\
    !defined(HAVE_COMPILER_PCC) &&	\
    defined(STRESS_ARCH_X86_64)

typedef long int (*stress_wrapper_func_t)(void);

/*
 *  syscall symbol mapping name to address and wrapper function
 */
typedef struct stress_x86syscall {
	const stress_wrapper_func_t func;	/* Wrapper function */
	const char *name;			/* Function name */
} stress_x86syscall_t;

/*
 *  stress_x86syscall_supported()
 *	check if tsc is supported
 */
static int stress_x86syscall_supported(const char *name)
{
	/* Intel CPU? */
	if (!stress_cpu_is_x86()) {
		pr_inf_skip("%s stressor will be skipped, "
			"not a recognised Intel CPU\n", name);
		return -1;
	}
	/* ..and supports syscall? */
	if (!stress_cpu_x86_has_syscall()) {
		pr_inf_skip("%s stressor will be skipped, CPU "
			"does not support the syscall instruction\n", name);
		return -1;
	}

#if defined(__NR_getcpu) ||		\
    defined(__NR_geteuid) ||		\
    defined(__NR_getgid) ||		\
    defined(__NR_getpid) ||		\
    defined(__NR_gettimeofday) ||	\
    defined(__NR_getuid) ||		\
    defined(__NR_time)
	return 0;
#else
	pr_inf_skip("%s: stressor will be skipped, no definitions for __NR_getpid, __NR_getcpu, __NR_gettimeofday or __NR_time\n", name);
	return -1;
#endif
}

/*
 *  x86_64_syscall0()
 *	syscall 0 arg wrapper
 */
static inline long int OPTIMIZE3 x86_64_syscall0(long int number)
{
	long int ret;

	__asm__ __volatile__("syscall\n\t"
			: "=a" (ret)
			: "0" (number)
			: "memory", "cc", "r11", "cx");
	if (UNLIKELY(ret < 0)) {
		errno = (int)ret;
		ret = -1;
	}
	return ret;
}

/*
 *  x86_64_syscall1()
 *	syscall 1 arg wrapper
 */
static inline long int OPTIMIZE3 x86_64_syscall1(long int number, long int arg1)
{
	long int ret;
	long int tmp_arg1 = arg1;
	register long int asm_arg1 __asm__("rdi") = tmp_arg1;

	__asm__ __volatile__("syscall\n\t"
			: "=a" (ret)
			: "0" (number), "r" (asm_arg1)
			: "memory", "cc", "r11", "cx");
	if (UNLIKELY(ret < 0)) {
		errno = (int)ret;
		ret = -1;
	}
	return ret;
}

/*
 *  x86_64_syscall2()
 *	syscall 2 arg wrapper
 */
static inline long int OPTIMIZE3 x86_64_syscall2(long int number, int long arg1, int long arg2)
{
	long int ret;
	long int tmp_arg1 = arg1;
	long int tmp_arg2 = arg2;
	register long int asm_arg1 __asm__("rdi") = tmp_arg1;
	register long int asm_arg2 __asm__("rsi") = tmp_arg2;

	__asm__ __volatile__("syscall\n\t"
			: "=a" (ret)
			: "0" (number), "r" (asm_arg1), "r" (asm_arg2)
			: "memory", "cc", "r11", "cx");
	if (UNLIKELY(ret < 0)) {
		errno = (int)ret;
		ret = -1;
	}
	return ret;
}

/*
 *  x86_64_syscall3()
 *	syscall 3 arg wrapper
 */
static inline long int OPTIMIZE3 x86_64_syscall3(long int number, long int arg1, long int arg2, long int arg3)
{
	long int ret;
	long int tmp_arg1 = arg1;
	long int tmp_arg2 = arg2;
	long int tmp_arg3 = arg3;
	register long int asm_arg1 __asm__("rdi") = tmp_arg1;
	register long int asm_arg2 __asm__("rsi") = tmp_arg2;
	register long int asm_arg3 __asm__("rdx") = tmp_arg3;

	__asm__ __volatile__("syscall\n\t"
			: "=a" (ret)
			: "0" (number), "r" (asm_arg1), "r" (asm_arg2), "r" (asm_arg3)
			: "memory", "cc", "r11", "cx");
	if (UNLIKELY(ret < 0)) {
		errno = (int)ret;
		ret = -1;
	}
	return ret;
}

#if defined(__NR_getuid)
/*
 *  wrap_getuid()
 *	invoke getuid()
 */
static long int OPTIMIZE3 wrap_getuid(void)
{
	return x86_64_syscall0(__NR_getuid);
}
#endif

#if defined(__NR_geteuid)
/*
 *  wrap_geteuid()
 *	invoke geteuid()
 */
static long int OPTIMIZE3 wrap_geteuid(void)
{
	return x86_64_syscall0(__NR_geteuid);
}
#endif

#if defined(__NR_getgid)
/*
 *  wrap_getgid()
 *	invoke getgid()
 */
static long int OPTIMIZE3 wrap_getgid(void)
{
	return x86_64_syscall0(__NR_getgid);
}
#endif

#if defined(__NR_getpid)
/*
 *  wrap_getpid()
 *	invoke getpid()
 */
static long int OPTIMIZE3 wrap_getpid(void)
{
	return x86_64_syscall0(__NR_getpid);
}
#endif

#if defined(__NR_getcpu)
/*
 *  wrap_getcpu()
 *	invoke getcpu()
 */
static long int wrap_getcpu(void)
{
	unsigned int cpu, node;

	return x86_64_syscall3(__NR_getcpu, (long int)&cpu, (long int)&node, (long int)NULL);
}
#endif

#if defined(__NR_gettimeofday)
/*
 *  wrap_gettimeofday()
 *	invoke gettimeofday()
 */
static long int wrap_gettimeofday(void)
{
	struct timeval tv;

	return x86_64_syscall2(__NR_gettimeofday, (long int)&tv, (long int)NULL);
}
#endif

#if defined(__NR_time)
/*
 *  wrap_time()
 *	invoke time()
 */
static long int wrap_time(void)
{
	time_t t;

	return x86_64_syscall1(__NR_time, (long int)&t);
}
#endif

/*
 *  wrap_dummy()
 *	dummy empty function for baseline
 */
static long int wrap_dummy(void)
{
	return (long int)-1;
}

/*
 *  mapping of wrappers to function symbol name
 */
static const stress_x86syscall_t x86syscalls[] = {
#if defined(__NR_getcpu)
	{ wrap_getcpu,		"getcpu" },
#endif
#if defined(__NR_geteuid)
	{ wrap_geteuid,		"geteuid" },
#endif
#if defined(__NR_getgid)
	{ wrap_getgid,		"getgid" },
#endif
#if defined(__NR_getpid)
	{ wrap_getpid,		"getpid" },
#endif
#if defined(__NR_gettimeofday)
	{ wrap_gettimeofday,	"gettimeofday" },
#endif
#if defined(__NR_getuid)
	{ wrap_getuid,		"getuid" },
#endif
#if defined(__NR_time)
	{ wrap_time,		"time" },
#endif
};

static bool x86syscalls_exercise[SIZEOF_ARRAY(x86syscalls)];

/*
 *  x86syscall_list_str()
 *	gather symbol names into a string
 */
static char *x86syscall_list_str(void)
{
	char *str = NULL;
	size_t i, len = 0;

	for (i = 0; i < SIZEOF_ARRAY(x86syscalls); i++) {
		if (x86syscalls_exercise[i]) {
			char *tmp;

			len += (strlen(x86syscalls[i].name) + 2);
			tmp = realloc(str, len);
			if (!tmp) {
				free(str);
				return NULL;
			}
			if (!str) {
				*tmp = '\0';
			} else {
				(void)shim_strlcat(tmp, " ", len);
			}
			(void)shim_strlcat(tmp, x86syscalls[i].name, len);
			str = tmp;
		}
	}
	return str;
}

/*
 *  x86syscall_check_x86syscall_func()
 *	if a x86syscall-func has been specified, locate it and
 *	mark it to be exercised.
 */
static int x86syscall_check_x86syscall_func(void)
{
	char *name;
	size_t i;
	bool exercise = false;

	if (!stress_get_setting("x86syscall-func", &name))
		return 0;

	for (i = 0; i < SIZEOF_ARRAY(x86syscalls); i++) {
		const bool match = !strcmp(x86syscalls[i].name, name);

		exercise |= match;
		x86syscalls_exercise[i] = match;
	}

	if (!exercise) {
		(void)fprintf(stderr, "invalid x86syscall-func '%s', must be one of:", name);
		for (i = 0; i < SIZEOF_ARRAY(x86syscalls); i++)
			(void)fprintf(stderr, " %s", x86syscalls[i].name);
		(void)fprintf(stderr, "\n");
		return -1;
	}
	return 0;
}

/*
 *  stress_x86syscall()
 *	stress x86 syscall instruction
 */
static int stress_x86syscall(stress_args_t *args)
{
	double t1, t2, t3, t4, dt, overhead_ns;
	uint64_t counter;
	stress_wrapper_func_t x86syscall_funcs[SIZEOF_ARRAY(x86syscalls)] ALIGN64;
	register size_t i, n;
	int rc = EXIT_SUCCESS;

	for (i = 0; i < SIZEOF_ARRAY(x86syscalls); i++)
		x86syscalls_exercise[i] = true;

	if (x86syscall_check_x86syscall_func() < 0)
		return EXIT_FAILURE;

	if (stress_instance_zero(args)) {
		char *str = x86syscall_list_str();

		if (str) {
			pr_inf("%s: exercising syscall on: %s\n",
				args->name, str);
			free(str);
		}
	}

	for (i = 0, n = 0; i < SIZEOF_ARRAY(x86syscalls); i++) {
		if (x86syscalls_exercise[i])
			x86syscall_funcs[n++] = x86syscalls[i].func;
	}

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	t1 = stress_time_now();
	do {
		for (i = 0; i < n; i++)
			x86syscall_funcs[i]();
		stress_bogo_add(args, n);
	} while (stress_continue(args));
	t2 = stress_time_now();

	/*
	 *  And spend 1/10th of a second measuring overhead of
	 *  the test framework
	 */
	for (i = 0; i < n; i++) {
		x86syscall_funcs[i] = wrap_dummy;
	}
	counter = stress_bogo_get(args);
	t3 = stress_time_now();
	do {
		register int j;

		for (j = 0; j < 1000000; j++) {
			for (i = 0; i < n; i++)
				x86syscall_funcs[i]();
			stress_bogo_add(args, n);
		}
		t4 = stress_time_now();
	} while (t4 - t3 < 0.1);

	overhead_ns = (double)STRESS_NANOSECOND * ((t4 - t3) / (double)(stress_bogo_get(args) - counter));
	stress_bogo_set(args, counter);

	dt = t2 - t1;
	if (dt > 0.0) {
		const uint64_t c = stress_bogo_get(args);
		const double ns = ((dt * (double)STRESS_NANOSECOND) / (double)c) - overhead_ns;

		stress_metrics_set(args, 0, "nanosecs per call (excluding test overhead",
			ns, STRESS_METRIC_HARMONIC_MEAN);
		stress_metrics_set(args, 1, "nanosecs for test overhead",
			overhead_ns, STRESS_METRIC_HARMONIC_MEAN);
	}

	/*
	 *  And now some simple verification
	 */
#if defined(__NR_getpid)
	{
		const pid_t pid1 = getpid();
		const pid_t pid2 = (pid_t)x86_64_syscall0(__NR_getpid);

		if (pid1 != pid2) {
			pr_fail("%s: getpid syscall returned PID %" PRIdMAX ", "
				"expected PID %" PRIdMAX "\n",
				args->name, (intmax_t)pid2, (intmax_t)pid1);
			rc = EXIT_FAILURE;
		}
	}
#endif
#if defined(__NR_getgid)
	{
		const pid_t gid1 = getgid();
		const pid_t gid2 = (gid_t)x86_64_syscall0(__NR_getgid);

		if (gid1 != gid2) {
			pr_fail("%s: getgid syscall returned GID %" PRIdMAX ", "
				"expected GID %" PRIdMAX "\n",
				args->name, (intmax_t)gid2, (intmax_t)gid1);
			rc = EXIT_FAILURE;
		}
	}
#endif
#if defined(__NR_getuid)
	{
		const uid_t uid1 = getuid();
		const uid_t uid2 = (uid_t)x86_64_syscall0(__NR_getuid);

		if (uid1 != uid2) {
			pr_fail("%s: getuid syscall returned UID %" PRIdMAX ", "
				"expected UID %" PRIdMAX "\n",
				args->name, (intmax_t)uid2, (intmax_t)uid1);
			rc = EXIT_FAILURE;
		}
	}
#endif
#if defined(__NR_geteuid)
	{
		const uid_t uid1 = geteuid();
		const uid_t uid2 = (uid_t)x86_64_syscall0(__NR_geteuid);

		if (uid1 != uid2) {
			pr_fail("%s: geteuid syscall returned UID %" PRIdMAX ", "
				"expected UID %" PRIdMAX "\n",
				args->name, (intmax_t)uid2, (intmax_t)uid1);
			rc = EXIT_FAILURE;
		}
	}
#endif
#if defined(__NR_time)
	{
		time_t time1 = 0, time2 = 0;

		if ((time(&time1) != (time_t)-1) &&
		    ((time_t)x86_64_syscall1(__NR_time, (long int)&time2) != (time_t)-1)) {
			if (time2 < time1) {
				pr_fail("%s: time syscall returned %" PRIdMAX
					" which was less than expected value %" PRIdMAX "\n",
					args->name, (intmax_t)time2, (intmax_t)time1);
				rc = EXIT_FAILURE;
			}
		}
	}
#endif
#if defined(__NR_gettimeofday)
	{
		struct timeval tv1, tv2;

		if ((gettimeofday(&tv1, NULL) != -1) &&
		    ((int)x86_64_syscall2(__NR_gettimeofday, (long)&tv2, (long)NULL) != -1)) {
			const double td1 = stress_timeval_to_double(&tv1);
			const double td2 = stress_timeval_to_double(&tv2);

			if (td2 < td1) {
				pr_fail("%s: gettimeofday syscall returned %.6f"
					" which was less than expected value %.6f\n",
					args->name, td2, td1);
				rc = EXIT_FAILURE;
			}
		}
	}
#endif
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	return rc;
}

const stressor_info_t stress_x86syscall_info = {
	.stressor = stress_x86syscall,
	.classifier = CLASS_OS,
	.supported = stress_x86syscall_supported,
	.opts = opts,
	.verify = VERIFY_ALWAYS,
	.help = help
};
#else
const stressor_info_t stress_x86syscall_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_OS,
	.opts = opts,
	.help = help,
	.verify = VERIFY_ALWAYS,
	.unimplemented_reason = "only supported on Linux x86-64 and non-PCC compilers"
};
#endif
