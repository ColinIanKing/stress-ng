/*
 * Copyrignt (C) 2023-2024 Colin Ian King.
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
#include "core-hash.h"
#include "core-killpid.h"
#include "core-mmap.h"

#define MIN_METAMIX_BYTES		(512)
#define MAX_METAMIX_BYTES		(MAX_FILE_LIMIT)
#define DEFAULT_METAMIX_BYTES		(1 * MB)

#define METAMIX_PROCS			(15)
#define METAMIX_WRITES			(256)

typedef struct {
	off_t	 offset;		/* seek offset */
	size_t	 data_len;		/* length of data written */
	uint32_t checksum;		/* checksum of data */
	bool	 valid;			/* true of data written sanely */
} file_info_t;

static const stress_help_t help[] = {
	{ NULL,	"metamix N",	 	"start N workers that have a mix of file metadata operations" },
	{ NULL,	"metamix-bytes N",	"write N bytes per metamix file (default is 1MB, 16 files per instance)" },
	{ NULL,	"metamix-ops N",	"stop metamix workers after N metamix bogo operations" },
	{ NULL, NULL,		 	NULL }
};

static void *counter_lock;

/*
 *  stress_metamix_cmp()
 *	sort by checksum to get randomized seek read ordering
 */
static int stress_metamix_cmp(const void *p1, const void *p2)
{
	const file_info_t *w1 = (const file_info_t *)p1;
	const file_info_t *w2 = (const file_info_t *)p2;

	if (w1->checksum < w2->checksum)
		return 1;
	else if (w1->checksum > w2->checksum)
		return -1;
	else
		return 0;
}

/*
 *  stress_metamix_file()
 *	try to mimic Lucene's access patterns
 */
static int stress_metamix_file(
	stress_args_t *args,
	const char *temp_dir,
	const char *fs_type,
	const uint32_t instance,
	const off_t metamix_bytes)
{
	char filename[PATH_MAX];
	file_info_t file_info[METAMIX_WRITES];
	size_t i, n;
	off_t offset = (metamix_bytes > (off_t)args->page_size) ?
		(off_t)stress_mwc16modn(metamix_bytes >> 2) : 0;
	off_t end = (off_t)0;
	const size_t min_data_len = sizeof(uint32_t);
	const size_t checksum_len = sizeof(file_info[0].checksum);
	const size_t buf_len = 256 + min_data_len + checksum_len;
	const size_t max_seek = metamix_bytes / METAMIX_WRITES;
	const off_t page_mask = ~(off_t)(args->page_size - 1);
	const bool verify = !!(g_opt_flags & OPT_FLAGS_VERIFY);
	int ret, fd, rc = EXIT_SUCCESS;
	uint8_t buf[buf_len];
	struct stat statbuf;

	stress_temp_filename(filename, sizeof(filename), args->name,
                args->pid, args->instance, stress_mwc32() ^ instance);
	if ((fd = open(filename, O_WRONLY | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR)) < 0) {
		ret = stress_exit_status(errno);
		pr_fail("%s: open for write %s failed, errno=%d (%s)%s\n",
			args->name, filename, errno, strerror(errno), fs_type);
		return ret;
	}

	stress_file_rw_hint_short(fd);

	(void)shim_memset(file_info, 0, sizeof(file_info));

	for (n = 0; n < METAMIX_WRITES; n++) {
		const size_t data_len = stress_mwc8modn(max_seek) + min_data_len;
		off_t bytes_left;

		file_info[n].offset = offset;
		file_info[n].data_len = data_len;

		if (UNLIKELY(lseek(fd, offset, SEEK_SET) < 0)) {
			pr_fail("%s: write: lseek %s failed, errno=%d (%s)%s\n",
				args->name, filename, errno, strerror(errno), fs_type);
			rc = EXIT_FAILURE;
			goto err_close;
		}
		stress_rndbuf(buf, data_len);

		/*
		 *  In verify mode we hash for a checksum and use that for read
		 *  sort ordering, in non-verify mode we use a 32 bit random
		 *  number in the checksum for read sort ordering
		 */
		if (verify)
			file_info[n].checksum = stress_hash_jenkin(buf, data_len);
		else
			file_info[n].checksum = stress_mwc32();

		if (UNLIKELY(write(fd, buf, data_len) != (ssize_t)data_len))
			break;

		offset += data_len;
		end = offset;

		bytes_left = (metamix_bytes - offset) / (METAMIX_WRITES - n);
		file_info[n].valid = true;

		if (offset > metamix_bytes)
			break;

		/* Occasionally force offset to be page aligned */
		if (((n % (METAMIX_WRITES >> 2)) == 0) && ((metamix_bytes - offset) > (off_t)args->page_size)) {
			offset += bytes_left;
			offset = (offset & page_mask) + args->page_size;
		} else {
			offset += bytes_left;
		}
	}
	if (stress_mwc8() > 240) {
		if (shim_fdatasync(fd) < 0) {
			if (UNLIKELY((errno != EINTR) && (errno != ENOSYS))) {
				pr_inf("%s: fdatasync on %s failed, errno=%d (%s)%s\n",
					args->name, filename, errno, strerror(errno), fs_type);
				rc = EXIT_FAILURE;
				goto err_close;
			}
		}
	}
	(void)close(fd);

	/*
	 *  stat/lstat 50/50% random choice
	 */
	if (stress_mwc1()) {
		if (UNLIKELY(shim_stat(filename, &statbuf) < 0)) {
			pr_fail("%s: stat on %s failed, errno=%d (%s)%s\n",
				args->name, filename, errno, strerror(errno), fs_type);
			rc = EXIT_FAILURE;
			goto err_unlink;
		}
	} else {
		if (UNLIKELY(shim_lstat(filename, &statbuf) < 0)) {
			pr_fail("%s: lstat on %s failed, errno=%d (%s)%s\n",
				args->name, filename, errno, strerror(errno), fs_type);
			rc = EXIT_FAILURE;
			goto err_unlink;
		}
	}

	if (UNLIKELY((intmax_t)statbuf.st_size != (intmax_t)end)) {
		pr_fail("%s: stat on %s, expecting file size %" PRIdMAX ", got %" PRIdMAX "\n",
			args->name, filename, (intmax_t)end, (intmax_t)statbuf.st_size);
		rc = EXIT_FAILURE;
		goto err_unlink;
	}

	fd = open(filename, O_RDONLY);
	if (UNLIKELY(fd < 0)) {
		pr_fail("%s: open for read %s failed, errno=%d (%s)%s\n",
			args->name, filename, errno, strerror(errno), fs_type);
		rc = EXIT_FAILURE;
		goto err_unlink;
	}
	if (shim_fdatasync(fd) < 0) {
		if (UNLIKELY((errno != EINTR) && (errno != ENOSYS) && (errno != EBADF))) {
			pr_inf("%s: fdatasync on %s failed, errno=%d (%s)%s\n",
				args->name, filename, errno, strerror(errno), fs_type);
			rc = EXIT_FAILURE;
			goto err_close;
		}
	}
	(void)close(fd);

#if defined(O_DIRECTORY)
	fd = open(temp_dir, O_RDONLY | O_DIRECTORY);
	if (fd != -1) {
		if (shim_fsync(fd) < 0) {
			if (UNLIKELY((errno != EINTR) && (errno != ENOSYS) && (errno != EBADF))) {
				pr_inf("%s: fsync on directory %s failed, errno=%d (%s)%s\n",
					args->name, temp_dir, errno, strerror(errno), fs_type);
				rc = EXIT_FAILURE;
				(void)close(fd);
				goto err_unlink;
			}
		}
		(void)close(fd);
	}
#else
	(void)temp_dir;
#endif

	/* Re-order seek position in quasi-random order */
	qsort((void *)file_info, n, sizeof(file_info_t), stress_metamix_cmp);

	fd = open(filename, O_RDONLY);
	if (UNLIKELY(fd < 0)) {
		pr_fail("%s: open for read %s failed, errno=%d (%s)%s\n",
			args->name, filename, errno, strerror(errno), fs_type);
		rc = EXIT_FAILURE;
		goto err_unlink;
	}
	for (i = 0; i < n; i++) {
		ssize_t rret;
		uint32_t checksum;
		const size_t data_len = file_info[i].data_len;

		if (UNLIKELY(lseek(fd, file_info[i].offset, SEEK_SET) < 0)) {
			pr_fail("%s: read: lseek %s failed, errno=%d (%s)%s\n",
				args->name, filename, errno, strerror(errno), fs_type);
			rc = EXIT_FAILURE;
			goto err_close;
		}

		rret = read(fd, buf, data_len);
		if (UNLIKELY(rret < 0)) {
			pr_fail("%s: read failure, errno=%d (%s)%s\n",
				args->name, errno, strerror(errno), fs_type);
			rc = EXIT_FAILURE;
			goto err_close;
		}
		if (UNLIKELY(rret != (ssize_t)data_len)) {
			pr_fail("%s: read failure, expected %zu bytes, got %zd bytes\n",
				args->name, data_len, rret);
			rc = EXIT_FAILURE;
			goto err_close;
		}

		if (verify) {
			checksum = stress_hash_jenkin(buf, data_len);
			if (UNLIKELY(checksum != file_info[i].checksum)) {
				pr_fail("%s: read failure, expected checksum 0x%" PRIx32 ", got 0x%" PRIx32 "\n",
					args->name, file_info[i].checksum, checksum);
				rc = EXIT_FAILURE;
				goto err_close;
			}
		}

		/* Page aligned data means we can mmap it and check it */
		if ((file_info[i].offset & page_mask) == file_info[i].offset) {
			void *ptr;

			ptr = stress_mmap_populate(NULL, args->page_size, PROT_READ, MAP_PRIVATE, fd, file_info[i].offset);
			if (ptr != MAP_FAILED) {
				if (verify && (data_len < args->page_size)) {
					checksum = stress_hash_jenkin(ptr, data_len);
					(void)munmap(ptr, args->page_size);

					if (UNLIKELY(checksum != file_info[i].checksum)) {
						pr_fail("%s: read failure, expected checksum 0x%" PRIx32 ", got 0x%" PRIx32 "\n",
							args->name, file_info[i].checksum, checksum);
						rc = EXIT_FAILURE;
						goto err_close;
					}
				} else {
					(void)munmap(ptr, args->page_size);
				}
			}
		}
	}
	if (shim_lstat(filename, &statbuf) < 0) {
		pr_fail("%s: lstat on %s failed, errno=%d (%s)%s\n",
			args->name, filename, errno, strerror(errno), fs_type);
		rc = EXIT_FAILURE;
		goto err_close;
	}
	if ((intmax_t)statbuf.st_size != (intmax_t)end) {
		pr_fail("%s: stat on %s, expecting file size %" PRIdMAX ", got %" PRIdMAX "\n",
			args->name, filename, (intmax_t)end, (intmax_t)statbuf.st_size);
		rc = EXIT_FAILURE;
		goto err_close;
	}

err_close:
	(void)close(fd);
err_unlink:
	(void)shim_unlink(filename);

	return rc;
}

/*
 *  stress_metamix
 *	stress metadata patterns using an emulation of
 *	Lucern's file access method, cf:
 *	https://github.com/ColinIanKing/stress-ng/issues/316
 */
static int stress_metamix(stress_args_t *args)
{
	int ret;
	off_t metamix_bytes, metamix_bytes_total = DEFAULT_METAMIX_BYTES;
	size_t i;
	stress_pid_t *s_pids, *s_pids_head = NULL;
	uint32_t w, z;
	char temp_dir[PATH_MAX];
	const char *fs_type;

	if (stress_sigchld_set_handler(args) < 0)
		return EXIT_NO_RESOURCE;

	s_pids = stress_sync_s_pids_mmap(METAMIX_PROCS);
	if (s_pids == MAP_FAILED) {
		pr_inf_skip("%s: failed to mmap %d PIDs%s, skipping stressor\n",
			args->name, METAMIX_PROCS, stress_get_memfree_str());
		return EXIT_NO_RESOURCE;
	}

	counter_lock = stress_lock_create("counter");
	if (!counter_lock) {
		pr_inf_skip("%s: failed to create counter lock. skipping stressor\n", args->name);
		ret = EXIT_NO_RESOURCE;
		goto tidy_s_pids;
	}

	(void)stress_get_setting("metamix-bytes", &metamix_bytes_total);
	metamix_bytes = metamix_bytes_total / args->instances;
	if (metamix_bytes < MIN_METAMIX_BYTES) {
		metamix_bytes = MIN_METAMIX_BYTES;
		metamix_bytes_total = metamix_bytes * args->instances;
	}
	if (stress_instance_zero(args))
		stress_fs_usage_bytes(args, metamix_bytes, metamix_bytes_total);

	stress_temp_dir_args(args, temp_dir, sizeof(temp_dir));
	ret = stress_temp_dir_mk_args(args);
	if (ret < 0) {
		ret = stress_exit_status(-ret);
		goto lock_destroy;
	}
	fs_type = stress_get_fs_type(temp_dir);

	stress_mwc_get_seed(&w, &z);

	for (i = 0; i < METAMIX_PROCS; i++) {
		(void)stress_mwc_set_seed(w ^ i, z + i);
		(void)stress_mwc32();

		stress_sync_start_init(&s_pids[i]);

		s_pids[i].pid = fork();
		if (s_pids[i].pid < 0) {
			goto reap;
		} else if (s_pids[i].pid == 0) {
			/* Child */
			stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
			s_pids[i].pid = getpid();
			stress_sync_start_wait_s_pid(&s_pids[i]);
			stress_set_proc_state(args->name, STRESS_STATE_RUN);

			(void)sched_settings_apply(true);

			do {
				ret = stress_metamix_file(args, temp_dir, fs_type, (uint32_t)i, metamix_bytes);
			} while (stress_bogo_inc_lock(args, counter_lock, true) && (ret == EXIT_SUCCESS));
			_exit(ret);
		} else {
			stress_sync_start_s_pid_list_add(&s_pids_head, &s_pids[i]);
		}
	}

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_sync_start_cont_list(s_pids_head);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		ret = stress_metamix_file(args, temp_dir, fs_type, (uint32_t)i, metamix_bytes);
	} while (stress_bogo_inc_lock(args, counter_lock, true) && (ret == EXIT_SUCCESS));

reap:
	if (stress_kill_and_wait_many(args, s_pids, METAMIX_PROCS, SIGALRM, true) != EXIT_SUCCESS)
		ret = EXIT_FAILURE;

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
	(void)stress_temp_dir_rm_args(args);
lock_destroy:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
	(void)stress_lock_destroy(counter_lock);
tidy_s_pids:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
	(void)stress_sync_s_pids_munmap(s_pids, METAMIX_PROCS);

	return ret;
}

static const stress_opt_t opts[] = {
	{ OPT_metamix_bytes, "metamix-bytes", TYPE_ID_OFF_T, MIN_METAMIX_BYTES, MAX_METAMIX_BYTES, NULL },
	END_OPT,
};

const stressor_info_t stress_metamix_info = {
	.stressor = stress_metamix,
	.classifier = CLASS_FILESYSTEM | CLASS_OS,
	.opts = opts,
	.verify = VERIFY_OPTIONAL,
	.help = help
};
