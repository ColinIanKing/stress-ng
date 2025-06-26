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

static const stress_help_t help[] = {
	{ NULL,	"mincore N",	  "start N workers exercising mincore" },
	{ NULL,	"mincore-ops N",  "stop after N mincore bogo operations" },
	{ NULL,	"mincore-random", "randomly select pages rather than linear scan" },
	{ NULL,	NULL,		  NULL }
};

static const stress_opt_t opts[] = {
	{ OPT_mincore_rand, "mincore-rand", TYPE_ID_BOOL, 0, 1, NULL },
	END_OPT,
};

#if defined(HAVE_MINCORE) &&	\
    NEED_GLIBC(2,2,0)

/*
 *  stress_mincore_file()
 *	create a temp file for file-back mmap'd page, return
 *	fd if successful, -1 if failed.
 */
static int stress_mincore_file(stress_args_t *args)
{
	int ret, fd;
	char filename[PATH_MAX];
	ret = stress_temp_dir_mk_args(args);
	if (ret != 0)
		return -1;

	(void)stress_temp_filename_args(args, filename,
					sizeof(filename), stress_mwc32());
	fd = open(filename, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
	(void)shim_unlink(filename);
	if (fd < 0) {
		(void)stress_temp_dir_rm_args(args);
		return -1;
	}
	ret = shim_fallocate(fd, 0, (off_t)0, (off_t)args->page_size);
	if (ret < 0) {
		(void)close(fd);
		(void)stress_temp_dir_rm_args(args);
		return -1;
	}
	return fd;
}

/*
 *  stress_mincore_expect()
 *	check for expected return code/errors
 */
static void stress_mincore_expect(
	stress_args_t *args,
	const int ret,		/* return value */
	const int ret_expected,	/* expected return value */
	const int err,		/* returned errno */
	const int err_expected,	/* expected errno */
	char *msg,		/* test message */
	int *rc)		/* return code */
{
	if (LIKELY(ret == ret_expected)) {
		if (LIKELY(ret_expected == 0))
			return;	/* Success! */
		pr_fail("%s: unexpected success exercising %s\n",
			args->name, msg);
		*rc = EXIT_FAILURE;
	}
	/* Silently ignore ENOSYS for now */
	if (UNLIKELY(err == ENOSYS))
		return;
	if (UNLIKELY(err != err_expected)) {
		pr_fail("%s: expected errno %d, got %d instead while exercising %s\n",
			args->name, err_expected, err, msg);
		*rc = EXIT_FAILURE;
	}
}

/*
 *  stress_mincore()
 *	stress mincore system call
 */
static int stress_mincore(stress_args_t *args)
{
	uint8_t *addr = NULL, *prev_addr = NULL;
	const size_t page_size = args->page_size;
	const intptr_t mask = ~((intptr_t)page_size - 1);
	bool mincore_rand = false;
	int fd, rc = EXIT_SUCCESS;
	uint8_t *mapped, *unmapped, *fdmapped;
	double duration = 0.0, count = 0.0, rate;

	(void)stress_get_setting("mincore-rand", &mincore_rand);

	/* Don't worry if we can't map a page, it is not critical */
	mapped = mmap(NULL, page_size, PROT_READ | PROT_WRITE,
			MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (mapped != MAP_FAILED)
		stress_set_vma_anon_name(mapped, page_size, "rw-page");

	/* Map a file backed page, silently ignore failure */
	fd = stress_mincore_file(args);
	if (fd >= 0) {
		fdmapped = mmap(NULL, page_size, PROT_READ | PROT_WRITE,
				MAP_PRIVATE, fd, 0);
	} else {
		fdmapped = MAP_FAILED;
	}
	if (fdmapped != MAP_FAILED)
		stress_set_vma_anon_name(fdmapped, page_size, "fd-page");

	/* Map then unmap a page to get an unmapped page address */
	unmapped = mmap(NULL, page_size, PROT_READ | PROT_WRITE,
			MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (unmapped != MAP_FAILED) {
		if (munmap((void *)unmapped, page_size) < 0)
			unmapped = MAP_FAILED;
	}

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		int i;

		for (i = 0; LIKELY((i < 100) && stress_continue_flag()); i++) {
			int ret, redo = 0;
			static unsigned char vec[1];
			double t;

redo: 			errno = 0;
			ret = shim_mincore((void *)addr, page_size, vec);
			if (UNLIKELY(ret < 0)) {
				switch (errno) {
				case ENOMEM:
					/* Page not mapped */
					break;
				case EAGAIN:
					if (LIKELY(++redo < 100))
						goto redo;
					break;
				case ENOSYS:
					if (UNLIKELY(stress_instance_zero(args)))
						pr_inf_skip("%s: mincore no not implemented, skipping stressor\n",
							args->name);
					rc = EXIT_NOT_IMPLEMENTED;
					goto err;
				default:
					pr_fail("%s: mincore on address %p errno=%d %s\n",
						args->name, (void *)addr, errno,
						strerror(errno));
					rc = EXIT_FAILURE;
					break;
				}
			}
			if (mapped != MAP_FAILED) {
				/* Force page to be resident */
				*mapped = 0xff;
				t = stress_time_now();
				ret = shim_mincore((void *)mapped, page_size, vec);
				if (ret >= 0) {
					duration += stress_time_now() - t;
					count += 1.0;
				} else {
					/* Should not return ENOMEM on a mapped page */
					if (UNLIKELY(errno == ENOMEM)) {
						pr_fail("%s: mincore on address %p failed, errno=$%d (%s)\n",
							args->name, (void *)mapped, errno,
							strerror(errno));
						rc = EXIT_FAILURE;
					}
				}
			}
			if (UNLIKELY(fdmapped != MAP_FAILED)) {
				/* Force page to be resident */
				*fdmapped = stress_mwc8();
#if defined(MS_ASYNC)
				VOID_RET(int, shim_msync((void *)fdmapped, page_size, MS_ASYNC));
#endif
				ret = shim_mincore((void *)fdmapped, page_size, vec);
				if (ret < 0) {
					/* Should not return ENOMEM on a mapped page */
					if (UNLIKELY(errno == ENOMEM)) {
						pr_fail("%s: mincore on address %p failed, errno=$%d (%s)\n",
							args->name, (void *)fdmapped, errno,
							strerror(errno));
						rc = EXIT_FAILURE;
					}
				}
			}
			if (UNLIKELY(unmapped != MAP_FAILED)) {
				/* mincore on unmapped page should fail */
				ret = shim_mincore((void *)unmapped, page_size, vec);
				if (UNLIKELY(ret == 0)) {
					pr_fail("%s: mincore on unmapped address %p should have failed but did not\n",
						args->name, (void *)unmapped);
					rc = EXIT_FAILURE;
				}
			}
			if (mincore_rand) {
				addr = (uint8_t *)(intptr_t)
					(((intptr_t)addr >> 1) & mask);
				if (addr == prev_addr)
					addr = (uint8_t *)(((intptr_t)stress_mwc64()) & mask);
				prev_addr = addr;
			} else {
				addr += page_size;
			}

			if (UNLIKELY(mapped != MAP_FAILED)) {
				/*
				 *  Exercise with zero length, ignore return
				 */
				ret = shim_mincore((void *)mapped, 0, vec);
				stress_mincore_expect(args, ret, 0, errno, EINVAL,
					"zero length for vector size", &rc);

#if 0
				/*
				 *  Exercise with huge length, ignore return
				 */
				ret = shim_mincore((void *)mapped, ~0, vec);
				stress_mincore_expect(args, ret, 0, errno, ENOMEM,
					"invalid length for vector size", &rc);
#endif

				/*
				 *  Exercise with masaligned address, ignore return
				 */
				ret = shim_mincore((void *)(mapped + 1), 0, vec);
				stress_mincore_expect(args, ret, 0, errno, EINVAL,
					"misaligned address", &rc);

				/*
				 *  Exercise with NULL vec, ignore return
				 */
				ret = shim_mincore((void *)mapped, page_size, NULL);
				stress_mincore_expect(args, ret, 0, errno, EFAULT,
					"NULL vector address", &rc);

				/*
				 *  Exercise with invalid page
				 */
				ret = shim_mincore(mapped, page_size, args->mapped->page_none);
				stress_mincore_expect(args, ret, 0, errno, EFAULT,
					"invalid vector address", &rc);
			}

			/*
			 *  Exercise with NULL address
			 */
			ret = shim_mincore(NULL, page_size, vec);
			stress_mincore_expect(args, ret, 0, errno, ENOMEM,
				"NULL memory address", &rc);

			/*
			 *  Exercise with NULL/zero arguments
			 */
			ret = shim_mincore(NULL, 0, NULL);
			/*  some systems return ENOMEM.. */
			if (UNLIKELY(errno != ENOMEM))
				stress_mincore_expect(args, ret, 0, errno, EINVAL,
					"NULL and zero arguments", &rc);
		}
		stress_bogo_inc(args);
	} while (stress_continue(args));

	rate = (count > 0.0) ? duration / count : 0.0;
	stress_metrics_set(args, 0, "nanosecs per mincore call",
		rate * STRESS_DBL_NANOSECOND, STRESS_METRIC_HARMONIC_MEAN);

err:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	if (fdmapped != MAP_FAILED)
		(void)munmap((void *)fdmapped, page_size);
	if (mapped != MAP_FAILED)
		(void)munmap((void *)mapped, page_size);

	if (fd >= 0) {
		(void)close(fd);
		(void)stress_temp_dir_rm_args(args);
	}
	return rc;
}

const stressor_info_t stress_mincore_info = {
	.stressor = stress_mincore,
	.classifier = CLASS_OS | CLASS_MEMORY,
	.opts = opts,
	.verify = VERIFY_ALWAYS,
	.help = help
};
#else
const stressor_info_t stress_mincore_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_OS | CLASS_MEMORY,
	.opts = opts,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.unimplemented_reason = "built without mincore() system call support"
};
#endif
