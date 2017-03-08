/*
 * Copyright (C) 2013-2017 Canonical, Ltd.
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

#if defined(__linux__)

#include <linux/rtc.h>

/*
 *  RTC interfaces, as described by
 *  	Documentation/rtc.txt
 */
static const char *interfaces[] = {
	"date",
	"hctosys",
	"max_user_freq",
	"name",
	"since_epoch",
	"time",
	"wakealarm",
	"offset"
};

static inline int stress_rtc_dev(const args_t *args)
{
#if defined(RTC_RD_TIME) || defined(RTC_ALM_READ) || \
    defined(RTC_WKALM_RD) || defined(RTC_IRQP_READ)
	struct rtc_time rtc_tm;
	struct rtc_wkalrm alarm;
	unsigned long tmp;
#endif
	int fd, ret = 0;
	static bool do_dev = true;

	if (!do_dev)
		return -EACCES;

	if ((fd = open("/dev/rtc", O_RDONLY)) < 0) {
		do_dev = false;
		return -errno;
	}

#if defined(RTC_RD_TIME)
	if (ioctl(fd, RTC_RD_TIME, &rtc_tm) < 0) {
		if (errno != ENOTTY) {
			pr_fail_err("ioctl RTC_RD_TIME");
			ret = -errno;
			goto err;
		}
	}
#endif

#if defined(RTC_ALM_READ)
	if (ioctl(fd, RTC_ALM_READ, &alarm) < 0) {
		if (errno != ENOTTY) {
			pr_fail_err("ioctl RTC_ALRM_READ");
			ret = -errno;
			goto err;
		}
	}
#endif

#if defined(RTC_WKALM_RD)
	if (ioctl(fd, RTC_WKALM_RD, &alarm) < 0) {
		if (errno != ENOTTY) {
			pr_fail_err("ioctl RTC_WKALRM_RD");
			ret = -errno;
			goto err;
		}
	}
#endif

#if defined(RTC_IRQP_READ)
	if (ioctl(fd, RTC_IRQP_READ, &tmp) < 0) {
		if (errno != ENOTTY) {
			pr_fail_err("ioctl RTC_IRQP_READ");
			ret = -errno;
			goto err;
		}
	}
#endif

#if defined(RTC_RD_TIME) || defined(RTC_ALM_READ) || \
    defined(RTC_WKALM_RD) || defined(RTC_IRQP_READ)
err:
#endif
	(void)close(fd);

	return ret;
}

static inline int stress_rtc_sys(const args_t *args)
{
	size_t i;
	int rc = 0;
	int enoents = 0;

	for (i = 0; i < SIZEOF_ARRAY(interfaces); i++) {
		char path[PATH_MAX];
		char buf[4096];
		int ret;

		(void)snprintf(path, sizeof(path), "/sys/class/rtc/rtc0/%s", interfaces[i]);
		ret = system_read(path, buf, sizeof(buf));
		if (ret < 0) {
			if (ret == -EINTR) {
				rc = ret;
				break;
			} else if (ret == -ENOENT) {
				enoents++;
			} else {
				pr_fail("%s: read of %s failed: errno=%d (%s)\n",
					args->name, path, -ret, strerror(-ret));
				rc = ret;
			}
		}
	}
	if (enoents == SIZEOF_ARRAY(interfaces)) {
		pr_fail("%s: no RTC interfaces found for /sys/class/rtc/rtc0\n", args->name);
		rc = -ENOENT;
	}

	return rc;
}

static inline int stress_rtc_proc(const args_t *args)
{
	int ret;
	char buf[4096];
	static char *path = "/proc/driver/rtc";

	ret = system_read(path, buf, sizeof(buf));
	if (ret < 0) {
		if ((ret != -ENOENT) && (ret != -EINTR)) {
			pr_fail("%s: read of %s failed: errno=%d (%s)\n",
			args->name, path, -ret, strerror(-ret));
		}
	}
	return ret;
}

/*
 *  stress_rtc
 *	stress some Linux RTC ioctls and /sys/class/rtc interface
 */
int stress_rtc(const args_t *args)
{
	do {
		int ret;

		ret = stress_rtc_dev(args);
		if (ret < 0) {
			if ((ret != -EACCES) && (ret != -EBUSY))
				break;
		}
		ret = stress_rtc_sys(args);
		if (ret < 0)
			break;
		ret = stress_rtc_proc(args);
		if (ret < 0)
			break;
		inc_counter(args);
	} while (keep_stressing());

	return EXIT_SUCCESS;
}
#else
int stress_rtc(const args_t *args)
{
	return stress_not_implemented(args);
}
#endif
