/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C)      2022 Colin Ian King.
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
#include "core-target-clones.h"

#define MIN_VM_ADDR_BYTES	(8 * MB)
#define MAX_VM_ADDR_BYTES	(64 * MB)
#define NO_MEM_RETRIES_MAX	(100)

#define ALIGN_VM	ALIGNED(64)

/*
 *  the VM stress test has diffent methods of vm stressor
 */
typedef size_t (*stress_vm_addr_func)(void *buf, const size_t sz);

typedef struct {
	const char *name;
	const stress_vm_addr_func func;
} stress_vm_addr_method_info_t;

typedef struct {
	uint64_t *bit_error_count;
	const stress_vm_addr_method_info_t *vm_addr_method;
} stress_vm_addr_context_t;

static const stress_vm_addr_method_info_t vm_addr_methods[];

static const stress_help_t help[] = {
	{ NULL,	"vm-addr N",	 "start N vm address exercising workers" },
	{ NULL,	"vm-addr-ops N", "stop after N vm address bogo operations" },
	{ NULL,	NULL,		 NULL }
};

/*
 *  keep_stressing(args)
 *	returns true if we can keep on running a stressor
 */
static bool HOT OPTIMIZE3 keep_stressing_vm(const stress_args_t *args)
{
	return (LIKELY(keep_stressing_flag()) &&
	        LIKELY(!args->max_ops || (get_counter(args) < args->max_ops)));
}

/*
 *  stress_vm_addr_pwr2()
 *	set data on power of 2 addresses
 */
static size_t TARGET_CLONES OPTIMIZE3 stress_vm_addr_pwr2(
	void *ptr,
	const size_t sz)
{
	size_t n, errs = 0, step;
	uint8_t rnd = stress_mwc8();
	uint8_t *ALIGN_VM buf = (uint8_t *)ptr;

	for (step = 1, n = 0; n < sz; n += step) {
		*(buf + n) = rnd;
		step = (step >= 4096) ? 1 : step << 1;
	}
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
	void *ptr,
	const size_t sz)
{
	const size_t mask = sz - 1;
	size_t n, errs = 0, step;
	uint8_t rnd = stress_mwc8();
	uint8_t *ALIGN_VM buf = (uint8_t *)ptr;

	for (step = 1, n = 0; n < sz; n += step) {
		*(buf + (n ^ mask)) = rnd;
		step = (step >= 4096) ? 1 : step << 1;
	}
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
	void *ptr,
	const size_t sz)
{
	size_t errs = 0, n;
	const size_t mask = sz - 1;
	uint8_t rnd = stress_mwc8();
	uint8_t *ALIGN_VM buf = (uint8_t *)ptr;

	for (n = 0; n < sz; n++) {
		size_t gray = ((n >> 1) ^ n) & mask;

		*(buf + gray) = rnd;
	}
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
	void *ptr,
	const size_t sz)
{
	size_t n, errs = 0;
	const size_t mask = sz - 1;
	uint8_t rnd = stress_mwc8();
	uint8_t *ALIGN_VM buf = (uint8_t *)ptr;

	for (n = 0; n < sz; n++) {
		size_t gray = (((n >> 1) ^ n) ^ mask) & mask;

		*(buf + gray) = rnd;
	}
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
	void *ptr,
	const size_t sz)
{
	size_t mask = sz - 1, n, errs = 0, shift;
	uint8_t rnd = stress_mwc8();
	uint8_t *ALIGN_VM buf = (uint8_t *)ptr;

	for (shift = 0, n = sz; n; shift++, n <<= 1)
		;

	for (n = 0; n < sz; n++) {
		size_t i = stress_reverse64(n << shift) & mask;

		*(buf + i) = rnd;
	}
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
	void *ptr,
	const size_t sz)
{
	size_t mask = sz - 1, n, errs = 0, shift;
	uint8_t rnd = stress_mwc8();
	uint8_t *ALIGN_VM buf = (uint8_t *)ptr;

	for (shift = 0, n = sz; n; shift++, n <<= 1)
		;

	for (n = 0; n < sz; n++) {
		size_t i = (stress_reverse64(n << shift) ^ mask) & mask;

		*(buf + i) = rnd;
	}
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
	void *ptr,
	const size_t sz)
{
	size_t errs = 0;
	uint8_t rnd = stress_mwc8();
	uint8_t *ALIGN_VM buf = (uint8_t *)ptr;
	uint8_t *ALIGN_VM buf_end = (uint8_t *)ptr + sz;

	for (buf = ptr; buf < buf_end; buf++) {
		*buf = rnd;
	}
	for (buf = ptr; LIKELY(buf < buf_end); buf++) {
		if (UNLIKELY(*buf != rnd))
			errs++;
	}
	return errs;
}

/*
 *  stress_vm_addr_inc()
 *	set data on inverted incrementing addresses
 */
static size_t TARGET_CLONES OPTIMIZE3 stress_vm_addr_incinv(
	void *ptr,
	const size_t sz)
{
	size_t mask = sz - 1, n, errs = 0;
	uint8_t rnd = stress_mwc8();
	uint8_t *ALIGN_VM buf = (uint8_t *)ptr;

	for (n = 0; n < sz; n++) {
		size_t i = (n ^ mask) & mask;

		*(buf + i) = rnd;
	}
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
	void *ptr,
	const size_t sz)
{
	size_t errs = 0;
	uint8_t rnd = stress_mwc8();
	uint8_t *ALIGN_VM buf_end = (uint8_t *)ptr;
	uint8_t *ALIGN_VM buf;

	for (buf = (uint8_t *)ptr + sz - 1; buf != buf_end; buf--) {
		*buf = rnd;
	}
	for (buf = (uint8_t *)ptr + sz - 1; buf != buf_end; buf--) {
		if (UNLIKELY(*buf != rnd))
			errs++;
	}

	return errs;
}

/*
 *  stress_vm_addr_dec()
 *	set data on inverted decrementing addresses
 */
static size_t TARGET_CLONES OPTIMIZE3 stress_vm_addr_decinv(
	void *ptr,
	const size_t sz)
{
	size_t mask = sz - 1, n, errs = 0;
	const uint8_t rnd = stress_mwc8();
	uint8_t *ALIGN_VM buf = (uint8_t *)ptr;

	for (n = sz; n; n--) {
		const size_t i = ((n - 1) ^ mask) & mask;

		*(buf + i) = rnd;
	}
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
	void *ptr,
	const size_t sz)
{
	int bits, nbits;
	size_t mask;
	size_t errs = 0;
	const uint8_t *buf_end = (uint8_t *)ptr + sz;
	const uint8_t rnd = stress_mwc8();

	/* log2(sz / 2) */
	for (mask = sz - 1, nbits = 0; mask; mask >>= 1)
		nbits++;

	for (bits = nbits; --bits >= 0; ) {
		register size_t stride = 1U << bits;
		register uint8_t *buf;

		for (buf = ptr; buf < buf_end; buf += stride)
			*buf = rnd;
	}

	for (bits = 0; bits < nbits; bits++) {
		register size_t stride = 1U << bits;
		register uint8_t *buf;

		for (buf = ptr; buf < buf_end; buf += stride) {
			if (UNLIKELY(*buf != rnd))
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
	void *ptr,
	const size_t sz)
{
	register size_t n;
	const uint8_t rnd = stress_mwc8();
	register uint8_t *buf = ptr;
	size_t errs = 0;
	register const size_t mask = sz - 1;

	for (n = 0; n < sz; n++) {
		register size_t gray = ((n >> 1) ^ n);

		buf[gray & mask] = rnd;
		buf[(gray ^ mask) & mask] = rnd;
	}

	for (n = 0; n < sz; n++) {
		register size_t gray = ((n >> 1) ^ n);

		if (UNLIKELY(buf[gray & mask] != rnd))
			errs++;
		if (UNLIKELY(buf[(gray ^ mask) & mask] != rnd))
			errs++;
	}
	return errs;
}

/*
 *  stress_vm_addr_all()
 *	work through all vm stressors sequentially
 */
static size_t stress_vm_addr_all(
	void *ptr,
	const size_t sz)
{
	static int i = 1;
	size_t bit_errors = 0;

	bit_errors = vm_addr_methods[i].func(ptr, sz);
	i++;
	if (vm_addr_methods[i].func == NULL)
		i = 1;

	return bit_errors;
}

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
	{ NULL,		NULL  }
};

/*
 *  stress_set_vm_addr_method()
 *      set default vm stress method
 */
static int stress_set_vm_addr_method(const char *name)
{
	stress_vm_addr_method_info_t const *info;

	for (info = vm_addr_methods; info->func; info++) {
		if (!strcmp(info->name, name)) {
			stress_set_setting("vm-addr-method", TYPE_ID_UINTPTR_T, &info);
			return 0;
		}
	}

	(void)fprintf(stderr, "vm-addr-method must be one of:");
	for (info = vm_addr_methods; info->func; info++) {
		(void)fprintf(stderr, " %s", info->name);
	}
	(void)fprintf(stderr, "\n");

	return -1;
}

static int stress_vm_addr_child(const stress_args_t *args, void *ctxt)
{
	int no_mem_retries = 0;
	void *vm_base_addr;
	size_t buf_sz;
	void *buf = NULL;
	stress_vm_addr_context_t *context = (stress_vm_addr_context_t *)ctxt;
	const stress_vm_addr_func func = context->vm_addr_method->func;

	buf_sz = MIN_VM_ADDR_BYTES;

	do {
		vm_base_addr = (void *)buf_sz;

		if (no_mem_retries >= NO_MEM_RETRIES_MAX) {
			pr_err("%s: gave up trying to mmap, no available memory\n",
				args->name);
			break;
		}
		if ((g_opt_flags & OPT_FLAGS_OOM_AVOID) && stress_low_memory(buf_sz))
			buf_sz = MIN_VM_ADDR_BYTES;

		buf = (void *)mmap(vm_base_addr, buf_sz,
			PROT_READ | PROT_WRITE,
			MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		if (buf == MAP_FAILED) {
			buf = NULL;
			no_mem_retries++;
			(void)shim_usleep(100000);
			goto next;	/* Try again */
		}

		no_mem_retries = 0;
		*(context->bit_error_count) += func(buf, buf_sz);
		(void)munmap((void *)buf, buf_sz);
next:
		buf_sz <<= 1;
		if (buf_sz > MAX_VM_ADDR_BYTES)
			buf_sz = MIN_VM_ADDR_BYTES;
		inc_counter(args);
	} while (keep_stressing_vm(args));

	return EXIT_SUCCESS;
}

/*
 *  stress_vm_addr()
 *	stress virtual memory addressing
 */
static int stress_vm_addr(const stress_args_t *args)
{
	const size_t page_size = args->page_size;
	size_t retries;
	int err = 0, ret = EXIT_SUCCESS;
	stress_vm_addr_context_t context;

	context.vm_addr_method = &vm_addr_methods[0];
	context.bit_error_count = MAP_FAILED;

	(void)stress_get_setting("vm-addr-method", &context.vm_addr_method);

	pr_dbg("%s: using method '%s'\n", args->name, context.vm_addr_method->name);

	for (retries = 0; (retries < 100) && keep_stressing_flag(); retries++) {
		context.bit_error_count = (uint64_t *)
			mmap(NULL, page_size, PROT_READ | PROT_WRITE,
				MAP_SHARED | MAP_ANONYMOUS, -1, 0);
		err = errno;
		if (context.bit_error_count != MAP_FAILED)
			break;
		(void)shim_usleep(100);
	}

	/* Cannot allocate a single page for bit error counter */
	if (context.bit_error_count == MAP_FAILED) {
		if (keep_stressing_flag()) {
			pr_err("%s: could not mmap bit error counter: "
				"retry count=%zu, errno=%d (%s)\n",
				args->name, retries, err, strerror(err));
		}
		return EXIT_NO_RESOURCE;
	}

	*context.bit_error_count = 0ULL;

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	ret = stress_oomable_child(args, &context, stress_vm_addr_child, STRESS_OOMABLE_NORMAL);

	if (*context.bit_error_count > 0) {
		pr_fail("%s: detected %" PRIu64 " bit errors while "
			"stressing memory\n",
			args->name, *context.bit_error_count);
		ret = EXIT_FAILURE;
	}
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	(void)munmap((void *)context.bit_error_count, page_size);

	return ret;
}

static const stress_opt_set_func_t opt_set_funcs[] = {
	{ OPT_vm_addr_method,	stress_set_vm_addr_method },
	{ 0,			NULL }
};

stressor_info_t stress_vm_addr_info = {
	.stressor = stress_vm_addr,
	.class = CLASS_VM | CLASS_MEMORY | CLASS_OS,
	.opt_set_funcs = opt_set_funcs,
	.verify = VERIFY_ALWAYS,
	.help = help
};
