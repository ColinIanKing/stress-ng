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
#include "core-asm-x86.h"
#include "core-builtin.h"
#include "core-cpu.h"
#include "core-madvise.h"
#include "core-mmap.h"
#include "core-nt-store.h"
#include "core-numa.h"
#include "core-target-clones.h"

#include <time.h>

#if defined(HAVE_LINUX_MEMPOLICY_H) &&  \
    defined(__NR_mbind)
#include <linux/mempolicy.h>
#define HAVE_MISALIGNED_NUMA	(1)
#endif

#define BITS_PER_BYTE           (8)
#define NUMA_LONG_BITS          (sizeof(unsigned long int) * BITS_PER_BYTE)


#define MISALIGN_LOOPS		(64)

/* Disable atomic ops for SH4 as this breaks gcc on Debian sid */
#if defined(STRESS_ARCH_SH4)
#undef HAVE_ATOMIC
#endif
/* Disable atomic ops for PPC64 with gcc < 5.0 as these can lock up in VM */
#if (defined(STRESS_ARCH_PPC64) || defined(STRESS_ARCH_PPC)) &&	\
    !NEED_GNUC(5, 0, 0)
#undef HAVE_ATOMIC
#endif

#if defined(HAVE_LIB_RT) &&		\
    defined(HAVE_TIMER_CREATE) &&	\
    defined(HAVE_TIMER_DELETE) &&	\
    defined(HAVE_TIMER_SETTIME)
#define HAVE_TIMER_FUNCTIONALITY
#endif

#define STRESS_MISALIGNED_ERROR		(1)
#define STRESS_MISALIGNED_TIMED_OUT	(2)
#define STRESS_MISALIGNED_WAIT_TIME_NS	(800000000)

#if defined(HAVE_ATOMIC)

#if defined(HAVE_ATOMIC_FETCH_ADD_2)
#define SHIM_ATOMIC_FETCH_ADD_2(ptr, val, order)	__atomic_fetch_add_2(ptr, val, order)
#elif defined(HAVE_ATOMIC_FETCH_ADD)
#define SHIM_ATOMIC_FETCH_ADD_2(ptr, val, order)	__atomic_fetch_add(ptr, val, order)
#endif

#if defined(HAVE_ATOMIC_FETCH_ADD_4)
#define SHIM_ATOMIC_FETCH_ADD_4(ptr, val, order)	__atomic_fetch_add_4(ptr, val, order)
#elif defined(HAVE_ATOMIC_FETCH_ADD)
#define SHIM_ATOMIC_FETCH_ADD_4(ptr, val, order)	__atomic_fetch_add(ptr, val, order)
#endif

#if defined(HAVE_ATOMIC_FETCH_ADD_8)
#define SHIM_ATOMIC_FETCH_ADD_8(ptr, val, order)	__atomic_fetch_add_8(ptr, val, order)
#elif defined(HAVE_ATOMIC_FETCH_ADD)
#define SHIM_ATOMIC_FETCH_ADD_8(ptr, val, order)	__atomic_fetch_add(ptr, val, order)
#endif

#endif

static const stress_help_t help[] = {
	{ NULL,	"misaligned N",	   	"start N workers performing misaligned read/writes" },
	{ NULL,	"misaligned-method M",	"use misaligned memory read/write method" },
	{ NULL,	"misaligned-ops N",	"stop after N misaligned bogo operations" },
	{ NULL,	NULL,			NULL }
};

#if defined(HAVE_SIGLONGJMP)

static sigjmp_buf jmp_env;
static int handled_signum = -1;
#if defined(HAVE_TIMER_FUNCTIONALITY)
static bool use_timer = false;
static timer_t timer_id;
static struct itimerspec timer;
#endif

typedef void (*stress_misaligned_func)(stress_args_t *args, uintptr_t buffer, const size_t page_size, bool *succeeded);

typedef struct {
	const char *name;
	const stress_misaligned_func func;
	bool disabled;
	bool exercised;
} stress_misaligned_method_info_t;

static stress_misaligned_method_info_t *current_method;

static inline ALWAYS_INLINE void stress_misligned_disable(void)
{
	if (current_method)
		current_method->disabled = true;
}

#if defined(__SSE__) &&         \
    defined(STRESS_ARCH_X86) && \
    defined(HAVE_TARGET_CLONES)
/*
 *  keep_running_no_sse()
 *	non-inlined to workaround inlining NOSSE code build
 *	issues
 */
static bool NOINLINE keep_running_no_sse(void)
{
	return stress_continue_flag();
}
#else
#define keep_running_no_sse()	stress_continue_flag()
#endif

static void stress_misaligned_int16rd(
	stress_args_t *args,
	uintptr_t buffer,
	const size_t page_size,
	bool *succeeded)
{
	register int i = MISALIGN_LOOPS;
	volatile uint16_t *ptr1  = (uint16_t *)(buffer + 1);
	volatile uint16_t *ptr2  = (uint16_t *)(buffer + 3);
	volatile uint16_t *ptr3  = (uint16_t *)(buffer + 5);
	volatile uint16_t *ptr4  = (uint16_t *)(buffer + 7);
	volatile uint16_t *ptr5  = (uint16_t *)(buffer + 9);
	volatile uint16_t *ptr6  = (uint16_t *)(buffer + 11);
	volatile uint16_t *ptr7  = (uint16_t *)(buffer + 13);
	volatile uint16_t *ptr8  = (uint16_t *)(buffer + 15);
	volatile uint16_t *ptr9  = (uint16_t *)(buffer + page_size - 1);
	volatile uint16_t *ptr10 = (uint16_t *)(buffer + page_size - 3);
	volatile uint16_t *ptr11 = (uint16_t *)(buffer + page_size - 5);
	volatile uint16_t *ptr12 = (uint16_t *)(buffer + page_size - 7);
	volatile uint16_t *ptr13 = (uint16_t *)(buffer + page_size - 9);
	volatile uint16_t *ptr14 = (uint16_t *)(buffer + page_size - 11);
	volatile uint16_t *ptr15 = (uint16_t *)(buffer + page_size - 13);
	volatile uint16_t *ptr16 = (uint16_t *)(buffer + page_size - 15);
	volatile uint16_t *ptr17  = (uint16_t *)(buffer + 63);

	(void)args;
	(void)succeeded;

	while (LIKELY(stress_continue_flag() && --i)) {
		(void)*ptr1;
		stress_asm_mb();
		(void)*ptr2;
		stress_asm_mb();
		(void)*ptr3;
		stress_asm_mb();
		(void)*ptr4;
		stress_asm_mb();
		(void)*ptr5;
		stress_asm_mb();
		(void)*ptr6;
		stress_asm_mb();
		(void)*ptr7;
		stress_asm_mb();
		(void)*ptr8;
		stress_asm_mb();

		(void)*ptr9;
		stress_asm_mb();
		(void)*ptr10;
		stress_asm_mb();
		(void)*ptr11;
		stress_asm_mb();
		(void)*ptr12;
		stress_asm_mb();
		(void)*ptr13;
		stress_asm_mb();
		(void)*ptr14;
		stress_asm_mb();
		(void)*ptr15;
		stress_asm_mb();
		(void)*ptr16;
		stress_asm_mb();

		(void)*ptr17;
		stress_asm_mb();
	}
}

static void stress_misaligned_int16wr(
	stress_args_t *args,
	uintptr_t buffer,
	const size_t page_size,
	bool *succeeded)
{
	register int i = MISALIGN_LOOPS;
	volatile uint16_t *ptr1  = (uint16_t *)(buffer + 1);
	volatile uint16_t *ptr2  = (uint16_t *)(buffer + 3);
	volatile uint16_t *ptr3  = (uint16_t *)(buffer + 5);
	volatile uint16_t *ptr4  = (uint16_t *)(buffer + 7);
	volatile uint16_t *ptr5  = (uint16_t *)(buffer + 9);
	volatile uint16_t *ptr6  = (uint16_t *)(buffer + 11);
	volatile uint16_t *ptr7  = (uint16_t *)(buffer + 13);
	volatile uint16_t *ptr8  = (uint16_t *)(buffer + 15);
	volatile uint16_t *ptr9  = (uint16_t *)(buffer + page_size - 1);
	volatile uint16_t *ptr10 = (uint16_t *)(buffer + page_size - 3);
	volatile uint16_t *ptr11 = (uint16_t *)(buffer + page_size - 5);
	volatile uint16_t *ptr12 = (uint16_t *)(buffer + page_size - 7);
	volatile uint16_t *ptr13 = (uint16_t *)(buffer + page_size - 9);
	volatile uint16_t *ptr14 = (uint16_t *)(buffer + page_size - 11);
	volatile uint16_t *ptr15 = (uint16_t *)(buffer + page_size - 13);
	volatile uint16_t *ptr16 = (uint16_t *)(buffer + page_size - 15);
	volatile uint16_t *ptr17  = (uint16_t *)(buffer + 63);

	while (LIKELY(stress_continue_flag() && --i)) {
		register uint16_t ui16 = (uint16_t)i;

		*ptr1  = ui16;
		stress_asm_mb();
		*ptr2  = ui16;
		stress_asm_mb();
		*ptr3  = ui16;
		stress_asm_mb();
		*ptr4  = ui16;
		stress_asm_mb();
		*ptr5  = ui16;
		stress_asm_mb();
		*ptr6  = ui16;
		stress_asm_mb();
		*ptr7  = ui16;
		stress_asm_mb();
		*ptr8  = ui16;
		stress_asm_mb();

		*ptr9  = ui16;
		stress_asm_mb();
		*ptr10 = ui16;
		stress_asm_mb();
		*ptr11 = ui16;
		stress_asm_mb();
		*ptr12 = ui16;
		stress_asm_mb();
		*ptr13 = ui16;
		stress_asm_mb();
		*ptr14 = ui16;
		stress_asm_mb();
		*ptr15 = ui16;
		stress_asm_mb();
		*ptr16 = ui16;
		stress_asm_mb();

		*ptr17 = ui16;
		stress_asm_mb();

		if (UNLIKELY(*ptr1 != ui16))
			goto fail;
		if (UNLIKELY(*ptr2 != ui16))
			goto fail;
		if (UNLIKELY(*ptr3 != ui16))
			goto fail;
		if (UNLIKELY(*ptr4 != ui16))
			goto fail;
		if (UNLIKELY(*ptr5 != ui16))
			goto fail;
		if (UNLIKELY(*ptr6 != ui16))
			goto fail;
		if (UNLIKELY(*ptr7 != ui16))
			goto fail;
		if (UNLIKELY(*ptr8 != ui16))
			goto fail;
		if (UNLIKELY(*ptr9 != ui16))
			goto fail;
		if (UNLIKELY(*ptr10 != ui16))
			goto fail;
		if (UNLIKELY(*ptr11 != ui16))
			goto fail;
		if (UNLIKELY(*ptr12 != ui16))
			goto fail;
		if (UNLIKELY(*ptr13 != ui16))
			goto fail;
		if (UNLIKELY(*ptr14 != ui16))
			goto fail;
		if (UNLIKELY(*ptr15 != ui16))
			goto fail;
		if (UNLIKELY(*ptr16 != ui16))
			goto fail;
		if (UNLIKELY(*ptr17 != ui16))
			goto fail;
	}
	return;

fail:
	pr_inf("%s: int16wr: difference between 16 bit value written and value read back\n", args->name);
	*succeeded = false;
}

static void stress_misaligned_int16inc(
	stress_args_t *args,
	uintptr_t buffer,
	const size_t page_size,
	bool *succeeded)
{
	register int i = MISALIGN_LOOPS;
	volatile uint16_t *ptr1  = (uint16_t *)(buffer + 1);
	volatile uint16_t *ptr2  = (uint16_t *)(buffer + 3);
	volatile uint16_t *ptr3  = (uint16_t *)(buffer + 5);
	volatile uint16_t *ptr4  = (uint16_t *)(buffer + 7);
	volatile uint16_t *ptr5  = (uint16_t *)(buffer + 9);
	volatile uint16_t *ptr6  = (uint16_t *)(buffer + 11);
	volatile uint16_t *ptr7  = (uint16_t *)(buffer + 13);
	volatile uint16_t *ptr8  = (uint16_t *)(buffer + 15);
	volatile uint16_t *ptr9  = (uint16_t *)(buffer + page_size - 1);
	volatile uint16_t *ptr10 = (uint16_t *)(buffer + page_size - 3);
	volatile uint16_t *ptr11 = (uint16_t *)(buffer + page_size - 5);
	volatile uint16_t *ptr12 = (uint16_t *)(buffer + page_size - 7);
	volatile uint16_t *ptr13 = (uint16_t *)(buffer + page_size - 9);
	volatile uint16_t *ptr14 = (uint16_t *)(buffer + page_size - 11);
	volatile uint16_t *ptr15 = (uint16_t *)(buffer + page_size - 13);
	volatile uint16_t *ptr16 = (uint16_t *)(buffer + page_size - 15);
	volatile uint16_t *ptr17  = (uint16_t *)(buffer + 63);

	(void)args;
	(void)succeeded;

	while (LIKELY(stress_continue_flag() && --i)) {
		(*ptr1)++;
		stress_asm_mb();
		(*ptr2)++;
		stress_asm_mb();
		(*ptr3)++;
		stress_asm_mb();
		(*ptr4)++;
		stress_asm_mb();
		(*ptr5)++;
		stress_asm_mb();
		(*ptr6)++;
		stress_asm_mb();
		(*ptr7)++;
		stress_asm_mb();
		(*ptr8)++;
		stress_asm_mb();

		(*ptr9)++;
		stress_asm_mb();
		(*ptr10)++;
		stress_asm_mb();
		(*ptr11)++;
		stress_asm_mb();
		(*ptr12)++;
		stress_asm_mb();
		(*ptr13)++;
		stress_asm_mb();
		(*ptr14)++;
		stress_asm_mb();
		(*ptr15)++;
		stress_asm_mb();
		(*ptr16)++;
		stress_asm_mb();

		(*ptr17)++;
		stress_asm_mb();
	}
}

#if defined(HAVE_ATOMIC) &&		\
    defined(SHIM_ATOMIC_FETCH_ADD_2) &&	\
    defined(__ATOMIC_SEQ_CST)
static void stress_misaligned_int16atomic(
	stress_args_t *args,
	uintptr_t buffer,
	const size_t page_size,
	bool *succeeded)
{
	register int i = MISALIGN_LOOPS;
	volatile uint16_t *ptr1  = (uint16_t *)(buffer + 1);
	volatile uint16_t *ptr2  = (uint16_t *)(buffer + 3);
	volatile uint16_t *ptr3  = (uint16_t *)(buffer + 5);
	volatile uint16_t *ptr4  = (uint16_t *)(buffer + 7);
	volatile uint16_t *ptr5  = (uint16_t *)(buffer + 9);
	volatile uint16_t *ptr6  = (uint16_t *)(buffer + 11);
	volatile uint16_t *ptr7  = (uint16_t *)(buffer + 13);
	volatile uint16_t *ptr8  = (uint16_t *)(buffer + 15);
	volatile uint16_t *ptr9 = (uint16_t *)(buffer + page_size - 1);
	volatile uint16_t *ptr10 = (uint16_t *)(buffer + page_size - 3);
	volatile uint16_t *ptr11 = (uint16_t *)(buffer + page_size - 5);
	volatile uint16_t *ptr12 = (uint16_t *)(buffer + page_size - 7);
	volatile uint16_t *ptr13 = (uint16_t *)(buffer + page_size - 9);
	volatile uint16_t *ptr14 = (uint16_t *)(buffer + page_size - 11);
	volatile uint16_t *ptr15 = (uint16_t *)(buffer + page_size - 13);
	volatile uint16_t *ptr16 = (uint16_t *)(buffer + page_size - 15);
	volatile uint16_t *ptr17  = (uint16_t *)(buffer + 63);

	(void)args;
	(void)succeeded;

	while (LIKELY(stress_continue_flag() && --i)) {
		SHIM_ATOMIC_FETCH_ADD_2(ptr1, 1, __ATOMIC_SEQ_CST);
		stress_asm_mb();
		SHIM_ATOMIC_FETCH_ADD_2(ptr2, 1, __ATOMIC_SEQ_CST);
		stress_asm_mb();
		SHIM_ATOMIC_FETCH_ADD_2(ptr3, 1, __ATOMIC_SEQ_CST);
		stress_asm_mb();
		SHIM_ATOMIC_FETCH_ADD_2(ptr4, 1, __ATOMIC_SEQ_CST);
		stress_asm_mb();
		SHIM_ATOMIC_FETCH_ADD_2(ptr5, 1, __ATOMIC_SEQ_CST);
		stress_asm_mb();
		SHIM_ATOMIC_FETCH_ADD_2(ptr6, 1, __ATOMIC_SEQ_CST);
		stress_asm_mb();
		SHIM_ATOMIC_FETCH_ADD_2(ptr7, 1, __ATOMIC_SEQ_CST);
		stress_asm_mb();
		SHIM_ATOMIC_FETCH_ADD_2(ptr8, 1, __ATOMIC_SEQ_CST);
		stress_asm_mb();

		SHIM_ATOMIC_FETCH_ADD_2(ptr9, 1, __ATOMIC_SEQ_CST);
		stress_asm_mb();
		SHIM_ATOMIC_FETCH_ADD_2(ptr10, 1, __ATOMIC_SEQ_CST);
		stress_asm_mb();
		SHIM_ATOMIC_FETCH_ADD_2(ptr11, 1, __ATOMIC_SEQ_CST);
		stress_asm_mb();
		SHIM_ATOMIC_FETCH_ADD_2(ptr12, 1, __ATOMIC_SEQ_CST);
		stress_asm_mb();
		SHIM_ATOMIC_FETCH_ADD_2(ptr13, 1, __ATOMIC_SEQ_CST);
		stress_asm_mb();
		SHIM_ATOMIC_FETCH_ADD_2(ptr14, 1, __ATOMIC_SEQ_CST);
		stress_asm_mb();
		SHIM_ATOMIC_FETCH_ADD_2(ptr15, 1, __ATOMIC_SEQ_CST);
		stress_asm_mb();
		SHIM_ATOMIC_FETCH_ADD_2(ptr16, 1, __ATOMIC_SEQ_CST);
		stress_asm_mb();

		SHIM_ATOMIC_FETCH_ADD_2(ptr17, 1, __ATOMIC_SEQ_CST);
		stress_asm_mb();
	}
}
#endif

static void stress_misaligned_int32rd(
	stress_args_t *args,
	uintptr_t buffer,
	const size_t page_size,
	bool *succeeded)
{
	register int i = MISALIGN_LOOPS;
	volatile uint32_t *ptr1 = (uint32_t *)(buffer + 1);
	volatile uint32_t *ptr2 = (uint32_t *)(buffer + 5);
	volatile uint32_t *ptr3 = (uint32_t *)(buffer + 9);
	volatile uint32_t *ptr4 = (uint32_t *)(buffer + 13);
	volatile uint32_t *ptr5 = (uint32_t *)(buffer + page_size - 1);
	volatile uint32_t *ptr6 = (uint32_t *)(buffer + page_size - 5);
	volatile uint32_t *ptr7 = (uint32_t *)(buffer + page_size - 9);
	volatile uint32_t *ptr8 = (uint32_t *)(buffer + page_size - 13);
	volatile uint32_t *ptr9 = (uint32_t *)(buffer + 63);

	(void)args;
	(void)succeeded;

	while (LIKELY(stress_continue_flag() && --i)) {
		(void)*ptr1;
		stress_asm_mb();
		(void)*ptr2;
		stress_asm_mb();
		(void)*ptr3;
		stress_asm_mb();
		(void)*ptr4;
		stress_asm_mb();

		(void)*ptr5;
		stress_asm_mb();
		(void)*ptr6;
		stress_asm_mb();
		(void)*ptr7;
		stress_asm_mb();
		(void)*ptr8;
		stress_asm_mb();

		(void)*ptr9;
		stress_asm_mb();
	}
}

static void stress_misaligned_int32wr(
	stress_args_t *args,
	uintptr_t buffer,
	const size_t page_size,
	bool *succeeded)
{
	register int i = MISALIGN_LOOPS;
	volatile uint32_t *ptr1 = (uint32_t *)(buffer + 1);
	volatile uint32_t *ptr2 = (uint32_t *)(buffer + 5);
	volatile uint32_t *ptr3 = (uint32_t *)(buffer + 9);
	volatile uint32_t *ptr4 = (uint32_t *)(buffer + 13);
	volatile uint32_t *ptr5 = (uint32_t *)(buffer + page_size - 1);
	volatile uint32_t *ptr6 = (uint32_t *)(buffer + page_size - 5);
	volatile uint32_t *ptr7 = (uint32_t *)(buffer + page_size - 9);
	volatile uint32_t *ptr8 = (uint32_t *)(buffer + page_size - 13);
	volatile uint32_t *ptr9 = (uint32_t *)(buffer + 63);

	while (LIKELY(stress_continue_flag() && --i)) {
		register uint32_t ui32 = (uint32_t)i;

		*ptr1 = ui32;
		stress_asm_mb();
		*ptr2 = ui32;
		stress_asm_mb();
		*ptr3 = ui32;
		stress_asm_mb();
		*ptr4 = ui32;
		stress_asm_mb();

		*ptr5 = ui32;
		stress_asm_mb();
		*ptr6 = ui32;
		stress_asm_mb();
		*ptr7 = ui32;
		stress_asm_mb();
		*ptr8 = ui32;
		stress_asm_mb();

		*ptr9 = ui32;
		stress_asm_mb();

		if (UNLIKELY(*ptr1 != ui32))
			goto fail;
		if (UNLIKELY(*ptr2 != ui32))
			goto fail;
		if (UNLIKELY(*ptr3 != ui32))
			goto fail;
		if (UNLIKELY(*ptr4 != ui32))
			goto fail;
		if (UNLIKELY(*ptr5 != ui32))
			goto fail;
		if (UNLIKELY(*ptr6 != ui32))
			goto fail;
		if (UNLIKELY(*ptr7 != ui32))
			goto fail;
		if (UNLIKELY(*ptr8 != ui32))
			goto fail;
		if (UNLIKELY(*ptr9 != ui32))
			goto fail;
	}
	return;

fail:
	pr_inf("%s: int32wr: difference between 32 bit value written and value read back\n", args->name);
	*succeeded = false;
}

#if defined(HAVE_NT_STORE32)
static void stress_misaligned_int32wrnt(
	stress_args_t *args,
	uintptr_t buffer,
	const size_t page_size,
	bool *succeeded)
{
	register int i = MISALIGN_LOOPS;
	uint32_t *ptr1 = (uint32_t *)(buffer + 1);
	uint32_t *ptr2 = (uint32_t *)(buffer + 5);
	uint32_t *ptr3 = (uint32_t *)(buffer + 9);
	uint32_t *ptr4 = (uint32_t *)(buffer + 13);
	uint32_t *ptr5 = (uint32_t *)(buffer + page_size - 1);
	uint32_t *ptr6 = (uint32_t *)(buffer + page_size - 5);
	uint32_t *ptr7 = (uint32_t *)(buffer + page_size - 9);
	uint32_t *ptr8 = (uint32_t *)(buffer + page_size - 13);
	uint32_t *ptr9 = (uint32_t *)(buffer + 63);

	if (!stress_cpu_x86_has_sse2()) {
		if (stress_instance_zero(args))
			pr_inf("%s: int32wrnt disabled, 32 bit non-temporal store not available\n", args->name);
		stress_misligned_disable();
		return;
	}

	while (LIKELY(stress_continue_flag() && --i)) {
		register uint32_t ui32 = (uint32_t)i;

		stress_nt_store32(ptr1, ui32);
		stress_nt_store32(ptr2, ui32);
		stress_nt_store32(ptr3, ui32);
		stress_nt_store32(ptr4, ui32);
		stress_nt_store32(ptr5, ui32);
		stress_nt_store32(ptr6, ui32);
		stress_nt_store32(ptr7, ui32);
		stress_nt_store32(ptr8, ui32);
		stress_nt_store32(ptr9, ui32);

		if (UNLIKELY(*ptr1 != ui32))
			goto fail;
		if (UNLIKELY(*ptr2 != ui32))
			goto fail;
		if (UNLIKELY(*ptr3 != ui32))
			goto fail;
		if (UNLIKELY(*ptr4 != ui32))
			goto fail;
		if (UNLIKELY(*ptr5 != ui32))
			goto fail;
		if (UNLIKELY(*ptr6 != ui32))
			goto fail;
		if (UNLIKELY(*ptr7 != ui32))
			goto fail;
		if (UNLIKELY(*ptr8 != ui32))
			goto fail;
		if (UNLIKELY(*ptr9 != ui32))
			goto fail;
	}
	return;

fail:
	pr_inf("%s: int32wrnt: difference between 32 bit value written and value read back\n", args->name);
	*succeeded = false;
}

#endif

static void stress_misaligned_int32inc(
	stress_args_t *args,
	uintptr_t buffer,
	const size_t page_size,
	bool *succeeded)
{
	register int i = MISALIGN_LOOPS;
	volatile uint32_t *ptr1 = (uint32_t *)(buffer + 1);
	volatile uint32_t *ptr2 = (uint32_t *)(buffer + 5);
	volatile uint32_t *ptr3 = (uint32_t *)(buffer + 9);
	volatile uint32_t *ptr4 = (uint32_t *)(buffer + 13);
	volatile uint32_t *ptr5 = (uint32_t *)(buffer + page_size - 1);
	volatile uint32_t *ptr6 = (uint32_t *)(buffer + page_size - 5);
	volatile uint32_t *ptr7 = (uint32_t *)(buffer + page_size - 9);
	volatile uint32_t *ptr8 = (uint32_t *)(buffer + page_size - 13);
	volatile uint32_t *ptr9 = (uint32_t *)(buffer + 63);

	(void)args;
	(void)succeeded;

	while (LIKELY(stress_continue_flag() && --i)) {
		(*ptr1)++;
		stress_asm_mb();
		(*ptr2)++;
		stress_asm_mb();
		(*ptr3)++;
		stress_asm_mb();
		(*ptr4)++;
		stress_asm_mb();

		(*ptr5)++;
		stress_asm_mb();
		(*ptr6)++;
		stress_asm_mb();
		(*ptr7)++;
		stress_asm_mb();
		(*ptr8)++;
		stress_asm_mb();

		(*ptr9)++;
		stress_asm_mb();
	}
}

#if defined(HAVE_ATOMIC) &&		\
    defined(SHIM_ATOMIC_FETCH_ADD_4) &&	\
    defined(__ATOMIC_SEQ_CST)
static void stress_misaligned_int32atomic(
	stress_args_t *args,
	uintptr_t buffer,
	const size_t page_size,
	bool *succeeded)
{
	register int i = MISALIGN_LOOPS;
	volatile uint32_t *ptr1 = (uint32_t *)(buffer + 1);
	volatile uint32_t *ptr2 = (uint32_t *)(buffer + 5);
	volatile uint32_t *ptr3 = (uint32_t *)(buffer + 9);
	volatile uint32_t *ptr4 = (uint32_t *)(buffer + 13);
	volatile uint32_t *ptr5 = (uint32_t *)(buffer + page_size - 1);
	volatile uint32_t *ptr6 = (uint32_t *)(buffer + page_size - 5);
	volatile uint32_t *ptr7 = (uint32_t *)(buffer + page_size - 9);
	volatile uint32_t *ptr8 = (uint32_t *)(buffer + page_size - 13);
	volatile uint32_t *ptr9 = (uint32_t *)(buffer + 63);

	(void)args;
	(void)succeeded;

	while (LIKELY(stress_continue_flag() && --i)) {
		SHIM_ATOMIC_FETCH_ADD_4(ptr1, 1, __ATOMIC_SEQ_CST);
		stress_asm_mb();
		SHIM_ATOMIC_FETCH_ADD_4(ptr2, 1, __ATOMIC_SEQ_CST);
		stress_asm_mb();
		SHIM_ATOMIC_FETCH_ADD_4(ptr3, 1, __ATOMIC_SEQ_CST);
		stress_asm_mb();
		SHIM_ATOMIC_FETCH_ADD_4(ptr4, 1, __ATOMIC_SEQ_CST);
		stress_asm_mb();

		SHIM_ATOMIC_FETCH_ADD_4(ptr5, 1, __ATOMIC_SEQ_CST);
		stress_asm_mb();
		SHIM_ATOMIC_FETCH_ADD_4(ptr6, 1, __ATOMIC_SEQ_CST);
		stress_asm_mb();
		SHIM_ATOMIC_FETCH_ADD_4(ptr7, 1, __ATOMIC_SEQ_CST);
		stress_asm_mb();
		SHIM_ATOMIC_FETCH_ADD_4(ptr8, 1, __ATOMIC_SEQ_CST);
		stress_asm_mb();

		SHIM_ATOMIC_FETCH_ADD_4(ptr9, 1, __ATOMIC_SEQ_CST);
		stress_asm_mb();
	}
}
#endif

static void stress_misaligned_int64rd(
	stress_args_t *args,
	uintptr_t buffer,
	const size_t page_size,
	bool *succeeded)
{
	register int i = MISALIGN_LOOPS;
	volatile uint64_t *ptr1 = (uint64_t *)(buffer + 1);
	volatile uint64_t *ptr2 = (uint64_t *)(buffer + 9);
	volatile uint64_t *ptr3 = (uint64_t *)(buffer + page_size - 1);
	volatile uint64_t *ptr4 = (uint64_t *)(buffer + page_size - 9);
	volatile uint64_t *ptr5 = (uint64_t *)(buffer + 63);

	(void)args;
	(void)succeeded;

	while (LIKELY(stress_continue_flag() && --i)) {
		(void)*ptr1;
		stress_asm_mb();
		(void)*ptr2;
		stress_asm_mb();

		(void)*ptr3;
		stress_asm_mb();
		(void)*ptr4;
		stress_asm_mb();

		(void)*ptr5;
		stress_asm_mb();
	}
}

static void stress_misaligned_int64wr(
	stress_args_t *args,
	uintptr_t buffer,
	const size_t page_size,
	bool *succeeded)
{
	register int i = MISALIGN_LOOPS;
	volatile uint64_t *ptr1 = (uint64_t *)(buffer + 1);
	volatile uint64_t *ptr2 = (uint64_t *)(buffer + 9);
	volatile uint64_t *ptr3 = (uint64_t *)(buffer + page_size - 1);
	volatile uint64_t *ptr4 = (uint64_t *)(buffer + page_size - 9);
	volatile uint64_t *ptr5 = (uint64_t *)(buffer + 63);

	while (LIKELY(stress_continue_flag() && --i)) {
		register uint64_t ui64 = (uint64_t)i;

		*ptr1 = ui64;
		stress_asm_mb();
		*ptr2 = ui64;
		stress_asm_mb();

		*ptr3 = ui64;
		stress_asm_mb();
		*ptr4 = ui64;
		stress_asm_mb();

		*ptr5 = ui64;
		stress_asm_mb();

		if (UNLIKELY(*ptr1 != ui64))
			goto fail;
		if (UNLIKELY(*ptr2 != ui64))
			goto fail;
		if (UNLIKELY(*ptr3 != ui64))
			goto fail;
		if (UNLIKELY(*ptr4 != ui64))
			goto fail;
		if (UNLIKELY(*ptr5 != ui64))
			goto fail;
	}
	return;

fail:
	pr_inf("%s: int64wr: difference between 64 bit value written and value read back\n", args->name);
	*succeeded = false;
}

#if defined(HAVE_NT_STORE64)
static void stress_misaligned_int64wrnt(
	stress_args_t *args,
	uintptr_t buffer,
	const size_t page_size,
	bool *succeeded)
{
	register int i = MISALIGN_LOOPS;
	uint64_t *ptr1 = (uint64_t *)(buffer + 1);
	uint64_t *ptr2 = (uint64_t *)(buffer + 9);
	uint64_t *ptr3 = (uint64_t *)(buffer + page_size - 1);
	uint64_t *ptr4 = (uint64_t *)(buffer + page_size - 9);
	uint64_t *ptr5 = (uint64_t *)(buffer + 63);

	if (!stress_cpu_x86_has_sse2()) {
		if (stress_instance_zero(args))
			pr_inf("%s: int64wrnt disabled, 64 bit non-temporal store not available\n", args->name);
		stress_misligned_disable();
		return;
	}

	while (LIKELY(stress_continue_flag() && --i)) {
		register uint64_t ui64 = (uint64_t)i;

		stress_nt_store64(ptr1, ui64);
		stress_nt_store64(ptr2, ui64);
		stress_nt_store64(ptr3, ui64);
		stress_nt_store64(ptr4, ui64);
		stress_nt_store64(ptr5, ui64);

		if (UNLIKELY(*ptr1 != ui64))
			goto fail;
		if (UNLIKELY(*ptr2 != ui64))
			goto fail;
		if (UNLIKELY(*ptr3 != ui64))
			goto fail;
		if (UNLIKELY(*ptr4 != ui64))
			goto fail;
		if (UNLIKELY(*ptr5 != ui64))
			goto fail;
	}
	return;

fail:
	pr_inf("%s: int64wrt: difference between 64 bit value written and value read back\n", args->name);
	*succeeded = false;
}
#endif

#if defined(HAVE_ASM_X86_MOVDIRI) &&	\
    defined(STRESS_ARCH_X86_64)
static void stress_misaligned_int64wrds(
	stress_args_t *args,
	uintptr_t buffer,
	const size_t page_size,
	bool *succeeded)
{
	register int i = MISALIGN_LOOPS;
	uint64_t *ptr1 = (uint64_t *)(buffer + 1);
	uint64_t *ptr2 = (uint64_t *)(buffer + 9);
	uint64_t *ptr3 = (uint64_t *)(buffer + page_size - 1);
	uint64_t *ptr4 = (uint64_t *)(buffer + page_size - 9);
	uint64_t *ptr5 = (uint64_t *)(buffer + 63);

	if (!stress_cpu_x86_has_movdiri()) {
		if (stress_instance_zero(args))
			pr_inf("%s: int64wrds disabled, 64 bit direct store not available\n", args->name);
		stress_misligned_disable();
		return;
	}

	while (LIKELY(stress_continue_flag() && --i)) {
		register uint64_t ui64 = (uint64_t)i;

		stress_ds_store64(ptr1, ui64);
		stress_ds_store64(ptr2, ui64);
		stress_ds_store64(ptr3, ui64);
		stress_ds_store64(ptr4, ui64);
		stress_ds_store64(ptr5, ui64);

		if (UNLIKELY(*ptr1 != ui64))
			goto fail;
		if (UNLIKELY(*ptr2 != ui64))
			goto fail;
		if (UNLIKELY(*ptr3 != ui64))
			goto fail;
		if (UNLIKELY(*ptr4 != ui64))
			goto fail;
		if (UNLIKELY(*ptr5 != ui64))
			goto fail;
	}
	return;

fail:
	pr_inf("%s: int64wrt: difference between 64 bit value written and value read back\n", args->name);
	*succeeded = false;
}
#endif

static void stress_misaligned_int64inc(
	stress_args_t *args,
	uintptr_t buffer,
	const size_t page_size,
	bool *succeeded)
{
	register int i = MISALIGN_LOOPS;
	volatile uint64_t *ptr1 = (uint64_t *)(buffer + 1);
	volatile uint64_t *ptr2 = (uint64_t *)(buffer + 9);
	volatile uint64_t *ptr3 = (uint64_t *)(buffer + page_size - 1);
	volatile uint64_t *ptr4 = (uint64_t *)(buffer + page_size - 9);
	volatile uint64_t *ptr5 = (uint64_t *)(buffer + 63);

	(void)args;
	(void)succeeded;

	while (LIKELY(stress_continue_flag() && --i)) {
		(*ptr1)++;
		stress_asm_mb();
		(*ptr2)++;
		stress_asm_mb();

		(*ptr3)++;
		stress_asm_mb();
		(*ptr4)++;
		stress_asm_mb();

		(*ptr5)++;
		stress_asm_mb();
	}
}

#if defined(HAVE_ATOMIC) &&		\
    defined(SHIM_ATOMIC_FETCH_ADD_8) &&	\
    defined(__ATOMIC_SEQ_CST)
static void stress_misaligned_int64atomic(
	stress_args_t *args,
	uintptr_t buffer,
	const size_t page_size,
	bool *succeeded)
{
	register int i = MISALIGN_LOOPS;
	volatile uint64_t *ptr1 = (uint64_t *)(buffer + 1);
	volatile uint64_t *ptr2 = (uint64_t *)(buffer + 9);
	volatile uint64_t *ptr3 = (uint64_t *)(buffer + page_size - 1);
	volatile uint64_t *ptr4 = (uint64_t *)(buffer + page_size - 9);
	volatile uint64_t *ptr5 = (uint64_t *)(buffer + 63);

	(void)args;
	(void)succeeded;

	while (LIKELY(stress_continue_flag() && --i)) {
		SHIM_ATOMIC_FETCH_ADD_8(ptr1, 1, __ATOMIC_SEQ_CST);
		stress_asm_mb();
		SHIM_ATOMIC_FETCH_ADD_8(ptr2, 1, __ATOMIC_SEQ_CST);
		stress_asm_mb();

		SHIM_ATOMIC_FETCH_ADD_8(ptr3, 1, __ATOMIC_SEQ_CST);
		stress_asm_mb();
		SHIM_ATOMIC_FETCH_ADD_8(ptr4, 1, __ATOMIC_SEQ_CST);
		stress_asm_mb();

		SHIM_ATOMIC_FETCH_ADD_8(ptr5, 1, __ATOMIC_SEQ_CST);
		stress_asm_mb();
	}
}
#endif

#if defined(HAVE_INT128_T)

/*
 *  Misaligned 128 bit fetches with SSE on x86 with some compilers
 *  with misaligned data may generate moveda rather than movdqu.
 *  For now, disable SSE optimization for x86 to workaround this
 *  even if it ends up generating two 64 bit reads.
 */
#if defined(__SSE__) && 	\
    defined(STRESS_ARCH_X86) &&	\
    defined(HAVE_TARGET_CLONES)
#define TARGET_CLONE_NO_SSE __attribute__ ((target("no-sse")))
#else
#define TARGET_CLONE_NO_SSE
#endif
static void TARGET_CLONE_NO_SSE stress_misaligned_int128rd(
	stress_args_t *args,
	uintptr_t buffer,
	const size_t page_size,
	bool *succeeded)
{
	register int i = MISALIGN_LOOPS;
	volatile __uint128_t *ptr1 = (__uint128_t *)(buffer + 1);
	volatile __uint128_t *ptr2 = (__uint128_t *)(buffer + page_size - 1);
	volatile __uint128_t *ptr3 = (__uint128_t *)(buffer + 63);

	(void)args;
	(void)succeeded;

	while (keep_running_no_sse() && --i) {
		(void)*ptr1;
		(void)*ptr2;
		(void)*ptr3;
	}
}

static void stress_misaligned_int128wr(
	stress_args_t *args,
	uintptr_t buffer,
	const size_t page_size,
	bool *succeeded)
{
	register int i = MISALIGN_LOOPS;
	volatile __uint128_t *ptr1 = (__uint128_t *)(buffer + 1);
	volatile __uint128_t *ptr2 = (__uint128_t *)(buffer + page_size - 1);
	volatile __uint128_t *ptr3 = (__uint128_t *)(buffer + 63);

	while (LIKELY(stress_continue_flag() && --i)) {
		__uint128_t ui128 = (__uint128_t)i;

		*ptr1 = ui128;
		stress_asm_mb();

		*ptr2 = ui128;
		stress_asm_mb();

		*ptr3 = ui128;
		stress_asm_mb();

		if (UNLIKELY(*ptr1 != ui128))
			goto fail;
		if (UNLIKELY(*ptr2 != ui128))
			goto fail;
		if (UNLIKELY(*ptr3 != ui128))
			goto fail;
	}
	return;

fail:
	pr_inf("%s: int128wr: difference between 128 bit value written and value read back\n", args->name);
	*succeeded = false;
}

#if defined(HAVE_NT_STORE128) && 0
static void stress_misaligned_int128wrnt(
	stress_args_t *args,
	uintptr_t buffer,
	const size_t page_size,
	bool *succeeded)
{
	register int i = MISALIGN_LOOPS;
	__uint128_t *ptr1 = (__uint128_t *)(buffer + 1);
	__uint128_t *ptr2 = (__uint128_t *)(buffer + page_size - 1);
	__uint128_t *ptr3 = (__uint128_t *)(buffer + 63);

	if (!stress_cpu_x86_has_sse2()) {
		if (stress_instance_zero(args))
			pr_inf("%s: int128wrnt disabled, 128 bit non-temporal store not available\n", args->name);
		stress_misligned_disable();
		return;
	}

	while (LIKELY(stress_continue_flag() && --i)) {
		stress_nt_store128(ptr1, (__uint128_t)i);
		stress_nt_store128(ptr2, (__uint128_t)i);
		stress_nt_store128(ptr3, (__uint128_t)i);
	}
}
#endif

static void TARGET_CLONE_NO_SSE stress_misaligned_int128inc(
	stress_args_t *args,
	uintptr_t buffer,
	const size_t page_size,
	bool *succeeded)
{
	register int i = MISALIGN_LOOPS;
	volatile __uint128_t *ptr1 = (__uint128_t *)(buffer + 1);
	volatile __uint128_t *ptr2 = (__uint128_t *)(buffer + page_size - 1);
	volatile __uint128_t *ptr3 = (__uint128_t *)(buffer + 63);

	(void)args;
	(void)succeeded;

	while (keep_running_no_sse() && --i) {
		(*ptr1)++;
		(*ptr2)++;
		(*ptr3)++;
	}
}

#if defined(HAVE_ATOMIC) &&		\
    defined(SHIM_ATOMIC_FETCH_ADD_8) &&	\
    defined(__ATOMIC_SEQ_CST)
static void stress_misaligned_int128atomic(
	stress_args_t *args,
	uintptr_t buffer,
	const size_t page_size,
	bool *succeeded)
{
	register int i = MISALIGN_LOOPS;
	volatile __uint128_t *ptr1 = (__uint128_t *)(buffer + 1);
	volatile __uint128_t *ptr2 = (__uint128_t *)(buffer + page_size - 1);
	volatile __uint128_t *ptr3 = (__uint128_t *)(buffer + 63);

	(void)args;
	(void)succeeded;

	while (LIKELY(stress_continue_flag() && --i)) {
		/* No add 16 variant, so do 2 x 8 adds for now */
		volatile uint64_t *ptr1u64 = (volatile uint64_t *)ptr1;
		volatile uint64_t *ptr2u64 = (volatile uint64_t *)ptr2;
		volatile uint64_t *ptr3u64 = (volatile uint64_t *)ptr3;

		SHIM_ATOMIC_FETCH_ADD_8(ptr1u64, 1, __ATOMIC_SEQ_CST);
		SHIM_ATOMIC_FETCH_ADD_8(ptr1u64 + 1, 1, __ATOMIC_SEQ_CST);

		SHIM_ATOMIC_FETCH_ADD_8(ptr2u64, 1, __ATOMIC_SEQ_CST);
		SHIM_ATOMIC_FETCH_ADD_8(ptr2u64 + 1, 1, __ATOMIC_SEQ_CST);

		SHIM_ATOMIC_FETCH_ADD_8(ptr3u64, 1, __ATOMIC_SEQ_CST);
		SHIM_ATOMIC_FETCH_ADD_8(ptr3u64 + 1, 1, __ATOMIC_SEQ_CST);
	 }
}
#endif
#endif

static void stress_misaligned_all(
	stress_args_t *args, uintptr_t buffer, const size_t page_size, bool *succeeded);

static stress_misaligned_method_info_t stress_misaligned_methods[] = {
	{ "all",	stress_misaligned_all,		false,	false },
	{ "int16rd",	stress_misaligned_int16rd,	false,	false },
	{ "int16wr",	stress_misaligned_int16wr,	false,	false },
	{ "int16inc",	stress_misaligned_int16inc,	false,	false },
#if defined(HAVE_ATOMIC) &&		\
    defined(SHIM_ATOMIC_FETCH_ADD_2) &&	\
    defined(__ATOMIC_SEQ_CST)
	{ "int16atomic",stress_misaligned_int16atomic,	false,	false },
#endif
	{ "int32rd",	stress_misaligned_int32rd,	false,	false },
	{ "int32wr",	stress_misaligned_int32wr,	false,	false },
#if defined(HAVE_NT_STORE64)
	{ "int32wrnt",	stress_misaligned_int32wrnt,	false,	false },
#endif
	{ "int32inc",	stress_misaligned_int32inc,	false,	false },
#if defined(HAVE_ATOMIC) &&		\
    defined(SHIM_ATOMIC_FETCH_ADD_4) &&	\
    defined(__ATOMIC_SEQ_CST)
	{ "int32atomic",stress_misaligned_int32atomic,	false,	false },
#endif
	{ "int64rd",	stress_misaligned_int64rd,	false,	false },
	{ "int64wr",	stress_misaligned_int64wr,	false,	false },
#if defined(HAVE_NT_STORE64)
	{ "int64wrnt",	stress_misaligned_int64wrnt,	false,	false },
#endif
#if defined(HAVE_ASM_X86_MOVDIRI) &&	\
    defined(STRESS_ARCH_X86_64)
	{ "int64wrds",	stress_misaligned_int64wrds,	false,  false },
#endif
	{ "int64inc",	stress_misaligned_int64inc,	false,	false },
#if defined(HAVE_ATOMIC) &&		\
    defined(SHIM_ATOMIC_FETCH_ADD_8) &&	\
    defined(__ATOMIC_SEQ_CST)
	{ "int64atomic",stress_misaligned_int64atomic,	false,	false },
#endif
#if defined(HAVE_INT128_T)
	{ "int128rd",	stress_misaligned_int128rd,	false,	false },
	{ "int128wr",	stress_misaligned_int128wr,	false,	false },
#if defined(HAVE_NT_STORE128) && 0
	{ "int128wrnt",	stress_misaligned_int128wrnt,	false,	false },
#endif
	{ "int128inc",	stress_misaligned_int128inc,	false,	false },
#if defined(HAVE_ATOMIC) &&		\
    defined(SHIM_ATOMIC_FETCH_ADD_8) &&	\
    defined(__ATOMIC_SEQ_CST)
	{ "int128atomic",stress_misaligned_int128atomic,false,	false },
#endif
#endif
};

static void stress_misaligned_all(
	stress_args_t *args,
	uintptr_t buffer,
	const size_t page_size,
	bool *succeeded)
{
	static bool exercised = false;
	size_t i;

	for (i = 1; LIKELY(i < SIZEOF_ARRAY(stress_misaligned_methods) && stress_continue_flag()); i++) {
		stress_misaligned_method_info_t *info = &stress_misaligned_methods[i];

		if (UNLIKELY(info->disabled))
			continue;
		current_method = info;
		info->func(args, buffer, page_size, succeeded);
		if (!info->disabled) {
			info->exercised = true;
			exercised = true;
		}
	}

	if (UNLIKELY(!exercised))
		stress_misaligned_methods[0].disabled = true;
}

static MLOCKED_TEXT NORETURN void stress_misaligned_handler(int signum)
{
	handled_signum = signum;
	stress_misligned_disable();
	siglongjmp(jmp_env, STRESS_MISALIGNED_ERROR);
	stress_no_return();
}

#if defined(HAVE_TIMER_FUNCTIONALITY)
static void stress_misaligned_reset_timer(void)
{
	timer.it_value.tv_sec = 0;
	timer.it_value.tv_nsec = STRESS_MISALIGNED_WAIT_TIME_NS;
	timer.it_interval.tv_sec = 0;
	timer.it_interval.tv_nsec = STRESS_MISALIGNED_WAIT_TIME_NS;
	VOID_RET(int, timer_settime(timer_id, 0, &timer, NULL));
}

static MLOCKED_TEXT void stress_misaligned_timer_handler(int signum)
{
	(void)signum;

	if (current_method)
		current_method->disabled = true;

	stress_misaligned_reset_timer();
	siglongjmp(jmp_env, STRESS_MISALIGNED_TIMED_OUT);
	stress_no_return();
}
#endif

static void stress_misaligned_enable_all(void)
{
	size_t i;

	for (i = 0; i < SIZEOF_ARRAY(stress_misaligned_methods); i++) {
		stress_misaligned_methods[i].disabled = false;
		stress_misaligned_methods[i].exercised = false;
	}
}

/*
 *  stress_misaligned_exercised()
 *	report the methods that were successfully exercised
 */
static void stress_misaligned_exercised(stress_args_t *args)
{
	char *str = NULL;
	ssize_t str_len = 0;
	size_t i;

	if (args->instance != 0)
		return;

	for (i = 0; i < SIZEOF_ARRAY(stress_misaligned_methods); i++) {
		const stress_misaligned_method_info_t *info = &stress_misaligned_methods[i];

		if (info->exercised && !info->disabled) {
			char *tmp;
			const size_t name_len = strlen(info->name);

			tmp = realloc(str, (size_t)str_len + name_len + 2);
			if (!tmp) {
				free(str);
				return;
			}
			str = tmp;
			if (str_len) {
				(void)shim_strscpy(str + str_len, " ", 2);
				str_len++;
			}
			(void)shim_strscpy(str + str_len, info->name, name_len + 1);
			str_len += name_len;
		}
	}

	if (str)
		pr_inf("%s: exercised %s\n", args->name, str);
	else
		pr_inf("%s: nothing exercised due to misalignment faults or disabled misaligned methods\n", args->name);

	free(str);
}

/*
 *  stress_misaligned()
 *	stress memory copies
 */
static int stress_misaligned(stress_args_t *args)
{
	uint8_t *buffer;
	size_t misaligned_method = 0;
	stress_misaligned_method_info_t *method;
	int ret, rc;
	const size_t page_size = args->page_size;
	const size_t buffer_size = page_size << 1;
	bool succeeded = true;
#if defined(HAVE_TIMER_FUNCTIONALITY)
	struct sigevent sev;
#if defined(CLOCK_PROCESS_CPUTIME_ID)
	const clockid_t clockid = CLOCK_PROCESS_CPUTIME_ID;
#else
	const clockid_t clockid = CLOCK_REALTIME;
#endif
#endif
#if defined(HAVE_MISALIGNED_NUMA)
	NOCLOBBER stress_numa_mask_t *numa_mask = NULL;
	NOCLOBBER stress_numa_mask_t *numa_nodes = NULL;
	int numa_loop;
#endif
	(void)stress_get_setting("misaligned-method", &misaligned_method);

	if (stress_sighandler(args->name, SIGBUS, stress_misaligned_handler, NULL) < 0)
		return EXIT_NO_RESOURCE;
	if (stress_sighandler(args->name, SIGILL, stress_misaligned_handler, NULL) < 0)
		return EXIT_NO_RESOURCE;
	if (stress_sighandler(args->name, SIGSEGV, stress_misaligned_handler, NULL) < 0)
		return EXIT_NO_RESOURCE;

#if defined(HAVE_TIMER_FUNCTIONALITY)
	if (stress_sighandler(args->name, SIGRTMIN, stress_misaligned_timer_handler, NULL) < 0)
		return EXIT_NO_RESOURCE;
#endif

	buffer = (uint8_t *)stress_mmap_populate(NULL, buffer_size,
				PROT_READ | PROT_WRITE,
				MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	if (buffer == MAP_FAILED) {
		pr_inf_skip("%s: cannot allocate 1 page buffer%s, "
			"errno=%d (%s), skipping stressor\n",
			args->name, stress_get_memfree_str(),
			errno, strerror(errno));
		return EXIT_NO_RESOURCE;
	}
	stress_set_vma_anon_name(buffer, buffer_size, "misaligned-data");
	(void)stress_madvise_mergeable(buffer, buffer_size);

#if defined(HAVE_MISALIGNED_NUMA)
	numa_mask = stress_numa_mask_alloc();
	if (numa_mask) {
		numa_nodes = stress_numa_mask_alloc();
		if (!numa_nodes) {
			stress_numa_mask_free(numa_mask);
			numa_mask = NULL;
		} else {
			if (stress_numa_mask_nodes_get(numa_nodes) < 1) {
				stress_numa_mask_free(numa_nodes);
				numa_nodes = NULL;
				stress_numa_mask_free(numa_mask);
				numa_mask = NULL;
			} else {
				stress_numa_randomize_pages(args, numa_nodes, numa_mask, buffer, buffer_size, page_size);
			}
		}
	}
#endif

#if defined(HAVE_TIMER_FUNCTIONALITY)
	(void)shim_memset(&sev, 0, sizeof(sev));
	sev.sigev_notify = SIGEV_SIGNAL;
	sev.sigev_signo = SIGRTMIN;
	sev.sigev_value.sival_ptr = &timer_id;
	if (timer_create(clockid, &sev, &timer_id) == 0) {
		use_timer = true;
		stress_misaligned_reset_timer();
	}
#endif
	stress_misaligned_enable_all();

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	method = &stress_misaligned_methods[misaligned_method];
	current_method = method;
	ret = sigsetjmp(jmp_env, 1);
	if (stress_instance_zero(args)) {
		switch (ret) {
		case STRESS_MISALIGNED_ERROR:
			pr_inf_skip("%s: skipping method %s, misaligned operations tripped %s\n",
				args->name, current_method->name,
				handled_signum == -1 ? "an error" :
				stress_strsignal(handled_signum));
			break;
		case STRESS_MISALIGNED_TIMED_OUT:
			pr_inf_skip("%s: skipping method %s, misaligned operations timed out after %.3f seconds, not fully tested\n",
				args->name, current_method->name,
				(double)STRESS_MISALIGNED_WAIT_TIME_NS / STRESS_DBL_NANOSECOND);
			break;
		default:
			break;
		}
	}

#if defined(HAVE_MISALIGNED_NUMA)
	numa_loop = 0;
#endif
	rc = EXIT_SUCCESS;
	do {
		if (stress_time_now() > args->time_end)
			break;
		if (method->disabled) {
			rc = EXIT_NO_RESOURCE;
			break;
		}
#if defined(HAVE_TIMER_FUNCTIONALITY)
		stress_misaligned_reset_timer();
#endif
		method->func(args, (uintptr_t)buffer, page_size, &succeeded);
		method->exercised = true;

#if defined(HAVE_MISALIGNED_NUMA)
		/*
		 *  On NUMA systems with > 1 node, randomize the
		 *  NUMA node binding of pages in the buffer
		 */
		if (numa_mask && numa_nodes && (numa_mask->nodes > 1)) {
			numa_loop++;
			if (numa_loop > 1024) {
				numa_loop = 0;
				stress_numa_randomize_pages(args, numa_nodes, numa_mask, buffer, buffer_size, page_size);
			}
		}
#endif
		stress_bogo_inc(args);
	} while (stress_continue(args));

#if defined(HAVE_TIMER_FUNCTIONALITY)
	if (use_timer) {
		(void)shim_memset(&timer, 0, sizeof(timer));
		VOID_RET(int, timer_settime(timer_id, 0, &timer, NULL));
		VOID_RET(int, timer_delete(timer_id));
	}
	(void)stress_sighandler_default(SIGRTMIN);
#endif
	(void)stress_sighandler_default(SIGBUS);
	(void)stress_sighandler_default(SIGILL);
	(void)stress_sighandler_default(SIGSEGV);

	stress_misaligned_exercised(args);

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

#if defined(HAVE_MISALIGNED_NUMA)
	if (numa_mask)
		stress_numa_mask_free(numa_mask);
	if (numa_nodes)
		stress_numa_mask_free(numa_nodes);
#endif
	(void)munmap((void *)buffer, buffer_size);

	if (!succeeded && (rc == EXIT_SUCCESS))
		rc = EXIT_FAILURE;

	return rc;
}

static const char *stress_misaligned_method(const size_t i)
{
	return (i < SIZEOF_ARRAY(stress_misaligned_methods)) ? stress_misaligned_methods[i].name : NULL;
}

static const stress_opt_t opts[] = {
	{ OPT_misaligned_method, "misaligned-method", TYPE_ID_SIZE_T_METHOD, 0, 0, stress_misaligned_method },
	END_OPT,
};

const stressor_info_t stress_misaligned_info = {
	.stressor = stress_misaligned,
	.classifier = CLASS_CPU_CACHE | CLASS_MEMORY,
	.opts = opts,
	.verify = VERIFY_ALWAYS,
	.help = help
};

#else

static const char *stress_misaligned_method(const size_t i)
{
	(void)i;

	return NULL;
}

static const stress_opt_t opts[] = {
	{ OPT_misaligned_method, "misaligned-method", TYPE_ID_SIZE_T_METHOD, 0, 0, stress_misaligned_method },
	END_OPT,
};

const stressor_info_t stress_misaligned_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_CPU_CACHE | CLASS_MEMORY,
	.opts = opts,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.unimplemented_reason = "built without siglongjmp support"
};

#endif
