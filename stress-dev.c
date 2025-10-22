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
#include "core-arch.h"
#include "core-builtin.h"
#include "core-capabilities.h"
#include "core-killpid.h"
#include "core-out-of-memory.h"
#include "core-pthread.h"
#include "core-pragma.h"
#include "core-put.h"
#include "core-try-open.h"

#include <ctype.h>
#include <sys/ioctl.h>

#if defined(HAVE_SYS_FILE_H)
#include <sys/file.h>
#endif

#if defined(HAVE_SYS_SELECT_H)
#include <sys/select.h>
#endif

#if defined(HAVE_TERMIOS_H)
#include <termios.h>
#endif

#if defined(HAVE_LIBGEN_H)
#include <libgen.h>
#endif

#if defined(HAVE_LINUX_AUTO_DEV_IOCTL_H)
#include <linux/auto_dev-ioctl.h>
#endif

#if defined(HAVE_LINUX_BLKZONED_H)
#include <linux/blkzoned.h>
#endif

#if defined(HAVE_LINUX_CDROM_H)
#include <linux/cdrom.h>
#endif

#if defined(HAVE_LINUX_DM_IOCTL_H)
#include <linux/dm-ioctl.h>
#endif

#if defined(HAVE_LINUX_FB_H)
#include <linux/fb.h>
#endif

#if defined(HAVE_LINUX_FD_H)
#include <linux/fd.h>
#endif

#if defined(HAVE_LINUX_FS_H)
#include <linux/fs.h>
#endif

#if defined(HAVE_LINUX_HDREG_H)
#include <linux/hdreg.h>
#endif

#if defined(HAVE_LINUX_HIDDEV)
#include <linux/hiddev.h>
#endif

#if defined(HAVE_LINUX_HIDRAW_H)
#include <linux/hidraw.h>
#endif

#if defined(HAVE_LINUX_HPET_H)
#include <linux/hpet.h>
#endif

#if defined(HAVE_LINUX_INPUT_H)
#include <linux/input.h>
#endif

#if defined(HAVE_LINUX_KD_H)
#include <linux/kd.h>
#endif

#if defined(HAVE_LINUX_KVM_H)
#include <linux/kvm.h>
#endif

#if defined(HAVE_LINUX_LIRC_H)
#include <linux/lirc.h>
#endif

#if defined(HAVE_LINUX_MEDIA_H)
#include <linux/media.h>
#endif

#if defined(HAVE_LINUX_PPDEV_H)
#include <linux/ppdev.h>
#endif

#if defined(HAVE_LINUX_RANDOM_H)
#include <linux/random.h>
#endif

#if defined(HAVE_LINUX_RTC_H)
#include <linux/rtc.h>
#endif

#if defined(HAVE_LINUX_SERIAL_H)
#include <linux/serial.h>
#endif

#if defined(HAVE_LINUX_PTP_CLOCK_H)
#include <linux/ptp_clock.h>
#endif

#if defined(HAVE_LINUX_UINPUT_H)
#include <linux/uinput.h>
#endif

#if defined(HAVE_LINUX_USBDEVICE_FS_H)
#include <linux/usbdevice_fs.h>
#endif

#if defined(HAVE_LINUX_USB_CDC_WDM_H)
#include <linux/usb/cdc-wdm.h>
#endif

#if defined(HAVE_LINUX_VIDEODEV2_H)
#include <linux/videodev2.h>
#endif

#if defined(HAVE_LINUX_VT_H)
#include <linux/vt.h>
#endif

#if defined(HAVE_SCSI_SCSI_H)
#include <scsi/scsi.h>
#endif

#if defined(HAVE_SCSI_SCSI_IOCTL_H)
#include <scsi/scsi_ioctl.h>
#endif

#if defined(HAVE_SCSI_SG_H)
#include <scsi/sg.h>
#endif

#if defined(HAVE_SOUND_ASOUND_H)
#include <sound/asound.h>
#endif

#if defined(HAVE_SYS_CAPABILITY_H)
#include <sys/capability.h>
#endif

#if defined(HAVE_POLL_H)
#include <poll.h>
#endif

#if defined(HAVE_TERMIO_H)
#include <termio.h>
#endif

#if defined(HAVE_SYS_UIO_H)
#include <sys/uio.h>
#endif

#if !defined(O_NDELAY)
#define O_NDELAY	(0)
#endif

#if defined(HAVE_TERMIOS_H)

#define HAVE_SHIM_TERMIOS2
/* shim_speed_t */
typedef unsigned int shim_speed_t;

/* shim termios2 */
struct shim_termios2 {
	tcflag_t c_iflag;		/* input mode flags */
	tcflag_t c_oflag;		/* output mode flags */
	tcflag_t c_cflag;		/* control mode flags */
	tcflag_t c_lflag;		/* local mode flags */
	cc_t c_line;			/* line discipline */
	cc_t c_cc[NCCS];		/* control characters */
	shim_speed_t c_ispeed;		/* input speed */
	shim_speed_t c_ospeed;		/* output speed */
};
#endif

/*
 *  Device information is held in a linked list of dev_info_t objects. Each
 *  nth element in the list also points to a unique device state which is
 *  the nth  dev_state_t structure in a memory mapped shared memory region.
 *  This allows child processes can keep the state up to date even if they die
 *  because the state information is also in the parent address space too.
 */
typedef struct {
	/*
	 *  These fields could be bit fields but updating would
	 *  need atomic operations on the bit fields. Make it simpler
	 *  by using bool types
	 */
	bool scsi_checked;	/* True if dev checked as SCSI dev */
	bool scsi_device;	/* True if SCSI dev */
	bool open_failed;	/* True if open failed */
	bool open_succeeded;	/* True if open succeeded */
} dev_state_t;

typedef struct dev_info {
	char *path;		/* Full path of device, e.g. /dev/null */
	char *name;		/* basename of path, e.g. null */
	uint32_t rnd_id;	/* randomized id for sorting */
	dev_state_t *state;	/* Pointer to shared memory state */
	struct dev_info *next;	/* Next device in device list */
} dev_info_t;

static const stress_help_t help[] = {
	{ NULL,	"dev N",	"start N device entry thrashing stressors" },
	{ NULL, "dev-file name","specify the /dev/ file to exercise" },
	{ NULL,	"dev-ops N",	"stop after N device thrashing bogo ops" },
	{ NULL,	NULL,		NULL }
};

static const stress_opt_t opts[] = {
	{ OPT_dev_file, "dev-file", TYPE_ID_STR, 0, 0, NULL },
	END_OPT,
};

#if defined(HAVE_POLL_H) &&		\
    defined(HAVE_POLL) &&		\
    defined(HAVE_LIB_PTHREAD) && 	\
    !defined(__sun__) && 		\
    !defined(__HAIKU__)

#define STRESS_DEV_THREADS_MAX		(4)
#define STRESS_DEV_OPEN_TRIES_MAX	(8)

typedef struct stress_dev_func {
	const char *devpath;
	const size_t devpath_len;
	void (*func)(stress_args_t *args, const int fd, const char *devpath);
} stress_dev_func_t;

typedef struct stress_sys_dev_info {
	struct stress_sys_dev_info	*next;
	char *sysdevpath;
} sys_dev_info_t;

static sigset_t set;
static shim_pthread_spinlock_t lock;
static shim_pthread_spinlock_t parport_lock;
static dev_info_t *pthread_dev_info;

#define VOID_ARGS(args, fd, devpath)	\
do {					\
	(void)args;			\
	(void)fd;			\
	(void)devpath;			\
} while (0)


/*
 *  linux_xen_guest()
 *	return true if stress-ng is running
 *	as a Linux Xen guest.
 */
static bool linux_xen_guest(void)
{
#if defined(__linux__)
	static bool xen_guest = false;
	static bool xen_guest_cached = false;
	struct stat statbuf;
	int ret;
	DIR *dp;
	struct dirent *de;

	if (xen_guest_cached)
		return xen_guest;

	/*
	 *  The features file is a good indicator for a Xen guest
	 */
	ret = shim_stat("/sys/hypervisor/properties/features", &statbuf);
	if (ret == 0) {
		xen_guest = true;
		goto done;
	}
	if (errno == EACCES) {
		xen_guest = true;
		goto done;
	}

	/*
	 *  Non-dot files in /sys/bus/xen/devices indicate a Xen guest too
	 */
	dp = opendir("/sys/bus/xen/devices");
	if (dp) {
		while ((de = readdir(dp)) != NULL) {
			if (de->d_name[0] != '.') {
				xen_guest = true;
				break;
			}
		}
		(void)closedir(dp);
		if (xen_guest)
			goto done;
	}

	/*
	 *  At this point Xen is being sneaky and pretending, so
	 *  apart from inspecting dmesg (which may not be possible),
	 *  assume it's not a Xen hosted guest.
	 */
	xen_guest = false;
done:
	xen_guest_cached = true;

	return xen_guest;
#else
	return false;
#endif
}

/*
 *  ioctl_set_timeout()
 *	set a itimer to interrupt ioctl call after secs seconds
 */
static void ioctl_set_timeout(const double secs)
{
#if defined(ITIMER_REAL)
	if (secs > 0.0) {
		struct itimerval it;
		time_t tsecs = (time_t)secs;

		it.it_interval.tv_sec = (time_t)secs;
		it.it_interval.tv_usec = (suseconds_t)(STRESS_DBL_MICROSECOND * (secs - (double)tsecs));
		it.it_value.tv_sec = it.it_interval.tv_sec;
		it.it_value.tv_usec = it.it_interval.tv_usec;
		VOID_RET(int, setitimer(ITIMER_REAL, &it, NULL));
	}
#else
	(void)secs;
#endif
}

/*
 *  ioctl_clr_timeout()
 *	clear itimer ioctl timeout alarm
 */
static void ioctl_clr_timeout(void)
{
#if defined(ITIMER_REAL)
	struct itimerval it;

	(void)shim_memset(&it, 0, sizeof(it));
	VOID_RET(int, setitimer(ITIMER_REAL, &it, NULL));
#endif
}

/*
 *   IOCTL_TIMEOUT()
 *	execute code, if time taken is > secs then
 *	execute the action.  Note there are limitations
 *	to the code that can be passed into the macro,
 *	variable declarations must be one variable at a
 *	time without any commas
 */
#define IOCTL_TIMEOUT(secs, code, action)		\
do {							\
	static bool timed_out_ = false;			\
	double timeout_t_ = stress_time_now();		\
							\
	if (!timed_out_) {				\
		ioctl_set_timeout(secs);		\
		code					\
		ioctl_clr_timeout();			\
	}						\
	if (stress_time_now() >= timeout_t_ + secs) {	\
		timed_out_ = true;			\
		action;					\
	}						\
} while (0)


#if defined(__linux__) && 		\
    defined(HAVE_LINUX_MEDIA_H) && 	\
    defined(MEDIA_IOC_DEVICE_INFO)
static void stress_dev_media_linux(
	stress_args_t *args,
	const int fd,
	const char *devpath)
{
	VOID_ARGS(args, fd, devpath);

#if defined(MEDIA_IOC_DEVICE_INFO) &&	\
    defined(HAVE_MEDIA_DEVICE_INFO)
	{
		struct media_device_info mdi;
		int ret;

		ret = ioctl(fd, MEDIA_IOC_DEVICE_INFO, &mdi);
		if (ret < 0)
			return;

		if (!mdi.driver[0])
			pr_inf("%s: ioctl MEDIA_IOC_DEVICE_INFO %s: null driver name\n",
				args->name, devpath);
		if (!mdi.model[0])
			pr_inf("%s: ioctl MEDIA_IOC_DEVICE_INFO %s: null model name\n",
				args->name, devpath);
		if (!mdi.bus_info[0])
			pr_inf("%s: ioctl MEDIA_IOC_DEVICE_INFO %s: null bus_info field\n",
				args->name, devpath);
	}
#endif
}
#endif

#if defined(__linux__) &&	\
    defined(HAVE_LINUX_VT_H)
static void stress_dev_vcs_linux(
	stress_args_t *args,
	const int fd,
	const char *devpath)
{
	VOID_ARGS(args, fd, devpath);

#if defined(VT_GETMODE) &&	\
    defined(HAVE_VT_MODE)
	{
		struct vt_mode mode;

		VOID_RET(int, ioctl(fd, VT_GETMODE, &mode));
	}
#endif
#if defined(VT_GETSTATE) &&	\
    defined(HAVE_VT_STAT)
	{
		struct vt_stat vt_stat;

		VOID_RET(int, ioctl(fd, VT_GETSTATE, &vt_stat));
	}
#endif
}
#endif

#if defined(__linux__) &&	\
    defined(HAVE_LINUX_DM_IOCTL_H)
static void stress_dev_dm_linux(
	stress_args_t *args,
	const int fd,
	const char *devpath)
{
	VOID_ARGS(args, fd, devpath);

#if defined(DM_VERSION) &&	\
    defined(HAVE_DM_IOCTL)
	{
		uint8_t buf[sizeof(struct dm_ioctl) + 4096];
		struct dm_ioctl *dm = (struct dm_ioctl *)buf;

		shim_memset(buf, 0, sizeof(buf));
		dm->version[0] = DM_VERSION_MAJOR;
		dm->version[1] = DM_VERSION_MINOR;
		dm->version[2] = 0;
		VOID_RET(int, ioctl(fd, DM_VERSION, dm));

		/* and try illegal version info */
		shim_memset(buf, 0, sizeof(buf));
		dm->version[0] = ~DM_VERSION_MAJOR;
		dm->version[1] = ~DM_VERSION_MINOR;
		dm->version[2] = ~0;
		VOID_RET(int, ioctl(fd, DM_VERSION, dm));
	}
#endif
#if defined(DM_LIST_DEVICES) &&	\
    defined(HAVE_DM_IOCTL)
	{
		uint8_t buf[sizeof(struct dm_ioctl) + 4096];
		struct dm_ioctl *dm = (struct dm_ioctl *)buf;

		shim_memset(buf, 0, sizeof(buf));
		dm->version[0] = DM_VERSION_MAJOR;
		dm->version[1] = DM_VERSION_MINOR;
		dm->version[2] = 0;
		dm->data_size = 4096;
		dm->data_start = sizeof(struct dm_ioctl);

		if (ioctl(fd, DM_LIST_DEVICES, dm) == 0) {
			struct dm_name_list *nl = (struct dm_name_list *)(buf + dm->data_start);
			uint32_t i;

			for (i = 0; i < dm->data_size; i++) {
#if defined(DM_DEV_STATUS)
				if (strlen(nl->name) < 4096) {

					uint8_t buf2[sizeof(struct dm_ioctl) + 4096];
					struct dm_ioctl *dm2 = (struct dm_ioctl *)buf2;

					shim_memset(buf2, 0, sizeof(buf2));
					dm2->version[0] = DM_VERSION_MAJOR;
					dm2->version[1] = DM_VERSION_MINOR;
					dm2->version[2] = 0;
					dm2->data_size = 4096;
					dm2->data_start = sizeof(struct dm_ioctl);
					(void)shim_strscpy(dm2->name, nl->name, sizeof(dm2->name));
					VOID_RET(int, ioctl(fd, DM_DEV_STATUS, dm2));

					/* and exercise invalid dev name */
					shim_memset(buf2, 0, sizeof(buf2));
					dm2->version[0] = DM_VERSION_MAJOR;
					dm2->version[1] = DM_VERSION_MINOR;
					dm2->version[2] = 0;
					dm2->data_size = 4096;
					dm2->data_start = sizeof(struct dm_ioctl);
					stress_rndstr(dm2->name, 32);
					VOID_RET(int, ioctl(fd, DM_DEV_STATUS, dm2));
				}
#endif
				if (nl->next == 0)
					break;
				nl = (struct dm_name_list *)((uintptr_t)nl + nl->next);
			}
		}
	}
#endif

#if defined(DM_LIST_VERSIONS) &&	\
    defined(HAVE_DM_IOCTL)
	{
		uint8_t buf[sizeof(struct dm_ioctl) + 4096];
		struct dm_ioctl *dm = (struct dm_ioctl *)buf;

		shim_memset(buf, 0, sizeof(buf));
		dm->version[0] = DM_VERSION_MAJOR;
		dm->version[1] = DM_VERSION_MINOR;
		dm->version[2] = 0;
		dm->data_size = 4096;
		dm->data_start = sizeof(buf);
		VOID_RET(int, ioctl(fd, DM_LIST_VERSIONS, dm));

		shim_memset(buf, 0, sizeof(buf));
		dm->version[0] = DM_VERSION_MAJOR;
		dm->version[1] = DM_VERSION_MINOR;
		dm->version[2] = 0;
		dm->data_size = ~0;
		dm->data_start = sizeof(buf);
		VOID_RET(int, ioctl(fd, DM_LIST_VERSIONS, dm));
	}
#endif

#if defined(RWF_NOWAIT) &&	\
    defined(O_DIRECT) &&	\
    defined(HAVE_PREADV2)
	{
		/*
		 *  exercise kernel fix to dm:
		 * "dm: don't attempt to queue IO under RCU protection"
		 * commit a9ce385344f916cd1c36a33905e564f5581beae9
		 */
		struct iovec iov;
		int fd2;

		fd2 = open(devpath, O_RDONLY | O_DIRECT);
		if (fd2 >= 0) {
			const size_t size = args->page_size;

			iov.iov_base = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
			if (iov.iov_base != MAP_FAILED) {
				iov.iov_len = size;
				VOID_RET(ssize_t, preadv2(fd, &iov, 1, 0, RWF_NOWAIT));
				(void)munmap(iov.iov_base, size);
			}
			(void)close(fd2);
		}
	}
#endif
}
#endif

#if defined(__linux__) &&	\
    defined(HAVE_LINUX_VIDEODEV2_H)
static void stress_dev_video_linux(
	stress_args_t *args,
	const int fd,
	const char *devpath)
{
	VOID_ARGS(args, fd, devpath);

#if defined(VIDIOC_QUERYCAP) &&	\
    defined(HAVE_V4L2_CAPABILITY)
	{
		struct v4l2_capability c;

		(void)shim_memset(&c, 0, sizeof(c));
		VOID_RET(int, ioctl(fd, VIDIOC_QUERYCAP, &c));
	}
#endif
#if defined(VIDIOC_G_FBUF) &&	\
    defined(HAVE_V4L2_FRAMEBUFFER)
	{
		struct v4l2_framebuffer f;

		(void)shim_memset(&f, 0, sizeof(f));
		VOID_RET(int, ioctl(fd, VIDIOC_G_FBUF, &f));
	}
#endif
#if defined(VIDIOC_G_STD) &&	\
    defined(HAVE_V4L2_STD_ID)
	{
		v4l2_std_id id;

		(void)shim_memset(&id, 0, sizeof(id));
		VOID_RET(int, ioctl(fd, VIDIOC_G_STD, &id));
	}
#endif
#if defined(VIDIOC_G_AUDIO) &&	\
    defined(HAVE_V4L2_AUDIO)
	{
		struct v4l2_audio a;

		(void)shim_memset(&a, 0, sizeof(a));
		VOID_RET(int, ioctl(fd, VIDIOC_G_AUDIO, &a));
	}
#endif
#if defined(VIDIOC_G_INPUT)
	{
		int in = 0;

		VOID_RET(int, ioctl(fd, VIDIOC_G_INPUT, &in));
	}
#endif
#if defined(VIDIOC_G_OUTPUT)
	{
		int in = 0;

		VOID_RET(int, ioctl(fd, VIDIOC_G_OUTPUT, &in));
	}
#endif
#if defined(VIDIOC_G_AUDOUT) &&	\
    defined(HAVE_V4L2_AUDIOOUT)
	{
		struct v4l2_audioout a;

		(void)shim_memset(&a, 0, sizeof(a));
		VOID_RET(int, ioctl(fd, VIDIOC_G_AUDOUT, &a));
	}
#endif
#if defined(VIDIOC_G_JPEGCOMP) && \
    defined(HAVE_V4L2_JPEGCOMPRESSION)
	{
		struct v4l2_jpegcompression a;

		(void)shim_memset(&a, 0, sizeof(a));
		VOID_RET(int, ioctl(fd, VIDIOC_G_JPEGCOMP, &a));
	}
#endif
#if defined(VIDIOC_QUERYSTD) &&	\
    defined(HAVE_V4L2_STD_ID)
	{
		v4l2_std_id a;

		(void)shim_memset(&a, 0, sizeof(a));
		VOID_RET(int, ioctl(fd, VIDIOC_QUERYSTD, &a));
	}
#endif
#if defined(VIDIOC_G_PRIORITY)
	{
		uint32_t a;

		VOID_RET(int, ioctl(fd, VIDIOC_G_PRIORITY, &a));
	}
#endif
#if defined(VIDIOC_G_ENC_INDEX) &&	\
    defined(HAVE_V4L2_ENC_IDX)
	{
		struct v4l2_enc_idx a;

		(void)shim_memset(&a, 0, sizeof(a));
		VOID_RET(int, ioctl(fd, VIDIOC_G_ENC_INDEX, &a));
	}
#endif
#if defined(VIDIOC_QUERY_DV_TIMINGS) &&	\
    defined(HAVE_V4L2_DV_TIMINGS)
	{
		struct v4l2_dv_timings a;

		(void)shim_memset(&a, 0, sizeof(a));
		VOID_RET(int, ioctl(fd, VIDIOC_QUERY_DV_TIMINGS, &a));
	}
#endif
}
#endif

#if defined(HAVE_TERMIOS_H) &&	\
    defined(HAVE_TERMIOS) &&	\
    defined(TCGETS)
static void stress_dev_tty(
	stress_args_t *args,
	const int fd,
	const char *devpath)
{
	struct termios t;
	int ret;

	VOID_ARGS(args, fd, devpath);

	if (!isatty(fd))
		return;

	VOID_RET(int, tcgetattr(fd, &t));
#if defined(TCGETS)
	{
		ret = ioctl(fd, TCGETS, &t);
#if defined(TCSETS)
		if (ret == 0) {
			ret = ioctl(fd, TCSETS, &t);
		}
#endif
		(void)ret;
	}
#endif
#if defined(TIOCGPTLCK)
	{
		int lck;

		ret = ioctl(fd, TIOCGPTLCK, &lck);
#if defined(TIOCSPTLCK)
		if (ret == 0) {
			ret = ioctl(fd, TIOCSPTLCK, &lck);
		}
#endif
		(void)ret;
	}
#endif
#if defined(TIOCGPKT)
	{
		int pktmode;

		ret = ioctl(fd, TIOCGPKT, &pktmode);
#if defined(TIOCPKT)
		if (ret == 0) {
			ret = ioctl(fd, TIOCPKT, &pktmode);
		}
#endif
		(void)ret;
	}
#endif
#if defined(TIOCGPTN)
	{
		int ptnum;

		VOID_RET(int, ioctl(fd, TIOCGPTN, &ptnum));
	}
#endif
#if defined(TIOCSIG) &&	\
    defined(SIGCONT)
	{
		int sig = SIGCONT;

		/* generally causes EINVAL */
		VOID_RET(int, ioctl(fd, TIOCSIG, &sig));
	}
#endif
#if defined(TIOCGWINSZ) && \
    defined(HAVE_WINSIZE)
	{
		struct winsize ws;

		ret = ioctl(fd, TIOCGWINSZ, &ws);
#if defined(TIOCSWINSZ)
		if (ret == 0) {
			ret = ioctl(fd, TIOCSWINSZ, &ws);
		}
#endif
		(void)ret;
	}
#endif
#if defined(FIONREAD)
	{
		int n;

		VOID_RET(int, ioctl(fd, FIONREAD, &n));
	}
#endif
#if defined(TIOCINQ)
	{
		int n;

		VOID_RET(int, ioctl(fd, TIOCINQ, &n));
	}
#endif
#if defined(TIOCOUTQ)
	{
		int n;

		VOID_RET(int, ioctl(fd, TIOCOUTQ, &n));
	}
#endif
#if defined(TIOCGPGRP)
	{
		pid_t pgrp;

		ret = ioctl(fd, TIOCGPGRP, &pgrp);
#if defined(TIOCSPGRP)
		if (ret == 0) {
			ret = ioctl(fd, TIOCSPGRP, &pgrp);
		}
#endif
		(void)ret;
	}
#endif
#if defined(TIOCGSID)
	{
		pid_t gsid;

		VOID_RET(int, ioctl(fd, TIOCGSID, &gsid));
	}
#endif
#if defined(TIOCGEXCL)
	{
		int excl;

		ret = ioctl(fd, TIOCGEXCL, &excl);
		if (ret == 0) {
#if defined(TIOCNXCL) &&	\
    defined(TIOCEXCL)
			if (excl) {
				VOID_RET(int, ioctl(fd, TIOCNXCL, NULL));
				VOID_RET(int, ioctl(fd, TIOCEXCL, NULL));
			} else {
				VOID_RET(int, ioctl(fd, TIOCEXCL, NULL));
				VOID_RET(int, ioctl(fd, TIOCNXCL, NULL));
			}
#endif
		}
		(void)ret;
	}
#endif
/*
 *  On some older 3.13 kernels this can lock up, need to add
 *  a method to detect and skip this somehow. For the moment
 *  disable this stress test.
 */
#if defined(TIOCGETD)
	{
		int ldis;

		ret = ioctl(fd, TIOCGETD, &ldis);
#if defined(TIOCSETD)
		if (ret == 0) {
			ret = ioctl(fd, TIOCSETD, &ldis);
		}
#endif
		(void)ret;
	}
#endif

#if defined(TIOCGPTPEER)
	{
		VOID_RET(int, ioctl(fd, TIOCGPTPEER, O_RDWR));
	}
#endif

#if STRESS_DEV_EXERCISE_TCXONC
#if defined(TCXONC) &&	\
    defined(TCOOFF) && 	\
    defined(TCOON)
	{
		ret = ioctl(fd, TCXONC, TCOOFF);
		if (ret == 0)
			ret = ioctl(fd, TCXONC, TCOON);
		(void)ret;
	}
#endif

#if defined(TCXONC) &&	\
    defined(TCIOFF) &&	\
    defined(TCION)
	{
		ret = ioctl(fd, TCXONC, TCIOFF);
		if (ret == 0)
			ret = ioctl(fd, TCXONC, TCION);
		(void)ret;
	}
#endif
#endif

#if defined(TIOCCONS)
	{
		VOID_RET(int, ioctl(fd, TIOCCONS, 0));
	}
#endif

	/* Modem */
#if defined(TIOCGSOFTCAR)
	{
		int flag;

		ret = ioctl(fd, TIOCGSOFTCAR, &flag);
#if defined(TIOCSSOFTCAR)
		if (ret == 0) {
			ret = ioctl(fd, TIOCSSOFTCAR, &flag);
		}
#endif
		(void)ret;
	}
#endif

#if defined(KDGETLED)
	{
		char state;

		VOID_RET(int, ioctl(fd, KDGETLED, &state));
	}
#endif

#if defined(KDGKBTYPE)
	{
		char type;

		VOID_RET(int, ioctl(fd, KDGKBTYPE, &type));
	}
#endif

#if defined(KDGETMODE)
	{
		int mode;

		VOID_RET(int, ioctl(fd, KDGETMODE, &mode));
	}
#endif

#if defined(KDGKBMODE)
	{
		long int mode;

		VOID_RET(int, ioctl(fd, KDGKBMODE, &mode));
	}
#endif

#if defined(KDGKBMETA)
	{
		long int mode;

		VOID_RET(int, ioctl(fd, KDGKBMETA, &mode));
	}
#endif
#if defined(TIOCMGET)
	{
		int status;

		ret = ioctl(fd, TIOCMGET, &status);
#if defined(TIOCMSET)
		if (ret == 0) {
#if defined(TIOCMBIC)
			VOID_RET(int, ioctl(fd, TIOCMBIC, &status));
#endif
#if defined(TIOCMBIS)
			VOID_RET(int, ioctl(fd, TIOCMBIS, &status));
#endif
			ret = ioctl(fd, TIOCMSET, &status);
		}
#endif
		(void)ret;
	}
#endif
#if defined(TIOCGICOUNT) &&		\
    defined(HAVE_LINUX_SERIAL_H) &&	\
    defined(HAVE_SERIAL_ICOUNTER)
	{
		struct serial_icounter_struct counter;

		VOID_RET(int, ioctl(fd, TIOCGICOUNT, &counter));
	}
#endif
#if defined(TIOCGSERIAL) &&		\
    defined(HAVE_LINUX_SERIAL_H) &&	\
    defined(HAVE_SERIAL_STRUCT)
	{
		struct serial_struct serial;

		ret = ioctl(fd, TIOCGSERIAL, &serial);
#if defined(TIOCSSERIAL)
		if (ret == 0)
			ret = ioctl(fd, TIOCSSERIAL, &serial);
#endif
		(void)ret;
	}
#endif

#if defined(HAVE_SHIM_TERMIOS2) &&	\
    defined(TCGETS2)
	{
#define termios2 shim_termios2
		struct termios2 t2;

		ret = ioctl(fd, TCGETS2, &t2);
		(void)ret;
#if defined(TCSETS2) ||		\
    defined(TCSETSW2) ||	\
    defined(TCSETSF2)
		if (ret == 0) {
#if defined(TCSETSF2)
			VOID_RET(int, ioctl(fd, TCSETSF2, &t2));
#endif
#if defined(TCSETSW2)
			VOID_RET(int, ioctl(fd, TCSETSW2, &t2));
#endif
#if defined(TCSETS2)
			VOID_RET(int, ioctl(fd, TCSETS2, &t2));
			(void)ret;
#endif
		}
#endif
	}
#endif
}
#endif

/*
 *  stress_dev_blk()
 *	block device specific ioctls
 */
static void stress_dev_blk(
	stress_args_t *args,
	const int fd,
	const char *devpath)
{
	off_t offset;

	VOID_ARGS(args, fd, devpath);

#if defined(BLKFLSBUF)
	{
		VOID_RET(int, ioctl(fd, BLKFLSBUF, 0));
	}
#endif

#if defined(BLKRAGET)
	/* readahead */
	{
		unsigned long int ra;
		int ret;

		ret = ioctl(fd, BLKRAGET, &ra);
#if defined(BLKRASET)
		if (ret == 0)
			ret = ioctl(fd, BLKRASET, ra);
#endif
		(void)ret;
	}
#endif

#if defined(BLKFRAGET)
	/* readahead */
	{
		unsigned long int fra;
		int ret;

		ret = ioctl(fd, BLKFRAGET, &fra);
#if defined(BLKFRASET)
		if (ret == 0)
			ret = ioctl(fd, BLKFRASET, fra);
#endif
		(void)ret;
	}
#endif

#if defined(BLKROGET)
	/* readonly state */
	{
		int ret, ro;

		ret = ioctl(fd, BLKROGET, &ro);
#if defined(BLKROSET)
		if (ret == 0)
			ret = ioctl(fd, BLKROSET, &ro);
#endif
		(void)ret;
	}
#endif

#if defined(BLKBSZGET)
	/* get block device soft block size */
	{
		int ret, sz;

		ret = ioctl(fd, BLKBSZGET, &sz);
#if defined(BLKBSZSET)
		if (ret == 0)
			ret = ioctl(fd, BLKBSZSET, &sz);
#endif
		(void)ret;
	}
#endif

#if defined(BLKPBSZGET)
	/* get block device physical block size */
	{
		unsigned int sz;

		VOID_RET(int, ioctl(fd, BLKPBSZGET, &sz));
	}
#endif

#if defined(BLKIOMIN)
	{
		unsigned int sz;

		VOID_RET(int, ioctl(fd, BLKIOMIN, &sz));
	}
#endif

#if defined(BLKIOOPT)
	{
		unsigned int sz;

		VOID_RET(int, ioctl(fd, BLKIOOPT, &sz));
	}
#endif

#if defined(BLKALIGNOFF)
	{
		unsigned int sz;

		VOID_RET(int, ioctl(fd, BLKALIGNOFF, &sz));
	}
#endif

#if defined(BLKROTATIONAL)
	{
		unsigned short int rotational;

		VOID_RET(int, ioctl(fd, BLKROTATIONAL, &rotational));
	}
#endif

#if defined(BLKSECTGET)
	{
		unsigned short int max_sectors;

		VOID_RET(int, ioctl(fd, BLKSECTGET, &max_sectors));
	}
#endif

#if defined(BLKGETSIZE)
	{
		unsigned long int sz;

		VOID_RET(int, ioctl(fd, BLKGETSIZE, &sz));
	}
#endif

#if defined(BLKGETSIZE64)
	{
		uint64_t sz;

		VOID_RET(int, ioctl(fd, BLKGETSIZE64, &sz));
	}
#endif

#if defined(BLKGETDISKSEQ)
	{
		uint64_t diskseq;

		VOID_RET(int, ioctl(fd, BLKGETDISKSEQ, &diskseq));
	}
#endif

#if defined(BLKGETZONESZ)
	{
		uint32_t sz;

		VOID_RET(int, ioctl(fd, BLKGETZONESZ, &sz));
	}
#endif

#if defined(BLKGETNRZONES)
	{
		uint32_t sz;

		VOID_RET(int, ioctl(fd, BLKGETNRZONES, &sz));
	}
#endif

	offset = lseek(fd, 0, SEEK_END);
	stress_uint64_put((uint64_t)offset);

	offset = lseek(fd, 0, SEEK_SET);
	stress_uint64_put((uint64_t)offset);

	offset = lseek(fd, 0, SEEK_CUR);
	stress_uint64_put((uint64_t)offset);
}

static inline char *stress_dev_basename(char *devpath)
{
	char *ptr = devpath;
	char *base = devpath;

	while (*ptr) {
		if ((*ptr == '/') && (*(ptr + 1)))
			base = ptr + 1;
		ptr++;
	}

	return base;
}

#if defined(__linux__)
static inline bool is_scsi_dev(dev_info_t *dev_info)
{
	int i, n;
	static const char scsi_device_path[] = "/sys/class/scsi_device/";
	struct dirent **scsi_device_list;
	bool is_scsi = false;

	if (dev_info->state->scsi_device)
		return true;
	if (dev_info->state->scsi_checked)
		return false;

	dev_info->state->scsi_checked = true;

	scsi_device_list = NULL;
	n = scandir(scsi_device_path, &scsi_device_list, NULL, alphasort);
	if (n <= 0)
		return false;

	for (i = 0; !is_scsi && (i < n); i++) {
		int j, m;
		char scsi_block_path[PATH_MAX];
		struct dirent **scsi_block_list;

		if (scsi_device_list[i]->d_name[0] == '.')
			continue;

		(void)snprintf(scsi_block_path, sizeof(scsi_block_path),
			"%s/%s/device/block", scsi_device_path,
			scsi_device_list[i]->d_name);
		scsi_block_list = NULL;
		m = scandir(scsi_block_path, &scsi_block_list, NULL, alphasort);
		if (m <= 0)
			continue;

		for (j = 0; j < m; j++) {
			if (!strcmp(dev_info->name, scsi_block_list[j]->d_name)) {
				is_scsi = true;
				break;
			}
		}

		stress_dirent_list_free(scsi_block_list, m);
	}

	stress_dirent_list_free(scsi_device_list, n);

	if (is_scsi)
		dev_info->state->scsi_device = true;

	return is_scsi;
}

#else
static inline bool is_scsi_dev(dev_info_t *dev_info)
{
	(void)dev_info;

	/* Assume not */
	return false;
}
#endif

/*
 *  stress_dev_scsi_blk()
 *	SCSI block device specific ioctls
 */
static void stress_dev_scsi_blk(
	stress_args_t *args,
	const int fd,
	const char *devpath)
{
	VOID_ARGS(args, fd, devpath);

#if defined(SG_GET_VERSION_NUM)
	{
		int ver;

		VOID_RET(int, ioctl(fd, SG_GET_VERSION_NUM, &ver));
	}
#endif
#if defined(SCSI_IOCTL_GET_IDLUN)
	{
		struct sng_scsi_idlun {
			int four_in_one;
			int host_unique_id;
		} lun;

		(void)shim_memset(&lun, 0, sizeof(lun));
		VOID_RET(int, ioctl(fd, SCSI_IOCTL_GET_IDLUN, &lun));
	}
#endif
#if defined(SCSI_IOCTL_GET_BUS_NUMBER)
	{
		int bus;

		VOID_RET(int, ioctl(fd, SCSI_IOCTL_GET_BUS_NUMBER, &bus));
	}
#endif
#if defined(SG_GET_TIMEOUT)
	{
		VOID_RET(int, ioctl(fd, SG_GET_TIMEOUT, 0));
	}
#endif
#if defined(SG_GET_RESERVED_SIZE)
	{
		int sz;

		VOID_RET(int, ioctl(fd, SG_GET_RESERVED_SIZE, &sz));
	}
#endif
#if defined(SCSI_IOCTL_GET_PCI)
	{
		/* Old ioctl was 20 chars, new API 8 chars, 32 is plenty */
		char pci[32];

		VOID_RET(int, ioctl(fd, SCSI_IOCTL_GET_PCI, pci));
	}
#endif
}

#if defined(__linux__) &&	\
    defined(HAVE_SCSI_SG_H)
/*
 *  stress_dev_scsi_generic_linux()
 *	SCSI generic device specific ioctls for linux
 */
static void stress_dev_scsi_generic_linux(
	stress_args_t *args,
	const int fd,
	const char *devpath)
{
	VOID_ARGS(args, fd, devpath);

#if defined(SG_GET_VERSION_NUM)
	{
		int version = 0;

		VOID_RET(int, ioctl(fd, SG_GET_VERSION_NUM, &version));
	}
#endif
#if defined(SG_GET_TIMEOUT)
	{
		VOID_RET(int, ioctl(fd, SG_GET_TIMEOUT, 0));
	}
#endif
#if defined(SG_GET_LOW_DMA)
	{
		int low;

		VOID_RET(int, ioctl(fd, SG_GET_LOW_DMA, &low));
	}
#endif
#if defined(SG_GET_PACK_ID)
	{
		int pack_id;

		VOID_RET(int, ioctl(fd, SG_GET_PACK_ID, &pack_id));
	}
#endif
#if defined(SG_GET_NUM_WAITING)
	{
		int n;

		VOID_RET(int, ioctl(fd, SG_GET_NUM_WAITING, &n));
	}
#endif
#if defined(SG_GET_SG_TABLESIZE)
	{
		int size;

		VOID_RET(int, ioctl(fd, SG_GET_SG_TABLESIZE, &size));
	}
#endif
#if defined(SG_GET_RESERVED_SIZE)
	{
		int size;

		VOID_RET(int, ioctl(fd, SG_GET_RESERVED_SIZE, &size));
	}
#endif
#if defined(SG_GET_COMMAND_Q)
	{
		int cmd_q;

		VOID_RET(int, ioctl(fd, SG_GET_COMMAND_Q, &cmd_q));
	}
#endif
#if defined(SG_GET_ACCESS_COUNT)
	{
		int n;

		VOID_RET(int, ioctl(fd, SG_GET_ACCESS_COUNT, &n));
	}
#endif
#if defined(SCSI_IOCTL_GET_IDLUN)
	{
		struct shim_scsi_idlun {
			int four_in_one;
			int host_unique_id;
		} idlun;

		VOID_RET(int, ioctl(fd, SCSI_IOCTL_GET_IDLUN, &idlun));
	}
#endif
#if defined(SCSI_IOCTL_GET_BUS_NUMBER)
	{
		int bus;

		VOID_RET(int, ioctl(fd, SCSI_IOCTL_GET_BUS_NUMBER, &bus));
	}
#endif
#if defined(SG_GET_TRANSFORM)
	{
		VOID_RET(int, ioctl(fd, SG_GET_TRANSFORM, 0));
	}
#endif
#if defined(SG_EMULATED_HOST)
	{
		int emulated;

		VOID_RET(int, ioctl(fd, SG_EMULATED_HOST, &emulated));
	}
#endif
#if defined(BLKSECTGET)
	{
		int n;

		VOID_RET(int, ioctl(fd, BLKSECTGET, &n));
	}
#endif
/*
#if defined(SCSI_IOCTL_SYNC)
	{
		VOID_RET(int, ioctl(fd, SCSI_IOCTL_SYNC, 0));
	}
#endif
*/
}
#endif

#if defined(__linux__) &&	\
    defined(HAVE_LINUX_RANDOM_H)
/*
 *  stress_dev_random_linux()
 *	Linux /dev/random ioctls
 */
static void stress_dev_random_linux(
	stress_args_t *args,
	const int fd,
	const char *devpath)
{
	VOID_ARGS(args, fd, devpath);

#if defined(RNDGETENTCNT)
	{
		long int entropy;

		VOID_RET(int, ioctl(fd, RNDGETENTCNT, &entropy));
	}
#endif

#if defined(RNDGETPOOL)
	{
		int pool[2];

		VOID_RET(int, ioctl(fd, RNDGETPOOL, pool));
	}
#endif

#if defined(RNDRESEEDCRNG)
	{
		VOID_RET(int, ioctl(fd, RNDRESEEDCRNG, NULL));
	}
#endif

#if defined(RNDADDENTROPY)
	{
		char filename[PATH_MAX];
		int fd_rdwr;

		/*
		 *  Re-open fd with O_RDWR
		 */
		(void)snprintf(filename, sizeof(filename), "/proc/self/fd/%d", fd);
		fd_rdwr = open(filename, O_RDWR);
		if (fd_rdwr != -1) {
			const uint32_t rnd = stress_mwc32();
			struct shim_rand_pool_info {
				int	entropy_count;
				int	buf_size;
				uint8_t	buf[4];
			} info;
			const size_t sz = sizeof(info.buf);

			info.entropy_count = sz * 8;
			info.buf_size = sz;
			(void)shim_memcpy(&info.buf, &rnd, sz);

			VOID_RET(int, ioctl(fd_rdwr, RNDADDENTROPY, &info));
			(void)close(fd_rdwr);
		}
	}
#endif
}
#endif

#if defined(__linux__)
/*
 *  stress_dev_mem_mmap_linux()
 *	Linux mmap'ing on a device
 */
static void stress_dev_mem_mmap_linux(
	const int fd,
	const char *devpath,
	const bool read_page,
	const bool write_page)
{
	void *ptr;
	char *buffer;
	const size_t page_size = stress_get_page_size();

#if !defined(STRESS_ARCH_X86)
	/* voidify for non-x86 */
	(void)devpath;
	(void)write_page;
#endif

	buffer = mmap(NULL, page_size, PROT_READ | PROT_WRITE,
		MAP_PRIVATE | MAP_ANONYMOUS, fd, 0);

	ptr = mmap(NULL, page_size, PROT_READ, MAP_PRIVATE, fd, 0);
	if (ptr != MAP_FAILED) {
		(void)munmap(ptr, page_size);
	}
	if (read_page && (buffer != MAP_FAILED)) {
		off_t off;

		/* Try seeking */
		off = lseek(fd, (off_t)0, SEEK_SET);
#if defined(STRESS_ARCH_X86)
		if (off == 0) {
			ssize_t ret;

			/* And try reading */
			ret = read(fd, buffer, page_size);
			(void)ret;
		}
#else
		(void)off;
#endif
	}

	ptr = mmap(NULL, page_size, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
	if (ptr != MAP_FAILED)
		(void)munmap(ptr, page_size);
#if defined(STRESS_ARCH_X86)
	if (write_page && (buffer != MAP_FAILED)) {
		int fdwr;

		fdwr = open(devpath, O_RDWR);
		if (fdwr >= 0) {
			if (lseek(fdwr, (off_t)0, SEEK_SET) == 0) {
				ssize_t ret;

				/* Page zero, offset zero should contain zero */
				buffer[0] = 0;
				ret = write(fdwr, buffer, 1);
				(void)ret;
			}
			(void)close(fdwr);
		}
	}
#endif
	if (buffer != MAP_FAILED)
		(void)munmap((void *)buffer, page_size);
}

static void stress_dev_mem_linux(
	stress_args_t *args,
	const int fd,
	const char *devpath)
{
	VOID_ARGS(args, fd, devpath);

	stress_dev_mem_mmap_linux(fd, devpath, true, true);
}
#endif

#if defined(__linux__) &&	\
    defined(HAVE_LINUX_RTC_H)
static void stress_dev_rtc_linux(
	stress_args_t *args,
	const int fd,
	const char *devpath)
{
	VOID_ARGS(args, fd, devpath);

#if !defined(RTC_IRQP_READ32) &&	\
    defined(_IOR)
#define RTC_IRQP_READ32               _IOR('p', 0x0b, uint32_t)
#endif

#if defined(RTC_IRQP_READ)
	{
		unsigned long int irqp;

		VOID_RET(int, ioctl(fd, RTC_IRQP_READ, &irqp));
	}
#endif
#if defined(RTC_IRQP_READ32)
	{
		uint32_t irqp;

		VOID_RET(int, ioctl(fd, RTC_IRQP_READ32, &irqp));
	}
#endif
#if defined(RTC_EPOCH_READ)
	{
		unsigned long int epoch;

		VOID_RET(int, ioctl(fd, RTC_EPOCH_READ, &epoch));
	}
#endif
#if defined(RTC_VL_READ)
	{
		unsigned int vl;

		VOID_RET(int, ioctl(fd, RTC_VL_READ, &vl));
	}
#endif
}
#endif

#if defined(__linux__)
static void stress_dev_kmem_linux(
	stress_args_t *args,
	const int fd,
	const char *devpath)
{
	VOID_ARGS(args, fd, devpath);

	stress_dev_mem_mmap_linux(fd, devpath, false, false);
}
#endif

#if defined(__linux__) &&		\
    defined(CDROMREADTOCENTRY) &&	\
    defined(HAVE_CDROM_MSF) &&		\
    defined(HAVE_CDROM_TOCENTRY)
/*
 * cdrom_get_address_msf()
 *      Given a track and fd, the function returns
 *      the address of the track in MSF format
 */
static void cdrom_get_address_msf(
	const int fd,
	const int track,
	uint8_t *min,
	uint8_t *seconds,
	uint8_t *frames)
{
	struct cdrom_tocentry entry;

	(void)shim_memset(&entry, 0, sizeof(entry));
	entry.cdte_track = (uint8_t)track;
	entry.cdte_format = CDROM_MSF;

	if (ioctl(fd, CDROMREADTOCENTRY, &entry) == 0) {
		*min = entry.cdte_addr.msf.minute;
		*seconds = entry.cdte_addr.msf.second;
		*frames = entry.cdte_addr.msf.frame;
	}
}
#endif

#if defined(__linux__)
/*
 * stress_cdrom_ioctl_msf()
 *      tests all CDROM ioctl syscalls that
 *      requires address argument in MSF Format
 */
static void stress_cdrom_ioctl_msf(const int fd)
{
	int starttrk = 0, endtrk = 0;

	(void)fd;
	(void)starttrk;
	(void)endtrk;

#if defined(CDROMREADTOCHDR) &&	\
    defined(HAVE_CDROM_MSF) &&	\
    defined(HAVE_CDROM_TOCHDR)
	IOCTL_TIMEOUT(0.10,
	{
		struct cdrom_tochdr header;
		/* Reading the number of tracks on disc */

		(void)shim_memset(&header, 0, sizeof(header));
		if (ioctl(fd, CDROMREADTOCHDR, &header) == 0) {
			starttrk = header.cdth_trk0;
			endtrk = header.cdth_trk1;
		}
	}, return);

	/* Return if endtrack is not set or starttrk is invalid */
	if ((endtrk == 0) && (starttrk != 0)) {
		return;
	}
#endif

#if defined(CDROMPLAYTRKIND) &&	\
    defined(HAVE_CDROM_TI) &&	\
    defined(CDROMPAUSE) && 0
	/* Play/pause is a bit rough on H/W, disable it for now */
	IOCTL_TIMEOUT(0.10, {
		struct cdrom_ti ti;

		(void)shim_memset(&ti, 0, sizeof(ti));
		ti.cdti_trk1 = endtrk;
		if (ioctl(fd, CDROMPLAYTRKIND, &ti) == 0)
			VOID_RET(int, ioctl(fd, CDROMPAUSE, 0));
	}, return);
#endif

#if defined(CDROMREADTOCENTRY) &&	\
    defined(HAVE_CDROM_MSF) &&		\
    defined(HAVE_CDROM_TOCENTRY)
	{
		struct cdrom_msf msf;

		/* Fetch address of start and end track in MSF format */
		(void)shim_memset(&msf, 0, sizeof(msf));
		cdrom_get_address_msf(fd, starttrk, &msf.cdmsf_min0,
			&msf.cdmsf_sec0, &msf.cdmsf_frame0);
		cdrom_get_address_msf(fd, endtrk, &msf.cdmsf_min1,
			&msf.cdmsf_sec1, &msf.cdmsf_frame1);

#if defined(CDROMPLAYMSF) && 	\
    defined(CDROMPAUSE)
		IOCTL_TIMEOUT(0.10, {
			if (ioctl(fd, CDROMPLAYMSF, &msf) == 0)
				VOID_RET(int, ioctl(fd, CDROMPAUSE, 0));
		}, return);
#endif

#if defined(CDROMREADRAW) &&	\
    defined(CD_FRAMESIZE_RAW)
		IOCTL_TIMEOUT(0.10, {
			union {
				struct cdrom_msf msf;		/* input */
				char buffer[CD_FRAMESIZE_RAW];	/* return */
			} arg;

			arg.msf = msf;
			VOID_RET(int, ioctl(fd, CDROMREADRAW, &arg));
		}, return);
#endif

#if defined(CDROMREADMODE1) &&	\
    defined(CD_FRAMESIZE)
		IOCTL_TIMEOUT(0.10, {
			union {
				struct cdrom_msf msf;		/* input */
				char buffer[CD_FRAMESIZE];	/* return */
			} arg;

			arg.msf = msf;
			VOID_RET(int, ioctl(fd, CDROMREADMODE1, &arg));
		}, return);
#endif

#if defined(CDROMREADMODE2) &&	\
    defined(CD_FRAMESIZE_RAW0)
		IOCTL_TIMEOUT(0.10, {
			union {
				struct cdrom_msf msf;		/* input */
				char buffer[CD_FRAMESIZE_RAW0];	/* return */
			} arg;

			arg.msf = msf;
			VOID_RET(int, ioctl(fd, CDROMREADMODE2, &arg));
		}, return);
#endif
	}
#endif
}
#endif

#if defined(__linux__)
static void stress_dev_cdrom_linux(
	stress_args_t *args,
	const int fd,
	const char *devpath)
{
	size_t i;

	static const char * const proc_files[] = {
		"autoclose",
		"autoeject",
		"check_media",
		"debug",
		"info",
		"lock",
	};

	VOID_ARGS(args, fd, devpath);

	for (i = 0; i < SIZEOF_ARRAY(proc_files); i++) {
		char path[PATH_MAX];
		char buf[4096];

		(void)snprintf(path, sizeof(path), "/proc/sys/dev/cdrom/%s", proc_files[i]);
		VOID_RET(ssize_t, stress_system_read(path, buf, sizeof(buf)));
	}

	stress_cdrom_ioctl_msf(fd);

#if defined(CDROMVOLREAD) &&		\
    defined(CDROMVOLCTRL) &&		\
    defined(HAVE_CDROM_VOLCTRL)
	IOCTL_TIMEOUT(0.10, {
		struct cdrom_volctrl volume;
		int ret;

		(void)shim_memset(&volume, 0, sizeof(volume));
		ret = ioctl(fd, CDROMVOLREAD, &volume);
		if (ret == 0)
			ret = ioctl(fd, CDROMVOLCTRL, &volume);
		(void)ret;
	}, return);
#endif
#if defined(CDROM_GET_MCN) &&	\
    defined(HAVE_CDROM_MCN)
	IOCTL_TIMEOUT(0.10, {
		struct cdrom_mcn mcn;

		(void)shim_memset(&mcn, 0, sizeof(mcn));
		VOID_RET(int, ioctl(fd, CDROM_GET_MCN, &mcn));
	}, return);
#endif
#if defined(CDROMREADTOCHDR) &&		\
    defined(HAVE_CDROM_TOCHDR)
	IOCTL_TIMEOUT(0.10, {
		struct cdrom_tochdr header;

		(void)shim_memset(&header, 0, sizeof(header));
		VOID_RET(int, ioctl(fd, CDROMREADTOCHDR, &header));
	}, return);
#endif
#if defined(CDROMREADTOCENTRY) &&	\
    defined(HAVE_CDROM_TOCENTRY)
	IOCTL_TIMEOUT(0.10, {
		struct cdrom_tocentry entry;

		(void)shim_memset(&entry, 0, sizeof(entry));
		VOID_RET(int, ioctl(fd, CDROMREADTOCENTRY, &entry));
	}, return);
#endif
#if defined(CDROMSUBCHNL) &&	\
    defined(HAVE_CDROM_SUBCHNL)
	IOCTL_TIMEOUT(0.10, {
		struct cdrom_subchnl q;

		(void)shim_memset(&q, 0, sizeof(q));
#if defined(CDROM_LBA)
		q.cdsc_format = CDROM_LBA;
#elif defined(CDROM_MSF)
		q.cdsc_format = CDROM_MSF;
#endif
		VOID_RET(int, ioctl(fd, CDROMSUBCHNL, &q));
	}, return);
#endif
#if defined(CDROMREADAUDIO) &&	\
    defined(HAVE_CDROM_READ_AUDIO)
	IOCTL_TIMEOUT(0.10, {
		struct cdrom_read_audio ra;

		(void)shim_memset(&ra, 0, sizeof(ra));
		VOID_RET(int, ioctl(fd, CDROMREADAUDIO, &ra));
	}, return);
#endif
#if defined(CDROMREADCOOKED) &&	\
    defined(CD_FRAMESIZE)
	IOCTL_TIMEOUT(0.10, {
		uint8_t buffer[CD_FRAMESIZE];

		(void)shim_memset(&buffer, 0, sizeof(buffer));
		VOID_RET(int, ioctl(fd, CDROMREADCOOKED, buffer));
	}, return);
#endif
#if defined(CDROMREADALL) &&	\
    defined(CD_FRAMESIZE)
	IOCTL_TIMEOUT(0.10, {
		uint8_t buffer[CD_FRAMESIZE];

		(void)shim_memset(&buffer, 0, sizeof(buffer));
		VOID_RET(int, ioctl(fd, CDROMREADALL, buffer));
	}, return);
#endif
#if defined(CDROMSEEK) &&	\
    defined(HAVE_CDROM_MSF)
	IOCTL_TIMEOUT(0.10, {
		struct cdrom_msf msf;

		(void)shim_memset(&msf, 0, sizeof(msf));
		VOID_RET(int, ioctl(fd, CDROMSEEK, &msf));
	}, return);
#endif
#if defined(CDROMGETSPINDOWN)
	{
		char spindown;
		int ret;

		ret = ioctl(fd, CDROMGETSPINDOWN, &spindown);
#if defined(CDROMSETSPINDOWN)
		if (ret == 0) {
			char bad_val = ~0;

			ret = ioctl(fd, CDROMSETSPINDOWN, &spindown);
			(void)ret;

			/*
			 * Exercise Invalid CDROMSETSPINDOWN ioctl call with
			 * invalid spindown as it could contain only 4 set bits
			 */
			ret = ioctl(fd, CDROMSETSPINDOWN, bad_val);
			if (ret == 0) {
				/* Unexpected success, so set it back */
				ret = ioctl(fd, CDROMSETSPINDOWN, &spindown);
			}
		}
#endif
		(void)ret;
	}
#endif

#if defined(CDROM_DISC_STATUS)
	IOCTL_TIMEOUT(0.10, { VOID_RET(int, ioctl(fd, CDROM_DISC_STATUS, 0)); }, return);
#endif

#if defined(CDROM_GET_CAPABILITY)
	IOCTL_TIMEOUT(0.10, { VOID_RET(int, ioctl(fd, CDROM_GET_CAPABILITY, 0)); }, return);
#endif
#if defined(CDROM_CHANGER_NSLOTS)
	IOCTL_TIMEOUT(0.10, { VOID_RET(int, ioctl(fd, CDROM_CHANGER_NSLOTS, 0)); }, return);
#endif
#if defined(CDROM_NEXT_WRITABLE)
	IOCTL_TIMEOUT(0.10, {
		long int next;

		VOID_RET(int, ioctl(fd, CDROM_NEXT_WRITABLE, &next));
	}, return);
#endif
#if defined(CDROM_LAST_WRITTEN)
	IOCTL_TIMEOUT(0.10, {
		long int last;

		VOID_RET(int, ioctl(fd, CDROM_LAST_WRITTEN, &last));
	}, return);
#endif
#if defined(CDROM_MEDIA_CHANGED) && 0
	IOCTL_TIMEOUT(0.10, {
		int slot = 0;

		VOID_RET(int, ioctl(fd, CDROM_MEDIA_CHANGED, slot));
	}, return);
#endif
#if defined(CDSL_NONE)
	IOCTL_TIMEOUT(0.10, {
		int slot = CDSL_NONE;

		VOID_RET(int, ioctl(fd, CDROM_MEDIA_CHANGED, slot));
	}, return);
#endif
#if defined(CDSL_CURRENT)
	IOCTL_TIMEOUT(0.10, {
		int slot = CDSL_CURRENT;

		VOID_RET(int, ioctl(fd, CDROM_MEDIA_CHANGED, slot));
	}, return);
#endif
#if defined(CDROMSTART)
	IOCTL_TIMEOUT(0.10, { VOID_RET(int, ioctl(fd, CDROMSTART, 0)); }, return);
#endif
#if defined(CDROMPAUSE)
	IOCTL_TIMEOUT(0.10, { VOID_RET(int, ioctl(fd, CDROMPAUSE, 0)); }, return);
#endif
#if defined(CDROMRESUME)
	IOCTL_TIMEOUT(0.10, { VOID_RET(int, ioctl(fd, CDROMRESUME, 0)); }, return);
#endif
#if defined(CDROMSTOP)
	IOCTL_TIMEOUT(0.10, { VOID_RET(int, ioctl(fd, CDROMSTOP, 0)); }, return);
#endif
#if defined(CDROM_DRIVE_STATUS)
	IOCTL_TIMEOUT(0.10, {
		int slot = 0;

		VOID_RET(int, ioctl(fd, CDROM_DRIVE_STATUS, slot));
	}, return);
#if defined(CDSL_NONE)
	IOCTL_TIMEOUT(0.10, {
		int slot = CDSL_NONE;

		VOID_RET(int, ioctl(fd, CDROM_DRIVE_STATUS, slot));
	}, return);
#endif
#if defined(CDSL_CURRENT)
	IOCTL_TIMEOUT(0.10, {
		int slot = CDSL_CURRENT;

		VOID_RET(int, ioctl(fd, CDROM_DRIVE_STATUS, slot));
	}, return);
#endif
#endif
#if defined(DVD_READ_STRUCT) &&	\
    defined(HAVE_DVD_STRUCT)
	IOCTL_TIMEOUT(0.10, {
		dvd_struct s;

		/*
		 *  Invalid DVD_READ_STRUCT ioctl syscall with
		 *  invalid layer number resulting in EINVAL
		 */
		(void)shim_memset(&s, 0, sizeof(s));
		s.type = DVD_STRUCT_PHYSICAL;
		s.physical.layer_num = UINT8_MAX;
		VOID_RET(int, ioctl(fd, DVD_READ_STRUCT, &s));

		/*
		 *  Exercise each DVD structure type to cover all the
		 *  respective functions to increase kernel coverage
		 */
		(void)shim_memset(&s, 0, sizeof(s));
		s.type = DVD_STRUCT_PHYSICAL;
		VOID_RET(int, ioctl(fd, DVD_READ_STRUCT, &s));

		(void)shim_memset(&s, 0, sizeof(s));
		s.type = DVD_STRUCT_COPYRIGHT;
		VOID_RET(int, ioctl(fd, DVD_READ_STRUCT, &s));

		(void)shim_memset(&s, 0, sizeof(s));
		s.type = DVD_STRUCT_DISCKEY;
		VOID_RET(int, ioctl(fd, DVD_READ_STRUCT, &s));

		(void)shim_memset(&s, 0, sizeof(s));
		s.type = DVD_STRUCT_BCA;
		VOID_RET(int, ioctl(fd, DVD_READ_STRUCT, &s));

		(void)shim_memset(&s, 0, sizeof(s));
		s.type = DVD_STRUCT_MANUFACT;
		VOID_RET(int, ioctl(fd, DVD_READ_STRUCT, &s));

		/* Invalid DVD_READ_STRUCT call with invalid type argument */
		(void)shim_memset(&s, 0, sizeof(s));
		s.type = UINT8_MAX;
		VOID_RET(int, ioctl(fd, DVD_READ_STRUCT, &s));
	}, return);
#endif
#if defined(CDROMAUDIOBUFSIZ)
	IOCTL_TIMEOUT(0.10, {
		int val = INT_MIN;

		/* Invalid CDROMAUDIOBUFSIZ call with negative buffer size */
		VOID_RET(int, ioctl(fd, CDROMAUDIOBUFSIZ, val));
	}, return);
#endif
#if defined(DVD_AUTH) &&	\
    defined(HAVE_DVD_AUTHINFO)
	IOCTL_TIMEOUT(0.40, {
		dvd_authinfo ai;

		/* Invalid DVD_AUTH call with no credentials */
		(void)shim_memset(&ai, 0, sizeof(ai));
		VOID_RET(int, ioctl(fd, DVD_AUTH, &ai));

		/*
		 *  Exercise each DVD AUTH type to cover all the
		 *  respective code to increase kernel coverage
		 */
		(void)shim_memset(&ai, 0, sizeof(ai));
		ai.type = DVD_LU_SEND_AGID;
		VOID_RET(int, ioctl(fd, DVD_AUTH, &ai));

		(void)shim_memset(&ai, 0, sizeof(ai));
		ai.type = DVD_LU_SEND_KEY1;
		VOID_RET(int, ioctl(fd, DVD_AUTH, &ai));

		(void)shim_memset(&ai, 0, sizeof(ai));
		ai.type = DVD_LU_SEND_CHALLENGE;
		VOID_RET(int, ioctl(fd, DVD_AUTH, &ai));

		(void)shim_memset(&ai, 0, sizeof(ai));
		ai.type = DVD_LU_SEND_TITLE_KEY;
		VOID_RET(int, ioctl(fd, DVD_AUTH, &ai));

		(void)shim_memset(&ai, 0, sizeof(ai));
		ai.type = DVD_LU_SEND_ASF;
		VOID_RET(int, ioctl(fd, DVD_AUTH, &ai));

		(void)shim_memset(&ai, 0, sizeof(ai));
		ai.type = DVD_HOST_SEND_CHALLENGE;
		VOID_RET(int, ioctl(fd, DVD_AUTH, &ai));

		(void)shim_memset(&ai, 0, sizeof(ai));
		ai.type = DVD_HOST_SEND_KEY2;
		VOID_RET(int, ioctl(fd, DVD_AUTH, &ai));

		(void)shim_memset(&ai, 0, sizeof(ai));
		ai.type = DVD_INVALIDATE_AGID;
		VOID_RET(int, ioctl(fd, DVD_AUTH, &ai));

		(void)shim_memset(&ai, 0, sizeof(ai));
		ai.type = DVD_LU_SEND_RPC_STATE;
		VOID_RET(int, ioctl(fd, DVD_AUTH, &ai));

		(void)shim_memset(&ai, 0, sizeof(ai));
		ai.type = DVD_HOST_SEND_RPC_STATE;
		VOID_RET(int, ioctl(fd, DVD_AUTH, &ai));

		/* Invalid DVD_READ_STRUCT call with invalid type argument */
		(void)shim_memset(&ai, 0, sizeof(ai));
		ai.type = (uint8_t)~0;
		VOID_RET(int, ioctl(fd, DVD_AUTH, &ai));
	}, return);
#endif

#if defined(CDROM_DEBUG)
	IOCTL_TIMEOUT(0.10, {
		int debug;

		/* Enable the DEBUG Messages */
		debug = 1;
		VOID_RET(int, ioctl(fd, CDROM_DEBUG, debug));

		/* Disable the DEBUG Messages */
		debug = 0;
		VOID_RET(int, ioctl(fd, CDROM_DEBUG, debug));
	}, return);
#endif

#if defined(CDROM_LOCKDOOR)
	IOCTL_TIMEOUT(0.10, {
		int lockdoor;

		/* Lock */
		lockdoor = 1;
		VOID_RET(int, ioctl(fd, CDROM_LOCKDOOR, lockdoor));

		/* Unlock */
		lockdoor = 0;
		VOID_RET(int, ioctl(fd, CDROM_LOCKDOOR, lockdoor));
	}, return);
#endif

#if defined(CDROM_SET_OPTIONS)
	IOCTL_TIMEOUT(0.20, {
		int option = 0;	/* Just read options */
		int ret;

		/* Read */
		ret = ioctl(fd, CDROM_SET_OPTIONS, option);
		if (ret >= 0) {
			option = ret;
			/* Set (with current options) */
			VOID_RET(int, ioctl(fd, CDROM_SET_OPTIONS, option));
		}
#if defined(CDROM_CLEAR_OPTIONS)
		VOID_RET(int, ioctl(fd, CDROM_CLEAR_OPTIONS, 0));
		VOID_RET(int, ioctl(fd, CDROM_CLEAR_OPTIONS, option));
		VOID_RET(int, ioctl(fd, CDROM_SET_OPTIONS, option));
#endif
	}, return);
#endif

#if defined(CDROM_SELECT_DISC) &&	\
    defined(CDSL_CURRENT)
	IOCTL_TIMEOUT(0.10, { VOID_RET(int, ioctl(fd, CDROM_SELECT_DISC, CDSL_CURRENT)); }, return);
#endif

#if defined(CDROMCLOSETRAY)
	IOCTL_TIMEOUT(0.10, { VOID_RET(int, ioctl(fd, CDROMCLOSETRAY, 0)); }, return);
#endif

#if defined(CDROM_SELECT_SPEED)
	IOCTL_TIMEOUT(0.10, {
		unsigned int j;

		for (j = 8; j < 16; j++) {
			unsigned int speed = 1 << j;

			VOID_RET(int, ioctl(fd, CDROM_SELECT_SPEED, speed));
		}
	}, return);
#endif

#if defined(CDROMPLAYBLK) &&	\
    defined(HAVE_CDROM_BLK)
	IOCTL_TIMEOUT(0.10, {
		struct cdrom_blk blk;

		(void)shim_memset(&blk, 0, sizeof(blk));
		VOID_RET(int, ioctl(fd, CDROMPLAYBLK, &blk));
	}, return);
#endif
}
#endif

#if defined(__linux__)
static void stress_dev_console_linux(
	stress_args_t *args,
	const int fd,
	const char *devpath)
{
	VOID_ARGS(args, fd, devpath);

#if defined(HAVE_LINUX_KD_H) &&	\
    defined(KDGETLED)
	{
		char argp;
		int ret;

		ret = ioctl(fd, KDGETLED, &argp);
#if defined(KDSETLED)
		if (ret == 0) {
			const char bad_val = ~0;

			VOID_RET(int, ioctl(fd, KDSETLED, &argp));

			/* Exercise Invalid KDSETLED ioctl call with invalid flags */
			ret = ioctl(fd, KDSETLED, &bad_val);
			if (ret == 0) {
				/* Unexpected success, so set it back */
				ret = ioctl(fd, KDSETLED, &argp);
			}
		}
#endif
		(void)ret;
	}
#endif

#if defined(HAVE_LINUX_KD_H) &&	\
    defined(KDGKBLED)
	{
		char argp;
		int ret;

		ret = ioctl(fd, KDGKBLED, &argp);
#if defined(KDSKBLED)
		if (ret == 0) {
			unsigned long int bad_val = ~0UL, val;

			val = (unsigned long int)argp;
			VOID_RET(int, ioctl(fd, KDSKBLED, val));

			/* Exercise Invalid KDSKBLED ioctl call with invalid flags */
			ret = ioctl(fd, KDSKBLED, bad_val);
			if (ret == 0) {
				/* Unexpected success, so set it back */
				ret = ioctl(fd, KDSKBLED, val);
			}
		}
#endif
		(void)ret;
	}
#endif

#if defined(HAVE_LINUX_KD_H) &&	\
    defined(KDGETMODE)
	{
		int ret;
		unsigned long int argp = 0;

		ret = ioctl(fd, KDGETMODE, &argp);
#if defined(KDSETMODE)
		if (ret == 0) {
			unsigned long int bad_val = ~0UL;

			VOID_RET(int, ioctl(fd, KDSETMODE, argp));

			/* Exercise Invalid KDSETMODE ioctl call with invalid flags */
			ret = ioctl(fd, KDSETMODE, bad_val);
			if (ret == 0) {
				/* Unexpected success, so set it back */
				ret = ioctl(fd, KDSETMODE, argp);
			}
		}
#endif
		(void)ret;
	}
#endif

#if defined(HAVE_LINUX_KD_H) && \
    defined(KDGKBTYPE)
	{
		int val = 0;

		VOID_RET(int, ioctl(fd, KDGKBTYPE, &val));
	}
#endif

#if defined(HAVE_LINUX_KD_H) && \
    defined(GIO_CMAP)
	{
		unsigned char colormap[3*16];
		int ret;

		ret = ioctl(fd, GIO_CMAP, colormap);
#if defined(PIO_CMAP)
		if (ret == 0)
			ret = ioctl(fd, PIO_CMAP, colormap);
#endif
		(void)ret;
	}
#endif

#if defined(HAVE_LINUX_KD_H) && \
    defined(GIO_FONTX) &&	\
    defined(HAVE_CONSOLEFONTDESC)
	{
		struct consolefontdesc font;
		int ret;

		(void)shim_memset(&font, 0, sizeof(font));
		ret = ioctl(fd, GIO_FONTX, &font);
#if defined(PIO_FONTX)
		if (ret == 0)
			ret = ioctl(fd, PIO_FONTX, &font);
#endif
		(void)ret;
	}
#endif

#if defined(HAVE_LINUX_KD_H) &&	\
    defined(KDGETKEYCODE) &&	\
    defined(HAVE_KBKEYCODE)
	{
		int ret;
		struct kbkeycode argp;

		(void)shim_memset(&argp, 0, sizeof(argp));
		ret = ioctl(fd, KDGETKEYCODE, &argp);
#if defined(KDSETKEYCODE)
		if (ret == 0) {
			struct kbkeycode bad_arg;

			VOID_RET(int, ioctl(fd, KDSETKEYCODE, &argp));

			/*
			 * Exercise Invalid KDSETKEYCODE ioctl call
			 * with invalid keycode having different values
			 * of scancode and keycode for scancode < 89
			 */
			(void)shim_memset(&bad_arg, 0, sizeof(bad_arg));
			bad_arg.scancode = (unsigned int)-1;
			bad_arg.keycode = 2;

			ret = ioctl(fd, KDSETKEYCODE, &bad_arg);
			if (ret == 0) {
				/* Unexpected success, so set it back */
				ret = ioctl(fd, KDSETKEYCODE, &argp);
			}
		}
#endif
		(void)ret;
	}
#endif

#if defined(HAVE_LINUX_KD_H) && \
    defined(GIO_FONT)
	{
		unsigned char argp[8192];
		int ret;

		ret = ioctl(fd, GIO_FONT, argp);
#if defined(PIO_FONT)
		if (ret == 0)
			ret = ioctl(fd, PIO_FONT, argp);
#endif
		(void)ret;
	}
#endif

#if defined(HAVE_LINUX_KD_H) && \
    defined(GIO_SCRNMAP) && \
    defined(E_TABSZ)
	{
		unsigned char argp[E_TABSZ];
		int ret;

		ret = ioctl(fd, GIO_SCRNMAP, argp);
#if defined(PIO_SCRNMAP)
		if (ret == 0)
			ret = ioctl(fd, PIO_SCRNMAP, argp);
#endif
		(void)ret;
	}
#endif

#if defined(HAVE_LINUX_KD_H) && \
    defined(GIO_UNISCRNMAP) && \
    defined(E_TABSZ)
	{
		unsigned short int argp[E_TABSZ];
		int ret;

		ret = ioctl(fd, GIO_UNISCRNMAP, argp);
#if defined(PIO_UNISCRNMAP)
		if (ret == 0)
			ret = ioctl(fd, PIO_UNISCRNMAP, argp);
#endif
		(void)ret;
	}
#endif

#if defined(HAVE_LINUX_KD_H) &&	\
    defined(KDGKBMODE)
	{
		int ret;
		unsigned long int argp = 0;

		ret = ioctl(fd, KDGKBMODE, &argp);
#if defined(KDSKBMODE)
		if (ret == 0) {
			unsigned long int bad_val = ~0UL;

			VOID_RET(int, ioctl(fd, KDSKBMODE, argp));

			/* Exercise Invalid KDSKBMODE ioctl call with invalid key mode */
			ret = ioctl(fd, KDSKBMODE, bad_val);
			if (ret == 0) {
				/* Unexpected success, so set it back */
				ret = ioctl(fd, KDSKBMODE, argp);
			}
		}
#endif
		(void)ret;
	}
#endif

#if defined(HAVE_LINUX_KD_H) &&	\
    defined(KDGKBMETA)
	{
		int ret;
		unsigned long int argp = 0;

		ret = ioctl(fd, KDGKBMETA, &argp);
#if defined(KDSKBMETA)
		if (ret == 0) {
			unsigned long int bad_val = ~0UL;

			VOID_RET(int, ioctl(fd, KDSKBMETA, argp));

			/* Exercise Invalid KDSKBMETA ioctl call with invalid key mode */
			ret = ioctl(fd, KDSKBMETA, bad_val);
			if (ret == 0) {
				/* Unexpected success, so set it back */
				ret = ioctl(fd, KDSKBMETA, argp);
			}
		}
#endif
		(void)ret;
	}
#endif

#if defined(HAVE_LINUX_KD_H) && \
    defined(GIO_UNIMAP) &&	\
    defined(HAVE_UNIMAPDESC)
	{
		int ret;
		struct unimapdesc argp;

		(void)shim_memset(&argp, 0, sizeof(argp));
		ret = ioctl(fd, GIO_UNIMAP, &argp);
#if defined(PIO_UNIMAP)
		if (ret == 0) {
			ret = ioctl(fd, PIO_UNIMAP, &argp);
			(void)ret;
		}
#endif
		(void)ret;
	}
#endif

#if defined(HAVE_LINUX_KD_H) && \
    defined(KDGKBDIACR) &&	\
    defined(HAVE_KBDIACRS)
	{
		struct kbdiacrs argp;

		(void)shim_memset(&argp, 0, sizeof(argp));
		VOID_RET(int, ioctl(fd, KDGKBDIACR, &argp));
	}
#endif

#if defined(HAVE_LINUX_KD_H) && \
    defined(VT_RESIZE) &&	\
    defined(HAVE_VT_SIZES) && \
    defined(CAP_SYS_TTY_CONFIG)
	{
		struct vt_sizes argp;
		bool perm = stress_check_capability(CAP_SYS_TTY_CONFIG);

		/* Exercise only if permission is not present */
		if (!perm) {
			(void)shim_memset(&argp, 0, sizeof(argp));
			VOID_RET(int, ioctl(fd, VT_RESIZE, &argp));
		}
	}
#endif

#if defined(HAVE_LINUX_KD_H) && \
    defined(VT_RESIZEX) &&	\
    defined(HAVE_VT_CONSIZE) && \
    defined(CAP_SYS_TTY_CONFIG)
	{
		struct vt_consize argp;
		bool perm = stress_check_capability(CAP_SYS_TTY_CONFIG);

		/* Exercise only if permission is not present */
		if (!perm) {
			(void)shim_memset(&argp, 0, sizeof(argp));
			VOID_RET(int, ioctl(fd, VT_RESIZEX, &argp));
		}
	}
#endif

#if defined(HAVE_LINUX_KD_H) && \
    defined(KDGKBSENT) &&	\
    defined(HAVE_KBSENTRY)
	{
		int ret;
		struct kbsentry argp;

		(void)shim_memset(&argp, 0, sizeof(argp));
		ret = ioctl(fd, KDGKBSENT, &argp);
#if defined(KDSKBSENT)
		if (ret == 0) {
			ret = ioctl(fd, KDSKBSENT, &argp);
			(void)ret;
		}
#endif
		(void)ret;
	}
#endif

#if defined(HAVE_LINUX_VT_H) &&	\
    defined(VT_GETMODE) &&	\
    defined(HAVE_VT_MODE)
	{
		int ret;
		struct vt_mode mode;

		(void)shim_memset(&mode, 0, sizeof(mode));
		ret = ioctl(fd, VT_GETMODE, &mode);
#if defined(VT_SETMODE)
		if (ret == 0) {
			ret = ioctl(fd, VT_SETMODE, &mode);
			(void)ret;
		}
#endif
		(void)ret;
	}
#endif

#if defined(HAVE_LINUX_KD_H) &&	\
    defined(KDGKBENT) && 	\
    defined(HAVE_KBENTRY)
	{
		int ret;
		struct kbentry entry;

		(void)shim_memset(&entry, 0, sizeof(entry));
		ret = ioctl(fd, KDGKBENT, &entry);
#if defined(KDSKBENT)
		if (ret == 0) {
			ret = ioctl(fd, KDSKBENT, &entry);
			(void)ret;

		}
#endif
		(void)ret;
	}
#endif
}
#endif

#if defined(__linux__)
#define ACPI_THERMAL_GET_TRT_LEN	_IOR('s', 1, unsigned long int)
#define ACPI_THERMAL_GET_ART_LEN	_IOR('s', 2, unsigned long int)
#define ACPI_THERMAL_GET_TRT_COUNT	_IOR('s', 3, unsigned long int)
#define ACPI_THERMAL_GET_ART_COUNT	_IOR('s', 4, unsigned long int)

#define ACPI_THERMAL_GET_TRT		_IOR('s', 5, unsigned long int)
#define ACPI_THERMAL_GET_ART		_IOR('s', 6, unsigned long int)

#define ACPI_THERMAL_GET_PSVT_LEN	_IOR('s', 7, unsigned long int)
#define ACPI_THERMAL_GET_PSVT_COUNT	_IOR('s', 8, unsigned long int)
#define ACPI_THERMAL_GET_PSVT		_IOR('s', 9, unsigned long int)

static void stress_dev_acpi_thermal_rel_get(
	const int fd,
	unsigned long int cmd,
	unsigned long int length)
{
	char *buf;

	if ((length < 1) || (length > 64 * KB))
		return;
	buf = (char *)malloc((size_t)length);
	if (!buf)
		return;
	VOID_RET(int, ioctl(fd, cmd, buf));
	free(buf);
}

static void stress_dev_acpi_thermal_rel_linux(
	stress_args_t *args,
	const int fd,
	const char *devpath)
{
	int count;
	unsigned long int length;

	VOID_ARGS(args, fd, devpath);

	VOID_RET(int, ioctl(fd, ACPI_THERMAL_GET_TRT_COUNT, &count));
	if (ioctl(fd, ACPI_THERMAL_GET_TRT_LEN, &length) == 0)
		stress_dev_acpi_thermal_rel_get(fd, ACPI_THERMAL_GET_TRT, length);

	VOID_RET(int, ioctl(fd, ACPI_THERMAL_GET_ART_COUNT, &count));
	if (ioctl(fd, ACPI_THERMAL_GET_ART_LEN, &length) == 0)
		stress_dev_acpi_thermal_rel_get(fd, ACPI_THERMAL_GET_ART, length);

	VOID_RET(int, ioctl(fd, ACPI_THERMAL_GET_PSVT_COUNT, &count));
	if (ioctl(fd, ACPI_THERMAL_GET_PSVT_LEN, &length) == 0)
		stress_dev_acpi_thermal_rel_get(fd, ACPI_THERMAL_GET_PSVT, length);
}
#endif

#if defined(__linux__) &&	\
    (defined(HAVE_LINUX_HIDRAW_H) || \
     defined(HAVE_LINUX_HIDDEV_H))
static void stress_dev_hid_linux(
	stress_args_t *args,
	const int fd,
	const char *devpath)
{
	int size = -1;

	VOID_ARGS(args, fd, devpath);

#if defined(HIDIOCGVERSION)
	{
		int version;

		IOCTL_TIMEOUT(0.05, { VOID_RET(int, ioctl(fd, HIDIOCGVERSION, &version)); }, return);
	}
#endif
#if defined(HIDIOCGFLAG)
	{
		int flag;

		IOCTL_TIMEOUT(0.05, { VOID_RET(int, ioctl(fd, HIDIOCGFLAG, &flag)); }, return);
	}
#endif
#if defined(HIDIOCGDEVINFO)
	{
		struct hiddev_devinfo devinfo;

		IOCTL_TIMEOUT(0.05, { VOID_RET(int, ioctl(fd, HIDIOCGDEVINFO, &devinfo)); }, return);
	}
#endif
#if defined(HIDIOCGRDESCSIZE)
	{
		if (ioctl(fd, HIDIOCGRDESCSIZE, &size) < 0)
			size = -1;
	}
#endif
#if defined(HIDIOCGRDESC)
	if (size > 0) {
		struct hidraw_report_descriptor rpt_desc;

		(void)shim_memset(&rpt_desc, 0, sizeof(rpt_desc));
		rpt_desc.size = size;

		VOID_RET(int, ioctl(fd, HIDIOCGRDESC, &rpt_desc));
	}
#endif
#if defined(HIDIOCGRAWINFO)
	{
		struct hidraw_devinfo info;

		VOID_RET(int, ioctl(fd, HIDIOCGRAWINFO, &info));
	}
#endif
#if defined(HIDIOCGRAWNAME)
	{
		char buf[256];

		VOID_RET(int, ioctl(fd, HIDIOCGRAWNAME(sizeof(buf)), buf));
	}
#endif
#if defined(HIDIOCGRAWPHYS)
	{
		char buf[256];

		VOID_RET(int, ioctl(fd, HIDIOCGRAWPHYS(sizeof(buf)), buf));
	}
#endif
#if defined(HIDIOCGFEATURE)
	{
		char buf[256];

		(void)shim_memset(buf, 0, sizeof(buf));
		buf[0] = 0x9;	/* Report Number */
		VOID_RET(int, ioctl(fd, HIDIOCGFEATURE(sizeof(buf)), buf));
	}
#endif
#if defined(HIDIOCGINPUT)
	/* No-op for now */
#endif
#if defined(HIDIOCGOUTPUT)
	/* No-op for now */
#endif
}
#endif

#if defined(__linux__)
static void stress_dev_kmsg_linux(
	stress_args_t *args,
	const int fd,
	const char *devpath)
{
	VOID_ARGS(args, fd, devpath);

	stress_dev_mem_mmap_linux(fd, devpath, true, false);
}
#endif

#if defined(__linux__)
static void stress_dev_nvram_linux(
	stress_args_t *args,
	const int fd,
	const char *devpath)
{
	char buffer[114];

	VOID_ARGS(args, fd, devpath);
	stress_dev_mem_mmap_linux(fd, devpath, true, false);

	VOID_RET(off_t, lseek(fd, (off_t)0, SEEK_SET));
	VOID_RET(ssize_t, read(fd, buffer, sizeof(buffer)));
}
#endif

#if defined(__linux__) &&	\
    defined(HAVE_LINUX_HPET_H)
static void stress_dev_hpet_linux(
	stress_args_t *args,
	const int fd,
	const char *devpath)
{
	VOID_ARGS(args, fd, devpath);

	/*
	 *  Avoid https://bugs.xenserver.org/browse/XSO-809
	 */
	if (linux_xen_guest())
		return;

#if defined(HPET_INFO)
	{
		struct hpet_info info;

		VOID_RET(int, ioctl(fd, HPET_INFO, &info));
	}
#endif
#if defined(HPET_IRQFREQ)
	{
		unsigned long int freq;

		VOID_RET(int, ioctl(fd, HPET_IRQFREQ, &freq));
	}
#endif
#if defined(CDROMMULTISESSION)
	{
		struct cdrom_multisession ms_info;

		/*
		 *  Invalid CDROMMULTISESSION ioctl syscall with
		 *  invalid format number resulting in EINVAL
		 */
		(void)shim_memset(&ms_info, 0, sizeof(ms_info));
		ms_info.addr_format = UINT8_MAX;
		VOID_RET(int, ioctl(fd, CDROMMULTISESSION, &ms_info));

		/* Valid CDROMMULTISESSION with address formats */
		(void)shim_memset(&ms_info, 0, sizeof(ms_info));
		ms_info.addr_format = CDROM_MSF;
		VOID_RET(int, ioctl(fd, CDROMMULTISESSION, &ms_info));

		(void)shim_memset(&ms_info, 0, sizeof(ms_info));
		ms_info.addr_format = CDROM_LBA;
		VOID_RET(int, ioctl(fd, CDROMMULTISESSION, &ms_info));
	}
#endif
}
#endif

#if defined(__linux__) &&	\
    defined(STRESS_ARCH_X86)
static void stress_dev_port_linux(
	stress_args_t *args,
	const int fd,
	const char *devpath)
{
	off_t off;
	uint8_t *ptr;
	const size_t page_size = stress_get_page_size();

	VOID_ARGS(args, fd, devpath);

	/* seek and read port 0x80 */
	off = lseek(fd, (off_t)0x80, SEEK_SET);
	if (off == 0) {
		char data[1];

		VOID_RET(ssize_t, read(fd, data, sizeof(data)));
	}

	/* Should fail */
	ptr = mmap(NULL, page_size, PROT_READ, MAP_PRIVATE, fd, 0);
	if (ptr != MAP_FAILED)
		(void)munmap(ptr, page_size);
}
#endif

#if defined(HAVE_LINUX_LIRC_H)
static void stress_dev_lirc_linux(
	stress_args_t *args,
	const int fd,
	const char *devpath)
{
	VOID_ARGS(args, fd, devpath);

#if defined(LIRC_GET_FEATURES)
	{
		uint32_t features;

		VOID_RET(int, ioctl(fd, LIRC_GET_FEATURES, &features));
	}
#endif
#if defined(LIRC_GET_REC_MODE)
	{
		uint32_t mode;

		VOID_RET(int, ioctl(fd, LIRC_GET_REC_MODE, &mode));
	}
#endif
#if defined(LIRC_GET_SEND_MODE)
	{
		uint32_t mode;

		VOID_RET(int, ioctl(fd, LIRC_GET_SEND_MODE, &mode));
	}
#endif
#if defined(LIRC_GET_REC_RESOLUTION)
	{
		uint32_t res;

		VOID_RET(int, ioctl(fd, LIRC_GET_REC_RESOLUTION, &res));
	}
#endif
#if defined(LIRC_GET_MIN_TIMEOUT)
	{
		uint32_t timeout;

		VOID_RET(int, ioctl(fd, LIRC_GET_MIN_TIMEOUT, &timeout));
	}
#endif
#if defined(LIRC_GET_MAX_TIMEOUT)
	{
		uint32_t timeout;

		VOID_RET(int, ioctl(fd, LIRC_GET_MAX_TIMEOUT, &timeout));
	}
#endif
#if defined(LIRC_GET_REC_TIMEOUT)
	{
		uint32_t timeout;

		VOID_RET(int, ioctl(fd, LIRC_GET_REC_TIMEOUT, &timeout));
	}
#endif
#if defined(LIRC_GET_LENGTH)
	{
		uint32_t len;

		VOID_RET(int, ioctl(fd, LIRC_GET_LENGTH, &len));
	}
#endif
}
#endif

#if defined(HAVE_LINUX_HDREG_H)
static void stress_dev_hd_linux_ioctl_long(int fd, unsigned long int cmd)
{
	long int val;

	VOID_RET(int, ioctl(fd, cmd, &val));
}

/*
 *  stress_dev_hd_linux()
 *	Linux HDIO ioctls
 */
static void stress_dev_hd_linux(
	stress_args_t *args,
	const int fd,
	const char *devpath)
{
	VOID_ARGS(args, fd, devpath);

#if defined(HDIO_GETGEO)
	{
		struct hd_geometry geom;

		VOID_RET(int, ioctl(fd, HDIO_GETGEO, &geom));
	}
#endif

#if defined(HDIO_GET_UNMASKINTR)
	stress_dev_hd_linux_ioctl_long(fd, HDIO_GET_UNMASKINTR);
#endif

#if defined(HDIO_GET_MULTCOUNT)
	{
		int val;

		VOID_RET(int, ioctl(fd, HDIO_GET_MULTCOUNT, &val));
	}
#endif

#if defined(HDIO_GET_IDENTITY)
	{
		unsigned char identity[512];

		VOID_RET(int, ioctl(fd, HDIO_GET_IDENTITY, identity));
	}
#endif

#if defined(HDIO_GET_KEEPSETTINGS)
	stress_dev_hd_linux_ioctl_long(fd, HDIO_GET_KEEPSETTINGS);
#endif

#if defined(HDIO_GET_32BIT)
	stress_dev_hd_linux_ioctl_long(fd, HDIO_GET_32BIT);
#endif

#if defined(HDIO_GET_NOWERR)
	stress_dev_hd_linux_ioctl_long(fd, HDIO_GET_NOWERR);
#endif

#if defined(HDIO_GET_DMA)
	stress_dev_hd_linux_ioctl_long(fd, HDIO_GET_DMA);
#endif

#if defined(HDIO_GET_NICE)
	stress_dev_hd_linux_ioctl_long(fd, HDIO_GET_NICE);
#endif

#if defined(HDIO_GET_WCACHE)
	stress_dev_hd_linux_ioctl_long(fd, HDIO_GET_WCACHE);
#endif

#if defined(HDIO_GET_ACOUSTIC)
	stress_dev_hd_linux_ioctl_long(fd, HDIO_GET_ACOUSTIC);
#endif

#if defined(HDIO_GET_ADDRESS)
	stress_dev_hd_linux_ioctl_long(fd, HDIO_GET_ADDRESS);
#endif

#if defined(HDIO_GET_BUSSTATE)
	stress_dev_hd_linux_ioctl_long(fd, HDIO_GET_BUSSTATE);
#endif
}
#endif

static void stress_dev_null_nop(
	stress_args_t *args,
	const int fd,
	const char *devpath)
{
	VOID_ARGS(args, fd, devpath);
}

#if defined(__linux__) &&	\
    defined(HAVE_LINUX_PTP_CLOCK_H)
/*
 *  stress_dev_ptp_linux()
 *	minor exercising of the PTP device
 */
static void stress_dev_ptp_linux(
	stress_args_t *args,
	const int fd,
	const char *devpath)
{
	VOID_ARGS(args, fd, devpath);

#if defined(PTP_CLOCK_GETCAPS) &&	\
    defined(PTP_PIN_GETFUNC)
	int ret;
	struct ptp_clock_caps caps;

	VOID_ARGS(args, fd, devpath);

	errno = 0;
	ret = ioctl(fd, PTP_CLOCK_GETCAPS, &caps);
	if (ret == 0) {
		int i, pins = caps.n_pins;

		for (i = 0; i < pins; i++) {
			struct ptp_pin_desc desc;

			(void)shim_memset(&desc, 0, sizeof(desc));
			desc.index = (unsigned int)i;
			VOID_RET(int, ioctl(fd, PTP_PIN_GETFUNC, &desc));
		}
	}
#endif
}
#endif

#if defined(__linux__) &&	\
    defined(HAVE_LINUX_FD_H)
/*
 *  stress_dev_floppy_linux()
 *	minor exercising of the floppy device
 */
static void stress_dev_floppy_linux(
	stress_args_t *args,
	const int fd,
	const char *devpath)
{
	VOID_ARGS(args, fd, devpath);

#if defined(FDMSGON)
	VOID_RET(int, ioctl(fd, FDMSGON, 0));
#endif

#if defined(FDFLUSH)
	VOID_RET(int, ioctl(fd, FDFLUSH, 0));
#endif

#if defined(FDTWADDLE)
	VOID_RET(int, ioctl(fd, FDTWADDLE, 0));
#endif

#if defined(FDCLRPRM)
	VOID_RET(int, ioctl(fd, FDCLRPRM, 0));
#endif

#if defined(FDWERRORGET) &&		\
    defined(HAVE_FLOPPY_WRITE_ERRORS)
	{
		struct floppy_write_errors errors;

		VOID_RET(int, ioctl(fd, FDWERRORGET, &errors));
	}
#endif

#if defined(FDWERRORCLR)
	VOID_RET(int, ioctl(fd, FDWERRORCLR, 0));
#endif

#if defined(FDGETDRVSTAT) &&	\
    defined(HAVE_FLOPPY_DRIVE_STRUCT)
	{
		struct floppy_drive_struct drive;

		VOID_RET(int, ioctl(fd, FDGETDRVSTAT, &drive));
	}
#endif

#if defined(FDPOLLDRVSTAT) &&	\
    defined(HAVE_FLOPPY_DRIVE_STRUCT)
	{
		struct floppy_drive_struct drive;

		VOID_RET(int, ioctl(fd, FDPOLLDRVSTAT, &drive));
	}
#endif

#if defined(FDGETDRVTYP)
	{
		char buf[64];

		VOID_RET(int, ioctl(fd, FDGETDRVTYP, buf));
	}
#endif

#if defined(FDGETFDCSTAT) &&		\
    defined(HAVE_FLOPPY_FDC_STATE)
	{
		struct floppy_fdc_state state;

		VOID_RET(int, ioctl(fd, FDGETFDCSTAT, &state));

	}
#endif

#if defined(_IO)
	/* Invalid ioctl */
	VOID_RET(int, ioctl(fd, _IO(2, 0xff), 0));
#endif

#if defined(FDMSGOFF)
	VOID_RET(int, ioctl(fd, FDMSGOFF, 0));
#endif
}
#endif

/*
 *  stress_dev_snd_control_linux()
 * 	exercise Linux sound devices
 */
static void stress_dev_snd_control_linux(
	stress_args_t *args,
	const int fd,
	const char *devpath)
{
	VOID_ARGS(args, fd, devpath);

#if defined(SNDRV_CTL_IOCTL_PVERSION)
	{
		int ver;

		VOID_RET(int, ioctl(fd, SNDRV_CTL_IOCTL_PVERSION, &ver));
	}
#endif

#if defined(SNDRV_CTL_IOCTL_CARD_INFO) &&	\
    defined(HAVE_SND_CTL_CARD_INFO)
	{
		struct snd_ctl_card_info card;

		VOID_RET(int, ioctl(fd, SNDRV_CTL_IOCTL_CARD_INFO, &card));
	}
#endif

#if defined(SNDRV_CTL_IOCTL_TLV_READ) &&	\
    defined(HAVE_SND_CTL_TLV)
	{
STRESS_PRAGMA_PUSH
STRESS_PRAGMA_WARN_OFF
		/* disable warning for -Wgnu-variable-sized-type-not-at-end */
		struct tlv_buf {
			struct snd_ctl_tlv tlv;
			unsigned int data[4];
		} buf;
STRESS_PRAGMA_POP

		/* intentionally will fail with -EINVAL */
		buf.tlv.numid = 0;
		buf.tlv.length = sizeof(buf.data);
		VOID_RET(int, ioctl(fd, SNDRV_CTL_IOCTL_TLV_READ, (struct snd_ctl_tlv *)&buf.tlv));

		/* intentionally will fail with -ENOENT */
		buf.tlv.numid = ~0U;
		buf.tlv.length = sizeof(buf.data);
		VOID_RET(int, ioctl(fd, SNDRV_CTL_IOCTL_TLV_READ, (struct snd_ctl_tlv *)&buf.tlv));
	}
#endif

#if defined(SNDRV_CTL_IOCTL_POWER_STATE)
	{
		int ret, state;

		ret = ioctl(fd, SNDRV_CTL_IOCTL_POWER_STATE, &state);
#if defined(SNDRV_CTL_IOCTL_POWER)
		if (ret == 0)
			ret = ioctl(fd, SNDRV_CTL_IOCTL_POWER, &state);
#endif
		(void)ret;
	}
#endif

#if defined(SNDRV_CTL_IOCTL_ELEM_LIST)
	{
		struct snd_ctl_elem_list list;


		(void)shim_memset(&list, 0, sizeof(list));

		if (ioctl(fd, SNDRV_CTL_IOCTL_ELEM_LIST, &list) == 0) {
			struct snd_ctl_elem_id *eids;

			eids = (struct snd_ctl_elem_id *)calloc(list.count, sizeof(struct snd_ctl_elem_id));
			if (eids) {
				list.space = list.count;
				list.pids = eids;

				if (ioctl(fd, SNDRV_CTL_IOCTL_ELEM_LIST, &list) == 0) {
#if defined(SNDRV_CTL_IOCTL_ELEM_INFO)
					unsigned int i;

					for (i = 0; i < list.count; i++) {
						struct snd_ctl_elem_info info;

						info.id.numid = eids[i].numid;
						VOID_RET(int, ioctl(fd, SNDRV_CTL_IOCTL_ELEM_INFO, &info));
					}
#endif
				}
				free(eids);
			}
		}
	}
#endif
}

#if defined(__linux__)
/*
 *   stress_dev_hwrng_linux()
 *   	Exercise Linux Hardware Random Number Generator
 */
static void stress_dev_hwrng_linux(
	stress_args_t *args,
	const int fd,
	const char *devpath)
{
	char buffer[8];

	VOID_ARGS(args, fd, devpath);

	VOID_RET(off_t, lseek(fd, (off_t)0, SEEK_SET));
	VOID_RET(ssize_t, read(fd, buffer, sizeof(buffer)));
}
#endif

#if defined(__linux__)

#if defined(PP_IOCT) &&	\
    !defined(PPGETTIME32)
#define PPGETTIME32     _IOR(PP_IOCTL, 0x95, int32_t[2])
#endif
#if defined(PP_IOCT) && \
    !defined(PPGETTIME64)
#define PPGETTIME64     _IOR(PP_IOCTL, 0x95, int64_t[2])
#endif

/*
 *   stress_dev_parport_linux()
 *   	Exercise Linux parallel port
 */
static void stress_dev_parport_linux(
	stress_args_t *args,
	const int fd,
	const char *devpath)
{
#if defined(PPCLAIM) && 	\
    defined(PPRELEASE)
	bool claimed = false;
#endif

	VOID_ARGS(args, fd, devpath);

	/*
	 *  We don't do a PPCLAIM or PPRELEASE on all
	 *  the stressor instances since this the claim
	 *  can indefinitely block and this stops the
	 *  progress of the stressor. Just do this for
	 *  instance 0. For other instances we run
	 *  run without claiming and this will cause
	 *  some of the ioctls to fail.
	 */
#if defined(PPCLAIM) &&	\
    defined(PPRELEASE)
	if (stress_instance_zero(args)) {
		int ret;

		ret = shim_pthread_spin_lock(&parport_lock);
		if (ret == 0) {
			ret = ioctl(fd, PPCLAIM);
			if (ret == 0)
				claimed = true;
			else
				VOID_RET(int, shim_pthread_spin_unlock(&parport_lock));
		}
	}
#endif

#if defined(PPGETMODE)
	{
		int ret, mode;

		ret = ioctl(fd, PPGETMODE, &mode);
#if defined(PPSETMODE)
		errno = 0;
		if (ret == 0)
			ret = ioctl(fd, PPSETMODE, &mode);
#endif
		(void)ret;
	}
#endif

#if defined(PPGETPHASE)
	{
		int ret, phase;

		ret = ioctl(fd, PPGETPHASE, &phase);
#if defined(PPSETPHASE)
		errno = 0;
		if (ret == 0)
			ret = ioctl(fd, PPSETPHASE, &phase);
#endif
		(void)ret;
	}
#endif

#if defined(PPGETMODES)
	{
		int modes;

		VOID_RET(int, ioctl(fd, PPGETMODES, &modes));
	}
#endif

#if defined(PPGETFLAGS)
	{
		int ret, uflags;

		ret = ioctl(fd, PPGETFLAGS, &uflags);
#if defined(PPSETFLAGS)
		errno = 0;
		if (ret == 0)
			ret = ioctl(fd, PPSETFLAGS, &uflags);
#endif
		(void)ret;
	}
#endif

#if defined(PPRSTATUS)
	{
		char reg;

		VOID_RET(int, ioctl(fd, PPRSTATUS, &reg));
	}
#endif

#if defined(PPRCONTROL)
	{
		char reg;

		VOID_RET(int, ioctl(fd, PPRCONTROL, &reg));
	}
#endif

#if defined(PPGETTIME32)
	{
		int32_t time32[2];

		VOID_RET(int, ioctl(fd, PPGETTIME32, time32));
	}
#endif

#if defined(PPGETTIME64)
	{
		int64_t time64[2];

		VOID_RET(int, ioctl(fd, PPGETTIME64, time64));
	}
#endif

#if defined(PPYIELD)
	{
		VOID_RET(int, ioctl(fd, PPYIELD));
	}
#endif


#if defined(PPCLAIM) &&	\
    defined(PPRELEASE)
	if (stress_instance_zero(args) && claimed) {
		VOID_RET(int, ioctl(fd, PPRELEASE));
		VOID_RET(int, shim_pthread_spin_unlock(&parport_lock));
	}
#endif
}
#endif

#if defined(__linux__)
/*
 *   stress_dev_bus_usb_linux()
 *   	Exercise Linux usb devices
 */
static void stress_dev_bus_usb_linux(
	stress_args_t *args,
	const int fd,
	const char *devpath)
{
	VOID_ARGS(args, fd, devpath);

#if defined(HAVE_LINUX_USBDEVICE_FS_H) &&	\
    (defined(USBDEVFS_GET_SPEED) ||		\
    (defined(HAVE_USBDEVFS_GETDRIVER) &&	\
     defined(USBDEVFS_GETDRIVER)))
	{
		int fdwr;

		fdwr = open(devpath, O_RDWR);
		if (fdwr < 0)
			return;

#if defined(USBDEVFS_GET_SPEED)
		VOID_RET(int, ioctl(fdwr, USBDEVFS_GET_SPEED, 0));
#endif

#if defined(HAVE_USBDEVFS_GETDRIVER) &&	\
    defined(USBDEVFS_GETDRIVER)
		{
			struct usbdevfs_getdriver dr;

			VOID_RET(int, ioctl(fdwr, USBDEVFS_GETDRIVER, &dr));
		}
#endif
		(void)close(fdwr);
	}
#endif
}
#endif

#if defined(__linux__) &&	\
    defined(_IO)
#if !defined(IOCTL_VMCI_VERSION)
#define IOCTL_VMCI_VERSION	_IO(7, 0x9f)
#endif
#if !defined(IOCTL_VMCI_VERSION2)
#define IOCTL_VMCI_VERSION2	_IO(7, 0xa7)
#endif

/*
 *   stress_dev_vmci_linux()
 *   	Exercise Linux vmci device
 */
static void stress_dev_vmci_linux(
	stress_args_t *args,
	const int fd,
	const char *devpath)
{
	int val;

	VOID_ARGS(args, fd, devpath);

	val = 0;
	VOID_RET(int, ioctl(fd, IOCTL_VMCI_VERSION, &val));
	val = 0;
	VOID_RET(int, ioctl(fd, IOCTL_VMCI_VERSION2, &val));
}
#endif

#if defined(__linux__) &&	\
    defined(HAVE_LINUX_USB_CDC_WDM_H)
static void stress_dev_cdc_wdm_linux(
	stress_args_t *args,
	const int fd,
	const char *devpath)
{
	uint16_t val;

	VOID_ARGS(args, fd, devpath);

	VOID_RET(int, ioctl(fd, IOCTL_WDM_MAX_COMMAND, &val));
}
#endif

#if defined(__linux__) &&	\
    defined(HAVE_LINUX_INPUT_H)
/*
 *   stress_dev_input_linux()
 *   	Exercise Linux input device
 */
static void stress_dev_input_linux(
	stress_args_t *args,
	const int fd,
	const char *devpath)
{
	VOID_ARGS(args, fd, devpath);

#if defined(EVIOCGVERSION)
	{
		int version;

		VOID_RET(int, ioctl(fd, EVIOCGVERSION, &version));
	}
#endif
#if defined(EVIOCGREP)
	{
		unsigned int repeat[2];

		VOID_RET(int, ioctl(fd, EVIOCGREP, repeat));
	}
#endif
#if defined(EVIOCGKEYCODE)
	{
		unsigned int keycode[2];

		keycode[0] = stress_mwc8();
		keycode[1] = stress_mwc8();

		VOID_RET(int, ioctl(fd, EVIOCGKEYCODE, keycode));
	}
#endif
#if defined(EVIOCGEFFECTS)
	{
		int effects;

		VOID_RET(int, ioctl(fd, EVIOCGEFFECTS, &effects));
	}
#endif
}
#endif

#if defined(__linux__) &&	\
    defined(HAVE_LINUX_UINPUT_H)
/*
 *   stress_dev_uinput_linux()
 *   	Exercise Linux uinput device
 */
static void stress_dev_uinput_linux(
	stress_args_t *args,
	const int fd,
	const char *devpath)
{
	VOID_ARGS(args, fd, devpath);

#if defined(UI_GET_VERSION)
	{
		unsigned int version;

		VOID_RET(int, ioctl(fd, UI_GET_VERSION, &version));
	}
#endif
}
#endif

#if defined(__linux__) &&	\
    defined(HAVE_LINUX_KVM_H)
/*
 *   stress_dev_kvm_linux()
 *   	Exercise Linux kvm device
 */
static void stress_dev_kvm_linux(
	stress_args_t *args,
	const int fd,
	const char *devpath)
{
	VOID_ARGS(args, fd, devpath);

#if defined(KVM_GET_API_VERSION)
	VOID_RET(int, ioctl(fd, KVM_GET_API_VERSION, 0));
#endif
#if defined(KVM_GET_VCPU_MMAP_SIZE)
	VOID_RET(int, ioctl(fd, KVM_GET_VCPU_MMAP_SIZE, 0));
#endif
#if defined(KVM_GET_NR_MMU_PAGES)
	/* deprecated, but worth exercising */
	VOID_RET(int, ioctl(fd, KVM_GET_NR_MMU_PAGES, 0));
#endif
#if defined(KVM_GET_TSC_KHZ)
	VOID_RET(int, ioctl(fd, KVM_GET_TSC_KHZ, 0));
#endif
#if defined(KVM_GET_STATS_FD)
	VOID_RET(int, ioctl(fd, KVM_GET_STATS_FD, 0));
#endif
}
#endif

#if defined(__linux__) &&	\
    defined(HAVE_LINUX_FB_H)
/*
 *   stress_dev_fb_linux()
 *   	Exercise Linux frame buffer device
 */
static void stress_dev_fb_linux(
	stress_args_t *args,
	const int fd,
	const char *devpath)
{
	VOID_ARGS(args, fd, devpath);

#if defined(FBIOGET_FSCREENINFO)
	{
		struct fb_fix_screeninfo screeninfo;

		VOID_RET(int, ioctl(fd, FBIOGET_FSCREENINFO, &screeninfo));
	}
#endif
#if defined(FBIOGET_VSCREENINFO)
	{
		struct fb_var_screeninfo screeninfo;
		int ret;

		ret = ioctl(fd, FBIOGET_VSCREENINFO, &screeninfo);
#if defined(FBIOPUT_VSCREENINFO)
		if (ret == 0)
			ret = ioctl(fd, FBIOPUT_VSCREENINFO, &screeninfo);
#endif
		(void)ret;
	}
#endif
}
#endif

#if defined(__linux__)
/* exercise arch/x86/kernel/cpuid.c driver */
static void stress_dev_cpu_cpuid(
	stress_args_t *args,
	const int fd,
	const char *devpath)
{
	uint8_t data[64];

	(void)args;
	(void)devpath;

	if (lseek(fd, 0, SEEK_SET) != (off_t)0)
		return;

	/* multiple of 16 bytes read */
	VOID_RET(ssize_t, read(fd, data, sizeof(data)));
	/* invalid non-multiple of 16 bytes read */
	VOID_RET(ssize_t, read(fd, data, sizeof(data) - 1));
}
#endif

#if defined(__linux__) &&	\
    defined(STRESS_ARCH_X86)
/* exercise arch/x86/kernel/msr.c driver */

#define X86_IOC_RDMSR_REGS	_IOWR('c', 0xA0, uint32_t[8])

static void stress_dev_cpu_msr(
	stress_args_t *args,
	const int fd,
	const char *devpath)
{
	uint64_t tsc;
	uint32_t regs[8];

	(void)args;
	(void)devpath;

	/* RDMSR_REGS ioctl(), see arch/x86/lib/msr-reg.S */
	regs[0] = 0x00000000;	/* EAX */
	regs[1] = 0x00000010;	/* ECX = TSC MSR */
	regs[2] = 0x00000000;	/* EDX */
	regs[3] = 0x00000000;	/* EBX */
	regs[4] = 0x00000000;	/* unused */
	regs[5] = 0x00000000;	/* R12D */
	regs[6] = 0x00000000;	/* ESI */
	regs[7] = 0x00000000;	/* EDI */
	VOID_RET(int, ioctl(fd, X86_IOC_RDMSR_REGS, regs));

	/* seek to TSC MSR */
	if (lseek(fd, (off_t)0x00000010, SEEK_SET) < 0)
		return;
	/* read 64 bit TSC */
	VOID_RET(ssize_t, read(fd, &tsc, sizeof(tsc)));
}
#endif

#if defined(__linux__) &&	\
    defined(HAVE_LINUX_AUTO_DEV_IOCTL_H) &&	\
    defined(AUTOFS_TYPE_ANY) &&			\
    defined(AUTOFS_DEV_IOCTL_ISMOUNTPOINT)
static void stress_dev_autofs_linux(
	stress_args_t *args,
	const int fd,
	const char *devpath)
{
	const char *tmp = ".....";
	const size_t tmp_len = strlen(tmp) + 1;
	size_t size;
	struct autofs_dev_ioctl *info;
	static bool try_invalid_path = false;

	(void)args;
	(void)devpath;

	size = sizeof(*info) + tmp_len;
	info = (struct autofs_dev_ioctl *)calloc(1, size);
	if (!info)
		return;

	init_autofs_dev_ioctl(info);
	errno = 0;
	if (ioctl(fd, AUTOFS_DEV_IOCTL_VERSION, info) < 0) {
		free(info);
		return;
	}

	/* and exercise an invalid pathname, will spam dmesg, so do once */
	if (!try_invalid_path) {
		try_invalid_path = true;

		init_autofs_dev_ioctl(info);
		info->ioctlfd = -1;
		(void)shim_strscpy(info->path, tmp, tmp_len);
		info->ismountpoint.in.type = AUTOFS_TYPE_ANY;
		info->size = size;
		VOID_RET(int, ioctl(fd, AUTOFS_DEV_IOCTL_ISMOUNTPOINT, info));
	}
	free(info);
}
#endif

#define DEV_FUNC(dev, func) \
	{ dev, sizeof(dev) - 1, func }

static const stress_dev_func_t dev_funcs[] = {
	DEV_FUNC("/dev/null",	stress_dev_null_nop),
#if defined(__linux__) &&		\
    defined(HAVE_LINUX_MEDIA_H) &&	\
    defined(MEDIA_IOC_DEVICE_INFO)
	DEV_FUNC("/dev/media",	stress_dev_media_linux),
#endif
#if defined(__linux__) &&	\
    defined(HAVE_LINUX_VT_H)
	DEV_FUNC("/dev/vcs",	stress_dev_vcs_linux),
#endif
#if defined(__linux__) &&	\
    defined(HAVE_LINUX_DM_IOCTL_H)
	DEV_FUNC("/dev/mapper/control",	stress_dev_dm_linux),
#endif
#if defined(__linux__) &&	\
    defined(HAVE_LINUX_VIDEODEV2_H)
	DEV_FUNC("/dev/video",	stress_dev_video_linux),
	DEV_FUNC("/dev/radio",	stress_dev_video_linux),
#endif
#if defined(__linux__) &&	\
    defined(HAVE_LINUX_RANDOM_H)
	DEV_FUNC("/dev/random",	stress_dev_random_linux),
#endif
#if defined(__linux__) &&	\
    defined(HAVE_LINUX_RTC_H)
	DEV_FUNC("/dev/rtc",	stress_dev_rtc_linux),
#endif
#if defined(__linux__)
	DEV_FUNC("/dev/mem",	stress_dev_mem_linux),
	DEV_FUNC("/dev/kmem",	stress_dev_kmem_linux),
	DEV_FUNC("/dev/kmsg",	stress_dev_kmsg_linux),
	DEV_FUNC("/dev/nvram",	stress_dev_nvram_linux),
	DEV_FUNC("/dev/cdrom",  stress_dev_cdrom_linux),
	DEV_FUNC("/dev/sr0",    stress_dev_cdrom_linux),
	DEV_FUNC("/dev/console",stress_dev_console_linux),
	DEV_FUNC("/dev/acpi_thermal_rel", stress_dev_acpi_thermal_rel_linux),
#endif
#if defined(__linux__) &&	\
    (defined(HAVE_LINUX_HIDRAW_H) || \
     defined(HAVE_LINUX_HIDDEV_H))
	DEV_FUNC("/dev/hid",	stress_dev_hid_linux),
	DEV_FUNC("/dev/usb/hiddev", stress_dev_hid_linux),
#endif
#if defined(__linux__) &&       \
    defined(HAVE_SCSI_SG_H)
	DEV_FUNC("/dev/sg",	stress_dev_scsi_generic_linux),
#endif
#if defined(__linux__) &&	\
    defined(STRESS_ARCH_X86)
	DEV_FUNC("/dev/port",	stress_dev_port_linux),
#endif
#if defined(__linux__) &&	\
    defined(HAVE_LINUX_HPET_H)
	DEV_FUNC("/dev/hpet",	stress_dev_hpet_linux),
#endif
#if defined(__linux__) &&	\
    defined(HAVE_LINUX_PTP_CLOCK_H)
	DEV_FUNC("/dev/ptp",	stress_dev_ptp_linux),
#endif
	DEV_FUNC("/dev/snd/control",	stress_dev_snd_control_linux),
#if defined(__linux__) &&	\
    defined(HAVE_LINUX_FD_H)
	DEV_FUNC("/dev/fd0",	stress_dev_floppy_linux),
#endif
#if defined(__linux__)
	DEV_FUNC("/dev/hwrng",	stress_dev_hwrng_linux),
#endif
#if defined(__linux__) &&	\
    defined(HAVE_LINUX_INPUT_H)
	DEV_FUNC("/dev/input/event", stress_dev_input_linux),
#endif
#if defined(__linux__) &&	\
    defined(HAVE_LINUX_KVM_H)
	DEV_FUNC("/dev/kvm", stress_dev_kvm_linux),
#endif
#if defined(__linux__)
	DEV_FUNC("/dev/parport",stress_dev_parport_linux),
#endif
#if defined(__linux__)
	DEV_FUNC("/dev/bus/usb",stress_dev_bus_usb_linux),
#endif
#if defined(HAVE_LINUX_LIRC_H)
	DEV_FUNC("/dev/lirc",	stress_dev_lirc_linux),
#endif
#if defined(__linux__) &&	\
    defined(HAVE_LINUX_UINPUT_H)
	DEV_FUNC("/dev/uinput",	stress_dev_uinput_linux),
#endif
#if defined(__linux__) &&	\
    defined(_IO)
	DEV_FUNC("/dev/vmci",	stress_dev_vmci_linux),
#endif
#if defined(__linux__) &&	\
    defined(HAVE_LINUX_USB_CDC_WDM_H)
	DEV_FUNC("/dev/cdc-wdm",stress_dev_cdc_wdm_linux),
#endif
#if defined(__linux__) &&	\
    defined(HAVE_LINUX_FB_H)
	DEV_FUNC("/dev/fb", 	stress_dev_fb_linux),
#endif
#if defined(__linux__)
	DEV_FUNC("/dev/cpu/0/cpuid", stress_dev_cpu_cpuid),
#endif
#if defined(__linux__) &&	\
    defined(STRESS_ARCH_X86)
	DEV_FUNC("/dev/cpu/0/msr", stress_dev_cpu_msr),
#endif
#if defined(__linux__) &&			\
    defined(HAVE_LINUX_AUTO_DEV_IOCTL_H) &&	\
    defined(AUTOFS_TYPE_ANY) &&			\
    defined(AUTOFS_DEV_IOCTL_ISMOUNTPOINT)
	DEV_FUNC("/dev/autofs", stress_dev_autofs_linux),
#endif
#if defined(__linux__)
	DEV_FUNC("/dev/bsg", stress_dev_scsi_blk),
	DEV_FUNC("/dev/sg", stress_dev_scsi_blk),
#endif
};

static void stress_dev_procname(const char *path)
{
	/*
	 *  Set process name to enable debugging if it gets stuck
	 */
	if (!(g_opt_flags & OPT_FLAGS_KEEP_NAME)) {
		char procname[55];

		(void)snprintf(procname, sizeof(procname), "stress-ng-dev:%-40.40s", path);
#if defined(HAVE_BSD_UNISTD_H) &&       \
    defined(HAVE_SETPROCTITLE)
		/* Sets argv[0] */
		setproctitle("-%s", procname);
#endif
	}
}

static inline int stress_dev_lock(const char *path, const int fd)
{
	errno = 0;
#if defined(TIOCEXCL) &&	\
    defined(TIOCNXCL)
	{
		int ret;

		if (strncmp(path, "/dev/tty", 8))
			return 0;

		ret = ioctl(fd, TIOCEXCL);
		if (ret < 0)
			return ret;
	}

#if defined(LOCK_EX) &&	\
    defined(LOCK_NB) &&	\
    defined(LOCK_UN)
	{
		if (flock(fd, LOCK_EX | LOCK_NB) < 0) {
			VOID_RET(int, ioctl(fd, TIOCNXCL));
			return -1;
		}
	}
#endif
	return 0;
#else
	(void)path;
	(void)fd;

	return 0;
#endif
}

static inline void stress_dev_unlock(const char *path, const int fd)
{
#if defined(TIOCEXCL) &&	\
    defined(TIOCNXCL)
	if (strncmp(path, "/dev/tty", 8))
		return;

#if defined(LOCK_EX) &&	\
    defined(LOCK_NB) &&	\
    defined(LOCK_UN)
	VOID_RET(int, flock(fd, LOCK_UN));
#endif
	VOID_RET(int, ioctl(fd, TIOCNXCL));
#else
	(void)path;
	(void)fd;
#endif
}

static int stress_dev_open_lock(
	stress_args_t *args,
	dev_info_t *dev_info,
	const int mode)
{
	int fd;

	fd = stress_open_timeout(args->name, dev_info->path, mode, 250000000);
	if (fd < 0) {
		if (errno == EBUSY)
			(void)shim_usleep(10000);
		return -1;
	}
	if (stress_dev_lock(dev_info->path, fd) < 0) {
		(void)close(fd);
		return -1;
	}
	return fd;
}

static int stress_dev_close_unlock(const char *path, const int fd)
{
	stress_dev_unlock(path, fd);
	return close(fd);
}

static const int open_flags[] = {
#if defined(O_ASYNC)
	O_ASYNC | O_RDONLY,
	O_ASYNC | O_WRONLY,
	O_ASYNC | O_RDWR,
#endif
#if defined(O_CLOEXEC)
	O_CLOEXEC | O_RDONLY,
	O_CLOEXEC | O_WRONLY,
	O_CLOEXEC | O_RDWR,
#endif
#if defined(O_DIRECT)
	O_DIRECT | O_RDONLY,
	O_DIRECT | O_WRONLY,
	O_DIRECT | O_RDWR,
#endif
#if defined(O_DSYNC)
	O_DSYNC | O_RDONLY,
	O_DSYNC | O_WRONLY,
	O_DSYNC | O_RDWR,
#endif
#if defined(O_NOATIME)
	O_NOATIME | O_RDONLY,
	O_NOATIME | O_WRONLY,
	O_NOATIME | O_RDWR,
#endif
#if defined(O_SYNC)
	O_SYNC | O_RDONLY,
	O_SYNC | O_WRONLY,
	O_SYNC | O_RDWR,
#endif
};

/*
 *  stress_dev_rw()
 *	exercise a dev entry
 */
static inline void stress_dev_rw(
	stress_args_t *args,
	sys_dev_info_t **sys_dev_info,
	int32_t loops)
{
	int fd, ret;
	off_t offset;
	struct stat statbuf;
	struct pollfd fds[1];
	fd_set rfds;
	const double threshold = 0.25;
	const pid_t pid = getpid();

	while ((loops == -1) || (loops > 0)) {
		double t_start;
		bool timeout = false;
		char *ptr;
		size_t i;
#if defined(HAVE_TERMIOS_H) &&	\
    defined(TCGETS) && \
    defined(HAVE_TERMIOS)
		struct termios tios;
#endif
		dev_info_t *dev_info;
		char *path;

		if (*sys_dev_info) {
			char buf[4096];

			VOID_RET(ssize_t, stress_system_read((*sys_dev_info)->sysdevpath, buf, sizeof(buf)));
			*sys_dev_info = (*sys_dev_info)->next;
		}

		ret = shim_pthread_spin_lock(&lock);
		if (ret)
			return;
		dev_info = pthread_dev_info;
		(void)shim_pthread_spin_unlock(&lock);
		if (!dev_info || !stress_continue_flag())
			break;

		/* state info no yet associated */
		if (UNLIKELY(!dev_info->state)) {
			(void)shim_sched_yield();
			continue;
		}

		path = dev_info->path;
		if (dev_info->state->open_failed)
			goto next;

		t_start = stress_time_now();
		fd = stress_dev_open_lock(args, dev_info, O_RDONLY | O_NONBLOCK | O_NDELAY);
		if (fd < 0)
			goto next;

		if (stress_time_now() - t_start > threshold) {
			timeout = true;
			stress_dev_close_unlock(path, fd);
			goto next;
		}

		(void)stress_read_fdinfo(pid, fd);

		if (shim_fstat(fd, &statbuf) < 0) {
			pr_fail("%s: stat failed on %s, errno=%d (%s)\n",
				args->name, path, errno, strerror(errno));
		} else {
			if ((S_ISBLK(statbuf.st_mode) | (S_ISCHR(statbuf.st_mode))) == 0) {
				stress_dev_close_unlock(path, fd);
				goto next;
			}
		}

		if (S_ISBLK(statbuf.st_mode)) {
			stress_dev_blk(args, fd, path);

			if (is_scsi_dev(dev_info))
				stress_dev_scsi_blk(args, fd, path);
#if defined(HAVE_LINUX_HDREG_H)
			stress_dev_hd_linux(args, fd, path);
#endif
		}
#if defined(HAVE_TERMIOS_H) &&	\
    defined(HAVE_TERMIOS) &&	\
    defined(TCGETS)
		if (S_ISCHR(statbuf.st_mode) &&
		    strncmp("/dev/vsock", path, 10) &&
		    strncmp("/dev/dri", path, 8) &&
		    strncmp("/dev/nmem", path, 9) &&
		    strncmp("/dev/ndctl", path, 10) &&
		    (ioctl(fd, TCGETS, &tios) == 0))
			stress_dev_tty(args, fd, path);
#endif

		offset = lseek(fd, 0, SEEK_SET);
		stress_uint64_put((uint64_t)offset);
		offset = lseek(fd, 0, SEEK_CUR);
		stress_uint64_put((uint64_t)offset);
		offset = lseek(fd, 0, SEEK_END);
		stress_uint64_put((uint64_t)offset);

		if (stress_time_now() - t_start > threshold) {
			timeout = true;
			stress_dev_close_unlock(path, fd);
			goto next;
		}

		FD_ZERO(&rfds);
		fds[0].fd = fd;
		fds[0].events = POLLIN;
		VOID_RET(int, poll(fds, 1, 0));

		if (stress_time_now() - t_start > threshold) {
			timeout = true;
			stress_dev_close_unlock(path, fd);
			goto next;
		}

#if !defined(__NetBSD__) &&	\
    defined(HAVE_SELECT)
		{
			struct timeval tv;
			fd_set wfds;

			FD_ZERO(&rfds);
			FD_SET(fd, &rfds);
			FD_ZERO(&wfds);
			FD_SET(fd, &wfds);
			tv.tv_sec = 0;
			tv.tv_usec = 10000;
			VOID_RET(int, select(fd + 1, &rfds, &wfds, NULL, &tv));

			if (stress_time_now() - t_start > threshold) {
				timeout = true;
				stress_dev_close_unlock(path, fd);
				goto next;
			}
		}
#endif

#if defined(F_GETFD)
		VOID_RET(int, fcntl(fd, F_GETFD, NULL));

		if (stress_time_now() - t_start > threshold) {
			timeout = true;
			stress_dev_close_unlock(path, fd);
			goto next;
		}
#endif
#if defined(F_GETFL)
		VOID_RET(int, fcntl(fd, F_GETFL, NULL));

		if (stress_time_now() - t_start > threshold) {
			timeout = true;
			stress_dev_close_unlock(path, fd);
			goto next;
		}
#endif
#if defined(F_GETSIG)
		VOID_RET(int, fcntl(fd, F_GETSIG, NULL));

		if (stress_time_now() - t_start > threshold) {
			timeout = true;
			stress_dev_close_unlock(path, fd);
			goto next;
		}
#endif
		ptr = mmap(NULL, args->page_size, PROT_READ, MAP_PRIVATE, fd, 0);
		if (ptr != MAP_FAILED)
			(void)munmap(ptr, args->page_size);
		ptr = mmap(NULL, args->page_size, PROT_READ, MAP_SHARED, fd, 0);
		if (ptr != MAP_FAILED)
			(void)munmap(ptr, args->page_size);
		(void)stress_dev_close_unlock(path, fd);

		if (stress_time_now() - t_start > threshold) {
			timeout = true;
			goto next;
		}

		fd = stress_dev_open_lock(args, dev_info, O_RDONLY | O_NONBLOCK | O_NDELAY);
		if (fd < 0)
			goto next;

		ptr = mmap(NULL, args->page_size, PROT_WRITE, MAP_PRIVATE, fd, 0);
		if (ptr != MAP_FAILED)
			(void)munmap(ptr, args->page_size);
		ptr = mmap(NULL, args->page_size, PROT_WRITE, MAP_SHARED, fd, 0);
		if (ptr != MAP_FAILED)
			(void)munmap(ptr, args->page_size);

		VOID_RET(int, shim_fsync(fd));

		for (i = 0; i < SIZEOF_ARRAY(dev_funcs); i++) {
			if (!strncmp(path, dev_funcs[i].devpath, dev_funcs[i].devpath_len))
				dev_funcs[i].func(args, fd, path);
		}
		stress_dev_close_unlock(path, fd);
		if (stress_time_now() - t_start > threshold) {
			timeout = true;
			goto next;
		}
		/*
		 *   O_RDONLY | O_WRONLY allows one to
		 *   use the fd for ioctl() only operations
		 */
		fd = stress_dev_open_lock(args, dev_info, O_RDONLY | O_WRONLY);
		if (fd < 0) {
			if (errno == EINTR)
				dev_info->state->open_failed = true;
		} else {
			stress_dev_close_unlock(path, fd);
		}

		/*
		 *  Exercise various open options on device
		 */
		for (i = 0; i < SIZEOF_ARRAY(open_flags); i++) {
			fd = stress_dev_open_lock(args, dev_info, open_flags[i] | O_NONBLOCK);
			if (fd >= 0)
				stress_dev_close_unlock(path, fd);
		}
next:
		if (loops > 0) {
			if (timeout)
				break;
			loops--;
		}
	}
}

/*
 *  stress_dev_thread
 *	keep exercising a /dev entry until
 *	controlling thread triggers an exit
 */
static void *stress_dev_thread(void *arg)
{
	const stress_pthread_args_t *pa = (stress_pthread_args_t *)arg;
	stress_args_t *args = pa->args;
	sys_dev_info_t *sys_dev_info = (sys_dev_info_t *)pa->data;

	/*
	 *  Block all signals, let controlling thread
	 *  handle these
	 */
	(void)sigprocmask(SIG_BLOCK, &set, NULL);

	stress_random_small_sleep();

	while (stress_continue_flag())
		stress_dev_rw(args, &sys_dev_info, -1);

	return &g_nowt;
}

/*
 *  stress_dev_files()
 *	stress all device files
 */
static void stress_dev_files(
	stress_args_t *args,
	dev_info_t *dev_info_list,
	sys_dev_info_t **sys_dev_info)
{
	int32_t loops = args->instance < 8 ? (int32_t)args->instance + 1 : 8;
	static int try_failed = 0;
	dev_info_t *di;

	if (UNLIKELY(!stress_continue_flag()))
		return;

	for (di = dev_info_list; LIKELY(di && stress_continue(args)); di = di->next) {
		int ret;

		/* Should never happen */
		if (UNLIKELY(!di->state))
			continue;

		/* Skip if open failed on an earlier attempt */
		if (di->state->open_failed)
			continue;

		stress_dev_procname(di->path);

		/* If it was opened OK before, no need for try_open check */
		if (!di->state->open_succeeded) {
			/* Limit the number of locked up try failures */
			if (try_failed > STRESS_DEV_OPEN_TRIES_MAX)
				continue;

			ret = stress_try_open(args, di->path, O_RDONLY | O_NONBLOCK | O_NDELAY, 1500000000);
			if (ret == STRESS_TRY_OPEN_FAIL) {
				di->state->open_failed = true;
				try_failed++;
				continue;
			}
			if (ret == STRESS_TRY_AGAIN)
				continue;
		}

		ret = shim_pthread_spin_lock(&lock);
		if (!ret) {
			pthread_dev_info = di;
			(void)shim_pthread_spin_unlock(&lock);
			stress_dev_rw(args, sys_dev_info, loops);
			stress_bogo_inc(args);
		}
		di->state->open_succeeded = true;
	}
}

/*
 *  stress_dev_infos_opened()
 *	return number of devices that were successfully opened
 */
static size_t stress_dev_infos_opened(dev_info_t *list)
{
	dev_info_t *di;
	size_t opened = 0;

	for (di = list; di; di = di->next) {
		if (di->state->open_succeeded)
			opened++;
	}
	return opened;
}

/*
 *  stress_dev_infos_free()
 *	free list of device information
 */
static void stress_dev_infos_free(dev_info_t **list)
{
	dev_info_t *di = *list;

	while (di) {
		dev_info_t *next = di->next;

		free(di->path);
		free(di);
		di = next;
	}
	*list = NULL;
}

/*
 *  stress_dev_avoid()
 *	check if filename should be avoided
 */
static bool stress_dev_avoid(char *filename)
{
	char tmp[PATH_MAX], *name;

	(void)shim_strscpy(tmp, filename, sizeof(tmp));
	name = basename(tmp);

	if (name == NULL)
		return true;
	if (stress_is_dot_filename(name))
		return true;
	/*
	 *  Avoid https://bugs.xenserver.org/browse/XSO-809
	 *  see: LP#1741409, so avoid opening /dev/hpet
	 */
	if (!strcmp(name, "hpet") && linux_xen_guest())
		return true;
	if (!strncmp(name, "ttyS", 4))
		return true;
	/*
	 *  Closing watchdog files will cause
	 *  systems to be rebooted, so avoid these!
	 */
	if (!strncmp(name, "watchdog", 8))
		return true;
	return false;
}

/*
 *  stress_dev_info_add()
 *	add new device path to device info list, silently drop devs
 *	that can't be added
 */
static void stress_dev_info_add(
	stress_args_t *args,
	char *path,
	dev_info_t **list,
	size_t *list_len,
	const bool warn)
{
	dev_info_t *new_dev;
	char linkpath[PATH_MAX];

	if (stress_dev_avoid(path)) {
		if (warn)
			pr_inf("%s: avoiding use of %s\n", args->name, path);
		return;
	}
	if (readlink(path, linkpath, sizeof(linkpath)) > 0) {
		char *name = basename(linkpath);

		if (name && stress_dev_avoid(name)) {
			if (warn)
				pr_inf("%s: avoiding use of %s\n", args->name, path);
			return;
		}
	}

	new_dev = (dev_info_t *)calloc(1, sizeof(*new_dev));
	if (!new_dev)
		return;

	new_dev->path = shim_strdup(path);
	if (!new_dev->path) {
		free(new_dev);
		return;
	}

	new_dev->name = stress_dev_basename(new_dev->path);
	new_dev->rnd_id = stress_mwc32();
	new_dev->next = *list;
	new_dev->state = NULL;

	*list = new_dev;
	(*list_len)++;
}

/*
 *  stress_dev_infos_get()
 *	traverse device directories adding device info to list
 */
static void stress_dev_infos_get(
	stress_args_t *args,
	const char *path,
	const char *tty_name,
	dev_info_t **list,
	size_t *list_len)
{
	struct dirent **dlist;
	int i, n;
	const mode_t flags = S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH;

	if (UNLIKELY(!stress_continue(args)))
		return;

	dlist = NULL;
	n = scandir(path, &dlist, NULL, alphasort);
	if (n <= 0)
		return;

	for (i = 0; LIKELY(stress_continue(args) && (i < n)); i++) {
		int ret;
		struct stat statbuf;
		char tmp[PATH_MAX];
		struct dirent *d = dlist[i];
		size_t len;

		if (UNLIKELY(!stress_continue(args)))
			break;

		if (stress_dev_avoid(d->d_name))
			continue;

		len = strlen(d->d_name);

		/*
		 *  Exercise no more than 3 of the same device
		 *  driver, e.g. ttyS0..ttyS1
		 */
		if (len > 1) {
			int dev_n;
			char *ptr = d->d_name + len - 1;

			while ((ptr > d->d_name) && isdigit((unsigned char)*ptr))
				ptr--;
			ptr++;
			if (sscanf(ptr, "%d", &dev_n) != 1)
				continue;
			if (dev_n > 2)
				continue;
		}

		(void)stress_mk_filename(tmp, sizeof(tmp), path, d->d_name);

		/* Don't exercise our tty */
		if (tty_name && !strcmp(tty_name, tmp))
			continue;

		switch (shim_dirent_type(path, d)) {
		case SHIM_DT_DIR:
			ret = shim_stat(tmp, &statbuf);
			if (ret < 0)
				continue;
			if ((statbuf.st_mode & flags) == 0)
				continue;
			stress_dev_infos_get(args, tmp, tty_name, list, list_len);
			break;
		case SHIM_DT_BLK:
		case SHIM_DT_CHR:
			stress_dev_info_add(args, tmp, list, list_len, false);
			break;
		default:
			break;
		}
	}
	stress_dirent_list_free(dlist, n);
}

/*
 *  stress_dev_state_init()
 *	initialize state flags
 */
static inline void stress_dev_state_init(dev_state_t *dev_state)
{
	dev_state->scsi_checked = false;
	dev_state->scsi_device = false;
	dev_state->open_succeeded = false;
	dev_state->open_failed = false;
}

/*
 *  stress_dev_info_list_state_init
 *	initialize and associate shared memory device state
 *	information with device information.
 */
static void stress_dev_info_list_state_init(dev_info_t *list, dev_state_t *dev_state)
{
	dev_info_t *di;
	dev_state_t *ds;

	for (di = list, ds = dev_state; di; di = di->next, ds++) {
		stress_dev_state_init(ds);
		di->state = ds;
	}
}

/*
 *  dev_info_rnd_id_cmp()
 *	sort order comparison based on randomized id
 */
static int dev_info_rnd_id_cmp(const void *p1, const void *p2)
{
	const dev_info_t * const *d1 = (const dev_info_t * const *)p1;
	const dev_info_t * const *d2 = (const dev_info_t * const *)p2;

	if ((*d1)->rnd_id  < (*d2)->rnd_id)
		return 1;
	else if ((*d1)->rnd_id > (*d2)->rnd_id)
		return -1;
	else
		return 0;
}

/*
 *  stress_dev_infos_mixup()
 *	mix up device list based on randomized id
 */
static void stress_dev_infos_mixup(dev_info_t **dev_info_list, const size_t dev_info_list_len)
{
	dev_info_t **dev_info_sorted, *dev;
	size_t i;

	dev_info_sorted = (dev_info_t **)calloc(dev_info_list_len, sizeof(*dev_info_sorted));
	if (!dev_info_sorted)
		return;

	for (i = 0, dev = *dev_info_list; i < dev_info_list_len; i++) {
		dev_info_sorted[i] = dev;
		dev = dev->next;
	}
	qsort(dev_info_sorted, dev_info_list_len, sizeof(dev_info_t *), dev_info_rnd_id_cmp);

	/* rebuild list based on randomized sorted rnd_id */
	*dev_info_list = NULL;
	for (i = 0; i < dev_info_list_len; i++) {
		dev = dev_info_sorted[i];
		dev->next = *dev_info_list;
		*dev_info_list = dev;
	}
	free(dev_info_sorted);
}

#if defined(__linux__)
/*
 *  stress_sys_dev_get()
 *	traverse /sys/dev device directories adding paths to list
 */
static void stress_sys_dev_infos_get(
	stress_args_t *args,
	const char *path,
	sys_dev_info_t **list,
	sys_dev_info_t **list_end,
	const int depth)
{
	struct dirent **dlist;
	int i, n;
	const mode_t flags = S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH;

	if (UNLIKELY(!stress_continue(args)))
		return;

	dlist = NULL;
	n = scandir(path, &dlist, NULL, alphasort);
	if (n <= 0)
		return;

	for (i = 0; LIKELY(stress_continue(args) && (i < n)); i++) {
		int ret;
		struct stat statbuf;
		char tmp[PATH_MAX];
		struct dirent *d = dlist[i];
		sys_dev_info_t *sys_dev_info;

		if (UNLIKELY(!stress_continue(args)))
			break;
		if (stress_is_dot_filename(d->d_name))
			continue;

		(void)stress_mk_filename(tmp, sizeof(tmp), path, d->d_name);

		switch (shim_dirent_type(path, d)) {
		case SHIM_DT_LNK:
			if (depth > 2)
				continue;
			ret = shim_stat(tmp, &statbuf);
			if (ret < 0)
				continue;
			if ((statbuf.st_mode & flags) == 0)
				continue;
			stress_sys_dev_infos_get(args, tmp, list, list_end, depth + 1);
			break;
		case SHIM_DT_DIR:
			ret = shim_stat(tmp, &statbuf);
			if (ret < 0)
				continue;
			if ((statbuf.st_mode & flags) == 0)
				continue;
			stress_sys_dev_infos_get(args, tmp, list, list_end, depth + 1);
			break;
		case SHIM_DT_REG:
			sys_dev_info = (sys_dev_info_t *)malloc(sizeof(*sys_dev_info));
			if (!sys_dev_info)
				break;
			sys_dev_info->next = *list;
			sys_dev_info->sysdevpath = shim_strdup(tmp);
			if (!sys_dev_info->sysdevpath) {
				free(sys_dev_info);
				break;
			}
			if (*list_end == NULL) {
				*list_end = sys_dev_info;
				*list = sys_dev_info;
			} else {
				(*list_end)->next = sys_dev_info;
				*list_end = sys_dev_info;
			}
			break;
		default:
			break;
		}
	}
	stress_dirent_list_free(dlist, n);
}

/*
 *  stress_sys_dev_infos_free()
 *	free circular sys_dev_info_t list
 */
static void stress_sys_dev_infos_free(sys_dev_info_t **list)
{
	sys_dev_info_t *sys_dev_info;

	if (*list == NULL)
		return;

	sys_dev_info = *list;

	while (sys_dev_info) {
		sys_dev_info_t *next = sys_dev_info->next;

		free(sys_dev_info->sysdevpath);
		free(sys_dev_info);
		sys_dev_info = next;

		if (sys_dev_info == *list)
			break;
	}
	*list = NULL;
}

#else

static inline void stress_sys_dev_infos_get(
	stress_args_t *args,
	const char *path,
	sys_dev_info_t **list,
	sys_dev_info_t **list_end,
	const int depth)
{
	(void)args;
	(void)path;
	(void)list;
	(void)list_end;
	(void)depth;
}

static inline void stress_sys_dev_infos_free(sys_dev_info_t **list)
{
	(void)list;
}
#endif

/*
 *  stress_dev
 *	stress reading all of /dev
 */
static int stress_dev(stress_args_t *args)
{
	pthread_t pthreads[STRESS_DEV_THREADS_MAX];
	int ret[STRESS_DEV_THREADS_MAX], rc = EXIT_SUCCESS;
	stress_pthread_args_t pa;
	char *dev_file = NULL;
	const int stdout_fd = fileno(stdout);
	const char *tty_name = (stdout_fd >= 0) ? ttyname(stdout_fd) : NULL;
	dev_state_t dev_state_null, *mmap_dev_states;
	dev_info_t dev_null = { "/dev/null", "null", 0, &dev_state_null, NULL };
	size_t mmap_dev_states_size;
	const size_t page_size = args->page_size;

	dev_info_t *dev_info_list = NULL;
	size_t dev_info_list_len = 0;

	sys_dev_info_t *sys_dev_info_list = NULL, *sys_dev_info_list_end = NULL;

	stress_dev_state_init(&dev_state_null);

	pthread_dev_info = &dev_null;
	pa.args = args;

	(void)shim_memset(ret, 0, sizeof(ret));

	(void)stress_get_setting("dev-file", &dev_file);
	if (dev_file) {
		mode_t mode;
		struct stat statbuf;

		if (shim_stat(dev_file, &statbuf) < 0) {
			pr_fail("%s: cannot access file %s\n",
				args->name, dev_file);
			return EXIT_FAILURE;
		}
		mode = statbuf.st_mode & S_IFMT;
		if ((mode != S_IFBLK) && (mode != S_IFCHR)) {
			pr_fail("%s: file %s is not a character or block device\n",
				args->name, dev_file);
			return EXIT_FAILURE;
		}

		stress_dev_info_add(args, dev_file, &dev_info_list, &dev_info_list_len, true);
	} else {
		stress_dev_infos_get(args, "/dev", tty_name, &dev_info_list, &dev_info_list_len);
		stress_sys_dev_infos_get(args, "/sys/dev", &sys_dev_info_list, &sys_dev_info_list_end, 0);
	}

	/* This should be rare */
	if (dev_info_list_len == 0) {
		pr_inf_skip("%s: cannot allocate device information or find any "
			"testable devices, skipping stressor\n", args->name);
		stress_dev_infos_free(&dev_info_list);
		stress_sys_dev_infos_free(&sys_dev_info_list);
		return EXIT_NO_RESOURCE;
	}
	pa.data = (void *)sys_dev_info_list;

	stress_dev_infos_mixup(&dev_info_list, dev_info_list_len);

	mmap_dev_states_size = sizeof(*mmap_dev_states) * dev_info_list_len;
	mmap_dev_states_size = (mmap_dev_states_size + page_size - 1) & ~(page_size - 1);
	mmap_dev_states = mmap(NULL, mmap_dev_states_size, PROT_READ | PROT_WRITE,
			MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (mmap_dev_states == MAP_FAILED) {
		pr_inf_skip("%s: cannot allocate shared memory for device state data, "
			"errno=%d (%s), skipping stressor\n",
			args->name, errno, strerror(errno));
		rc = EXIT_NO_RESOURCE;
		goto deinit;
	}
	stress_set_vma_anon_name(mmap_dev_states, mmap_dev_states_size, "dev-states");
	
	stress_dev_info_list_state_init(dev_info_list, mmap_dev_states);

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		pid_t pid;

again:
		pid = fork();
		if (pid < 0) {
			if (stress_redo_fork(args, errno))
				goto again;
		} else if (pid > 0) {
			int status;
			pid_t wret;

			/* Parent, wait for child */
			wret = waitpid(pid, &status, 0);
			if (wret < 0) {
				if (errno != EINTR)
					pr_dbg("%s: waitpid() on PID %" PRIdMAX" failed, errno=%d (%s)\n",
						args->name, (intmax_t)pid, errno, strerror(errno));
				/* Ring ring, time to die */
				(void)stress_kill_and_wait(args, pid, SIGALRM, false);
			} else {
				if (WIFEXITED(status) &&
				    WEXITSTATUS(status) != 0) {
					rc = EXIT_FAILURE;
					break;
				}
			}
		} else {
			size_t i;
			int r;
			sys_dev_info_t *sys_dev_info = sys_dev_info_list;

			stress_set_proc_state(args->name, STRESS_STATE_RUN);
			stress_parent_died_alarm();
			(void)sched_settings_apply(true);
			rc = shim_pthread_spin_init(&lock, SHIM_PTHREAD_PROCESS_SHARED);
			if (rc) {
				pr_inf("%s: pthread_spin_init failed, errno=%d (%s)\n",
					args->name, rc, strerror(rc));
				_exit(EXIT_NO_RESOURCE);
			}
			rc = shim_pthread_spin_init(&parport_lock, SHIM_PTHREAD_PROCESS_SHARED);
			if (rc) {
				pr_inf("%s: pthread_spin_init failed, errno=%d (%s)\n",
					args->name, rc, strerror(rc));
				_exit(EXIT_NO_RESOURCE);
			}

			/* Make sure this is killable by OOM killer */
			stress_set_oom_adjustment(args, true);

			for (i = 0; i < STRESS_DEV_THREADS_MAX; i++) {
				ret[i] = pthread_create(&pthreads[i], NULL,
						stress_dev_thread, (void *)&pa);
			}

			do {
				stress_dev_files(args, dev_info_list, &sys_dev_info);
			} while (stress_continue(args));

			r = shim_pthread_spin_lock(&lock);
			if (r) {
				pr_dbg("%s: failed to lock spin lock for dev_path\n", args->name);
			} else {
				pthread_dev_info = NULL;
				VOID_RET(int, shim_pthread_spin_unlock(&lock));
			}

			for (i = 0; i < STRESS_DEV_THREADS_MAX; i++) {
				if (ret[i] == 0)
					(void)pthread_join(pthreads[i], NULL);
			}
			_exit(EXIT_SUCCESS);
		}
	} while (stress_continue(args));

	if (stress_instance_zero(args)) {
		const size_t opened = stress_dev_infos_opened(dev_info_list);
		const bool is_root = stress_check_capability(SHIM_CAP_IS_ROOT);

		pr_inf("%s: %zd of %zd devices opened and exercised%s\n",
			args->name, opened, dev_info_list_len,
			is_root ? "" : " (run as root to exercise more devices)");
	}

	(void)munmap((void *)mmap_dev_states, mmap_dev_states_size);

deinit:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
	(void)shim_pthread_spin_destroy(&lock);

	/*
	 *  Ensure we don't get build warnings if these are not
	 *  referenced.
	 */
	(void)ioctl_set_timeout;
	(void)ioctl_clr_timeout;

	stress_sys_dev_infos_free(&sys_dev_info_list);
	stress_dev_infos_free(&dev_info_list);

	return rc;
}
const stressor_info_t stress_dev_info = {
	.stressor = stress_dev,
	.classifier = CLASS_DEV | CLASS_OS,
	.opts = opts,
	.help = help
};
#else
const stressor_info_t stress_dev_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_DEV | CLASS_OS,
	.opts = opts,
	.help = help,
	.unimplemented_reason = "built without pthread support or poll.h"
};
#endif
