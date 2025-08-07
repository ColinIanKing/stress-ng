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
#include "core-bitops.h"
#include "core-cpu-cache.h"
#include "core-madvise.h"
#include "core-mmap.h"
#include "core-numa.h"
#include "core-out-of-memory.h"
#include "core-pragma.h"
#include "core-target-clones.h"

#define MIN_VM_ADDR_BYTES	(8 * MB)
#define MAX_VM_ADDR_BYTES	(64 * MB)

#define NO_MEM_RETRIES_MAX	(100)

/*
 *  the VM stress test has different methods of vm stressor
 */
typedef size_t (*stress_vm_addr_func)(uint8_t *buf, const size_t sz);

typedef struct {
	const char *name;
	const stress_vm_addr_func func;
} stress_vm_addr_method_info_t;

typedef struct {
	uint64_t *bit_error_count;
	const stress_vm_addr_method_info_t *vm_addr_method;
#if defined(HAVE_LINUX_MEMPOLICY_H)
	stress_numa_mask_t *numa_mask;
	stress_numa_mask_t *numa_nodes;
#endif
	bool vm_addr_mlock;
	bool vm_addr_numa;
} stress_vm_addr_context_t;

static const stress_vm_addr_method_info_t vm_addr_methods[];

static const stress_help_t help[] = {
	{ NULL,	"vm-addr N",	    "start N vm address exercising workers" },
	{ NULL, "vm-addr-method M", "select method to exercise vm addresses" },
	{ NULL,	"vm-addr-mlock",    "attempt to mlock pages into memory" },
	{ NULL,	"vm-addr-numa",     "bind memory mappings to randomly selected NUMA nodes" },
	{ NULL,	"vm-addr-ops N",    "stop after N vm address bogo operations" },
	{ NULL,	NULL,		 NULL }
};

/*
 *  stress_continue(args)
 *	returns true if we can keep on running a stressor
 */
static bool OPTIMIZE3 stress_continue_vm(stress_args_t *args)
{
	return (LIKELY(stress_continue_flag()) &&
	        LIKELY(!args->bogo.max_ops || (stress_bogo_get(args) < args->bogo.max_ops)));
}

/*
 *  stress_vm_addr_pwr2()
 *	set data on power of 2 addresses
 */
static size_t TARGET_CLONES OPTIMIZE3 stress_vm_addr_pwr2(
	uint8_t *buf, const size_t sz)
{
	size_t n, errs = 0, step;
	const uint8_t rnd = stress_mwc8();

PRAGMA_UNROLL_N(4)
	for (step = 1, n = 0; n < sz; n += step) {
		*(buf + n) = rnd;
		step = (step >= 4096) ? 1 : step << 1;
	}
	if (g_opt_flags & OPT_FLAGS_AGGRESSIVE)
		stress_cpu_data_cache_flush((void *)buf, sz);
PRAGMA_UNROLL_N(4)
	for (step = 1, n = 0; n < sz; n += step) {
		if (UNLIKELY(*(buf + n) != rnd))
			errs++;
		step = (step >= 4096) ? 1 : step << 1;
	}
	return errs;
}

/*
 *  stress_vm_addr_pwr2inv()
 *	set data on inverted power of 2 addresses
 */
static size_t TARGET_CLONES OPTIMIZE3 stress_vm_addr_pwr2inv(
	uint8_t *buf, const size_t sz)
{
	const size_t mask = sz - 1;
	size_t n, errs = 0, step;
	const uint8_t rnd = stress_mwc8();

PRAGMA_UNROLL_N(4)
	for (step = 1, n = 0; n < sz; n += step) {
		*(buf + (n ^ mask)) = rnd;
		step = (step >= 4096) ? 1 : step << 1;
	}
	if (g_opt_flags & OPT_FLAGS_AGGRESSIVE)
		stress_cpu_data_cache_flush((void *)buf, sz);
PRAGMA_UNROLL_N(4)
	for (step = 1, n = 0; n < sz; n += step) {
		if (UNLIKELY(*(buf + (n ^ mask)) != rnd))
			errs++;
		step = (step >= 4096) ? 1 : step << 1;
	}
	return errs;
}

/*
 *  stress_vm_addr_gray()
 *	set data on gray coded addresses,
 *	each address changes by just 1 bit
 */
static size_t TARGET_CLONES OPTIMIZE3 stress_vm_addr_gray(
	uint8_t *buf, const size_t sz)
{
	size_t errs = 0, n;
	const size_t mask = sz - 1;
	const uint8_t rnd = stress_mwc8();

PRAGMA_UNROLL_N(4)
	for (n = 0; n < sz; n++) {
		size_t gray = ((n >> 1) ^ n) & mask;

		*(buf + gray) = rnd;
	}
	if (g_opt_flags & OPT_FLAGS_AGGRESSIVE)
		stress_cpu_data_cache_flush((void *)buf, sz);
PRAGMA_UNROLL_N(4)
	for (n = 0; n < sz; n++) {
		size_t gray = ((n >> 1) ^ n) & mask;

		if (UNLIKELY(*(buf + gray) != rnd))
			errs++;
	}
	return errs;
}

/*
 *  stress_vm_addr_grayinv()
 *	set data on inverted gray coded addresses,
 *	each address changes by as many bits possible
 */
static size_t TARGET_CLONES OPTIMIZE3 stress_vm_addr_grayinv(
	uint8_t *buf, const size_t sz)
{
	size_t n, errs = 0;
	const size_t mask = sz - 1;
	const uint8_t rnd = stress_mwc8();

PRAGMA_UNROLL_N(4)
	for (n = 0; n < sz; n++) {
		size_t gray = (((n >> 1) ^ n) ^ mask) & mask;

		*(buf + gray) = rnd;
	}
	if (g_opt_flags & OPT_FLAGS_AGGRESSIVE)
		stress_cpu_data_cache_flush((void *)buf, sz);
PRAGMA_UNROLL_N(4)
	for (n = 0; n < sz; n++) {
		size_t gray = (((n >> 1) ^ n) ^ mask) & mask;

		if (UNLIKELY(*(buf + gray) != rnd))
			errs++;
	}
	return errs;
}

/*
 *  stress_vm_addr_rev()
 *	set data on reverse address bits, for example
 *	a 32 bit address range becomes:
 *	0x00000001 -> 0x1000000
 * 	0x00000002 -> 0x2000000
 */
static size_t TARGET_CLONES OPTIMIZE3 stress_vm_addr_rev(
	uint8_t *buf, const size_t sz)
{
	size_t n, errs = 0, shift;
	const size_t mask = sz - 1;
	const uint8_t rnd = stress_mwc8();

	for (shift = 0, n = sz; n; shift++, n <<= 1)
		;

PRAGMA_UNROLL_N(4)
	for (n = 0; n < sz; n++) {
		size_t i = stress_reverse64(n << shift) & mask;

		*(buf + i) = rnd;
	}
	if (g_opt_flags & OPT_FLAGS_AGGRESSIVE)
		stress_cpu_data_cache_flush((void *)buf, sz);
PRAGMA_UNROLL_N(4)
	for (n = 0; n < sz; n++) {
		size_t i = stress_reverse64(n << shift) & mask;

		if (UNLIKELY(*(buf + i) != rnd))
			errs++;
	}
	return errs;
}

/*
 *  stress_vm_addr_revinv()
 *	set data on inverted reverse address bits, for example
 *	a 32 bit address range becomes:
 *	0x00000001 -> 0xeffffff
 * 	0x00000002 -> 0xdffffff
 */
static size_t TARGET_CLONES OPTIMIZE3 stress_vm_addr_revinv(
	uint8_t *buf, const size_t sz)
{
	size_t n, errs = 0, shift;
	const size_t mask = sz - 1;
	const uint8_t rnd = stress_mwc8();

	for (shift = 0, n = sz; n; shift++, n <<= 1)
		;

PRAGMA_UNROLL_N(4)
	for (n = 0; n < sz; n++) {
		size_t i = (stress_reverse64(n << shift) ^ mask) & mask;

		*(buf + i) = rnd;
	}
	if (g_opt_flags & OPT_FLAGS_AGGRESSIVE)
		stress_cpu_data_cache_flush((void *)buf, sz);
PRAGMA_UNROLL_N(4)
	for (n = 0; n < sz; n++) {
		size_t i = (stress_reverse64(n << shift) ^ mask) & mask;

		if (UNLIKELY(*(buf + i) != rnd))
			errs++;
	}
	return errs;
}

/*
 *  stress_vm_addr_inc()
 *	set data on incrementing addresses
 */
static size_t TARGET_CLONES OPTIMIZE3 stress_vm_addr_inc(
	uint8_t *buf, const size_t sz)
{
	size_t errs = 0;
	const uint8_t rnd = stress_mwc8();
	register uint8_t *ptr;
	register const uint8_t *ptr_end = buf + sz;

	for (ptr = buf; LIKELY(ptr < ptr_end); ptr++) {
		*ptr = rnd;
	}
	for (ptr = buf; LIKELY(ptr < ptr_end); ptr++) {
		if (UNLIKELY(*ptr != rnd))
			errs++;
	}
	return errs;
}

/*
 *  stress_vm_addr_inc()
 *	set data on inverted incrementing addresses
 */
static size_t TARGET_CLONES OPTIMIZE3 stress_vm_addr_incinv(
	uint8_t *buf, const size_t sz)
{
	size_t n, errs = 0;
	const size_t mask = sz - 1;
	const uint8_t rnd = stress_mwc8();

PRAGMA_UNROLL_N(4)
	for (n = 0; n < sz; n++) {
		size_t i = (n ^ mask) & mask;

		*(buf + i) = rnd;
	}
	if (g_opt_flags & OPT_FLAGS_AGGRESSIVE)
		stress_cpu_data_cache_flush((void *)buf, sz);
PRAGMA_UNROLL_N(4)
	for (n = 0; n < sz; n++) {
		size_t i = (n ^ mask) & mask;

		if (UNLIKELY(*(buf + i) != rnd))
			errs++;
	}
	return errs;
}

/*
 *  stress_vm_addr_dec()
 *	set data on decrementing addresses
 */
static size_t TARGET_CLONES OPTIMIZE3 stress_vm_addr_dec(
	uint8_t *buf, const size_t sz)
{
	size_t errs = 0;
	const uint8_t rnd = stress_mwc8();
	uint8_t *ptr;

PRAGMA_UNROLL_N(4)
	for (ptr = (uint8_t *)buf + sz - 1; ptr != buf; ptr--) {
		*ptr = rnd;
	}
	if (g_opt_flags & OPT_FLAGS_AGGRESSIVE)
		stress_cpu_data_cache_flush((void *)buf, sz);
PRAGMA_UNROLL_N(4)
	for (ptr = (uint8_t *)buf + sz - 1; ptr != buf; ptr--) {
		if (UNLIKELY(*ptr != rnd))
			errs++;
	}

	return errs;
}

/*
 *  stress_vm_addr_dec()
 *	set data on inverted decrementing addresses
 */
static size_t TARGET_CLONES OPTIMIZE3 stress_vm_addr_decinv(
	uint8_t *buf, const size_t sz)
{
	size_t n, errs = 0;
	const size_t mask = sz - 1;
	const uint8_t rnd = stress_mwc8();

PRAGMA_UNROLL_N(4)
	for (n = sz; n; n--) {
		const size_t i = ((n - 1) ^ mask) & mask;

		*(buf + i) = rnd;
	}
PRAGMA_UNROLL_N(4)
	for (n = sz; n; n--) {
		const size_t i = ((n - 1) ^ mask) & mask;

		if (UNLIKELY(*(buf + i) != rnd))
			errs++;
	}
	return errs;
}

/*
 *  stress_vm_addr_bitposn()
 *	write across addresses in bit posn strides, in repeated
 *      strides	of log2(sz / 2) down to 1 and check in strides of 1
 *	to log2(sz / 2)
 */
static size_t TARGET_CLONES OPTIMIZE3 stress_vm_addr_bitposn(
	uint8_t *buf, const size_t sz)
{
	int bits, nbits;
	size_t mask;
	size_t errs = 0;
	const uint8_t *ptr_end = buf + sz;
	const uint8_t rnd = stress_mwc8();

	/* log2(sz / 2) */
	for (mask = sz - 1, nbits = 0; mask; mask >>= 1)
		nbits++;

PRAGMA_UNROLL_N(4)
	for (bits = nbits; --bits >= 0; ) {
		register size_t stride = 1U << bits;
		register uint8_t *ptr;

		for (ptr = buf; ptr < ptr_end; ptr += stride)
			*ptr = rnd;
	}
	if (g_opt_flags & OPT_FLAGS_AGGRESSIVE)
		stress_cpu_data_cache_flush((void *)buf, sz);
PRAGMA_UNROLL_N(4)
	for (bits = 0; bits < nbits; bits++) {
		register size_t stride = 1U << bits;
		register uint8_t *ptr;

		for (ptr = buf; ptr < ptr_end; ptr += stride) {
			if (UNLIKELY(*ptr != rnd))
				errs++;
		}
	}
	return errs;
}

/*
 *  stress_vm_addr_flip()
 * 	address memory using gray coded increments and inverse
 *	to flip as many address bits per write/read cycle
 */
static size_t TARGET_CLONES OPTIMIZE3 stress_vm_addr_flip(
	uint8_t *buf, const size_t sz)
{
	register size_t n;
	const uint8_t rnd = stress_mwc8();
	size_t errs = 0;
	register const size_t mask = sz - 1;

PRAGMA_UNROLL_N(4)
	for (n = 0; n < sz; n++) {
		register size_t gray = ((n >> 1) ^ n);

		buf[gray & mask] = rnd;
		buf[(gray ^ mask) & mask] = rnd;
	}
	if (g_opt_flags & OPT_FLAGS_AGGRESSIVE)
		stress_cpu_data_cache_flush((void *)buf, sz);
PRAGMA_UNROLL_N(4)
	for (n = 0; n < sz; n++) {
		register size_t gray = ((n >> 1) ^ n);

		if (UNLIKELY(buf[gray & mask] != rnd))
			errs++;
		if (UNLIKELY(buf[(gray ^ mask) & mask] != rnd))
			errs++;
	}
	return errs;
}

static size_t stress_vm_addr_all(uint8_t *buf, const size_t sz);

static const stress_vm_addr_method_info_t vm_addr_methods[] = {
	{ "all",	stress_vm_addr_all },
	{ "bitposn",	stress_vm_addr_bitposn },
	{ "pwr2",	stress_vm_addr_pwr2 },
	{ "pwr2inv",	stress_vm_addr_pwr2inv },
	{ "flip",	stress_vm_addr_flip },
	{ "gray",	stress_vm_addr_gray },
	{ "grayinv",	stress_vm_addr_grayinv },
	{ "rev",	stress_vm_addr_rev },
	{ "revinv",	stress_vm_addr_revinv },
	{ "inc",	stress_vm_addr_inc },
	{ "incinv",	stress_vm_addr_incinv },
	{ "dec",	stress_vm_addr_dec },
	{ "decinv",	stress_vm_addr_decinv },
};

/*
 *  stress_vm_addr_all()
 *	work through all vm stressors sequentially
 */
static size_t stress_vm_addr_all(uint8_t *buf, const size_t sz)
{
	static size_t i = 1;
	size_t bit_errors = 0;

	bit_errors = vm_addr_methods[i].func(buf, sz);
	i++;
	if (i >= SIZEOF_ARRAY(vm_addr_methods))
		i = 1;

	return bit_errors;
}

static int stress_vm_addr_child(stress_args_t *args, void *ctxt)
{
	int no_mem_retries = 0;
	size_t buf_sz = MIN_VM_ADDR_BYTES;
	uintptr_t buf_addr, max_addr = ~(uintptr_t)0;
	uint8_t *buf = NULL;
	stress_vm_addr_context_t *context = (stress_vm_addr_context_t *)ctxt;
	const stress_vm_addr_func func = context->vm_addr_method->func;
	const size_t page_size = args->page_size;

	stress_catch_sigill();

	do {
		for (buf_addr = page_size; buf_addr && (buf_addr < max_addr); buf_addr <<= 1) {
			if (no_mem_retries >= NO_MEM_RETRIES_MAX) {
				pr_err("%s: gave up trying to mmap, no available memory\n",
					args->name);
				break;
			}
			if ((g_opt_flags & OPT_FLAGS_OOM_AVOID) && stress_low_memory(buf_sz))
				buf_sz = MIN_VM_ADDR_BYTES;

			buf = (uint8_t *)mmap((void *)buf_addr, buf_sz,
				PROT_READ | PROT_WRITE,
				MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
			if (buf == MAP_FAILED) {
				no_mem_retries++;
				(void)shim_usleep(100000);
				continue;
			}
			(void)stress_madvise_mergeable(buf, buf_sz);
#if defined(HAVE_LINUX_MEMPOLICY_H)
			if (context->vm_addr_numa)
				stress_numa_randomize_pages(args, context->numa_nodes, context->numa_mask, buf, buf_sz, page_size);
#endif
			if (context->vm_addr_mlock)
				(void)shim_mlock(buf, buf_sz);

			no_mem_retries = 0;
			*(context->bit_error_count) += func(buf, buf_sz);
			(void)stress_munmap_force((void *)buf, buf_sz);
			stress_bogo_inc(args);
			if (!stress_continue_vm(args))
				break;
		}
		buf_sz <<= 1;
		if (buf_sz > MAX_VM_ADDR_BYTES)
			buf_sz = MIN_VM_ADDR_BYTES;
	} while (stress_continue_vm(args));

	return EXIT_SUCCESS;
}

/*
 *  stress_vm_addr()
 *	stress virtual memory addressing
 */
static int stress_vm_addr(stress_args_t *args)
{
	const size_t page_size = args->page_size;
	size_t retries, vm_addr_method = 0;
	int err = 0, ret = EXIT_SUCCESS;
	stress_vm_addr_context_t context;

	context.vm_addr_mlock = false;
	context.vm_addr_numa = false;

	(void)stress_get_setting("vm-addr-mlock", &context.vm_addr_mlock);
	(void)stress_get_setting("vm-addr-method", &vm_addr_method);
	(void)stress_get_setting("vm-addr-numa", &context.vm_addr_numa);

	context.vm_addr_method = &vm_addr_methods[vm_addr_method];
	context.vm_addr_mlock = false;
	context.bit_error_count = MAP_FAILED;
#if defined(HAVE_LINUX_MEMPOLICY_H)
	context.numa_mask = NULL;
	context.numa_nodes = NULL;
#endif

	if (stress_instance_zero(args))
		pr_dbg("%s: using method '%s'\n", args->name, context.vm_addr_method->name);

	for (retries = 0; LIKELY((retries < 100) && stress_continue_flag()); retries++) {
		context.bit_error_count = (uint64_t *)
			stress_mmap_populate(NULL, page_size,
				PROT_READ | PROT_WRITE,
				MAP_SHARED | MAP_ANONYMOUS, -1, 0);
		err = errno;
		if (context.bit_error_count != MAP_FAILED)
			break;
		(void)shim_usleep(100);
	}

	/* Cannot allocate a single page for bit error counter */
	if (context.bit_error_count == MAP_FAILED) {
		if (UNLIKELY(stress_continue_flag())) {
			pr_err("%s: could not mmap bit error counter: "
				"retry count=%zu, errno=%d (%s)\n",
				args->name, retries, err, strerror(err));
		}
		return EXIT_NO_RESOURCE;
	}
	stress_set_vma_anon_name(context.bit_error_count, page_size, "bit-error-count");

	if (context.vm_addr_numa) {
#if defined(HAVE_LINUX_MEMPOLICY_H)
		stress_numa_mask_and_node_alloc(args, &context.numa_nodes,
						&context.numa_mask, "--vm-addr-numa",
						&context.vm_addr_numa);
#else
		if (stress_instance_zero(args))
			pr_inf("%s: --vm-addr-uma selected but not supported by this system, disabling option\n",
				args->name);
		context.vm_addr_numa = false;
#endif
	}

	*context.bit_error_count = 0ULL;

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	ret = stress_oomable_child(args, &context, stress_vm_addr_child, STRESS_OOMABLE_NORMAL);

	if (*context.bit_error_count > 0) {
		pr_fail("%s: detected %" PRIu64 " bit errors while "
			"stressing memory\n",
			args->name, *context.bit_error_count);
		ret = EXIT_FAILURE;
	}
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

#if defined(HAVE_LINUX_MEMPOLICY_H)
	if (context.numa_mask)
		stress_numa_mask_free(context.numa_mask);
	if (context.numa_nodes)
		stress_numa_mask_free(context.numa_nodes);
#endif
	(void)munmap((void *)context.bit_error_count, page_size);

	return ret;
}

static const char *stress_vm_addr_method(const size_t i)
{
	return (i < SIZEOF_ARRAY(vm_addr_methods)) ? vm_addr_methods[i].name : NULL;
}

static const stress_opt_t opts[] = {
	{ OPT_vm_addr_method, "vm-addr-method", TYPE_ID_SIZE_T_METHOD, 0, 0, stress_vm_addr_method },
	{ OPT_vm_addr_mlock,  "vm-addr-mlock",  TYPE_ID_BOOL, 0, 1, NULL },
	{ OPT_vm_addr_numa,   "vm-addr-numa",   TYPE_ID_BOOL, 0, 1, NULL },
	END_OPT,
};

const stressor_info_t stress_vm_addr_info = {
	.stressor = stress_vm_addr,
	.classifier = CLASS_VM | CLASS_MEMORY | CLASS_OS,
	.opts = opts,
	.verify = VERIFY_ALWAYS,
	.help = help
};
