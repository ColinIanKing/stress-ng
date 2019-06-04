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

static const char *bitmap_file = "/sys/kernel/mm/page_idle/bitmap";

static const help_t help[] = {
	{ NULL,	"idle-page N",	   "start N idle page scanning workers" },
	{ NULL,	"idle-page-ops N", "stop after N idle page scan bogo operations" },
	{ NULL, NULL,		   NULL }
};

/*
 *  stress_idle_page_supported()
 *      check if we can run this as root
 */
static int stress_idle_page_supported(void)
{

	if (!stress_check_capability(SHIM_CAP_SYS_RESOURCE)) {
		pr_inf("idle-page stressor will be skipped, "
			"need to be running with CAP_SYS_RESOURCE "
			"rights for this stressor\n");
		return -1;
	}
        if (geteuid() != 0) {
                pr_inf("idle_page stressor will be skipped, "
                        "need to be running as root for this stressor\n");
                return -1;
        }

	if (access(bitmap_file, R_OK) != 0) {
		pr_inf("idle_page stressor will be skipped, "
			"cannot access file %s\n", bitmap_file);
		return -1;
	}
        return 0;
}

#if defined(__linux__)

#define BITMAP_BYTES	(8)
#define PAGES_TO_SCAN	(64)

/*
 *  stress_idle_page
 *	stress kernel logging interface
 */
static int stress_idle_page(const args_t *args)
{
	int fd;
	off_t posn = 0, last_posn = 0xfffffffffffffff8ULL;
	uint64_t bitmap_set[PAGES_TO_SCAN] ALIGNED(8);

	fd = open(bitmap_file, O_RDWR);
	if (fd < 0) {
		pr_inf("idle_page stressor will be skipped, "
			"cannot access file %s\n", bitmap_file);
		return EXIT_NO_RESOURCE;
	}

	(void)memset(bitmap_set, 0xff, sizeof(bitmap_set));

	do {
		off_t oret;
		int ret;
		uint64_t bitmap_get[PAGES_TO_SCAN] ALIGNED(8);

		oret = lseek(fd, posn, SEEK_SET);
		if (oret == (off_t)-1)
			goto next;

		ret = write(fd, bitmap_set, sizeof(bitmap_set));
		if ((ret < 0) && (errno == ENXIO)) {
			posn = 0;
			goto next;
		}

		oret = lseek(fd, posn, SEEK_SET);
		if (oret == (off_t)-1)
			goto next;

		ret = read(fd, &bitmap_get, BITMAP_BYTES);
		if ((ret < 0) && (errno == ENXIO)) {
			posn = 0;
			goto next;
		}
		last_posn = posn;
		posn += sizeof(bitmap_set);

		inc_counter(args);
next:
		if (posn == last_posn) {
			pr_inf("%s: aborting early, seek position not advancing\n",
				args->name);
			break;
		}
	} while (keep_stressing());

	(void)close(fd);

	return EXIT_SUCCESS;
}

stressor_info_t stress_idle_page_info = {
	.stressor = stress_idle_page,
	.supported = stress_idle_page_supported,
	.class = CLASS_OS,
	.help = help
};
#else
stressor_info_t stress_idle_page_info = {
	.stressor = stress_not_implemented,
	.supported = stress_idle_page_supported,
	.class = CLASS_OS,
	.help = help
};
#endif
