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

#if defined(HAVE_MSYNC)
static sigjmp_buf jmp_env;
static uint64_t sigbus_count;
#endif

static const help_t help[] = {
	{ NULL,	"msync N",	 "start N workers syncing mmap'd data with msync" },
	{ NULL,	"msync-ops N",	 "stop msync workers after N bogo msyncs" },
	{ NULL,	"msync-bytes N", "size of file and memory mapped region to msync" },
	{ NULL,	NULL,		 NULL }
};

static int stress_set_msync_bytes(const char *opt)
{
	size_t msync_bytes;

	msync_bytes = (size_t)get_uint64_byte_memory(opt, 1);
	check_range_bytes("msync-bytes", msync_bytes,
		MIN_MSYNC_BYTES, MAX_MEM_LIMIT);
	return set_setting("msync-bytes", TYPE_ID_SIZE_T, &msync_bytes);
}

static const opt_set_func_t opt_set_funcs[] = {
	{ OPT_msync_bytes,	stress_set_msync_bytes },
	{ 0,			NULL }
};

#if defined(HAVE_MSYNC)
/*
 *  stress_page_check()
 *	check if mmap'd data is sane
 */
static int stress_page_check(
	uint8_t *buf,
	const uint8_t val,
	const size_t sz)
{
	size_t i;

	for (i = 0; i < sz; i++) {
		if (buf[i] != val)
			return -1;
	}
	return 0;
}

/*
 *  stress_sigbus_handler()
 *     SIGBUS handler
 */
static void MLOCKED_TEXT stress_sigbus_handler(int signum)
{
	(void)signum;

	sigbus_count++;

	siglongjmp(jmp_env, 1); /* bounce back */
}

/*
 *  stress_msync()
 *	stress msync
 */
static int stress_msync(const args_t *args)
{
	uint8_t *buf = NULL;
	const size_t page_size = args->page_size;
	const size_t min_size = 2 * page_size;
	size_t msync_bytes = DEFAULT_MSYNC_BYTES;
	NOCLOBBER size_t sz;
	ssize_t ret;
	NOCLOBBER ssize_t rc = EXIT_SUCCESS;

	int fd = -1;
	char filename[PATH_MAX];

	ret = sigsetjmp(jmp_env, 1);
	if (ret) {
		pr_fail_err("sigsetjmp");
		return EXIT_FAILURE;
	}
	if (stress_sighandler(args->name, SIGBUS, stress_sigbus_handler, NULL) < 0)
		return EXIT_FAILURE;

	if (!get_setting("msync-bytes", &msync_bytes)) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			msync_bytes = MAX_MSYNC_BYTES;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			msync_bytes = MIN_MSYNC_BYTES;
	}
	msync_bytes /= args->num_instances;
	if (msync_bytes < MIN_MSYNC_BYTES)
		msync_bytes = MIN_MSYNC_BYTES;
	if (msync_bytes < page_size)
		msync_bytes = page_size;
	sz = msync_bytes & ~(page_size - 1);
	if (sz < min_size)
		sz = min_size;

	/* Make sure this is killable by OOM killer */
	set_oom_adjustment(args->name, true);

	rc = stress_temp_dir_mk_args(args);
	if (rc < 0)
		return exit_status(-rc);

	(void)stress_temp_filename_args(args,
		filename, sizeof(filename), mwc32());

	if ((fd = open(filename, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR)) < 0) {
		rc = exit_status(errno);
		pr_fail_err("open");
		(void)unlink(filename);
		(void)stress_temp_dir_rm_args(args);

		return rc;
	}
	(void)unlink(filename);

	if (ftruncate(fd, sz) < 0) {
		pr_err("%s: ftruncate failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		(void)close(fd);
		(void)stress_temp_dir_rm_args(args);

		return EXIT_FAILURE;
	}

	buf = (uint8_t *)mmap(NULL, sz,
		PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (buf == MAP_FAILED) {
		pr_err("%s: failed to mmap memory, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		rc = EXIT_NO_RESOURCE;
		goto err;
	}

	do {
		off_t offset;
		uint8_t val, data[page_size];

		ret = sigsetjmp(jmp_env, 1);
		if (ret) {
			/* Try again */
			continue;
		}
		/*
		 *  Change data in memory, msync to disk
		 */
		offset = (mwc64() % (sz - page_size)) & ~(page_size - 1);
		val = mwc8();

		(void)memset(buf + offset, val, page_size);
		ret = shim_msync(buf + offset, page_size, MS_SYNC);
		if (ret < 0) {
			pr_fail("%s: msync MS_SYNC on "
				"offset %jd failed, errno=%d (%s)\n",
				args->name, (intmax_t)offset, errno,
				strerror(errno));
			goto do_invalidate;
		}
		ret = lseek(fd, offset, SEEK_SET);
		if (ret == (off_t)-1) {
			pr_err("%s: cannot seet to offset %jd, "
				"errno=%d (%s)\n",
				args->name, (intmax_t)offset, errno,
				strerror(errno));
			rc = EXIT_NO_RESOURCE;
			break;
		}
		ret = read(fd, data, sizeof(data));
		if (ret < (ssize_t)sizeof(data)) {
			pr_fail_err("read");
			goto do_invalidate;
		}
		if (stress_page_check(data, val, sizeof(data)) < 0) {
			pr_fail("%s: msync'd data in file different "
				"to data in memory\n", args->name);
		}

do_invalidate:
		/*
		 *  Now change data on disc, msync invalidate
		 */
		offset = (mwc64() % (sz - page_size)) & ~(page_size - 1);
		val = mwc8();

		(void)memset(buf + offset, val, page_size);

		ret = lseek(fd, offset, SEEK_SET);
		if (ret == (off_t)-1) {
			pr_err("%s: cannot seet to offset %jd, errno=%d (%s)\n",
				args->name, (intmax_t)offset, errno,
				strerror(errno));
			rc = EXIT_NO_RESOURCE;
			break;
		}
		ret = read(fd, data, sizeof(data));
		if (ret < (ssize_t)sizeof(data)) {
			pr_fail_err("read");
			goto do_next;
		}
		ret = shim_msync(buf + offset, page_size, MS_INVALIDATE);
		if (ret < 0) {
			pr_fail("%s: msync MS_INVALIDATE on "
				"offset %jd failed, errno=%d (%s)\n",
				args->name, (intmax_t)offset, errno,
				strerror(errno));
			goto do_next;
		}
		if (stress_page_check(buf + offset, val, sizeof(data)) < 0) {
			pr_fail("%s: msync'd data in memory "
				"different to data in file\n", args->name);
		}
do_next:
		inc_counter(args);
	} while (keep_stressing());

	(void)munmap((void *)buf, sz);
err:
	(void)close(fd);
	(void)stress_temp_dir_rm_args(args);

	if (sigbus_count)
		pr_inf("%s: caught %" PRIu64 " SIGBUS signals\n",
			args->name, sigbus_count);
	return rc;
}

stressor_info_t stress_msync_info = {
	.stressor = stress_msync,
	.class = CLASS_VM | CLASS_OS,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
#else
stressor_info_t stress_msync_info = {
	.stressor = stress_not_implemented,
	.class = CLASS_VM | CLASS_OS,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
#endif
