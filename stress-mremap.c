/*
 * Copyright (C) 2013-2020 Canonical, Ltd.
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

static const stress_help_t help[] = {
	{ NULL,	"mremap N",	  "start N workers stressing mremap" },
	{ NULL,	"mremap-ops N",	  "stop after N mremap bogo operations" },
	{ NULL,	"mremap-bytes N", "mremap N bytes maximum for each stress iteration" },
	{ NULL,	NULL,		  NULL }
};

static int stress_set_mremap_bytes(const char *opt)
{
	size_t mremap_bytes;

	mremap_bytes = (size_t)stress_get_uint64_byte_memory(opt, 1);
	stress_check_range_bytes("mremap-bytes", mremap_bytes,
		MIN_MREMAP_BYTES, MAX_MEM_LIMIT);
	return stress_set_setting("mremap-bytes", TYPE_ID_SIZE_T, &mremap_bytes);
}

static const stress_opt_set_func_t opt_set_funcs[] = {
	{ OPT_mremap_bytes,	stress_set_mremap_bytes },
	{ 0,			NULL }
};

#if defined(HAVE_MREMAP) && NEED_GLIBC(2,4,0)

#if defined(MREMAP_FIXED)
/*
 *  rand_mremap_addr()
 *	try and find a random unmapped region of memory
 */
static inline void *rand_mremap_addr(const size_t sz, int flags)
{
	void *addr;

	flags &= ~(MREMAP_FIXED | MAP_SHARED | MAP_POPULATE);
	flags |= (MAP_PRIVATE | MAP_ANONYMOUS);

	addr = mmap(NULL, sz, PROT_READ | PROT_WRITE, flags, -1, 0);
	if (addr == MAP_FAILED)
		return NULL;

	(void)munmap(addr, sz);

	/*
	 * At this point, we know that we can remap to this addr
	 * in this process if we don't do any memory mappings between
	 * the munmap above and the remapping
	 */
	return addr;
}
#endif

/*
 *  try_remap()
 *	try and remap old size to new size
 */
static int try_remap(
	const stress_args_t *args,
	uint8_t **buf,
	const size_t old_sz,
	const size_t new_sz)
{
	uint8_t *newbuf;
	int retry, flags = 0;
#if defined(MREMAP_MAYMOVE)
	const int maymove = MREMAP_MAYMOVE;
#else
	const int maymove = 0;
#endif

#if defined(MREMAP_FIXED) && defined(MREMAP_MAYMOVE)
	flags = maymove | (stress_mwc32() & MREMAP_FIXED);
#else
	flags = maymove;
#endif

	for (retry = 0; retry < 100; retry++) {
#if defined(MREMAP_FIXED)
		void *addr = rand_mremap_addr(new_sz, flags);
#endif
		if (!keep_stressing_flag())
			return 0;
#if defined(MREMAP_FIXED)
		if (addr) {
			newbuf = mremap(*buf, old_sz, new_sz, flags, addr);
		} else {
			newbuf = mremap(*buf, old_sz, new_sz, flags & ~MREMAP_FIXED);
		}
#else
		newbuf = mremap(*buf, old_sz, new_sz, flags);
#endif
		if (newbuf != MAP_FAILED) {
			*buf = newbuf;
			return 0;
		}

		switch (errno) {
		case ENOMEM:
		case EAGAIN:
			continue;
		case EINVAL:
#if defined(MREMAP_FIXED)
			/*
			 * Earlier kernels may not support this or we
			 * chose a bad random address, so just fall
			 * back to non fixed remapping
			 */
			if (flags & MREMAP_FIXED) {
				flags &= ~MREMAP_FIXED;
				continue;
			}
#endif
			break;
		case EFAULT:
		default:
			break;
		}
	}
	pr_fail_err("mremap");
	return -1;
}

static int stress_mremap_child(const stress_args_t *args, void *context)
{
	size_t new_sz, sz, mremap_bytes = DEFAULT_MREMAP_BYTES;
	int flags = MAP_PRIVATE | MAP_ANONYMOUS;
	const size_t page_size = args->page_size;

#if defined(MAP_POPULATE)
	flags |= MAP_POPULATE;
#endif
	(void)context;

	if (!stress_get_setting("mremap-bytes", &mremap_bytes)) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			mremap_bytes = MAX_32;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			mremap_bytes = MIN_MREMAP_BYTES;
	}
	mremap_bytes /= args->num_instances;
	if (mremap_bytes < MIN_MREMAP_BYTES)
		mremap_bytes = MIN_MREMAP_BYTES;
	if (mremap_bytes < page_size)
		mremap_bytes = page_size;
	new_sz = sz = mremap_bytes & ~(page_size - 1);

	do {
		uint8_t *buf = NULL;
		size_t old_sz;

		if (!keep_stressing_flag())
			break;

		buf = mmap(NULL, new_sz, PROT_READ | PROT_WRITE, flags, -1, 0);
		if (buf == MAP_FAILED) {
			/* Force MAP_POPULATE off, just in case */
#if defined(MAP_POPULATE)
			flags &= ~MAP_POPULATE;
#endif
			continue;	/* Try again */
		}
		(void)stress_madvise_random(buf, new_sz);
		(void)stress_mincore_touch_pages(buf, mremap_bytes);

		/* Ensure we can write to the mapped pages */
		if (g_opt_flags & OPT_FLAGS_VERIFY) {
			stress_mmap_set(buf, new_sz, page_size);
			if (stress_mmap_check(buf, sz, page_size) < 0) {
				pr_fail("%s: mmap'd region of %zu "
					"bytes does not contain expected data\n",
					args->name, sz);
				(void)munmap(buf, new_sz);
				return EXIT_FAILURE;
			}
		}

		old_sz = new_sz;
		new_sz >>= 1;
		while (new_sz > page_size) {
			if (try_remap(args, &buf, old_sz, new_sz) < 0) {
				(void)munmap(buf, old_sz);
				return EXIT_FAILURE;
			}
			(void)stress_madvise_random(buf, new_sz);
			if (g_opt_flags & OPT_FLAGS_VERIFY) {
				if (stress_mmap_check(buf, new_sz, page_size) < 0) {
					pr_fail("%s: mremap'd region "
						"of %zu bytes does "
						"not contain expected data\n",
						args->name, sz);
					(void)munmap(buf, new_sz);
					return EXIT_FAILURE;
				}
			}
			old_sz = new_sz;
			new_sz >>= 1;
		}

		new_sz <<= 1;
		while (new_sz < mremap_bytes) {
			if (try_remap(args, &buf, old_sz, new_sz) < 0) {
				(void)munmap(buf, old_sz);
				return EXIT_FAILURE;
			}
			(void)stress_madvise_random(buf, new_sz);
			old_sz = new_sz;
			new_sz <<= 1;
		}
		(void)munmap(buf, old_sz);

		inc_counter(args);
	} while (keep_stressing());

	return EXIT_SUCCESS;
}

/*
 *  stress_mremap()
 *	stress mmap
 */
static int stress_mremap(const stress_args_t *args)
{
	return stress_oomable_child(args, NULL, stress_mremap_child, STRESS_OOMABLE_NORMAL);
}

stressor_info_t stress_mremap_info = {
	.stressor = stress_mremap,
	.class = CLASS_VM | CLASS_OS,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
#else
stressor_info_t stress_mremap_info = {
	.stressor = stress_not_implemented,
	.class = CLASS_VM | CLASS_OS,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
#endif
