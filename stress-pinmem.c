/*
 * Copyright (C) 2026      Colin Ian King.
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
#include "core-madvise.h"
#include "core-mmap.h"
#include "core-out-of-memory.h"

#if defined(HAVE_SYS_UIO_H)
#include <sys/uio.h>
#endif
#if defined(HAVE_LINUX_IO_URING_H)
#include <linux/io_uring.h>
#endif

#if defined(HAVE_SYS_UIO_H) &&			\
    defined(HAVE_LINUX_IO_URING_H) &&		\
    defined(HAVE_IOVEC) &&			\
    defined(__NR_io_uring_setup) &&		\
    defined(__NR_io_uring_register) &&		\
    defined(RLIMIT_MEMLOCK)
#define USE_IO_URING
#endif

#if defined(MADV_POPULATE_WRITE) &&		\
    defined(RLIMIT_MEMLOCK)
#define USE_MADVISE
#endif

#if defined(HAVE_MLOCK) &&			\
    defined(RLIMIT_MEMLOCK)
#define USE_MLOCK
#endif

#define PINMEM_METHOD_ALL	(0)
#define PINMEM_METHOD_IO_URING	(1)
#define PINMEM_METHOD_MADVISE	(2)
#define PINMEM_METHOD_MLOCK	(3)

typedef struct stress_pinmem_context {
	void *buf;
	struct iovec *iovecs;
	stress_memory_info_t mem_info;
	struct rlimit limit;
	size_t pinmem_bytes;
	size_t pinmem_method;
	size_t nr_iovecs;
	size_t buf_size;
	double mincore_pages_incore;
	double mincore_pages_total;
	uint64_t count;
	int io_uring_fd;
	bool pinmem_cycle;
} stress_pinmem_context_t;

static const stress_help_t help[] = {
	{ NULL,	"pinmem N",	   "start N workers pinning available memory" },
	{ NULL, "pinmem-cycle",	   "cycle memory pinning/unpinning" },
	{ NULL, "pimmem-method M", "memory pinning methid [ all | io-uring | madvise | mlock" },
	{ NULL,	"pinmem-ops N",	"  stop after N memory pinning bogo operations" },
	{ NULL,	NULL,		   NULL }
};

static const char * const stress_pinmem_methods[] = {
	"all",
#if defined(USE_IO_URING)
	"io-uring",
#endif
#if defined(USE_MADVISE)
	"madvise",
#endif
#if defined(USE_MLOCK)
	"mlock",
#endif
};

static const char *stress_pinmem_method(const size_t i)
{
	return (i < SIZEOF_ARRAY(stress_pinmem_methods)) ? stress_pinmem_methods[i] : NULL;
}

static const stress_opt_t opts[] = {
	{ OPT_pinmem_bytes,  "pinmem-bytes",  TYPE_ID_SIZE_T_BYTES_VM, 1 * STRESS_MB, ~0, NULL },
	{ OPT_pinmem_cycle,  "pinmem-cycle",  TYPE_ID_BOOL, 0, 1, NULL },
	{ OPT_pinmem_method, "pinmem-method", TYPE_ID_SIZE_T_METHOD, 0, 0, stress_pinmem_method },
	END_OPT,
};

#if defined(USE_IO_URING)
/*
 *  shim_io_uring_setup
 *	wrapper for io_uring_setup()
 */
static inline int shim_io_uring_setup(unsigned int entries, struct io_uring_params *p)
{
	return (int)syscall(__NR_io_uring_setup, entries, p);
}

/*
 *  shim_io_uring_register
 *	wrapper for io_uring_register()
 */
static inline int shim_io_uring_register(
	int fd,
	unsigned int opcode,
	void *arg,
	unsigned int nr_args)
{
	return (int)syscall(__NR_io_uring_register, fd, opcode, arg, nr_args);
}
#endif

#if defined(USE_IO_URING) ||	\
    defined(USE_MLOCK) || 	\
    defined(USE_MADVISE)
/*
 *  stress_pinmem_mmap()
 *	attempt to mmap and populate memory, adjust down if allocation
 *	fails to biggest mappable size
 */
static void *stress_pinmem_mmap(
	stress_args_t *args,
	size_t *buf_size)
{
	void *buf;
	size_t len = *buf_size;
	size_t prev_len = 0;
	size_t min_len = args->page_size;
	size_t max_len = *buf_size;
	size_t page_mask = ~(args->page_size - 1);

	*buf_size = 0;
	while (stress_continue(args)) {
		buf = stress_mmap_populate(NULL, len, PROT_READ | PROT_WRITE,
				MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		if (buf != MAP_FAILED) {
			/* allocated and target size reached */
			if ((len >= *buf_size) || (len == prev_len)) {
				*buf_size = len;
				return buf;
			}
			(void)munmap(buf, len);
			min_len = len;
		} else {
			max_len = len;
		}
		prev_len = len;
		len = min_len + ((max_len - min_len) * 3 / 4);
		len &= page_mask;
	}
	return MAP_FAILED;
}

/*
 *  stress_pinmem_pin()
 *	pin memory
 */
static int stress_pinmem_pin(stress_args_t *args, stress_pinmem_context_t *context)
{
	int rc = 0;

#if defined(USE_IO_URING)
	if ((context->pinmem_method == PINMEM_METHOD_ALL) ||
	    (context->pinmem_method == PINMEM_METHOD_IO_URING)) {
		if (shim_io_uring_register(context->io_uring_fd, IORING_REGISTER_BUFFERS, context->iovecs, context->nr_iovecs) < 0) {
			pr_inf("%s: io_uring_register buffer register failed, errno=%d %s\n",
				args->name, errno, strerror(errno));
			rc = -1;
		}
	}
#endif
#if defined(USE_MLOCK)
	if ((context->pinmem_method == PINMEM_METHOD_ALL) ||
	    (context->pinmem_method == PINMEM_METHOD_MLOCK)) {
		if (mlock(context->buf, context->buf_size) < 0) {
			pr_inf("%s: mlock failed, errno=%d %s\n",
				args->name, errno, strerror(errno));
			rc = -1;
		}
	}
#endif
#if defined(USE_MADVISE)
	if ((context->pinmem_method == PINMEM_METHOD_ALL) ||
	    (context->pinmem_method == PINMEM_METHOD_MADVISE)) {
		if (madvise(context->buf, context->buf_size, MADV_POPULATE_WRITE) < 0) {
			pr_inf("%s: madvise MADV_POPULATE_WRITE failed, errno=%d %s\n",
				args->name, errno, strerror(errno));
			rc = -1;
		}
	}
#endif
	return rc;
}

/*
 *  stress_pinmem_unpin()
 *	unpin memory
 */
static int stress_pinmem_unpin(stress_args_t *args, stress_pinmem_context_t *context)
{
	int rc = 0;

#if defined(USE_IO_URING)
	if ((context->pinmem_method == PINMEM_METHOD_ALL) ||
	    (context->pinmem_method == PINMEM_METHOD_IO_URING)) {
		if (shim_io_uring_register(context->io_uring_fd, IORING_UNREGISTER_BUFFERS, NULL, 0) < 0) {
			pr_inf("%s: io_uring_register buffer unregister failed, errno=%d %s\n",
				args->name, errno, strerror(errno));
			rc = -1;
		}
	}
#endif
#if defined(USE_MLOCK)
	if ((context->pinmem_method == PINMEM_METHOD_ALL) ||
	    (context->pinmem_method == PINMEM_METHOD_MLOCK)) {
		if (munlock(context->buf, context->buf_size) < 0) {
			pr_inf("%s: munlock failed, errno=%d %s\n",
				args->name, errno, strerror(errno));
			rc = -1;
		}
	}
#endif
#if defined(USE_MADVISE)
	if ((context->pinmem_method == PINMEM_METHOD_ALL) ||
	    (context->pinmem_method == PINMEM_METHOD_MADVISE)) {
		if (madvise(context->buf, context->buf_size, MADV_NORMAL) < 0) {
			pr_inf("%s: madvise MADV_NORMAL failed, errno=%d %s\n",
				args->name, errno, strerror(errno));
			rc = -1;
		}
	}
#endif
	return rc;
}

static stress_memory_info_t mem_info;

static void stress_pinmem_init(const uint32_t instances)
{
	(void)instances;

	(void)stress_memory_info_get(&mem_info);
}

/*
 *  stress_pinmem_mincore()
 *	keep a tally of pages physically mapped
 */
static void OPTIMIZE3 stress_pinmem_mincore(
	stress_pinmem_context_t *context,
	const size_t page_size)
{
	unsigned char vec[65536];
	const size_t vec_pages = sizeof(vec) * page_size;
	uintptr_t ptr = (uintptr_t)context->buf;
	const uintptr_t ptr_end = ptr + context->buf_size;
	register size_t count = 0;

	while (ptr < ptr_end) {
		const size_t residual = (ptr_end - ptr);
		const size_t size = STRESS_MINIMUM(residual, vec_pages);
		int ret;

		ret = shim_mincore(context->buf, size, vec);
		if (LIKELY(ret == 0)) {
			register size_t i;

			for (i = 0; i < size / page_size; i++)
				count += vec[i];
		} else
			return;	/* mincore fail */
		ptr += size;
	}
	context->mincore_pages_incore += (double)count;
	context->mincore_pages_total += (double)context->buf_size / page_size;
}

static int stress_pinmem_child(stress_args_t *args, void *child_context)
{
	stress_pinmem_context_t *context = (stress_pinmem_context_t *)child_context;
	int rc = EXIT_SUCCESS;
	const size_t page_size = args->page_size;
	const bool was_oomed = context->count > 0;

	stress_set_oom_adjustment(args, true);
	context->count++;

	/* child restarted after an OOM? then shrink allocation */
	if (was_oomed)
		context->pinmem_bytes = (context->pinmem_bytes / 64) * 63;

	if (context->limit.rlim_cur < (rlim_t)context->pinmem_bytes) {
		size_t rlim_cur = (size_t)context->limit.rlim_cur >> 10;

		context->limit.rlim_cur = context->pinmem_bytes;
		context->limit.rlim_max = context->pinmem_bytes;
		if (setrlimit(RLIMIT_MEMLOCK, &context->limit) < 0) {
			if (errno == EPERM)
				pr_inf_skip("%s: need to run as root to setrlimit RLIMIT_MEMLOCK more than %zuK, skipping stressor\n",
					args->name, (size_t)rlim_cur);
			else
				pr_inf_skip("%s: failed to setrlimit RLIMIT_MEMLOCK, errno=%d (%s), skipping stressor\n",
					args->name, errno, strerror(errno));
			return EXIT_NO_RESOURCE;
		}
	}

#if defined(USE_IO_URING)
	{
		struct io_uring_params params;

		(void)shim_memset(&params, 0, sizeof(params));
#if defined(IORING_SETUP_COOP_TASKRUN) &&       \
    defined(IORING_SETUP_DEFER_TASKRUN) &&      \
    defined(IORING_SETUP_SINGLE_ISSUER)
		params.flags = IORING_SETUP_COOP_TASKRUN | IORING_SETUP_DEFER_TASKRUN | IORING_SETUP_SINGLE_ISSUER;
#endif
		context->io_uring_fd = shim_io_uring_setup(2, &params);
		if (context->io_uring_fd < 0) {
			pr_inf_skip("%s: failed to setup io_uring, errno=%d (%s), skipping stressor\n",
				args->name, errno, strerror(errno));
			return EXIT_NO_RESOURCE;
		}
	}
#endif
	stress_proc_state_set(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_proc_state_set(args->name, STRESS_STATE_RUN);

	context->buf_size = (context->pinmem_bytes / args->instances) & ~(page_size - 1);
	context->buf_size = STRESS_MAXIMUM(context->buf_size, page_size);
	context->buf = stress_pinmem_mmap(args, &context->buf_size);
	if (context->buf == MAP_FAILED) {
		pr_inf_skip("%s: failed to mmap %zu bytes, skipping stressor\n",
			args->name, context->buf_size);
		return EXIT_NO_RESOURCE;
	}

	/* Touch pages with unique value */
	stress_mmap_set_light(context->buf, context->buf_size, page_size);

#if defined(USE_IO_URING)
	{
		size_t i;
		size_t buf_size;
		size_t offset;

		context->nr_iovecs = (context->buf_size + STRESS_GB - 1) / STRESS_GB;
		if (UNLIKELY(context->nr_iovecs < 1))
			context->nr_iovecs = 1;
		context->iovecs = (struct iovec *)calloc(context->nr_iovecs, sizeof(*context->iovecs));
		if (!context->iovecs) {
			pr_inf_skip("%s: failed to allocate %zu iovecs, skipping stressor\n",
				args->name, context->nr_iovecs);
			rc = EXIT_NO_RESOURCE;
			goto deinit;
		}

		buf_size = context->buf_size / context->nr_iovecs;
		buf_size &= ~(page_size - 1);

		for (i = 0, offset = 0; i < context->nr_iovecs; i++, offset += buf_size) {
			context->iovecs[i].iov_base = ((uint8_t *)context->buf) + offset;
			context->iovecs[i].iov_len = buf_size;
		}
	}
#endif

	if (context->pinmem_cycle) {
		do {
			if (stress_pinmem_pin(args, context) < 0) {
				rc = EXIT_FAILURE;
				break;
			}
			stress_mmap_set_light(context->buf, context->buf_size, page_size);
			stress_pinmem_mincore(context, page_size);
			shim_sched_yield();
			if (stress_pinmem_unpin(args, context) < 0) {
				rc = EXIT_FAILURE;
				break;
			}
			stress_bogo_inc(args);
		} while (stress_continue(args));
	} else {
		if (stress_pinmem_pin(args, context) < 0) {
			rc = EXIT_FAILURE;
			goto deinit;
		}
		do {
			stress_mmap_set_light(context->buf, context->buf_size, page_size);
			stress_pinmem_mincore(context, page_size);
			shim_sched_yield();
			stress_bogo_inc(args);
		} while (stress_continue(args));

		if (stress_pinmem_unpin(args, context) < 0)
			rc = EXIT_FAILURE;
	}

deinit:
	stress_proc_state_set(args->name, STRESS_STATE_DEINIT);
#if defined(USE_IO_URING)
	(void)close(context->io_uring_fd);
	free(context->iovecs);
#endif
	(void)munmap(context->buf, context->buf_size);

	return rc;
}

/*
 *  stress_pinmem()
 *	stress pinning memory
 */
static int stress_pinmem(stress_args_t *args)
{
	stress_pinmem_context_t *context;
	size_t pinmem_bytes;
	int rc;
	double pc;

	if (mem_info.availablemem == 0) {
		pr_inf("%s: failed to determine memory sizes, skipping stressor\n",
			args->name);
		return EXIT_NO_RESOURCE;
	}
	pinmem_bytes = stress_memory_phys_size_get();
	if (pinmem_bytes == 0) {
		pr_inf("%s: failed to determine total physical memory, skipping stressor\n",
			args->name);
		return EXIT_NO_RESOURCE;
	}

	context = stress_mmap_populate(NULL, sizeof(*context),
				PROT_READ | PROT_WRITE,
				MAP_ANONYMOUS | MAP_SHARED, -1, 0);
	if (context == MAP_FAILED) {
		pr_inf("%s: failed to mmap %zu bytes, skipping stressor\n",
			args->name, sizeof(*context));
		return EXIT_NO_RESOURCE;
	}
	shim_memcpy(&context->mem_info, &mem_info, sizeof(context->mem_info));
	context->count = 0;
	context->pinmem_cycle = false;
	context->pinmem_bytes = pinmem_bytes;
	context->pinmem_method = PINMEM_METHOD_IO_URING;
	context->mincore_pages_incore = 0.0;
	context->mincore_pages_total = 0.0;

	(void)stress_setting_get("pinmem-cycle", &context->pinmem_cycle);
	(void)stress_setting_get("pinmem-method", &context->pinmem_method);

	if (!stress_setting_get("pinmem-bytes", &context->pinmem_bytes)) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			context->pinmem_bytes = mem_info.availablemem;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			context->pinmem_bytes = args->page_size;
	} else {
		/* ensure we don't over commit! */
		if (context->pinmem_bytes > mem_info.availablemem) {
			char buf[64];

			context->pinmem_bytes = mem_info.availablemem;
			if (stress_instance_zero(args)) {
				pr_inf("%s: limiting pinned memory size to available memory size of %s\n",
					args->name,
					stress_uint64_to_str(buf, sizeof(buf), (uint64_t)context->pinmem_bytes, 2, false));
			}
		}
	}

	if (getrlimit(RLIMIT_MEMLOCK, &context->limit) < 0) {
		pr_inf_skip("%s: failed to getrlimit RLIMIT_MEMLOCK, errno=%d (%s), skipping stressor\n",
			args->name, errno, strerror(errno));
		rc = EXIT_NO_RESOURCE;
		goto unmap_context;
	}

	if (stress_instance_zero(args)) {
		char str[64];

		pr_inf("%s: using a default of %s pinned memory\n",
			args->name,
			stress_uint64_to_str(str, sizeof(str), (uint64_t)context->pinmem_bytes, 2, true));
	}

	rc = stress_oomable_child(args, (void *)context, stress_pinmem_child, STRESS_OOMABLE_NORMAL);

	pc = (context->mincore_pages_total > 0.0) ?
		(context->mincore_pages_incore * 100.0) / context->mincore_pages_total: 0.0;
	stress_metrics_set(args, "% pages physically mapped", pc, STRESS_METRIC_GEOMETRIC_MEAN);

unmap_context:
	(void)munmap((void *)context, sizeof(*context));
	return rc;
}

static const stress_exercises_t exercises[] = {
	STRESS_EX_FEATURE("oom"),

	STRESS_EX_END,
};

const stressor_info_t stress_pinmem_info = {
	.stressor = stress_pinmem,
	.classifier = CLASS_VM | CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.init = stress_pinmem_init,
	.help = help,
	.opts = opts,
	.exercises = exercises,
};
#else
const stressor_info_t stress_pinmem_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_VM | CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.opts = opts,
	.unimplemented_reason = "built without io_uring, mlock(), madvise() or rlimit RLIMIT_MLOCK"
};
#endif
