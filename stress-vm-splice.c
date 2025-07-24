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
#include "core-builtin.h"
#include "core-prime.h"
#include "core-mmap.h"

#define MIN_VM_SPLICE_BYTES	(4 * KB)
#define MAX_VM_SPLICE_BYTES	(64 * MB)
#define DEFAULT_VM_SPLICE_BYTES	(64 * KB)

static const stress_help_t help[] = {
	{ NULL,	"vm-splice N",		"start N workers reading/writing using vmsplice" },
	{ NULL,	"vm-splice-bytes N",	"number of bytes to transfer per vmsplice call" },
	{ NULL,	"vm-splice-ops N",	"stop after N bogo splice operations" },
	{ NULL,	NULL,			NULL }
};

static const stress_opt_t opts[] = {
	{ OPT_vm_splice_bytes, "vm-splice-bytes", TYPE_ID_SIZE_T_BYTES_VM, MIN_VM_SPLICE_BYTES, MAX_MEM_LIMIT, NULL },
	END_OPT,
};

#if defined(HAVE_VMSPLICE) &&	\
    defined(SPLICE_F_MOVE)

/*
 *  stress_splice
 *	stress copying of /dev/zero to /dev/null
 */
static int stress_vm_splice(stress_args_t *args)
{
	int fd, fds[2], rc = EXIT_SUCCESS;
	uint8_t *buf;
	const size_t page_size = args->page_size;
	size_t sz, vm_splice_bytes, vm_splice_bytes_total = DEFAULT_VM_SPLICE_BYTES;
	char *data;
	double duration = 0.0, bytes = 0.0, vm_splices = 0.0, rate;
	int metrics_counter = 0;
	uint64_t checkval = stress_mwc64();
	uint64_t prime;

	if (!stress_get_setting("vm-splice-bytes", &vm_splice_bytes_total)) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			vm_splice_bytes_total = MAX_VM_SPLICE_BYTES;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			vm_splice_bytes_total = MIN_VM_SPLICE_BYTES;
	}
	vm_splice_bytes = vm_splice_bytes_total / args->instances;
	if (vm_splice_bytes < MIN_VM_SPLICE_BYTES)
		vm_splice_bytes = MIN_VM_SPLICE_BYTES;
	if (vm_splice_bytes < page_size)
		vm_splice_bytes = page_size;
	if (stress_instance_zero(args))
		stress_usage_bytes(args, vm_splice_bytes, vm_splice_bytes * args->instances);
	sz = vm_splice_bytes & ~(page_size - 1);

	buf = stress_mmap_populate(NULL, sz, PROT_READ | PROT_WRITE,
		MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	if (buf == MAP_FAILED) {
		pr_inf_skip("%s: failed to mmap %zu bytes buffer%s, errno=%d (%s), skipping stressor\n",
			args->name, sz,
			stress_get_memfree_str(), errno, strerror(errno));
		return EXIT_NO_RESOURCE;
	}
	stress_set_vma_anon_name(buf, sz, "iov-buffer");
	data = stress_mmap_populate(NULL, page_size, PROT_READ | PROT_WRITE,
		MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (data == MAP_FAILED) {
		pr_inf_skip("%s: failed to mmap %zu sized buffer%s, errno=%d (%s), skipping stressor\n",
			args->name, page_size,
			stress_get_memfree_str(), errno, strerror(errno));
		(void)munmap((void *)buf, sz);
		return EXIT_NO_RESOURCE;
	}
	stress_set_vma_anon_name(data, page_size, "io-buffer");

	if (pipe(fds) < 0) {
		(void)munmap((void *)data, page_size);
		(void)munmap((void *)buf, sz);
		pr_fail("%s: pipe failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_FAILURE;
	}

	fd = open("/dev/null", O_WRONLY);
	if (fd < 0) {
		pr_fail("%s: open /dev/null failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		(void)munmap((void *)data, page_size);
		(void)munmap((void *)buf, sz);
		(void)close(fds[0]);
		(void)close(fds[1]);
		return EXIT_FAILURE;
	}

	stress_rndbuf(data, page_size);
	prime = stress_get_prime64(vm_splice_bytes);

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	(void)shim_memset(buf, 0, sz);
	do {
		ssize_t ret, n_bytes;
		struct iovec iov ALIGN64;
		double t;

		/*
		 *  vmsplice from memory to pipe
		 */
		iov.iov_base = buf;
		iov.iov_len = sz;

		if (LIKELY(metrics_counter != 0)) {
			n_bytes = vmsplice(fds[1], &iov, 1, 0);
			if (UNLIKELY(n_bytes < 0))
				break;
		} else {
			t = stress_time_now();
			n_bytes = vmsplice(fds[1], &iov, 1, 0);
			if (UNLIKELY(n_bytes < 0))
				break;
			duration += stress_time_now() - t;
		}
		bytes += (double)n_bytes;
		vm_splices += 1.0;

		ret = splice(fds[0], NULL, fd, NULL,
			vm_splice_bytes, SPLICE_F_MOVE);
		if (UNLIKELY(ret < 0))
			break;

		/*
		 *  vmsplice from pipe to memory
		 */
		checkval += prime;
		*(uint64_t *)data = checkval;
		n_bytes = write(fds[1], data, page_size);
		if (LIKELY(n_bytes > 0)) {
			iov.iov_base = buf;
			iov.iov_len = (size_t)n_bytes;

			if (LIKELY(metrics_counter != 0)) {
				n_bytes = vmsplice(fds[0], &iov, 1, 0);
				if (UNLIKELY(n_bytes < 0))
					break;
			} else {
				t = stress_time_now();
				n_bytes = vmsplice(fds[0], &iov, 1, 0);
				if (UNLIKELY(n_bytes < 0))
					break;
				duration += stress_time_now() - t;
			}
			/* Sanity check the data */
			if (LIKELY(n_bytes > (ssize_t)sizeof(checkval)) &&
			    UNLIKELY(checkval != *(uint64_t *)buf)) {
				pr_fail("%s: data check pattern failed, got %" PRIx64 ", expected %" PRIx64 "\n",
					args->name, *(uint64_t *)buf, checkval);
				rc = EXIT_FAILURE;
			}
			bytes += (double)n_bytes;
			vm_splices += 1.0;
		}
		if (UNLIKELY(metrics_counter++ > 1000))
			metrics_counter = 0;

		stress_bogo_inc(args);
	} while (stress_continue(args));

	rate = (duration > 0.0) ? bytes / duration : 0.0;
	stress_metrics_set(args, 0, "MB per sec vm-splice rate",
		rate / (double)MB, STRESS_METRIC_HARMONIC_MEAN);
	rate = (duration > 0.0) ? vm_splices / duration : 0.0;
	stress_metrics_set(args, 1, "vm-splice calls per sec",
		rate, STRESS_METRIC_HARMONIC_MEAN);

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	(void)munmap((void *)data, page_size);
	(void)munmap((void *)buf, sz);
	(void)close(fd);
	(void)close(fds[0]);
	(void)close(fds[1]);

	return rc;
}

const stressor_info_t stress_vm_splice_info = {
	.stressor = stress_vm_splice,
	.classifier = CLASS_VM | CLASS_PIPE_IO | CLASS_OS,
	.opts = opts,
	.verify = VERIFY_ALWAYS,
	.help = help
};
#else
const stressor_info_t stress_vm_splice_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_VM | CLASS_PIPE_IO | CLASS_OS,
	.opts = opts,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.unimplemented_reason = "built without vmsplice() or undefined SPLICE_F_MOVE"
};
#endif
