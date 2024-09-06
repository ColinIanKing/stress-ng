/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2024 Colin Ian King.
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
#include "core-out-of-memory.h"

#define MIN_MSYNC_BYTES		(1 * MB)  /* MUST NOT BE page size or less! */
#define MAX_MSYNC_BYTES		(MAX_FILE_LIMIT)
#define DEFAULT_MSYNC_BYTES	(256 * MB)

#if defined(HAVE_MSYNC)
static sigjmp_buf jmp_env;
static uint64_t sigbus_count;
#endif

static const stress_help_t help[] = {
	{ NULL,	"msync N",	 "start N workers syncing mmap'd data with msync" },
	{ NULL,	"msync-bytes N", "size of file and memory mapped region to msync" },
	{ NULL,	"msync-ops N",	 "stop msync workers after N bogo msyncs" },
	{ NULL,	NULL,		 NULL }
};

static const stress_opt_t opts[] = {
	{ OPT_msync_bytes, "msync-bytes", TYPE_ID_SIZE_T_BYTES_FS, MIN_MSYNC_BYTES, MAX_MSYNC_BYTES, NULL },
	END_OPT,
};

#if defined(HAVE_MSYNC)
/*
 *  stress_page_check()
 *	check if mmap'd data is sane, sz is a page size
 *	check in 64 bit byte chunks
 */
static int OPTIMIZE3 stress_page_check(
	const uint8_t *buf,
	const uint8_t val,
	const size_t sz)
{
#if defined(__FreeBSD__)
	(void)buf;
	(void)val;
	(void)sz;
#else
	uint16_t val16 = (uint16_t)val << 8 | val;
	uint32_t val32 = (uint32_t)val16 << 16 | val16;
	register uint64_t val64 = (uint64_t)val32 << 32 | val32;
	register const uint64_t *buf64 = (const uint64_t *)buf;
	register const uint64_t *buf64end = (const uint64_t *)(buf + sz);

	while (buf64 < buf64end) {
		if (UNLIKELY(*(buf64++) != val64))
			return -1;
		if (UNLIKELY(*(buf64++) != val64))
			return -1;
		if (UNLIKELY(*(buf64++) != val64))
			return -1;
		if (UNLIKELY(*(buf64++) != val64))
			return -1;
	}
#endif
	return 0;
}

/*
 *  stress_sigbus_handler()
 *     SIGBUS handler
 */
static void MLOCKED_TEXT NORETURN stress_sigbus_handler(int signum)
{
	(void)signum;

	sigbus_count++;

	siglongjmp(jmp_env, 1); /* bounce back */
}

/*
 *  stress_msync()
 *	stress msync
 */
static int stress_msync(stress_args_t *args)
{
	NOCLOBBER uint8_t *buf = NULL;
	uint8_t *data = NULL;
	const size_t page_size = args->page_size;
	const size_t min_size = 2 * page_size;
	size_t msync_bytes = DEFAULT_MSYNC_BYTES;
	NOCLOBBER size_t sz;
	ssize_t ret;
	NOCLOBBER ssize_t rc = EXIT_SUCCESS;
	NOCLOBBER int fd = -1;
	char filename[PATH_MAX];

	ret = sigsetjmp(jmp_env, 1);
	if (ret) {
		pr_fail("%s: sigsetjmp failed\n", args->name);
		return EXIT_FAILURE;
	}
	if (stress_sighandler(args->name, SIGBUS, stress_sigbus_handler, NULL) < 0)
		return EXIT_FAILURE;

	if (!stress_get_setting("msync-bytes", &msync_bytes)) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			msync_bytes = MAXIMIZED_FILE_SIZE;
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
	stress_set_oom_adjustment(args, true);

	rc = stress_temp_dir_mk_args(args);
	if (rc < 0)
		return stress_exit_status((int)-rc);

	(void)stress_temp_filename_args(args,
		filename, sizeof(filename), stress_mwc32());

	if ((fd = open(filename, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR)) < 0) {
		rc = stress_exit_status(errno);
		pr_fail("%s: open %s failed, errno=%d (%s)\n",
			args->name, filename, errno, strerror(errno));
		(void)shim_unlink(filename);
		(void)stress_temp_dir_rm_args(args);

		return (int)rc;
	}
	(void)shim_unlink(filename);

	if (ftruncate(fd, (off_t)sz) < 0) {
		pr_err("%s: ftruncate failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		(void)close(fd);
		(void)stress_temp_dir_rm_args(args);

		return EXIT_FAILURE;
	}

	buf = (uint8_t *)stress_mmap_populate(NULL, sz,
		PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (buf == MAP_FAILED) {
		pr_err("%s: failed to mmap memory of size %zu, errno=%d (%s)\n",
			args->name, sz, errno, strerror(errno));
		rc = EXIT_NO_RESOURCE;
		goto err;
	}
	data = (uint8_t *)stress_mmap_populate(NULL, page_size,
		PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (data == MAP_FAILED) {
		pr_err("%s: failed to mmap memory of size %zu, errno=%d (%s)\n",
			args->name, page_size, errno, strerror(errno));
		rc = EXIT_NO_RESOURCE;
		goto err_unmap;
	}

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		off_t offset;
		uint8_t val;

		ret = sigsetjmp(jmp_env, 1);
		if (ret) {
			/* Try again */
			continue;
		}
		/*
		 *  Change data in memory, msync to disk
		 */
		offset = (off_t)stress_mwc64modn(sz - page_size) & ~((off_t)page_size - 1);
		val = stress_mwc8();

		(void)shim_memset(buf + offset, val, page_size);
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
		ret = read(fd, data, page_size);
		if (ret < (ssize_t)page_size) {
			pr_fail("%s: read failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			goto do_invalidate;
		}
		if (stress_page_check(data, val, page_size) < 0) {
			pr_fail("%s: msync'd data in file different "
				"to data in memory\n", args->name);
		}

do_invalidate:
		/*
		 *  Now change data on disc, msync invalidate
		 */
		offset = (off_t)stress_mwc64modn(sz - page_size) & ~((off_t)page_size - 1);
		val = stress_mwc8();

		(void)shim_memset(buf + offset, val, page_size);

		ret = lseek(fd, offset, SEEK_SET);
		if (ret == (off_t)-1) {
			pr_err("%s: cannot seet to offset %jd, errno=%d (%s)\n",
				args->name, (intmax_t)offset, errno,
				strerror(errno));
			rc = EXIT_NO_RESOURCE;
			break;
		}
		ret = read(fd, data, page_size);
		if (ret < (ssize_t)page_size) {
			pr_fail("%s: read failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
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
		if (stress_page_check(buf + offset, val, page_size) < 0) {
			pr_fail("%s: msync'd data in memory "
				"different to data in file\n", args->name);
		}

		/* Exercise invalid msync flags */
		VOID_RET(int, shim_msync(buf + offset, page_size, MS_ASYNC | MS_SYNC));
		VOID_RET(int, shim_msync(buf + offset, page_size, ~0));

		/* Exercise invalid address wrap-around */
		VOID_RET(int, shim_msync((void *)(~(uintptr_t)0 & ~(page_size - 1)),
				page_size << 1, MS_ASYNC));

		/* Exercise start == end no-op msync */
		VOID_RET(int, shim_msync(buf + offset, 0, MS_ASYNC));

#if defined(HAVE_MLOCK) &&	\
    defined(MS_INVALIDATE)
		/* Force EBUSY when invalidating on a locked page */
		ret = shim_mlock(buf + offset, page_size);
		if (ret == 0) {
			VOID_RET(int, shim_msync(buf + offset, page_size, MS_INVALIDATE));
			VOID_RET(int, shim_munlock(buf + offset, page_size));
		}
#endif

do_next:
		stress_bogo_inc(args);
	} while (stress_continue(args));


	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	(void)munmap((void *)data, page_size);
err_unmap:
	(void)munmap((void *)buf, sz);
err:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
	(void)close(fd);
	(void)stress_temp_dir_rm_args(args);

	if (sigbus_count)
		pr_inf("%s: caught %" PRIu64 " SIGBUS signals\n",
			args->name, sigbus_count);
	return (int)rc;
}

const stressor_info_t stress_msync_info = {
	.stressor = stress_msync,
	.class = CLASS_VM | CLASS_OS,
	.opts = opts,
	.verify = VERIFY_ALWAYS,
	.help = help
};
#else
const stressor_info_t stress_msync_info = {
	.stressor = stress_unimplemented,
	.class = CLASS_VM | CLASS_OS,
	.opts = opts,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.unimplemented_reason = "built without msync() system call support"
};
#endif
