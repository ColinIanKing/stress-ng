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
	{ NULL,	"memfd N",	 "start N workers allocating memory with memfd_create" },
	{ NULL,	"memfd-bytes N", "allocate N bytes for each stress iteration" },
	{ NULL,	"memfd-fds N",	 "number of memory fds to open per stressors" },
	{ NULL,	"memfd-ops N",	 "stop after N memfd bogo operations" },
	{ NULL,	NULL,		 NULL }
};

/*
 *  stress_set_memfd_bytes
 *	set max size of each memfd size
 */
static int stress_set_memfd_bytes(const char *opt)
{
	size_t memfd_bytes;

	memfd_bytes = (size_t)stress_get_uint64_byte_memory(opt, 1);
	stress_check_range_bytes("memfd-bytes", memfd_bytes,
		MIN_MEMFD_BYTES, MAX_MEM_LIMIT);
	return stress_set_setting("memfd-bytes", TYPE_ID_SIZE_T, &memfd_bytes);
}

/*
 *  stress_set_memfd_fds()
 *      set number of memfd file descriptors
 */
static int stress_set_memfd_fds(const char *opt)
{
	uint32_t memfd_fds;

	memfd_fds = (uint32_t)stress_get_uint64(opt);
	stress_check_range("memfd-fds", memfd_fds,
		MIN_MEMFD_FDS, MAX_MEMFD_FDS);
	return stress_set_setting("memfd-fds", TYPE_ID_UINT32, &memfd_fds);
}

static const stress_opt_set_func_t opt_set_funcs[] = {
	{ OPT_memfd_bytes,	stress_set_memfd_bytes },
	{ OPT_memfd_fds,	stress_set_memfd_fds },
	{ 0,			NULL }
};

#if defined(HAVE_MEMFD_CREATE)

/*
 *  Create allocations using memfd_create, ftruncate and mmap
 */
static int stress_memfd_child(const stress_args_t *args, void *context)
{
	int *fds;
	void **maps;
	uint64_t i;
	const size_t page_size = args->page_size;
	const size_t min_size = 2 * page_size;
	size_t size;
	size_t memfd_bytes = DEFAULT_MEMFD_BYTES;
	uint32_t memfd_fds = DEFAULT_MEMFD_FDS;

	(void)context;

	if (!stress_get_setting("memfd-bytes", &memfd_bytes)) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			memfd_bytes = MAX_32;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			memfd_bytes = MIN_MEMFD_BYTES;
	}
	memfd_bytes /= args->num_instances;
	if (memfd_bytes < MIN_MEMFD_BYTES)
		memfd_bytes = MIN_MEMFD_BYTES;

	(void)stress_get_setting("memfd-fds", &memfd_fds);

	size = memfd_bytes / memfd_fds;

	if (size < min_size)
		size = min_size;

	fds = calloc(memfd_fds, sizeof(*fds));
	if (!fds) {
		pr_dbg("%s: cannot allocate fds buffer: %d (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_NO_RESOURCE;
	}
	maps = calloc(memfd_fds, sizeof(*maps));
	if (!maps) {
		pr_dbg("%s: cannot allocate maps uffer: %d (%s)\n",
			args->name, errno, strerror(errno));
		free(fds);
		return EXIT_NO_RESOURCE;
	}

	do {
		for (i = 0; i < memfd_fds; i++) {
			fds[i] = -1;
			maps[i] = MAP_FAILED;
		}

		for (i = 0; i < memfd_fds; i++) {
			char filename[PATH_MAX];

			(void)snprintf(filename, sizeof(filename), "memfd-%u-%" PRIu64, args->pid, i);
			fds[i] = shim_memfd_create(filename, 0);
			if (fds[i] < 0) {
				switch (errno) {
				case EMFILE:
				case ENFILE:
					break;
				case ENOMEM:
					goto clean;
				case ENOSYS:
				case EFAULT:
				default:
					pr_err("%s: memfd_create failed: errno=%d (%s)\n",
						args->name, errno, strerror(errno));
					keep_stressing_set_flag(false);
					goto clean;
				}
			}
			if (!keep_stressing_flag())
				goto clean;
		}

		for (i = 0; i < memfd_fds; i++) {
			ssize_t ret;
#if defined(FALLOC_FL_PUNCH_HOLE) && defined(FALLOC_FL_KEEP_SIZE)
			size_t whence;
#endif

			if (fds[i] < 0)
				continue;

			if (!keep_stressing_flag())
				break;

			/* Allocate space */
			ret = ftruncate(fds[i], size);
			if (ret < 0) {
				switch (errno) {
				case EINTR:
					break;
				default:
					pr_fail_err("ftruncate");
					break;
				}
			}
			/*
			 * ..and map it in, using MAP_POPULATE
			 * to force page it in
			 */
			maps[i] = mmap(NULL, size, PROT_WRITE,
				MAP_FILE | MAP_SHARED | MAP_POPULATE,
				fds[i], 0);
			(void)stress_mincore_touch_pages(maps[i], size);
			(void)stress_madvise_random(maps[i], size);

#if defined(FALLOC_FL_PUNCH_HOLE) && defined(FALLOC_FL_KEEP_SIZE)
			/*
			 *  ..and punch a hole
			 */
			whence = (stress_mwc32() % size) & ~(page_size - 1);
			ret = shim_fallocate(fds[i], FALLOC_FL_PUNCH_HOLE |
				FALLOC_FL_KEEP_SIZE, whence, page_size);
			(void)ret;
#endif
			if (!keep_stressing_flag())
				goto clean;
		}

		for (i = 0; i < memfd_fds; i++) {
			if (fds[i] < 0)
				continue;
#if defined(SEEK_SET)
			if (lseek(fds[i], (off_t)size>> 1, SEEK_SET) < 0) {
				if (errno != ENXIO)
					pr_fail_err("lseek SEEK_SET on memfd");
			}
#endif
#if defined(SEEK_CUR)
			if (lseek(fds[i], (off_t)0, SEEK_CUR) < 0) {
				if (errno != ENXIO)
					pr_fail_err("lseek SEEK_CUR on memfd");
			}
#endif
#if defined(SEEK_END)
			if (lseek(fds[i], (off_t)0, SEEK_END) < 0) {
				if (errno != ENXIO)
					pr_fail_err("lseek SEEK_END on memfd");
			}
#endif
#if defined(SEEK_HOLE)
			if (lseek(fds[i], (off_t)0, SEEK_HOLE) < 0) {
				if (errno != ENXIO)
					pr_fail_err("lseek SEEK_HOLE on memfd");
			}
#endif
#if defined(SEEK_DATA)
			if (lseek(fds[i], (off_t)0, SEEK_DATA) < 0) {
				if (errno != ENXIO)
					pr_fail_err("lseek SEEK_DATA on memfd");
			}
#endif
			if (!keep_stressing_flag())
				goto clean;
		}
clean:
		for (i = 0; i < memfd_fds; i++) {
			if (maps[i] != MAP_FAILED)
				(void)munmap(maps[i], size);
			if (fds[i] >= 0)
				(void)close(fds[i]);
		}
		inc_counter(args);
	} while (keep_stressing());

	free(maps);
	free(fds);

	return EXIT_SUCCESS;
}


/*
 *  stress_memfd()
 *	stress memfd
 */
static int stress_memfd(const stress_args_t *args)
{
	return stress_oomable_child(args, NULL, stress_memfd_child, STRESS_OOMABLE_NORMAL);
}

stressor_info_t stress_memfd_info = {
	.stressor = stress_memfd,
	.class = CLASS_OS | CLASS_MEMORY,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
#else
stressor_info_t stress_memfd_info = {
	.stressor = stress_not_implemented,
	.class = CLASS_OS | CLASS_MEMORY,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
#endif
