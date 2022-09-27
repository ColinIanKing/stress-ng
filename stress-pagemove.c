/*
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

#define DEFAULT_PAGE_MOVE_BYTES		(4 * MB)
#define MIN_PAGE_MOVE_BYTES		(64 * KB)
#define MAX_PAGE_MOVE_BYTES		(MAX_MEM_LIMIT)

static const stress_help_t help[] = {
	{ NULL,	"pagemove N",	  	"start N workers that shuffle move pages" },
	{ NULL,	"pagemove-ops N",	"stop after N page move bogo operations" },
	{ NULL,	"pagemove-bytes N",	"size of mmap'd region to exercise page moving in bytes" },
	{ NULL,	NULL,			NULL }
};

typedef struct {
	void *	virt_addr;		/* original virtual address of page */
	size_t	page_num;		/* original page number relative to start of entire mapping */
} page_info_t;

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
	{ 0,			NULL }
};

#if defined(HAVE_MREMAP) &&	\
    defined(MREMAP_FIXED) &&	\
    defined(MREMAP_MAYMOVE)

static int stress_pagemove_child(const stress_args_t *args, void *context)
{
	size_t sz, pages, pagemove_bytes = DEFAULT_PAGE_MOVE_BYTES;
	int flags = MAP_PRIVATE | MAP_ANONYMOUS;
	const size_t page_size = args->page_size;
	size_t page_num;
	uint8_t *buf, *buf_end, *unmapped_page = NULL, *ptr;
	int rc = EXIT_FAILURE;

	(void)context;

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
		pr_inf("%s: failed to mmap %zu bytes (errno=%d) %s, skipping stressor\n",
			args->name, sz + page_size, errno, strerror(errno));
		return EXIT_NO_RESOURCE;
	}
	buf_end = buf + sz;
	unmapped_page = buf_end;
	(void)munmap((void *)unmapped_page, page_size);

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		if (!keep_stressing(args))
			break;

		(void)madvise((void *)buf, sz, PROT_WRITE);

		for (page_num = 0, ptr = buf; ptr < buf_end; ptr += page_size, page_num++) {
			page_info_t *p = (page_info_t *)ptr;

			p->page_num = page_num;
			p->virt_addr = (void *)ptr;
		}

		(void)madvise((void *)buf, sz, PROT_READ);

		for (page_num = 0, ptr = buf; ptr < buf_end; ptr += page_size, page_num++) {
			page_info_t *p = (page_info_t *)ptr;

			if ((p->page_num != page_num) ||
			    (p->virt_addr != (void *)ptr)) {
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

			remap_addr1 = mremap((void *)ptr, page_size, page_size,
					MREMAP_FIXED | MREMAP_MAYMOVE, unmapped_page);
			if (remap_addr1 == MAP_FAILED) {
				pr_fail("%s: mremap of address %p to %p failed, errno=%d (%s)\n",
					args->name, ptr, unmapped_page, errno, strerror(errno));
				goto fail;
			}
			remap_addr2 = mremap((void *)(ptr + page_size), page_size,
				page_size, MREMAP_FIXED | MREMAP_MAYMOVE, ptr);
			if (remap_addr2 == MAP_FAILED) {
				pr_fail("%s: mremap of address %p to %p failed, errno=%d (%s)\n",
					args->name, ptr + page_size, ptr, errno, strerror(errno));
				goto fail;
			}
			remap_addr3 = mremap((void *)remap_addr1, page_size, page_size,
				MREMAP_FIXED | MREMAP_MAYMOVE, (void *)(ptr + page_size));
			if (remap_addr3 == MAP_FAILED) {
				pr_fail("%s: mremap of address %p to %p failed, errno=%d (%s)\n",
					args->name, remap_addr1, ptr + page_size, errno, strerror(errno));
				goto fail;
			}
		}
		for (page_num = 0, ptr = buf; ptr < buf_end; ptr += page_size, page_num++) {
			page_info_t *p = (page_info_t *)ptr;

			if (((p->page_num - 1) % pages) != page_num)
				pr_fail("%s: page shuffle failed for page %zu, mismatch on contents\n", args->name, page_num);
			if (p->virt_addr == ptr)
				pr_fail("%s: page shuffle failed for page %zu, virtual address didn't change\n", args->name, page_num);
		}
		inc_counter(args);
	} while (keep_stressing(args));

	rc = EXIT_SUCCESS;
fail:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
	(void)munmap(buf, sz);

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
	.stressor = stress_not_implemented,
	.class = CLASS_VM | CLASS_OS,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
#endif
