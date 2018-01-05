/*
 * Copyright (C) 2013-2018 Canonical, Ltd.
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

/*
 *  stress_swap_supported()
 *      check if we can run this as root
 */
int stress_swap_supported(void)
{
        if (geteuid() != 0) {
		pr_inf("stress-swap stressor needs to be run as root to add/remove swap\n");
                return -1;
        }
        return 0;
}

#if defined(__linux__)

#include <sys/swap.h>

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

typedef struct {
	uint8_t		bootbits[1024];
	uint32_t	version;
	uint32_t	last_page;
	uint32_t	nr_badpages;
	uint8_t		sws_uuid[SWAP_UUID_LENGTH];
	uint8_t		sws_volume[SWAP_LABEL_LENGTH];
	uint32_t	padding[117];
	uint32_t	badpages[1];
} swap_info_t;

static int stress_swap_zero(
	const args_t *args,
	const int fd,
	const uint32_t npages,
	const uint8_t *page)
{
	uint32_t i;

	if (lseek(fd, 0, SEEK_SET) < 0) {
		pr_fail_err("lseek");
		return -1;
	}

	for (i = 0; i < npages; i++) {
		if (write(fd, page, args->page_size) < 0) {
			pr_fail_err("write");
			return -1;
		}
	}
	return 0;
}

static int stress_swap_set_size(
	const args_t *args,
	const int fd,
	const uint32_t npages)
{
	static const char signature[] = SWAP_SIGNATURE;
	swap_info_t swap_info;
	size_t i;

	if (npages < MIN_SWAP_PAGES) {
		pr_fail("%s: incorrect swap size, must be > 16\n", args->name);
		return -1;
	}
	if (lseek(fd, 0, SEEK_SET) < 0) {
		pr_fail_err("lseek");
		return -1;
	}
	memset(&swap_info, 0, sizeof(swap_info));
	for (i = 0; i < sizeof(swap_info.sws_uuid); i++)
		swap_info.sws_uuid[i] = mwc8();
	snprintf((char *)swap_info.sws_volume, sizeof(swap_info.sws_volume),
		"SNG-SWP-%" PRIx32, args->instance);
	swap_info.version = SWAP_VERSION;
	swap_info.last_page = npages - 1;
	swap_info.nr_badpages = 0;
	if (write(fd, &swap_info, sizeof(swap_info)) < 0) {
		pr_fail_err("write swap info");
		return -1;
	}
	if (lseek(fd, args->page_size - SWAP_SIGNATURE_SZ, SEEK_SET) < 0) {
		pr_fail_err("lseek");
		return -1;
	}
	if (write(fd, signature, SWAP_SIGNATURE_SZ) < 0) {
		pr_fail_err("write swap info");
		return -1;
	}
	return 0;
}

/*
 *  stress_swap()
 *	stress swap operations
 */
int stress_swap(const args_t *args)
{
	char filename[PATH_MAX];
	int fd = -1, ret;
	uint8_t *page;

	page = calloc(1, args->page_size);
	if (!page) {
		pr_err("%s: failed to allocate 1 page: errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		ret = EXIT_NO_RESOURCE;
		goto tidy_ret;
	}

	ret = stress_temp_dir_mk_args(args);
	if (ret < 0) {
		ret = exit_status(-ret);
		goto tidy_free;
	}

	(void)stress_temp_filename_args(args,
		filename, sizeof(filename), mwc32());
	fd = open(filename, O_CREAT | O_RDWR, S_IRUSR);
	if (fd < 0) {
		ret = exit_status(errno);
		pr_fail_err("open");
		goto tidy_rm;
	}

	if (stress_swap_zero(args, fd, MAX_SWAP_PAGES, page) < 0) {
		ret = EXIT_FAILURE;
		goto tidy_close;
	}

	do {
		int swapflags;
		uint32_t npages = (mwc32() % (MAX_SWAP_PAGES - MIN_SWAP_PAGES)) +
				  MIN_SWAP_PAGES;

		if (mwc32() & 0x1000) {
			swapflags = (mwc8() << SWAP_FLAG_PRIO_SHIFT) & SWAP_FLAG_PRIO_MASK;
			swapflags |= SWAP_FLAG_PREFER;
		} else {
			swapflags = 0;
		}

		if (stress_swap_set_size(args, fd, npages) < 0) {
			ret = EXIT_FAILURE;
			goto tidy_close;
		}
		ret = swapon(filename, swapflags);
		if (ret < 0) {
			if (errno == EPERM) {
				/*
				 * We may hit EPERM if we request
				 * too many swap files
				 */
				ret = EXIT_NO_RESOURCE;
			} else {
				pr_fail_err("swapon");
				ret = EXIT_FAILURE;
			}
			goto tidy_close;
		}

		ret = swapoff(filename);
		if (ret < 0) {
			pr_fail_err("swapoff");
			ret = EXIT_FAILURE;
			(void)thrash_stop();
			goto tidy_close;
		}

		inc_counter(args);
	} while (keep_stressing());

	ret = EXIT_SUCCESS;
tidy_close:
	(void)close(fd);
tidy_rm:
	(void)unlink(filename);
	(void)stress_temp_dir_rm_args(args);
tidy_free:
	free(page);
tidy_ret:
	return ret;
}
#else
int stress_swap(const args_t *args)
{
	return stress_not_implemented(args);
}
#endif
