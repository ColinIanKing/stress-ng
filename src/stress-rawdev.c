/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
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
	{ NULL,	"rawdev N",	   "start N workers that read a raw device" },
	{ NULL,	"rawdev-ops N",	   "stop after N rawdev read operations" },
	{ NULL,	"rawdev-method M", "specify the rawdev reead method to use" },
	{ NULL,	NULL,		   NULL }
};


#define	MIN_BLKSZ	((int)512)
#define	MAX_BLKSZ	((int)(128 * KB))

#if defined(HAVE_SYS_SYSMACROS_H) &&	\
    defined(BLKGETSIZE) && 		\
    defined(BLKSSZGET)

typedef void (*stress_rawdev_func)(const stress_args_t *args, const int fd,
				   char *buffer, const size_t blks,
				   const size_t blksz);

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
		pr_inf("%s flood stressor will be skipped, "
			"need to be running as root for this stressor\n", name);
		return -1;
	}
	return 0;
}

/*
 *  shift_ul()
 *	shift v by shift bits, always return non-zero
 */
static inline unsigned long shift_ul(unsigned long v, unsigned int shift)
{
	v >>= shift;
	return (v == 0) ? 1 : v;
}

/*
 *  stress_rawdev_path()
 */
static char *stress_rawdev_path(
	const dev_t dev,
	char *devpath,
	const size_t devpath_len)
{
	DIR *dir;
	struct dirent *d;
	const dev_t majdev = makedev(major(dev), 0);

	dir = opendir("/dev");
	if (!dir)
		return NULL;

	while ((d = readdir(dir)) != NULL) {
		int ret;
		struct stat stat_buf;

		stress_mk_filename(devpath, devpath_len, "/dev", d->d_name);
		ret = stat(devpath, &stat_buf);
		if ((ret == 0) &&
		    (S_ISBLK(stat_buf.st_mode)) &&
		    (stat_buf.st_rdev == majdev)) {
			(void)closedir(dir);
			return devpath;
		}
	}
	(void)closedir(dir);

	return NULL;
}

/*
 *  stress_rawdev_sweep()
 *	sweep reads across raw block device
 */
static void stress_rawdev_sweep(
	const stress_args_t *args,
	const int fd,
	char *buffer,
	const size_t blks,
	const size_t blksz)
{
	size_t i;
	int ret;
	off_t offset;

	for (i = 0; i < blks && keep_stressing(args); i += shift_ul(blks, 8)) {
		offset = (off_t)i * (off_t)blksz;
		ret = pread(fd, buffer, blksz, offset);
		if (ret < 0) {
			pr_err("%s: pread at %ju failed, errno=%d (%s)\n",
				args->name, (intmax_t)offset, errno, strerror(errno));
		}
		inc_counter(args);
	}
	for (; i > 0 && keep_stressing(args); i -= shift_ul(blks, 8)) {
		offset = (off_t)i * (off_t)blksz;
		ret = pread(fd, buffer, blksz, offset);
		if (ret < 0) {
			pr_err("%s: pread at %ju failed, errno=%d (%s)\n",
				args->name, (intmax_t)offset, errno, strerror(errno));
		}
		inc_counter(args);
	}
}

/*
 *  stress_rawdev_wiggle()
 *	sweep reads with non-linear wiggles across device
 */
static void stress_rawdev_wiggle(
	const stress_args_t *args,
	const int fd,
	char *buffer,
	const size_t blks,
	const size_t blksz)
{
	size_t i;
	int ret;
	off_t offset;

	for (i = shift_ul(blks, 8); i < blks && keep_stressing(args); i += shift_ul(blks, 8)) {
		unsigned long j;

		for (j = 0; j < shift_ul(blks, 8) && keep_stressing(args); j += shift_ul(blks, 10)) {
			offset = (off_t)(i - j) * (off_t)blksz;
			ret = pread(fd, buffer, blksz, offset);
			if (ret < 0) {
				pr_err("%s: pread at %ju failed, errno=%d (%s)\n",
					args->name, (intmax_t)offset, errno, strerror(errno));
			}
			inc_counter(args);
		}
	}
}

/*
 *  stress_rawdev_ends()
 *	read start/end of raw device, will case excessive
 *	sweeping of heads on physical device
 */
static void stress_rawdev_ends(
	const stress_args_t *args,
	const int fd,
	char *buffer,
	const size_t blks,
	const size_t blksz)
{
	size_t i;
	off_t offset;

	for (i = 0; i < 128; i++) {
		int ret;

		offset = (off_t)i * (off_t)blksz;
		ret = pread(fd, buffer, blksz, offset);
		if (ret < 0) {
			pr_err("%s: pread at %ju failed, errno=%d (%s)\n",
				args->name, (intmax_t)offset, errno, strerror(errno));
		}
		inc_counter(args);

		offset = (off_t)(blks - (i + 1)) * (off_t)blksz;
		ret = pread(fd, buffer, blksz, offset);
		if (ret < 0) {
			pr_err("%s: pread at %ju failed, errno=%d (%s)\n",
				args->name, (intmax_t)offset, errno, strerror(errno));
		}
		inc_counter(args);
	}
}

/*
 *  stress_rawdev_random()
 *	read at random locations across a device
 */
static void stress_rawdev_random(
	const stress_args_t *args,
	const int fd,
	char *buffer,
	const size_t blks,
	const size_t blksz)
{
	size_t i;

	for (i = 0; i < 256 && keep_stressing(args); i++) {
		int ret;
		off_t offset = (off_t)blksz * (stress_mwc64() % blks);

		ret = pread(fd, buffer, blksz, offset);
		if (ret < 0) {
			pr_err("%s: pread at %ju failed, errno=%d (%s)\n",
				args->name, (intmax_t)offset, errno, strerror(errno));
		}
		inc_counter(args);
	}
}

/*
 *  stress_rawdev_burst()
 *	bursts of reads from random places on a device
 */
static void stress_rawdev_burst(
	const stress_args_t *args,
	const int fd,
	char *buffer,
	const size_t blks,
	const size_t blksz)
{
	int i;
	off_t blk = (stress_mwc64() % blks);

	for (i = 0; i < 256 && keep_stressing(args); i++) {
		int ret;
		off_t offset = blk * blksz;

		ret = pread(fd, buffer, blksz, offset);
		if (ret < 0) {
			pr_err("%s: pread at %ju failed, errno=%d (%s)\n",
				args->name, (intmax_t)offset, errno, strerror(errno));
		}
		blk++;
		blk %= blks;
		inc_counter(args);
	}
}

static const stress_rawdev_method_info_t rawdev_methods[];

/*
 *  stress_rawdev_all()
 *      iterate over all rawdev methods
 */
static void stress_rawdev_all(
	const stress_args_t *args,
	const int fd,
	char *buffer,
	const size_t blks,
	const size_t blksz)
{
	static int i = 1;       /* Skip over stress_rawdev_all */

	rawdev_methods[i++].func(args, fd, buffer, blks, blksz);
	if (!rawdev_methods[i].func)
		i = 1;
}


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
	{ NULL,         NULL }
};

/*
 *  stress_set_rawdev_method()
 *	set the default rawdev method
 */
static int stress_set_rawdev_method(const char *name)
{
	stress_rawdev_method_info_t const *info;

	for (info = rawdev_methods; info->func; info++) {
		if (!strcmp(info->name, name)) {
			stress_set_setting("rawdev-method", TYPE_ID_UINTPTR_T, &info);
			return 0;
		}
	}

	(void)fprintf(stderr, "rawdev-method must be one of:");
	for (info = rawdev_methods; info->func; info++) {
		(void)fprintf(stderr, " %s", info->name);
	}
	(void)fprintf(stderr, "\n");

	return -1;
}
#else
/*
 *  stress_set_rawdev_method()
 *	set the default rawdev method
 */
static int stress_set_rawdev_method(const char *name)
{
	(void)name;

	(void)fprintf(stderr, "option --rawdev-method not supported\n");
	return -1;
}
#endif

static const stress_opt_set_func_t opt_set_funcs[] = {
	{ OPT_rawdev_method,	stress_set_rawdev_method },
	{ 0,			NULL }
};

#if defined(HAVE_SYS_SYSMACROS_H) &&	\
    defined(BLKGETSIZE) && 		\
    defined(BLKSSZGET)

static int stress_rawdev(const stress_args_t *args)
{
	int ret;
	char path[PATH_MAX], devpath[PATH_MAX];
	char *buffer;
	struct stat stat_buf;
	int fd;
	size_t blks, blksz = 0, mmapsz;
	const stress_rawdev_method_info_t *rawdev_method = &rawdev_methods[0];
	const size_t page_size = args->page_size;
	stress_rawdev_func func;

	stress_temp_dir_args(args, path, sizeof(path));

	(void)stress_get_setting("rawdev-method", &rawdev_method);
	func = rawdev_method->func;

	fd = open(path, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
	if (fd < 0) {
		ret = exit_status(errno);
		pr_err("%s: open failed: %d (%s)\n",
			args->name, errno, strerror(errno));
		return ret;
	}

	ret = fstat(fd, &stat_buf);
	if (ret <  0) {
		pr_err("%s: cannot stat %s: errno=%d (%s)\n",
			args->name, path, errno, strerror(errno));
		(void)unlink(path);
		(void)close(fd);
		return EXIT_FAILURE;
	}
	(void)unlink(path);
	(void)close(fd);

	if (!stress_rawdev_path(stat_buf.st_dev, devpath, sizeof(devpath))) {
		pr_inf("%s: cannot determine raw block device\n",
			args->name);
		return EXIT_NO_RESOURCE;
	}

	fd = open(devpath, O_RDONLY | O_NONBLOCK);
	if (fd < 0) {
		pr_inf("%s: cannot open raw block device: errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_NO_RESOURCE;
	}
	ret = ioctl(fd, BLKGETSIZE, &blks);
	if (ret < 0) {
		pr_inf("%s: cannot get block size: errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		(void)close(fd);
		return EXIT_NO_RESOURCE;
	}
	ret = ioctl(fd, BLKSSZGET, &blksz);
	if (ret < 0) {
		pr_inf("%s: cannot get block size: errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		(void)close(fd);
		return EXIT_NO_RESOURCE;
	}
	/* Truncate if blksize looks too big */
	if (blksz > MAX_BLKSZ)
		blksz = MAX_BLKSZ;
	if (blksz < MIN_BLKSZ)
		blksz = MIN_BLKSZ;

	mmapsz = ((blksz + page_size - 1) & ~(page_size - 1));
	buffer = mmap(NULL, mmapsz, PROT_READ | PROT_WRITE,
			MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	if (buffer == MAP_FAILED) {
		pr_inf("%s: cannot allocate buffer of %zd bytes with %zd allocation\n",
			args->name, blksz, mmapsz);
		(void)close(fd);
		return EXIT_NO_RESOURCE;
	}

	(void)close(fd);
	fd = open(devpath, O_RDONLY | O_DIRECT);
	if (fd < 0) {
		pr_inf("%s: cannot open raw block device: errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_NO_RESOURCE;
	}

	if (args->instance == 0)
		pr_dbg("%s: exercising %s (%zd blocks of size %zd bytes)\n",
			args->name, devpath, blks, blksz);

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		func(args, fd, buffer, blks, blksz);
	} while (keep_stressing(args));

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	(void)munmap((void *)buffer, mmapsz);
	(void)close(fd);

	return EXIT_SUCCESS;
}

stressor_info_t stress_rawdev_info = {
	.stressor = stress_rawdev,
	.supported = stress_rawdev_supported,
	.class = CLASS_IO,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
#else
stressor_info_t stress_rawdev_info = {
	.stressor = stress_not_implemented,
	.class = CLASS_IO,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
#endif
