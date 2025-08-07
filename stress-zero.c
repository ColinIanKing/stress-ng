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
#include "core-helper.h"
#include "core-madvise.h"
#include "core-mmap.h"
#include "core-pragma.h"

#include <sys/ioctl.h>

#if defined(HAVE_LINUX_FS_H)
#include <linux/fs.h>
#endif

static const stress_help_t help[] = {
	{ NULL,	"zero N",	"start N workers exercising /dev/zero with read, mmap, ioctl, lseek" },
	{ NULL, "zero-read",	"just exercise /dev/zero with reading" },
	{ NULL,	"zero-ops N",	"stop after N /dev/zero bogo read operations" },
	{ NULL,	NULL,		NULL }
};

#if defined(__linux__)

typedef struct {
	const int flag;
	const char *flag_str;
} mmap_flags_t;

#define MMAP_FLAG_INFO(x)	{ x, # x }

/*
 *  A subset of mmap flags to exercise on /dev/zero mmap'ings
 */
static const mmap_flags_t mmap_flags[] = {
	MMAP_FLAG_INFO(MAP_PRIVATE | MAP_ANONYMOUS),
	MMAP_FLAG_INFO(MAP_SHARED | MAP_ANONYMOUS),
#if defined(MAP_LOCKED)
	MMAP_FLAG_INFO(MAP_PRIVATE | MAP_ANONYMOUS | MAP_LOCKED),
	MMAP_FLAG_INFO(MAP_SHARED | MAP_ANONYMOUS | MAP_LOCKED),
#endif
#if defined(MAP_POPULATE)
	MMAP_FLAG_INFO(MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE),
	MMAP_FLAG_INFO(MAP_SHARED | MAP_ANONYMOUS | MAP_POPULATE),
#endif
};
#endif

/*
 *  stress_zero
 *	stress reading of /dev/zero
 */
static int stress_zero(stress_args_t *args)
{
	int fd, rc = EXIT_SUCCESS;
	double duration = 0.0, rate;
	uint64_t bytes = 0ULL;
	const size_t page_size = args->page_size;
	void *rd_buffer, *wr_buffer;
	bool zero_read = false;
#if defined(__minix__)
	const int flags = O_RDONLY;
#else
	const int flags = O_RDWR;
#endif
	(void)stress_get_setting("zero-read", &zero_read);

	rd_buffer = stress_mmap_populate(NULL, page_size,
			PROT_READ | PROT_WRITE,
			MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	if (rd_buffer == MAP_FAILED) {
		pr_inf_skip("%s: failed to mmap %zu byte read buffer%s, "
			"errno=%d (%s), skipping stressor\n",
			args->name, page_size,
			stress_get_memfree_str(), errno, strerror(errno));
		return EXIT_NO_RESOURCE;
	}
	stress_set_vma_anon_name(rd_buffer, page_size, "read-buffer");
	(void)stress_madvise_mergeable(rd_buffer, page_size);

	wr_buffer = stress_mmap_populate(NULL, page_size,
			PROT_READ | PROT_WRITE,
			MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	if (wr_buffer == MAP_FAILED) {
		pr_inf_skip("%s: failed to mmap %zu byte write buffer%s, "
			"errno=%d (%s), skipping stressor\n",
			args->name, page_size,
			stress_get_memfree_str(), errno, strerror(errno));
		(void)munmap(rd_buffer, page_size);
		return EXIT_NO_RESOURCE;
	}
	stress_set_vma_anon_name(wr_buffer, page_size, "write-buffer");
	(void)stress_madvise_mergeable(wr_buffer, page_size);

	if ((fd = open("/dev/zero", flags)) < 0) {
		pr_fail("%s: open /dev/zero failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		(void)munmap(wr_buffer, page_size);
		(void)munmap(rd_buffer, page_size);
		return EXIT_FAILURE;
	}

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	if (zero_read) {
		double t;
		ssize_t ret = 0;

		if (stress_instance_zero(args))
			pr_inf("%s: exercising /dev/zero with just reads\n", args->name);

		t = stress_time_now();
		do {
			ret = read(fd, rd_buffer, page_size);
			if (UNLIKELY(ret < 0)) {
				if ((errno == EAGAIN) || (errno == EINTR))
					continue;
				pr_fail("%s: read failed, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
				(void)close(fd);
				(void)munmap(wr_buffer, page_size);
				(void)munmap(rd_buffer, page_size);
				return EXIT_FAILURE;
			}
			stress_bogo_inc(args);
			bytes += ret;
		} while (stress_continue(args));
		duration += stress_time_now() - t;

		if ((ret > 0) && stress_data_is_not_zero((uint64_t *)rd_buffer, (size_t)ret)) {
			pr_fail("%s: non-zero value from a read of /dev/zero\n",
				args->name);
			rc = EXIT_FAILURE;
		}
	} else {
#if defined(__linux__)
		int mmap_counter = 0;
		size_t mmap_index = 0;
#endif

		if (stress_instance_zero(args))
			pr_inf("%s: exercising /dev/zero with reads, mmap, lseek, and ioctl; for just read benchmarking use --zero-read\n",
				args->name);
		do {
			ssize_t ret = 0;
			size_t i;
			double t;

			t = stress_time_now();
			for (i = 0; LIKELY((i < 1024) && stress_continue(args)); i++) {
				ret = read(fd, rd_buffer, page_size);
				if (UNLIKELY(ret < 0)) {
					if ((errno == EAGAIN) || (errno == EINTR))
						continue;
					pr_fail("%s: read failed, errno=%d (%s)\n",
						args->name, errno, strerror(errno));
					(void)close(fd);
					(void)munmap(wr_buffer, page_size);
					(void)munmap(rd_buffer, page_size);
					return EXIT_FAILURE;
				}
				stress_bogo_inc(args);
				bytes += ret;
			}
			duration += stress_time_now() - t;

			if ((ret > 0) && stress_data_is_not_zero((uint64_t *)rd_buffer, (size_t)ret)) {
				pr_fail("%s: non-zero value from a read of /dev/zero\n",
					args->name);
				rc = EXIT_FAILURE;
			}
#if !defined(__minix__)
			/* One can also write to /dev/zero w/o failure */
			ret = write(fd, wr_buffer, page_size);
			if (UNLIKELY(ret < 0)) {
				if ((errno == EAGAIN) || (errno == EINTR))
					continue;
				pr_fail("%s: write failed, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
				(void)close(fd);
				(void)munmap(wr_buffer, page_size);
				(void)munmap(rd_buffer, page_size);
				return EXIT_FAILURE;
			}
#endif

#if defined(__linux__)
			/*
			 *  Periodically exercise mmap
			 */
			if (mmap_counter++ > 500) {
				mmap_counter = 0;
				int32_t *ptr;

				/*
				 *  check if we can mmap /dev/zero
				 */
				ptr = (int32_t *)mmap(NULL, page_size, PROT_READ, mmap_flags[mmap_index].flag,
					fd, (off_t)(page_size * stress_mwc16()));
				if (UNLIKELY(ptr == MAP_FAILED)) {
					if ((errno == ENOMEM) || (errno == EAGAIN))
						continue;
					pr_fail("%s: mmap /dev/zero using %s failed, errno=%d (%s)\n",
						args->name, mmap_flags[i].flag_str, errno, strerror(errno));
					(void)close(fd);
					(void)munmap(wr_buffer, page_size);
					(void)munmap(rd_buffer, page_size);
					return EXIT_FAILURE;
				}
				if (stress_data_is_not_zero((uint64_t *)rd_buffer, (size_t)ret)) {
					pr_fail("%s: memory mapped page of /dev/zero using %s is not zero\n",
						args->name, mmap_flags[i].flag_str);
				}
				(void)stress_munmap_force(ptr, page_size);
				mmap_index++;
				if (mmap_index >= SIZEOF_ARRAY(mmap_flags))
					mmap_index = 0;
			}
#endif
			/*
			 *  lseek on /dev/zero just because we can
			 */
			(void)lseek(fd, SEEK_SET, 0);
			(void)lseek(fd, SEEK_END, 0);
			(void)lseek(fd, SEEK_CUR, 0);

#if defined(FIONBIO)
			{
				int opt;

				opt = 1;
				VOID_RET(int, ioctl(fd, FIONBIO, &opt));
				opt = 0;
				VOID_RET(int, ioctl(fd, FIONBIO, &opt));
			}
#endif
#if defined(FIONREAD)
			{
				int isz = 0;

				/* Should be inappropriate ioctl */
				VOID_RET(int, ioctl(fd, FIONREAD, &isz));
			}
#endif
#if defined(FIGETBSZ)
			{
				int isz = 0;

				VOID_RET(int, ioctl(fd, FIGETBSZ, &isz));
			}
#endif
			stress_bogo_inc(args);
		} while ((rc == EXIT_SUCCESS) && stress_continue(args));
	}
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
	(void)close(fd);

	(void)munmap(wr_buffer, page_size);
	(void)munmap(rd_buffer, page_size);

	rate = (duration > 0.0) ? ((double)bytes / duration) / (double)MB : 0.0;
	stress_metrics_set(args, 0, "MB per sec /dev/zero read rate",
		rate, STRESS_METRIC_HARMONIC_MEAN);

	return rc;
}

static const stress_opt_t opts[] = {
        { OPT_zero_read, "zero-read", TYPE_ID_BOOL, 0, 1, NULL },
	END_OPT,
};

const stressor_info_t stress_zero_info = {
	.stressor = stress_zero,
	.classifier = CLASS_DEV | CLASS_MEMORY | CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.opts = opts,
	.help = help
};
