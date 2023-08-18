// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King.
 *
 */
#include "stress-ng.h"
#include "core-arch.h"
#include "core-builtin.h"
#include "core-pthread.h"
#include "core-pragma.h"
#include "core-put.h"

#if defined(HAVE_LIBGEN_H)
#include <libgen.h>
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

#if defined(HAVE_LINUX_FD_H)
#include <linux/fd.h>
#endif

#if defined(HAVE_LINUX_FS_H)
#include <linux/fs.h>
#endif

#if defined(HAVE_LINUX_HDREG_H)
#include <linux/hdreg.h>
#endif

#if defined(HAVE_LINUX_HPET_H)
#include <linux/hpet.h>
#endif

#if defined(HAVE_LINUX_KD_H)
#include <linux/kd.h>
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

#if defined(HAVE_LINUX_SERIAL_H)
#include <linux/serial.h>
#endif

#if defined(HAVE_LINUX_PTP_CLOCK_H)
#include <linux/ptp_clock.h>
#endif

#if defined(HAVE_LINUX_USBDEVICE_FS_H)
#include <linux/usbdevice_fs.h>
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
	dev_state_t *state;	/* Pointer to shared memory state */
	struct dev_info *next;	/* Next device in device list */
} dev_info_t;

static const stress_help_t help[] = {
	{ NULL,	"dev N",	"start N device entry thrashing stressors" },
	{ NULL, "dev-file name","specify the /dev/ file to exercise" },
	{ NULL,	"dev-ops N",	"stop after N device thrashing bogo ops" },
	{ NULL,	NULL,		NULL }
};

static int stress_set_dev_file(const char *opt)
{
	return stress_set_setting("dev-file", TYPE_ID_STR, opt);
}

static const stress_opt_set_func_t opt_set_funcs[] = {
	{ OPT_dev_file,         stress_set_dev_file },
        { 0,                    NULL },
};

#if defined(HAVE_POLL_H) &&		\
    defined(HAVE_LIB_PTHREAD) && 	\
    !defined(__sun__) && 		\
    !defined(__HAIKU__)

#define STRESS_DEV_THREADS_MAX		(4)
#define STRESS_DEV_OPEN_TRIES_MAX	(8)

typedef struct stress_dev_func {
	const char *devpath;
	const size_t devpath_len;
	void (*func)(const stress_args_t *args, const int fd, const char *devpath);
} stress_dev_func_t;

static sigset_t set;
static shim_pthread_spinlock_t lock;
static shim_pthread_spinlock_t parport_lock;
static dev_info_t *pthread_dev_info;

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
	ret = stat("/sys/hypervisor/properties/features", &statbuf);
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
	UNEXPECTED
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
#else
	UNEXPECTED
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
	const stress_args_t *args,
	const int fd,
	const char *devpath)
{
	(void)args;
	(void)fd;
	(void)devpath;

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
#else
	UNEXPECTED
#endif
}
#endif

#if defined(HAVE_LINUX_VT_H)
static void stress_dev_vcs_linux(
	const stress_args_t *args,
	const int fd,
	const char *devpath)
{
	(void)args;
	(void)fd;
	(void)devpath;

#if defined(VT_GETMODE) &&	\
    defined(HAVE_VT_MODE)
	{
		struct vt_mode mode;

		VOID_RET(int, ioctl(fd, VT_GETMODE, &mode));
	}
#else
	UNEXPECTED
#endif
#if defined(VT_GETSTATE) &&	\
    defined(HAVE_VT_STAT)
	{
		struct vt_stat vt_stat;

		VOID_RET(int, ioctl(fd, VT_GETSTATE, &vt_stat));
	}
#else
	UNEXPECTED
#endif
}
#endif

#if defined(HAVE_LINUX_DM_IOCTL_H)
static void stress_dev_dm_linux(
	const stress_args_t *args,
	const int fd,
	const char *devpath)
{
	(void)args;
	(void)fd;
	(void)devpath;

#if defined(DM_VERSION) &&	\
    defined(HAVE_DM_IOCTL)
	{
		struct dm_ioctl dm;

		VOID_RET(int, ioctl(fd, DM_VERSION, &dm));
	}
#else
	UNEXPECTED
#endif
#if defined(DM_DEV_STATUS) &&	\
    defined(HAVE_DM_IOCTL)
	{
		struct dm_ioctl dm;

		VOID_RET(int, ioctl(fd, DM_DEV_STATUS, &dm));
	}
#else
	UNEXPECTED
#endif
}
#endif

#if defined(HAVE_LINUX_VIDEODEV2_H)
static void stress_dev_video_linux(
	const stress_args_t *args,
	const int fd,
	const char *devpath)
{
	(void)args;
	(void)fd;
	(void)devpath;

#if defined(VIDIOC_QUERYCAP) &&	\
    defined(HAVE_V4L2_CAPABILITY)
	{
		struct v4l2_capability c;

		(void)shim_memset(&c, 0, sizeof(c));
		VOID_RET(int, ioctl(fd, VIDIOC_QUERYCAP, &c));
	}
#else
	UNEXPECTED
#endif
#if defined(VIDIOC_G_FBUF) &&	\
    defined(HAVE_V4L2_FRAMEBUFFER)
	{
		struct v4l2_framebuffer f;

		(void)shim_memset(&f, 0, sizeof(f));
		VOID_RET(int, ioctl(fd, VIDIOC_G_FBUF, &f));
	}
#else
	UNEXPECTED
#endif
#if defined(VIDIOC_G_STD) &&	\
    defined(HAVE_V4L2_STD_ID)
	{
		v4l2_std_id id;

		(void)shim_memset(&id, 0, sizeof(id));
		VOID_RET(int, ioctl(fd, VIDIOC_G_STD, &id));
	}
#else
	UNEXPECTED
#endif
#if defined(VIDIOC_G_AUDIO) &&	\
    defined(HAVE_V4L2_AUDIO)
	{
		struct v4l2_audio a;

		(void)shim_memset(&a, 0, sizeof(a));
		VOID_RET(int, ioctl(fd, VIDIOC_G_AUDIO, &a));
	}
#else
	UNEXPECTED
#endif
#if defined(VIDIOC_G_INPUT)
	{
		int in = 0;

		VOID_RET(int, ioctl(fd, VIDIOC_G_INPUT, &in));
	}
#else
	UNEXPECTED
#endif
#if defined(VIDIOC_G_OUTPUT)
	{
		int in = 0;

		VOID_RET(int, ioctl(fd, VIDIOC_G_OUTPUT, &in));
	}
#else
	UNEXPECTED
#endif
#if defined(VIDIOC_G_AUDOUT) &&	\
    defined(HAVE_V4L2_AUDIOOUT)
	{
		struct v4l2_audioout a;

		(void)shim_memset(&a, 0, sizeof(a));
		VOID_RET(int, ioctl(fd, VIDIOC_G_AUDOUT, &a));
	}
#else
	UNEXPECTED
#endif
#if defined(VIDIOC_G_JPEGCOMP) && \
    defined(HAVE_V4L2_JPEGCOMPRESSION)
	{
		struct v4l2_jpegcompression a;

		(void)shim_memset(&a, 0, sizeof(a));
		VOID_RET(int, ioctl(fd, VIDIOC_G_JPEGCOMP, &a));
	}
#else
	UNEXPECTED
#endif
#if defined(VIDIOC_QUERYSTD) &&	\
    defined(HAVE_V4L2_STD_ID)
	{
		v4l2_std_id a;

		(void)shim_memset(&a, 0, sizeof(a));
		VOID_RET(int, ioctl(fd, VIDIOC_QUERYSTD, &a));
	}
#else
	UNEXPECTED
#endif
#if defined(VIDIOC_G_PRIORITY)
	{
		uint32_t a;

		VOID_RET(int, ioctl(fd, VIDIOC_G_PRIORITY, &a));
	}
#else
	UNEXPECTED
#endif
#if defined(VIDIOC_G_ENC_INDEX) &&	\
    defined(HAVE_V4L2_ENC_IDX)
	{
		struct v4l2_enc_idx a;

		(void)shim_memset(&a, 0, sizeof(a));
		VOID_RET(int, ioctl(fd, VIDIOC_G_ENC_INDEX, &a));
	}
#else
	UNEXPECTED
#endif
#if defined(VIDIOC_QUERY_DV_TIMINGS) &&	\
    defined(HAVE_V4L2_DV_TIMINGS)
	{
		struct v4l2_dv_timings a;

		(void)shim_memset(&a, 0, sizeof(a));
		VOID_RET(int, ioctl(fd, VIDIOC_QUERY_DV_TIMINGS, &a));
	}
#else
	UNEXPECTED
#endif
}
#endif

#if defined(HAVE_TERMIOS_H) &&	\
    defined(HAVE_TERMIOS) &&	\
    defined(TCGETS)
static void stress_dev_tty(
	const stress_args_t *args,
	const int fd,
	const char *devpath)
{
	struct termios t;
	int ret;

	(void)args;
	(void)devpath;

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
#else
		UNEXPECTED
#endif
		(void)ret;
	}
#else
	UNEXPECTED
#endif
#if defined(TIOCGPTLCK)
	{
		int lck;

		ret = ioctl(fd, TIOCGPTLCK, &lck);
#if defined(TIOCSPTLCK)
		if (ret == 0) {
			ret = ioctl(fd, TIOCSPTLCK, &lck);
		}
#else
		UNEXPECTED
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
#else
		UNEXPECTED
#endif
		(void)ret;
	}
#else
	UNEXPECTED
#endif
#if defined(TIOCGPTN)
	{
		int ptnum;

		VOID_RET(int, ioctl(fd, TIOCGPTN, &ptnum));
	}
#else
	UNEXPECTED
#endif
#if defined(TIOCSIG) &&	\
    defined(SIGCONT)
	{
		int sig = SIGCONT;

		/* generally causes EINVAL */
		VOID_RET(int, ioctl(fd, TIOCSIG, &sig));
	}
#else
	UNEXPECTED
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
#else
		UNEXPECTED
#endif
		(void)ret;
	}
#else
	UNEXPECTED
#endif
#if defined(FIONREAD)
	{
		int n;

		VOID_RET(int, ioctl(fd, FIONREAD, &n));
	}
#else
	UNEXPECTED
#endif
#if defined(TIOCINQ)
	{
		int n;

		VOID_RET(int, ioctl(fd, TIOCINQ, &n));
	}
#else
	UNEXPECTED
#endif
#if defined(TIOCOUTQ)
	{
		int n;

		VOID_RET(int, ioctl(fd, TIOCOUTQ, &n));
	}
#else
	UNEXPECTED
#endif
#if defined(TIOCGPGRP)
	{
		pid_t pgrp;

		ret = ioctl(fd, TIOCGPGRP, &pgrp);
#if defined(TIOCSPGRP)
		if (ret == 0) {
			ret = ioctl(fd, TIOCSPGRP, &pgrp);
		}
#else
		UNEXPECTED
#endif
		(void)ret;
	}
#else
	UNEXPECTED
#endif
#if defined(TIOCGSID)
	{
		pid_t gsid;

		VOID_RET(int, ioctl(fd, TIOCGSID, &gsid));
	}
#else
	UNEXPECTED
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
#else
		UNEXPECTED
#endif
		}
		(void)ret;
	}
#else
	UNEXPECTED
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
#else
		UNEXPECTED
#endif
		(void)ret;
	}
#else
	/* UNEXPECTED */
#endif

#if defined(TIOCGPTPEER)
	{
		VOID_RET(int, ioctl(fd, TIOCGPTPEER, O_RDWR));
	}
#else
	UNEXPECTED
#endif

#if defined(TCXONC) &&	\
    defined(TCOOFF) && 	\
    defined(TCOON)
	{
		ret = ioctl(fd, TCXONC, TCOOFF);
		if (ret == 0)
			ret = ioctl(fd, TCXONC, TCOON);
		(void)ret;
	}
#else
	UNEXPECTED
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
#else
	UNEXPECTED
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
#else
		UNEXPECTED
#endif
		(void)ret;
	}
#else
	UNEXPECTED
#endif

#if defined(KDGETLED)
	{
		char state;

		VOID_RET(int, ioctl(fd, KDGETLED, &state));
	}
#else
	UNEXPECTED
#endif

#if defined(KDGKBTYPE)
	{
		char type;

		VOID_RET(int, ioctl(fd, KDGKBTYPE, &type));
	}
#else
	UNEXPECTED
#endif

#if defined(KDGETMODE)
	{
		int mode;

		VOID_RET(int, ioctl(fd, KDGETMODE, &mode));
	}
#else
	UNEXPECTED
#endif

#if defined(KDGKBMODE)
	{
		long mode;

		VOID_RET(int, ioctl(fd, KDGKBMODE, &mode));
	}
#else
	UNEXPECTED
#endif

#if defined(KDGKBMETA)
	{
		long mode;

		VOID_RET(int, ioctl(fd, KDGKBMETA, &mode));
	}
#else
	UNEXPECTED
#endif
#if defined(TIOCMGET)
	{
		int status;

		ret = ioctl(fd, TIOCMGET, &status);
#if defined(TIOCMSET)
		if (ret == 0) {
#if defined(TIOCMBIC)
			VOID_RET(int, ioctl(fd, TIOCMBIC, &status));
#else
			UNEXPECTED
#endif
#if defined(TIOCMBIS)
			VOID_RET(int, ioctl(fd, TIOCMBIS, &status));
#else
			UNEXPECTED
#endif
			ret = ioctl(fd, TIOCMSET, &status);
		}
#else
		UNEXPECTED
#endif
		(void)ret;
	}
#else
	UNEXPECTED
#endif
#if defined(TIOCGICOUNT) &&		\
    defined(HAVE_LINUX_SERIAL_H) &&	\
    defined(HAVE_SERIAL_ICOUNTER)
	{
		struct serial_icounter_struct counter;

		VOID_RET(int, ioctl(fd, TIOCGICOUNT, &counter));
	}
#else
	UNEXPECTED
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
#else
		UNEXPECTED
#endif
		(void)ret;
	}
#else
	UNEXPECTED
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
#else
			UNEXPECTED
#endif
#if defined(TCSETSW2)
			VOID_RET(int, ioctl(fd, TCSETSW2, &t2));
#else
			UNEXPECTED
#endif
#if defined(TCSETS2)
			VOID_RET(int, ioctl(fd, TCSETS2, &t2));
			(void)ret;
#else
			UNEXPECTED
#endif
		}
#else
		UNEXPECTED
#endif
	}
#else
	UNEXPECTED
#endif
}
#else
UNEXPECTED
#endif

/*
 *  stress_dev_blk()
 *	block device specific ioctls
 */
static void stress_dev_blk(
	const stress_args_t *args,
	const int fd,
	const char *devpath)
{
	off_t offset;

	(void)args;
	(void)fd;
	(void)devpath;

#if defined(BLKFLSBUF)
	{
		VOID_RET(int, ioctl(fd, BLKFLSBUF, 0));
	}
#else
	UNEXPECTED
#endif

#if defined(BLKRAGET)
	/* readahead */
	{
		unsigned long ra;
		int ret;

		ret = ioctl(fd, BLKRAGET, &ra);
#if defined(BLKRASET)
		if (ret == 0)
			ret = ioctl(fd, BLKRASET, ra);
#endif
		(void)ret;
	}
#else
	UNEXPECTED
#endif

#if defined(BLKFRAGET)
	/* readahead */
	{
		unsigned long fra;
		int ret;

		ret = ioctl(fd, BLKFRAGET, &fra);
#if defined(BLKFRASET)
		if (ret == 0)
			ret = ioctl(fd, BLKFRASET, fra);
#endif
		(void)ret;
	}
#else
	UNEXPECTED
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
#else
	UNEXPECTED
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
#else
	UNEXPECTED
#endif

#if defined(BLKPBSZGET)
	/* get block device physical block size */
	{
		unsigned int sz;

		VOID_RET(int, ioctl(fd, BLKPBSZGET, &sz));
	}
#else
	UNEXPECTED
#endif

#if defined(BLKIOMIN)
	{
		unsigned int sz;

		VOID_RET(int, ioctl(fd, BLKIOMIN, &sz));
	}
#else
	UNEXPECTED
#endif

#if defined(BLKIOOPT)
	{
		unsigned int sz;

		VOID_RET(int, ioctl(fd, BLKIOOPT, &sz));
	}
#else
	UNEXPECTED
#endif

#if defined(BLKALIGNOFF)
	{
		unsigned int sz;

		VOID_RET(int, ioctl(fd, BLKALIGNOFF, &sz));
	}
#else
	UNEXPECTED
#endif

#if defined(BLKROTATIONAL)
	{
		unsigned short rotational;

		VOID_RET(int, ioctl(fd, BLKROTATIONAL, &rotational));
	}
#else
	UNEXPECTED
#endif

#if defined(BLKSECTGET)
	{
		unsigned short max_sectors;

		VOID_RET(int, ioctl(fd, BLKSECTGET, &max_sectors));
	}
#else
	UNEXPECTED
#endif

#if defined(BLKGETSIZE)
	{
		unsigned long sz;

		VOID_RET(int, ioctl(fd, BLKGETSIZE, &sz));
	}
#else
	UNEXPECTED
#endif

#if defined(BLKGETSIZE64)
	{
		uint64_t sz;

		VOID_RET(int, ioctl(fd, BLKGETSIZE64, &sz));
	}
#else
	UNEXPECTED
#endif

#if defined(BLKGETDISKSEQ)
	{
		uint64_t diskseq;

		VOID_RET(int, ioctl(fd, BLKGETDISKSEQ, &diskseq));
	}
#else
	UNEXPECTED
#endif

#if defined(BLKGETZONESZ)
	{
		uint32_t sz;

		VOID_RET(int, ioctl(fd, BLKGETZONESZ, &sz));
	}
#else
	UNEXPECTED
#endif

#if defined(BLKGETNRZONES)
	{
		uint32_t sz;

		VOID_RET(int, ioctl(fd, BLKGETNRZONES, &sz));
	}
#else
	UNEXPECTED
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
	const stress_args_t *args,
	const int fd,
	dev_info_t *dev_info)
{
	(void)args;
	(void)fd;

	if (!is_scsi_dev(dev_info))
		return;

#if defined(SG_GET_VERSION_NUM)
	{
		int ver;

		VOID_RET(int, ioctl(fd, SG_GET_VERSION_NUM, &ver));
	}
#else
	UNEXPECTED
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
#else
	UNEXPECTED
#endif
#if defined(SCSI_IOCTL_GET_BUS_NUMBER)
	{
		int bus;

		VOID_RET(int, ioctl(fd, SCSI_IOCTL_GET_BUS_NUMBER, &bus));
	}
#else
	UNEXPECTED
#endif
#if defined(SG_GET_TIMEOUT)
	{
		VOID_RET(int, ioctl(fd, SG_GET_TIMEOUT, 0));
	}
#else
	UNEXPECTED
#endif
#if defined(SG_GET_RESERVED_SIZE)
	{
		int sz;

		VOID_RET(int, ioctl(fd, SG_GET_RESERVED_SIZE, &sz));
	}
#else
	UNEXPECTED
#endif
#if defined(SCSI_IOCTL_GET_PCI)
	{
		/* Old ioctl was 20 chars, new API 8 chars, 32 is plenty */
		char pci[32];

		VOID_RET(int, ioctl(fd, SCSI_IOCTL_GET_PCI, pci));
	}
#else
	/* UNEXPECTED */
#endif
}

#if defined(__linux__)
/*
 *  stress_dev_scsi_generic_linux()
 *	SCSI generic device specific ioctls for linux
 */
static void stress_dev_scsi_generic_linux(
	const stress_args_t *args,
	const int fd,
	const char *devpath)
{
	(void)args;
	(void)fd;
	(void)devpath;

#if defined(SG_GET_VERSION_NUM)
	{
		int version = 0;

		VOID_RET(int, ioctl(fd, SG_GET_VERSION_NUM, &version));
	}
#else
	UNEXPECTED
#endif
#if defined(SG_GET_TIMEOUT)
	{
		VOID_RET(int, ioctl(fd, SG_GET_TIMEOUT, 0));
	}
#else
	UNEXPECTED
#endif
#if defined(SG_GET_LOW_DMA)
	{
		int low;

		VOID_RET(int, ioctl(fd, SG_GET_LOW_DMA, &low));
	}
#else
	UNEXPECTED
#endif
#if defined(SG_GET_PACK_ID)
	{
		int pack_id;

		VOID_RET(int, ioctl(fd, SG_GET_PACK_ID, &pack_id));
	}
#else
	UNEXPECTED
#endif
#if defined(SG_GET_NUM_WAITING)
	{
		int n;

		VOID_RET(int, ioctl(fd, SG_GET_NUM_WAITING, &n));
	}
#else
	UNEXPECTED
#endif
#if defined(SG_GET_SG_TABLESIZE)
	{
		int size;

		VOID_RET(int, ioctl(fd, SG_GET_SG_TABLESIZE, &size));
	}
#else
	UNEXPECTED
#endif
#if defined(SG_GET_RESERVED_SIZE)
	{
		int size;

		VOID_RET(int, ioctl(fd, SG_GET_RESERVED_SIZE, &size));
	}
#else
	UNEXPECTED
#endif
#if defined(SG_GET_COMMAND_Q)
	{
		int cmd_q;

		VOID_RET(int, ioctl(fd, SG_GET_COMMAND_Q, &cmd_q));
	}
#else
	UNEXPECTED
#endif
#if defined(SG_GET_ACCESS_COUNT)
	{
		int n;

		VOID_RET(int, ioctl(fd, SG_GET_ACCESS_COUNT, &n));
	}
#else
	/* UNEXPECTED */
#endif
#if defined(SCSI_IOCTL_GET_IDLUN)
	{
		struct shim_scsi_idlun {
			int four_in_one;
			int host_unique_id;
		} idlun;

		VOID_RET(int, ioctl(fd, SCSI_IOCTL_GET_IDLUN, &idlun));
	}
#else
	UNEXPECTED
#endif
#if defined(SCSI_IOCTL_GET_BUS_NUMBER)
	{
		int bus;

		VOID_RET(int, ioctl(fd, SCSI_IOCTL_GET_BUS_NUMBER, &bus));
	}
#else
	UNEXPECTED
#endif
#if defined(SG_GET_TRANSFORM)
	{
		VOID_RET(int, ioctl(fd, SG_GET_TRANSFORM, 0));
	}
#else
	UNEXPECTED
#endif
#if defined(SG_EMULATED_HOST)
	{
		int emulated;

		VOID_RET(int, ioctl(fd, SG_EMULATED_HOST, &emulated));
	}
#else
	UNEXPECTED
#endif
#if defined(BLKSECTGET)
	{
		int n;

		VOID_RET(int, ioctl(fd, BLKSECTGET, &n));
	}
#else
	UNEXPECTED
#endif
}
#else
UNEXPECTED
#endif

#if defined(HAVE_LINUX_RANDOM_H)
/*
 *  stress_dev_random_linux()
 *	Linux /dev/random ioctls
 */
static void stress_dev_random_linux(
	const stress_args_t *args,
	const int fd,
	const char *devpath)
{
	(void)args;
	(void)fd;
	(void)devpath;

#if defined(RNDGETENTCNT)
	{
		long entropy;

		VOID_RET(int, ioctl(fd, RNDGETENTCNT, &entropy));
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
	const stress_args_t *args,
	const int fd,
	const char *devpath)
{
	(void)args;
	(void)devpath;

	stress_dev_mem_mmap_linux(fd, devpath, true, true);
}
#endif

#if defined(__linux__)
static void stress_dev_kmem_linux(
	const stress_args_t *args,
	const int fd,
	const char *devpath)
{
	(void)args;
	(void)devpath;

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
#else
	UNEXPECTED
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
#else
	/* UNEXPECTED */
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
#else
		UNEXPECTED
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
#else
		UNEXPECTED
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
#else
		UNEXPECTED
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
#else
		UNEXPECTED
#endif
	}
#else
	UNEXPECTED
#endif
}
#else
UNEXPECTED
#endif

#if defined(__linux__)
static void stress_dev_cdrom_linux(
	const stress_args_t *args,
	const int fd,
	const char *devpath)
{
	size_t i;

	static const char *proc_files[] = {
		"autoclose",
		"autoeject",
		"check_media",
		"debug",
		"info",
		"lock",
	};

	(void)args;
	(void)fd;
	(void)devpath;

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
#else
	UNEXPECTED
#endif
#if defined(CDROM_GET_MCN) &&	\
    defined(HAVE_CDROM_MCN)
	IOCTL_TIMEOUT(0.10, {
		struct cdrom_mcn mcn;

		(void)shim_memset(&mcn, 0, sizeof(mcn));
		VOID_RET(int, ioctl(fd, CDROM_GET_MCN, &mcn));
	}, return);
#else
	UNEXPECTED
#endif
#if defined(CDROMREADTOCHDR) &&		\
    defined(HAVE_CDROM_TOCHDR)
	IOCTL_TIMEOUT(0.10, {
		struct cdrom_tochdr header;

		(void)shim_memset(&header, 0, sizeof(header));
		VOID_RET(int, ioctl(fd, CDROMREADTOCHDR, &header));
	}, return);
#else
	UNEXPECTED
#endif
#if defined(CDROMREADTOCENTRY) &&	\
    defined(HAVE_CDROM_TOCENTRY)
	IOCTL_TIMEOUT(0.10, {
		struct cdrom_tocentry entry;

		(void)shim_memset(&entry, 0, sizeof(entry));
		VOID_RET(int, ioctl(fd, CDROMREADTOCENTRY, &entry));
	}, return);
#else
	UNEXPECTED
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
#else
	UNEXPECTED
#endif
#if defined(CDROMREADAUDIO) &&	\
    defined(HAVE_CDROM_READ_AUDIO)
	IOCTL_TIMEOUT(0.10, {
		struct cdrom_read_audio ra;

		(void)shim_memset(&ra, 0, sizeof(ra));
		VOID_RET(int, ioctl(fd, CDROMREADAUDIO, &ra));
	}, return);
#else
	UNEXPECTED
#endif
#if defined(CDROMREADCOOKED) &&	\
    defined(CD_FRAMESIZE)
	IOCTL_TIMEOUT(0.10, {
		uint8_t buffer[CD_FRAMESIZE];

		(void)shim_memset(&buffer, 0, sizeof(buffer));
		VOID_RET(int, ioctl(fd, CDROMREADCOOKED, buffer));
	}, return);
#else
	UNEXPECTED
#endif
#if defined(CDROMREADALL) &&	\
    defined(CD_FRAMESIZE)
	IOCTL_TIMEOUT(0.10, {
		uint8_t buffer[CD_FRAMESIZE];

		(void)shim_memset(&buffer, 0, sizeof(buffer));
		VOID_RET(int, ioctl(fd, CDROMREADALL, buffer));
	}, return);
#else
	UNEXPECTED
#endif
#if defined(CDROMSEEK) &&	\
    defined(HAVE_CDROM_MSF)
	IOCTL_TIMEOUT(0.10, {
		struct cdrom_msf msf;

		(void)shim_memset(&msf, 0, sizeof(msf));
		VOID_RET(int, ioctl(fd, CDROMSEEK, &msf));
	}, return);
#else
	UNEXPECTED
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
#else
		UNEXPECTED
#endif
		(void)ret;
	}
#else
	UNEXPECTED
#endif

#if defined(CDROM_DISC_STATUS)
	IOCTL_TIMEOUT(0.10, { VOID_RET(int, ioctl(fd, CDROM_DISC_STATUS, 0)); }, return);
#else
	UNEXPECTED
#endif

#if defined(CDROM_GET_CAPABILITY)
	IOCTL_TIMEOUT(0.10, { VOID_RET(int, ioctl(fd, CDROM_GET_CAPABILITY, 0)); }, return);
#else
	UNEXPECTED
#endif
#if defined(CDROM_CHANGER_NSLOTS)
	IOCTL_TIMEOUT(0.10, { VOID_RET(int, ioctl(fd, CDROM_CHANGER_NSLOTS, 0)); }, return);
#else
	UNEXPECTED
#endif
#if defined(CDROM_NEXT_WRITABLE)
	IOCTL_TIMEOUT(0.10, {
		long next;

		VOID_RET(int, ioctl(fd, CDROM_NEXT_WRITABLE, &next));
	}, return);
#else
	UNEXPECTED
#endif
#if defined(CDROM_LAST_WRITTEN)
	IOCTL_TIMEOUT(0.10, {
		long last;

		VOID_RET(int, ioctl(fd, CDROM_LAST_WRITTEN, &last));
	}, return);
#else
	UNEXPECTED
#endif
#if defined(CDROM_MEDIA_CHANGED) && 0
	IOCTL_TIMEOUT(0.10, {
		int slot = 0;

		VOID_RET(int, ioctl(fd, CDROM_MEDIA_CHANGED, slot));
	}, return);
#else
	/* UNEXPECTED */
#endif
#if defined(CDSL_NONE)
	IOCTL_TIMEOUT(0.10, {
		int slot = CDSL_NONE;

		VOID_RET(int, ioctl(fd, CDROM_MEDIA_CHANGED, slot));
	}, return);
#else
	UNEXPECTED
#endif
#if defined(CDSL_CURRENT)
	IOCTL_TIMEOUT(0.10, {
		int slot = CDSL_CURRENT;

		VOID_RET(int, ioctl(fd, CDROM_MEDIA_CHANGED, slot));
	}, return);
#else
	UNEXPECTED
#endif
#if defined(CDROMSTART)
	IOCTL_TIMEOUT(0.10, { VOID_RET(int, ioctl(fd, CDROMSTART, 0)); }, return);
#else
	UNEXPECTED
#endif
#if defined(CDROMPAUSE)
	IOCTL_TIMEOUT(0.10, { VOID_RET(int, ioctl(fd, CDROMPAUSE, 0)); }, return);
#else
	UNEXPECTED
#endif
#if defined(CDROMRESUME)
	IOCTL_TIMEOUT(0.10, { VOID_RET(int, ioctl(fd, CDROMRESUME, 0)); }, return);
#else
	UNEXPECTED
#endif
#if defined(CDROMSTOP)
	IOCTL_TIMEOUT(0.10, { VOID_RET(int, ioctl(fd, CDROMSTOP, 0)); }, return);
#else
	UNEXPECTED
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
#else
	UNEXPECTED
#endif
#if defined(CDSL_CURRENT)
	IOCTL_TIMEOUT(0.10, {
		int slot = CDSL_CURRENT;

		VOID_RET(int, ioctl(fd, CDROM_DRIVE_STATUS, slot));
	}, return);
#else
	UNEXPECTED
#endif
#else
	UNEXPECTED
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
#else
	UNEXPECTED
#endif
#if defined(CDROMAUDIOBUFSIZ)
	IOCTL_TIMEOUT(0.10, {
		int val = INT_MIN;

		/* Invalid CDROMAUDIOBUFSIZ call with negative buffer size */
		VOID_RET(int, ioctl(fd, CDROMAUDIOBUFSIZ, val));
	}, return);
#else
	UNEXPECTED
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
#else
	UNEXPECTED
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
#else
	UNEXPECTED
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
#else
	UNEXPECTED
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
#else
	UNEXPECTED
#endif

#if defined(CDROM_SELECT_DISC) &&	\
    defined(CDSL_CURRENT)
	IOCTL_TIMEOUT(0.10, { VOID_RET(int, ioctl(fd, CDROM_SELECT_DISC, CDSL_CURRENT)); }, return);
#else
	UNEXPECTED
#endif

#if defined(CDROMCLOSETRAY)
	IOCTL_TIMEOUT(0.10, { VOID_RET(int, ioctl(fd, CDROMCLOSETRAY, 0)); }, return);
#else
	UNEXPECTED
#endif

#if defined(CDROM_SELECT_SPEED)
	IOCTL_TIMEOUT(0.10, {
		unsigned int j;

		for (j = 8; j < 16; j++) {
			unsigned int speed = 1 << j;

			VOID_RET(int, ioctl(fd, CDROM_SELECT_SPEED, speed));
		}
	}, return);
#else
	UNEXPECTED
#endif

#if defined(CDROMPLAYBLK) &&	\
    defined(HAVE_CDROM_BLK)
	IOCTL_TIMEOUT(0.10, {
		struct cdrom_blk blk;

		(void)shim_memset(&blk, 0, sizeof(blk));
		VOID_RET(int, ioctl(fd, CDROMPLAYBLK, &blk));
	}, return);
#else
	UNEXPECTED
#endif
}
#else
UNEXPECTED
#endif

#if defined(__linux__)
static void stress_dev_console_linux(
	const stress_args_t *args,
	const int fd,
	const char *devpath)
{
	(void)args;
	(void)fd;
	(void)devpath;

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
#else
		UNEXPECTED
#endif
		(void)ret;
	}
#else
	UNEXPECTED
#endif

#if defined(HAVE_LINUX_KD_H) &&	\
    defined(KDGKBLED)
	{
		char argp;
		int ret;

		ret = ioctl(fd, KDGKBLED, &argp);
#if defined(KDSKBLED)
		if (ret == 0) {
			unsigned long bad_val = ~0UL, val;

			val = (unsigned long)argp;
			VOID_RET(int, ioctl(fd, KDSKBLED, val));

			/* Exercise Invalid KDSKBLED ioctl call with invalid flags */
			ret = ioctl(fd, KDSKBLED, bad_val);
			if (ret == 0) {
				/* Unexpected success, so set it back */
				ret = ioctl(fd, KDSKBLED, val);
			}
		}
#else
		UNEXPECTED
#endif
		(void)ret;
	}
#else
	UNEXPECTED
#endif

#if defined(HAVE_LINUX_KD_H) &&	\
    defined(KDGETMODE)
	{
		int ret;
		unsigned long argp = 0;

		ret = ioctl(fd, KDGETMODE, &argp);
#if defined(KDSETMODE)
		if (ret == 0) {
			unsigned long bad_val = ~0UL;

			VOID_RET(int, ioctl(fd, KDSETMODE, argp));

			/* Exercise Invalid KDSETMODE ioctl call with invalid flags */
			ret = ioctl(fd, KDSETMODE, bad_val);
			if (ret == 0) {
				/* Unexpected success, so set it back */
				ret = ioctl(fd, KDSETMODE, argp);
			}
		}
#else
		UNEXPECTED
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
#else
	UNEXPECTED
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
#else
		UNEXPECTED
#endif
		(void)ret;
	}
#else
	UNEXPECTED
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
#else
		UNEXPECTED
#endif
		(void)ret;
	}
#else
	UNEXPECTED
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
#else
		UNEXPECTED
#endif
		(void)ret;
	}
#else
	UNEXPECTED
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
#else
		UNEXPECTED
#endif
		(void)ret;
	}
#else
	UNEXPECTED
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
#else
		UNEXPECTED
#endif
		(void)ret;
	}
#else
	UNEXPECTED
#endif

#if defined(HAVE_LINUX_KD_H) && \
    defined(GIO_UNISCRNMAP) && \
    defined(E_TABSZ)
	{
		unsigned short argp[E_TABSZ];
		int ret;

		ret = ioctl(fd, GIO_UNISCRNMAP, argp);
#if defined(PIO_UNISCRNMAP)
		if (ret == 0)
			ret = ioctl(fd, PIO_UNISCRNMAP, argp);
#else
		UNEXPECTED
#endif
		(void)ret;
	}
#else
	UNEXPECTED
#endif

#if defined(HAVE_LINUX_KD_H) &&	\
    defined(KDGKBMODE)
	{
		int ret;
		unsigned long argp = 0;

		ret = ioctl(fd, KDGKBMODE, &argp);
#if defined(KDSKBMODE)
		if (ret == 0) {
			unsigned long bad_val = ~0UL;

			VOID_RET(int, ioctl(fd, KDSKBMODE, argp));

			/* Exercise Invalid KDSKBMODE ioctl call with invalid key mode */
			ret = ioctl(fd, KDSKBMODE, bad_val);
			if (ret == 0) {
				/* Unexpected success, so set it back */
				ret = ioctl(fd, KDSKBMODE, argp);
			}
		}
#else
		UNEXPECTED
#endif
		(void)ret;
	}
#else
	UNEXPECTED
#endif

#if defined(HAVE_LINUX_KD_H) &&	\
    defined(KDGKBMETA)
	{
		int ret;
		unsigned long argp = 0;

		ret = ioctl(fd, KDGKBMETA, &argp);
#if defined(KDSKBMETA)
		if (ret == 0) {
			unsigned long bad_val = ~0UL;

			VOID_RET(int, ioctl(fd, KDSKBMETA, argp));

			/* Exercise Invalid KDSKBMETA ioctl call with invalid key mode */
			ret = ioctl(fd, KDSKBMETA, bad_val);
			if (ret == 0) {
				/* Unexpected success, so set it back */
				ret = ioctl(fd, KDSKBMETA, argp);
			}
		}
#else
		UNEXPECTED
#endif
		(void)ret;
	}
#else
	UNEXPECTED
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
#else
		UNEXPECTED
#endif
		(void)ret;
	}
#else
	UNEXPECTED
#endif

#if defined(HAVE_LINUX_KD_H) && \
    defined(KDGKBDIACR) &&	\
    defined(HAVE_KBDIACRS)
	{
		struct kbdiacrs argp;

		(void)shim_memset(&argp, 0, sizeof(argp));
		VOID_RET(int, ioctl(fd, KDGKBDIACR, &argp));
	}
#else
	UNEXPECTED
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
#else
	UNEXPECTED
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
#else
	UNEXPECTED
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
#else
		UNEXPECTED
#endif
		(void)ret;
	}
#else
	UNEXPECTED
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
#else
		UNEXPECTED
#endif
		(void)ret;
	}
#else
	UNEXPECTED
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
#else
		UNEXPECTED
#endif
		(void)ret;
	}
#else
	UNEXPECTED
#endif
}
#else
UNEXPECTED
#endif

#if defined(__linux__)
static void stress_dev_kmsg_linux(
	const stress_args_t *args,
	const int fd,
	const char *devpath)
{
	(void)args;
	(void)devpath;

	stress_dev_mem_mmap_linux(fd, devpath, true, false);
}
#endif

#if defined(__linux__)
static void stress_dev_nvram_linux(
	const stress_args_t *args,
	const int fd,
	const char *devpath)
{
	(void)args;
	(void)devpath;

	stress_dev_mem_mmap_linux(fd, devpath, true, false);
}
#endif

#if defined(HAVE_LINUX_HPET_H)
static void stress_dev_hpet_linux(
	const stress_args_t *args,
	const int fd,
	const char *devpath)
{
	(void)args;
	(void)fd;
	(void)devpath;

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
#else
	UNEXPECTED
#endif
#if defined(HPET_IRQFREQ)
	{
		unsigned long freq;

		VOID_RET(int, ioctl(fd, HPET_IRQFREQ, &freq));
	}
#else
	UNEXPECTED
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
#else
	UNEXPECTED
#endif
}
#else
UNEXPECTED
#endif

#if defined(__linux__) &&	\
    defined(STRESS_ARCH_X86)
static void stress_dev_port_linux(
	const stress_args_t *args,
	const int fd,
	const char *devpath)
{
	off_t off;
	uint8_t *ptr;
	const size_t page_size = stress_get_page_size();

	(void)args;
	(void)devpath;

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

#if defined(HAVE_LINUX_HDREG_H)
static void stress_dev_hd_linux_ioctl_long(int fd, unsigned long cmd)
{
	long val;

	VOID_RET(int, ioctl(fd, cmd, &val));
}

/*
 *  stress_dev_hd_linux()
 *	Linux HDIO ioctls
 */
static void stress_dev_hd_linux(
	const stress_args_t *args,
	const int fd,
	const char *devpath)
{
	(void)args;
	(void)devpath;

#if defined(HDIO_GETGEO)
	{
		struct hd_geometry geom;

		VOID_RET(int, ioctl(fd, HDIO_GETGEO, &geom));
	}
#else
	UNEXPECTED
#endif

#if defined(HDIO_GET_UNMASKINTR)
	stress_dev_hd_linux_ioctl_long(fd, HDIO_GET_UNMASKINTR);
#else
	UNEXPECTED
#endif

#if defined(HDIO_GET_MULTCOUNT)
	{
		int val;

		VOID_RET(int, ioctl(fd, HDIO_GET_MULTCOUNT, &val));
	}
#else
	UNEXPECTED
#endif

#if defined(HDIO_GET_IDENTITY)
	{
		unsigned char identity[512];

		VOID_RET(int, ioctl(fd, HDIO_GET_IDENTITY, identity));
	}
#else
	UNEXPECTED
#endif

#if defined(HDIO_GET_KEEPSETTINGS)
	stress_dev_hd_linux_ioctl_long(fd, HDIO_GET_KEEPSETTINGS);
#else
	UNEXPECTED
#endif

#if defined(HDIO_GET_32BIT)
	stress_dev_hd_linux_ioctl_long(fd, HDIO_GET_32BIT);
#else
	UNEXPECTED
#endif

#if defined(HDIO_GET_NOWERR)
	stress_dev_hd_linux_ioctl_long(fd, HDIO_GET_NOWERR);
#else
	UNEXPECTED
#endif

#if defined(HDIO_GET_DMA)
	stress_dev_hd_linux_ioctl_long(fd, HDIO_GET_DMA);
#else
	UNEXPECTED
#endif

#if defined(HDIO_GET_NICE)
	stress_dev_hd_linux_ioctl_long(fd, HDIO_GET_NICE);
#else
	UNEXPECTED
#endif

#if defined(HDIO_GET_WCACHE)
	stress_dev_hd_linux_ioctl_long(fd, HDIO_GET_WCACHE);
#else
	UNEXPECTED
#endif

#if defined(HDIO_GET_ACOUSTIC)
	stress_dev_hd_linux_ioctl_long(fd, HDIO_GET_ACOUSTIC);
#else
	UNEXPECTED
#endif

#if defined(HDIO_GET_ADDRESS)
	stress_dev_hd_linux_ioctl_long(fd, HDIO_GET_ADDRESS);
#else
	UNEXPECTED
#endif

#if defined(HDIO_GET_BUSSTATE)
	stress_dev_hd_linux_ioctl_long(fd, HDIO_GET_BUSSTATE);
#else
	UNEXPECTED
#endif
}
#endif

static void stress_dev_null_nop(
	const stress_args_t *args,
	const int fd,
	const char *devpath)
{
	(void)args;
	(void)fd;
	(void)devpath;
}

/*
 *  stress_dev_ptp_linux()
 *	minor exercising of the PTP device
 */
static void stress_dev_ptp_linux(
	const stress_args_t *args,
	const int fd,
	const char *devpath)
{
#if defined(HAVE_LINUX_PTP_CLOCK_H) &&	\
    defined(PTP_CLOCK_GETCAPS) &&	\
    defined(PTP_PIN_GETFUNC)
	int ret;
	struct ptp_clock_caps caps;

	(void)args;
	(void)devpath;

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
#else
	(void)args;
	(void)fd;
	(void)devpath;
#endif
}

#if defined(HAVE_LINUX_FD_H)
/*
 *  stress_dev_floppy_linux()
 *	minor exercising of the floppy device
 */
static void stress_dev_floppy_linux(
	const stress_args_t *args,
	const int fd,
	const char *devpath)
{
	(void)args;
	(void)fd;
	(void)devpath;

#if defined(FDMSGON)
	VOID_RET(int, ioctl(fd, FDMSGON, 0));
#else
	UNEXPECTED
#endif

#if defined(FDFLUSH)
	VOID_RET(int, ioctl(fd, FDFLUSH, 0));
#else
	UNEXPECTED
#endif

#if defined(FDTWADDLE)
	VOID_RET(int, ioctl(fd, FDTWADDLE, 0));
#else
	UNEXPECTED
#endif

#if defined(FDCLRPRM)
	VOID_RET(int, ioctl(fd, FDCLRPRM, 0));
#else
	UNEXPECTED
#endif

#if defined(FDWERRORGET) &&		\
    defined(HAVE_FLOPPY_WRITE_ERRORS)
	{
		struct floppy_write_errors errors;

		VOID_RET(int, ioctl(fd, FDWERRORGET, &errors));
	}
#else
	UNEXPECTED
#endif

#if defined(FDWERRORCLR)
	VOID_RET(int, ioctl(fd, FDWERRORCLR, 0));
#else
	UNEXPECTED
#endif

#if defined(FDGETPRM) &&	\
    defined(HAVE_FLOPPY_STRUCT)
	{
		struct floppy_struct floppy;

		if (ioctl(fd, FDGETPRM, &floppy) == 0)
			VOID_RET(int, ioctl(fd, FDSETPRM, &floppy));
	}
#else
	UNEXPECTED
#endif

#if defined(FDGETDRVSTAT) &&	\
    defined(HAVE_FLOPPY_DRIVE_STRUCT)
	{
		struct floppy_drive_struct drive;

		VOID_RET(int, ioctl(fd, FDGETDRVSTAT, &drive));
	}
#else
	UNEXPECTED
#endif

#if defined(FDPOLLDRVSTAT) &&	\
    defined(HAVE_FLOPPY_DRIVE_STRUCT)
	{
		struct floppy_drive_struct drive;

		VOID_RET(int, ioctl(fd, FDPOLLDRVSTAT, &drive));
	}
#else
	UNEXPECTED
#endif

#if defined(FDGETDRVTYP)
	{
		char buf[64];

		VOID_RET(int, ioctl(fd, FDGETDRVTYP, buf));
	}
#else
	UNEXPECTED
#endif

#if defined(FDGETFDCSTAT) &&		\
    defined(HAVE_FLOPPY_FDC_STATE)
	{
		struct floppy_fdc_state state;

		VOID_RET(int, ioctl(fd, FDGETFDCSTAT, &state));

	}
#else
	UNEXPECTED
#endif

#if defined(_IO)
	/* Invalid ioctl */
	VOID_RET(int, ioctl(fd, _IO(2, 0xff), 0));
#endif

#if defined(FDMSGOFF)
	VOID_RET(int, ioctl(fd, FDMSGOFF, 0));
#else
	UNEXPECTED
#endif
}
#else
UNEXPECTED
#endif

/*
 *  stress_dev_snd_control_linux()
 * 	exercise Linux sound devices
 */
static void stress_dev_snd_control_linux(
	const stress_args_t *args,
	const int fd,
	const char *devpath)
{
	(void)args;
	(void)fd;
	(void)devpath;

#if defined(SNDRV_CTL_IOCTL_PVERSION)
	{
		int ver;

		VOID_RET(int, ioctl(fd, SNDRV_CTL_IOCTL_PVERSION, &ver));
	}
#else
	UNEXPECTED
#endif

#if defined(SNDRV_CTL_IOCTL_CARD_INFO) &&	\
    defined(HAVE_SND_CTL_CARD_INFO)
	{
		struct snd_ctl_card_info card;

		VOID_RET(int, ioctl(fd, SNDRV_CTL_IOCTL_CARD_INFO, &card));
	}
#else
	UNEXPECTED
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
#else
	UNEXPECTED
#endif

#if defined(SNDRV_CTL_IOCTL_POWER_STATE)
	{
		int ret, state;

		ret = ioctl(fd, SNDRV_CTL_IOCTL_POWER_STATE, &state);
#if defined(SNDRV_CTL_IOCTL_POWER)
		if (ret == 0)
			ret = ioctl(fd, SNDRV_CTL_IOCTL_POWER, &state);
#else
		UNEXPECTED
#endif
		(void)ret;
	}
#else
	UNEXPECTED
#endif
}

#if defined(__linux__)
/*
 *   stress_dev_hwrng_linux()
 *   	Exercise Linux Hardware Random Number Generator
 */
static void stress_dev_hwrng_linux(
	const stress_args_t *args,
	const int fd,
	const char *devpath)
{
	char buffer[8];

	(void)args;
	(void)devpath;

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
	const stress_args_t *args,
	const int fd,
	const char *devpath)
{
#if defined(PPCLAIM) && 	\
    defined(PPRELEASE)
	bool claimed = false;
#endif

	(void)args;
	(void)fd;
	(void)devpath;

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
	if (args->instance == 0) {
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
#else
	UNEXPECTED
#endif

#if defined(PPGETMODE)
	{
		int ret, mode;

		ret = ioctl(fd, PPGETMODE, &mode);
#if defined(PPSETMODE)
		errno = 0;
		if (ret == 0)
			ret = ioctl(fd, PPSETMODE, &mode);
#else
		UNEXPECTED
#endif
		(void)ret;
	}
#else
	UNEXPECTED
#endif

#if defined(PPGETPHASE)
	{
		int ret, phase;

		ret = ioctl(fd, PPGETPHASE, &phase);
#if defined(PPSETPHASE)
		errno = 0;
		if (ret == 0)
			ret = ioctl(fd, PPSETPHASE, &phase);
#else
		UNEXPECTED
#endif
		(void)ret;
	}
#else
	UNEXPECTED
#endif

#if defined(PPGETMODES)
	{
		int modes;

		VOID_RET(int, ioctl(fd, PPGETMODES, &modes));
	}
#else
	UNEXPECTED
#endif

#if defined(PPGETFLAGS)
	{
		int ret, uflags;

		ret = ioctl(fd, PPGETFLAGS, &uflags);
#if defined(PPSETFLAGS)
		errno = 0;
		if (ret == 0)
			ret = ioctl(fd, PPSETFLAGS, &uflags);
#else
		UNEXPECTED
#endif
		(void)ret;
	}
#else
	UNEXPECTED
#endif

#if defined(PPRSTATUS)
	{
		char reg;

		VOID_RET(int, ioctl(fd, PPRSTATUS, &reg));
	}
#else
	UNEXPECTED
#endif

#if defined(PPRCONTROL)
	{
		char reg;

		VOID_RET(int, ioctl(fd, PPRCONTROL, &reg));
	}
#else
	UNEXPECTED
#endif

#if defined(PPGETTIME32)
	{
		int32_t time32[2];

		VOID_RET(int, ioctl(fd, PPGETTIME32, time32));
	}
#else
	/* UNEXPECTED */
#endif

#if defined(PPGETTIME64)
	{
		int64_t time64[2];

		VOID_RET(int, ioctl(fd, PPGETTIME64, time64));
	}
#else
	/* UNEXPECTED */
#endif

#if defined(PPYIELD)
	{
		VOID_RET(int, ioctl(fd, PPYIELD));
	}
#else
	UNEXPECTED
#endif


#if defined(PPCLAIM) &&	\
    defined(PPRELEASE)
	if ((args->instance == 0) && claimed) {
		VOID_RET(int, ioctl(fd, PPRELEASE));
		VOID_RET(int, shim_pthread_spin_unlock(&parport_lock));
	}
#else
	UNEXPECTED
#endif
}
#else
UNEXPECTED
#endif

#if defined(__linux__)
/*
 *   stress_dev_bus_usb_linux()
 *   	Exercise Linux usb devices
 */
static void stress_dev_bus_usb_linux(
	const stress_args_t *args,
	const int fd,
	const char *devpath)
{
	(void)args;
	(void)fd;
	(void)devpath;

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
#else
		UNEXPECTED
#endif

#if defined(HAVE_USBDEVFS_GETDRIVER) &&	\
    defined(USBDEVFS_GETDRIVER)
		{
			struct usbdevfs_getdriver dr;

			VOID_RET(int, ioctl(fdwr, USBDEVFS_GETDRIVER, &dr));
		}
#else
		UNEXPECTED
#endif
		(void)close(fdwr);
	}
#else
	UNEXPECTED
#endif
}
#else
UNEXPECTED
#endif

#define DEV_FUNC(dev, func) \
	{ dev, sizeof(dev) - 1, func }

static const stress_dev_func_t dev_funcs[] = {
#if defined(__linux__) &&		\
    defined(HAVE_LINUX_MEDIA_H) &&	\
    defined(MEDIA_IOC_DEVICE_INFO)
	DEV_FUNC("/dev/media",	stress_dev_media_linux),
#else
	UNEXPECTED
#endif
#if defined(HAVE_LINUX_VT_H)
	DEV_FUNC("/dev/vcs",	stress_dev_vcs_linux),
#else
	UNEXPECTED
#endif
#if defined(HAVE_LINUX_DM_IOCTL_H)
	DEV_FUNC("/dev/dm",	stress_dev_dm_linux),
#else
	UNEXPECTED
#endif
#if defined(HAVE_LINUX_VIDEODEV2_H)
	DEV_FUNC("/dev/video",	stress_dev_video_linux),
#else
	UNEXPECTED
#endif
#if defined(HAVE_LINUX_RANDOM_H)
	DEV_FUNC("/dev/random",	stress_dev_random_linux),
#else
	UNEXPECTED
#endif
#if defined(__linux__)
	DEV_FUNC("/dev/mem",	stress_dev_mem_linux),
	DEV_FUNC("/dev/kmem",	stress_dev_kmem_linux),
	DEV_FUNC("/dev/kmsg",	stress_dev_kmsg_linux),
	DEV_FUNC("/dev/nvram",	stress_dev_nvram_linux),
	DEV_FUNC("/dev/cdrom",  stress_dev_cdrom_linux),
	DEV_FUNC("/dev/sr0",    stress_dev_cdrom_linux),
	DEV_FUNC("/dev/sg",	stress_dev_scsi_generic_linux),
	DEV_FUNC("/dev/console",stress_dev_console_linux),
#else
	UNEXPECTED
#endif
#if defined(__linux__) &&	\
    defined(STRESS_ARCH_X86)
	DEV_FUNC("/dev/port",	stress_dev_port_linux),
#else
	UNEXPECTED
#endif
#if defined(HAVE_LINUX_HPET_H)
	DEV_FUNC("/dev/hpet",	stress_dev_hpet_linux),
#else
	UNEXPECTED
#endif
	DEV_FUNC("/dev/null",	stress_dev_null_nop),
	DEV_FUNC("/dev/ptp",	stress_dev_ptp_linux),
	DEV_FUNC("/dev/snd/control",	stress_dev_snd_control_linux),
#if defined(HAVE_LINUX_FD_H) &&	\
    defined(HAVE_FLOPPY_STRUCT)
	DEV_FUNC("/dev/fd0",	stress_dev_floppy_linux),
#else
	UNEXPECTED
#endif
#if defined(__linux__)
	DEV_FUNC("/dev/hwrng",	stress_dev_hwrng_linux),
#else
	UNEXPECTED
#endif
#if defined(__linux__)
	DEV_FUNC("/dev/parport",stress_dev_parport_linux),
#else
	UNEXPECTED
#endif
#if defined(__linux__)
	DEV_FUNC("/dev/bus/usb",stress_dev_bus_usb_linux),
#else
	UNEXPECTED
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
#else
	UNEXPECTED
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
#else
	UNEXPECTED
#endif
	VOID_RET(int, ioctl(fd, TIOCNXCL));
#else
	(void)path;
	(void)fd;
#endif
}

static int stress_dev_open_lock(
	const stress_args_t *args,
	dev_info_t *dev_info,
	const int mode)
{
	int fd;

	fd = stress_open_timeout(args->name, dev_info->path, mode, 250000000);
	if (fd < 0) {
		if (errno == EBUSY)
			dev_info->state->open_failed = true;
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
	const stress_args_t *args,
	int32_t loops)
{
	int fd, ret;
	off_t offset;
	struct stat buf;
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

		ret = shim_pthread_spin_lock(&lock);
		if (ret)
			return;
		dev_info = pthread_dev_info;
		(void)shim_pthread_spin_unlock(&lock);

		if (!dev_info || !stress_continue_flag())
			break;

		/* state info no yet associated */
		if (UNLIKELY(!dev_info->state)) {
			shim_sched_yield();
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

		if (fstat(fd, &buf) < 0) {
			pr_fail("%s: stat failed on %s, errno=%d (%s)\n",
				args->name, path, errno, strerror(errno));
		} else {
			if ((S_ISBLK(buf.st_mode) | (S_ISCHR(buf.st_mode))) == 0) {
				stress_dev_close_unlock(path, fd);
				goto next;
			}
		}

		if (S_ISBLK(buf.st_mode)) {
			stress_dev_blk(args, fd, path);
			stress_dev_scsi_blk(args, fd, dev_info);
#if defined(HAVE_LINUX_HDREG_H)
			stress_dev_hd_linux(args, fd, path);
#endif
		}
#if defined(HAVE_TERMIOS_H) &&	\
    defined(HAVE_TERMIOS) &&	\
    defined(TCGETS)
		if (S_ISCHR(buf.st_mode) &&
		    strncmp("/dev/vsock", path, 10) &&
		    strncmp("/dev/dri", path, 8) &&
		    strncmp("/dev/nmem", path, 9) &&
		    strncmp("/dev/ndctl", path, 10) &&
		    (ioctl(fd, TCGETS, &tios) == 0))
			stress_dev_tty(args, fd, path);
#else
		UNEXPECTED
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
#else
		UNEXPECTED
#endif
#if defined(F_GETFL)
		VOID_RET(int, fcntl(fd, F_GETFL, NULL));

		if (stress_time_now() - t_start > threshold) {
			timeout = true;
			stress_dev_close_unlock(path, fd);
			goto next;
		}
#else
		UNEXPECTED
#endif
#if defined(F_GETSIG)
		VOID_RET(int, fcntl(fd, F_GETSIG, NULL));

		if (stress_time_now() - t_start > threshold) {
			timeout = true;
			stress_dev_close_unlock(path, fd);
			goto next;
		}
#else
		UNEXPECTED
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
	static void *nowt = NULL;
	const stress_pthread_args_t *pa = (stress_pthread_args_t *)arg;
	const stress_args_t *args = pa->args;

	/*
	 *  Block all signals, let controlling thread
	 *  handle these
	 */
	(void)sigprocmask(SIG_BLOCK, &set, NULL);

	while (stress_continue_flag())
		stress_dev_rw(args, -1);

	return &nowt;
}

/*
 *  stress_dev_files()
 *	stress all device files
 */
static void stress_dev_files(const stress_args_t *args, dev_info_t *dev_info_list)
{
	int32_t loops = args->instance < 8 ? (int32_t)args->instance + 1 : 8;
	static int try_failed = 0;
	dev_info_t *di;

	if (!stress_continue_flag())
		return;

	for (di = dev_info_list; di && stress_continue(args); di = di->next) {
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
			stress_dev_rw(args, loops);
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
 *  stress_dev_info_add()
 *	add new device path to device info list
 */
static dev_info_t *stress_dev_info_add(const char *path, dev_info_t **list)
{
	dev_info_t *new_dev;

	new_dev = calloc(1, sizeof(*new_dev));
	if (!new_dev)
		return NULL;

	new_dev->path = strdup(path);
	if (!new_dev->path) {
		free(new_dev);
		return NULL;
	}

	new_dev->name = stress_dev_basename(new_dev->path);
	new_dev->next = *list;
	new_dev->state = NULL;

	*list = new_dev;

	return new_dev;
}

/*
 *  stress_dev_infos_get()
 *	traverse device directories adding device info to list
 */
static size_t stress_dev_infos_get(
	const stress_args_t *args,
	const char *path,
	const char *tty_name,
	dev_info_t **list)
{
	struct dirent **dlist;
	int i, n;
	size_t total = 0;
	const mode_t flags = S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH;

	if (!stress_continue(args))
		return 0;

	n = scandir(path, &dlist, NULL, alphasort);
	if (n <= 0)
		return 0;

	for (i = 0; stress_continue(args) && (i < n); i++) {
		int ret;
		struct stat buf;
		char tmp[PATH_MAX];
		struct dirent *d = dlist[i];
		size_t len;

		if (!stress_continue(args))
			break;
		if (stress_is_dot_filename(d->d_name))
			continue;
		/*
		 *  Avoid https://bugs.xenserver.org/browse/XSO-809
		 *  see: LP#1741409, so avoid opening /dev/hpet
		 */
		if (!strcmp(d->d_name, "hpet") && linux_xen_guest())
			continue;
		if (!strncmp(d->d_name, "ttyS", 4))
			continue;

		len = strlen(d->d_name);

		/*
		 *  Exercise no more than 3 of the same device
		 *  driver, e.g. ttyS0..ttyS1
		 */
		if (len > 1) {
			int dev_n;
			char *ptr = d->d_name + len - 1;

			while ((ptr > d->d_name) && isdigit((int)*ptr))
				ptr--;
			ptr++;
			dev_n = atoi(ptr);
			if (dev_n > 2)
				continue;
		}

		(void)stress_mk_filename(tmp, sizeof(tmp), path, d->d_name);

		/* Don't exercise our tty */
		if (tty_name && !strcmp(tty_name, tmp))
			continue;

		switch (d->d_type) {
		case DT_DIR:
			ret = stat(tmp, &buf);
			if (ret < 0)
				continue;
			if ((buf.st_mode & flags) == 0)
				continue;
			total += stress_dev_infos_get(args, tmp, tty_name, list);
			break;
		case DT_BLK:
		case DT_CHR:
			if (strstr(tmp, "watchdog"))
				continue;
			stress_dev_info_add(tmp, list);
			total++;
			break;
		default:
			break;
		}
	}
	stress_dirent_list_free(dlist, n);

	return total;
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
 *  stress_dev
 *	stress reading all of /dev
 */
static int stress_dev(const stress_args_t *args)
{
	pthread_t pthreads[STRESS_DEV_THREADS_MAX];
	int ret[STRESS_DEV_THREADS_MAX], rc = EXIT_SUCCESS;
	stress_pthread_args_t pa;
	char *dev_file = NULL;
	const int stdout_fd = fileno(stdout);
	const char *tty_name = (stdout_fd >= 0) ? ttyname(stdout_fd) : NULL;
	dev_state_t dev_state_null, *mmap_dev_states;
	dev_info_t dev_null = { "/dev/null", "null", &dev_state_null, NULL };
	size_t n_devs = 0, mmap_dev_states_size;
	const size_t page_size = args->page_size;
	dev_info_t *dev_info_list = NULL;

	stress_dev_state_init(&dev_state_null);

	pthread_dev_info = &dev_null;
	pa.args = args;
	pa.data = NULL;

	(void)shim_memset(ret, 0, sizeof(ret));

	(void)stress_get_setting("dev-file", &dev_file);
	if (dev_file) {
		mode_t mode;
		struct stat statbuf;

		if (stat(dev_file, &statbuf) < 0) {
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

		if (stress_dev_info_add(dev_file, &dev_info_list))
			n_devs = 1;
	} else {
		n_devs = stress_dev_infos_get(args, "/dev", tty_name, &dev_info_list);
	}

	/* This should be rare */
	if (n_devs == 0) {
		pr_inf_skip("%s: cannot allocate device information or find any "
			"testable devices, skipping stressor\n", args->name);
		return EXIT_NO_RESOURCE;
	}

	mmap_dev_states_size = sizeof(*mmap_dev_states) * n_devs;
	mmap_dev_states_size = (mmap_dev_states_size + page_size - 1) & ~(page_size - 1);
	mmap_dev_states = mmap(NULL, mmap_dev_states_size, PROT_READ | PROT_WRITE,
			MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (mmap_dev_states == MAP_FAILED) {
		pr_inf_skip("%s: cannot allocate shared memory for device state data, "
			"errno=%d (%s), slipping stressor\n",
			args->name, errno, strerror(errno));
		rc = EXIT_NO_RESOURCE;
		goto deinit;
	}

	stress_dev_info_list_state_init(dev_info_list, mmap_dev_states);

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		pid_t pid;

again:
		pid = fork();
		if (pid < 0) {
			if (stress_redo_fork(args, errno))
				goto again;
		} else if (pid > 0) {
			int status, wret;

			/* Parent, wait for child */
			wret = waitpid(pid, &status, 0);
			if (wret < 0) {
				if (errno != EINTR)
					pr_dbg("%s: waitpid(): errno=%d (%s)\n",
						args->name, errno, strerror(errno));
				/* Ring ring, time to die */
				(void)shim_kill(pid, SIGALRM);
				VOID_RET(int, shim_waitpid(pid, &status, 0));
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
				stress_dev_files(args, dev_info_list);
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

	if (args->instance == 0) {
		const size_t opened = stress_dev_infos_opened(dev_info_list);

		pr_inf("%s: %zd of %zd devices opened and exercised\n",
			args->name, opened, n_devs);
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

	stress_dev_infos_free(&dev_info_list);

	return rc;
}
stressor_info_t stress_dev_info = {
	.stressor = stress_dev,
	.class = CLASS_DEV | CLASS_OS,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
#else
stressor_info_t stress_dev_info = {
	.stressor = stress_unimplemented,
	.class = CLASS_DEV | CLASS_OS,
	.opt_set_funcs = opt_set_funcs,
	.help = help,
	.unimplemented_reason = "built without pthread support or poll.h"
};
#endif
