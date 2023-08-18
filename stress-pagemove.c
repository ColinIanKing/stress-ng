// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2022-2023 Colin Ian King.
 *
 */
#include "stress-ng.h"

#define DEFAULT_PAGE_MOVE_BYTES		(4 * MB)
#define MIN_PAGE_MOVE_BYTES		(64 * KB)
#define MAX_PAGE_MOVE_BYTES		(MAX_MEM_LIMIT)

static const stress_help_t help[] = {
	{ NULL,	"pagemove N",	  	"start N workers that shuffle move pages" },
	{ NULL,	"pagemove-bytes N",	"size of mmap'd region to exercise page moving in bytes" },
	{ NULL, "pagemove-mlock",	"attempt to mlock pages into memory" },
	{ NULL,	"pagemove-ops N",	"stop after N page move bogo operations" },
	{ NULL,	NULL,			NULL }
};

typedef struct {
	void *	virt_addr;		/* original virtual address of page */
	size_t	page_num;		/* original page number relative to start of entire mapping */
} page_info_t;

static int stress_set_pagemove_mlock(const char *opt)
{
	return stress_set_setting_true("pagemove-mlock", opt);
}

static int stress_set_pagemove_bytes(const char *opt)
{
	size_t pagemove_bytes;

	pagemove_bytes = (size_t)stress_get_uint64_byte_memory(opt, 1);
	stress_check_range_bytes("pagemove-bytes", pagemove_bytes,
		MIN_PAGE_MOVE_BYTES, MAX_PAGE_MOVE_BYTES);
	return stress_set_setting("pagemove-bytes", TYPE_ID_SIZE_T, &pagemove_bytes);
}

static const stress_opt_set_func_t opt_set_funcs[] = {
	{ OPT_pagemove_bytes,	stress_set_pagemove_bytes },
	{ OPT_pagemove_mlock,	stress_set_pagemove_mlock },
	{ 0,			NULL }
};

#if defined(HAVE_MREMAP) &&	\
    defined(MREMAP_FIXED) &&	\
    defined(MREMAP_MAYMOVE)

/*
 *  stress_pagemove_remap_fail()
 *	report remap failure message
 */
static void stress_pagemove_remap_fail(
	const stress_args_t *args,
	const uint8_t *from,
	const uint8_t *to)
{
	pr_fail("%s: mremap of address %p to %p failed, errno=%d (%s)\n",
		args->name, from, to, errno, strerror(errno));
}

static int stress_pagemove_child(const stress_args_t *args, void *context)
{
	size_t sz, pages, pagemove_bytes = DEFAULT_PAGE_MOVE_BYTES;
	int flags = MAP_PRIVATE | MAP_ANONYMOUS;
	const size_t page_size = args->page_size;
	size_t page_num;
	uint8_t *buf, *buf_end, *unmapped_page = NULL, *ptr;
	int rc = EXIT_FAILURE;
	double duration = 0.0, count = 0.0, rate;
	int metrics_count = 0;
	bool pagemove_mlock = false;

	(void)context;

	(void)stress_get_setting("pagemove-mlock", &pagemove_mlock);

	if (!stress_get_setting("pagemove-bytes", &pagemove_bytes)) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			pagemove_bytes = MAX_32;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			pagemove_bytes = MIN_PAGE_MOVE_BYTES;
	}
	pagemove_bytes /= args->num_instances;
	if (pagemove_bytes < MIN_PAGE_MOVE_BYTES)
		pagemove_bytes = MIN_PAGE_MOVE_BYTES;
	if (pagemove_bytes < page_size)
		pagemove_bytes = page_size;

	sz = pagemove_bytes & ~(page_size - 1);
	pages = sz / page_size;

	buf = (uint8_t *)mmap(NULL, sz + page_size, PROT_READ | PROT_WRITE, flags, -1, 0);
	if (buf == MAP_FAILED) {
		pr_inf_skip("%s: failed to mmap %zu bytes (errno=%d) %s, skipping stressor\n",
			args->name, sz + page_size, errno, strerror(errno));
		return EXIT_NO_RESOURCE;
	}
	if (pagemove_mlock)
		(void)shim_mlock(buf, sz + page_size);
	buf_end = buf + sz;
	unmapped_page = buf_end;
	(void)munmap((void *)unmapped_page, page_size);

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		if (!stress_continue(args))
			break;

		(void)madvise((void *)buf, sz, PROT_WRITE);

		for (page_num = 0, ptr = buf; ptr < buf_end; ptr += page_size, page_num++) {
			page_info_t *p = (page_info_t *)ptr;

			p->page_num = page_num;
			p->virt_addr = (void *)ptr;
		}

		(void)madvise((void *)buf, sz, PROT_READ);

		for (page_num = 0, ptr = buf; ptr < buf_end; ptr += page_size, page_num++) {
			register const page_info_t *p = (page_info_t *)ptr;

			if (UNLIKELY((p->page_num != page_num) ||
				     (p->virt_addr != (void *)ptr))) {
				pr_fail("%s: mmap'd region of %zu "
					"bytes does not contain expected data at page %zu\n",
					args->name, sz, page_num);
				goto fail;
			}
		}

		/*
		 * Shuffle pages down by 1 page using page moves:
		 *    tmp = buf;
		 *    buf = buf + page_size;
		 *    buf + page_size = tmp
		 */
		for (ptr = buf; ptr < buf_end - page_size; ptr += page_size) {
			void *remap_addr1, *remap_addr2, *remap_addr3;

			if (LIKELY(metrics_count > 0)) {
				/* faster non-metrics mremaps */
				remap_addr1 = mremap((void *)ptr, page_size, page_size,
						MREMAP_FIXED | MREMAP_MAYMOVE, unmapped_page);
				if (UNLIKELY(remap_addr1 == MAP_FAILED)) {
					stress_pagemove_remap_fail(args, ptr, unmapped_page);
					goto fail;
				}
				if (pagemove_mlock)
					(void)shim_mlock(remap_addr1, page_size);

				remap_addr2 = mremap((void *)(ptr + page_size), page_size,
					page_size, MREMAP_FIXED | MREMAP_MAYMOVE, ptr);
				if (UNLIKELY(remap_addr2 == MAP_FAILED)) {
					stress_pagemove_remap_fail(args, ptr + page_size, ptr);
					goto fail;
				}
				if (pagemove_mlock)
					(void)shim_mlock(remap_addr2, page_size);

				remap_addr3 = mremap((void *)remap_addr1, page_size, page_size,
					MREMAP_FIXED | MREMAP_MAYMOVE, (void *)(ptr + page_size));
				if (UNLIKELY(remap_addr3 == MAP_FAILED)) {
					stress_pagemove_remap_fail(args, remap_addr1, ptr + page_size);
					goto fail;
				}
				if (pagemove_mlock)
					(void)shim_mlock(remap_addr3, page_size);
			} else {
				/* slower metrics mremaps */
				double t1, t2;

				t1 = stress_time_now();
				remap_addr1 = mremap((void *)ptr, page_size, page_size,
						MREMAP_FIXED | MREMAP_MAYMOVE, unmapped_page);
				t2 = stress_time_now();
				duration += (t2 - t1);
				count += 1.0;
				if (UNLIKELY(remap_addr1 == MAP_FAILED)) {
					stress_pagemove_remap_fail(args, ptr, unmapped_page);
					goto fail;
				}
				if (pagemove_mlock)
					(void)shim_mlock(remap_addr1, page_size);

				t1 = stress_time_now();
				remap_addr2 = mremap((void *)(ptr + page_size), page_size,
					page_size, MREMAP_FIXED | MREMAP_MAYMOVE, ptr);
				t2 = stress_time_now();
				duration += (t2 - t1);
				count += 1.0;
				if (UNLIKELY(remap_addr2 == MAP_FAILED)) {
					stress_pagemove_remap_fail(args, ptr + page_size, ptr);
					goto fail;
				}
				if (pagemove_mlock)
					(void)shim_mlock(remap_addr2, page_size);

				t1 = stress_time_now();
				remap_addr3 = mremap((void *)remap_addr1, page_size, page_size,
					MREMAP_FIXED | MREMAP_MAYMOVE, (void *)(ptr + page_size));
				t2 = stress_time_now();
				duration += (t2 - t1);
				count += 1.0;
				if (UNLIKELY(remap_addr3 == MAP_FAILED)) {
					stress_pagemove_remap_fail(args, remap_addr1, ptr + page_size);
					goto fail;
				}
				if (pagemove_mlock)
					(void)shim_mlock(remap_addr3, page_size);
			}
			if (metrics_count++ > 1000)
				metrics_count = 0;
		}
		for (page_num = 0, ptr = buf; ptr < buf_end; ptr += page_size, page_num++) {
			register const page_info_t *p = (page_info_t *)ptr;

			if (UNLIKELY(((p->page_num + pages - 1) % pages) != page_num))
				pr_fail("%s: page shuffle failed for page %zu, mismatch on contents\n", args->name, page_num);
			if (UNLIKELY(p->virt_addr == ptr))
				pr_fail("%s: page shuffle failed for page %zu, virtual address didn't change\n", args->name, page_num);
		}
		stress_bogo_inc(args);
	} while (stress_continue(args));

	rc = EXIT_SUCCESS;
fail:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
	(void)munmap(buf, sz);

	rate = (duration > 0.0) ? count / duration : 0.0;
	stress_metrics_set(args, 0, "page remaps per sec", rate);

	return rc;
}

/*
 *  stress_pagemove()
 *	stress mmap
 */
static int stress_pagemove(const stress_args_t *args)
{
	return stress_oomable_child(args, NULL, stress_pagemove_child, STRESS_OOMABLE_NORMAL);
}

stressor_info_t stress_pagemove_info = {
	.stressor = stress_pagemove,
	.class = CLASS_VM | CLASS_OS,
	.opt_set_funcs = opt_set_funcs,
	.verify = VERIFY_ALWAYS,
	.help = help
};
#else
stressor_info_t stress_pagemove_info = {
	.stressor = stress_unimplemented,
	.class = CLASS_VM | CLASS_OS,
	.opt_set_funcs = opt_set_funcs,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.unimplemented_reason = "built without mremap() or MREMAP_FIXED/MREMAP_MAYMOVE defined"
};
#endif
