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

#include <sys/ioctl.h>

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
static const char * const interfaces[] = {
	"date",
	"hctosys",
	"max_user_freq",
	"name",
	"since_epoch",
	"time",
	"wakealarm",
	"offset"
};

static inline int stress_rtc_dev(stress_args_t *args)
{
	int fd, ret = 0;
	static bool do_dev = true;

	if (!do_dev)
		return -EACCES;

	if (UNLIKELY((fd = open("/dev/rtc", O_RDONLY)) < 0)) {
		do_dev = false;
		return -errno;
	}

#if defined(RTC_RD_TIME)
	{
		struct rtc_time rtc_tm;

		if (UNLIKELY(ioctl(fd, RTC_RD_TIME, &rtc_tm) < 0)) {
			if ((errno != EINTR) && (errno != ENOTTY)) {
				ret = -errno;
				pr_fail("%s: ioctl RTC_RD_TIME failed, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
				goto err;
			}
		} else {
			VOID_RET(int, ioctl(fd, RTC_SET_TIME, &rtc_tm));
		}
	}
#endif

#if defined(RTC_ALM_READ)
	{
		struct rtc_time rtc_tm;

		if (UNLIKELY(ioctl(fd, RTC_ALM_READ, &rtc_tm) < 0)) {
			if ((errno != EINVAL) && (errno != EINTR) && (errno != ENOTTY)) {
				ret = -errno;
				pr_fail("%s: ioctl RTC_ALRM_READ failed, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
				goto err;
			}
		} else {
#if defined(RTC_ALM_SET)
			VOID_RET(int, ioctl(fd, RTC_ALM_SET, &rtc_tm));
#endif
		}
	}
#endif

#if defined(RTC_WKALM_RD)
	{
		struct rtc_wkalrm wake_alarm;

		if (UNLIKELY(ioctl(fd, RTC_WKALM_RD, &wake_alarm) < 0)) {
			if ((errno != EINVAL) && (errno != EINTR) && (errno != ENOTTY)) {
				ret = -errno;
				pr_fail("%s: ioctl RTC_WKALRM_RD failed, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
				goto err;
			}
		} else {
#if defined(RTC_WKALM_SET)
			VOID_RET(int, ioctl(fd, RTC_WKALM_SET, &wake_alarm));
#endif
		}
	}
#endif

#if defined(RTC_AIE_ON) && 	\
    defined(RTC_AIE_OFF)
	if (ioctl(fd, RTC_AIE_ON, NULL) == 0) {
		(void)ioctl(fd, RTC_AIE_OFF, NULL);
	}
#endif

#if defined(RTC_UIE_ON) && 	\
    defined(RTC_UIE_OFF)
	if (ioctl(fd, RTC_UIE_ON, NULL) == 0) {
		(void)ioctl(fd, RTC_UIE_OFF, NULL);
	}
#endif

#if defined(RTC_PIE_ON) && 	\
    defined(RTC_PIE_OFF)
	if (ioctl(fd, RTC_PIE_ON, NULL) == 0) {
		(void)ioctl(fd, RTC_PIE_OFF, NULL);
	}
#endif

#if defined(RTC_EPOCH_READ)
	{
		unsigned long int tmp;

		if (UNLIKELY(ioctl(fd, RTC_EPOCH_READ, &tmp) < 0)) {
			if ((errno != EINVAL) && (errno != EINTR) && (errno != ENOTTY)) {
				ret = -errno;
				pr_fail("%s: ioctl RTC_EPOCH_READ failed, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
				goto err;
			}
		} else {
#if defined(RTC_EPOCH_SET)
			VOID_RET(int, ioctl(fd, RTC_EPOCH_SET, tmp));
#endif
		}
	}
#endif

#if defined(RTC_IRQP_READ)
	{
		unsigned long int tmp;

		if (UNLIKELY(ioctl(fd, RTC_IRQP_READ, &tmp) < 0)) {
			if ((errno != EINVAL) && (errno != EINTR) && (errno != ENOTTY)) {
				ret = -errno;
				pr_fail("%s: ioctl RTC_IRQP_READ failed, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
				goto err;
			}
		} else {
			VOID_RET(int, ioctl(fd, RTC_IRQP_SET, tmp));
		}
	}
#endif

#if defined(HAVE_SYS_SELECT_H) &&	\
    defined(HAVE_SELECT)
	{
		struct timeval timeout;
		fd_set rfds;

		/*
		 *  Very short delay select on the device
		 *  that should normally always timeout because
		 *  there are no RTC alarm interrupts pending
		 */
		timeout.tv_sec = 0;
		timeout.tv_usec = 1;
		FD_ZERO(&rfds);
		FD_SET(fd, &rfds);
		(void)select(fd + 1, &rfds, NULL, NULL, &timeout);
	}
#endif

#if defined(RTC_VL_READ)
	{
		unsigned long int tmp;

		if (UNLIKELY(ioctl(fd, RTC_VL_READ, &tmp) < 0)) {
			if ((errno != EINVAL) && (errno != EINTR) && (errno != ENOTTY)) {
				ret = -errno;
				pr_fail("%s: ioctl RTC_VL_READ failed, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
				goto err;
			}
		}
	}
#endif

#if defined(HAVE_RTC_PARAM) &&		\
    defined(RTC_PARAM_GET) &&		\
    defined(RTC_PARAM_SET) &&		\
    defined(RTC_PARAM_FEATURES) &&	\
    defined(RTC_PARAM_CORRECTION)
	{
		struct rtc_param param;

		(void)shim_memset(&param, 0, sizeof(param));
		param.param = RTC_PARAM_FEATURES;
		if (ioctl(fd, RTC_PARAM_GET, &param) == 0) {
			/* Should be EINVAL */
			VOID_RET(int, ioctl(fd, RTC_PARAM_SET, &param));
		}

		(void)shim_memset(&param, 0, sizeof(param));
		param.param = RTC_PARAM_CORRECTION;
		param.index = 0;
		if (ioctl(fd, RTC_PARAM_GET, &param) == 0) {
			VOID_RET(int, ioctl(fd, RTC_PARAM_SET, &param));
		}

		(void)shim_memset(&param, 0, sizeof(param));
		param.param = ~0U;
		if (ioctl(fd, RTC_PARAM_GET, &param) == 0) {
			VOID_RET(int, ioctl(fd, RTC_PARAM_SET, &param));
		}
	}
#endif

	/* Exercise an illegal RTC ioctl, -> -ENOTTY */
	{
		char buf[4096];

		(void)shim_memset(buf, 0, sizeof(buf));
		VOID_RET(int, ioctl(fd, 0xff, buf));
	}

#if defined(RTC_RD_TIME) || defined(RTC_ALM_READ) || \
    defined(RTC_WKALM_RD) || defined(RTC_IRQP_READ)
err:
#endif
	(void)close(fd);

	return ret;
}

static inline int stress_rtc_sys(stress_args_t *args)
{
	size_t i;
	int rc = 0;
	int enoents = 0;

	for (i = 0; i < SIZEOF_ARRAY(interfaces); i++) {
		char path[PATH_MAX];
		char buf[4096];
		ssize_t ret;

		(void)snprintf(path, sizeof(path), "/sys/class/rtc/rtc0/%s", interfaces[i]);
		ret = stress_system_read(path, buf, sizeof(buf));
		if (UNLIKELY(ret < 0)) {
			if (ret == -EINTR) {
				rc = (int)ret;
				break;
			} else if (ret == -ENOENT) {
				enoents++;
			} else if (ret == -EINVAL) {
				/* this can occur on interrupted EFI rtc reads, ignore */
				continue;
			} else {
				pr_fail("%s: read of %s failed, errno=%zd (%s)\n",
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

static inline int stress_rtc_proc(stress_args_t *args)
{
	ssize_t ret;
	char buf[4096];
	static char *path = "/proc/driver/rtc";

	ret = stress_system_read(path, buf, sizeof(buf));
	if (ret < 0) {
		if ((ret != -ENOENT) && (ret != -EINTR)) {
			pr_fail("%s: read of %s failed, errno=%zd (%s)\n",
			args->name, path, -ret, strerror((int)-ret));
		}
	}
	return (int)ret;
}

/*
 *  stress_rtc
 *	stress some Linux RTC ioctls and /sys/class/rtc interface
 */
static int stress_rtc(stress_args_t *args)
{
	int rc = EXIT_SUCCESS;

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		int ret;

		ret = stress_rtc_dev(args);
		if (ret < 0) {
			if ((ret != -ENOENT) && (ret != -EINTR) &&
			    (ret != -EACCES) && (ret != -EBUSY) &&
			    (ret != -EPERM)) {
				rc = EXIT_FAILURE;
				break;
			}
		}
		ret = stress_rtc_sys(args);
		if (ret < 0) {
			if ((ret != -ENOENT) && (ret != -EINTR)) {
				rc = EXIT_FAILURE;
			}
			break;
		}
		ret = stress_rtc_proc(args);
		if (ret < 0) {
			if ((ret != -ENOENT) && (ret != -EINTR)) {
				rc = EXIT_FAILURE;
			}
			break;
		}
		stress_bogo_inc(args);
	} while (stress_continue(args));

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	return rc;
}

const stressor_info_t stress_rtc_info = {
	.stressor = stress_rtc,
	.classifier = CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.help = help
};
#else
const stressor_info_t stress_rtc_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.unimplemented_reason = "built without linux/rtc.h real-time clock support"
};
#endif
