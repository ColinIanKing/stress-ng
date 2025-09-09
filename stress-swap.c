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
#include "core-builtin.h"
#include "core-capabilities.h"
#include "core-madvise.h"
#include "core-out-of-memory.h"

#include <sys/ioctl.h>

#if defined(__sun__)
/* Disable for SunOs/Solaris because */
#undef HAVE_SYS_SWAP_H
#endif
#if defined(HAVE_SYS_SWAP_H)
#include <sys/swap.h>
#endif

#define SHIM_EXT2_IOC_GETFLAGS		_IOR('f', 1, long int)
#define SHIM_EXT2_IOC_SETFLAGS		_IOW('f', 2, long int)
#define SHIM_FS_NOCOW_FL		0x00800000 /* No Copy-on-Write file */

static const stress_help_t help[] = {
	{ NULL,	"swap N",	"start N workers exercising swapon/swapoff" },
	{ NULL,	"swap-ops N",	"stop after N swapon/swapoff operations" },
	{ NULL,	"swap-self",	"attempt to swap stressors pages out" },
	{ NULL,	NULL,		NULL }
};

#if defined(HAVE_SYS_SWAP_H) &&	\
    defined(HAVE_SWAP)

#define SWAP_VERSION		(1)
#define SWAP_UUID_LENGTH	(16)
#define SWAP_LABEL_LENGTH	(16)
#define SWAP_SIGNATURE 		"SWAPSPACE2"
#define SWAP_SIGNATURE_SZ	(sizeof(SWAP_SIGNATURE) - 1)

#define MIN_SWAP_PAGES		(32)
#define MAX_SWAP_PAGES		(256)

#if !defined(SWAP_FLAG_PRIO_SHIFT)
#define SWAP_FLAG_PRIO_SHIFT	(0)
#endif
#if !defined(SWAP_FLAG_PRIO_MASK)
#define SWAP_FLAG_PRIO_MASK	(0x7fff)
#endif

#define SWAP_HDR_SANE		(0x01)
#define SWAP_HDR_BAD_SIGNATURE	(0x02)
#define SWAP_HDR_BAD_VERSION	(0x04)
#define SWAP_HDR_ZERO_LAST_PAGE (0x08)
#define SWAP_HDR_BAD_LAST_PAGE	(0x10)
#define SWAP_HDR_BAD_NR_BAD	(0x20)

static const int bad_header_flags[] = {
	SWAP_HDR_BAD_SIGNATURE,
	SWAP_HDR_BAD_VERSION,
	SWAP_HDR_ZERO_LAST_PAGE,
	SWAP_HDR_BAD_LAST_PAGE,
	SWAP_HDR_BAD_NR_BAD,
};

typedef struct {
	uint8_t		bootbits[1024];	/* cppcheck-suppress unusedStructMember */
	uint32_t	version;
	uint32_t	last_page;
	uint32_t	nr_badpages;
	uint8_t		sws_uuid[SWAP_UUID_LENGTH];
	uint8_t		sws_volume[SWAP_LABEL_LENGTH];
	uint32_t	padding[117];	/* cppcheck-suppress unusedStructMember */
	uint32_t	badpages[1];	/* cppcheck-suppress unusedStructMember */
} stress_swap_info_t;

/*
 *  stress_swap_supported()
 *      check if we can run this with SHIM_CAP_SYS_ADMIN capability
 */
static int stress_swap_supported(const char *name)
{
	if (!stress_check_capability(SHIM_CAP_SYS_ADMIN)) {
		pr_inf_skip("%s stressor will be skipped, "
			"need to be running with CAP_SYS_ADMIN "
			"rights for this stressor\n", name);
		return -1;
	}
	return 0;
}

#endif

static const stress_opt_t opts[] = {
	{ OPT_swap_self, "swap-self", TYPE_ID_BOOL, 0, 1, NULL },
	END_OPT,
};

#if defined(HAVE_SYS_SWAP_H) &&	\
    defined(HAVE_SWAP)

#if defined(MADV_PAGEOUT) &&	\
    defined(__linux__)

/*
 *  stress_swap_self()
 *	swap pages out of process
 */
static void stress_swap_self(const size_t page_size)
{
	char buffer[4096];
	FILE *fp;
	const uintmax_t max_addr = UINTMAX_MAX - (UINTMAX_MAX >> 1);

	fp = fopen("/proc/self/maps", "r");
	if (!fp)
		return;

	/*
	 * Look for field 0060b000-0060c000 r--p 0000b000 08:01 1901726
	 */
	while (fgets(buffer, sizeof(buffer), fp)) {
		uint64_t begin, end, len, offset;
		char tmppath[1024];
		char prot[6];

		tmppath[0] = '\0';
		if (sscanf(buffer, "%" SCNx64 "-%" SCNx64
		           " %5s %" SCNx64 " %*x:%*x %*d %1023s", &begin, &end, prot, &offset, tmppath) != 5) {
			continue;
		}
#if 0
		if ((prot[2] != 'x') && (prot[1] != 'w'))
			continue;
		if (tmppath[0] == '\0')
			continue;
#endif
		/* Avoid vdso and vvar */
		if (strncmp("[v", tmppath, 2) == 0)
			continue;

		if ((begin > UINTPTR_MAX) || (end > UINTPTR_MAX))
			continue;

		/* Ignore bad ranges */
		if ((begin >= end) || (begin == 0) || (end == 0) || (end >= max_addr))
			continue;

		len = end - begin;
		/* Skip invalid ranges */
		if ((len < page_size) || (len > 0x80000000UL))
			continue;

		(void)madvise((void *)(uintptr_t)begin, len, MADV_PAGEOUT);
	}
	(void)fclose(fp);
}
#endif

static int32_t stress_swap_zero(
	stress_args_t *args,
	const int fd,
	const uint32_t npages,
	const uint8_t *page)
{
	uint32_t i;

	if (lseek(fd, 0, SEEK_SET) < 0) {
		pr_fail("%s: lseek failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return -1;
	}

	for (i = 0; i < npages; i++) {
		if (write(fd, page, args->page_size) < 0) {
			if (errno == ENOSPC) {
				pr_inf("%s: out of free space creating swap "
					"file, skipping stressor\n", args->name);
				return (int32_t)i;
			}
			pr_fail("%s: write failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			return -1;
		}
	}
	return (int32_t)i;
}

static int stress_swap_set_size(
	stress_args_t *args,
	const int fd,
	const uint32_t npages,
	const int bad_flags)
{
	char signature[] = SWAP_SIGNATURE;
	stress_swap_info_t swap_info;
	size_t i;

	if (npages < MIN_SWAP_PAGES) {
		pr_fail("%s: incorrect swap size, must be > 16\n", args->name);
		return -1;
	}
	if (lseek(fd, 0, SEEK_SET) < 0) {
		pr_fail("%s: lseek failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return -1;
	}

	if (bad_flags & SWAP_HDR_BAD_SIGNATURE)
		signature[0]++;	/* Invalid */

	(void)shim_memset(&swap_info, 0, sizeof(swap_info));
	for (i = 0; i < sizeof(swap_info.sws_uuid); i++)
		swap_info.sws_uuid[i] = stress_mwc8();
	(void)snprintf((char *)swap_info.sws_volume,
		sizeof(swap_info.sws_volume),
		"SNG-SWP-%" PRIx32, args->instance);

	if (bad_flags & SWAP_HDR_BAD_VERSION)
		swap_info.version = (uint32_t)~SWAP_VERSION;	/* Invalid */
	else
		swap_info.version = SWAP_VERSION;

	swap_info.last_page = npages - 1;		/* default */
	if (bad_flags & SWAP_HDR_ZERO_LAST_PAGE)
		swap_info.last_page = 0;		/* Invalid */
	else if (bad_flags & SWAP_HDR_BAD_LAST_PAGE)
		swap_info.last_page = npages + 1;	/* Invalid */

	if (bad_flags & SWAP_HDR_BAD_NR_BAD)
		swap_info.nr_badpages = ~0U;		/* Dire */
	else
		swap_info.nr_badpages = 0;

	if (write(fd, &swap_info, sizeof(swap_info)) < 0) {
		pr_fail("%s: write of swap info failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return -1;
	}
	if (lseek(fd, (off_t)(args->page_size - SWAP_SIGNATURE_SZ), SEEK_SET) < 0) {
		pr_fail("%s: lseek failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return -1;
	}
	if (write(fd, signature, SWAP_SIGNATURE_SZ) < 0) {
		pr_fail("%s: write of swap signature failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return -1;
	}
	return 0;
}

static void stress_swap_check_swapped(uint64_t *swapped_out)
{
	FILE *fp;
	char buf[4096];
	uint64_t swapout = 0;
	static uint64_t prev_swapout = 0;

	fp = fopen("/proc/vmstat", "r");
	if (!fp)
		return;

	while (fgets(buf, sizeof(buf), fp) != NULL) {
		if (strncmp(buf, "pswpout", 7) == 0) {
			swapout = (uint64_t)atoll(buf + 8);
			break;
		}
	}
	(void)fclose(fp);

	if (prev_swapout == 0) {
		prev_swapout = swapout;
		return;
	}

	*swapped_out += swapout - prev_swapout;
	prev_swapout = swapout;
}

static void stress_swap_clean_dir(stress_args_t *args)
{
	char path[PATH_MAX];
	DIR *dir;
	const struct dirent *d;

	stress_temp_dir(path, sizeof(path), args->name,
		args->pid, args->instance);
	dir = opendir(path);
	if (!dir)
		return;

	while ((d = readdir(dir)) != NULL) {
		struct stat stat_buf;
		char filename[PATH_MAX];

		stress_mk_filename(filename, sizeof(filename), path, d->d_name);
		if (shim_stat(filename, &stat_buf) == 0) {
			if (S_ISREG(stat_buf.st_mode)) {
				(void)stress_swapoff(filename);
				(void)unlink(filename);
			}
		}
	}
	(void)closedir(dir);
	(void)rmdir(path);
}

/*
 *  stress_swap_child()
 *	stress swap operations
 */
static int stress_swap_child(stress_args_t *args, void *context)
{
	char filename[PATH_MAX];
	int fd, ret;
	uint8_t *page;
	uint64_t swapped_out = 0;
	int32_t max_swap_pages;
	const size_t page_size = args->page_size;
	bool swap_self = false;
	double t, duration, rate;

	(void)context;

	if (!stress_get_setting("swap-self", &swap_self)) {
		if (g_opt_flags & OPT_FLAGS_AGGRESSIVE)
			swap_self = true;
	}

	page = mmap(NULL, page_size, PROT_READ | PROT_WRITE,
			MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	if (page == MAP_FAILED) {
		pr_inf_skip("%s: failed to mmap 1 page%s, errno=%d (%s), skipping stressor\n",
			args->name, stress_get_memfree_str(), errno, strerror(errno));
		ret = EXIT_NO_RESOURCE;
		goto tidy_ret;
	}
	stress_set_vma_anon_name(page, page_size, "swap-page");
	(void)stress_madvise_mergeable(page, page_size);

	stress_swap_clean_dir(args);
	ret = stress_temp_dir_mk_args(args);
	if (ret < 0) {
		ret = stress_exit_status(-ret);
		goto tidy_free;
	}

	(void)stress_temp_filename_args(args,
		filename, sizeof(filename), stress_mwc32());
	fd = open(filename, O_CREAT | O_RDWR, S_IRUSR);
	if (fd < 0) {
		ret = stress_exit_status(errno);
		pr_fail("%s: open swap file %s failed, errno=%d (%s)\n",
			args->name, filename, errno, strerror(errno));
		goto tidy_rm;
	}

#if defined(__linux__)
	{
		unsigned long int flags;

		/*
		 *  Disable Copy-on-Write on file where possible, since
		 *  file systems such as btrfs have CoW enabled by default
		 *  and we swap does not support this feature.
		 */
		if (ioctl(fd, SHIM_EXT2_IOC_GETFLAGS, &flags) == 0) {
			flags |= SHIM_FS_NOCOW_FL;
			VOID_RET(int, ioctl(fd, SHIM_EXT2_IOC_SETFLAGS, &flags));
		}
	}
#endif

	max_swap_pages = stress_swap_zero(args, fd, MAX_SWAP_PAGES, page);
	if (max_swap_pages < 0) {
		ret = EXIT_FAILURE;
		goto tidy_close;
	} else if (max_swap_pages < MAX_SWAP_PAGES) {
		ret = EXIT_NO_RESOURCE;
		goto tidy_close;
	}

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	t = stress_time_now();
	do {
		int swapflags = 0;
		int bad_flags;
		char *ptr;
		uint32_t npages = stress_mwc32modn(MAX_SWAP_PAGES - MIN_SWAP_PAGES) +
				  MIN_SWAP_PAGES;
		const size_t mmap_size = (size_t)npages * page_size;

#if defined(SWAP_FLAG_PREFER)
		if (stress_mwc1()) {
			swapflags = (stress_mwc8() << SWAP_FLAG_PRIO_SHIFT) & SWAP_FLAG_PRIO_MASK;
			swapflags |= SWAP_FLAG_PREFER;
		}
#endif
#if defined(SWAP_FLAG_DISCARD)
		if (stress_mwc1())
			swapflags |= SWAP_FLAG_DISCARD;
#endif
		/* Periodically create bad swap header */
		if (stress_mwc8() < 16) {
			const size_t idx = stress_mwc8modn(SIZEOF_ARRAY(bad_header_flags));
			bad_flags = bad_header_flags[idx];
		} else {
			bad_flags = SWAP_HDR_SANE;	/* No bad header */
		}

		if (stress_swap_set_size(args, fd, npages, bad_flags) < 0) {
			ret = EXIT_FAILURE;
			goto tidy_close;
		}
		ret = swapon(filename, swapflags);
		if ((bad_flags == SWAP_HDR_SANE) && (ret < 0)) {
			switch (errno) {
			case EPERM:
				/*
				 * We may hit EPERM if we request
				 * too many swap files, so delay, retry to
				 * keep the pressure up.
				 */
				if (stress_check_capability(SHIM_CAP_SYS_ADMIN)) {
					(void)shim_usleep(100000);
					continue;
				}
				pr_inf_skip("%s: cannot enable swap%s, skipping stressor\n",
					args->name, stress_get_fs_type(filename));
				ret = EXIT_NO_RESOURCE;
				break;
			case EINVAL:
				pr_inf_skip("%s: cannot enable swap%s, skipping stressor\n",
					args->name, stress_get_fs_type(filename));
				ret = EXIT_NO_RESOURCE;
				break;
			case EBUSY:
				continue;
			default:
				pr_fail("%s: swapon failed%s, errno=%d (%s)\n",
					args->name, stress_get_fs_type(filename),
					errno, strerror(errno));
				ret = EXIT_FAILURE;
				break;
			}
			goto tidy_close;
		}

		ptr = mmap(NULL, mmap_size, PROT_READ | PROT_WRITE,
				MAP_SHARED | MAP_ANONYMOUS, -1, 0);
		if (ptr != MAP_FAILED) {
			size_t i;
			const char *p_end = ptr + mmap_size;
			char *p;

			(void)shim_madvise(ptr, mmap_size, MADV_WILLNEED);
			/* Add simple check value to start of each page */
			for (i = 0, p = ptr; p < p_end; p += page_size, i++) {
				uintptr_t *up = (uintptr_t *)(uintptr_t)p;

				(void)shim_memset(p, (int)i, page_size);
				*up = (uintptr_t)p;
			}
#if defined(MADV_PAGEOUT)
			(void)shim_madvise(ptr, mmap_size, MADV_PAGEOUT);
#endif
#if defined(MADV_PAGEOUT) &&	\
    defined(__linux__)
			if (swap_self)
				stress_swap_self(args->page_size);
#endif
			stress_swap_check_swapped(&swapped_out);

			/* Check page has check address value */
			for (i = 0, p = ptr; p < p_end; p += page_size, i++) {
				const uintptr_t *up = (uintptr_t *)(uintptr_t)p;

				if (*up != (uintptr_t)p) {
					pr_fail("%s: failed, address %p contains "
						"%" PRIuPTR " and not %" PRIuPTR "\n",
						args->name, (void *)p, *up, (uintptr_t)p);
				}
			}
			(void)stress_munmap_force(ptr, mmap_size);
		}

		ret = stress_swapoff(filename);
		if ((bad_flags == SWAP_HDR_SANE) && (ret < 0)) {
			pr_fail("%s: swapoff failed%s, errno=%d (%s)\n",
				args->name, stress_get_fs_type(filename),
				errno, strerror(errno));
			ret = EXIT_FAILURE;
			goto tidy_close;
		}

		/* Exercise illegal swapon filename */
		ret = swapon("", swapflags);
		if (ret == 0)
			VOID_RET(int, stress_swapoff(""));	/* Should never happen */

		/* Exercise illegal swapoff filename */
		ret = stress_swapoff("");
		if (ret == 0)
			VOID_RET(int, swapon("", swapflags));	/* Should never happen */

		/* Exercise illegal swapon flags */
		ret = swapon(filename, ~0);
		if (ret == 0)
			VOID_RET(int, stress_swapoff(filename));/* Should never happen */

		stress_bogo_inc(args);
	} while (stress_continue(args));

	duration = stress_time_now() - t;
	rate = (duration > 0.0) ? swapped_out / duration : 0.0;
	stress_metrics_set(args, 0, "pages swapped out per second", rate, STRESS_METRIC_GEOMETRIC_MEAN);

	ret = EXIT_SUCCESS;
tidy_close:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
	(void)close(fd);
tidy_rm:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
	(void)shim_unlink(filename);
	(void)stress_temp_dir_rm_args(args);
tidy_free:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
	(void)munmap((void *)page, page_size);
tidy_ret:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
	stress_swap_clean_dir(args);
	return ret;
}

static int stress_swap(stress_args_t *args)
{
	int ret;

	ret = stress_oomable_child(args, NULL, stress_swap_child, STRESS_OOMABLE_NORMAL);
	stress_swap_clean_dir(args);
	return ret;
}

const stressor_info_t stress_swap_info = {
	.stressor = stress_swap,
	.supported = stress_swap_supported,
	.classifier = CLASS_VM | CLASS_OS,
	.opts = opts,
	.verify = VERIFY_ALWAYS,
	.help = help
};
#else
const stressor_info_t stress_swap_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_VM | CLASS_OS,
	.opts = opts,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.unimplemented_reason = "built without sys/swap.h or swap() system call"
};
#endif
