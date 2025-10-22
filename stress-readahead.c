/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2021-2025 Colin Ian King.
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
#include "core-pragma.h"

#define MIN_READAHEAD_BYTES	(1 * MB)
#define MAX_READAHEAD_BYTES	(MAX_FILE_LIMIT)
#define DEFAULT_READAHEAD_BYTES	(64 * MB)

#define BUF_ALIGNMENT		(4096)
#define BUF_SIZE		(4096)
#define MAX_OFFSETS		(16)

static const stress_help_t help[] = {
	{ NULL,	"readahead N",		"start N workers exercising file readahead" },
	{ NULL,	"readahead-bytes N",	"size of file to readahead on (default is 1GB)" },
	{ NULL,	"readahead-ops N",	"stop after N readahead bogo operations" },
	{ NULL,	NULL,			NULL }
};

static const stress_opt_t opts[] = {
	{ OPT_readahead_bytes, "readahead-bytes", TYPE_ID_UINT64_BYTES_FS, MIN_READAHEAD_BYTES, MAX_READAHEAD_BYTES, NULL },
	END_OPT,
};

#if defined(__linux__) &&	\
    NEED_GLIBC(2,3,0)

typedef uint64_t	buffer_t;

static void OPTIMIZE3 stress_readahead_generate_offsets(
	off_t *offsets,
	const uint64_t rounded_readahead_bytes)
{
	register size_t i;

	for (i = 0; i < MAX_OFFSETS; i++)
		offsets[i] = (off_t)stress_mwc64modn(rounded_readahead_bytes - BUF_SIZE) & ~(BUF_SIZE - 1);
}

static void OPTIMIZE3 stress_readahead_modify_offsets(off_t *offsets)
{
	register int i;

	for (i = 0; i < MAX_OFFSETS; i++)
		offsets[i] = ((offsets[i] * 31) >> 5) & ~(BUF_SIZE - 1);
}

static int do_readahead(
	stress_args_t *args,
	const int fd,
	const char *fs_type,
	off_t *offsets)
{
	register int i;

	for (i = 0; i < MAX_OFFSETS; i++) {
		if (readahead(fd, offsets[i], BUF_SIZE) < 0) {
			pr_fail("%s: ftruncate failed, errno=%d (%s)%s\n",
				args->name, errno, strerror(errno), fs_type);
			return -1;
		}
	}
	return 0;
}

/*
 *  stress_readahead
 *	stress file system cache via readahead calls
 */
static int stress_readahead(stress_args_t *args)
{
	buffer_t *buf = NULL;
	uint64_t rounded_readahead_bytes, i;
	uint64_t readahead_bytes, readahead_bytes_total = DEFAULT_READAHEAD_BYTES;
	uint64_t misreads = 0;
	uint64_t baddata = 0;
	int ret, rc = EXIT_FAILURE;
	char filename[PATH_MAX];
	int flags = O_CREAT | O_RDWR | O_TRUNC;
	int fd, fd_wr;
	struct stat statbuf;
	const char *fs_type;
	off_t offsets[MAX_OFFSETS] ALIGN64;
	int generate_offsets = 0;
	const bool verify = !!(g_opt_flags & OPT_FLAGS_VERIFY);

	if (!stress_get_setting("readahead-bytes", &readahead_bytes_total)) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			readahead_bytes_total = MAX_32;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			readahead_bytes_total = MIN_READAHEAD_BYTES;
	}
	if (readahead_bytes_total < MIN_READAHEAD_BYTES) {
		readahead_bytes_total = MIN_READAHEAD_BYTES;
		if (stress_instance_zero(args))
			pr_inf("%s: --readahead bytes too small, using %" PRIu64 " instead\n",
				args->name, readahead_bytes_total);
	}
	if (readahead_bytes_total > MAX_READAHEAD_BYTES) {
		readahead_bytes_total = MAX_READAHEAD_BYTES;
		if (stress_instance_zero(args))
			pr_inf("%s: --readahead-bytes too large, using %" PRIu64 " instead\n",
				args->name, readahead_bytes_total);
	}

	readahead_bytes = readahead_bytes_total / args->instances;
	if (readahead_bytes < MIN_READAHEAD_BYTES) {
		readahead_bytes = MIN_READAHEAD_BYTES;
		readahead_bytes_total = readahead_bytes * args->instances;
	}
	if (stress_instance_zero(args))
		stress_fs_usage_bytes(args, readahead_bytes, readahead_bytes_total);

	ret = stress_temp_dir_mk_args(args);
	if (ret < 0)
		return stress_exit_status(-rc);

	ret = posix_memalign((void **)&buf, BUF_ALIGNMENT, BUF_SIZE);
	if (ret || !buf) {
		rc = stress_exit_status(errno);
		pr_err("%s: cannot allocate %d byte buffer%s\n",
			args->name, BUF_SIZE, stress_get_memfree_str());
		(void)stress_temp_dir_rm_args(args);
		return rc;
	}

	(void)stress_temp_filename_args(args,
		filename, sizeof(filename), stress_mwc32());

	fd = open(filename, flags, S_IRUSR | S_IWUSR);
	if (fd < 0) {
		rc = stress_exit_status(errno);
		pr_fail("%s: open %s failed, errno=%d (%s)\n",
			args->name, filename, errno, strerror(errno));
		goto finish;
	}
	fs_type = stress_get_fs_type(filename);

	/* write-only open, ignore failure */
	fd_wr = open(filename, O_WRONLY, S_IRUSR | S_IWUSR);

	if (ftruncate(fd, (off_t)0) < 0) {
		rc = stress_exit_status(errno);
		pr_fail("%s: ftruncate failed, errno=%d (%s)%s\n",
			args->name, errno, strerror(errno), fs_type);
		goto close_finish;
	}
	(void)shim_unlink(filename);

#if defined(HAVE_POSIX_FADVISE) &&	\
    defined(POSIX_FADV_DONTNEED)
	if (posix_fadvise(fd, 0, (off_t)readahead_bytes, POSIX_FADV_DONTNEED) < 0) {
		pr_fail("%s: posix_fadvise failed, errno=%d (%s)%s\n",
			args->name, errno, strerror(errno), fs_type);
		goto close_finish;
	}

	/* Invalid lengths */
	(void)posix_fadvise(fd, 0, (off_t)~0, POSIX_FADV_DONTNEED);
	(void)posix_fadvise(fd, 0, (off_t)-1, POSIX_FADV_DONTNEED);
	/* Invalid offset */
	(void)posix_fadvise(fd, (off_t)-1, 1, POSIX_FADV_DONTNEED);
#endif

	/* Sequential Write */
	for (i = 0; i < readahead_bytes; i += BUF_SIZE) {
		ssize_t pret;
		size_t j;
		const off_t o = i / BUF_SIZE;
seq_wr_retry:
		if (UNLIKELY(!stress_continue_flag())) {
			pr_inf("%s: test expired during test setup "
				"(writing of data file)\n", args->name);
			rc = EXIT_SUCCESS;
			goto close_finish;
		}

PRAGMA_UNROLL_N(8)
		for (j = 0; j < (BUF_SIZE / sizeof(*buf)); j++)
			buf[j] = (buffer_t)(o << 12) + j;

		pret = pwrite(fd, buf, BUF_SIZE, (off_t)i);
		if (pret <= 0) {
			if ((errno == EAGAIN) || (errno == EINTR))
				goto seq_wr_retry;
			if (errno == ENOSPC)
				break;
			if (errno) {
				pr_fail("%s: pwrite failed, errno=%d (%s)%s\n",
					args->name, errno, strerror(errno), fs_type);
				goto close_finish;
			}
		}
	}

	if (shim_fstat(fd, &statbuf) < 0) {
		pr_fail("%s: fstat failed, errno=%d (%s)%s\n",
			args->name, errno, strerror(errno), fs_type);
		goto close_finish;
	}

	/* Round to write size to get no partial reads */
	rounded_readahead_bytes = (uint64_t)statbuf.st_size -
		(uint64_t)(statbuf.st_size % BUF_SIZE);

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	if (statbuf.st_size < (off_t)readahead_bytes) {
		pr_inf_skip("%s: out of free file space on %s, stressor instance %" PRIu32 " terminating early\n",
			args->name, stress_get_temp_path(), args->instance);
		rc = EXIT_NO_RESOURCE;
		goto close_finish;
	}

	stress_readahead_generate_offsets(offsets, rounded_readahead_bytes);

	do {
		if (UNLIKELY(do_readahead(args, fd, fs_type, offsets) < 0))
			goto close_finish;

		for (i = 0; i < MAX_OFFSETS; i++) {
			ssize_t pret;
rnd_rd_retry:
			if (UNLIKELY(!stress_continue(args)))
				break;

			pret = pread(fd, buf, BUF_SIZE, offsets[i]);
			if (UNLIKELY(pret <= 0)) {
				if ((errno == EAGAIN) || (errno == EINTR))
					goto rnd_rd_retry;
				if (errno) {
					pr_fail("%s: read failed, errno=%d (%s)%s at offset 0x%" PRIxMAX "\n",
						args->name, errno, strerror(errno), fs_type, (intmax_t)offsets[i]);
					goto close_finish;
				}
				continue;
			}
			if (UNLIKELY(pret != BUF_SIZE))
				misreads++;

			if (verify) {
				size_t j;
				const off_t o = offsets[i] / BUF_SIZE;

PRAGMA_UNROLL_N(8)
				for (j = 0; j < (BUF_SIZE / sizeof(*buf)); j++) {
					const buffer_t v = (buffer_t)(o << 12) + j;

					if (UNLIKELY(buf[j] != v)) {
						if (baddata == 0) {
							pr_inf("%s: first data error at offset 0x%" PRIxMAX
								", got 0x%" PRIx64 ", expecting 0x%" PRIx64 "\n",
								args->name, (intmax_t)(offsets[i] + (j * sizeof(*buf))), buf[j], v);
						}
						baddata++;
					}
				}
				if (UNLIKELY(baddata)) {
					pr_fail("%s: error in data between 0x%" PRIxMAX " and 0x%" PRIxMAX "\n",
						args->name,
						(intmax_t)offsets[i],
						(intmax_t)offsets[i] + BUF_SIZE - 1);
				}
			}
			stress_bogo_inc(args);
		}

#if defined(HAVE_POSIX_FADVISE) &&	\
    defined(POSIX_FADV_DONTNEED)
		VOID_RET(int, posix_fadvise(fd, 0, (off_t)readahead_bytes, POSIX_FADV_DONTNEED));
#endif

                /* Exercise illegal fd */
                VOID_RET(ssize_t, readahead(~0, 0, 512));

		/* Exercise zero size readahead */
                VOID_RET(ssize_t, readahead(fd, 0, 0));

		/* Exercise invalid readahead on write-only file, EBADF */
		if (fd_wr >= 0) {
			VOID_RET(ssize_t, readahead(fd_wr, 0, 512));
		}

                /* Exercise large sizes and illegal sizes */
		for (i = 15; i < sizeof(size_t) * 8; i += 4) {
			VOID_RET(ssize_t, readahead(fd, 0, 1ULL << i));
		}

		if (LIKELY(generate_offsets++ < 16)) {
			stress_readahead_modify_offsets(offsets);
		} else {
			stress_readahead_generate_offsets(offsets, rounded_readahead_bytes);
			generate_offsets = 0;
		}

	} while (stress_continue(args));

	rc = EXIT_SUCCESS;
close_finish:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
	if (fd_wr >= 0)
		(void)close(fd_wr);
	(void)close(fd);
finish:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
	free(buf);
	(void)stress_temp_dir_rm_args(args);

	if (misreads)
		pr_dbg("%s: %" PRIu64 " incomplete random reads\n",
			args->name, misreads);

	return rc;
}

const stressor_info_t stress_readahead_info = {
	.stressor = stress_readahead,
	.classifier = CLASS_IO | CLASS_OS,
	.opts = opts,
	.verify = VERIFY_OPTIONAL,
	.help = help
};
#else
const stressor_info_t stress_readahead_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_IO | CLASS_OS,
	.opts = opts,
	.verify = VERIFY_OPTIONAL,
	.help = help,
	.unimplemented_reason = "only supported on Linux"
};
#endif
