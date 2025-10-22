/*
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
#include "core-mmap.h"
#include "core-numa.h"
#include "core-out-of-memory.h"

#define MIN_PAGES			(3)	/* Min number to move */
#define DEFAULT_PAGE_MOVE_BYTES		(4 * MB)
#define MIN_PAGE_MOVE_BYTES		(64 * KB)
#define MAX_PAGE_MOVE_BYTES		(MAX_MEM_LIMIT)

static const stress_help_t help[] = {
	{ NULL,	"pagemove N",	  	"start N workers that shuffle move pages" },
	{ NULL,	"pagemove-bytes N",	"size of mmap'd region to exercise page moving in bytes" },
	{ NULL, "pagemove-mlock",	"attempt to mlock pages into memory" },
	{ NULL, "pagemove-numa",	"bind memory mappings to randomly selected NUMA nodes" },
	{ NULL,	"pagemove-ops N",	"stop after N page move bogo operations" },
	{ NULL,	NULL,			NULL }
};

typedef struct {
	void *	virt_addr;		/* original virtual address of page */
	size_t	page_num;		/* original page number relative to start of entire mapping */
} page_info_t;

static const stress_opt_t opts[] = {
	{ OPT_pagemove_bytes, "pagemove-bytes", TYPE_ID_SIZE_T_BYTES_VM, MIN_PAGE_MOVE_BYTES, MAX_PAGE_MOVE_BYTES, NULL },
	{ OPT_pagemove_mlock, "pagemove-mlock", TYPE_ID_BOOL, 0, 1, NULL },
	{ OPT_pagemove_numa,  "pagemove-numa",  TYPE_ID_BOOL, 0, 1, NULL },
	END_OPT,
};

#if defined(HAVE_MREMAP) &&	\
    defined(MREMAP_FIXED) &&	\
    defined(MREMAP_MAYMOVE)

/*
 *  pagemove parsed args
 */
typedef struct {
	bool pagemove_mlock;	/* mlock option */
	bool pagemove_numa;	/* numa option */
	size_t sz;		/* size of buffer, bytes */
	size_t pages;		/* size of buffer, pages */
} stress_pagemove_info_t;


/*
 *  stress_pagemove_remap_fail()
 *	report remap failure message
 */
static void stress_pagemove_remap_fail(
	stress_args_t *args,
	const uint8_t *from,
	const uint8_t *to)
{
	pr_fail("%s: mremap of address %p to %p failed, errno=%d (%s)\n",
		args->name, from, to, errno, strerror(errno));
}

static int stress_pagemove_child(stress_args_t *args, void *context)
{
	const size_t page_size = args->page_size;
	size_t page_num;
	uint8_t *buf, *buf_end, *unmapped_page = NULL, *ptr;
	int rc = EXIT_FAILURE;
	double duration = 0.0, count = 0.0, rate;
	int metrics_count = 0;
	stress_pagemove_info_t *info = (stress_pagemove_info_t *)context;
#if defined(HAVE_LINUX_MEMPOLICY_H)
       stress_numa_mask_t *numa_mask = NULL;
       stress_numa_mask_t *numa_nodes = NULL;
#endif

	buf = (uint8_t *)stress_mmap_populate(NULL, info->sz + page_size,
		PROT_READ | PROT_WRITE,
		MAP_PRIVATE | MAP_ANONYMOUS,
		-1, 0);
	if (buf == MAP_FAILED) {
		pr_inf_skip("%s: failed to mmap %zu bytes%s, errno=%d (%s), skipping stressor\n",
			args->name, info->sz + page_size, stress_get_memfree_str(),
			errno, strerror(errno));
		return EXIT_NO_RESOURCE;
	}
	if (info->pagemove_mlock)
		(void)shim_mlock(buf, info->sz + page_size);
	buf_end = buf + info->sz;
	unmapped_page = buf_end;
	(void)munmap((void *)unmapped_page, page_size);

	if (info->pagemove_numa) {
#if defined(HAVE_LINUX_MEMPOLICY_H)
		stress_numa_mask_and_node_alloc(args, &numa_nodes, &numa_mask,
						"--pagemove-numa", &info->pagemove_numa);
#else
		if (stress_instance_zero(args))
			pr_inf("%s: --pagemove-numa selected but not supported by this system, disabling option\n",
				args->name);
		info->pagemove_numa = false;
#endif
	}

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		if (UNLIKELY(!stress_continue(args)))
			break;

		(void)mprotect((void *)buf, info->sz, PROT_WRITE);

		for (page_num = 0, ptr = buf; ptr < buf_end; ptr += page_size, page_num++) {
			page_info_t *p = (page_info_t *)ptr;

			p->page_num = page_num;
			p->virt_addr = (void *)ptr;
		}

		(void)mprotect((void *)buf, info->sz, PROT_READ);

		for (page_num = 0, ptr = buf; ptr < buf_end; ptr += page_size, page_num++) {
			register const page_info_t *p = (page_info_t *)ptr;

			if (UNLIKELY((p->page_num != page_num) ||
				     (p->virt_addr != (void *)ptr))) {
				pr_fail("%s: mmap'd region of %zu "
					"bytes does not contain expected data at page %zu\n",
					args->name, info->sz, page_num);
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
#if defined(HAVE_LINUX_MEMPOLICY_H)
				if (info->pagemove_numa)
					stress_numa_randomize_pages(args, numa_nodes, numa_mask, remap_addr1, page_size, page_size);
#endif
				if (info->pagemove_mlock)
					(void)shim_mlock(remap_addr1, page_size);

				remap_addr2 = mremap((void *)(ptr + page_size), page_size,
					page_size, MREMAP_FIXED | MREMAP_MAYMOVE, ptr);
				if (UNLIKELY(remap_addr2 == MAP_FAILED)) {
					stress_pagemove_remap_fail(args, ptr + page_size, ptr);
					goto fail;
				}
#if defined(HAVE_LINUX_MEMPOLICY_H)
				if (info->pagemove_numa)
					stress_numa_randomize_pages(args, numa_nodes, numa_mask, remap_addr2, page_size, page_size);
#endif
				if (info->pagemove_mlock)
					(void)shim_mlock(remap_addr2, page_size);

				remap_addr3 = mremap((void *)remap_addr1, page_size, page_size,
					MREMAP_FIXED | MREMAP_MAYMOVE, (void *)(ptr + page_size));
				if (UNLIKELY(remap_addr3 == MAP_FAILED)) {
					stress_pagemove_remap_fail(args, remap_addr1, ptr + page_size);
					goto fail;
				}
#if defined(HAVE_LINUX_MEMPOLICY_H)
				if (info->pagemove_numa)
					stress_numa_randomize_pages(args, numa_nodes, numa_mask, remap_addr3, page_size, page_size);
#endif
				if (info->pagemove_mlock)
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
#if defined(HAVE_LINUX_MEMPOLICY_H)
				if (info->pagemove_numa)
					stress_numa_randomize_pages(args, numa_nodes, numa_mask, remap_addr1, page_size, page_size);
#endif
				if (info->pagemove_mlock)
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
#if defined(HAVE_LINUX_MEMPOLICY_H)
				if (info->pagemove_numa)
					stress_numa_randomize_pages(args, numa_nodes, numa_mask, remap_addr2, page_size, page_size);
#endif
				if (info->pagemove_mlock)
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
#if defined(HAVE_LINUX_MEMPOLICY_H)
				if (info->pagemove_numa)
					stress_numa_randomize_pages(args, numa_nodes, numa_mask, remap_addr3, page_size, page_size);
#endif
				if (info->pagemove_mlock)
					(void)shim_mlock(remap_addr3, page_size);
			}
			if (metrics_count++ > 1000)
				metrics_count = 0;
		}
		for (page_num = 0, ptr = buf; ptr < buf_end; ptr += page_size, page_num++) {
			register const page_info_t *p = (page_info_t *)ptr;
			register const size_t expected = (page_num + 1) % info->pages;

			if (UNLIKELY(expected != p->page_num))
				pr_fail("%s: page shuffle failed for page %zu, mismatch on contents, %zu vs %zu\n", args->name, page_num, expected, p->page_num);
			if (UNLIKELY(p->virt_addr == ptr))
				pr_fail("%s: page shuffle failed for page %zu, virtual address didn't change\n", args->name, page_num);
		}
		stress_bogo_inc(args);
	} while (stress_continue(args));

	rc = EXIT_SUCCESS;
fail:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
	(void)munmap((void *)buf, info->sz);
#if defined(HAVE_LINUX_MEMPOLICY_H)
	if (numa_mask)
		stress_numa_mask_free(numa_mask);
	if (numa_nodes)
		stress_numa_mask_free(numa_nodes);
#endif

	rate = (duration > 0.0) ? count / duration : 0.0;
	stress_metrics_set(args, 0, "page remaps per sec",
		rate, STRESS_METRIC_HARMONIC_MEAN);

	return rc;
}

/*
 *  stress_pagemove()
 *	stress mmap
 */
static int stress_pagemove(stress_args_t *args)
{
	size_t pagemove_bytes_total = DEFAULT_PAGE_MOVE_BYTES;
	size_t pagemove_bytes;
	const size_t page_size = args->page_size;
	stress_pagemove_info_t info;
	bool adjusted_min = false;
	bool adjusted_max = false;

	(void)shim_memset(&info, 0, sizeof(info));

	info.pagemove_mlock = false;
	info.pagemove_numa = false;
	if (!stress_get_setting("pagemove-mlock", &info.pagemove_mlock)) {
		if (g_opt_flags & OPT_FLAGS_AGGRESSIVE)
			info.pagemove_mlock = true;
	}
	if (!stress_get_setting("pagemove-numa", &info.pagemove_numa)) {
		if (g_opt_flags & OPT_FLAGS_AGGRESSIVE)
			info.pagemove_numa = true;
	}
	if (!stress_get_setting("pagemove-bytes", &pagemove_bytes_total)) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			pagemove_bytes_total = MAX_32;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			pagemove_bytes_total = MIN_PAGE_MOVE_BYTES;
	}
	pagemove_bytes = pagemove_bytes_total / args->instances;
	if (pagemove_bytes < MIN_PAGE_MOVE_BYTES)
		pagemove_bytes = MIN_PAGE_MOVE_BYTES;
	if (pagemove_bytes < page_size)
		pagemove_bytes = page_size;

	info.sz = pagemove_bytes & ~(page_size - 1);
	if (info.sz > (MAX_32 - page_size)) {
		pagemove_bytes = (MAX_32 - page_size) & ~(page_size - 1);
		info.sz = pagemove_bytes;
		adjusted_max = true;
	}
	info.pages = info.sz / page_size;
	/* need a few pages to move! */
	if (info.pages < MIN_PAGES) {
		pagemove_bytes = page_size * MIN_PAGES;
		info.sz = pagemove_bytes;
		info.pages = info.sz / page_size;
		adjusted_min = true;
	}
	pagemove_bytes_total = pagemove_bytes * args->instances;

	if (stress_instance_zero(args)) {
		if (adjusted_min || adjusted_max) {
			char buf[32];

			stress_uint64_to_str(buf, sizeof(buf), (uint64_t)pagemove_bytes, 2, true);
			pr_inf("%s: adjusted pagemove-bytes to a per stressor instance %s of %s (%zu x %zuK pages)\n",
				args->name,
				adjusted_min ? "minimum" : "maximum",
				buf,
				info.pages, page_size >> 10);
		}
		stress_usage_bytes(args, pagemove_bytes, pagemove_bytes_total);
	}

	return stress_oomable_child(args, &info, stress_pagemove_child, STRESS_OOMABLE_NORMAL);
}

const stressor_info_t stress_pagemove_info = {
	.stressor = stress_pagemove,
	.classifier = CLASS_VM | CLASS_OS,
	.opts = opts,
	.verify = VERIFY_ALWAYS,
	.help = help
};
#else
const stressor_info_t stress_pagemove_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_VM | CLASS_OS,
	.opts = opts,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.unimplemented_reason = "built without mremap() or MREMAP_FIXED/MREMAP_MAYMOVE defined"
};
#endif
