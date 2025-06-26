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

static const char bitmap_file[] = "/sys/kernel/mm/page_idle/bitmap";

static const stress_help_t help[] = {
	{ NULL,	"idle-page N",	   "start N idle page scanning workers" },
	{ NULL,	"idle-page-ops N", "stop after N idle page scan bogo operations" },
	{ NULL, NULL,		   NULL }
};

/*
 *  stress_idle_page_supported()
 *      check if we can run this as root
 */
static int stress_idle_page_supported(const char *name)
{

	if (!stress_check_capability(SHIM_CAP_SYS_RESOURCE)) {
		pr_inf_skip("%s stressor will be skipped, "
			"need to be running with CAP_SYS_RESOURCE "
			"rights for this stressor\n", name);
		return -1;
	}
	if (geteuid() != 0) {
		pr_inf_skip("%s stressor will be skipped, "
		       "need to be running as root for this stressor\n", name);
		return -1;
	}

	if (access(bitmap_file, R_OK) != 0) {
		pr_inf_skip("%s stressor will be skipped, "
			"cannot access file %s\n", name, bitmap_file);
		return -1;
	}
	return 0;
}

#if defined(__linux__)

#define BITMAP_BYTES	(8)
#define PAGES_TO_SCAN	(64)

/*
 *  stress_idle_page
 *	stress page scanning
 */
static int stress_idle_page(stress_args_t *args)
{
	int fd;
	off_t posn = 0, last_posn = ~(off_t)7;
	uint64_t bitmap_set[PAGES_TO_SCAN] ALIGNED(8);

	fd = open(bitmap_file, O_RDWR);
	if (fd < 0) {
		if (stress_instance_zero(args))
			pr_inf_skip("idle_page stressor will be skipped, "
				"cannot access file %s\n", bitmap_file);
		return EXIT_NO_RESOURCE;
	}

	(void)shim_memset(bitmap_set, 0xff, sizeof(bitmap_set));

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		off_t oret;
		ssize_t ret;
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

		stress_bogo_inc(args);
next:
		if (posn == last_posn) {
			pr_inf("%s: aborting early, seek position not advancing\n",
				args->name);
			break;
		}
	} while (stress_continue(args));

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	(void)close(fd);

	return EXIT_SUCCESS;
}

const stressor_info_t stress_idle_page_info = {
	.stressor = stress_idle_page,
	.supported = stress_idle_page_supported,
	.classifier = CLASS_OS,
	.help = help
};
#else
const stressor_info_t stress_idle_page_info = {
	.stressor = stress_unimplemented,
	.supported = stress_idle_page_supported,
	.classifier = CLASS_OS,
	.help = help,
	.unimplemented_reason = "only supported on Linux"
};
#endif
