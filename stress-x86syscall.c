/*
 * Copyright (C) 2013-2020 Canonical, Ltd.
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
 * This code is a complete clean re-write of the stress tool by
 * Colin Ian King <colin.king@canonical.com> and attempts to be
 * backwardly compatible with the stress tool by Amos Waterland
 * <apw@rossby.metr.ou.edu> but has more stress tests and more
 * functionality.
 *
 */
#include "stress-ng.h"

static const stress_help_t help[] = {
	{ NULL,	"x86syscall N",		"start N workers exercising functions using syscall" },
	{ NULL,	"x86syscall-ops N",	"stop after N syscall function calls" },
	{ NULL,	"x86syscall-func F",	"use just syscall function F" },
	{ NULL,	NULL,		NULL }
};

/*
 *  stress_set_x86syscall_func()
 *      set the default x86syscall function
 */
static int stress_set_x86syscall_func(const char *name)
{
	return stress_set_setting("x86syscall-func", TYPE_ID_STR, name);
}

static const stress_opt_set_func_t opt_set_funcs[] = {
	{ OPT_x86syscall_func,	stress_set_x86syscall_func },
	{ 0,			NULL }
};

#if defined(__linux__) &&	\
    (defined(__x86_64__) || defined(__x86_64)) && \
    defined(HAVE_CPUID_H) &&    \
    defined(HAVE_CPUID) &&      \
    NEED_GNUC(4,6,0)

typedef int (*stress_wfunc_t)(void);

/*
 *  syscall symbol mapping name to address and wrapper function
 */
typedef struct stress_x86syscall {
	const stress_wfunc_t func;	/* Wrapper function */
	const char *name;	/* Function name */
	bool exercise;		/* True = exercise the syscall */
} stress_x86syscall_t;

/*
 *  stress_x86syscall_supported()
 *	check if tsc is supported
 */
static int stress_x86syscall_supported(void)
{
	uint32_t eax, ebx, ecx, edx;

	/* Intel CPU? */
	if (!stress_cpu_is_x86()) {
		pr_inf("x86syscall stressor will be skipped, "
			"not a recognised Intel CPU\n");
		return -1;
	}
	/* ..and supports syscall? */
	__cpuid(0x80000001, eax, ebx, ecx, edx);
	if (!(edx & (1ULL << 11))) {
		pr_inf("x86syscall stressor will be skipped, CPU "
			"does not support the syscall instruction\n");
		return -1;
	}
	return 0;
}

/*
 *  x86_64_syscall1()
 *	syscall 1 arg wrapper
 */
static inline long x86_64_syscall1(long number, long arg1)
{
	long ret;
	unsigned long _arg1 = arg1;
	register long __arg1 asm ("rdi") = _arg1;

	asm volatile ("syscall\n\t"
			: "=a" (ret)
			: "0" (number), "r" (__arg1)
			: "memory", "cc", "r11", "cx");
	if (ret < 0) {
		errno = ret;
		ret = -1;
	}
	return ret;
}

/*
 *  x86_64_syscall2()
 *	syscall 2 arg wrapper
 */
static inline long x86_64_syscall2(long number, long arg1, long arg2)
{
	long ret;
	unsigned long _arg1 = arg1;
	unsigned long _arg2 = arg2;
	register long __arg1 asm ("rdi") = _arg1;
	register long __arg2 asm ("rsi") = _arg2;

	asm volatile ("syscall\n\t"
			: "=a" (ret)
			: "0" (number), "r" (__arg1), "r" (__arg2)
			: "memory", "cc", "r11", "cx");
	if (ret < 0) {
		errno = ret;
		ret = -1;
	}
	return ret;
}

/*
 *  x86_64_syscall3()
 *	syscall 3 arg wrapper
 */
static inline long x86_64_syscall3(long number, long arg1, long arg2, long arg3)
{
	long ret;
	unsigned long _arg1 = arg1;
	unsigned long _arg2 = arg2;
	unsigned long _arg3 = arg3;
	register long __arg1 asm ("rdi") = _arg1;
	register long __arg2 asm ("rsi") = _arg2;
	register long __arg3 asm ("rdx") = _arg3;

	asm volatile ("syscall\n\t"
			: "=a" (ret)
			: "0" (number), "r" (__arg1), "r" (__arg2), "r" (__arg3)
			: "memory", "cc", "r11", "cx");
	if (ret < 0) {
		errno = ret;
		ret = -1;
	}
	return ret;
}

/*
 *  wrap_getcpu()
 *	invoke getcpu()
 */
static int wrap_getcpu(void)
{
	unsigned cpu, node;

	return x86_64_syscall3(__NR_getcpu, (long)&cpu, (long)&node, (long)NULL);
}

/*
 *  wrap_gettimeofday()
 *	invoke gettimeofday()
 */
static int wrap_gettimeofday(void)
{
	struct timeval tv;

	return x86_64_syscall2(__NR_gettimeofday, (long)&tv, (long)NULL);
}

/*
 *  wrap_time()
 *	invoke time()
 */
static int wrap_time(void)
{
	time_t t;

	return x86_64_syscall1(__NR_time, (long)&t);
}

/*
 *  wrap_dummy()
 *	dummy empty function for baseline
 */
static int wrap_dummy(void)
{
	int ret = -1;

	return ret;
}

/*
 *  mapping of wrappers to function symbol name
 */
static stress_x86syscall_t x86syscalls[] = {
	{ wrap_getcpu,		"getcpu",		true },
	{ wrap_gettimeofday,	"gettimeofday",		true },
	{ wrap_time,		"time",			true },
};

/*
 *  mapping of wrappers for instrumentation measurement,
 *  MUST NOT be static to avoid optimizer from removing the
 *  indirect calls
 */
stress_x86syscall_t ___dummy_x86syscalls[] = {
	{ wrap_dummy,		"dummy",		true },
};

/*
 *  x86syscall_list_str()
 *	gather symbol names into a string
 */
static char *x86syscall_list_str(void)
{
	char *str = NULL;
	size_t i, len = 0;

	for (i = 0; i < SIZEOF_ARRAY(x86syscalls); i++) {
		if (x86syscalls[i].exercise) {
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
				(void)strcat(tmp, " ");
				str = tmp;
			}
			(void)strcat(tmp, x86syscalls[i].name);
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
		x86syscalls[i].exercise = match;
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
static int stress_x86syscall(const stress_args_t *args)
{
	char *str;
	double t1, t2, t3, overhead_ns;
	uint64_t counter;

	if (x86syscall_check_x86syscall_func() < 0)
		return EXIT_FAILURE;

	if (args->instance == 0) {
		str = x86syscall_list_str();
		if (str) {
			pr_inf("%s: exercising syscall on: %s\n",
				args->name, str);
			free(str);
		}
	}

	t1 = stress_time_now();
	do {
		size_t i;

		for (i = 0; i < SIZEOF_ARRAY(x86syscalls); i++) {
			if (x86syscalls[i].exercise) {
				x86syscalls[i].func();
				inc_counter(args);
			}
		}
	} while (keep_stressing());
	t2 = stress_time_now();

	/*
	 *  And spend 1/10th of a second measuring overhead of
	 *  the test framework
	 */
	counter = get_counter(args);
	do {
		int j;

		for (j = 0; j < 1000000; j++) {
			if (___dummy_x86syscalls[0].exercise) {
				___dummy_x86syscalls[0].func();
				inc_counter(args);
			}
		}
		t3 = stress_time_now();
	} while (t3 - t2 < 0.1);

	overhead_ns = 1000000000.0 * ((t3 - t2) / (double)(get_counter(args) - counter));
	set_counter(args, counter);

	pr_inf("%s: %.2f nanoseconds per call (excluding %.2f nanoseconds test overhead)\n",
		args->name,
		((((t2 - t1) ) * 1000000000.0) / (double)get_counter(args)) - overhead_ns,
		overhead_ns);

	return EXIT_SUCCESS;
}

stressor_info_t stress_x86syscall_info = {
	.stressor = stress_x86syscall,
	.class = CLASS_OS,
	.supported = stress_x86syscall_supported,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
#else
stressor_info_t stress_x86syscall_info = {
	.stressor = stress_not_implemented,
	.class = CLASS_OS,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
#endif
