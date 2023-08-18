// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King.
 *
 */
#include "stress-ng.h"
#include "core-bitops.h"
#include "core-target-clones.h"

#define MIN_VM_ADDR_BYTES	(8 * MB)
#define MAX_VM_ADDR_BYTES	(64 * MB)

#define NO_MEM_RETRIES_MAX	(100)

/*
 *  the VM stress test has diffent methods of vm stressor
 */
typedef size_t (*stress_vm_addr_func)(uint8_t *buf, const size_t sz);

typedef struct {
	const char *name;
	const stress_vm_addr_func func;
} stress_vm_addr_method_info_t;

typedef struct {
	uint64_t *bit_error_count;
	const stress_vm_addr_method_info_t *vm_addr_method;
	bool vm_addr_mlock;
} stress_vm_addr_context_t;

static const stress_vm_addr_method_info_t vm_addr_methods[];

static const stress_help_t help[] = {
	{ NULL,	"vm-addr N",	 "start N vm address exercising workers" },
	{ NULL,	"vm-addr-mlock", "attempt to mlock pages into memory" },
	{ NULL,	"vm-addr-ops N", "stop after N vm address bogo operations" },
	{ NULL,	NULL,		 NULL }
};

static int stress_set_vm_addr_mlock(const char *opt)
{
	return stress_set_setting_true("vm-addr-mlock", opt);
}

/*
 *  stress_continue(args)
 *	returns true if we can keep on running a stressor
 */
static bool HOT OPTIMIZE3 stress_continue_vm(const stress_args_t *args)
{
	return (LIKELY(stress_continue_flag()) &&
	        LIKELY(!args->max_ops || (stress_bogo_get(args) < args->max_ops)));
}

/*
 *  stress_vm_addr_pwr2()
 *	set data on power of 2 addresses
 */
static size_t TARGET_CLONES OPTIMIZE3 stress_vm_addr_pwr2(
	uint8_t *buf, const size_t sz)
{
	size_t n, errs = 0, step;
	uint8_t rnd = stress_mwc8();

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
	uint8_t *buf, const size_t sz)
{
	const size_t mask = sz - 1;
	size_t n, errs = 0, step;
	uint8_t rnd = stress_mwc8();

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
	uint8_t *buf, const size_t sz)
{
	size_t errs = 0, n;
	const size_t mask = sz - 1;
	uint8_t rnd = stress_mwc8();

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
	uint8_t *buf, const size_t sz)
{
	size_t n, errs = 0;
	const size_t mask = sz - 1;
	uint8_t rnd = stress_mwc8();

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
	uint8_t *buf, const size_t sz)
{
	size_t mask = sz - 1, n, errs = 0, shift;
	uint8_t rnd = stress_mwc8();

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
	uint8_t *buf, const size_t sz)
{
	size_t mask = sz - 1, n, errs = 0, shift;
	uint8_t rnd = stress_mwc8();

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
	uint8_t *buf, const size_t sz)
{
	size_t errs = 0;
	uint8_t rnd = stress_mwc8();
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
	size_t mask = sz - 1, n, errs = 0;
	uint8_t rnd = stress_mwc8();

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
	uint8_t *buf, const size_t sz)
{
	size_t errs = 0;
	uint8_t rnd = stress_mwc8();
	uint8_t *ptr;

	for (ptr = (uint8_t *)buf + sz - 1; ptr != buf; ptr--) {
		*ptr = rnd;
	}
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
	size_t mask = sz - 1, n, errs = 0;
	const uint8_t rnd = stress_mwc8();

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

	for (bits = nbits; --bits >= 0; ) {
		register size_t stride = 1U << bits;
		register uint8_t *ptr;

		for (ptr = buf; ptr < ptr_end; ptr += stride)
			*ptr = rnd;
	}

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

/*
 *  stress_set_vm_addr_method()
 *      set default vm stress method
 */
static int stress_set_vm_addr_method(const char *name)
{
	size_t i;

	for (i = 0; i < SIZEOF_ARRAY(vm_addr_methods); i++) {
		if (!strcmp(vm_addr_methods[i].name, name)) {
			stress_set_setting("vm-addr-method", TYPE_ID_SIZE_T, &i);
			return 0;
		}
	}

	(void)fprintf(stderr, "vm-addr-method must be one of:");
	for (i = 0; i < SIZEOF_ARRAY(vm_addr_methods); i++) {
		(void)fprintf(stderr, " %s", vm_addr_methods[i].name);
	}
	(void)fprintf(stderr, "\n");

	return -1;
}

static int stress_vm_addr_child(const stress_args_t *args, void *ctxt)
{
	int no_mem_retries = 0;
	size_t buf_sz = MIN_VM_ADDR_BYTES;
	uintptr_t buf_addr, max_addr = ~(uintptr_t)0;
	uint8_t *buf = NULL;
	stress_vm_addr_context_t *context = (stress_vm_addr_context_t *)ctxt;
	const stress_vm_addr_func func = context->vm_addr_method->func;

	do {
		for (buf_addr = args->page_size; buf_addr && (buf_addr < max_addr); buf_addr <<= 1) {
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
			if (context->vm_addr_mlock)
				(void)shim_mlock(buf, buf_sz);

			no_mem_retries = 0;
			*(context->bit_error_count) += func(buf, buf_sz);
			(void)stress_munmap_retry_enomem((void *)buf, buf_sz);
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
static int stress_vm_addr(const stress_args_t *args)
{
	const size_t page_size = args->page_size;
	size_t retries, vm_addr_method = 0;
	int err = 0, ret = EXIT_SUCCESS;
	stress_vm_addr_context_t context;


	(void)stress_get_setting("vm-addr-mlock", &context.vm_addr_mlock);
	(void)stress_get_setting("vm-addr-method", &vm_addr_method);

	context.vm_addr_method = &vm_addr_methods[vm_addr_method];
	context.vm_addr_mlock = false;
	context.bit_error_count = MAP_FAILED;

	if (args->instance == 0)
		pr_dbg("%s: using method '%s'\n", args->name, context.vm_addr_method->name);

	for (retries = 0; (retries < 100) && stress_continue_flag(); retries++) {
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
		if (stress_continue_flag()) {
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
	{ OPT_vm_addr_mlock,	stress_set_vm_addr_mlock },
	{ 0,			NULL }
};

stressor_info_t stress_vm_addr_info = {
	.stressor = stress_vm_addr,
	.class = CLASS_VM | CLASS_MEMORY | CLASS_OS,
	.opt_set_funcs = opt_set_funcs,
	.verify = VERIFY_ALWAYS,
	.help = help
};
