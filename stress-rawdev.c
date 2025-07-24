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
#include "core-mmap.h"
#include "core-vmstat.h"

#if defined(HAVE_SYS_SYSMACROS_H)
#include <sys/sysmacros.h>
#endif

#if defined(HAVE_SYS_MOUNT_H)
#include <sys/mount.h>
#endif

static const stress_help_t help[] = {
	{ NULL,	"rawdev N",	   "start N workers that read a raw device" },
	{ NULL,	"rawdev-method M", "specify the rawdev read method to use" },
	{ NULL,	"rawdev-ops N",	   "stop after N rawdev read operations" },
	{ NULL,	NULL,		   NULL }
};

#define	MIN_BLKSZ	((int)512)
#define	MAX_BLKSZ	((int)(128 * KB))

#if defined(HAVE_SYS_SYSMACROS_H) &&	\
    defined(BLKGETSIZE) && 		\
    defined(BLKSSZGET)

typedef int (*stress_rawdev_func)(stress_args_t *args, const int fd,
				   char *buffer, const size_t blks,
				   const size_t blksz,
				   stress_metrics_t *metrics);

typedef struct {
	const char              *name;
	const stress_rawdev_func func;
} stress_rawdev_method_info_t;

/*
 *  stress_rawdev_supported()
 *      check if we can run this as root
 */
static int stress_rawdev_supported(const char *name)
{
	if (geteuid() != 0) {
		pr_inf_skip("%s stressor will be skipped, "
			"need to be running as root for this stressor\n", name);
		return -1;
	}
	return 0;
}

/*
 *  shift_ul()
 *	shift v by shift bits, always return non-zero
 */
static inline unsigned long int shift_ul(unsigned long int v, unsigned int shift)
{
	v >>= shift;
	return (v == 0) ? 1 : v;
}

/*
 *  stress_rawdev_sweep()
 *	sweep reads across raw block device
 */
static int stress_rawdev_sweep(
	stress_args_t *args,
	const int fd,
	char *buffer,
	const size_t blks,
	const size_t blksz,
	stress_metrics_t *metrics)
{
	size_t i;
	ssize_t ret;
	double t;

	for (i = 0; LIKELY((i < blks) && stress_continue(args)); i += shift_ul(blks, 8)) {
		const off_t offset = (off_t)i * (off_t)blksz;

		t = stress_time_now();
		ret = pread(fd, buffer, blksz, offset);
		if (UNLIKELY(ret < 0)) {
			if (errno != EINTR) {
				pr_fail("%s: pread at %" PRIdMAX " failed, errno=%d (%s)\n",
					args->name, (intmax_t)offset, errno, strerror(errno));
				return -1;
			}
		} else {
			metrics->duration += stress_time_now() - t;
			metrics->count += ret;
			stress_bogo_inc(args);
		}
	}
	for (; LIKELY((i > 0) && stress_continue(args)); i -= shift_ul(blks, 8)) {
		const off_t offset = (off_t)i * (off_t)blksz;

		t = stress_time_now();
		ret = pread(fd, buffer, blksz, offset);
		if (UNLIKELY(ret < 0)) {
			if (errno != EINTR) {
				pr_fail("%s: pread at %" PRIdMAX " failed, errno=%d (%s)\n",
					args->name, (intmax_t)offset, errno, strerror(errno));
				return -1;
			}
		} else {
			metrics->duration += stress_time_now() - t;
			metrics->count += ret;
			stress_bogo_inc(args);
		}
	}
	return 0;
}

/*
 *  stress_rawdev_wiggle()
 *	sweep reads with non-linear wiggles across device
 */
static int stress_rawdev_wiggle(
	stress_args_t *args,
	const int fd,
	char *buffer,
	const size_t blks,
	const size_t blksz,
	stress_metrics_t *metrics)
{
	size_t i;
	ssize_t ret;

	for (i = shift_ul(blks, 8); LIKELY((i < blks) && stress_continue(args)); i += shift_ul(blks, 8)) {
		unsigned long int j;

		for (j = 0; LIKELY((j < shift_ul(blks, 8)) && stress_continue(args)); j += shift_ul(blks, 10)) {
			const off_t offset = (off_t)(i - j) * (off_t)blksz;
			double t;

			t = stress_time_now();
			ret = pread(fd, buffer, blksz, offset);
			if (UNLIKELY(ret < 0)) {
				if (errno != EINTR) {
					pr_fail("%s: pread at %" PRIdMAX " failed, errno=%d (%s)\n",
						args->name, (intmax_t)offset, errno, strerror(errno));
					return -1;
				}
			} else {
				metrics->duration += stress_time_now() - t;
				metrics->count += ret;
				stress_bogo_inc(args);
			}
		}
	}
	return 0;
}

/*
 *  stress_rawdev_ends()
 *	read start/end of raw device, will case excessive
 *	sweeping of heads on physical device
 */
static int stress_rawdev_ends(
	stress_args_t *args,
	const int fd,
	char *buffer,
	const size_t blks,
	const size_t blksz,
	stress_metrics_t *metrics)
{
	size_t i;

	for (i = 0; i < 128; i++) {
		ssize_t ret;
		off_t offset;
		double t;

		offset = (off_t)i * (off_t)blksz;
		t = stress_time_now();
		ret = pread(fd, buffer, blksz, offset);
		if (UNLIKELY(ret < 0)) {
			if (errno != EINTR) {
				pr_fail("%s: pread at %" PRIdMAX " failed, errno=%d (%s)\n",
					args->name, (intmax_t)offset, errno, strerror(errno));
				return -1;
			}
		} else {
			metrics->duration += stress_time_now() - t;
			metrics->count += ret;
			stress_bogo_inc(args);
		}

		offset = (off_t)(blks - (i + 1)) * (off_t)blksz;
		t = stress_time_now();
		ret = pread(fd, buffer, blksz, offset);
		if (UNLIKELY(ret < 0)) {
			if (errno != EINTR) {
				pr_fail("%s: pread at %" PRIdMAX " failed, errno=%d (%s)\n",
					args->name, (intmax_t)offset, errno, strerror(errno));
				return -1;
			}
		} else {
			metrics->duration += stress_time_now() - t;
			metrics->count += ret;
			stress_bogo_inc(args);
		}
		stress_bogo_inc(args);
	}
	return 0;
}

/*
 *  stress_rawdev_random()
 *	read at random locations across a device
 */
static int stress_rawdev_random(
	stress_args_t *args,
	const int fd,
	char *buffer,
	const size_t blks,
	const size_t blksz,
	stress_metrics_t *metrics)
{
	size_t i;

	for (i = 0; LIKELY((i < 256) && stress_continue(args)); i++) {
		ssize_t ret;
		const off_t offset = (off_t)blksz * stress_mwc64modn(blks);
		double t;

		t = stress_time_now();
		ret = pread(fd, buffer, blksz, offset);
		if (UNLIKELY(ret < 0)) {
			if (errno != EINTR) {
				pr_fail("%s: pread at %" PRIdMAX " failed, errno=%d (%s)\n",
					args->name, (intmax_t)offset, errno, strerror(errno));
				return -1;
			}
		} else {
			metrics->duration += stress_time_now() - t;
			metrics->count += ret;
			stress_bogo_inc(args);
		}
	}
	return 0;
}

/*
 *  stress_rawdev_burst()
 *	bursts of reads from random places on a device
 */
static int stress_rawdev_burst(
	stress_args_t *args,
	const int fd,
	char *buffer,
	const size_t blks,
	const size_t blksz,
	stress_metrics_t *metrics)
{
	int i;
	off_t blk = (off_t)stress_mwc64modn(blks);

	for (i = 0; LIKELY((i < 256) && stress_continue(args)); i++) {
		ssize_t ret;
		const off_t offset = blk * (off_t)blksz;
		double t;

		t = stress_time_now();
		ret = pread(fd, buffer, blksz, offset);
		if (UNLIKELY(ret < 0)) {
			if (errno != EINTR) {
				pr_fail("%s: pread at %" PRIdMAX " failed, errno=%d (%s)\n",
					args->name, (intmax_t)offset, errno, strerror(errno));
				return -1;
			}
		} else {
			metrics->duration += stress_time_now() - t;
			metrics->count += ret;
			stress_bogo_inc(args);
		}
		blk++;
		if (blk >= (off_t)blks)
			blk = 0;
	}
	return 0;
}

static int stress_rawdev_all(
	stress_args_t *args,
	const int fd,
	char *buffer,
	const size_t blks,
	const size_t blksz,
	stress_metrics_t *metrics);

/*
 *  rawdev methods
 */
static const stress_rawdev_method_info_t rawdev_methods[] = {
	{ "all",	stress_rawdev_all },
	{ "sweep",	stress_rawdev_sweep },
	{ "wiggle",	stress_rawdev_wiggle },
	{ "ends",	stress_rawdev_ends },
	{ "random",	stress_rawdev_random },
	{ "burst",	stress_rawdev_burst },
};

/*
 *  stress_rawdev_all()
 *      iterate over all rawdev methods
 */
static int stress_rawdev_all(
	stress_args_t *args,
	const int fd,
	char *buffer,
	const size_t blks,
	const size_t blksz,
	stress_metrics_t *metrics)
{
	static size_t i = 1;       /* Skip over stress_rawdev_all */
	int ret;

	ret = rawdev_methods[i].func(args, fd, buffer, blks, blksz, &metrics[i]);

	metrics[0].duration += metrics[i].duration;
	metrics[0].count += metrics[i].count;

	i++;
	if (i >= SIZEOF_ARRAY(rawdev_methods))
		i = 1;

	return ret;
}

static const char *stress_rawdev_method(const size_t i)
{
	return (i < SIZEOF_ARRAY(rawdev_methods)) ? rawdev_methods[i].name : NULL;
}

static const stress_opt_t opts[] = {
	{ OPT_rawdev_method, "rawdev-method", TYPE_ID_SIZE_T_METHOD, 0, 0, stress_rawdev_method },
	END_OPT,
};

#else

static const stress_opt_t opts[] = {
	{ OPT_rawdev_method, "rawdev-method", TYPE_ID_SIZE_T_METHOD, 0, 0, stress_unimplemented_method },
	END_OPT,
};

#endif

#if defined(HAVE_SYS_SYSMACROS_H) &&	\
    defined(BLKGETSIZE) && 		\
    defined(BLKSSZGET)

static int stress_rawdev(stress_args_t *args)
{
	int ret, fd, rc = EXIT_SUCCESS;
	char *devpath, *buffer;
	const char *path = stress_get_temp_path();
	size_t blks, blksz = 0, mmapsz;
	size_t i, j, rawdev_method = 0;
	const size_t page_size = args->page_size;
	stress_rawdev_func func;
	stress_metrics_t *metrics;

	metrics = (stress_metrics_t *)calloc(SIZEOF_ARRAY(rawdev_methods), sizeof(*metrics));
	if (!metrics) {
		pr_inf_skip("%s: cannot allocate %zu byte metrics table%s, "
			"skipping stressor\n",
			args->name, SIZEOF_ARRAY(rawdev_methods),
			stress_get_memfree_str());
		return EXIT_NO_RESOURCE;
	}

	stress_zero_metrics(metrics, SIZEOF_ARRAY(rawdev_methods));

	if (!path) {
		pr_inf("%s: cannot determine temporary path\n",
			args->name);
		free(metrics);
		return EXIT_NO_RESOURCE;
	}
	devpath = stress_find_mount_dev(path);
	if (!devpath) {
		pr_inf("%s: cannot determine raw block device\n",
			args->name);
		free(metrics);
		return EXIT_NO_RESOURCE;
	}

	(void)stress_get_setting("rawdev-method", &rawdev_method);
	func = rawdev_methods[rawdev_method].func;

	fd = open(devpath, O_RDONLY | O_NONBLOCK);
	if (fd < 0) {
		pr_inf("%s: cannot open raw block device, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		free(metrics);
		return EXIT_NO_RESOURCE;
	}
	ret = ioctl(fd, BLKGETSIZE, &blks);
	if (ret < 0) {
		pr_inf("%s: cannot get block size, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		(void)close(fd);
		free(metrics);
		return EXIT_NO_RESOURCE;
	}
	ret = ioctl(fd, BLKSSZGET, &blksz);
	if (ret < 0) {
		pr_inf("%s: cannot get block size, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		(void)close(fd);
		free(metrics);
		return EXIT_NO_RESOURCE;
	}
	/* Truncate if blksize looks too big */
	if (blksz > MAX_BLKSZ)
		blksz = MAX_BLKSZ;
	if (blksz < MIN_BLKSZ)
		blksz = MIN_BLKSZ;

	mmapsz = ((blksz + page_size - 1) & ~(page_size - 1));
	buffer = stress_mmap_populate(NULL, mmapsz,
			PROT_READ | PROT_WRITE,
			MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	if (buffer == MAP_FAILED) {
		pr_inf("%s: failed to mmap buffer of %zu bytes with %zu allocations%s\n",
			args->name, blksz, mmapsz, stress_get_memfree_str());
		(void)close(fd);
		free(metrics);
		return EXIT_NO_RESOURCE;
	}
	stress_set_vma_anon_name(buffer, mmapsz, "io-buffer");

	(void)close(fd);
	fd = open(devpath, O_RDONLY | O_DIRECT);
	if (fd < 0) {
		pr_inf("%s: cannot open raw block device, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		(void)munmap((void *)buffer, mmapsz);
		free(metrics);
		return EXIT_NO_RESOURCE;
	}

	if (stress_instance_zero(args))
		pr_dbg("%s: exercising %s (%zd blocks of size %zd bytes)\n",
			args->name, devpath, blks, blksz);

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		if (func(args, fd, buffer, blks, blksz, &metrics[rawdev_method]) < 0) {
			rc = EXIT_FAILURE;
			break;
		}
	} while (stress_continue(args));

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	for (i = 0, j = 0; i < SIZEOF_ARRAY(rawdev_methods); i++) {
		const double duration = metrics[i].duration;

		if (duration > 0.0) {
			char str[50];
			const double rate = (metrics[i].count / duration) / (double)MB;

			(void)snprintf(str, sizeof(str), "MB per sec read rate (%s)", rawdev_methods[i].name);
			stress_metrics_set(args, j, str,
				rate, STRESS_METRIC_HARMONIC_MEAN);
			j++;
		}
	}

	(void)munmap((void *)buffer, mmapsz);
	(void)close(fd);
	free(metrics);

	return rc;
}

const stressor_info_t stress_rawdev_info = {
	.stressor = stress_rawdev,
	.supported = stress_rawdev_supported,
	.classifier = CLASS_IO,
	.opts = opts,
	.verify = VERIFY_ALWAYS,
	.help = help
};
#else
const stressor_info_t stress_rawdev_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_IO,
	.opts = opts,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.unimplemented_reason = "built without sys/sysmacros.h or undefined BLKGETSIZE, BLKSSZGET"
};
#endif
