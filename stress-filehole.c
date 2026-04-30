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
#include "core-killpid.h"
#include "core-madvise.h"
#include "core-mmap.h"
#include "core-signal.h"

#define MIN_FILEHOLE_BYTES	(1 * MB)
#define MAX_FILEHOLE_BYTES	(32 * GB)
#define DEFAULT_FILEHOLE_BYTES	(16 * MB)

#if defined(HAVE_PREADV) || \
    defined(HAVE_PWRITEV)
#define HAVE_PREADV_WRITEV
#endif

static const stress_help_t help[] = {
	{ NULL,	"filehole N",		"start N workers punching holes in a 16MB file" },
	{ NULL,	"filehole-bytes N",	"size of file being punched" },
	{ NULL, "filehole-defrag",	"defragment file at end of each iteration" },
	{ NULL,	"filehole-ops N",	"stop after N punch bogo operations" },
	{ NULL,	NULL,			NULL }
};

static const stress_opt_t opts[] = {
	{ OPT_filehole_bytes,  "filehole-bytes",  TYPE_ID_UINT64_BYTES_FS, MIN_FILEHOLE_BYTES, MAX_FILEHOLE_BYTES, NULL },
	{ OPT_filehole_defrag, "filehole-defrag", TYPE_ID_BOOL, 0, 1, NULL },
	END_OPT,
};

#if defined(HAVE_FALLOCATE) &&			\
    ((defined(FALLOC_FL_KEEP_SIZE) &&		\
      defined(FALLOC_FL_PUNCH_HOLE)) ||		\
     (defined(FALLOC_FL_ZERO_RANGE)))		\

#define ERR_SKIP			(-1)
#define ERR_FAIL			(-2)

typedef struct {
	const int mode;	/* fallocate mode flags */
	bool zeroed;	/* true of flags zero the data */
} fallocate_mode_t;

/*
 *  3 to 1 ratio of punch hole to zero range
 */
static const fallocate_mode_t fallocate_modes[] = {
	{ 0,						false },
#if defined(FALLOC_FL_KEEP_SIZE) &&	\
    defined(FALLOC_FL_PUNCH_HOLE)
	{ FALLOC_FL_KEEP_SIZE | FALLOC_FL_PUNCH_HOLE,	true },
	{ FALLOC_FL_KEEP_SIZE | FALLOC_FL_PUNCH_HOLE,	true },
	{ FALLOC_FL_KEEP_SIZE | FALLOC_FL_PUNCH_HOLE,	true },
#endif
#if defined(FALLOC_FL_ZERO_RANGE)
	{ FALLOC_FL_ZERO_RANGE, 			true },
#endif
};

/*
 *  stress_filehole_write()
 *	write a page size of data
 */
static int stress_filehole_write(
	stress_args_t *args,
	const int fd,
	void *buf,
	const size_t buf_len,
	const off_t offset)
{
	ssize_t ret;
	static int fsync_counter = 0;

#if defined(HAVE_PWRITE)
	ret = pwrite(fd, buf, buf_len, offset);
#else
	if (lseek(fd, offset, SEEK_SET) < 0) {
		pr_fail("%s: lseek at offset %jd failed, errno=%d (%s)\n",
			args->name, (intmax_t)offset, errno, strerror(errno));
		return ERR_FAIL;
	}
	ret = write(fd, buf, buf_len);
#endif
	if (ret < 0) {
		switch (errno) {
		case EAGAIN:
		case EFBIG:
		case ENOSPC:
		case EINTR:
			return ERR_SKIP;
		default:
			pr_fail("%s: write at offset %jd failed, errno=%d (%s)\n",
				args->name, (intmax_t)offset, errno, strerror(errno));
			return ERR_FAIL;
		}
	}

	/*
	 *  Periodically sync data
	 */
	fsync_counter++;
	if (fsync_counter > 8192) {
		fsync_counter = 0;
		(void)shim_fsync(fd);
	}
	return 0;
}

/*
 *  stress_filehole_read_check()
 *	check for fatal read errors and report them
 */
static int stress_filehole_read_check(
	stress_args_t *args,
	const off_t offset,
	const ssize_t ret,
	const int err)
{
	if (ret < 0) {
		switch (err) {
		case EAGAIN:
			return ERR_SKIP;
		default:
			pr_fail("%s: read at offset %jd failed, errno=%d (%s)\n",
				args->name, (intmax_t)offset, err, strerror(err));
			return ERR_FAIL;
		}
	}
	return 0;
}

/*
 *  stress_filehole_read()
 *	rwad a page size of data
 */
static int stress_filehole_read(
	stress_args_t *args,
	const int fd,
	void *buf,
	const size_t buf_len,
	const off_t offset)
{
	ssize_t ret;

#if defined(HAVE_PWRITE)
	ret = pread(fd, buf, buf_len, offset);
#else
	if (lseek(fd, offset, SEEK_SET) < 0) {
		pr_fail("%s: lseek at offset %jd failed, errno=%d (%s)\n",
			args->name, (intmax_t)offset, errno, strerror(errno));
		return ERR_FAIL;
	}
	ret = read(fd, buf, buf_len);
#endif
	return stress_filehole_read_check(args, offset, ret, errno);
}

/*
 *  stress_filehole_lseek_read()
 *	random seek+reads, SEEK_SETs will read at any random position
 *	whereas SEEK_DATA or SEEK_HOLE will seek to the positions that are
 *	page aligned as that's where the holes and non-holes are aligned to.
 */
static void stress_filehole_lseek_read(
	stress_args_t *args,
	const int fd,
	uint64_t *buf,
	const size_t page_size,
	off_t filehole_bytes,
	const size_t pages)
{
	static const int seek_whences[] = {
#if defined(SEEK_SET)
		SEEK_SET,
#endif
#if defined(SEEK_DATA)
		SEEK_DATA,
#endif
#if defined(SEEK_HOLE)
		SEEK_HOLE,
#endif
	};
	size_t i;
	off_t offset;

	if (UNLIKELY(SIZEOF_ARRAY(seek_whences) == 0))
		return;

	for  (i = 0; stress_continue(args) && (i < pages); i++) {
		const int whence = seek_whences[stress_mwcsizemodn(SIZEOF_ARRAY(seek_whences))];
		off_t offret;

		offset = stress_mwc64modn((uint64_t)filehole_bytes);
		offret = lseek(fd, offset, whence);
		if (offret >= 0) {
			ssize_t ret;

			ret = read(fd, buf, page_size);
			if (stress_filehole_read_check(args, offset, ret, errno) == ERR_FAIL)
				return;
			stress_bogo_inc(args);
		}
	}
}

/*
 *  stress_filehole_non_zeros_to_holes()
 *	turn non-zero data to holes
 */
static void stress_filehole_non_zeros_to_holes(
	stress_args_t *args,
	const int fd,
	uint64_t *buf,
	const size_t page_size,
	const off_t filehole_bytes)
{
#if defined(FALLOC_FL_KEEP_SIZE) &&	\
    defined(FALLOC_FL_PUNCH_HOLE)
	off_t offset;

	for (offset = 0; stress_continue(args) && (offset < filehole_bytes); offset += page_size) {
		ssize_t ret;

#if defined(HAVE_PWRITE)
		ret = pread(fd, buf, page_size, offset);
#else
		if (lseek(fd, offset, SEEK_SET) < 0)
			continue;
		ret = read(fd, buf, buf_len);
#endif
		if (ret != (ssize_t)page_size)
			continue;
		if (stress_data_is_not_zero(buf, page_size))
			VOID_RET(int, fallocate(fd, FALLOC_FL_KEEP_SIZE | FALLOC_FL_PUNCH_HOLE, offset, page_size));
	}
#else
	(void)fd;
	(void)buf;
	(void)page_size;
	(void)filehold_bytes;
#endif
}

/*
 *  stress_filehole_defrag()
 *	naive file defrag, read hole'd file and write out a
 *	none-hole'd copy. This reduces the number of extents
 *	of original. It's not perfect, but it's portable.
 */
static int stress_filehole_defrag(
	const char *filename,
	int *fd_in,
	uint64_t *buf,
	const size_t page_size)
{
	char filename_tmp[PATH_MAX + 5];
	int fd_out;

	if (lseek(*fd_in, 0, SEEK_SET) < 0) {
		/* can't seek, abort no action */
		return 0;
	}

	(void)snprintf(filename_tmp, sizeof(filename_tmp), "%s-tmp", filename);
	if ((fd_out = open(filename_tmp, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR)) < 0) {
		/* abort, force unlink just in case */
		(void)unlink(filename_tmp);
		return 0;
	}
	/*  do manual copy without holes */
	for (;;) {
		ssize_t nrd, nwr;

		nrd = read(*fd_in, buf, page_size);
		if (UNLIKELY(nrd == 0))
			break;
		if (UNLIKELY(nrd < 0)) {
			/*
			 *   copy failed, revert back to
			 *   using original file
			 */
			(void)close(fd_out);
			(void)unlink(filename_tmp);
			return 0;
		}
		nwr = write(fd_out, buf, nrd);
		if (UNLIKELY(nwr != (ssize_t)nrd)) {
			/*
			 *   copy failed, revert back to
			 *   using original file
			 */
			(void)close(fd_out);
			(void)unlink(filename_tmp);
			return 0;
		}
	}
	if (rename(filename_tmp, filename) < 0) {
		/*
		 *  rename failed, try to revert back to
		 *  using original file
		 */
		(void)close(fd_out);
		(void)unlink(filename_tmp);
		return 0;
	}
	/* renamed, so use new file that was renamed */
	(void)close(*fd_in);
	*fd_in = fd_out;
	return 0;
}
/*
 *  stress_filehole_zero()
 * 	fallocate a page sized chunk, if this fails, write zero's to it
 */
static int stress_filehole_zero(
	stress_args_t *args,
	const int fd,
	uint64_t *zero_buf,
	const int mode,
	const off_t offset,
	const size_t page_size,
	const bool write_zero)
{
	if (fallocate(fd, mode, offset, page_size) < 0)
		if (write_zero)
			return stress_filehole_write(args, fd, zero_buf, page_size, offset);
	return 0;
}

/*
 *  stress_filehole_filesize
 *	get file size
 */
static void stress_filehole_filesize(
	const int fd,
	double *max_size,
	double *max_blks)
{
	struct stat sb;
	double val;

	if (fstat(fd, &sb) < 0)
		return;

	val = (double)sb.st_size;
	if (*max_size < val)
		*max_size = val;

	val = (double)sb.st_blocks;
	if (*max_blks < val)
		*max_blks = val;
}

/*
 *  stress_filehole_io()
 */
static int stress_filehole_io(
	stress_args_t *args,
	const int fd,
	uint64_t *buf,
	uint64_t *zero_buf,
	const size_t page_size,
	const off_t offset,
	const bool verify)
{
	static const int msync_flags[] = {
#if defined(MS_ASYNC)
		MS_ASYNC,
#endif
#if defined(MS_SYNC)
		MS_SYNC,
#endif
#if defined(MS_INVALIDATE)
		MS_INVALIDATE,
#endif
	};

	int ret;
	size_t modes_index;
	const size_t flags_index = stress_mwcsizemodn(SIZEOF_ARRAY(msync_flags));
	uint8_t *ptr;
	uint8_t val;

	val = stress_mwc8();
	val = (val == 0) ? 0x5a : val;

	(void)shim_memset(buf, val, page_size);
	ret = stress_filehole_write(args, fd, buf, page_size, offset + page_size);
	if (ret < 0)
		return ret;
	ptr = (uint8_t *)mmap(NULL, page_size, PROT_READ | PROT_WRITE,
			MAP_SHARED, fd, offset + page_size);
	if (ptr != MAP_FAILED) {
		register size_t i;

		for (i = 0; i < page_size; i++) {
			if (UNLIKELY(ptr[i] != val)) {
				pr_fail("%s: mmap'd read of file data failed at offset %jd, "
					"got 0x%2.2x, expected 0x%2.2x\n",
					args->name, offset + i, ptr[i], val);
				(void)munmap((void *)ptr, page_size);
				return ERR_FAIL;
			}
		}
		(void)memset((void *)ptr, 0xaa, page_size);
		(void)shim_msync((void *)ptr, page_size, msync_flags[flags_index]);
		(void)munmap((void *)ptr, page_size);
	}

	modes_index = stress_mwcsizemodn(SIZEOF_ARRAY(fallocate_modes));
	ret = stress_filehole_zero(args, fd, zero_buf, fallocate_modes[modes_index].mode, offset, page_size, true);
	if (ret < 0)
		return ret;

	ret = stress_filehole_read(args, fd, buf, page_size, offset);
	if (ret < 0)
		return ret;
	if (verify && fallocate_modes[modes_index].zeroed) {
		if (stress_data_is_not_zero(buf, page_size)) {
			pr_fail("%s: data at offset %jd not zero\n",
				args->name, (intmax_t)offset);
			return ERR_FAIL;
		}
	}

	if (offset > (off_t)page_size) {
		val ^= 0xff;
		val = (val == 0) ? 0xa5 : val;
		(void)shim_memset(buf, val, page_size);
		ret = stress_filehole_write(args, fd, buf, page_size, offset - page_size);
		if (ret < 0)
			return ret;
	}

	modes_index = stress_mwcsizemodn(SIZEOF_ARRAY(fallocate_modes));
	return stress_filehole_zero(args, fd, zero_buf, fallocate_modes[modes_index].mode, offset, page_size, false);
}

/*
 *  stress_filehole
 *	stress punching holes in files
 */
static int stress_filehole(stress_args_t *args)
{
	int fd = -1, ret, rc = EXIT_SUCCESS;
	char filename[PATH_MAX];
	size_t extents;
	uint64_t *buf, *zero_buf;
	uint64_t filehole_bytes_total = DEFAULT_FILEHOLE_BYTES;
	const size_t page_size = args->page_size;
	off_t offset, filehole_bytes;
	size_t pages;
	double max_size, max_blks;
	double extents_total, extents_count;
	const bool verify = !!(g_opt_flags & OPT_FLAGS_VERIFY);
	bool filehole_defrag = false;

	if (!stress_setting_get("filehole-bytes", &filehole_bytes_total)) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			filehole_bytes_total = MAX_FILEHOLE_BYTES;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			filehole_bytes_total = MIN_FILEHOLE_BYTES;
	}
	if (filehole_bytes_total < MIN_FILEHOLE_BYTES) {
		filehole_bytes_total = MIN_FILEHOLE_BYTES;
		if (stress_instance_zero(args))
			pr_inf("%s: --filehole-bytes too small, using %" PRIu64 " instead\n",
				args->name, filehole_bytes_total);
	}
	if (filehole_bytes_total > MAX_FILEHOLE_BYTES) {
		filehole_bytes_total = MAX_FILEHOLE_BYTES;
		if (stress_instance_zero(args))
			pr_inf("%s: --filehole-bytes too large, using %" PRIu64 " instead\n",
				args->name, filehole_bytes_total);
	}
	(void)stress_setting_get("filehole-defrag", &filehole_defrag);

	filehole_bytes = (off_t)(filehole_bytes_total / args->instances);
	if (filehole_bytes < (off_t)MIN_FILEHOLE_BYTES)
		filehole_bytes = (off_t)MIN_FILEHOLE_BYTES;
	if (filehole_bytes < (off_t)page_size * 8)
		filehole_bytes = (off_t)(page_size * 8);
	if (stress_instance_zero(args))
		stress_fs_usage_bytes(args, filehole_bytes, filehole_bytes_total);

	buf = (uint64_t *)stress_mmap_populate(NULL, page_size * 2,
			PROT_READ | PROT_WRITE,
			MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	if (buf == MAP_FAILED) {
		pr_inf("%s: failed to mmap %zu sized buffer%s, errno=%d (%s), skipping stressor\n",
			args->name, page_size, stress_memory_free_get(),
			errno, strerror(errno));
		rc = EXIT_NO_RESOURCE;
		goto tidy_ret;
	}
	zero_buf = buf + (page_size / sizeof(uint64_t));
	pages = (size_t)(filehole_bytes / page_size);

	stress_memory_anon_name_set(buf, page_size, "filehole-buffer");
	(void)stress_madvise_mergeable(buf, page_size);

	ret = stress_fs_temp_dir_make_args(args);
	if (ret < 0) {
		rc = stress_exit_status(-ret);
		goto tidy_buf;
	}

	(void)stress_fs_temp_filename_args(args,
		filename, sizeof(filename), stress_mwc32());
	if ((fd = open(filename, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR)) < 0) {
		rc = stress_exit_status(errno);
		pr_fail("%s: open %s failed, errno=%d (%s)\n",
			args->name, filename, errno, strerror(errno));
		goto tidy_temp;
	}
	stress_fs_file_rw_hint_short(fd);

	if (UNLIKELY(!stress_continue(args)))
		goto tidy;

	stress_proc_state_set(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_proc_state_set(args->name, STRESS_STATE_RUN);

	max_size = 0.0;
	max_blks = 0.0;

	extents_total = 0.0;
	extents_count = 0.0;

	do {
		size_t i;
		uint8_t val;

#if defined(FALLOC_FL_PUNCH_HOLE) &&	\
    defined(FALLOC_FL_KEEP_SIZE)
		VOID_RET(int, fallocate(fd, FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE, 0, filehole_bytes));
#elif defined(HAVE_FTRUNCATE)
		VOID_RET(int, ftruncate(fd, 0));
#endif

		/*
		 *  Reverse file write and faloocate, makes lots of holes
		 */
		for (offset = filehole_bytes; stress_continue(args) && (offset >= 0); offset -= page_size) {
			if (stress_filehole_io(args, fd, buf, zero_buf, page_size, offset, verify) == ERR_FAIL)
				break;
			stress_bogo_inc(args);
		}

		/*
		 *  Random positioned write and fallocate
		 */
#if defined(SHIM_POSIX_FADV_RANDOM)
		(void)shim_posix_fadvise(fd, 0, filehole_bytes, SHIM_POSIX_FADV_RANDOM);
#endif
		for  (i = 0; stress_continue(args) && (i < pages >> 2); i++) {
			offset = stress_mwc64modn((uint64_t)filehole_bytes) & (~(uint64_t)(page_size - 1));
			if (stress_filehole_io(args, fd, buf,  zero_buf,page_size, offset, verify) == ERR_FAIL)
				break;
			stress_bogo_inc(args);
		}

		/*
		 *  Forward file write and fallocate, makes some more holes
		 */
#if defined(SHIM_POSIX_FADV_SEQUENTIAL)
		(void)shim_posix_fadvise(fd, 0, filehole_bytes, SHIM_POSIX_FADV_SEQUENTIAL);
#endif
		for (offset = 0; stress_continue(args) && (offset < filehole_bytes); offset += page_size) {
			if (stress_filehole_io(args, fd, buf,  zero_buf,page_size, offset, verify) == ERR_FAIL)
				break;
			stress_bogo_inc(args);
		}

		if (stress_mwc1()) {
			/*
			 *  Reverse fallocate holes
			 */
			for (offset = filehole_bytes; stress_continue(args) && (offset >= 0); offset -= page_size) {
				const size_t modes_index = stress_mwcsizemodn(SIZEOF_ARRAY(fallocate_modes));

				stress_filehole_zero(args, fd, zero_buf, fallocate_modes[modes_index].mode, offset, page_size, false);
				stress_bogo_inc(args);
			}
		} else {
			/*
			 *  Forward fallocate holes
			 */
#if defined(SHIM_POSIX_FADV_SEQUENTIAL)
			(void)shim_posix_fadvise(fd, 0, filehole_bytes, SHIM_POSIX_FADV_SEQUENTIAL);
#endif
			for (offset = 0; stress_continue(args) && (offset < filehole_bytes); offset += page_size) {
				const size_t modes_index = stress_mwcsizemodn(SIZEOF_ARRAY(fallocate_modes));

				stress_filehole_zero(args, fd, zero_buf, fallocate_modes[modes_index].mode, offset, page_size, false);
				stress_bogo_inc(args);
			}
		}
		(void)stress_filehole_filesize(fd, &max_size, &max_blks);
		extents = stress_fs_extents_get(fd);
		extents_total += extents;
		extents_count += 1.0;
		/*
		 *  Random positioned lseek reads on data/hole
		 */
#if defined(SHIM_POSIX_FADV_RANDOM)
		(void)shim_posix_fadvise(fd, 0, filehole_bytes, SHIM_POSIX_FADV_RANDOM);
#endif
		stress_filehole_lseek_read(args, fd, buf, page_size, filehole_bytes, pages);

		/*
		 *  Reverse file fill with random data
		 */
		for (offset = filehole_bytes; stress_continue(args) && (offset >= 0); offset -= page_size) {
			val = stress_mwc8();
			if (val == 0)
				val = 1U << (stress_mwc8() & 0x7);
			(void)shim_memset(buf, val, page_size);
			if (stress_filehole_write(args, fd, buf, page_size, offset) == ERR_FAIL)
				break;
		}
		/*
		 *  Forward fallocate holes with gaps
		 */
#if defined(SHIM_POSIX_FADV_SEQUENTIAL)
		(void)shim_posix_fadvise(fd, 0, filehole_bytes, SHIM_POSIX_FADV_SEQUENTIAL);
#endif
		for (offset = 0; stress_continue(args) && (offset < filehole_bytes); offset += (page_size + page_size)) {
			size_t modes_index = stress_mwcsizemodn(SIZEOF_ARRAY(fallocate_modes));

			stress_filehole_zero(args, fd, zero_buf, fallocate_modes[modes_index].mode, offset, page_size, false);
		}

		/*
		 *  Random positioned lseek reads on data/hole
		 */
#if defined(SHIM_POSIX_FADV_RANDOM)
		(void)shim_posix_fadvise(fd, 0, filehole_bytes, SHIM_POSIX_FADV_RANDOM);
#endif
		stress_filehole_lseek_read(args, fd, buf, page_size, filehole_bytes, pages);

		/*
		 *  Turn non-zero data to holes
		 */
#if defined(POSIX_FADV_DONTNEED)
		(void)shim_posix_fadvise(fd, 0, filehole_bytes, POSIX_FADV_DONTNEED);
#endif
		/*
		 *  Naive defrag mode
		 */
		if (filehole_defrag) {
			if (stress_filehole_defrag(filename, &fd, buf, page_size) < 0) {
				pr_fail("%s: defrag of '%s' failed, errno=%d (%s)\n",
					args->name, filename, errno, strerror(errno));
				break;
			}
		}
		stress_filehole_non_zeros_to_holes(args, fd, buf, page_size, filehole_bytes);
	} while (stress_continue(args));

	stress_proc_state_set(args->name, STRESS_STATE_DEINIT);

	stress_metrics_set(args, "Mbytes per file (maximum)",
		max_size / (double)MB, STRESS_METRIC_GEOMETRIC_MEAN);
	stress_metrics_set(args, "blocks used per file (maximum)",
		max_blks, STRESS_METRIC_GEOMETRIC_MEAN);
	if (extents_count > 0.0) {
		extents = extents_total / extents_count;
		stress_metrics_set(args, "extents per file",
			(double)extents, STRESS_METRIC_GEOMETRIC_MEAN);
	}
tidy:
	(void)shim_unlink(filename);
	stress_proc_state_set(args->name, STRESS_STATE_DEINIT);
	if (fd != -1)
		(void)close(fd);
tidy_temp:
	(void)stress_fs_temp_dir_rm_args(args);
tidy_buf:
	(void)munmap((void *)buf, page_size * 2);
tidy_ret:
	return rc;
}

const stressor_info_t stress_filehole_info = {
	.stressor = stress_filehole,
	.classifier = CLASS_FILESYSTEM | CLASS_OS,
	.opts = opts,
	.verify = VERIFY_OPTIONAL,
	.help = help
};
#else
const stressor_info_t stress_filehole_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_FILESYSTEM | CLASS_OS,
	.opts = opts,
	.verify = VERIFY_OPTIONAL,
	.help = help,
	.unimplemented_reason = "built without fallocate() FALLOC_FL_PUNCH_HOLE or FALLOC_FL_ZERO_RANGE support"
};
#endif
