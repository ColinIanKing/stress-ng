/*
 * Copyright (C) 2014-2021 Canonical, Ltd.
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
#include "core-stack.h"

#include <ctype.h>

#if defined(HAVE_SYS_AUXV_H)
#include <sys/auxv.h>
#endif
#if defined(HAVE_EXECINFO_H)
#include <execinfo.h>
#endif

#define BACKTRACE_BUF_SIZE		(64)
#define STRESS_ABS_MIN_STACK_SIZE	(64 * 1024)

static bool stress_stack_check_flag;

/*
 *  stress_get_stack_direction_helper()
 *	helper to determine direction of stack
 */
static ssize_t NOINLINE OPTIMIZE0 stress_get_stack_direction_helper(const uint8_t *val1)
{
	const uint8_t val2 = *val1;
	const ssize_t diff = &val2 - (const uint8_t *)val1;

	return (diff > 0) - (diff < 0);
}

/*
 *  stress_get_stack_direction()
 *      determine which way the stack goes, up / down
 *	just pass in any var on the stack before calling
 *	return:
 *		 1 - stack goes down (conventional)
 *		 0 - error
 *	  	-1 - stack goes up (unconventional)
 */
ssize_t stress_get_stack_direction(void)
{
	uint8_t val1 = 0;
	uint8_t waste[64];

	waste[(sizeof waste) - 1] = 0;
	return stress_get_stack_direction_helper(&val1);
}

/*
 *  stress_get_stack_top()
 *	Get the stack top given the start and size of the stack,
 *	offset by a bit of slop. Assumes stack is > 64 bytes
 */
void *stress_get_stack_top(void *start, const size_t size)
{
	const size_t offset = stress_get_stack_direction() < 0 ? (size - 64) : 64;

	return (void *)((char *)start + offset);
}

/*
 *  stress_sigaltstack_no_check()
 *	attempt to set up an alternative signal stack with no
 *	minimum size check on stack
 *	  stack - must be at least MINSIGSTKSZ
 *	  size  - size of stack (- STACK_ALIGNMENT)
 */
int stress_sigaltstack_no_check(void *stack, const size_t size)
{
#if defined(HAVE_SIGALTSTACK)
	stack_t ss;

	if (stack == NULL) {
		ss.ss_sp = NULL;
		ss.ss_size = 0;
		ss.ss_flags = SS_DISABLE;
	} else {
		ss.ss_sp = (void *)stack;
		ss.ss_size = size;
		ss.ss_flags = 0;
	}
	return sigaltstack(&ss, NULL);
#else
	UNEXPECTED
	(void)stack;
	(void)size;
	return 0;
#endif
}

/*
 *  stress_sigaltstack()
 *	attempt to set up an alternative signal stack
 *	  stack - must be at least MINSIGSTKSZ
 *	  size  - size of stack (- STACK_ALIGNMENT)
 */
int stress_sigaltstack(void *stack, const size_t size)
{
#if defined(HAVE_SIGALTSTACK)
	if (stack && (size < (size_t)STRESS_MINSIGSTKSZ)) {
		pr_err("sigaltstack stack size %zu must be more than %zuK\n",
			size, (size_t)STRESS_MINSIGSTKSZ / 1024);
		return -1;
	}

	if (stress_sigaltstack_no_check(stack, size) < 0) {
		pr_fail("sigaltstack failed, errno=%d (%s)\n",
			errno, strerror(errno));
		return -1;
	}
#else
	UNEXPECTED
	(void)stack;
	(void)size;
#endif
	return 0;
}

/*
 *  stress_sigaltstack_disable()
 *	disable the alternative signal stack
 */
void stress_sigaltstack_disable(void)
{
#if defined(HAVE_SIGALTSTACK)
	stack_t ss;

	ss.ss_sp = NULL;
	ss.ss_size = 0;
	ss.ss_flags = SS_DISABLE;

	sigaltstack(&ss, NULL);
#endif
	return;
}

/*
 *  stress_get_min_aux_sig_stack_size()
 *	For ARM we should check AT_MINSIGSTKSZ as this
 *	also includes SVE register saving overhead
 *	https://blog.linuxplumbersconf.org/2017/ocw/system/presentations/4671/original/plumbers-dm-2017.pdf
 */
static inline long int stress_get_min_aux_sig_stack_size(void)
{
#if defined(HAVE_SYS_AUXV_H) && \
    defined(HAVE_GETAUXVAL) &&	\
    defined(AT_MINSIGSTKSZ)
	const long int sz = (long int)getauxval(AT_MINSIGSTKSZ);

	if (LIKELY(sz > 0))
		return sz;
#else
	UNEXPECTED
#endif
	return -1;
}

/*
 *  stress_get_sig_stack_size()
 *	wrapper for STRESS_SIGSTKSZ, try and find
 *	stack size required
 */
size_t stress_get_sig_stack_size(void)
{
	static long int sz = -1;
	long int min;
#if defined(_SC_SIGSTKSZ) ||	\
    defined(SIGSTKSZ)
	long int tmp;
#endif

	/* return cached copy */
	if (LIKELY(sz > 0))
		return (size_t)sz;

	min = stress_get_min_aux_sig_stack_size();
#if defined(_SC_SIGSTKSZ)
	tmp = sysconf(_SC_SIGSTKSZ);
	if (tmp > 0)
		min = STRESS_MAXIMUM(tmp, min);
#endif
#if defined(SIGSTKSZ)
	tmp = SIGSTKSZ;
	if (tmp > 0)
		min = STRESS_MAXIMUM(tmp, min);
#endif
	sz = STRESS_MAXIMUM(STRESS_ABS_MIN_STACK_SIZE, min);
	return (size_t)sz;
}

/*
 *  stress_get_min_sig_stack_size()
 *	wrapper for STRESS_MINSIGSTKSZ
 */
size_t stress_get_min_sig_stack_size(void)
{
	static long int sz = -1;
	long int min;
#if defined(_SC_MINSIGSTKSZ) ||	\
    defined(SIGSTKSZ)
	long int tmp;
#endif

	/* return cached copy */
	if (sz > 0)
		return (size_t)sz;

	min = stress_get_min_aux_sig_stack_size();
#if defined(_SC_MINSIGSTKSZ)
	tmp = sysconf(_SC_MINSIGSTKSZ);
	if (tmp > 0)
		min = STRESS_MAXIMUM(tmp, min);
#endif
#if defined(SIGSTKSZ)
	tmp = SIGSTKSZ;
	if (tmp > 0)
		min = STRESS_MAXIMUM(tmp, min);
#endif
	sz = STRESS_MAXIMUM(STRESS_ABS_MIN_STACK_SIZE, min);
	return (size_t)sz;
}

/*
 *  stress_get_min_pthread_stack_size()
 *	return the minimum size of stack for a pthread
 */
size_t stress_get_min_pthread_stack_size(void)
{
	static long int sz = -1;
	long int min, tmp;

	/* return cached copy */
	if (sz > 0)
		return (size_t)sz;

	min = stress_get_min_aux_sig_stack_size();
#if defined(__SC_THREAD_STACK_MIN_VALUE)
	tmp = sysconf(__SC_THREAD_STACK_MIN_VALUE);
	if (tmp > 0)
		min = STRESS_MAXIMUM(tmp, min);
#endif
#if defined(_SC_THREAD_STACK_MIN_VALUE)
	tmp = sysconf(_SC_THREAD_STACK_MIN_VALUE);
	if (tmp > 0)
		min = STRESS_MAXIMUM(tmp, min);
#endif
#if defined(PTHREAD_STACK_MIN)
	tmp = PTHREAD_STACK_MIN;
	if (tmp > 0)
		tmp = STRESS_MAXIMUM(tmp, 8192);
	else
		tmp = 8192;
#else
	tmp = 8192;
#endif
	sz = STRESS_MAXIMUM(tmp, min);
	return (size_t)sz;
}

/*
 *  __stack_chk_fail()
 *	override stack smashing callback
 */
#if defined(HAVE_COMPILER_GCC_OR_MUSL) &&	\
    !defined(HAVE_COMPILER_CLANG) &&		\
    defined(HAVE_WEAK_ATTRIBUTE)
extern void __stack_chk_fail(void);

NORETURN WEAK void __stack_chk_fail(void)
{
	if (stress_stack_check_flag) {
		(void)fprintf(stderr, "Stack overflow detected! Aborting stress-ng.\n");
		(void)fflush(stderr);
		abort();
	}
	/* silently exit */
	_exit(0);
}
#endif

/*
 *  stress_set_stack_smash_check_flag()
 *	set flag, true = report flag, false = silently ignore
 */
void stress_set_stack_smash_check_flag(const bool flag)
{
	stress_stack_check_flag = flag;
}

/*
 *  stress_backtrace
 *	dump stack trace to stdout, this could be called
 *	from a signal context so try to keep buffer small
 *	and fflush on all printfs to ensure we dump as
 *	much as possible.
 */
void stress_backtrace(void)
{
#if defined(HAVE_EXECINFO_H) &&	\
    defined(HAVE_BACKTRACE)
	int i, n_ptrs;
	void *buffer[BACKTRACE_BUF_SIZE];
	char **strings;

	n_ptrs = backtrace(buffer, BACKTRACE_BUF_SIZE);
	if (n_ptrs < 1)
		return;

	strings = backtrace_symbols(buffer, n_ptrs);
	if (!strings)
		return;

	printf("backtrace:\n");
	fflush(stdout);

	for (i = 0; i < n_ptrs; i++) {
		printf("  %s\n", strings[i]);
		fflush(stdout);
	}
	free(strings);
#endif
}
