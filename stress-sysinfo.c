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

static const help_t help[] = {
	{ NULL,	"sysinfo N",	 "start N workers reading system information" },
	{ NULL,	"sysinfo-ops N", "stop after sysinfo bogo operations" },
	{ NULL,	NULL,		 NULL }
};

#define check_do_run()			\
	if (!g_keep_stressing_flag)	\
		break;			\

/*
 *  stress on system information
 *	stress system by rapid fetches of system information
 */
static int stress_sysinfo(const args_t *args)
{
	int n_mounts;
	char *mnts[128];

	(void)memset(mnts, 0, sizeof(mnts));

	n_mounts = mount_get(mnts, SIZEOF_ARRAY(mnts));
	if (n_mounts < 0) {
		pr_err("%s: failed to get mount points\n", args->name);
		return EXIT_FAILURE;
	}
	if (args->instance == 0)
		pr_dbg("%s: found %d mount points\n",
			args->name, n_mounts);

	do {
		struct tms tms_buf;
		clock_t clk;
#if defined(HAVE_SYS_SYSINFO_H) &&	\
    defined(HAVE_SYSINFO) &&		\
    defined(HAVE_SYS_STATFS_H)
		{
			struct sysinfo sysinfo_buf;
			struct statfs statfs_buf;
			int i, ret;

			ret = sysinfo(&sysinfo_buf);
			if ((ret < 0) && (g_opt_flags & OPT_FLAGS_VERIFY))
			 	pr_fail_err("sysinfo");

			check_do_run();

			/* Linux statfs variant */
			for (i = 0; i < n_mounts; i++) {
				int fd;

				check_do_run();

				if (!mnts[i])
					continue;

				ret = statfs(mnts[i], &statfs_buf);
				/* Mount may have been removed, so purge it */
				if ((ret < 0) && (errno == ENOENT)) {
					free(mnts[i]);
					mnts[i] = NULL;
					continue;
				}
				if ((ret < 0) && (g_opt_flags & OPT_FLAGS_VERIFY)) {
					if (errno != ENOSYS &&
					    errno != EOVERFLOW &&
					    errno != EACCES) {
						pr_fail("%s: statfs on %s "
							"failed: errno=%d (%s)\n",
							args->name, mnts[i], errno,
							strerror(errno));
					}
				}

				fd = open(mnts[i], O_RDONLY | O_DIRECTORY);
				if (fd < 0)
					continue;

				ret = fstatfs(fd, &statfs_buf);
				(void)close(fd);
				if ((ret < 0) && (errno == ENOENT))
					continue;
				if ((ret < 0) && (g_opt_flags & OPT_FLAGS_VERIFY)) {
					if (errno != ENOSYS &&
					    errno != EOVERFLOW &&
					    errno != EACCES) {
						pr_fail("%s: fstatfs on %s "
							"failed: errno=%d (%s)\n",
							args->name, mnts[i], errno,
							strerror(errno));
					}
				}
			}
		}
#endif

#if defined(HAVE_USTAT)
		{
			int i;

			check_do_run();

			for (i = 0; i < n_mounts; i++) {
				struct stat sbuf;
				struct ustat ubuf;
				int ret;

				ret = stat(mnts[i], &sbuf);
				if (ret < 0)
					continue;

				ret = ustat(sbuf.st_dev, &ubuf);
				if ((ret < 0) && (g_opt_flags & OPT_FLAGS_VERIFY)) {
					if (errno != EINVAL &&
					    errno != ENOSYS) {
						pr_fail("%s: ustat on %s "
							"failed: errno=%d (%s)\n",
							args->name, mnts[i], errno,
							strerror(errno));
					}
				}
			}
		}
#endif
		check_do_run();

#if defined(HAVE_SYS_STATVFS_H)
		{
			int i;

			struct statvfs statvfs_buf;
			/* POSIX.1-2001 statfs variant */
			for (i = 0; i < n_mounts; i++) {
				int ret;

				check_do_run();

				if (!mnts[i])
					continue;

				ret = statvfs(mnts[i], &statvfs_buf);
				if ((ret < 0) && (g_opt_flags & OPT_FLAGS_VERIFY)) {
					if (errno != ENOSYS &&
					    errno != EOVERFLOW &&
					    errno != EACCES) {
						pr_fail("%s: statvfs on %s "
							"failed: errno=%d (%s)\n",
							args->name, mnts[i], errno,
							strerror(errno));
					}
				}
			}
		}
#endif

		check_do_run();
		clk = times(&tms_buf);
		if ((clk == (clock_t)-1) && (g_opt_flags & OPT_FLAGS_VERIFY)) {
			 pr_fail_err("times");
		}
		inc_counter(args);
	} while (keep_stressing());

	mount_free(mnts, n_mounts);

	return EXIT_SUCCESS;
}

stressor_info_t stress_sysinfo_info = {
	.stressor = stress_sysinfo,
	.class = CLASS_OS,
	.help = help
};
