/*
 * Copyright (C) 2013-2019 Canonical, Ltd.
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

static const stress_vm_addr_method_info_t vm_addr_methods[];

static const help_t help[] = {
	{ NULL,	"vm-addr N",	 "start N vm address exercising workers" },
	{ NULL,	"vm-addr-ops N", "stop after N vm address bogo operations" },
	{ NULL,	NULL,		 NULL }
};

/*
 *  keep_stressing()
 *	returns true if we can keep on running a stressor
 */
static bool HOT OPTIMIZE3 keep_stressing_vm(const args_t *args)
{
	return (LIKELY(g_keep_stressing_flag) &&
	        LIKELY(!args->max_ops || (get_counter(args) < args->max_ops)));
}

/*
 *  reverse64
 *	generic fast-ish 64 bit reverse
 */
static uint64_t reverse64(register uint64_t x)
{
	x = (((x & 0xaaaaaaaaaaaaaaaaULL) >> 1)  | ((x & 0x5555555555555555ULL) << 1));
	x = (((x & 0xccccccccccccccccULL) >> 2)  | ((x & 0x3333333333333333ULL) << 2));
	x = (((x & 0xf0f0f0f0f0f0f0f0ULL) >> 4)  | ((x & 0x0f0f0f0f0f0f0f0fULL) << 4));
	x = (((x & 0xff00ff00ff00ff00ULL) >> 8)  | ((x & 0x00ff00ff00ff00ffULL) << 8));
	x = (((x & 0xffff0000ffff0000ULL) >> 16) | ((x & 0x0000ffff0000ffffULL) << 16));
	return ((x >> 32) | (x << 32));
}

/*
 *  stress_vm_addr_pwr2()
 *	set data on power of 2 addresses
 */
static size_t TARGET_CLONES stress_vm_addr_pwr2(
	uint8_t *buf,
	const size_t sz)
{
	size_t n, errs = 0;
	uint8_t rnd = mwc8();

	for (n = 1; n < sz; n++) {
		*(buf + n) = rnd;
	}
	for (n = 1; n < sz; n++) {
		if (*(buf + n) != rnd)
			errs++;
	}
	return errs;
}

/*
 *  stress_vm_addr_pwr2inv()
 *	set data on inverted power of 2 addresses
 */
static size_t TARGET_CLONES stress_vm_addr_pwr2inv(
	uint8_t *buf,
	const size_t sz)
{
	size_t mask = sz - 1, n, errs = 0;
	uint8_t rnd = mwc8();

	for (n = 1; n < sz; n++) {
		*(buf + (n ^ mask)) = rnd;
	}
	for (n = 1; n < sz; n++) {
		if (*(buf + (n ^ mask)) != rnd)
			errs++;
	}
	return errs;
}

/*
 *  stress_vm_addr_gray()
 *	set data on gray coded addresses,
 *	each address changes by just 1 bit
 */
static size_t TARGET_CLONES stress_vm_addr_gray(
	uint8_t *buf,
	const size_t sz)
{
	size_t mask = sz - 1, n, errs = 0;
	uint8_t rnd = mwc8();

	for (n = 0; n < sz; n++) {
		size_t gray = ((n >> 1) ^ n) & mask;
		*(buf + gray) = rnd;
	}
	for (n = 0; n < sz; n++) {
		size_t gray = ((n >> 1) ^ n) & mask;
		if (*(buf + gray) != rnd)
			errs++;
	}
	return errs;
}

/*
 *  stress_vm_addr_grayinv()
 *	set data on inverted gray coded addresses,
 *	each address changes by as many bits possible
 */
static size_t TARGET_CLONES stress_vm_addr_grayinv(
	uint8_t *buf,
	const size_t sz)
{
	size_t mask = sz - 1, n, errs = 0;
	uint8_t rnd = mwc8();

	for (n = 0; n < sz; n++) {
		size_t gray = (((n >> 1) ^ n) ^ mask) & mask;
		*(buf + gray) = rnd;
	}
	for (n = 0; n < sz; n++) {
		size_t gray = (((n >> 1) ^ n) ^ mask) & mask;
		if (*(buf + gray) != rnd)
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
static size_t TARGET_CLONES stress_vm_addr_rev(
	uint8_t *buf,
	const size_t sz)
{
	size_t mask = sz - 1, n, errs = 0, shift;
	uint8_t rnd = mwc8();

	for (shift = 0, n = sz; n; shift++, n <<= 1)
		;

	for (n = 0; n < sz; n++) {
		size_t i = reverse64(n << shift) & mask;
		*(buf + i) = rnd;
	}
	for (n = 0; n < sz; n++) {
		size_t i = reverse64(n << shift) & mask;
		if (*(buf + i) != rnd)
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
static size_t TARGET_CLONES stress_vm_addr_revinv(
	uint8_t *buf,
	const size_t sz)
{
	size_t mask = sz - 1, n, errs = 0, shift;
	uint8_t rnd = mwc8();

	for (shift = 0, n = sz; n; shift++, n <<= 1)
		;

	for (n = 0; n < sz; n++) {
		size_t i = (reverse64(n << shift) ^ mask) & mask;
		*(buf + i) = rnd;
	}
	for (n = 0; n < sz; n++) {
		size_t i = (reverse64(n << shift) ^ mask) & mask;
		if (*(buf + i) != rnd)
			errs++;
	}
	return errs;
}

/*
 *  stress_vm_addr_inc()
 *	set data on incrementing addresses
 */
static size_t TARGET_CLONES stress_vm_addr_inc(
	uint8_t *buf,
	const size_t sz)
{
	size_t n, errs = 0;
	uint8_t rnd = mwc8();

	for (n = 0; n < sz; n++) {
		*(buf + n) = rnd;
	}
	for (n = 0; n < sz; n++) {
		if (*(buf + n) != rnd)
			errs++;
	}
	return errs;
}

/*
 *  stress_vm_addr_inc()
 *	set data on inverted incrementing addresses
 */
static size_t TARGET_CLONES stress_vm_addr_incinv(
	uint8_t *buf,
	const size_t sz)
{
	size_t mask = sz - 1, n, errs = 0;
	uint8_t rnd = mwc8();

	for (n = 0; n < sz; n++) {
		size_t i = (n ^ mask) & mask;
		*(buf + i) = rnd;
	}
	for (n = 0; n < sz; n++) {
		size_t i = (n ^ mask) & mask;
		if (*(buf + i) != rnd)
			errs++;
	}
	return errs;
}

/*
 *  stress_vm_addr_dec()
 *	set data on decrementing addresses
 */
static size_t TARGET_CLONES stress_vm_addr_dec(
	uint8_t *buf,
	const size_t sz)
{
	size_t n, errs = 0;
	uint8_t rnd = mwc8();

	for (n = sz; n; n--) {
		*(buf + n - 1) = rnd;
	}
	for (n = sz; n; n--) {
		if (*(buf + n - 1) != rnd)
			errs++;
	}
	return errs;
}

/*
 *  stress_vm_addr_dec()
 *	set data on inverted decrementing addresses
 */
static size_t TARGET_CLONES stress_vm_addr_decinv(
	uint8_t *buf,
	const size_t sz)
{
	size_t mask = sz - 1, n, errs = 0;
	uint8_t rnd = mwc8();

	for (n = sz; n; n--) {
		size_t i = ((n - 1) ^ mask) & mask;
		*(buf + i) = rnd;
	}
	for (n = sz; n; n--) {
		size_t i = ((n - 1) ^ mask) & mask;
		if (*(buf + i) != rnd)
			errs++;
	}
	return errs;
}

/*
 *  stress_vm_addr_all()
 *	work through all vm stressors sequentially
 */
static size_t stress_vm_addr_all(
	uint8_t *buf,
	const size_t sz)
{
	static int i = 1;
	size_t bit_errors = 0;

	bit_errors = vm_addr_methods[i].func(buf, sz);
	i++;
	if (vm_addr_methods[i].func == NULL)
		i = 1;

	return bit_errors;
}

static const stress_vm_addr_method_info_t vm_addr_methods[] = {
	{ "all",	stress_vm_addr_all },
	{ "pwr2",	stress_vm_addr_pwr2 },
	{ "pwr2inv",	stress_vm_addr_pwr2inv },
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
			set_setting("vm-addr-method", TYPE_ID_UINTPTR_T, &info);
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

/*
 *  stress_vm_addr()
 *	stress virtual memory addressing
 */
static int stress_vm_addr(const args_t *args)
{
	uint64_t *bit_error_count = MAP_FAILED;
	uint32_t restarts = 0, nomems = 0;
	uint8_t *buf = NULL;
	pid_t pid;
        const size_t page_size = args->page_size;
	size_t retries;
	int err = 0, ret = EXIT_SUCCESS;
	const stress_vm_addr_method_info_t *vm_addr_method = &vm_addr_methods[0];
	stress_vm_addr_func func;

	(void)get_setting("vm-addr-method", &vm_addr_method);

	func = vm_addr_method->func;
	pr_dbg("%s using method '%s'\n", args->name, vm_addr_method->name);

	for (retries = 0; (retries < 100) && g_keep_stressing_flag; retries++) {
		bit_error_count = (uint64_t *)
			mmap(NULL, page_size, PROT_READ | PROT_WRITE,
				MAP_SHARED | MAP_ANONYMOUS, -1, 0);
		err = errno;
		if (bit_error_count != MAP_FAILED)
			break;
		(void)shim_usleep(100);
	}

	/* Cannot allocate a single page for bit error counter */
	if (bit_error_count == MAP_FAILED) {
		if (g_keep_stressing_flag) {
			pr_err("%s: could not mmap bit error counter: "
				"retry count=%zu, errno=%d (%s)\n",
				args->name, retries, err, strerror(err));
		}
		return EXIT_NO_RESOURCE;
	}

	*bit_error_count = 0ULL;

again:
	if (!g_keep_stressing_flag)
		goto clean_up;
	pid = fork();
	if (pid < 0) {
		if (errno == EAGAIN)
			goto again;
		pr_err("%s: fork failed: errno=%d: (%s)\n",
			args->name, errno, strerror(errno));
	} else if (pid > 0) {
		int status, waitret;

		/* Parent, wait for child */
		(void)setpgid(pid, g_pgrp);
		waitret = shim_waitpid(pid, &status, 0);
		if (waitret < 0) {
			if (errno != EINTR)
				pr_dbg("%s: waitpid(): errno=%d (%s)\n",
					args->name, errno, strerror(errno));
			(void)kill(pid, SIGTERM);
			(void)kill(pid, SIGKILL);
			(void)shim_waitpid(pid, &status, 0);
		} else if (WIFSIGNALED(status)) {
			pr_dbg("%s: child died: %s (instance %d)\n",
				args->name, stress_strsignal(WTERMSIG(status)),
				args->instance);
			/* If we got killed by OOM killer, re-start */
			if (WTERMSIG(status) == SIGKILL) {
				log_system_mem_info();
				pr_dbg("%s: assuming killed by OOM killer, "
					"restarting again (instance %d)\n",
					args->name, args->instance);
				restarts++;
				goto again;
			}
		}
	} else if (pid == 0) {
		int no_mem_retries = 0;
		void *vm_base_addr;
		size_t buf_sz;

		(void)setpgid(0, g_pgrp);
		stress_parent_died_alarm();

		/* Make sure this is killable by OOM killer */
		set_oom_adjustment(args->name, true);

		buf_sz = MIN_VM_ADDR_BYTES;

		do {
			vm_base_addr = (void *)buf_sz;

			if (no_mem_retries >= NO_MEM_RETRIES_MAX) {
				pr_err("%s: gave up trying to mmap, no available memory\n",
					args->name);
				break;
			}

			buf = (uint8_t *)mmap(vm_base_addr, buf_sz,
				PROT_READ | PROT_WRITE,
				MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
			if (buf == MAP_FAILED) {
				buf = NULL;
				no_mem_retries++;
				(void)shim_usleep(100000);
				goto next;	/* Try again */
			}

			no_mem_retries = 0;
			*bit_error_count += func(buf, buf_sz);
			(void)munmap((void *)buf, buf_sz);
next:
			buf_sz <<= 1;
			if (buf_sz > MAX_VM_ADDR_BYTES)
				buf_sz = MIN_VM_ADDR_BYTES;
			inc_counter(args);
		} while (keep_stressing_vm(args));

		_exit(EXIT_SUCCESS);
	}
clean_up:
	if (*bit_error_count > 0) {
		pr_fail("%s: detected %" PRIu64 " bit errors while "
			"stressing memory\n",
			args->name, *bit_error_count);
		ret = EXIT_FAILURE;
	}
	(void)munmap((void *)bit_error_count, page_size);

	if (restarts + nomems > 0)
		pr_dbg("%s: OOM restarts: %" PRIu32
			", out of memory restarts: %" PRIu32 ".\n",
			args->name, restarts, nomems);

	return ret;
}

static const opt_set_func_t opt_set_funcs[] = {
	{ OPT_vm_addr_method,	stress_set_vm_addr_method },
	{ 0,			NULL }
};

stressor_info_t stress_vm_addr_info = {
	.stressor = stress_vm_addr,
	.class = CLASS_VM | CLASS_MEMORY | CLASS_OS,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
