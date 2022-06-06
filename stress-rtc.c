/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C)      2022 Colin Ian King.
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

#if defined(HAVE_LINUX_RTC_H)
#include <linux/rtc.h>
#endif

#if defined(HAVE_SYS_SELECT_H)
#include <sys/select.h>
#endif

static const stress_help_t help[] = {
	{ NULL,	"rtc N",	"start N workers that exercise the RTC interfaces" },
	{ NULL,	"rtc-ops N",	"stop after N RTC bogo operations" },
	{ NULL,	NULL,		NULL }
};

#if defined(HAVE_LINUX_RTC_H)

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

static inline int stress_rtc_dev(const stress_args_t *args)
{
#if defined(RTC_RD_TIME) || defined(RTC_ALM_READ) || \
    defined(RTC_WKALM_RD) || defined(RTC_IRQP_READ)
	struct rtc_time rtc_tm;
	struct rtc_wkalrm wake_alarm;
	unsigned long tmp;
#endif
#if defined(HAVE_RTC_PARAM) &&		\
    defined(RTC_PARAM_GET) &&		\
    defined(RTC_PARAM_SET) &&		\
    defined(RTC_PARAM_FEATURES)	&& 	\
    defined(RTC_PARAM_CORRECTION)
	struct rtc_param param;
#endif
	int fd, ret = 0;
	static bool do_dev = true;
	struct timeval timeout;
	fd_set rfds;

	if (!do_dev)
		return -EACCES;

	if ((fd = open("/dev/rtc", O_RDONLY)) < 0) {
		do_dev = false;
		return -errno;
	}

#if defined(RTC_RD_TIME)
	if (ioctl(fd, RTC_RD_TIME, &rtc_tm) < 0) {
		if (errno != ENOTTY) {
			pr_fail("%s: ioctl RTC_RD_TIME failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			ret = -errno;
			goto err;
		}
	} else {
		int r;

		r = ioctl(fd, RTC_SET_TIME, &rtc_tm);
		(void)r;
	}
#endif

#if defined(RTC_ALM_READ)
	if (ioctl(fd, RTC_ALM_READ, &rtc_tm) < 0) {
		if (errno != ENOTTY) {
			pr_fail("%s: ioctl RTC_ALRM_READ failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			ret = -errno;
			goto err;
		}
	} else {
#if defined(RTC_ALM_SET)
		int r;

		r = ioctl(fd, RTC_ALM_SET, &rtc_tm);
		(void)r;
#endif
	}
#endif

#if defined(RTC_WKALM_RD)
	if (ioctl(fd, RTC_WKALM_RD, &wake_alarm) < 0) {
		if (errno != ENOTTY) {
			pr_fail("%s: ioctl RTC_WKALRM_RD failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			ret = -errno;
			goto err;
		}
	} else {
#if defined(RTC_WKALM_SET)
		int r;

		r = ioctl(fd, RTC_WKALM_SET, &wake_alarm);
		(void)r;
#endif
	}
#endif

#if defined(RTC_EPOCH_READ)
	if (ioctl(fd, RTC_EPOCH_READ, &tmp) < 0) {
		if (errno != ENOTTY) {
			pr_fail("%s: ioctl RTC_EPOCH_READ failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			ret = -errno;
			goto err;
		}
	} else {
#if defined(RTC_EPOCH_SET)
		int r;

		r = ioctl(fd, RTC_EPOCH_SET, tmp);
		(void)r;
#endif
	}
#endif

#if defined(RTC_IRQP_READ)
	if (ioctl(fd, RTC_IRQP_READ, &tmp) < 0) {
		if (errno != ENOTTY) {
			pr_fail("%s: ioctl RTC_IRQP_READ failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			ret = -errno;
			goto err;
		}
	} else {
		int r;

		r = ioctl(fd, RTC_IRQP_SET, tmp);
		(void)r;
	}
#endif

#if defined(HAVE_SYS_SELECT_H)
	/*
	 *  Very short delay select on the device
	 *  that should normally always timeout because
	 *  there are no RTC alarm interrupts pending
	 */
	timeout.tv_sec = 0;
	timeout.tv_usec = 1000;
	FD_ZERO(&rfds);
	FD_SET(fd, &rfds);
	(void)select(fd + 1, &rfds, NULL, NULL, &timeout);
#endif

#if defined(RTC_VL_READ)
	if (ioctl(fd, RTC_VL_READ, &tmp) < 0) {
		if (errno != ENOTTY) {
			pr_fail("%s: ioctl RTC_VL_READ failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			ret = -errno;
			goto err;
		}
	}
#endif

#if defined(HAVE_RTC_PARAM) &&		\
    defined(RTC_PARAM_GET) &&		\
    defined(RTC_PARAM_SET) &&		\
    defined(RTC_PARAM_FEATURES) &&	\
    defined(RTC_PARAM_CORRECTION)

	(void)memset(&param, 0, sizeof(param));
	param.param = RTC_PARAM_FEATURES;
	if (ioctl(fd, RTC_PARAM_GET, &param) == 0) {
		int r;

		/* Should be EINVAL */
		r = ioctl(fd, RTC_PARAM_SET, &param);
		(void)r;
	}

	(void)memset(&param, 0, sizeof(param));
	param.param = RTC_PARAM_CORRECTION;
	param.index = 0;
	if (ioctl(fd, RTC_PARAM_GET, &param) == 0) {
		int r;

		r = ioctl(fd, RTC_PARAM_SET, &param);
		(void)r;
	}

	(void)memset(&param, 0, sizeof(param));
	param.param = ~0U;
	if (ioctl(fd, RTC_PARAM_GET, &param) == 0) {
		int r;

		r = ioctl(fd, RTC_PARAM_SET, &param);
		(void)r;
	}
#endif

	/* Exercise an illegal RTC ioctl, -> -ENOTTY */
	{
		char buf[4096];

		(void)memset(buf, 0, sizeof(buf));
		(void)ioctl(fd, 0xff, buf);
	}

#if defined(RTC_RD_TIME) || defined(RTC_ALM_READ) || \
    defined(RTC_WKALM_RD) || defined(RTC_IRQP_READ)
err:
#endif
	(void)close(fd);

	return ret;
}

static inline int stress_rtc_sys(const stress_args_t *args)
{
	size_t i;
	int rc = 0;
	int enoents = 0;

	for (i = 0; i < SIZEOF_ARRAY(interfaces); i++) {
		char path[PATH_MAX];
		char buf[4096];
		ssize_t ret;

		(void)snprintf(path, sizeof(path), "/sys/class/rtc/rtc0/%s", interfaces[i]);
		ret = system_read(path, buf, sizeof(buf));
		if (ret < 0) {
			if (ret == -EINTR) {
				rc = (int)ret;
				break;
			} else if (ret == -ENOENT) {
				enoents++;
			} else {
				pr_fail("%s: read of %s failed: errno=%zd (%s)\n",
					args->name, path, -ret, strerror((int)-ret));
				rc = (int)ret;
			}
		}
	}
	if (enoents == SIZEOF_ARRAY(interfaces)) {
		pr_fail("%s: no RTC interfaces found for /sys/class/rtc/rtc0\n", args->name);
		rc = -ENOENT;
	}

	return rc;
}

static inline int stress_rtc_proc(const stress_args_t *args)
{
	ssize_t ret;
	char buf[4096];
	static char *path = "/proc/driver/rtc";

	ret = system_read(path, buf, sizeof(buf));
	if (ret < 0) {
		if ((ret != -ENOENT) && (ret != -EINTR)) {
			pr_fail("%s: read of %s failed: errno=%zd (%s)\n",
			args->name, path, -ret, strerror((int)-ret));
		}
	}
	return (int)ret;
}

/*
 *  stress_rtc
 *	stress some Linux RTC ioctls and /sys/class/rtc interface
 */
static int stress_rtc(const stress_args_t *args)
{
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

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
	} while (keep_stressing(args));

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	return EXIT_SUCCESS;
}

stressor_info_t stress_rtc_info = {
	.stressor = stress_rtc,
	.class = CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.help = help
};
#else
stressor_info_t stress_rtc_info = {
	.stressor = stress_not_implemented,
	.class = CLASS_OS,
	.help = help
};
#endif
