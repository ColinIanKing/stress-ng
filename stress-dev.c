/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
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

static const stress_help_t help[] = {
	{ NULL,	"dev N",	"start N device entry thrashing stressors" },
	{ NULL,	"dev-ops N",	"stop after N device thrashing bogo ops" },
	{ NULL, "dev-file name","specify the /dev/ file to exercise" },
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
#define STRESS_DEV_HASH_SIZE		(113)

typedef struct stress_dev_func {
	const char *devpath;
	const size_t devpath_len;
	void (*func)(const stress_args_t *args, const int fd, const char *devpath);
} stress_dev_func_t;

typedef struct stress_dev_hash_info {
	char    *dev_path;			/* Device driver path */
	struct stress_dev_hash_info *next;	/* Next one in hash list */
	bool	open_fail;			/* True if failed to open */
	bool	open_ok;			/* True if open OK */
	bool	is_scsi;			/* True if SCSI device */
} stress_dev_hash_info_t;

static sigset_t set;
static shim_pthread_spinlock_t lock;
static shim_pthread_spinlock_t parport_lock;
static stress_dev_hash_info_t *stress_dev_hash_info;
static stress_dev_hash_info_t *stress_dev_hash[STRESS_DEV_HASH_SIZE];

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
 *  stress_dev_new()
 *	add a new device path to hash
 */
static stress_dev_hash_info_t *stress_dev_new(const char *dev_path)
{
	stress_dev_hash_info_t *dev_hash_info;
	const uint32_t hash = stress_hash_pjw(dev_path) % STRESS_DEV_HASH_SIZE;
	dev_hash_info = stress_dev_hash[hash];

	while (dev_hash_info) {
		if (!strcmp(dev_path, dev_hash_info->dev_path))
			return dev_hash_info;
		dev_hash_info = dev_hash_info->next;
	}

	dev_hash_info = calloc(1, sizeof(*dev_hash_info));
	dev_hash_info->dev_path = strdup(dev_path);
	if (!dev_hash_info->dev_path) {
		free(dev_hash_info);
		return NULL;
	}
	dev_hash_info->open_fail = false;
	dev_hash_info->open_ok = false;
	dev_hash_info->is_scsi = false;

	dev_hash_info->next = stress_dev_hash[hash];
	stress_dev_hash[hash] = dev_hash_info;

	return dev_hash_info;
}

/*
 *  stress_free()
 *	free tree
 */
static void stress_dev_free(void)
{
	size_t i;

	for (i = 0; i < STRESS_DEV_HASH_SIZE; i++) {
		stress_dev_hash_info_t *dev_hash_info = stress_dev_hash[i];

		while (dev_hash_info) {
			stress_dev_hash_info_t *next = dev_hash_info->next;

			free(dev_hash_info->dev_path);
			free(dev_hash_info);
			dev_hash_info = next;
		}
	}
}

/*
 *  stress_dev_dir()
 *	read dev directory, add device information to tree
 */
static void stress_dev_dir(
	const stress_args_t *args,
	const char *path,
	const int depth,
	const char *tty_name)
{
	struct dirent **dlist;
	const mode_t flags = S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH;
	int i, n;

	if (!keep_stressing_flag())
		return;

	/* Don't want to go too deep */
	if (depth > 20)
		return;

	dlist = NULL;
	n = scandir(path, &dlist, NULL, alphasort);
	if (n <= 0)
		goto done;

	for (i = 0; i < n; i++) {
		int ret;
		struct stat buf;
		char tmp[PATH_MAX];
		struct dirent *d = dlist[i];
		size_t len;

		if (!keep_stressing(args))
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

			while (ptr > d->d_name && isdigit((int)*ptr))
				ptr--;
			ptr++;
			dev_n = atoi(ptr);
			if (dev_n > 2)
				continue;
		}

		(void)stress_mk_filename(tmp, sizeof(tmp), path, d->d_name);
		/*
		 *  Avoid any actions on owner's tty
		 */
		if (tty_name && !strcmp(tty_name, tmp))
			continue;

		(void)stress_mk_filename(tmp, sizeof(tmp), path, d->d_name);
		switch (d->d_type) {
		case DT_DIR:
			ret = stat(tmp, &buf);
			if (ret < 0)
				continue;
			if ((buf.st_mode & flags) == 0)
				continue;
			stress_dev_dir(args, tmp, depth + 1, tty_name);
			break;
		case DT_BLK:
		case DT_CHR:
			if (strstr(tmp, "watchdog"))
				continue;
			stress_dev_new(tmp);
			break;
		default:
			break;
		}
	}
done:
	stress_dirent_list_free(dlist, n);
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
		int ret;
		time_t tsecs = (time_t)secs;

		it.it_interval.tv_sec = (time_t)secs;
		it.it_interval.tv_usec = (suseconds_t)(1000000.0 * (secs - (double)tsecs));
		it.it_value.tv_sec = it.it_interval.tv_sec;
		it.it_value.tv_usec = it.it_interval.tv_usec;
		ret = setitimer(ITIMER_REAL, &it, NULL);
		(void)ret;
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
	int ret;

	(void)memset(&it, 0, sizeof(it));
	ret = setitimer(ITIMER_REAL, &it, NULL);
	(void)ret;
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
		int ret;

		ret = ioctl(fd, VT_GETMODE, &mode);
		(void)ret;
	}
#endif
#if defined(VT_GETSTATE) &&	\
    defined(HAVE_VT_STAT)
	{
		struct vt_stat vt_stat;
		int ret;

		ret = ioctl(fd, VT_GETSTATE, &vt_stat);
		(void)ret;
	}
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
		int ret;

		ret = ioctl(fd, DM_VERSION, &dm);
		(void)ret;
	}
#endif
#if defined(DM_STATUS) &&	\
    defined(HAVE_DM_IOCTL)
	{
		struct dm_ioctl dm;
		int ret;

		ret = ioctl(fd, DM_STATUS, &dm);
		(void)ret;
	}
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
		int ret;

		(void)memset(&c, 0, sizeof(c));
		ret = ioctl(fd, VIDIOC_QUERYCAP, &c);
		(void)ret;
	}
#endif
#if defined(VIDIOC_G_FBUF) &&	\
    defined(HAVE_V4L2_FRAMEBUFFER)
	{
		struct v4l2_framebuffer f;
		int ret;

		(void)memset(&f, 0, sizeof(f));
		ret = ioctl(fd, VIDIOC_G_FBUF, &f);
		(void)ret;
	}
#endif
#if defined(VIDIOC_G_STD) &&	\
    defined(HAVE_V4L2_STD_ID)
	{
		v4l2_std_id id;
		int ret;

		(void)memset(&id, 0, sizeof(id));
		ret = ioctl(fd, VIDIOC_G_STD, &id);
		(void)ret;
	}
#endif
#if defined(VIDIOC_G_AUDIO) &&	\
    defined(HAVE_V4L2_AUDIO)
	{
		struct v4l2_audio a;
		int ret;

		(void)memset(&a, 0, sizeof(a));
		ret = ioctl(fd, VIDIOC_G_AUDIO, &a);
		(void)ret;
	}
#endif
#if defined(VIDIOC_G_INPUT)
	{
		int in = 0, ret;

		ret = ioctl(fd, VIDIOC_G_INPUT, &in);
		(void)ret;
	}
#endif
#if defined(VIDIOC_G_OUTPUT)
	{
		int in = 0, ret;

		ret = ioctl(fd, VIDIOC_G_OUTPUT, &in);
		(void)ret;
	}
#endif
#if defined(VIDIOC_G_AUDOUT) &&	\
    defined(HAVE_V4L2_AUDIOOUT)
	{
		struct v4l2_audioout a;
		int ret;

		(void)memset(&a, 0, sizeof(a));
		ret = ioctl(fd, VIDIOC_G_AUDOUT, &a);
		(void)ret;
	}
#endif
#if defined(VIDIOC_G_JPEGCOMP) && \
    defined(HAVE_V4L2_JPEGCOMPRESSION)
	{
		struct v4l2_jpegcompression a;
		int ret;

		(void)memset(&a, 0, sizeof(a));
		ret = ioctl(fd, VIDIOC_G_JPEGCOMP, &a);
		(void)ret;
	}
#endif
#if defined(VIDIOC_QUERYSTD) &&	\
    defined(HAVE_V4L2_STD_ID)
	{
		v4l2_std_id a;
		int ret;

		(void)memset(&a, 0, sizeof(a));
		ret = ioctl(fd, VIDIOC_QUERYSTD, &a);
		(void)ret;
	}
#endif
#if defined(VIDIOC_G_PRIORITY)
	{
		uint32_t a;
		int ret;

		ret = ioctl(fd, VIDIOC_G_PRIORITY, &a);
		(void)ret;
	}
#endif
#if defined(VIDIOC_G_ENC_INDEX) &&	\
    defined(HAVE_V4L2_ENC_IDX)
	{
		struct v4l2_enc_idx a;
		int ret;

		(void)memset(&a, 0, sizeof(a));
		ret = ioctl(fd, VIDIOC_G_ENC_INDEX, &a);
		(void)ret;
	}
#endif
#if defined(VIDIOC_QUERY_DV_TIMINGS) &&	\
    defined(HAVE_V4L2_DV_TIMINGS)
	{
		struct v4l2_dv_timings a;
		int ret;

		(void)memset(&a, 0, sizeof(a));
		ret = ioctl(fd, VIDIOC_QUERY_DV_TIMINGS, &a);
		(void)ret;
	}
#endif
}
#endif

#if defined(HAVE_TERMIOS_H) &&	\
    defined(TCGETS) &&		\
    defined(HAVE_TERMIOS)
static void stress_dev_tty(
	const stress_args_t *args,
	const int fd,
	const char *devpath)
{
	int ret;
	struct termios t;

	(void)args;
	(void)devpath;

	if (!isatty(fd))
		return;

	ret = tcgetattr(fd, &t);
	(void)ret;
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

		ret = ioctl(fd, TIOCGPTN, &ptnum);
		(void)ret;
	}
#endif
#if defined(TIOCSIG) &&	\
    defined(SIGCONT)
	{
		int sig = SIGCONT;

		/* generally causes EINVAL */
		ret = ioctl(fd, TIOCSIG, &sig);
		(void)ret;
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

		ret = ioctl(fd, FIONREAD, &n);
		(void)ret;
	}
#endif
#if defined(TIOCINQ)
	{
		int n;

		ret = ioctl(fd, TIOCINQ, &n);
		(void)ret;
	}
#endif
#if defined(TIOCOUTQ)
	{
		int n;

		ret = ioctl(fd, TIOCOUTQ, &n);
		(void)ret;
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

		ret = ioctl(fd, TIOCGSID, &gsid);
		(void)ret;
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
				ret = ioctl(fd, TIOCNXCL, NULL);
				(void)ret;
				ret = ioctl(fd, TIOCEXCL, NULL);
			} else {
				ret = ioctl(fd, TIOCEXCL, NULL);
				(void)ret;
				ret = ioctl(fd, TIOCNXCL, NULL);
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
#if defined(TIOCGETD) && 0
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
		ret = ioctl(fd, TIOCGPTPEER, O_RDWR);
		(void)ret;
	}
#endif

#if defined(TCOOFF) && 	\
    defined(TCOON)
	{
		ret = ioctl(fd, TCOOFF, 0);
		if (ret == 0)
			ret = ioctl(fd, TCOON, 0);
		(void)ret;
	}
#endif

#if defined(TCIOFF) &&	\
    defined(TCION)
	{
		ret = ioctl(fd, TCIOFF, 0);
		if (ret == 0)
			ret = ioctl(fd, TCION, 0);
		(void)ret;
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

		ret = ioctl(fd, KDGETLED, &state);
		(void)ret;
	}
#endif

#if defined(KDGKBTYPE)
	{
		char type;

		ret = ioctl(fd, KDGKBTYPE, &type);
		(void)ret;
	}
#endif

#if defined(KDGETMODE)
	{
		int mode;

		ret = ioctl(fd, KDGETMODE, &mode);
		(void)ret;
	}
#endif

#if defined(KDGKBMODE)
	{
		long mode;

		ret = ioctl(fd, KDGKBMODE, &mode);
		(void)ret;
	}
#endif

#if defined(KDGKBMETA)
	{
		long mode;

		ret = ioctl(fd, KDGKBMETA, &mode);
		(void)ret;
	}
#endif
#if defined(TIOCMGET)
	{
		int status;

		ret = ioctl(fd, TIOCMGET, &status);
#if defined(TIOCMSET)
		if (ret == 0) {
#if defined(TIOCMBIC)
			ret = ioctl(fd, TIOCMBIC, &status);
			(void)ret;
#endif
#if defined(TIOCMBIS)
			ret = ioctl(fd, TIOCMBIS, &status);
			(void)ret;
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

		ret = ioctl(fd, TIOCGICOUNT, &counter);
		(void)ret;
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
			ret = ioctl(fd, TCSETSF2, &t2);
			(void)ret;
#endif
#if defined(TCSETSW2)
			ret = ioctl(fd, TCSETSW2, &t2);
			(void)ret;
#endif
#if defined(TCSETS2)
			ret = ioctl(fd, TCSETS2, &t2);
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
		int ret;
		ret = ioctl(fd, BLKFLSBUF, 0);
		(void)ret;
	}
#endif
#if defined(BLKRAGET)
	/* readahead */
	{
		unsigned long ra;
		int ret;

		ret = ioctl(fd, BLKRAGET, &ra);
		(void)ret;
	}
#endif
#if defined(BLKROGET)
	/* readonly state */
	{
		int ret, ro;

		ret = ioctl(fd, BLKROGET, &ro);
		(void)ret;
	}
#endif
#if defined(BLKBSZGET)
	/* get block device soft block size */
	{
		int ret, sz;

		ret = ioctl(fd, BLKBSZGET, &sz);
		(void)ret;
	}
#endif
#if defined(BLKPBSZGET)
	/* get block device physical block size */
	{
		unsigned int sz;
		int ret;

		ret = ioctl(fd, BLKPBSZGET, &sz);
		(void)ret;
	}
#endif
#if defined(BLKIOMIN)
	{
		unsigned int sz;
		int ret;

		ret = ioctl(fd, BLKIOMIN, &sz);
		(void)ret;
	}
#endif
#if defined(BLKIOOPT)
	{
		unsigned int sz;
		int ret;

		ret = ioctl(fd, BLKIOOPT, &sz);
		(void)ret;
	}
#endif
#if defined(BLKALIGNOFF)
	{
		unsigned int sz;
		int ret;

		ret = ioctl(fd, BLKALIGNOFF, &sz);
		(void)ret;
	}
#endif
#if defined(BLKROTATIONAL)
	{
		unsigned short rotational;
		int ret;

		ret = ioctl(fd, BLKROTATIONAL, &rotational);
		(void)ret;
	}
#endif
#if defined(BLKSECTGET)
	{
		unsigned short max_sectors;
		int ret;

		ret = ioctl(fd, BLKSECTGET, &max_sectors);
		(void)ret;
	}
#endif
#if defined(BLKGETSIZE)
	{
		unsigned long sz;
		int ret;

		ret = ioctl(fd, BLKGETSIZE, &sz);
		(void)ret;
	}
#endif
#if defined(BLKGETSIZE64)
	{
		uint64_t sz;
		int ret;

		ret = ioctl(fd, BLKGETSIZE64, &sz);
		(void)ret;
	}
#endif
#if defined(BLKGETZONESZ)
	{
		uint32_t sz;
		int ret;

		ret = ioctl(fd, BLKGETZONESZ, &sz);
		(void)ret;
	}
#endif
#if defined(BLKGETNRZONES)
	{
		uint32_t sz;
		int ret;

		ret = ioctl(fd, BLKGETNRZONES, &sz);
		(void)ret;
	}
#endif
	offset = lseek(fd, 0, SEEK_END);
	stress_uint64_put((uint64_t)offset);

	offset = lseek(fd, 0, SEEK_SET);
	stress_uint64_put((uint64_t)offset);

	offset = lseek(fd, 0, SEEK_CUR);
	stress_uint64_put((uint64_t)offset);
}

#if defined(__linux__)
static inline const char *dev_basename(const char *devpath)
{
	const char *ptr = devpath;
	const char *base = devpath;

	while (*ptr) {
		if ((*ptr == '/') && (*(ptr + 1)))
			base = ptr + 1;
		ptr++;
	}

	return base;
}

static inline bool is_scsi_dev(stress_dev_hash_info_t *dev_hash_info)
{
	int i, n;
	static const char scsi_device_path[] = "/sys/class/scsi_device/";
	struct dirent **scsi_device_list;
	bool is_scsi = false;
	const char *devname = dev_basename(dev_hash_info->dev_path);

	if (!*devname)
		return false;

	if (dev_hash_info->is_scsi)
		return true;

	scsi_device_list = NULL;
	n = scandir(scsi_device_path, &scsi_device_list, NULL, alphasort);
	if (n <= 0)
		return is_scsi;

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
			if (!strcmp(devname, scsi_block_list[j]->d_name)) {
				is_scsi = true;
				break;
			}
		}

		stress_dirent_list_free(scsi_block_list, m);
	}

	stress_dirent_list_free(scsi_device_list, n);

	if (is_scsi)
		dev_hash_info->is_scsi = true;

	return is_scsi;
}

#else
static inline bool is_scsi_dev(stress_dev_hash_info_t *dev_hash_info)
{
	(void)dev_hash_info;

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
	stress_dev_hash_info_t *dev_hash_info)
{
	(void)args;
	(void)fd;

	if (!is_scsi_dev(dev_hash_info))
		return;

#if defined(SG_GET_VERSION_NUM)
	{
		int ret, ver;

		ret = ioctl(fd, SG_GET_VERSION_NUM, &ver);
		(void)ret;
	}
#endif
#if defined(SCSI_IOCTL_GET_IDLUN)
	{
		int ret;
		struct sng_scsi_idlun {
			int four_in_one;
			int host_unique_id;
		} lun;

		(void)memset(&lun, 0, sizeof(lun));
		ret = ioctl(fd, SCSI_IOCTL_GET_IDLUN, &lun);
		(void)ret;
	}
#endif
#if defined(SCSI_IOCTL_GET_BUS_NUMBER)
	{
		int ret, bus;

		ret = ioctl(fd, SCSI_IOCTL_GET_BUS_NUMBER, &bus);
		(void)ret;
	}
#endif
#if defined(SCSI_IOCTL_GET_TIMEOUT)
	{
		int ret;

		ret = ioctl(fd, SCSI_IOCTL_GET_TIMEOUT, 0);
		(void)ret;
	}
#endif
#if defined(SCSI_IOCTL_GET_RESERVED_SIZE)
	{
		int ret, sz;

		ret = ioctl(fd, SCSI_IOCTL_GET_RESERVED_SIZE, &sz);
		(void)ret;
	}
#endif
#if defined(SCSI_IOCTL_GET_PCI)
	{
		/* Old ioctl was 20 chars, new API 8 chars, 32 is plenty */
		char pci[32];

		ret = ioctl(fd, SCSI_IOCTL_GET_PCI, pci);
		(void)ret;
	}
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
		int ret, version = 0;

		ret = ioctl(fd, SG_GET_VERSION_NUM, &version);
		(void)ret;
	}
#endif
#if defined(SG_GET_TIMEOUT)
	{
		int ret;

		ret = ioctl(fd, SG_GET_TIMEOUT, 0);
		(void)ret;
	}
#endif
#if defined(SG_GET_LOW_DMA)
	{
		int ret, low;

		ret = ioctl(fd, SG_GET_LOW_DMA, &low);
		(void)ret;
	}
#endif
#if defined(SG_GET_PACK_ID)
	{
		int ret, pack_id;

		ret = ioctl(fd, SG_GET_PACK_ID, &pack_id);
		(void)ret;
	}
#endif
#if defined(SG_GET_NUM_WAITING)
	{
		int ret, n;

		ret = ioctl(fd, SG_GET_NUM_WAITING, &n);
		(void)ret;
	}
#endif
#if defined(SG_GET_SG_TABLESIZE)
	{
		int ret, size;

		ret = ioctl(fd, SG_GET_SG_TABLESIZE, &size);
		(void)ret;
	}
#endif
#if defined(SG_GET_RESERVED_SIZE)
	{
		int ret, size;

		ret = ioctl(fd, SG_GET_RESERVED_SIZE, &size);
		(void)ret;
	}
#endif
#if defined(SG_GET_COMMAND_Q)
	{
		int ret, cmd_q;

		ret = ioctl(fd, SG_GET_COMMAND_Q, &cmd_q);
		(void)ret;
	}
#endif
#if defined(SG_GET_ACCESS_COUNT)
	{
		int ret, n;

		ret = ioctl(fd, SG_GET_ACCESS_COUNT, &n);
		(void)ret;
	}
#endif
#if defined(SCSI_IOCTL_GET_IDLUN)
	{
		int ret;
		struct shim_scsi_idlun {
			int four_in_one;
			int host_unique_id;
		} idlun;

		ret = ioctl(fd, SCSI_IOCTL_GET_IDLUN, &idlun);
		(void)ret;
	}
#endif
#if defined(SCSI_IOCTL_GET_BUS_NUMBER)
	{
		int ret, bus;

		ret = ioctl(fd, SCSI_IOCTL_GET_BUS_NUMBER, &bus);
		(void)ret;
	}
#endif
#if defined(SCSI_GET_TRANSFORM)
	{
		int ret, bus;

		ret = ioctl(fd, SCSI_GET_TRANSFORM, 0);
		(void)ret;
	}
#endif
#if defined(SG_EMULATED_HOST)
	{
		int ret, emulated;

		ret = ioctl(fd, SG_EMULATED_HOST, &emulated);
		(void)ret;
	}
#endif
#if defined(BLKSECTGET)
	{
		int ret, n;

		ret = ioctl(fd, BLKSECTGET, &n);
		(void)ret;
	}
#endif
}
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
		int ret;

		ret = ioctl(fd, RNDGETENTCNT, &entropy);
		(void)ret;
	}
#endif

#if defined(RNDRESEEDCRNG)
	{
		int ret;

		ret = ioctl(fd, RNDRESEEDCRNG, NULL);
		(void)ret;
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
			int ret;
			const uint32_t rnd = stress_mwc32();
			struct shim_rand_pool_info {
				int	entropy_count;
				int	buf_size;
				uint8_t	buf[4];
			} info;
			const size_t sz = sizeof(info.buf);

			info.entropy_count = sz * 8;
			info.buf_size = sz;
			(void)memcpy(&info.buf, &rnd, sz);

			ret = ioctl(fd_rdwr, RNDADDENTROPY, &info);
			(void)ret;
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
static void stress_dev_mem_mmap_linux(const int fd, const bool read_page)
{
	void *ptr;
	const size_t page_size = stress_get_pagesize();

	ptr = mmap(NULL, page_size, PROT_READ, MAP_PRIVATE, fd, 0);
	if (ptr != MAP_FAILED) {
		(void)munmap(ptr, page_size);
	}
	if (read_page) {
		off_t off;

		/* Try seeking */
		off = lseek(fd, (off_t)0, SEEK_SET);
#if defined(STRESS_ARCH_X86)
		if (off == 0) {
			char buffer[page_size];
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
	if (ptr != MAP_FAILED) {
		(void)munmap(ptr, page_size);
	}

}

static void stress_dev_mem_linux(
	const stress_args_t *args,
	const int fd,
	const char *devpath)
{
	(void)args;
	(void)devpath;

	stress_dev_mem_mmap_linux(fd, true);
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

	stress_dev_mem_mmap_linux(fd, false);
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
	int track,
	uint8_t *min,
	uint8_t *seconds,
	uint8_t *frames)
{
	struct cdrom_tocentry entry;

	(void)memset(&entry, 0, sizeof(entry));
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

		(void)memset(&header, 0, sizeof(header));
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
		int ret;

		(void)memset(&ti, 0, sizeof(ti));
		ti.cdti_trk1 = endtrk;
		if (ioctl(fd, CDROMPLAYTRKIND, &ti) == 0) {
			ret = ioctl(fd, CDROMPAUSE, 0);
			(void)ret;
		}
	}, return);
#endif

#if defined(CDROMREADTOCENTRY) &&	\
    defined(HAVE_CDROM_MSF) &&		\
    defined(HAVE_CDROM_TOCENTRY)
	{
		struct cdrom_msf msf;
		int ret;

		/* Fetch address of start and end track in MSF format */
		(void)memset(&msf, 0, sizeof(msf));
		cdrom_get_address_msf(fd, starttrk, &msf.cdmsf_min0,
			&msf.cdmsf_sec0, &msf.cdmsf_frame0);
		cdrom_get_address_msf(fd, endtrk, &msf.cdmsf_min1,
			&msf.cdmsf_sec1, &msf.cdmsf_frame1);

#if defined(CDROMPLAYMSF) && 	\
    defined(CDROMPAUSE)
		IOCTL_TIMEOUT(0.10, {
			if (ioctl(fd, CDROMPLAYMSF, &msf) == 0) {
				ret = ioctl(fd, CDROMPAUSE, 0);
				(void)ret;
			}
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
			ret = ioctl(fd, CDROMREADRAW, &arg);
			(void)ret;
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
			ret = ioctl(fd, CDROMREADMODE1, &arg);
			(void)ret;
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
			ret = ioctl(fd, CDROMREADMODE2, &arg);
			(void)ret;
		}, return);
#endif
	}
#endif
}
#endif

#if defined(__linux__)
static void stress_dev_cdrom_linux(
	const stress_args_t *args,
	const int fd,
	const char *devpath)
{
	(void)args;
	(void)fd;
	(void)devpath;

	stress_cdrom_ioctl_msf(fd);

#if defined(CDROM_GET_MCN) &&	\
    defined(HAVE_CDROM_MCN)
	IOCTL_TIMEOUT(0.10, {
		struct cdrom_mcn mcn;
		int ret;

		(void)memset(&mcn, 0, sizeof(mcn));
		ret = ioctl(fd, CDROM_GET_MCN, &mcn);
		(void)ret;
	}, return);
#endif
#if defined(CDROMREADTOCHDR) &&		\
    defined(HAVE_CDROM_TOCHDR)
	IOCTL_TIMEOUT(0.10, {
		struct cdrom_tochdr header;
		int ret;

		(void)memset(&header, 0, sizeof(header));
		ret = ioctl(fd, CDROMREADTOCHDR, &header);
		(void)ret;
	}, return);
#endif
#if defined(CDROMREADTOCENTRY) &&	\
    defined(HAVE_CDROM_TOCENTRY)
	IOCTL_TIMEOUT(0.10, {
		struct cdrom_tocentry entry;
		int ret;

		(void)memset(&entry, 0, sizeof(entry));
		ret = ioctl(fd, CDROMREADTOCENTRY, &entry);
		(void)ret;
	}, return);
#endif
#if defined(CDROMVOLREAD) &&		\
    defined(CDROMVOLCTRL) &&		\
    defined(HAVE_CDROM_VOLCTRL)
	IOCTL_TIMEOUT(0.10, {
		struct cdrom_volctrl volume;
		int ret;

		(void)memset(&volume, 0, sizeof(volume));
		ret = ioctl(fd, CDROMVOLREAD, &volume);
		if (ret == 0)
			ret = ioctl(fd, CDROMVOLCTRL, &volume);
		(void)ret;
	}, return);
#endif
#if defined(CDROMSUBCHNL) &&	\
    defined(HAVE_CDROM_SUBCHNL)
	IOCTL_TIMEOUT(0.10, {
		struct cdrom_subchnl q;
		int ret;

		(void)memset(&q, 0, sizeof(q));
		ret = ioctl(fd, CDROMSUBCHNL, &q);
		(void)ret;
	}, return);
#endif
#if defined(CDROMREADAUDIO) &&	\
    defined(HAVE_CDROM_READ_AUDIO)
	IOCTL_TIMEOUT(0.10, {
		struct cdrom_read_audio ra;
		int ret;

		(void)memset(&ra, 0, sizeof(ra));
		ret = ioctl(fd, CDROMREADAUDIO, &ra);
		(void)ret;
	}, return);
#endif
#if defined(CDROMREADCOOKED) &&	\
    defined(CD_FRAMESIZE)
	IOCTL_TIMEOUT(0.10, {
		uint8_t buffer[CD_FRAMESIZE];
		int ret;

		(void)memset(&buffer, 0, sizeof(buffer));
		ret = ioctl(fd, CDROMREADCOOKED, buffer);
		(void)ret;
	}, return);
#endif
#if defined(CDROMREADALL) &&	\
    defined(CD_FRAMESIZE)
	IOCTL_TIMEOUT(0.10, {
		uint8_t buffer[CD_FRAMESIZE];
		int ret;

		(void)memset(&buffer, 0, sizeof(buffer));
		ret = ioctl(fd, CDROMREADALL, buffer);
		(void)ret;
	}, return);
#endif
#if defined(CDROMSEEK) &&	\
    defined(HAVE_CDROM_MSF)
	IOCTL_TIMEOUT(0.10, {
		struct cdrom_msf msf;
		int ret;

		(void)memset(&msf, 0, sizeof(msf));
		ret = ioctl(fd, CDROMSEEK, &msf);
		(void)ret;
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
	IOCTL_TIMEOUT(0.10, {
		int ret;

		ret = ioctl(fd, CDROM_DISC_STATUS, 0);
		(void)ret;
	}, return);
#endif
#if defined(CDROM_GET_CAPABILITY)
	IOCTL_TIMEOUT(0.10, {
		int ret;

		ret = ioctl(fd, CDROM_GET_CAPABILITY, 0);
		(void)ret;
	}, return);
#endif
#if defined(CDROM_CHANGER_NSLOTS)
	IOCTL_TIMEOUT(0.10, {
		int ret;

		ret = ioctl(fd, CDROM_CHANGER_NSLOTS, 0);
		(void)ret;
	}, return);
#endif
#if defined(CDROM_NEXT_WRITABLE)
	IOCTL_TIMEOUT(0.10, {
		int ret;
		long next;

		ret = ioctl(fd, CDROM_NEXT_WRITABLE, &next);
		(void)ret;
	}, return);
#endif
#if defined(CDROM_LAST_WRITTEN)
	IOCTL_TIMEOUT(0.10, {
		int ret;
		long last;

		ret = ioctl(fd, CDROM_LAST_WRITTEN, &last);
		(void)ret;
	}, return);
#endif
#if defined(CDROM_MEDIA_CHANGED) && 0
	IOCTL_TIMEOUT(0.10, {
		int ret;
		int slot = 0;

		ret = ioctl(fd, CDROM_MEDIA_CHANGED, slot);
		(void)ret;
	}, return);
#endif
#if defined(CDSL_NONE)
	IOCTL_TIMEOUT(0.10, {
		int ret;
		int slot = CDSL_NONE;

		ret = ioctl(fd, CDROM_MEDIA_CHANGED, slot);
		(void)ret;
	}, return);
#endif
#if defined(CDSL_CURRENT)
	IOCTL_TIMEOUT(0.10, {
		int ret;
		int slot = CDSL_CURRENT;

		ret = ioctl(fd, CDROM_MEDIA_CHANGED, slot);
		(void)ret;
	}, return);
#endif
#if defined(CDROMPAUSE)
	IOCTL_TIMEOUT(0.10, {
		int ret;

		ret = ioctl(fd, CDROMPAUSE, 0);
		(void)ret;
	}, return);
#endif
#if defined(CDROMRESUME)
	IOCTL_TIMEOUT(0.10, {
		int ret;

		ret = ioctl(fd, CDROMRESUME, 0);
		(void)ret;
	}, return);
#endif
#if defined(CDROM_DRIVE_STATUS)
	IOCTL_TIMEOUT(0.10, {
		int ret;
		int slot = 0;

		ret = ioctl(fd, CDROM_DRIVE_STATUS, slot);
		(void)ret;
	}, return);
#if defined(CDSL_NONE)
	IOCTL_TIMEOUT(0.10, {
		int ret;
		int slot = CDSL_NONE;

		ret = ioctl(fd, CDROM_DRIVE_STATUS, slot);
		(void)ret;
	}, return);
#endif
#if defined(CDSL_CURRENT)
	IOCTL_TIMEOUT(0.10, {
		int ret;
		int slot = CDSL_CURRENT;

		ret = ioctl(fd, CDROM_DRIVE_STATUS, slot);
		(void)ret;
	}, return);
#endif
#endif
#if defined(DVD_READ_STRUCT) &&	\
    defined(HAVE_DVD_STRUCT)
	IOCTL_TIMEOUT(0.10, {
		dvd_struct s;
		int ret;

		/*
		 *  Invalid DVD_READ_STRUCT ioctl syscall with
		 *  invalid layer number resulting in EINVAL
		 */
		(void)memset(&s, 0, sizeof(s));
		s.type = DVD_STRUCT_PHYSICAL;
		s.physical.layer_num = UINT8_MAX;
		ret = ioctl(fd, DVD_READ_STRUCT, &s);
		(void)ret;

		/*
		 *  Exercise each DVD structure type to cover all the
		 *  respective functions to increase kernel coverage
		 */
		(void)memset(&s, 0, sizeof(s));
		s.type = DVD_STRUCT_PHYSICAL;
		ret = ioctl(fd, DVD_READ_STRUCT, &s);
		(void)ret;

		(void)memset(&s, 0, sizeof(s));
		s.type = DVD_STRUCT_COPYRIGHT;
		ret = ioctl(fd, DVD_READ_STRUCT, &s);
		(void)ret;

		(void)memset(&s, 0, sizeof(s));
		s.type = DVD_STRUCT_DISCKEY;
		ret = ioctl(fd, DVD_READ_STRUCT, &s);
		(void)ret;

		(void)memset(&s, 0, sizeof(s));
		s.type = DVD_STRUCT_BCA;
		ret = ioctl(fd, DVD_READ_STRUCT, &s);
		(void)ret;

		(void)memset(&s, 0, sizeof(s));
		s.type = DVD_STRUCT_MANUFACT;
		ret = ioctl(fd, DVD_READ_STRUCT, &s);
		(void)ret;

		/* Invalid DVD_READ_STRUCT call with invalid type argument */
		(void)memset(&s, 0, sizeof(s));
		s.type = UINT8_MAX;
		ret = ioctl(fd, DVD_READ_STRUCT, &s);
		(void)ret;
	}, return);
#endif
#if defined(CDROMAUDIOBUFSIZ)
	IOCTL_TIMEOUT(0.10, {
		int val = INT_MIN;
		int ret;

		/* Invalid CDROMAUDIOBUFSIZ call with negative buffer size */
		ret = ioctl(fd, CDROMAUDIOBUFSIZ, val);
		(void)ret;
	}, return);
#endif
#if defined(DVD_AUTH) &&	\
    defined(HAVE_DVD_AUTHINFO)
	IOCTL_TIMEOUT(0.40, {
		int ret;
		dvd_authinfo ai;

		/* Invalid DVD_AUTH call with no credentials */
		(void)memset(&ai, 0, sizeof(ai));
		ret = ioctl(fd, DVD_AUTH, &ai);
		(void)ret;

		/*
		 *  Exercise each DVD AUTH type to cover all the
		 *  respective code to increase kernel coverage
		 */
		(void)memset(&ai, 0, sizeof(ai));
		ai.type = DVD_LU_SEND_AGID;
		ret = ioctl(fd, DVD_AUTH, &ai);
		(void)ret;

		(void)memset(&ai, 0, sizeof(ai));
		ai.type = DVD_LU_SEND_KEY1;
		ret = ioctl(fd, DVD_AUTH, &ai);
		(void)ret;

		(void)memset(&ai, 0, sizeof(ai));
		ai.type = DVD_LU_SEND_CHALLENGE;
		ret = ioctl(fd, DVD_AUTH, &ai);
		(void)ret;

		(void)memset(&ai, 0, sizeof(ai));
		ai.type = DVD_LU_SEND_TITLE_KEY;
		ret = ioctl(fd, DVD_AUTH, &ai);
		(void)ret;

		(void)memset(&ai, 0, sizeof(ai));
		ai.type = DVD_LU_SEND_ASF;
		ret = ioctl(fd, DVD_AUTH, &ai);
		(void)ret;

		(void)memset(&ai, 0, sizeof(ai));
		ai.type = DVD_HOST_SEND_CHALLENGE;
		ret = ioctl(fd, DVD_AUTH, &ai);
		(void)ret;

		(void)memset(&ai, 0, sizeof(ai));
		ai.type = DVD_HOST_SEND_KEY2;
		ret = ioctl(fd, DVD_AUTH, &ai);
		(void)ret;

		(void)memset(&ai, 0, sizeof(ai));
		ai.type = DVD_INVALIDATE_AGID;
		ret = ioctl(fd, DVD_AUTH, &ai);
		(void)ret;

		(void)memset(&ai, 0, sizeof(ai));
		ai.type = DVD_LU_SEND_RPC_STATE;
		ret = ioctl(fd, DVD_AUTH, &ai);
		(void)ret;

		(void)memset(&ai, 0, sizeof(ai));
		ai.type = DVD_HOST_SEND_RPC_STATE;
		ret = ioctl(fd, DVD_AUTH, &ai);
		(void)ret;

		/* Invalid DVD_READ_STRUCT call with invalid type argument */
		(void)memset(&ai, 0, sizeof(ai));
		ai.type = (uint8_t)~0;
		ret = ioctl(fd, DVD_AUTH, &ai);
		(void)ret;
	}, return);
#endif

#if defined(CDROM_DEBUG)
	IOCTL_TIMEOUT(0.10, {
		int debug;
		int ret;

		/* Enable the DEBUG Messages */
		debug = 1;
		ret = ioctl(fd, CDROM_DEBUG, debug);
		(void)ret;

		/* Disable the DEBUG Messages */
		debug = 0;
		ret = ioctl(fd, CDROM_DEBUG, debug);
		(void)ret;
	}, return);
#endif

#if defined(CDROM_SELECT_SPEED)
	IOCTL_TIMEOUT(0.10, {
		unsigned int i;
		unsigned int speed;
		int ret;

		for (i = 8; i < 16; i++) {
			speed = 1 << i;
			ret = ioctl(fd, CDROM_SELECT_SPEED, speed);
			(void)ret;
		}
	}, return);
#endif

#if defined(CDROMPLAYBLK) &&	\
    defined(HAVE_CDROM_BLK)
	IOCTL_TIMEOUT(0.10, {
		struct cdrom_blk blk;
		int ret;

		(void)memset(&blk, 0, sizeof(blk));
		ret = ioctl(fd, CDROMPLAYBLK, &blk);
		(void)ret;
	}, return);
#endif
}
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

			ret = ioctl(fd, KDSETLED, &argp);
			(void)ret;

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
			unsigned long bad_val = ~0UL, val;

			val = (unsigned long)argp;
			ret = ioctl(fd, KDSKBLED, val);
			(void)ret;

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
		unsigned long argp = 0;

		ret = ioctl(fd, KDGETMODE, &argp);
#if defined(KDSETMODE)
		if (ret == 0) {
			unsigned long bad_val = ~0UL;

			ret = ioctl(fd, KDSETMODE, argp);
			(void)ret;

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
		int val = 0, ret;

		ret = ioctl(fd, KDGKBTYPE, &val);
		(void)ret;
	}
#endif

#if defined(HAVE_LINUX_KD_H) && \
    defined(GIO_CMAP)
	{
		unsigned char colormap[3*16];
		int ret;

		ret = ioctl(fd, GIO_CMAP, colormap);
#if defined(PIO_CMAP)
		if (ret == 0) {
			ret = ioctl(fd, PIO_CMAP, colormap);
		}
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

		(void)memset(&font, 0, sizeof(font));
		ret = ioctl(fd, GIO_FONTX, &font);
#if defined(PIO_FONTX)
		if (ret == 0) {
			ret = ioctl(fd, PIO_FONTX, &font);
		}
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

		(void)memset(&argp, 0, sizeof(argp));
		ret = ioctl(fd, KDGETKEYCODE, &argp);
#if defined(KDSETKEYCODE)
		if (ret == 0) {
			struct kbkeycode bad_arg;

			ret = ioctl(fd, KDSETKEYCODE, &argp);
			(void)ret;

			/*
			 * Exercise Invalid KDSETKEYCODE ioctl call
			 * with invalid keycode having different values
			 * of scancode and keycode for scancode < 89
			 */
			(void)memset(&bad_arg, 0, sizeof(bad_arg));
			bad_arg.scancode = 1;
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
		if (ret == 0) {
			ret = ioctl(fd, PIO_FONT, argp);
		}
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
		if (ret == 0) {
			ret = ioctl(fd, PIO_SCRNMAP, argp);
		}
#endif
		(void)ret;
	}
#endif

#if defined(HAVE_LINUX_KD_H) && \
    defined(GIO_UNISCRNMAP) && \
    defined(E_TABSZ)
	{
		unsigned short argp[E_TABSZ];
		int ret;

		ret = ioctl(fd, GIO_UNISCRNMAP, argp);
#if defined(PIO_UNISCRNMAP)
		if (ret == 0) {
			ret = ioctl(fd, PIO_UNISCRNMAP, argp);
		}
#endif
		(void)ret;
	}
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

			ret = ioctl(fd, KDSKBMODE, argp);
			(void)ret;

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
		unsigned long argp = 0;

		ret = ioctl(fd, KDGKBMETA, &argp);
#if defined(KDSKBMETA)
		if (ret == 0) {
			unsigned long bad_val = ~0UL;

			ret = ioctl(fd, KDSKBMETA, argp);
			(void)ret;

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

		(void)memset(&argp, 0, sizeof(argp));
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
		int ret;
		struct kbdiacrs argp;

		(void)memset(&argp, 0, sizeof(argp));
		ret = ioctl(fd, KDGKBDIACR, &argp);
		(void)ret;
	}
#endif

#if defined(HAVE_LINUX_KD_H) && \
    defined(VT_RESIZE) &&	\
    defined(HAVE_VT_SIZES) && \
    defined(CAP_SYS_TTY_CONFIG)
	{
		int ret;
		struct vt_sizes argp;
		bool perm = stress_check_capability(CAP_SYS_TTY_CONFIG);

		/* Exercise only if permission is not present */
		if (!perm) {
			(void)memset(&argp, 0, sizeof(argp));
			ret = ioctl(fd, VT_RESIZE, &argp);
			(void)ret;
		}
	}
#endif

#if defined(HAVE_LINUX_KD_H) && \
    defined(VT_RESIZEX) &&	\
    defined(HAVE_VT_CONSIZE) && \
    defined(CAP_SYS_TTY_CONFIG)
	{
		int ret;
		struct vt_consize argp;
		bool perm = stress_check_capability(CAP_SYS_TTY_CONFIG);

		/* Exercise only if permission is not present */
		if (!perm) {
			(void)memset(&argp, 0, sizeof(argp));
			ret = ioctl(fd, VT_RESIZEX, &argp);
			(void)ret;
		}
	}
#endif

#if defined(HAVE_LINUX_KD_H) && \
    defined(KDGKBSENT) &&	\
    defined(HAVE_KBSENTRY)
	{
		int ret;
		struct kbsentry argp;

		(void)memset(&argp, 0, sizeof(argp));
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

		(void)memset(&mode, 0, sizeof(mode));
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

		(void)memset(&entry, 0, sizeof(entry));
		ret = ioctl(fd, KDGKBENT, &entry);
#if defined(KDSKBENT)
		if (ret == 0) {
			ret = ioctl(fd, KDSKBENT, &entry);
			(void)ret;

#if defined(MAX_NR_KEYMAPS)
			struct kbentry bad_entry;

			(void)memset(&bad_entry, 0, sizeof(bad_entry));
			bad_entry.kb_table = MAX_NR_KEYMAPS;
			ret = ioctl(fd, KDSKBENT, &bad_entry);
			if (ret == 0) {
				/* Unexpected success, so set it back */
				ret = ioctl(fd, KDSKBENT, &entry);
			}
#endif

#if defined(HAVE_LINUX_KD_H) &&	\
    defined(KDSKBENT) &&	\
    defined(HAVE_KBENTRY) &&	\
    defined(NR_KEYS)
			struct kbentry bad_entry;

			(void)memset(&entry, 0, sizeof(entry));
			bad_entry.kb_index = NR_KEYS;
			ret = ioctl(fd, KDSKBENT, &bad_entry);
			if (ret == 0) {
				/* Unexpected success, so set it back */
				ret = ioctl(fd, KDSKBENT, &entry);
			}
#endif
		}
#endif
		(void)ret;
	}
#endif

}
#endif

#if defined(__linux__)
static void stress_dev_kmsg_linux(
	const stress_args_t *args,
	const int fd,
	const char *devpath)
{
	(void)args;
	(void)devpath;

	stress_dev_mem_mmap_linux(fd, true);
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

	stress_dev_mem_mmap_linux(fd, true);
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
		int ret;

		ret = ioctl(fd, HPET_INFO, &info);
		(void)ret;
	}
#endif
#if defined(HPET_IRQFREQ)
	{
		unsigned long freq;
		int ret;

		ret = ioctl(fd, HPET_IRQFREQ, &freq);
		(void)ret;
	}
#endif
#if defined(CDROMMULTISESSION)
	{
		int ret;
		struct cdrom_multisession ms_info;

		/*
		 *  Invalid CDROMMULTISESSION ioctl syscall with
		 *  invalid format number resulting in EINVAL
		 */
		(void)memset(&ms_info, 0, sizeof(ms_info));
		ms_info.addr_format = UINT8_MAX;
		ret = ioctl(fd, CDROMMULTISESSION, &ms_info);
		(void)ret;

		/* Valid CDROMMULTISESSION with address formats */
		(void)memset(&ms_info, 0, sizeof(ms_info));
		ms_info.addr_format = CDROM_MSF;
		ret = ioctl(fd, CDROMMULTISESSION, &ms_info);
		(void)ret;

		(void)memset(&ms_info, 0, sizeof(ms_info));
		ms_info.addr_format = CDROM_LBA;
		ret = ioctl(fd, CDROMMULTISESSION, &ms_info);
		(void)ret;
	}
#endif
}
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
	const size_t page_size = stress_get_pagesize();

	(void)args;
	(void)devpath;

	/* seek and read port 0x80 */
	off = lseek(fd, (off_t)0x80, SEEK_SET);
	if (off == 0) {
		char data[1];
		ssize_t ret;

		ret = read(fd, data, sizeof(data));
		(void)ret;
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
	int ret;

	ret = ioctl(fd, cmd, &val);
	(void)ret;
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
		int ret;

		ret = ioctl(fd, HDIO_GETGEO, &geom);
		(void)ret;
	}
#endif

#if defined(HDIO_GET_UNMASKINTR)
	stress_dev_hd_linux_ioctl_long(fd, HDIO_GET_UNMASKINTR);
#endif

#if defined(HDIO_GET_MULTCOUNT)
	{
		int val, ret;

		ret = ioctl(fd, HDIO_GET_MULTCOUNT, &val);
		(void)ret;
	}
#endif

#if defined(HDIO_GET_IDENTITY)
	{
		unsigned char identity[512];
		int ret;

		ret = ioctl(fd, HDIO_GET_IDENTITY, identity);
		(void)ret;
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

			(void)memset(&desc, 0, sizeof(desc));
			desc.index = (unsigned int)i;
			ret = ioctl(fd, PTP_PIN_GETFUNC, &desc);
			(void)ret;
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
 *  stress_dev_fd_linux()
 *	minor exercising of the floppy device
 */
static void stress_dev_fd_linux(
	const stress_args_t *args,
	const int fd,
	const char *devpath)
{
	(void)args;
	(void)fd;
	(void)devpath;

#if defined(FDMSGON)
	{
		int ret;

		ret = ioctl(fd, FDMSGON, 0);
		(void)ret;
	}
#endif

#if defined(FDFLUSH)
	{
		int ret;

		ret = ioctl(fd, FDFLUSH, 0);
		(void)ret;
	}
#endif

#if defined(FDGETPRM) &&	\
    defined(HAVE_FLOPPY_STRUCT)
	{
		int ret;
		struct floppy_struct floppy;

		ret = ioctl(fd, FDGETPRM, &floppy);
		if (ret == 0) {
			ret = ioctl(fd, FDSETPRM, &floppy);
		}
		(void)ret;
	}
#endif

#if defined(FDGETDRVSTAT) &&	\
    defined(HAVE_FLOPPY_DRIVE_STRUCT)
	{
		int ret;
		struct floppy_drive_struct drive;

		ret = ioctl(fd, FDGETDRVSTAT, &drive);
		(void)ret;
	}
#endif

#if defined(FDPOLLDRVSTAT) &&	\
    defined(HAVE_FLOPPY_DRIVE_STRUCT)
	{
		int ret;
		struct floppy_drive_struct drive;

		ret = ioctl(fd, FDPOLLDRVSTAT, &drive);
		(void)ret;
	}
#endif

#if defined(FDGETDRVTYP)
	{
		int ret;
		char buf[64];

		ret = ioctl(fd, FDGETDRVTYP, buf);
		(void)ret;
	}
#endif

#if defined(FDGETFDCSTAT) &&		\
    defined(HAVE_FLOPPY_FDC_STATE)
	{
		int ret;
		struct floppy_fdc_state state;

		ret = ioctl(fd, FDGETFDCSTAT, &state);
		(void)ret;

	}
#endif

#if defined(FDMSGOFF)
	{
		int ret;

		ret = ioctl(fd, FDMSGOFF, 0);
		(void)ret;
	}
#endif
}
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
		int ret, ver;

		ret = ioctl(fd, SNDRV_CTL_IOCTL_PVERSION, &ver);
		(void)ret;
	}
#endif

#if defined(SNDRV_CTL_IOCTL_CARD_INFO) &&	\
    defined(HAVE_SND_CTL_CARD_INFO)
	{
		int ret;
		struct snd_ctl_card_info card;

		ret = ioctl(fd, SNDRV_CTL_IOCTL_CARD_INFO, &card);
		(void)ret;
	}
#endif

#if defined(SNDRV_CTL_IOCTL_TLV_READ) &&	\
    defined(HAVE_SND_CTL_TLV)
	{
		int ret;
		struct tlv_buf {
			struct snd_ctl_tlv tlv;
			unsigned int data[4];
		} buf;

		/* intentionally will fail with -EINVAL */
		buf.tlv.numid = 0;
		buf.tlv.length = sizeof(buf.data);
		ret = ioctl(fd, SNDRV_CTL_IOCTL_TLV_READ, (struct snd_ctl_tlv *)&buf.tlv);
		(void)ret;

		/* intentionally will fail with -ENOENT */
		buf.tlv.numid = ~0U;
		buf.tlv.length = sizeof(buf.data);
		ret = ioctl(fd, SNDRV_CTL_IOCTL_TLV_READ, (struct snd_ctl_tlv *)&buf.tlv);
		(void)ret;
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
	ssize_t ret;
	off_t offset = (off_t)0;
	char buffer[8];

	(void)args;
	(void)devpath;

	offset = lseek(fd, offset, SEEK_SET);
	(void)offset;

	ret = read(fd, buffer, sizeof(buffer));
	(void)ret;
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
		int ret, modes;

		ret = ioctl(fd, PPGETMODES, &modes);
		(void)ret;
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
		int ret;
		char reg;

		ret = ioctl(fd, PPRSTATUS, &reg);
		(void)ret;
	}
#endif

#if defined(PPRCONTROL)
	{
		int ret;
		char reg;

		ret = ioctl(fd, PPRCONTROL, &reg);
		(void)ret;
	}
#endif

#if defined(PPGETTIME32)
	{
		int ret;
		int32_t time32[2];

		ret = ioctl(fd, PPGETTIME32, time32);
		(void)ret;
	}
#endif

#if defined(PPGETTIME64)
	{
		int ret;
		int64_t time64[2];

		ret = ioctl(fd, PPGETTIME64, time64);
		(void)ret;
	}
#endif

#if defined(PPYIELD)
	{
		int ret;

		ret = ioctl(fd, PPYIELD);
		(void)ret;
	}
#endif


#if defined(PPCLAIM) &&	\
    defined(PPRELEASE)
	if ((args->instance == 0) && claimed) {
		int ret;

		ret = ioctl(fd, PPRELEASE);
		(void)ret;
		ret = shim_pthread_spin_unlock(&parport_lock);
		(void)ret;
	}
#endif
}
#endif


#define DEV_FUNC(dev, func) \
	{ dev, sizeof(dev) - 1, func }

static const stress_dev_func_t dev_funcs[] = {
#if defined(__linux__) &&		\
    defined(HAVE_LINUX_MEDIA_H) &&	\
    defined(MEDIA_IOC_DEVICE_INFO)
	DEV_FUNC("/dev/media",	stress_dev_media_linux),
#endif
#if defined(HAVE_LINUX_VT_H)
	DEV_FUNC("/dev/vcs",	stress_dev_vcs_linux),
#endif
#if defined(HAVE_LINUX_DM_IOCTL_H)
	DEV_FUNC("/dev/dm",	stress_dev_dm_linux),
#endif
#if defined(HAVE_LINUX_VIDEODEV2_H)
	DEV_FUNC("/dev/video",	stress_dev_video_linux),
#endif
#if defined(HAVE_LINUX_RANDOM_H)
	DEV_FUNC("/dev/random",	stress_dev_random_linux),
#endif
#if defined(__linux__)
	DEV_FUNC("/dev/mem",	stress_dev_mem_linux),
	DEV_FUNC("/dev/kmem",	stress_dev_kmem_linux),
	DEV_FUNC("/dev/kmsg",	stress_dev_kmsg_linux),
	DEV_FUNC("/dev/nvram",	stress_dev_nvram_linux),
	DEV_FUNC("/dev/cdrom",  stress_dev_cdrom_linux),
	DEV_FUNC("/dev/sg",	stress_dev_scsi_generic_linux),
	DEV_FUNC("/dev/sr0",    stress_dev_cdrom_linux),
	DEV_FUNC("/dev/console",stress_dev_console_linux),
#endif
#if defined(__linux__) &&	\
    defined(STRESS_ARCH_X86)
	DEV_FUNC("/dev/port",	stress_dev_port_linux),
#endif
#if defined(HAVE_LINUX_HPET_H)
	DEV_FUNC("/dev/hpet",	stress_dev_hpet_linux),
#endif
	DEV_FUNC("/dev/null",	stress_dev_null_nop),
	DEV_FUNC("/dev/ptp",	stress_dev_ptp_linux),
	DEV_FUNC("/dev/snd/control",	stress_dev_snd_control_linux),
#if defined(HAVE_LINUX_FD_H) &&	\
    defined(HAVE_FLOPPY_STRUCT)
	DEV_FUNC("/dev/fd",	stress_dev_fd_linux),
#endif
#if defined(__linux__)
	DEV_FUNC("/dev/hwrng",	stress_dev_hwrng_linux),
#endif
#if defined(__linux__)
	DEV_FUNC("/dev/parport",stress_dev_parport_linux),
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
		int ret;

		ret = flock(fd, LOCK_EX | LOCK_NB);
		if (ret < 0) {
			ret = ioctl(fd, TIOCNXCL);
			(void)ret;
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
	int ret;

	if (strncmp(path, "/dev/tty", 8))
		return;

#if defined(LOCK_EX) &&	\
    defined(LOCK_NB) &&	\
    defined(LOCK_UN)
	ret = flock(fd, LOCK_UN);
	(void)ret;
#endif
	ret = ioctl(fd, TIOCNXCL);
	(void)ret;
#else
	(void)path;
	(void)fd;
#endif
}

static int stress_dev_open_lock(
	const stress_args_t *args,
	stress_dev_hash_info_t *dev_hash_info,
	const int mode)
{
	int fd, ret;

	fd = stress_open_timeout(args->name, dev_hash_info->dev_path, mode, 250000000);
	if (fd < 0) {
		if (errno == EBUSY)
			dev_hash_info->open_fail = true;
		return -1;
	}
	ret = stress_dev_lock(dev_hash_info->dev_path, fd);
	if (ret < 0) {
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
		stress_dev_hash_info_t *dev_hash_info;
		char *path;

		ret = shim_pthread_spin_lock(&lock);
		if (ret)
			return;
		dev_hash_info = stress_dev_hash_info;
		(void)shim_pthread_spin_unlock(&lock);

		if (!dev_hash_info || !keep_stressing_flag())
			break;

		path = dev_hash_info->dev_path;

		if (dev_hash_info->open_fail)
			goto next;

		t_start = stress_time_now();

		fd = stress_dev_open_lock(args, dev_hash_info, O_RDONLY | O_NONBLOCK | O_NDELAY);
		if (fd < 0)
			goto next;

		if (stress_time_now() - t_start > threshold) {
			timeout = true;
			stress_dev_close_unlock(path, fd);
			goto next;
		}

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
			stress_dev_scsi_blk(args, fd, dev_hash_info);
#if defined(HAVE_LINUX_HDREG_H)
			stress_dev_hd_linux(args, fd, path);
#endif
		}
#if defined(HAVE_TERMIOS_H) &&	\
    defined(TCGETS) && \
    defined(HAVE_TERMIOS)
		if (S_ISCHR(buf.st_mode) &&
		    strncmp("/dev/vsock", path, 10) &&
		    strncmp("/dev/dri", path, 8) &&
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
		ret = poll(fds, 1, 0);
		(void)ret;

		if (stress_time_now() - t_start > threshold) {
			timeout = true;
			stress_dev_close_unlock(path, fd);
			goto next;
		}

#if !defined(__NetBSD__)
		{
			struct timeval tv;
			fd_set wfds;

			FD_ZERO(&rfds);
			FD_SET(fd, &rfds);
			FD_ZERO(&wfds);
			FD_SET(fd, &wfds);
			tv.tv_sec = 0;
			tv.tv_usec = 10000;
			ret = select(fd + 1, &rfds, &wfds, NULL, &tv);
			(void)ret;

			if (stress_time_now() - t_start > threshold) {
				timeout = true;
				stress_dev_close_unlock(path, fd);
				goto next;
			}
		}
#endif

#if defined(F_GETFD)
		ret = fcntl(fd, F_GETFD, NULL);
		(void)ret;

		if (stress_time_now() - t_start > threshold) {
			timeout = true;
			stress_dev_close_unlock(path, fd);
			goto next;
		}
#endif
#if defined(F_GETFL)
		ret = fcntl(fd, F_GETFL, NULL);
		(void)ret;

		if (stress_time_now() - t_start > threshold) {
			timeout = true;
			stress_dev_close_unlock(path, fd);
			goto next;
		}
#endif
#if defined(F_GETSIG)
		ret = fcntl(fd, F_GETSIG, NULL);
		(void)ret;

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

		fd = stress_dev_open_lock(args, dev_hash_info, O_RDONLY | O_NONBLOCK | O_NDELAY);
		if (fd < 0)
			goto next;

		ptr = mmap(NULL, args->page_size, PROT_WRITE, MAP_PRIVATE, fd, 0);
		if (ptr != MAP_FAILED)
			(void)munmap(ptr, args->page_size);
		ptr = mmap(NULL, args->page_size, PROT_WRITE, MAP_SHARED, fd, 0);
		if (ptr != MAP_FAILED)
			(void)munmap(ptr, args->page_size);

		ret = shim_fsync(fd);
		(void)ret;

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
		fd = stress_dev_open_lock(args, dev_hash_info, O_RDONLY | O_WRONLY);
		if (fd < 0) {
			if (errno == EINTR)
				dev_hash_info->open_fail = true;
		} else {
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

	while (keep_stressing_flag())
		stress_dev_rw(args, -1);

	return &nowt;
}

/*
 *  stress_dev_file()
 *	stress a specific device file
 */
static void stress_dev_file(const stress_args_t *args, char *path)
{
	int ret;
	int32_t loops = args->instance < 8 ? (int32_t)args->instance + 1 : 8;
	stress_dev_hash_info_t dev_hash_info;

	dev_hash_info.dev_path = path;

	ret = shim_pthread_spin_lock(&lock);
	if (!ret) {
		stress_dev_hash_info = &dev_hash_info;
		(void)shim_pthread_spin_unlock(&lock);
		stress_dev_rw(args, loops);
		inc_counter(args);
	}
}

/*
 *  stress_dev_dir()
 *	stress all device files
 */
static void stress_dev_files(const stress_args_t *args)
{
	int32_t loops = args->instance < 8 ? (int32_t)args->instance + 1 : 8;
	static int try_failed;
	size_t i;

	if (!keep_stressing_flag())
		return;

	for (i = 0; keep_stressing(args) && (i < STRESS_DEV_HASH_SIZE); i++) {
		stress_dev_hash_info_t *dev_hash_info;

		for (dev_hash_info = stress_dev_hash[i];
		     keep_stressing(args) && dev_hash_info;
		     dev_hash_info = dev_hash_info->next) {
			int ret;

			if (dev_hash_info->open_fail)
				continue;
			/* Limit the number of locked up try failures */
			if (try_failed > STRESS_DEV_OPEN_TRIES_MAX)
				continue;
			stress_dev_procname(dev_hash_info->dev_path);

			/* If it was opened OK before, no need for try_open check */
			if (!dev_hash_info->open_ok) {
				ret = stress_try_open(args, dev_hash_info->dev_path, O_RDONLY | O_NONBLOCK | O_NDELAY, 1500000000);
				if (ret == STRESS_TRY_OPEN_FAIL) {
					dev_hash_info->open_fail = true;
					try_failed++;
					continue;
				}
				if (ret == STRESS_TRY_AGAIN)
					continue;
			}
			ret = shim_pthread_spin_lock(&lock);
			if (!ret) {
				stress_dev_hash_info = dev_hash_info;
				(void)shim_pthread_spin_unlock(&lock);
				stress_dev_rw(args, loops);
				inc_counter(args);
			}
			dev_hash_info->open_ok = true;
		}
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
	stress_dev_hash_info_t dev_null = { "/dev/null", NULL, false, false, false };

	stress_dev_hash_info = &dev_null;
	pa.args = args;
	pa.data = NULL;

	(void)memset(ret, 0, sizeof(ret));

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
	}

	if (!dev_file)
		stress_dev_dir(args, "/dev", 0, tty_name);

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		pid_t pid;

again:
		pid = fork();
		if (pid < 0) {
			if (stress_redo_fork(errno))
				goto again;
		} else if (pid > 0) {
			int status, wret;

			(void)setpgid(pid, g_pgrp);
			/* Parent, wait for child */
			wret = waitpid(pid, &status, 0);
			if (wret < 0) {
				if (errno != EINTR)
					pr_dbg("%s: waitpid(): errno=%d (%s)\n",
						args->name, errno, strerror(errno));
				/* Ring ring, time to die */
				(void)kill(pid, SIGALRM);
				wret = shim_waitpid(pid, &status, 0);
				(void)wret;
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

			(void)setpgid(0, g_pgrp);
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
			stress_set_oom_adjustment(args->name, true);

			for (i = 0; i < STRESS_DEV_THREADS_MAX; i++) {
				ret[i] = pthread_create(&pthreads[i], NULL,
						stress_dev_thread, (void *)&pa);
			}

			do {
				if (dev_file)
					stress_dev_file(args, dev_file);
				else
					stress_dev_files(args);
			} while (keep_stressing(args));

			r = shim_pthread_spin_lock(&lock);
			if (r) {
				pr_dbg("%s: failed to lock spin lock for dev_path\n", args->name);
			} else {
				stress_dev_hash_info = NULL;
				r = shim_pthread_spin_unlock(&lock);
				(void)r;
			}

			for (i = 0; i < STRESS_DEV_THREADS_MAX; i++) {
				if (ret[i] == 0)
					(void)pthread_join(pthreads[i], NULL);
			}
			_exit(EXIT_SUCCESS);
		}
	} while (keep_stressing(args));

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
	(void)shim_pthread_spin_destroy(&lock);

	/*
	 *  Ensure we don't get build warnings if these are not
	 *  referenced.
	 */
	(void)ioctl_set_timeout;
	(void)ioctl_clr_timeout;

	if (!dev_file)
		stress_dev_free();

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
	.stressor = stress_not_implemented,
	.class = CLASS_DEV | CLASS_OS,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
#endif
